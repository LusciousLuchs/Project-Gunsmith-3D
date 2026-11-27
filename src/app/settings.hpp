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
    int perfOverlay = 0;
    int lastFullMode = 1;
    std::string userDir;                 // where your files are kept ("": the default, Documents/OAB Foundry/Gunsmith/User on Windows)                // what F11 goes to from a window: 1 borderless, 2 fullscreen (the last one used)
    int mapUnit = 0;                     // the Level editor's lengths: 0 m, 1 cm, 2 ft (and inches), 3 in, 4 yd
    bool shadows = true;                 // sun shadows on maps (Settings > Display)
    bool poseRings = true, poseArrows = true;
    bool panelAll = false;               // the designer's part panel: every setting (false: the everyday ones, the rest under More settings)
    bool hoverHelp = true;               // the designer: what a click or drag will do, by the mouse after it rests a moment
    bool editArrows = true;              // the designer, NPC editor and level editor: move arrows on picked corners and points (their toolbars' switch)
    bool editRings = true;               // the NPC editor and character maker: the picked bone's rotation rings (its toolbar's switch)   // Pose and animations: the rotation rings and move arrows on a picked point
    bool fpArms = true;                  // the range: your arms and hands on the gun
    bool devTools = false;               // developer tools (the owner's, Settings > Files): set the game's built-in maps and new players' settings
    int paperHelp = 0;                   // career counter paperwork: 0 by hand, 1 filled in and problems spelled out, 2 done for you
    bool cheatAutoFeed = false;          // cheats (Settings > Cheats): sim::cheats() follows these
    bool cheatBruteForce = false;
    bool poseCharacter = true;           // Pose mode: your character (if you made one) holds the gun, not the mannequin                 // the performance overlay (F3): 0 off, 1 frame rate, 2 full details
    float uiScale = 1.0f;
    bool uiScaleAuto = true;             // pick the scale from the screen's height (until you choose one)
    bool msaa = true;                    // applies after a restart
    Backdrop backdrop = Backdrop::Blueprint;  // the workspace behind the gun
    unsigned solidColor = 0x2E3440;      // RGB, for Backdrop::Solid
    bool grid = true;                    // drafting grid behind the parts in the designer
    bool workbench = true;               // a bench, a few tools and a ruler under the gun in the designer, for scale
    // audio (0..1)
    float volMaster = 0.8f, volGunfire = 1.0f, volMechanical = 1.0f, volInterface = 0.7f, volRadio = 0.8f;
    bool titleSplash = true;       // the game's title splash at start (after the studio's)
    bool menuPicture = true;       // the main menu over the workshop picture (off: your last gun turning in 3D)
    std::string seenVersion;       // the version whose "What's new" was last looked at (a new one opens it)
    bool muteUnfocused = true;
    bool autoUpdate = true;              // fetch and install new versions from GitHub at start (Windows)
    // interface
    Units units = Units::Metric;
    StatsDetail stats = StatsDetail::Minimal;
    bool tooltips = true;
    bool hints = true;                   // the short how-to line over the 3D view
    bool colorblind = false;             // blue/orange instead of green/red for good/bad
    // designer
    double snap = 0.5;                   // mm, the snap step (0 in old settings files = off)
    bool snapOn = true;
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
    int maxLooseMags = 6;                // magazines lying out on the bench at once (the oldest is cleared away)
    float magLife = 20;                  // seconds a magazine out of the gun lies untouched before it's cleared away
    // first person
    float mouseSens = 1.0f;              // 1 = 0.12 degrees per pixel
    bool invertMouse = false;
    float fov = 80;                      // vertical field of view, degrees
    // controls: action id -> key (raylib KeyboardKey); missing entries use the defaults
    std::map<std::string, int> keys;
    std::string tutorialsDone;           // ids of finished tutorials, comma separated
    std::string eulaAgreed;              // the fingerprint of the agreement text agreed to (empty: not yet)
};
extern Settings CFG;

void loadSettings();
void saveSettings();
inline bool devBuild() { return CFG.devTools; }   // the developer tools are on
void applyDevTools();                         // (after loading settings, or switching them) your own Defaults folder comes first
void applyDisplay();          // window mode, size, monitor, vsync, frame cap
void toggleFullscreenKey();   // F11: window <-> full screen
void applyAudio();            // per frame: volumes and mute when unfocused
const char* settingsFile();
// the cheats in the settings handed to the simulation (every gun built from now on); the gun in your hands too
void applyCheats();
