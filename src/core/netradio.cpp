// Gunsmith3D - internet radio (see netradio.hpp).
//
// How a stream is fetched:
//   Windows: plain http:// over a socket (SHOUTcast servers answer "ICY 200 OK", which WinHTTP refuses), https:// with
//            WinHTTP (the system's own TLS, proxies and certificates).
//   elsewhere (the developer's Linux build): the system's curl, except http:// to this machine over a socket (tests).
// Then: redirects and .pls/.m3u playlists are followed, ICY song titles are taken out of the stream, dr_mp3 decodes,
// and the sound is resampled to 48 kHz stereo into a few seconds' buffer that read() drains.
#include "netradio.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include "json.hpp"

#define DRMP3_API static
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../../third_party/raylib/src/external/dr_mp3.h"   // (our own private copy: raylib's is built into raylib)
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
using sock_t = SOCKET;
static const sock_t BAD_SOCK = INVALID_SOCKET;
static void closeSock(sock_t s) { closesocket(s); }
#else
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
using sock_t = int;
static const sock_t BAD_SOCK = -1;
static void closeSock(sock_t s) { close(s); }
#endif

namespace netradio {

static const char* AGENT = "Gunsmith3D";

// ---------------------------------------------------------------- small text helpers
static std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
struct Url { std::string scheme, host, path = "/"; int port = 80; bool ok = false; };
static Url parseUrl(const std::string& u) {
    Url r;
    size_t s = u.find("://");
    if (s == std::string::npos) return r;
    r.scheme = lower(u.substr(0, s));
    if (r.scheme != "http" && r.scheme != "https") return r;
    r.port = r.scheme == "https" ? 443 : 80;
    std::string rest = u.substr(s + 3);
    size_t sl = rest.find('/');
    std::string hp = sl == std::string::npos ? rest : rest.substr(0, sl);
    r.path = sl == std::string::npos ? "/" : rest.substr(sl);
    size_t at = hp.rfind('@');
    if (at != std::string::npos) hp = hp.substr(at + 1);
    size_t co = hp.rfind(':');
    if (co != std::string::npos && hp.find(']') == std::string::npos) { r.port = std::atoi(hp.c_str() + co + 1); hp = hp.substr(0, co); }
    r.host = hp;
    r.ok = !r.host.empty() && r.port > 0 && r.port < 65536;
    return r;
}
std::string icyTitle(const std::string& meta) {
    size_t a = meta.find("StreamTitle='");
    if (a == std::string::npos) return "";
    a += 13;
    size_t b = meta.find("';", a);
    if (b == std::string::npos) b = meta.rfind('\'');
    if (b == std::string::npos || b < a) return "";
    return trim(meta.substr(a, b - a));
}
std::string playlistUrl(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        std::string line = trim(text.substr(i, e == std::string::npos ? std::string::npos : e - i));
        i = e == std::string::npos ? text.size() : e + 1;
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        std::string l = lower(line);
        if (l.rfind("file", 0) == 0) { size_t eq = line.find('='); if (eq != std::string::npos) line = trim(line.substr(eq + 1)); l = lower(line); }
        if (l.rfind("http://", 0) == 0 || l.rfind("https://", 0) == 0) return line;
    }
    return "";
}

