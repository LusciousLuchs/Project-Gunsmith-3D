// Gunsmith3D - updates itself from the GitHub releases (see updater.hpp). Kept apart from raylib: Windows' headers
// clash with its names.
#include "app/updater.hpp"
#include "version.hpp"
#include "json.hpp"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace update {
namespace {
std::mutex gMu;
Status gStatus;
void set(State s, const std::string& note = "", const std::string& latest = "") {
    std::lock_guard<std::mutex> l(gMu);
    gStatus.state = s;
    if (!note.empty()) gStatus.note = note;
    if (!latest.empty()) gStatus.latest = latest;
}
std::vector<int> parts(const std::string& v) {
    std::vector<int> out;
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    while (i < v.size() && out.size() < 3) {
        if (!std::isdigit((unsigned char)v[i])) break;
        out.push_back(std::atoi(v.c_str() + i));
        while (i < v.size() && std::isdigit((unsigned char)v[i])) i++;
        if (i < v.size() && v[i] == '.') i++; else break;
    }
    while (out.size() < 3) out.push_back(0);
    return out;
}
}  // namespace

bool newer(const std::string& a, const std::string& b) { return parts(a) > parts(b); }
Status status() { std::lock_guard<std::mutex> l(gMu); return gStatus; }
}  // namespace update

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

