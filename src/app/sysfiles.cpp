// Gunsmith3D - the operating system's file boxes. Kept apart from raylib: Windows' headers clash with its names.
#include <string>
#include <filesystem>
#include "app/paths.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

// A wide Windows path as a plain (ANSI) one the C file functions take: its short form, so a user name with accents
// or other letters still gives a path that opens
static std::string narrowPath(const wchar_t* w) {
    // The long name when every character fits the ANSI code page (the file functions use it); else the short 8.3 name
    char out[MAX_PATH * 4];
    BOOL lost = FALSE;
    int m = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, out, sizeof out, nullptr, &lost);
    if (m <= 0 || lost) {
        wchar_t sh[MAX_PATH * 2];
        DWORD n = GetShortPathNameW(w, sh, MAX_PATH * 2);
        if (n == 0 || n >= MAX_PATH * 2) return "";
        m = WideCharToMultiByte(CP_ACP, 0, sh, -1, out, sizeof out, nullptr, nullptr);
        if (m <= 0) return "";
    }
    std::string s(out);
    for (auto& c : s) if (c == '\\') c = '/';
    return s;
}
std::string documentsFolder() {
    wchar_t w[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, w))) return "";
    return narrowPath(w);
}
bool pickFolder(std::string& out, const char* title) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t wt[256];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 256);
    BROWSEINFOW bi{};
    bi.lpszTitle = wt;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return false;
    wchar_t p[MAX_PATH];
    bool ok = SHGetPathFromIDListW(id, p) != 0;
    CoTaskMemFree(id);
    if (!ok) return false;
    out = narrowPath(p);
    return !out.empty();
}
static char gCrashDir[MAX_PATH * 2] = "user\\crashes";
void setCrashFolder(const std::string& dir) {
    std::string d = dir;
    for (auto& c : d) if (c == '/') c = '\\';
    lstrcpynA(gCrashDir, d.c_str(), sizeof gCrashDir);
}

