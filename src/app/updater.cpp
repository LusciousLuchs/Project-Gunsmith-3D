// Gunsmith3D - updates itself from the GitHub releases (see updater.hpp), on Windows and Linux. Kept apart from raylib: Windows' headers
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
    // ask each place releases are published (the downloads page, then the game's own repository) and take the
    // newest version that has a zip
    std::vector<std::string> apis;
    if (const char* over = std::getenv("GUNSMITH_UPDATE_API")) apis = {over};   // (testing) somewhere else to ask
    else for (const char* r : {REPO, CODE_REPO}) apis.push_back(std::string("https://api.github.com/repos/") + r + "/releases/latest");
    std::string latest, zipUrl;
    bool reached = false;
    for (auto& api : apis) {
        std::string body;
        if (!httpGet(api, body, L"application/vnd.github+json")) continue;
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object() || !j.contains("tag_name")) continue;
        reached = true;
        std::string tag = j.value("tag_name", "");
        std::string v = tag.size() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
        std::string url;
        for (auto& a : j.value("assets", nlohmann::json::array()))
            if (a.is_object()) { std::string nm = a.value("name", ""); if (nm.size() > 4 && nm.substr(nm.size() - 4) == ".zip") { url = a.value("browser_download_url", ""); break; } }
        if (url.empty()) continue;
        if (latest.empty() || newer(v, latest)) { latest = v; zipUrl = url; }
    }
    if (!reached) { set(State::Failed, "Couldn't reach the update server (offline?)."); return; }
    if (latest.empty() || !newer(latest, version::text())) { set(State::UpToDate, "", latest.empty() ? version::text() : latest); return; }
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
    {   // installed with the installer: Windows' Apps list shows the new version too (the key is only there if it was)
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Gunsmith3D", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            std::wstring wv(latest.begin(), latest.end());
            RegSetValueExW(k, L"DisplayVersion", 0, REG_SZ, (const BYTE*)wv.c_str(), (DWORD)((wv.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
    }
    set(State::Installed);
}
}  // namespace

void checkNow() {   // (a press of Check for updates: again, even after an answer)
    {
        std::lock_guard<std::mutex> l(gMu);
        if (gStatus.state == State::Checking || gStatus.state == State::Downloading || gStatus.state == State::Installing || gStatus.state == State::Installed) return;
        gStatus = Status{};
    }
    start();
}
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

#else   // Linux: the same steps with the system's curl and tar; the release's Gunsmith3D-Linux.tar.gz
#include <csignal>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
namespace update {
namespace fs = std::filesystem;
namespace {
std::string quoted(const std::string& s) { std::string o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'"; }
// a URL's body, through curl (redirects followed, a time limit); false if curl isn't there or it failed
bool httpGet(const std::string& url, std::string& out, const char* accept, int seconds = 15) {
    std::string cmd = "curl -fsSL --max-time " + std::to_string(seconds) + " -H " + quoted(std::string("Accept: ") + accept) + " " + quoted(url) + " 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    return pclose(f) == 0 && !out.empty();
}
bool download(const std::string& url, const fs::path& to) {
    std::string cmd = "curl -fsSL --max-time 600 -o " + quoted(to.string()) + " " + quoted(url) + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}
fs::path exePath() { std::error_code ec; return fs::read_symlink("/proc/self/exe", ec); }
fs::path gExe;

void work() {
    set(State::Checking);
    std::vector<std::string> apis;
    if (const char* over = std::getenv("GUNSMITH_UPDATE_API")) apis = {over};
    else for (const char* r : {REPO, CODE_REPO}) apis.push_back(std::string("https://api.github.com/repos/") + r + "/releases/latest");
    std::string latest, url;
    bool reached = false;
    for (auto& api : apis) {
        std::string body;
        if (!httpGet(api, body, "application/vnd.github+json", 8)) continue;
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object() || !j.contains("tag_name")) continue;
        reached = true;
        std::string tag = j.value("tag_name", "");
        std::string v = tag.size() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
        std::string u;
        for (auto& a : j.value("assets", nlohmann::json::array()))
            if (a.is_object() && a.value("name", "") == "Gunsmith3D-Linux.tar.gz") { u = a.value("browser_download_url", ""); break; }
        if (u.empty()) continue;
        if (latest.empty() || newer(v, latest)) { latest = v; url = u; }
    }
    if (!reached) { set(State::Failed, "Couldn't reach the update server (offline, or curl isn't installed)."); return; }
    if (latest.empty() || !newer(latest, version::text())) { set(State::UpToDate, "", latest.empty() ? version::text() : latest); return; }
    set(State::Downloading, "", latest);
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("gunsmith3d-update-" + std::to_string(getpid()));
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "files", ec);
    if (!download(url, tmp / "g.tar.gz")) { fs::remove_all(tmp, ec); set(State::Failed, "The download didn't finish."); return; }
    set(State::Installing, "", latest);
    if (std::system(("tar -xzf " + quoted((tmp / "g.tar.gz").string()) + " -C " + quoted((tmp / "files").string()) + " >/dev/null 2>&1").c_str()) != 0) {
        fs::remove_all(tmp, ec); set(State::Failed, "Couldn't unpack the download."); return;
    }
    fs::path root = tmp / "files" / "Gunsmith3D";
    if (!fs::exists(root / "gunsmith3d", ec)) { fs::remove_all(tmp, ec); set(State::Failed, "The download didn't hold the game."); return; }
    fs::path exe = gExe, dir = exe.parent_path();
    if (access(dir.c_str(), W_OK) != 0) { fs::remove_all(tmp, ec); set(State::Failed, "The game folder can't be written to: download the new version from GitHub."); return; }
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        fs::path rel = fs::relative(it->path(), root, ec), dst = dir / rel;
        bool userFile = !rel.empty() && *rel.begin() == "user";
        if (userFile && fs::exists(dst)) continue;   // the player's own files: only add missing examples
        fs::create_directories(dst.parent_path(), ec);
        // copied beside it, then renamed over it: the running program keeps its old file until it quits
        fs::path part = dst; part += ".new";
        fs::copy_file(it->path(), part, fs::copy_options::overwrite_existing, ec);
        if (ec) { fs::remove_all(tmp, ec); set(State::Failed, "Couldn't copy the new files."); return; }
        fs::permissions(part, fs::status(it->path(), ec).permissions(), ec);
        fs::rename(part, dst, ec);
        if (ec) { fs::remove_all(tmp, ec); set(State::Failed, "Couldn't replace the game's files."); return; }
    }
    // the launcher (installed with install-linux.sh) and its version note, so it agrees with what's here now
    if (fs::exists(dir / "install-linux.sh", ec)) fs::copy_file(dir / "install-linux.sh", dir / ".launcher.sh", fs::copy_options::overwrite_existing, ec);
    std::ofstream(dir / "VERSION") << latest << "\n";
    fs::remove_all(tmp, ec);
    set(State::Installed);
}
}  // namespace

void checkNow() {
    {
        std::lock_guard<std::mutex> l(gMu);
        if (gStatus.state == State::Checking || gStatus.state == State::Downloading || gStatus.state == State::Installing || gStatus.state == State::Installed) return;
        gStatus = Status{};
    }
    start();
}
void start() {
    if (std::getenv("GUNSMITH_NO_UPDATE")) return;
    {
        std::lock_guard<std::mutex> l(gMu);
        if (gStatus.state != State::Off) return;
        gStatus.state = State::Checking;
    }
    if (gExe.empty()) gExe = exePath();
    if (gExe.empty()) { set(State::Failed, "Couldn't find the game's own file."); return; }
    std::thread(work).detach();
}
bool restart() {   // a fresh copy of the (new) program, on its own, then this one quits
    if (status().state != State::Installed || gExe.empty()) return false;
    pid_t p = fork();
    if (p < 0) return false;
    if (p == 0) {
        setsid();
        if (chdir(gExe.parent_path().c_str()) != 0) _exit(1);
        execl(gExe.c_str(), gExe.c_str(), (char*)nullptr);
        _exit(1);
    }
    return true;
}
void cleanup() { if (gExe.empty()) gExe = exePath(); }
}  // namespace update
#endif
