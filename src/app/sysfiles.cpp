// Gunsmith3D - the operating system's file boxes. Kept apart from raylib: Windows' headers clash with its names.
#include <string>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

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
    std::filesystem::create_directories("user/textures", ec);
    std::string p = std::filesystem::absolute("user/textures", ec).string();
    ShellExecuteA(nullptr, "open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
// If the game crashes, leave a report in user/crashes: a short text file (what went wrong, and where) and a minidump
// (the state of the program at that moment) that a developer can open to see exactly which line it was.
static LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    CreateDirectoryA("user", nullptr);
    CreateDirectoryA("user\\crashes", nullptr);
    SYSTEMTIME t; GetLocalTime(&t);
    char base[128];
    wsprintfA(base, "user\\crashes\\crash-%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    char path[160];
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
    MessageBoxA(nullptr, "Gunsmith3D crashed. A crash report was saved in the game folder, under user\\crashes.\nPlease send it to the developer.",
                "Gunsmith3D", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
void installCrashHandler() { SetUnhandledExceptionFilter(onCrash); }
#else
#include <cstdlib>
bool pickImageFile(std::string&) { return false; }  // (the game ships for Windows; on Linux, drop files on the window)
void installCrashHandler() {}
void openTexturesFolder() {
    std::error_code ec;
    std::filesystem::create_directories("user/textures", ec);
    std::string cmd = "xdg-open \"" + std::filesystem::absolute("user/textures", ec).string() + "\" >/dev/null 2>&1 &";
    if (std::system(cmd.c_str())) {}
}
#endif
