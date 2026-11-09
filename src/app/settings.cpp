// Gunsmith3D - Player settings (see settings.hpp).
#include "app/settings.hpp"
#include "app/app.hpp"
#include "app/audio.hpp"
#include "json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

using json = nlohmann::json;
Settings CFG;

const char* settingsFile() { return "gunsmith3d_settings.json"; }

void loadSettings() {
    std::ifstream in(settingsFile());
    if (!in) return;
    json j;
    try { in >> j; } catch (...) { return; }
    Settings d;  // defaults for anything missing
    auto g = [&](const char* k, auto def) { return j.contains(k) ? j[k].get<decltype(def)>() : def; };
    try {
        CFG.windowMode = (WindowMode)std::clamp(g("windowMode", (int)d.windowMode), 0, 2);
        CFG.resW = std::max(800, g("resW", d.resW)); CFG.resH = std::max(600, g("resH", d.resH));
        CFG.monitor = std::max(0, g("monitor", d.monitor));
        CFG.vsync = g("vsync", d.vsync);
        CFG.fpsCap = std::max(0, g("fpsCap", d.fpsCap));
        CFG.perfOverlay = std::clamp(g("perfOverlay", d.perfOverlay), 0, 2);
        CFG.uiScale = std::clamp(g("uiScale", d.uiScale), 0.75f, 2.0f);
        CFG.uiScaleAuto = g("uiScaleAuto", std::fabs(CFG.uiScale - 1.0f) < 0.01f);  // (older settings: a scale someone set stays set)
        CFG.msaa = g("msaa", d.msaa);
        CFG.backdrop = (Backdrop)std::clamp(g("backdrop", (int)d.backdrop), 0, 4);
        CFG.solidColor = g("solidColor", d.solidColor) & 0xFFFFFFu;
        CFG.grid = g("grid", d.grid);
        CFG.workbench = g("workbench", d.workbench);
        CFG.volMaster = std::clamp(g("volMaster", d.volMaster), 0.f, 1.f);
        CFG.volGunfire = std::clamp(g("volGunfire", d.volGunfire), 0.f, 1.f);
        CFG.volMechanical = std::clamp(g("volMechanical", d.volMechanical), 0.f, 1.f);
        CFG.volInterface = std::clamp(g("volInterface", d.volInterface), 0.f, 1.f);
        CFG.muteUnfocused = g("muteUnfocused", d.muteUnfocused);
        CFG.units = (Units)std::clamp(g("units", (int)d.units), 0, 1);
        CFG.stats = (StatsDetail)std::clamp(g("stats", (int)d.stats), 0, 2);
        CFG.tooltips = g("tooltips", d.tooltips);
        CFG.hints = g("hints", d.hints);
        CFG.colorblind = g("colorblind", d.colorblind);
        CFG.snap = std::max(0.0, g("snap", d.snap));
        CFG.autosave = (AutosaveEvery)std::clamp(g("autosave", (int)d.autosave), 0, 3);
        CFG.orbitMiddle = g("orbitMiddle", d.orbitMiddle);
        CFG.zoomSpeed = std::clamp(g("zoomSpeed", d.zoomSpeed), 0.25f, 3.f);
        CFG.invertZoom = g("invertZoom", d.invertZoom);
        CFG.slowOnShot = g("slowOnShot", d.slowOnShot);
        CFG.slowSpeed = std::clamp(g("slowSpeed", d.slowSpeed), 0.005f, 1.f);
        CFG.slowWindowMs = std::clamp(g("slowWindowMs", d.slowWindowMs), 10.f, 1000.f);
        CFG.benchSpeed = std::clamp(g("benchSpeed", d.benchSpeed), 0.01f, 1.f);
        CFG.relRounds = std::clamp(g("relRounds", d.relRounds), 5, 1000);
        CFG.maxLooseMags = std::clamp(g("maxLooseMags", d.maxLooseMags), 1, 20);
        CFG.magLife = std::clamp(g("magLife", d.magLife), 5.f, 600.f);
        CFG.mouseSens = std::clamp(g("mouseSens", d.mouseSens), 0.05f, 10.f);
        CFG.invertMouse = g("invertMouse", d.invertMouse);
        CFG.fov = std::clamp(g("fov", d.fov), 50.f, 110.f);
        if (j.contains("keys") && j["keys"].is_object())
            for (auto& [k, v] : j["keys"].items()) if (v.is_number_integer()) CFG.keys[k] = v.get<int>();
        if (j.contains("tutorialsDone") && j["tutorialsDone"].is_string()) CFG.tutorialsDone = j["tutorialsDone"].get<std::string>();
        if (j.contains("eulaAgreed") && j["eulaAgreed"].is_string()) CFG.eulaAgreed = j["eulaAgreed"].get<std::string>();
    } catch (...) {}
}