bool pickImageFile(std::string& out) {
    char buf[MAX_PATH * 4] = "";
    OPENFILENAMEA o{};
    o.lStructSize = sizeof o;
    o.lpstrFilter = "Pictures (*.png;*.jpg;*.jpeg;*.bmp;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All files\0*.*\0";
    o.lpstrFile = buf;
    o.nMaxFile = sizeof buf;
    o.lpstrTitle = "Add a texture";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&o)) return false;
    out = buf;
    return true;
}
void openTexturesFolder() {
    std::error_code ec;
    std::filesystem::create_directories(TEXTURES_DIR, ec);
    std::string p = std::filesystem::absolute(TEXTURES_DIR, ec).string();
    ShellExecuteA(nullptr, "open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
// If the game crashes, leave a report in the Crashes folder: a short text file (what went wrong, and where) and a minidump
// (the state of the program at that moment) that a developer can open to see exactly which line it was.
static LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    SHCreateDirectoryExA(nullptr, gCrashDir, nullptr);   // (makes the whole path)
    if (gCrashDir[0] && !(gCrashDir[1] == ':')) { char full[MAX_PATH * 2]; GetFullPathNameA(gCrashDir, sizeof full, full, nullptr); SHCreateDirectoryExA(nullptr, full, nullptr); }
    SYSTEMTIME t; GetLocalTime(&t);
    char base[MAX_PATH * 2 + 64];
    wsprintfA(base, "%s\\crash-%04d%02d%02d-%02d%02d%02d", gCrashDir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    char path[MAX_PATH * 2 + 80];
    wsprintfA(path, "%s.txt", base);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        char msg[512];
        void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
        HMODULE mod = GetModuleHandleA(nullptr);
        int n = wsprintfA(msg, "Gunsmith3D crashed.\r\nException 0x%08lX at %p (offset 0x%lX in the game)\r\n"
                               "Please send this file and the .dmp next to it to the developer.\r\n",
                          ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0, addr,
                          (unsigned long)((char*)addr - (char*)mod));
        DWORD w; WriteFile(f, msg, n, &w, nullptr);
        CloseHandle(f);
    }
    HMODULE dbg = LoadLibraryA("dbghelp.dll");  // (loaded here, so the game doesn't need it to start)
    if (dbg) {
        typedef BOOL(WINAPI * Dump)(HANDLE, DWORD, HANDLE, int, PVOID, PVOID, PVOID);
        Dump write = (Dump)(void*)GetProcAddress(dbg, "MiniDumpWriteDump");
        wsprintfA(path, "%s.dmp", base);
        HANDLE d = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (write && d != INVALID_HANDLE_VALUE) {
            struct { DWORD tid; EXCEPTION_POINTERS* ep; BOOL client; } info{GetCurrentThreadId(), ep, FALSE};
            write(GetCurrentProcess(), GetCurrentProcessId(), d, 0x00000000 | 0x00000004 /* normal + handle data */, ep ? &info : nullptr, nullptr, nullptr);
        }
        if (d != INVALID_HANDLE_VALUE) CloseHandle(d);
    }
    char msgBox[MAX_PATH * 2 + 160];
    wsprintfA(msgBox, "Gunsmith3D crashed. A crash report was saved in:\n%s\nPlease send it to the developer.", gCrashDir);
    MessageBoxA(nullptr, msgBox, "Gunsmith3D", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
void installCrashHandler() { SetUnhandledExceptionFilter(onCrash); }
void openFolder(const std::string& p) { CreateDirectoryA(p.c_str(), nullptr); ShellExecuteA(nullptr, "open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
#else
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
// Linux file boxes: the desktop's own (zenity on GNOME and most others, kdialog on KDE), run as a child; its answer
// is the path it prints. False if cancelled or neither is installed (then a message says what to install).
void toast(const std::string& t);   // (app.cpp)
static void linuxDialogMissing() { toast("No file box on this desktop: install zenity (or kdialog) with your package manager, or drag files onto the window."); }
static bool have(const char* tool) { return std::system((std::string("command -v ") + tool + " >/dev/null 2>&1").c_str()) == 0; }
static bool askDialog(const std::string& zenityArgs, const std::string& kdialogArgs, std::string& out) {
    std::string cmd;
    bool kde = std::getenv("KDE_FULL_SESSION") && have("kdialog");
    if (!kde && have("zenity")) cmd = "zenity " + zenityArgs;
    else if (have("kdialog")) cmd = "kdialog " + kdialogArgs;
    else { linuxDialogMissing(); return false; }
    FILE* f = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!f) return false;
    char buf[4096]; std::string got;
    while (fgets(buf, sizeof buf, f)) got += buf;
    int rc = pclose(f);
    while (!got.empty() && (got.back() == '\n' || got.back() == '\r')) got.pop_back();
    if (rc != 0 || got.empty()) return false;
    out = got;
    return true;
}
bool pickImageFile(std::string& out) {
    return askDialog("--file-selection --title='Add a texture' --file-filter='Pictures | *.png *.jpg *.jpeg *.bmp *.tga' --file-filter='All files | *'",
                     "--title 'Add a texture' --getopenfilename ~ 'Pictures (*.png *.jpg *.jpeg *.bmp *.tga)'", out);
}
// Linux: the desktop's Documents folder (XDG_DOCUMENTS_DIR from ~/.config/user-dirs.dirs, else ~/Documents), so the
// player's files sit in Documents/OAB Foundry/Gunsmith as on Windows. GUNSMITH_PORTABLE keeps them in the game folder.
std::string documentsFolder() {
    if (std::getenv("GUNSMITH_PORTABLE")) return "";
    const char* home = std::getenv("HOME");
    if (!home || !*home) return "";
    std::string h = home;
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    std::ifstream in((cfg && *cfg ? std::string(cfg) : h + "/.config") + "/user-dirs.dirs");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("XDG_DOCUMENTS_DIR=", 0) != 0) continue;
        std::string v = line.substr(18);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (v.rfind("$HOME", 0) == 0) v = h + v.substr(5);
        if (!v.empty() && v != h && v != h + "/") return v;   // (a desktop with no Documents folder points it at home)
    }
    return h + "/Documents";
}
bool pickFolder(std::string& out, const char* title) {
    std::string t = title; for (auto& c : t) if (c == '\'') c = ' ';
    return askDialog("--file-selection --directory --title='" + t + "'", "--title '" + t + "' --getexistingdirectory ~", out);
}
// If the game crashes, leave a report in the Crashes folder (what went wrong and the call stack), as on Windows
static char gCrashDir[2048] = "user/crashes";
void setCrashFolder(const std::string& dir) { std::snprintf(gCrashDir, sizeof gCrashDir, "%s", dir.c_str()); }
static void onCrash(int sig) {
    std::signal(sig, SIG_DFL);
    std::string mk = std::string("mkdir -p '") + gCrashDir + "'";
    if (std::system(mk.c_str())) {}
    char path[2300];
    std::snprintf(path, sizeof path, "%s/crash-%ld.txt", gCrashDir, (long)std::time(nullptr));
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char head[256];
        int n = std::snprintf(head, sizeof head, "Gunsmith3D crashed (signal %d: %s).\nCall stack:\n", sig, strsignal(sig));
        if (write(fd, head, n) < 0) {}
        void* frames[64];
        int k = backtrace(frames, 64);
        backtrace_symbols_fd(frames, k, fd);
        close(fd);
    }
    std::fprintf(stderr, "Gunsmith3D crashed. A crash report was saved in %s. Please send it to the developer.\n", gCrashDir);
    if (have("zenity")) { std::string m = std::string("zenity --error --title=Gunsmith3D --text='Gunsmith3D crashed. A crash report was saved in:\n") + gCrashDir + "\nPlease send it to the developer.' >/dev/null 2>&1"; if (std::system(m.c_str())) {} }
    std::raise(sig);
}
void installCrashHandler() { for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) std::signal(s, onCrash); }
void openFolder(const std::string& p) {
    std::error_code ec; std::filesystem::create_directories(p, ec);
    std::string cmd = "xdg-open \"" + p + "\" >/dev/null 2>&1 &";
    if (std::system(cmd.c_str())) {}
}
void openTexturesFolder() {
    std::error_code ec;
    std::filesystem::create_directories(TEXTURES_DIR, ec);
    std::string cmd = "xdg-open \"" + std::filesystem::absolute(TEXTURES_DIR, ec).string() + "\" >/dev/null 2>&1 &";
    if (std::system(cmd.c_str())) {}
}
#endif
