// Gunsmith3D - the player's own textures: pictures in user/textures, for gun parts and map blocks.
#pragma once
#include "raylib.h"
#include <string>
#include <vector>

void scanUserTextures();                              // find the pictures in user/textures (and make them map materials)
const std::vector<std::string>& userTextureFiles();   // file names, sorted
Texture2D userTexture(const std::string& file);       // loaded once; a checkerboard if the file is missing or unreadable
// The game's own textures (real gun finishes), always there: saved in blueprints as "builtin:<id>"; userTexture loads them.
struct BuiltinTexture { std::string file, name; double scale; };   // file: "builtin:<id>"; scale: a natural repeat (mm)
const std::vector<BuiltinTexture>& builtinTextures();
std::string textureName(const std::string& file);      // what to call it in the game: its name, or the file name
bool isImageFile(const std::string& path);
std::string importTexture(const std::string& path);   // copy a picture into user/textures; returns its file name, "" on failure
void handleDroppedFiles();                            // pictures dropped on the window are imported
bool pickImageFile(std::string& out);                 // the system's open-file box (Windows); false if cancelled or not available
void openTexturesFolder();
void installCrashHandler();                           // Windows: a crash leaves a report in user/crashes
void openFolder(const std::string& path);                // in the system's file browser
