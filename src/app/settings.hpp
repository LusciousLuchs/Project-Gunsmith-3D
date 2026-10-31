// Gunsmith3D - Player settings: display, audio, interface, designer, test bench, controls.
// Saved to gunsmith3d_settings.json next to the program. No setting changes the physics: the simulation always
// runs at full rate with the same rules; settings only change how it is shown, heard and controlled.
#pragma once
#include <map>
#include <string>

enum class WindowMode { Windowed, Borderless, Fullscreen };
enum class Units { Metric, Imperial };
enum class StatsDetail { Minimal, Standard, Engineering };
enum class AutosaveEvery { Change, Minute, FiveMinutes, Off };
enum class Backdrop { Blueprint, Paper, Dark, Studio, Solid };

struct Settings {
    // display
    WindowMode windowMode = WindowMode::Windowed;
    int resW = 1440, resH = 900;         // window size (windowed) or video mode (fullscreen)
    int monitor = 0;
    bool vsync = true;
    int fpsCap = 60;                     // 0 = no cap
    float uiScale = 1.0f;
    bool msaa = true;                    // applies after a restart
    Backdrop backdrop = Backdrop::Blueprint;  // the workspace behind the gun
    unsigned solidColor = 0x2E3440;      // RGB, for Backdrop::Solid
    bool grid = true;                    // drafting grid behind the parts in the designer
    // audio (0..1)
    float volMaster = 0.8f, volGunfire = 1.0f, volMechanical = 1.0f, volInterface = 0.7f;
    bool muteUnfocused = true;
    // interface
    Units units = Units::Metric;
    StatsDetail stats = StatsDetail::Minimal;
    bool tooltips = true;
    bool hints = true;                   // the short how-to line over the 3D view
    bool colorblind = false;             // blue/orange instead of green/red for good/bad
    // designer
    double snap = 0.5;                   // mm, 0 = off
    AutosaveEvery autosave = AutosaveEvery::Change;
    bool orbitMiddle = false;            // orbit with the middle button and pan with the right, instead of the reverse
    float zoomSpeed = 1.0f;
    bool invertZoom = false;
    // test bench
    bool slowOnShot = true;
    float slowSpeed = 0.02f;             // fraction of real time while slowed
    float slowWindowMs = 150;
    float benchSpeed = 1.0f;
    int relRounds = 50;
    // first person
    float mouseSens = 1.0f;              // 1 = 0.12 degrees per pixel
    bool invertMouse = false;
    float fov = 80;                      // vertical field of view, degrees
    // controls: action id -> key (raylib KeyboardKey); missing entries use the defaults
    std::map<std::string, int> keys;
    std::string tutorialsDone;           // ids of finished tutorials, comma separated
};
extern Settings CFG;

void loadSettings();
void saveSettings();
void applyDisplay();          // window mode, size, monitor, vsync, frame cap
void applyAudio();            // per frame: volumes and mute when unfocused
const char* settingsFile();
