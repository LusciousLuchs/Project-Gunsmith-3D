// Gunsmith3D - the player's folders (see paths.hpp).
#include "app/paths.hpp"
#include "app/app.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Move every file from an old folder into the new one (never over a file already there), then drop the old folder if
// it's empty. Copy-then-delete, so a move across drives works too.
static void moveOver(const char* from, const char* to) {
    std::error_code ec;
    if (!fs::is_directory(from, ec)) return;
    for (auto& e : fs::directory_iterator(from, ec)) {
        if (!e.is_regular_file(ec)) continue;
        fs::path dst = fs::path(to) / e.path().filename();
        if (fs::exists(dst, ec)) continue;
        fs::copy_file(e.path(), dst, ec);
        if (!ec) fs::remove(e.path(), ec);
    }
    if (fs::is_empty(from, ec)) fs::remove(from, ec);
}

void setupUserFolders() {
    std::error_code ec;
    for (const char* d : {GUNS_DIR, MAPS_DIR, TEXTURES_DIR, PARTS_DIR, NPCS_DIR, GROUPS_DIR}) fs::create_directories(d, ec);
    moveOver("blueprints", GUNS_DIR);  // where earlier versions kept them
    moveOver("maps", MAPS_DIR);
}

std::string safeFileName(const std::string& s) {
    std::string o;
    for (char c : s) o += (std::strchr("<>:\"/\\|?*", c) || (unsigned char)c < 32) ? '-' : c;
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    return o.empty() ? "Untitled" : o;
}

bool saveBlueprint() {
    std::string name = A.saveName[0] ? A.saveName : (A.stash ? A.stash->name : A.design.name);
    name = safeFileName(name);
    std::snprintf(A.saveName, sizeof A.saveName, "%s", name.c_str());
    std::error_code ec;
    fs::create_directories(GUNS_DIR, ec);
    std::ofstream f(fs::path(GUNS_DIR) / (name + ".json"));
    if (!f) { toast("Couldn't save " + name + "."); return false; }
    f << designToJson(A.stash ? *A.stash : A.design, true);  // the sandbox design, even with another gun on the bench
    toast("Saved " + name + " (user/guns).");
    return true;
}
