// Gunsmith3D - where the player's own files live, inside the game folder:
//   user/guns      blueprints (.json)
//   user/maps      ranges (.gsmap)
//   user/textures  pictures to paint parts and map blocks with (.png, .jpg)
#pragma once
#include <string>

inline const char* GUNS_DIR = "user/guns";
inline const char* MAPS_DIR = "user/maps";
inline const char* TEXTURES_DIR = "user/textures";

void setupUserFolders();                         // makes the folders, and moves files over from the old blueprints/ and maps/
std::string safeFileName(const std::string& s);  // a name Windows accepts as a file name
bool saveBlueprint();                            // the design to user/guns/<name>.json (the File tab's name, else the design's)
