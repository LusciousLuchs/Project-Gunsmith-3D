// Gunsmith3D - the player's own textures (see usertex.hpp).
#include "app/usertex.hpp"
#include "app/app.hpp"
#include "app/paths.hpp"
#include "map.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

static std::vector<std::string> FILES;
static std::map<std::string, Texture2D> CACHE;

bool isImageFile(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga" || e == ".qoi";
}

void scanUserTextures() {
    FILES.clear();
    std::error_code ec;
    if (fs::is_directory(TEXTURES_DIR, ec))
        for (auto& e : fs::directory_iterator(TEXTURES_DIR, ec))
            if (e.is_regular_file(ec) && isImageFile(e.path().string())) FILES.push_back(e.path().filename().string());
    std::sort(FILES.begin(), FILES.end());
    for (auto& f : FILES) gmap::materialIndex("user:" + f);  // so map blocks can use them too
}
const std::vector<std::string>& userTextureFiles() { return FILES; }

Texture2D userTexture(const std::string& file) {
    auto it = CACHE.find(file);
    if (it != CACHE.end()) return it->second;
    Image img = LoadImage((fs::path(TEXTURES_DIR) / file).string().c_str());
    if (!img.data) img = GenImageChecked(64, 64, 8, 8, Color{220, 60, 200, 255}, Color{30, 30, 30, 255});  // missing: loud on purpose
    int big = std::max(img.width, img.height);
    if (big > 2048) ImageResize(&img, img.width * 2048 / big, img.height * 2048 / big);  // plenty, and kind to the graphics card
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    CACHE[file] = t;
    return t;
}

std::string importTexture(const std::string& path) {
    if (!isImageFile(path)) return "";
    std::error_code ec;
    fs::create_directories(TEXTURES_DIR, ec);
    fs::path src(path), dst = fs::path(TEXTURES_DIR) / src.filename();
    if (fs::exists(dst, ec) && fs::equivalent(src, dst, ec)) return dst.filename().string();  // already in the folder
    for (int k = 2; fs::exists(dst, ec); k++)  // never overwrite: "rust (2).png"
        dst = fs::path(TEXTURES_DIR) / (src.stem().string() + " (" + std::to_string(k) + ")" + src.extension().string());
    fs::copy_file(src, dst, ec);
    if (ec) return "";
    scanUserTextures();
    return dst.filename().string();
}

void handleDroppedFiles() {
    if (!IsFileDropped()) return;
    FilePathList list = LoadDroppedFiles();
    int added = 0;
    std::string last;
    for (unsigned i = 0; i < list.count; i++) {
        std::string n = importTexture(list.paths[i]);
        if (!n.empty()) { added++; last = n; }
    }
    UnloadDroppedFiles(list);
    if (added == 1) toast("Added the texture " + last + ". Pick it under Surface on a part, or on the map editor's Textures tab.");
    else if (added > 1) toast("Added " + std::to_string(added) + " textures.");
    else toast("Only pictures can be dropped here (.png, .jpg, .bmp, .tga).");
}
