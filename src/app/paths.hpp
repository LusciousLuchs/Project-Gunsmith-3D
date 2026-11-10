// Gunsmith3D - where the player's own files live. On Windows: Documents/OAB Foundry/Gunsmith/User (or any folder the
// player picks in Settings > Files), with the settings and the autosave one level up in Documents/OAB Foundry/Gunsmith.
// Elsewhere (and before setUserFolder): the game folder's user/. Inside it:
//   Blueprints  your guns (.json)            Levels      maps (.gsmap)
//   Rounds      your cartridges (one .json each)          Characters  your player character
//   NPCs        NPC designs                  Textures    pictures for parts and map blocks
//   Parts       parts and assemblies to drop into any gun Groups      groups of map objects
// The names below point at those folders once setUserFolder has run (they're plain text for the C file functions).
#pragma once
#include <string>

inline const char* GUNS_DIR = "user/guns";
inline const char* MAPS_DIR = "user/maps";
inline const char* TEXTURES_DIR = "user/textures";
inline const char* PARTS_DIR = "user/parts";        // your parts library (app/pieces.cpp)
inline const char* NPCS_DIR = "user/npcs";          // your NPC designs (app/npcedit.cpp)
inline const char* CARTS_DIR = "user/cartridges";   // your cartridges, one .json each
inline const char* CHARACTER_FILE = "user/character.json";   // your player character (the NPC editor in its character mode)
inline const char* GROUPS_DIR = "user/groups";      // groups of map objects saved as one, to place again (app/mapedit.cpp)

void initAppFolder();                  // first thing: where the settings and autosave live (Documents/OAB Foundry/Gunsmith on Windows)
std::string appFolder();               // that folder ("" = the game folder)
const char* settingsPath();
const char* autosavePath();
std::string defaultUserFolder();       // Documents/OAB Foundry/Gunsmith/User on Windows, else user/ in the game folder
void setUserFolder(const std::string& root);   // point every folder above into this one
std::string userFolder();
// Use a different user folder: what's in the current one is copied over (nothing is overwritten), then the game uses it
bool changeUserFolder(const std::string& root, std::string& err);
std::string documentsFolder();         // (sysfiles.cpp) Windows' Documents; "" elsewhere
bool pickFolder(std::string& out, const char* title);   // (sysfiles.cpp) the system's choose-a-folder box; false if cancelled
void setCrashFolder(const std::string& dir);            // (sysfiles.cpp) where crash reports go
void setupUserFolders();                         // makes the folders, and moves files over from older places (user/guns, blueprints/, ...)
std::string safeFileName(const std::string& s);  // a name Windows accepts as a file name
bool saveBlueprint();                            // the design to Blueprints/<name>.json (the File tab's name, else the design's)
