// Gunsmith3D - the player's folders (see paths.hpp).
#include "app/paths.hpp"
#include "app/app.hpp"
#include "design.hpp"
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

namespace {
std::string gApp, gUser, sSettings = "gunsmith3d_settings.json", sAutosave = "gunsmith3d_autosave.json";
std::string sGuns, sMaps, sTex, sParts, sNpcs, sCarts, sChar, sGroups;
void copyIfMissing(const std::string& from, const std::string& to) {
    std::error_code ec;
    if (fs::exists(to, ec) || !fs::exists(from, ec)) return;
    fs::copy_file(from, to, ec);
}
}  // namespace

void initAppFolder() {
    std::string doc = documentsFolder();
    gApp = doc.empty() ? "" : doc + "/OAB Foundry/Gunsmith";
    std::error_code ec;
    if (!gApp.empty()) fs::create_directories(gApp, ec);
    if (ec) gApp.clear();   // (can't write there: everything stays in the game folder, as before)
    sSettings = gApp.empty() ? "gunsmith3d_settings.json" : gApp + "/settings.json";
    sAutosave = gApp.empty() ? "gunsmith3d_autosave.json" : gApp + "/autosave.json";
    if (!gApp.empty()) {   // earlier versions kept these next to the game: carried over the first time
        copyIfMissing("gunsmith3d_settings.json", sSettings);
        copyIfMissing("gunsmith3d_autosave.json", sAutosave);
    }
    setCrashFolder(gApp.empty() ? "user/crashes" : gApp + "/Crashes");
}
std::string appFolder() { return gApp; }
const char* settingsPath() { return sSettings.c_str(); }
const char* autosavePath() { return sAutosave.c_str(); }
std::string defaultUserFolder() { return gApp.empty() ? "user" : gApp + "/User"; }
std::string userFolder() { return gUser; }
void setUserFolder(const std::string& root) {
    gUser = root;
    sGuns = root + "/Blueprints"; sMaps = root + "/Levels"; sTex = root + "/Textures"; sParts = root + "/Parts";
    sNpcs = root + "/NPCs"; sCarts = root + "/Rounds"; sGroups = root + "/Groups"; sChar = root + "/Characters/character.json";
    GUNS_DIR = sGuns.c_str(); MAPS_DIR = sMaps.c_str(); TEXTURES_DIR = sTex.c_str(); PARTS_DIR = sParts.c_str();
    NPCS_DIR = sNpcs.c_str(); CARTS_DIR = sCarts.c_str(); GROUPS_DIR = sGroups.c_str(); CHARACTER_FILE = sChar.c_str();
    setUserCartridgesDir(sCarts);
}
bool changeUserFolder(const std::string& root, std::string& err) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec || !fs::is_directory(root, ec)) { err = "The game can't make or write to that folder."; return false; }
    if (!gUser.empty() && fs::is_directory(gUser, ec) && fs::weakly_canonical(gUser, ec) != fs::weakly_canonical(root, ec))
        fs::copy(gUser, root, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);   // (your files come along; nothing is overwritten)
    if (ec) { err = "Some files couldn't be copied there: " + ec.message(); }
    setUserFolder(root);
    setupUserFolders();
    return true;
}

void setupUserFolders() {
    std::error_code ec;
    for (const char* d : {GUNS_DIR, MAPS_DIR, TEXTURES_DIR, PARTS_DIR, NPCS_DIR, GROUPS_DIR, CARTS_DIR}) fs::create_directories(d, ec);
    fs::create_directories(fs::path(CHARACTER_FILE).parent_path(), ec);
    // files from where earlier versions kept them, in the game folder: moved in (never over a file already there)
    const char* pairs[][2] = {{"blueprints", GUNS_DIR}, {"maps", MAPS_DIR}, {"user/guns", GUNS_DIR}, {"user/maps", MAPS_DIR},
                              {"user/textures", TEXTURES_DIR}, {"user/parts", PARTS_DIR}, {"user/npcs", NPCS_DIR},
                              {"user/groups", GROUPS_DIR}, {"user/cartridges", CARTS_DIR}};
    for (auto& p : pairs)
        if (fs::weakly_canonical(p[0], ec) != fs::weakly_canonical(p[1], ec)) moveOver(p[0], p[1]);
    if (fs::exists("user/character.json", ec) && !fs::exists(CHARACTER_FILE, ec)) {
        fs::copy_file("user/character.json", CHARACTER_FILE, ec);
        if (!ec) fs::remove("user/character.json", ec);
    }
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
    toast("Saved " + name + " to your Blueprints folder.");
    return true;
}
