// Gunsmith3D - where the player's own files live, inside the game folder:
//   user/guns      blueprints (.json)
//   user/maps      ranges (.gsmap)
//   user/textures  pictures to paint parts and map blocks with (.png, .jpg)
//   user/parts     your parts and assemblies, to drop into any gun (.json)
#pragma once
#include <string>

inline const char* GUNS_DIR = "user/guns";
inline const char* MAPS_DIR = "user/maps";
inline const char* TEXTURES_DIR = "user/textures";
inline const char* PARTS_DIR = "user/parts";        // your parts library (app/pieces.cpp)
inline const char* NPCS_DIR = "user/npcs";          // your NPC designs (app/npcedit.cpp)
inline const char* GROUPS_DIR = "user/groups";      // groups of map objects saved as one, to place again (app/mapedit.cpp)

void setupUserFolders();                         // makes the folders, and moves files over from the old blueprints/ and maps/
std::string safeFileName(const std::string& s);  // a name Windows accepts as a file name
bool saveBlueprint();                            // the design to user/guns/<name>.json (the File tab's name, else the design's)