void saveSettings() {
    json j = {
        {"windowMode", (int)CFG.windowMode}, {"resW", CFG.resW}, {"resH", CFG.resH}, {"monitor", CFG.monitor},
        {"vsync", CFG.vsync}, {"fpsCap", CFG.fpsCap}, {"perfOverlay", CFG.perfOverlay}, {"uiScale", CFG.uiScale}, {"uiScaleAuto", CFG.uiScaleAuto}, {"msaa", CFG.msaa},
        {"backdrop", (int)CFG.backdrop}, {"solidColor", CFG.solidColor}, {"grid", CFG.grid}, {"workbench", CFG.workbench},
        {"volMaster", CFG.volMaster}, {"volGunfire", CFG.volGunfire}, {"volMechanical", CFG.volMechanical},
        {"volInterface", CFG.volInterface}, {"muteUnfocused", CFG.muteUnfocused},
        {"units", (int)CFG.units}, {"stats", (int)CFG.stats}, {"tooltips", CFG.tooltips}, {"hints", CFG.hints},
        {"colorblind", CFG.colorblind}, {"snap", CFG.snap}, {"autosave", (int)CFG.autosave},
        {"orbitMiddle", CFG.orbitMiddle}, {"zoomSpeed", CFG.zoomSpeed}, {"invertZoom", CFG.invertZoom},
        {"slowOnShot", CFG.slowOnShot}, {"slowSpeed", CFG.slowSpeed}, {"slowWindowMs", CFG.slowWindowMs},
        {"benchSpeed", CFG.benchSpeed}, {"relRounds", CFG.relRounds}, {"maxLooseMags", CFG.maxLooseMags}, {"magLife", CFG.magLife},
        {"mouseSens", CFG.mouseSens}, {"invertMouse", CFG.invertMouse}, {"fov", CFG.fov}, {"keys", CFG.keys}, {"tutorialsDone", CFG.tutorialsDone}, {"eulaAgreed", CFG.eulaAgreed}};
    std::ofstream(settingsFile()) << j.dump(2);
}

void applyDisplay() {
    int mc = std::max(1, GetMonitorCount());
    int m = std::clamp(CFG.monitor, 0, mc - 1);
    // leave any special mode first, so each mode is entered from a plain window
    if (IsWindowFullscreen()) ToggleFullscreen();
    if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();
    Vector2 mp = GetMonitorPosition(m);
    int mw = GetMonitorWidth(m), mh = GetMonitorHeight(m);
    int w = std::min(CFG.resW, mw > 0 ? mw : CFG.resW), h = std::min(CFG.resH, mh > 0 ? mh : CFG.resH);
    switch (CFG.windowMode) {
    case WindowMode::Windowed:
        SetWindowSize(w, h);
        SetWindowPosition((int)mp.x + std::max(0, (mw - w) / 2), (int)mp.y + std::max(0, (mh - h) / 2));
        break;
    case WindowMode::Borderless:
        SetWindowPosition((int)mp.x + 40, (int)mp.y + 40);  // onto the chosen monitor, then fill it
        ToggleBorderlessWindowed();
        break;
    case WindowMode::Fullscreen:
        SetWindowPosition((int)mp.x + 40, (int)mp.y + 40);
        SetWindowSize(CFG.resW, CFG.resH);
        ToggleFullscreen();
        break;
    }
    if (CFG.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
    SetTargetFPS(CFG.fpsCap);
}

void applyAudio() {
    if (!IsAudioDeviceReady()) return;
    bool mute = CFG.muteUnfocused && !IsWindowFocused();
    SetMasterVolume(mute ? 0.f : CFG.volMaster);
    if (!SFX.ok) return;
    SetSoundVolume(SFX.bang, CFG.volGunfire);
    SetSoundVolume(SFX.clack, CFG.volMechanical);
    SetSoundVolume(SFX.click, CFG.volMechanical);
    SetSoundVolume(SFX.tick, CFG.volInterface);
}