namespace update {
namespace fs = std::filesystem;
namespace {
std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
// GET a URL (https or http; redirects followed) into `out`. False on any failure.
bool httpGet(const std::string& url, std::string& out, const wchar_t* accept) {
    std::wstring wu = widen(url);
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    wchar_t extra[1024]; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(wu.c_str(), 0, 0, &uc)) return false;
    std::wstring full = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    HINTERNET s = WinHttpOpen(L"Gunsmith3D-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", full.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r) {
        std::wstring hdr = std::wstring(L"Accept: ") + accept;
        DWORD timeout = 30000;
        WinHttpSetOption(r, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof timeout);
        if (WinHttpSendRequest(r, hdr.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, sz = sizeof code;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            if (code == 200) {
                out.clear();
                DWORD avail = 0;
                ok = true;
                while (WinHttpQueryDataAvailable(r, &avail) && avail > 0) {
                    size_t at = out.size();
                    out.resize(at + avail);
                    DWORD got = 0;
                    if (!WinHttpReadData(r, &out[at], avail, &got)) { ok = false; break; }
                    out.resize(at + got);
                }
            }
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
// raylib's DEFLATE decoder (raylib.h itself can't be included next to windows.h)
extern "C" unsigned char* DecompressData(const unsigned char* compData, int compDataSize, int* dataSize);
extern "C" void MemFree(void* ptr);
unsigned rd16(const std::string& z, size_t at) { return at + 2 <= z.size() ? (unsigned char)z[at] | (unsigned char)z[at + 1] << 8 : 0; }
unsigned rd32(const std::string& z, size_t at) { return at + 4 <= z.size() ? rd16(z, at) | rd16(z, at + 2) << 16 : 0; }
// Unpack a zip (stored or deflated entries) into `out`. False if it isn't a zip we can read.
bool unzipTo(const std::string& z, const fs::path& out) {
    if (z.size() < 22) return false;
    size_t eocd = std::string::npos;
    for (size_t i = z.size() - 22 + 1; i-- > 0 && z.size() - i < 66000;) if (rd32(z, i) == 0x06054b50) { eocd = i; break; }
    if (eocd == std::string::npos) return false;
    unsigned count = rd16(z, eocd + 10);
    size_t at = rd32(z, eocd + 16);
    int files = 0;
    for (unsigned k = 0; k < count; k++) {
        if (rd32(z, at) != 0x02014b50) return false;
        unsigned method = rd16(z, at + 10), csize = rd32(z, at + 20), usize = rd32(z, at + 24);
        unsigned nlen = rd16(z, at + 28), xlen = rd16(z, at + 30), clen = rd16(z, at + 32), loc = rd32(z, at + 42);
        std::string name = z.substr(at + 46, nlen);
        at += 46 + nlen + xlen + clen;
        if (name.empty() || name.back() == '/' || name.find("..") != std::string::npos) continue;   // folders; nothing outside `out`
        if (rd32(z, loc) != 0x04034b50) return false;
        size_t data = loc + 30 + rd16(z, loc + 26) + rd16(z, loc + 28);
        if (data + csize > z.size()) return false;
        std::string bytes;
        if (method == 0) bytes = z.substr(data, csize);
        else if (method == 8) {
            int n = 0;
            unsigned char* u = DecompressData((const unsigned char*)z.data() + data, (int)csize, &n);
            if (!u) return false;
            bytes.assign((const char*)u, n > 0 ? (size_t)n : 0);
            MemFree(u);
        } else return false;
        if (bytes.size() != usize) return false;
        fs::path dst = out / fs::u8path(name);
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        FILE* f = _wfopen(dst.c_str(), L"wb");
        if (!f) return false;
        bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        fclose(f);
        if (!ok) return false;
        files++;
    }
    return files > 0;
}
fs::path exePath() {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    return fs::path(std::wstring(b, n));
}
fs::path oldExe(const fs::path& exe) { fs::path o = exe; o.replace_extension(L".old.exe"); return o; }
fs::path gExe;   // where this program was started from (taken before an update renames it)

void work() {
    set(State::Checking);
    const char* over = std::getenv("GUNSMITH_UPDATE_API");   // (testing) somewhere else to ask
    std::string api = over ? over : std::string("https://api.github.com/repos/") + REPO + "/releases/latest";
    std::string body;
    if (!httpGet(api, body, L"application/vnd.github+json")) { set(State::Failed, "Couldn't reach the update server (offline?)."); return; }
    nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("tag_name")) { set(State::Failed, "The update server's answer didn't make sense."); return; }
    std::string tag = j.value("tag_name", "");
    std::string latest = tag.size() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
    if (!newer(latest, version::text())) { set(State::UpToDate, "", latest); return; }
    std::string zipUrl;
    for (auto& a : j.value("assets", nlohmann::json::array()))
        if (a.is_object()) { std::string n = a.value("name", ""); if (n.size() > 4 && n.substr(n.size() - 4) == ".zip") { zipUrl = a.value("browser_download_url", ""); break; } }
    if (zipUrl.empty()) { set(State::Failed, "The new version has no download yet.", latest); return; }
    set(State::Downloading, "", latest);
    std::string zip;
    if (!httpGet(zipUrl, zip, L"application/octet-stream") || zip.size() < 1000) { set(State::Failed, "The download didn't finish."); return; }
    set(State::Installing);
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / L"gunsmith3d-update";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    fs::path out = tmp / L"files";
    fs::create_directories(out, ec);
    if (!unzipTo(zip, out)) { set(State::Failed, "Couldn't unpack the download."); return; }
    fs::path root = out;   // the zip holds one folder, Gunsmith3D-<version>
    for (auto& e : fs::directory_iterator(out, ec)) if (e.is_directory()) { root = e.path(); break; }
    {   // it must hold the game: a program file at its top
        bool hasExe = false;
        for (auto& e : fs::directory_iterator(root, ec)) if (e.path().extension() == L".exe") hasExe = true;
        if (!hasExe) { set(State::Failed, "The download didn't hold the game."); return; }
    }
    fs::path exe = gExe, dir = exe.parent_path();
    {   // can we write here at all (a game installed under Program Files can't be updated without asking)?
        fs::path probe = dir / L"update-probe.tmp";
        FILE* f = _wfopen(probe.c_str(), L"wb");
        if (!f) { set(State::Failed, "The game folder can't be written to: download the new version from GitHub."); return; }
        fclose(f); fs::remove(probe, ec);
    }
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        fs::path rel = fs::relative(it->path(), root, ec);
        fs::path dst = dir / rel;
        bool userFile = !rel.empty() && *rel.begin() == L"user";
        if (userFile && fs::exists(dst)) continue;   // the player's own files: only add missing examples
        fs::create_directories(dst.parent_path(), ec);
        bool isExe = rel.extension() == L".exe" && rel.parent_path().empty();
        if (isExe) {   // the running program: move it aside (its name frees up), then put the new one there
            fs::path target = dir / exe.filename();
            fs::remove(oldExe(exe), ec);
            if (!MoveFileExW(exe.c_str(), oldExe(exe).c_str(), MOVEFILE_REPLACE_EXISTING)) { set(State::Failed, "Couldn't replace the game's program file."); return; }
            if (!CopyFileW(it->path().c_str(), target.c_str(), FALSE)) {
                MoveFileExW(oldExe(exe).c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);   // put it back
                set(State::Failed, "Couldn't copy the new program file.");
                return;
            }
            continue;
        }
        fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec);
    }
    fs::remove_all(tmp, ec);
    set(State::Installed);
}
}  // namespace

void start() {
    if (std::getenv("GUNSMITH_NO_UPDATE")) return;
    {
        std::lock_guard<std::mutex> l(gMu);
        if (gStatus.state != State::Off) return;   // once a run
        gStatus.state = State::Checking;
    }
    if (gExe.empty()) gExe = exePath();
    std::thread(work).detach();
}
bool restart() {
    if (status().state != State::Installed) return false;
    fs::path exe = gExe;
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
}
void cleanup() {
    std::error_code ec;
    if (gExe.empty()) gExe = exePath();
    fs::path exe = gExe;
    for (int i = 0; i < 20 && fs::exists(oldExe(exe), ec); i++) {   // (the old one may still be closing)
        if (fs::remove(oldExe(exe), ec)) break;
        Sleep(100);
    }
}
}  // namespace update

#else   // other systems: updates come from the zip (the game ships for Windows)
namespace update {
void start() {}
bool restart() { return false; }
void cleanup() {}
}  // namespace update
#endif