// ---------------------------------------------------------------- connections
// read(): > 0 bytes, 0 the end, -1 an error, -2 nothing yet (try again: lets the caller notice it should stop)
struct Conn {
    virtual ~Conn() = default;
    virtual long read(char* b, size_t n) = 0;
    int status = 0;
    std::map<std::string, std::string> head;   // lower-case names
    std::string pending;                        // body bytes that came in with the headers
    long take(char* b, size_t n) {              // the pending bytes first
        size_t k = std::min(n, pending.size());
        std::memcpy(b, pending.data(), k);
        pending.erase(0, k);
        return (long)k;
    }
    std::string header(const char* k) const { auto it = head.find(k); return it == head.end() ? "" : it->second; }
};
// One header block ("HTTP/1.1 200 OK" or "ICY 200 OK", then name: value lines). Returns false if it isn't one.
static bool parseHead(const std::string& block, int& status, std::map<std::string, std::string>& head) {
    head.clear();
    size_t e = block.find('\n');
    std::string first = trim(block.substr(0, e));
    size_t sp = first.find(' ');
    if (sp == std::string::npos) return false;
    std::string proto = first.substr(0, sp);
    if (proto.rfind("HTTP/", 0) != 0 && proto != "ICY") return false;
    status = std::atoi(first.c_str() + sp + 1);
    size_t i = e == std::string::npos ? block.size() : e + 1;
    while (i < block.size()) {
        size_t f = block.find('\n', i);
        std::string line = block.substr(i, f == std::string::npos ? std::string::npos : f - i);
        i = f == std::string::npos ? block.size() : f + 1;
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        head[lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
    }
    return status > 0;
}
// Reads header blocks off a byte source until a final one (skipping a proxy's "Connection Established" and, for curl
// following redirects, the 3xx blocks before the last). What follows the last block stays in `pending`.
template <class ReadFn>
static bool readHeaders(Conn& c, ReadFn rd, const std::atomic<bool>& stop, bool skipRedirects, std::string& err) {
    std::string buf;
    auto t0 = std::chrono::steady_clock::now();
    while (!stop) {
        size_t end = buf.find("\r\n\r\n");
        size_t endLen = 4;
        if (end == std::string::npos) { end = buf.find("\n\n"); endLen = 2; }
        if (end != std::string::npos) {
            std::string block = buf.substr(0, end);
            buf.erase(0, end + endLen);
            if (!parseHead(block, c.status, c.head)) { err = "The station's answer wasn't understood."; return false; }
            bool tunnel = c.status == 200 && lower(block).find("connection established") != std::string::npos;
            bool more = tunnel || (c.status >= 100 && c.status < 200) || (skipRedirects && c.status >= 300 && c.status < 400);
            if (more) continue;
            c.pending = buf;
            return true;
        }
        if (buf.size() > 65536) { err = "The station's answer wasn't understood."; return false; }
        char tmp[4096];
        long n = rd(tmp, sizeof tmp);
        if (n > 0) buf.append(tmp, (size_t)n);
        else if (n == 0 || n == -1) { err = buf.empty() ? "Couldn't reach the station." : "The station's answer wasn't understood."; return false; }
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(15)) { err = "The station didn't answer."; return false; }
    }
    err = "Stopped.";
    return false;
}

// a plain socket (http://)
struct SockConn : Conn {
    sock_t s = BAD_SOCK;
    ~SockConn() override { if (s != BAD_SOCK) closeSock(s); }
    long raw(char* b, size_t n) {
#ifndef _WIN32
        pollfd p{s, POLLIN, 0};
        int r = poll(&p, 1, 250);
        if (r == 0) return -2;
        if (r < 0) return -1;
#endif
        long k = (long)recv(s, b, (int)n, 0);
        if (k < 0) {
#ifdef _WIN32
            return WSAGetLastError() == WSAETIMEDOUT ? -2 : -1;
#else
            return -1;
#endif
        }
        return k;
    }
    long read(char* b, size_t n) override { return pending.empty() ? raw(b, n) : take(b, n); }
};
static std::unique_ptr<Conn> openSocket(const Url& u, bool icy, const std::atomic<bool>& stop, std::string& err) {
#ifdef _WIN32
    static bool wsa = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
    if (!wsa) { err = "Networking isn't available."; return nullptr; }
#endif
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(u.host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res) { err = "Couldn't find " + u.host + "."; return nullptr; }
    auto c = std::make_unique<SockConn>();
    for (addrinfo* a = res; a && c->s == BAD_SOCK; a = a->ai_next) {
        sock_t s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == BAD_SOCK) continue;
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) != 0) { closeSock(s); continue; }
        c->s = s;
    }
    freeaddrinfo(res);
    if (c->s == BAD_SOCK) { err = "Couldn't connect to " + u.host + "."; return nullptr; }
#ifdef _WIN32
    DWORD ms = 250;
    setsockopt(c->s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof ms);
