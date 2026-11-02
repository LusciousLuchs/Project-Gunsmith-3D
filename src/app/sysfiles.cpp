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
#else
#include <cstdlib>
bool pickImageFile(std::string&) { return false; }  // (the game ships for Windows; on Linux, drop files on the window)
void openTexturesFolder() {
    std::error_code ec;
    std::filesystem::create_directories("user/textures", ec);
    std::string cmd = "xdg-open \"" + std::filesystem::absolute("user/textures", ec).string() + "\" >/dev/null 2>&1 &";
    if (std::system(cmd.c_str())) {}
}
#endif
