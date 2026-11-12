// Gunsmith3D - updates itself from the GitHub releases (Windows only).
// At start, a background thread asks GitHub for the newest release. If it's newer than this build, it downloads the
// zip, unpacks it, and puts the new files in the game folder: the running .exe is renamed out of the way (Windows
// lets a running program be renamed, not overwritten) and the new one copied in. The player's own files (the user
// folder, settings) are never touched. The new version runs the next time the game starts, or at once with Restart.
// The release itself is made by .github/workflows/release.yml when the version goes up.
#pragma once
#include <string>

namespace update {
enum class State { Off, Checking, UpToDate, Downloading, Installing, Installed, Failed };
struct Status {
    State state = State::Off;
    std::string latest;   // the newest release's version ("0.6.2-alpha")
    std::string note;     // what went wrong, in plain words
};
constexpr const char* REPO = "LusciousLuchs/Gunsmith3D-Releases";   // the downloads page (if there is one)
constexpr const char* CODE_REPO = "LusciousLuchs/Project-Gunsmith-3D";   // the game's own repository (public)
void start();             // check (and if newer, download and install) in the background; nothing on other systems
Status status();
void checkNow();          // Check for updates (a button): ask again now
bool restart();           // start the new version and quit this one
void cleanup();           // remove the old .exe left by the last update (call at start)
// a.b.c[-stage] newer than b? (numbers only; the stage doesn't count)
bool newer(const std::string& a, const std::string& b);
}  // namespace update
