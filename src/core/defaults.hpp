// Gunsmith3D - the game's built-in defaults, set by the owner in the developer build: the built-in maps (a career
// property's building, the practice range) and a new player's Settings. Embedded from assets/defaults by
// tools/embed_defaults.py; the developer build reads its own Defaults folder first, so changes show at once.
#pragma once
#include <string>

namespace defaults {
// (developer build) the folder whose maps/<kind>.gsmap and settings.json come before the built-in ones ("" none)
void setOverrideDir(const std::string& dir);
std::string overrideDir();
// a built-in map's text ("garage", "shack", "retail", "warehouse", "range"), or "" for the one made in code
std::string mapText(const std::string& kind);
// a new player's settings (settings.json's text), or ""
std::string settingsText();
}  // namespace defaults