#endif
    std::string req = "GET " + u.path + " HTTP/1.0\r\nHost: " + u.host + (u.port != 80 ? ":" + std::to_string(u.port) : "") +
                      "\r\nUser-Agent: " + AGENT + "\r\nAccept: */*\r\n" + (icy ? "Icy-MetaData: 1\r\n" : "") + "Connection: close\r\n\r\n";
    if (send(c->s, req.data(), (int)req.size(), 0) != (int)req.size()) { err = "Couldn't talk to the station."; return nullptr; }
    SockConn* sc = c.get();
    if (!readHeaders(*c, [&](char* b, size_t n) { return sc->raw(b, n); }, stop, false, err)) return nullptr;
    return c;
}

#ifdef _WIN32
// https:// through WinHTTP
struct WinConn : Conn {
    HINTERNET ses = nullptr, con = nullptr, req = nullptr;
    ~WinConn() override { if (req) WinHttpCloseHandle(req); if (con) WinHttpCloseHandle(con); if (ses) WinHttpCloseHandle(ses); }
    long read(char* b, size_t n) override {
        if (!pending.empty()) return take(b, n);
        DWORD got = 0;
        if (!WinHttpReadData(req, b, (DWORD)n, &got)) return GetLastError() == ERROR_WINHTTP_TIMEOUT ? -2 : -1;
        return (long)got;
    }
};
static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
static std::unique_ptr<Conn> openWinHttp(const Url& u, bool icy, std::string& err) {
    auto c = std::make_unique<WinConn>();
    c->ses = WinHttpOpen(L"Gunsmith3D", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!c->ses) { err = "Networking isn't available."; return nullptr; }
    WinHttpSetTimeouts(c->ses, 10000, 10000, 10000, 15000);
    c->con = WinHttpConnect(c->ses, widen(u.host).c_str(), (INTERNET_PORT)u.port, 0);
    c->req = c->con ? WinHttpOpenRequest(c->con, L"GET", widen(u.path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.scheme == "https" ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (!c->req) { err = "Couldn't connect to " + u.host + "."; return nullptr; }
    std::wstring hdr = icy ? L"Icy-MetaData: 1\r\n" : L"";
    if (!WinHttpSendRequest(c->req, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), hdr.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(c->req, nullptr)) { err = "Couldn't reach the station."; return nullptr; }
    DWORD code = 0, sz = sizeof code;
    WinHttpQueryHeaders(c->req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
    c->status = (int)code;
    DWORD len = 0;
    WinHttpQueryHeaders(c->req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &len, WINHTTP_NO_HEADER_INDEX);
    if (len > 0) {
        std::wstring w(len / sizeof(wchar_t) + 1, L'\0');
        if (WinHttpQueryHeaders(c->req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, &w[0], &len, WINHTTP_NO_HEADER_INDEX)) {
            int st = 0;
            parseHead(narrow(w.c_str()), st, c->head);
        }
    }
    return c;
}
#else
// curl as a child process (the developer's build): reads its output through a pipe
struct CurlConn : Conn {
    pid_t pid = -1;
    int fd = -1;
    ~CurlConn() override {
        if (fd >= 0) close(fd);
        if (pid > 0) { kill(pid, SIGTERM); waitpid(pid, nullptr, 0); }
    }
    long raw(char* b, size_t n) {
        pollfd p{fd, POLLIN, 0};
        int r = poll(&p, 1, 250);
        if (r == 0) return -2;
        if (r < 0) return -1;
        ssize_t k = ::read(fd, b, n);
        return k < 0 ? -1 : (long)k;
    }
    long read(char* b, size_t n) override { return pending.empty() ? raw(b, n) : take(b, n); }
};
static std::unique_ptr<Conn> openCurl(const std::string& url, bool icy, const std::atomic<bool>& stop, std::string& err) {
    int p[2];
    if (pipe(p) != 0) { err = "Couldn't start the download."; return nullptr; }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], 1);
    posix_spawn_file_actions_addclose(&fa, p[0]);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    std::vector<std::string> args = {"curl", "-sSL", "-N", "--connect-timeout", "10", "-A", AGENT, "-D", "-"};
    if (icy) { args.push_back("-H"); args.push_back("Icy-MetaData: 1"); }
    args.push_back("--");
    args.push_back(url);
    std::vector<char*> av;
    for (auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    auto c = std::make_unique<CurlConn>();
    int r = posix_spawnp(&c->pid, "curl", &fa, nullptr, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(p[1]);
    c->fd = p[0];
    if (r != 0) { c->pid = -1; err = "Couldn't start the download (no curl)."; return nullptr; }
    CurlConn* cc = c.get();
    if (!readHeaders(*c, [&](char* b, size_t n) { return cc->raw(b, n); }, stop, true, err)) return nullptr;
    return c;
}
#endif

[[maybe_unused]] static bool isLocal(const std::string& h) { return h == "localhost" || h == "127.0.0.1" || h == "::1"; }
// Opens a URL and follows redirects (and playlists when `playlists`): a connection positioned at the body
static std::unique_ptr<Conn> openUrl(std::string url, bool icy, bool playlists, const std::atomic<bool>& stop, std::string& err) {
    for (int hop = 0; hop < 6 && !stop; hop++) {
        Url u = parseUrl(url);
        if (!u.ok) { err = "That isn't a web link (it should start with http:// or https://)."; return nullptr; }
        std::unique_ptr<Conn> c;
#ifdef _WIN32
        c = u.scheme == "http" ? openSocket(u, icy, stop, err) : openWinHttp(u, icy, err);
#else
        c = u.scheme == "http" && isLocal(u.host) ? openSocket(u, icy, stop, err) : openCurl(url, icy, stop, err);
#endif
        if (!c) return nullptr;
        if (c->status >= 300 && c->status < 400) {
            std::string loc = c->header("location");
            if (loc.empty()) { err = "The station sent us elsewhere without saying where."; return nullptr; }
            if (loc.rfind("http", 0) != 0) loc = u.scheme + "://" + u.host + ":" + std::to_string(u.port) + (loc[0] == '/' ? "" : "/") + loc;
            url = loc;
            continue;
        }
        if (c->status != 200) { err = "The station answered " + std::to_string(c->status) + (c->status == 404 ? " (not found)." : "."); return nullptr; }
        std::string ct = lower(c->header("content-type"));
        std::string lu = lower(url.substr(0, url.find('?')));
        bool list = ct.find("mpegurl") != std::string::npos || ct.find("scpls") != std::string::npos || ct.find("pls+xml") != std::string::npos ||
                    (lu.size() > 4 && (lu.compare(lu.size() - 4, 4, ".m3u") == 0 || lu.compare(lu.size() - 4, 4, ".pls") == 0)) ||
                    (lu.size() > 5 && lu.compare(lu.size() - 5, 5, ".m3u8") == 0);
        if (playlists && list) {   // a playlist: read it and go to the first stream in it
            std::string text;
            char b[4096];
            for (int k = 0; k < 400 && text.size() < 65536 && !stop; k++) {
                long n = c->read(b, sizeof b);
                if (n > 0) text.append(b, (size_t)n);
                else if (n != -2) break;
            }
            if (lu.size() > 5 && lu.compare(lu.size() - 5, 5, ".m3u8") == 0 && text.find("#EXT-X-") != std::string::npos) { err = "That's a streaming-video style playlist (HLS): not supported yet. Try the station's MP3 link."; return nullptr; }
            std::string next = playlistUrl(text);
            if (next.empty()) { err = "The playlist had no station link in it."; return nullptr; }
            url = next;
            continue;
        }
        return c;
    }
    if (!stop) err = "Too many redirects.";
    return nullptr;
}

bool httpGet(const std::string& url, std::string& body, std::string* err) {
    std::atomic<bool> stop{false};
    std::string e;
    auto c = openUrl(url, false, false, stop, e);
    if (!c) { if (err) *err = e; return false; }
    body.clear();
    char b[16384];
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        long n = c->read(b, sizeof b);
        if (n > 0) body.append(b, (size_t)n);
        else if (n == 0) break;
        else if (n == -1) { if (err) *err = "The download broke off."; return false; }
        if (body.size() > (8u << 20) || std::chrono::steady_clock::now() - t0 > std::chrono::seconds(20)) break;
    }
    return true;
}

// ---------------------------------------------------------------- the player
struct Player::Impl {
    std::string url;
    std::atomic<bool> stop{false};
    mutable std::mutex m;
    std::vector<float> ring = std::vector<float>((size_t)RATE * 2 * 8);   // 8 seconds of stereo
    size_t head = 0, count = 0;   // floats
    bool started = false;          // enough buffered to play
    std::string state = "Connecting", title, station;
    bool failed = false;

    void fail(const std::string& e) { std::lock_guard<std::mutex> l(m); state = "Error: " + e; failed = true; }
    void setState(const char* s) { std::lock_guard<std::mutex> l(m); if (!failed) state = s; }
    size_t space() { std::lock_guard<std::mutex> l(m); return ring.size() - count; }
    void push(const float* x, size_t n) {
        std::lock_guard<std::mutex> l(m);
        n = std::min(n, ring.size() - count);
        size_t tail = (head + count) % ring.size();
        for (size_t i = 0; i < n; i++) ring[(tail + i) % ring.size()] = x[i];
        count += n;
    }

    // the stream with its ICY metadata taken out
    Conn* conn = nullptr;
    int metaint = 0, untilMeta = 0;
    size_t idleMs = 0;
    long readAudio(char* out, size_t n) {
        while (!stop) {
            if (metaint > 0 && untilMeta == 0) {   // a metadata block: one length byte, then 16 x that
                unsigned char lb = 0;
                if (!readFull((char*)&lb, 1)) return 0;
                std::string meta(lb * 16u, '\0');
                if (lb && !readFull(&meta[0], meta.size())) return 0;
                std::string t = icyTitle(meta);
                if (!t.empty()) { std::lock_guard<std::mutex> l(m); title = t; }
                untilMeta = metaint;
                continue;
            }
            size_t want = metaint > 0 ? std::min(n, (size_t)untilMeta) : n;
            long k = conn->read(out, want);
            if (k == -2) { idleMs += 250; if (idleMs > 15000) return 0; continue; }
            if (k <= 0) return 0;
            idleMs = 0;
            if (metaint > 0) untilMeta -= (int)k;
            return k;
        }
        return 0;
    }
    bool readFull(char* b, size_t n) {
        size_t got = 0;
        while (got < n && !stop) {
            long k = conn->read(b + got, n - got);
            if (k == -2) { idleMs += 250; if (idleMs > 15000) return false; continue; }
            if (k <= 0) return false;
            idleMs = 0;
            got += (size_t)k;
        }
        return got == n;
    }
    static size_t onRead(void* u, void* out, size_t n) {
        Impl* p = (Impl*)u;
        size_t got = 0;
        while (got < n) {   // dr_mp3 wants as much as it asks for, unless it's the end
            long k = p->readAudio((char*)out + got, n - got);
            if (k <= 0) break;
            got += (size_t)k;
        }
        return got;
    }
    static drmp3_bool32 onSeek(void*, int, drmp3_seek_origin) { return DRMP3_FALSE; }

    void run() {
        std::string err;
        auto c = openUrl(url, true, true, stop, err);
        if (!c) { if (!stop) fail(err); return; }
        std::string ct = lower(c->header("content-type"));
        if (ct.find("aac") != std::string::npos || ct.find("mp4") != std::string::npos) { fail("this station sends AAC sound; only MP3 stations play for now. Try its MP3 link."); return; }
        if (ct.find("ogg") != std::string::npos || ct.find("opus") != std::string::npos || ct.find("flac") != std::string::npos) { fail("this station sends Ogg/Opus/FLAC sound; only MP3 stations play for now."); return; }
        if (ct.find("text/html") != std::string::npos) { fail("that link is a web page, not a stream."); return; }
        {
            std::lock_guard<std::mutex> l(m);
            station = c->header("icy-name");
        }
        conn = c.get();
        metaint = std::max(0, std::atoi(c->header("icy-metaint").c_str()));
        untilMeta = metaint;
        setState("Buffering");
        drmp3 mp3;
        if (!drmp3_init(&mp3, onRead, onSeek, this, nullptr)) { if (!stop) fail("couldn't read the sound (is it an MP3 stream?)."); return; }
        const int CH = std::max(1, (int)mp3.channels);
        const double step = mp3.sampleRate > 0 ? (double)mp3.sampleRate / RATE : 1.0;
        std::vector<float> in(1152 * 2 * CH), out;
        double pos = 0;                  // where the next output frame falls in `in` (between frames: interpolated)
        float last[2] = {0, 0};          // the frame before `in`'s first (for interpolating across chunks)
        while (!stop) {
            drmp3_uint64 n = drmp3_read_pcm_frames_f32(&mp3, 1152, in.data());
            if (n == 0) { if (!stop) fail("the station stopped sending."); break; }
            out.clear();
            auto frame = [&](long i, int ch) -> float {   // i = -1: the last frame of the chunk before
                if (i < 0) return last[ch];
                return in[(size_t)i * CH + (CH > 1 ? ch : 0)];
            };
            while (pos < (double)n - 1 + 1e-9) {
                long i0 = (long)std::floor(pos);
                double f = pos - i0;
                long a = i0, b = std::min<long>(i0 + 1, (long)n - 1);
                if (pos < 0) { a = -1; b = 0; f = pos + 1; }
                for (int ch = 0; ch < 2; ch++) out.push_back((float)(frame(a, ch) * (1 - f) + frame(b, ch) * f));
                pos += step;
            }
            pos -= (double)n;
            last[0] = frame((long)n - 1, 0); last[1] = frame((long)n - 1, 1);
            while (!stop && space() < out.size()) std::this_thread::sleep_for(std::chrono::milliseconds(20));   // (a full buffer: wait)
            push(out.data(), out.size());
        }
        drmp3_uninit(&mp3);
    }
};

Player::Player(const std::string& url) : url_(url), p_(std::make_shared<Impl>()) {
    p_->url = url;
    std::shared_ptr<Impl> p = p_;
    std::thread([p] { p->run(); }).detach();   // (it holds its own share: it can finish after the Player has gone)
}
Player::~Player() { p_->stop = true; }
void Player::read(float* lr, int frames) {
    Impl& p = *p_;
    std::lock_guard<std::mutex> l(p.m);
    size_t want = (size_t)frames * 2;
    if (!p.started && p.count >= (size_t)RATE * 2 * 3 / 2) { p.started = true; if (!p.failed) p.state = "Playing"; }   // 1.5 s waiting: start
    if (!p.started) { std::fill(lr, lr + want, 0.0f); return; }
    size_t n = std::min(want, p.count);
    for (size_t i = 0; i < n; i++) lr[i] = p.ring[(p.head + i) % p.ring.size()];
    std::fill(lr + n, lr + want, 0.0f);
    p.head = (p.head + n) % p.ring.size();
    p.count -= n;
    if (n < want && !p.failed) { p.started = false; p.state = "Buffering"; }   // ran dry: wait for more
}
std::string Player::state() const { std::lock_guard<std::mutex> l(p_->m); return p_->state; }
std::string Player::title() const { std::lock_guard<std::mutex> l(p_->m); return p_->title; }
std::string Player::station() const { std::lock_guard<std::mutex> l(p_->m); return p_->station; }
bool Player::failed() const { std::lock_guard<std::mutex> l(p_->m); return p_->failed; }
double Player::buffered() const { std::lock_guard<std::mutex> l(p_->m); return (double)p_->count / (2.0 * RATE); }

// ---------------------------------------------------------------- where it's heard from
Spatial spatial(double side, double front, double dist, double volume, double reach, bool blocked) {
    Spatial s;
    const double ref = 1.5;                                   // full loudness inside this
    double g = dist <= ref ? 1.0 : ref / dist;                // falls off as 1/distance (6 dB each time it doubles)
    g *= std::clamp((reach - dist) / (0.35 * reach), 0.0, 1.0);   // and fades out over the last third of its reach
    g *= std::clamp(volume, 0.0, 1.0);
    double l = std::hypot(side, front);
    double pan = l > 1e-6 ? std::clamp(side / l, -1.0, 1.0) : 0.0;
    pan *= std::clamp(dist / 1.0, 0.0, 1.0) * 0.85;          // (right on top of it: from both sides; never fully one ear)
    double a = (pan + 1) * 0.7853981633974483;                // 0..pi/2
    double behind = l > 1e-6 ? std::clamp(-front / l, 0.0, 1.0) : 0.0;
    s.cutoff = 20000 - 13000 * behind;                        // from behind: duller (the head shades the highs)
    g *= 1 - 0.15 * behind;
    s.cutoff = std::min(s.cutoff, 20000.0 / (1 + dist / 60));   // far away: the air takes the highs
    if (blocked) { g *= 0.4; s.cutoff = std::min(s.cutoff, 700.0); }   // through a wall: quieter and muffled
    s.left = g * std::cos(a) * 1.41421356;
    s.right = g * std::sin(a) * 1.41421356;
    s.left = std::min(s.left, 1.0); s.right = std::min(s.right, 1.0);
    return s;
}

// ---------------------------------------------------------------- stations
const std::vector<StationInfo>& builtinStations() {
    static const std::vector<StationInfo> list = {
        {"Groove Salad (SomaFM)", "https://ice1.somafm.com/groovesalad-128-mp3", "ambient, downtempo", "US", "MP3", 128},
        {"Secret Agent (SomaFM)", "https://ice1.somafm.com/secretagent-128-mp3", "lounge, spy", "US", "MP3", 128},
        {"Boot Liquor (SomaFM)", "https://ice1.somafm.com/bootliquor-128-mp3", "americana, country", "US", "MP3", 128},
        {"Indie Pop Rocks (SomaFM)", "https://ice1.somafm.com/indiepop-128-mp3", "indie, pop", "US", "MP3", 128},
        {"DEF CON Radio (SomaFM)", "https://ice1.somafm.com/defcon-128-mp3", "electronic", "US", "MP3", 128},
        {"Lush (SomaFM)", "https://ice1.somafm.com/lush-128-mp3", "vocals, chill", "US", "MP3", 128},
        {"Radio Paradise", "https://stream.radioparadise.com/mp3-192", "eclectic, rock", "US", "MP3", 192},
    };
    return list;
}
std::vector<StationInfo> parseStations(const std::string& text) {
    std::vector<StationInfo> out;
    try {
        auto j = nlohmann::json::parse(text);
        if (!j.is_array()) return out;
        for (auto& s : j) {
            StationInfo st;
            st.name = trim(s.value("name", ""));
            st.url = s.value("url_resolved", "");
            if (st.url.empty()) st.url = s.value("url", "");
            st.tags = s.value("tags", "");
            st.country = s.value("countrycode", "");
            st.codec = s.value("codec", "");
            st.bitrate = s.value("bitrate", 0);
            if (st.name.empty() || st.url.empty()) continue;
            if (!st.codec.empty() && lower(st.codec) != "mp3") continue;
            if (std::any_of(out.begin(), out.end(), [&](const StationInfo& o) { return o.url == st.url; })) continue;
            out.push_back(st);
        }
    } catch (...) {}
    return out;
}
static std::string urlEncode(const std::string& s) {
    std::string o;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
std::vector<StationInfo> searchStations(const std::string& query, int limit, std::string* err) {
    std::string q = urlEncode(trim(query));
    std::string base = "https://all.api.radio-browser.info/json/stations/search?codec=MP3&hidebroken=true&order=clickcount&reverse=true&limit=" + std::to_string(limit);
    std::vector<StationInfo> out;
    for (const char* field : {"name", "tag"}) {   // by name, then by genre to fill up
        std::string body;
        if (!httpGet(base + "&" + field + "=" + q, body, err)) {
            if (std::string(field) == "name") {   // (the round-robin name failed: one server directly)
                if (!httpGet("https://de1.api.radio-browser.info/json/stations/search?codec=MP3&hidebroken=true&order=clickcount&reverse=true&limit=" +
                             std::to_string(limit) + "&name=" + q, body, err)) return out;
            } else break;
        }
        for (auto& s : parseStations(body))
            if ((int)out.size() < limit && std::none_of(out.begin(), out.end(), [&](const StationInfo& o) { return o.url == s.url; })) out.push_back(s);
        if ((int)out.size() >= limit / 2) break;
    }
    return out;
}

}  // namespace netradio
