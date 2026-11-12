// Gunsmith3D - Main menu, pause menu, settings, and choosing a gun for the test bench.
#include "app/paths.hpp"
#include "app/app.hpp"
#include "app/npcedit.hpp"
#include "app/careermode.hpp"
#include "app/sysinfo.hpp"
#include "app/usertex.hpp"
#include "app/audio.hpp"
#include "app/keys.hpp"
#include "app/mapedit.hpp"
#include "app/settings.hpp"
#include "app/updater.hpp"
#include "app/view.hpp"
#include "ui/ui.hpp"
#include "app/range.hpp"
#include "map.hpp"
#include "version.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static float S() { return CFG.uiScale; }
static const char* DISCORD_URL = "https://discord.gg/nHpYEKRjdn";   // the community server (a permanent invite)

// A big text-only menu entry: dim until hovered, then bright with a short amber bar beside it.
static bool menuItem(const char* label, bool enabled = true, const char* note = nullptr) {
    ImGui::PushID(label);
    float h = 40 * S(), w = 360 * S();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##m", {w, h}) && enabled;
    bool hov = ImGui::IsItemHovered() && enabled;
    auto* dl = ImGui::GetWindowDrawList();
    if (hov) dl->AddRectFilled({p.x, p.y + 10 * S()}, {p.x + 3 * S(), p.y + h - 10 * S()}, ImGui::GetColorU32(colAccent()));
    ImGui::PushFont(gFontBold, 22.0f);
    ImU32 col = !enabled ? IM_COL32(110, 115, 125, 255) : hov ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 204, 212, 255);
    float fs = ImGui::GetFontSize();
    dl->AddText({p.x + 16 * S(), p.y + (h - fs) / 2}, col, label);
    float lw = ImGui::CalcTextSize(label).x;
    ImGui::PopFont();
    if (note) {
        float nfs = ImGui::GetFontSize();
        dl->AddText({p.x + 16 * S() + lw + 12 * S(), p.y + (h - nfs) / 2 + 2 * S()}, IM_COL32(120, 126, 136, 255), note);
    }
    if (clicked) uiTick();
    ImGui::PopID();
    return clicked;
}

static void centerNext(float w, float h) {
    ImGui::SetNextWindowPos({GetScreenWidth() / 2.f, GetScreenHeight() / 2.f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({w, h});
}
static void dimBackground(float a = 0.55f) {
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, {(float)GetScreenWidth(), (float)GetScreenHeight()}, IM_COL32(8, 9, 11, (int)(a * 255)));
}
static constexpr ImGuiWindowFlags PLAIN = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

// ---------------------------------------------------------------- what's new (from CHANGELOG.md, built in)
#include "changelog.inc"
namespace {
struct Release { std::string title; std::vector<std::string> lines; };
const std::vector<Release>& releases() {
    static std::vector<Release> out;
    if (!out.empty()) return out;
    std::string line;
    const char* p = CHANGELOG_TEXT;
    while (*p) {
        const char* e = std::strchr(p, '\n');
        line.assign(p, e ? e - p : std::strlen(p));
        p = e ? e + 1 : p + line.size();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("## ", 0) == 0) { out.push_back({line.substr(3), {}}); continue; }
        if (out.empty()) continue;   // (the explanation of version numbers at the top)
        std::string t;
        for (size_t i = 0; i < line.size(); i++) {   // markdown's ** and ` are left out
            if (line[i] == '`') continue;
            if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '*') { i++; continue; }
            t += line[i];
        }
        out.back().lines.push_back(t);
    }
    return out;
}
void releaseText(const Release& r) {
    for (auto& l : r.lines) {
        if (l.empty()) continue;
        if (l.rfind("### ", 0) == 0) {
            ImGui::Dummy({0, 4 * S()});
            ImGui::PushFont(gFontBold, 15.0f);
            ImGui::TextUnformatted(l.c_str() + 4);
            ImGui::PopFont();
        } else if (l.rfind("- ", 0) == 0) {
            ImGui::Bullet(); ImGui::SameLine();
            ImGui::TextWrapped("%s", l.c_str() + 2);
        } else if (l.rfind("  ", 0) == 0) {   // a bullet's continued line, or one under it
            ImGui::Indent(18 * S());
            std::string t = l.substr(l.find_first_not_of(' '));
            if (t.rfind("- ", 0) == 0) { ImGui::Bullet(); ImGui::SameLine(); t = t.substr(2); }
            ImGui::TextWrapped("%s", t.c_str());
            ImGui::Unindent(18 * S());
        } else ImGui::TextWrapped("%s", l.c_str());
    }
}
// The box in the top right corner of the main menu: this version's changes, and the ones before on request
void whatsNew() {
    const auto& R = releases();
    if (R.empty()) return;
    float w = std::min(440 * S(), GetScreenWidth() * 0.36f), m = 24 * S();
    static bool older = false;
    ImGui::SetNextWindowPos({GetScreenWidth() - w - m, m});
    ImGui::SetNextWindowSize({w, older ? GetScreenHeight() - 2 * m : 0});
    ImGui::SetNextWindowSizeConstraints({w, 0}, {w, GetScreenHeight() * (older ? 1.0f : 0.62f) - 2 * m});
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::Begin("##whatsnew", nullptr, PLAIN | (older ? 0 : ImGuiWindowFlags_AlwaysAutoResize));
    ImGui::PushFont(gFontBold, 18.0f);
    ImGui::Text("What's new in %s", R[0].title.c_str());
    ImGui::PopFont();
    ImGui::Separator();
    releaseText(R[0]);
    ImGui::Dummy({0, 6 * S()});
    if (ImGui::SmallButton(older ? "Hide earlier updates" : "Earlier updates")) older = !older;
    if (older)
        for (size_t k = 1; k < R.size(); k++) {
            ImGui::Dummy({0, 8 * S()});
            ImGui::PushFont(gFontBold, 16.0f);
            ImGui::TextUnformatted(R[k].title.c_str());
            ImGui::PopFont();
            ImGui::Separator();
            releaseText(R[k]);
        }
    ImGui::End();
}
}  // namespace

// ---------------------------------------------------------------- about
// The game's version, who made it, and what it runs on: raylib, the OpenGL it uses and what this PC's driver reports
void agreementTextPublic();   // (ui/intro.cpp) the agreement, laid out
void aboutWindow() {
    dimBackground(0.6f);
    centerNext(560 * S(), 0);
    ImGui::Begin("##about", nullptr, PLAIN | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushFont(gFontBold, 24.0f);
    ImGui::TextUnformatted("GUNSMITH3D");
    ImGui::PopFont();
    ImGui::TextDisabled("Firearm design workshop");
    ImGui::Dummy({0, 6 * S()});
    auto row = [](const char* k, const std::string& v) {
        ImGui::TextDisabled("%s", k);
        ImGui::SameLine(190 * S());
        ImGui::TextWrapped("%s", v.empty() ? "(not known)" : v.c_str());
    };
    row("Version", version::full());
    row("Studio", "OAB Foundry");
    row("Developer", "The One Armed Bandit");
    ImGui::Dummy({0, 4 * S()});
    ImGui::SeparatorText("What it runs on");
    const GpuInfo& g = gpuInfo();
    row("raylib", RAYLIB_VERSION);
    row("OpenGL", requestedOpenGL());
    row("Your driver's OpenGL", g.version);
    row("Shading language", g.glsl.empty() ? "" : "GLSL " + g.glsl);
    row("Graphics card", g.renderer.empty() ? "" : g.renderer + (g.vendor.empty() ? "" : " (" + g.vendor + ")"));
    row("Dear ImGui", IMGUI_VERSION);
    ImGui::Dummy({0, 4 * S()});
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Licences for the libraries and fonts the game uses are in the licenses folder next to the game.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0, 6 * S()});
    static bool showAgreement = false;
    if (ImGui::Button(showAgreement ? "Hide the license agreement" : "Read the license agreement", {-1, 0})) showAgreement = !showAgreement;
    if (tipHover()) ImGui::SetTooltip("The agreement you accepted, as built into the game (it isn't read from a file, so it can't be changed).");
    if (showAgreement) {   // the agreement, from inside the game itself (ui/eula.inc), never from a file next to it
        ImGui::BeginChild("##agreement", {0, 300 * S()}, ImGuiChildFlags_Borders);
        agreementTextPublic();
        ImGui::EndChild();
    }
    ImGui::Dummy({0, 4 * S()});
    float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    if (ImGui::Button("Copy these details", {w3, 0})) { SetClipboardText(aboutText().c_str()); toast("Copied: paste them into a bug report."); }
    ImGui::SameLine();
    if (ImGui::Button("Open licenses folder", {w3, 0})) openFolder("licenses");
    ImGui::SameLine();
    if (ImGui::Button("Close", {w3, 0}) || (IsKeyPressed(KEY_ESCAPE) && A.modalAtFrameStart)) { uiTick(); A.showAbout = false; }
    ImGui::End();
}

// ---------------------------------------------------------------- main menu
void mainMenu() {
    if (A.showSettings) { settingsWindow(); return; }
    if (A.showBenchPicker) { benchPicker(); return; }
    if (A.showTutorials) { tutorialsWindow(); return; }
    if (A.showRangePicker) { rangePicker(); return; }
    if (A.showAbout) { aboutWindow(); return; }
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)GetScreenWidth(), (float)GetScreenHeight()});
    ImGui::SetNextWindowBgAlpha(0);
    ImGui::Begin("##menu", nullptr, PLAIN | ImGuiWindowFlags_NoBringToFrontOnFocus);
    float x = 72 * S(), y = GetScreenHeight() * 0.22f;   // (high enough that the whole list clears the version line)
    ImGui::SetCursorPos({x, y});
    ImGui::PushFont(gFontBold, 46.0f);
    ImGui::TextUnformatted("GUNSMITH3D");
    ImGui::PopFont();
    ImGui::SetCursorPosX(x + 2 * S());
    ImGui::TextDisabled("Firearm design workshop");
    ImGui::Dummy({0, 30 * S()});
    ImGui::SetCursorPosX(x - 16 * S());
    ImGui::BeginGroup();
    if (menuItem("Tutorials", true, tutorialsDoneCount() == 0 ? "start here" : nullptr)) A.showTutorials = true;
    if (menuItem("Career", true, careerExists() ? "carry on" : "start your gun business")) enterCareer();
    if (menuItem("Sandbox")) enterWorkshop(false);
    if (menuItem("Firing range")) A.showRangePicker = true;
    if (menuItem("Your character", true, "who holds your guns")) enterCharacterEditor();
    {   // the tools for making places to shoot and things to shoot at
        static bool mapTools = false;
        if (menuItem(mapTools ? "Map editing tools  -" : "Map editing tools  +")) mapTools = !mapTools;
        if (mapTools) {
            ImGui::Indent(22 * S());
            if (menuItem("Level editor", true, "build maps")) enterMapEditor();
            if (menuItem("NPC editor", true, "build enemies")) enterNpcEditor();
            ImGui::Unindent(22 * S());
        }
    }
    if (menuItem("Test bench")) A.showBenchPicker = true;
    if (menuItem("Settings")) A.showSettings = true;
    if (menuItem("Discord", true, "join the community")) OpenURL(DISCORD_URL);
    if (menuItem("About", true, "version, graphics")) A.showAbout = true;
    if (menuItem("Quit")) A.quit = true;
    ImGui::EndGroup();
    ImGui::SetCursorPos({x, GetScreenHeight() - 34 * S()});
    ImGui::TextDisabled("%s", version::text());
    {   // updates: fetched from GitHub and installed in the background
        update::Status u = update::status();
        ImGui::SameLine(0, 14 * S());
        switch (u.state) {
            case update::State::Checking: ImGui::TextDisabled("Checking for updates..."); break;
            case update::State::UpToDate: ImGui::TextDisabled("Up to date"); ImGui::SameLine(); if (ImGui::SmallButton("Check again")) update::checkNow(); break;
            case update::State::Downloading: ImGui::TextDisabled("Downloading %s...", u.latest.c_str()); break;
            case update::State::Installing: ImGui::TextDisabled("Installing %s...", u.latest.c_str()); break;
            case update::State::Installed:
                ImGui::TextColored({0.55f, 0.85f, 0.55f, 1}, "Updated to %s: it starts next time.", u.latest.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Restart now") && update::restart()) A.quit = true;
                break;
            case update::State::Failed:
                ImGui::TextDisabled("Update: %s", u.note.c_str());
                ImGui::SameLine(); if (ImGui::SmallButton("Try again")) update::checkNow();
                break;
            default: if (ImGui::SmallButton("Check for updates")) update::checkNow(); break;
        }
    }
    ImGui::End();
    whatsNew();
}

// ---------------------------------------------------------------- pause menu
void pauseMenu() {
    if (A.showSettings) { settingsWindow(); return; }
    dimBackground();
    centerNext(300 * S(), 0);
    ImGui::Begin("##pause", nullptr, PLAIN | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushFont(gFontBold, 18.0f);
    ImGui::TextUnformatted("Paused");
    ImGui::PopFont();
    ImGui::Dummy({0, 6 * S()});
    ImVec2 bw{-1, 34 * S()};
    // (Escape closes it only if it was already open when this frame began: the press that opened it isn't a second press)
    if (ImGui::Button("Resume", bw) || (IsKeyPressed(KEY_ESCAPE) && A.modalAtFrameStart)) { uiTick(); A.showPause = false; }
    if (ImGui::Button("Tutorials", bw)) { uiTick(); A.showPause = false; A.showTutorials = true; }
    if (ImGui::Button("Settings", bw)) { uiTick(); A.showSettings = true; }
    if (ImGui::Button(A.screen == Screen::Range ? "Leave the range" : A.benchOnly ? "Leave the bench" : "Main menu", bw)) { uiTick(); backToMenu(); }
    if (A.screen == Screen::MapEditor) ImGui::TextDisabled("Your map is autosaved.");
    if (A.screen == Screen::NpcEditor) ImGui::TextDisabled(NE.player ? "Your character is kept." : "Your NPC is kept as it is for next time.");
    if (ImGui::Button("Quit to desktop", bw)) A.quit = true;
    ImGui::End();
}

// ---------------------------------------------------------------- firing range: which gun, which map
void rangePicker() {
    dimBackground(0.35f);
    centerNext(520 * S(), 560 * S());
    ImGui::Begin("##range", nullptr, PLAIN);
    ImGui::PushFont(gFontBold, 20.0f);
    ImGui::TextUnformatted("Firing range");
    ImGui::PopFont();
    ImGui::TextDisabled("Choose where to shoot, then the gun to take.");
    ImGui::Dummy({0, 4 * S()});
    // the map: the practice range, the one in the map editor, or a saved map
    static int mapPick = 0;
    std::vector<fs::path> maps;
    if (fs::exists(MAPS_DIR))
        for (auto& e : fs::directory_iterator(MAPS_DIR))
            if (e.path().extension() == ".gsmap" && e.path().filename().string()[0] != '.') maps.push_back(e.path());
    std::sort(maps.begin(), maps.end());
    std::vector<std::string> names = {"Practice range", "Your map (in the map editor)"};
    for (auto& m : maps) names.push_back(m.stem().string());
    mapPick = std::clamp(mapPick, 0, (int)names.size() - 1);
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Where"); ImGui::SameLine(90 * S()); ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##map", names[mapPick].c_str())) {
        for (int i = 0; i < (int)names.size(); i++) if (ImGui::Selectable(names[i].c_str(), i == mapPick)) mapPick = i;
        ImGui::EndCombo();
    }
    auto go = [&](const Design& d) {
        gmap::Map m = gmap::builtinRange();
        auto readMap = [&](const std::string& path) {
            std::ifstream in(path); std::stringstream ss; ss << in.rdbuf();
            gmap::Map mm;
            if (in && gmap::mapFromJson(ss.str(), mm)) { m = mm; return true; }
            return false;
        };
        if (mapPick == 1 && !readMap(std::string(MAPS_DIR) + "/.autosave.gsmap")) toast("No map in the map editor yet: off to the practice range.");
        if (mapPick >= 2 && !readMap(maps[mapPick - 2].string())) toast("That map couldn't be read: off to the practice range.");
        uiTick();
        enterRange(d, m);
    };
    ImGui::BeginChild("##list", {0, -44 * S()});
    ImGui::SeparatorText("Your sandbox design");
    if (ImGui::Selectable((A.design.name + "##cur").c_str())) go(A.design);
    ImGui::SeparatorText("Examples");
    for (auto& p : presetList()) {
        if (std::string(p.key) == "blank") continue;
        if (ImGui::Selectable(p.label)) go(loadPreset(p.key));
    }
    std::vector<fs::path> files;
    if (fs::exists(GUNS_DIR))
        for (auto& e : fs::directory_iterator(GUNS_DIR)) if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    ImGui::SeparatorText("Your blueprints");
    if (files.empty()) ImGui::TextDisabled("None saved yet. Save one from the Sandbox.");
    for (auto& f : files) {
        std::ifstream in(f); std::stringstream ss; ss << in.rdbuf();
        Design d;
        bool ok = designFromJson(ss.str(), d);
        if (ok && d.npcGun.on) { ImGui::TextDisabled("%s  (made for NPCs)", f.stem().string().c_str()); continue; }
        if (!ImGui::Selectable(f.stem().string().c_str())) continue;
        if (ok) go(d);
        else toast("That blueprint couldn't be read.");
    }
    ImGui::EndChild();
    if (ImGui::Button("Back", {120 * S(), 32 * S()}) || IsKeyPressed(KEY_ESCAPE)) { uiTick(); A.showRangePicker = false; }
    ImGui::End();
}

// ---------------------------------------------------------------- test bench: which gun
void benchPicker() {
    dimBackground(0.35f);
    centerNext(520 * S(), 520 * S());
    ImGui::Begin("##bench", nullptr, PLAIN);
    ImGui::PushFont(gFontBold, 20.0f);
    ImGui::TextUnformatted("Test bench");
    ImGui::PopFont();
    ImGui::TextDisabled("Choose a gun to put on the bench.");
    ImGui::Dummy({0, 4 * S()});
    ImGui::BeginChild("##list", {0, -44 * S()});
    ImGui::SeparatorText("Your sandbox design");
    if (ImGui::Selectable((A.design.name + "##cur").c_str())) { uiTick(); enterWorkshop(true); }
    ImGui::SeparatorText("Examples");
    for (auto& p : presetList()) {
        if (std::string(p.key) == "blank") continue;
        if (ImGui::Selectable(p.label)) { uiTick(); enterBenchWith(loadPreset(p.key)); }
    }
    std::vector<fs::path> files;
    if (fs::exists(GUNS_DIR))
        for (auto& e : fs::directory_iterator(GUNS_DIR)) if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    ImGui::SeparatorText("Your blueprints");
    if (files.empty()) ImGui::TextDisabled("None saved yet. Save one from the Sandbox.");
    for (auto& f : files) {
        if (!ImGui::Selectable(f.stem().string().c_str())) continue;
        std::ifstream in(f); std::stringstream ss; ss << in.rdbuf();
        Design d;
        if (designFromJson(ss.str(), d)) { uiTick(); enterBenchWith(d); }
        else toast("That blueprint couldn't be read.");
    }
    ImGui::EndChild();
    if (ImGui::Button("Back", {120 * S(), 32 * S()}) || IsKeyPressed(KEY_ESCAPE)) { uiTick(); A.showBenchPicker = false; }
    ImGui::End();
}

// ---------------------------------------------------------------- settings
namespace {
enum class Page { Display, Audio, Interface, Designer, Bench, Controls, Files };
Page page = Page::Display;
Settings pending;          // display choices not applied yet
bool pendingInit = false;
Settings beforeApply;      // to revert to if the player doesn't confirm
double confirmUntil = 0;   // a display change is waiting for "keep"
int capturing = -1;        // action waiting for a key press
bool dirty = false;

void row(const char* label, const char* help = nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help && tipHover()) ImGui::SetTooltip("%s", help);
    ImGui::SameLine(230 * S());
    ImGui::SetNextItemWidth(-1);
}
template <class E> bool enumCombo(const char* id, E& v, const char* items) {
    int i = (int)v;
    if (ImGui::Combo(id, &i, items)) { v = (E)i; return true; }
    return false;
}
bool pctSlider(const char* id, float& v) {
    int p = (int)std::lround(v * 100);
    bool ch = ImGui::SliderInt(id, &p, 0, 100, "%d%%");
    typeable();
    if (ch) v = p / 100.f;
    return ch;
}

void displayPage() {
    if (!pendingInit) { pending = CFG; pendingInit = true; }
    ImGui::SeparatorText("Window");
    row("Mode");
    enumCombo("##wm", pending.windowMode, "Windowed\0Borderless window\0Fullscreen\0");
    int mc = std::max(1, GetMonitorCount());
    if (mc > 1) {
        row("Monitor");
        std::string items;
        for (int i = 0; i < mc; i++) { items += std::to_string(i + 1) + ": " + GetMonitorName(i); items.push_back('\0'); }
        ImGui::Combo("##mon", &pending.monitor, items.c_str());
    }
    if (pending.windowMode != WindowMode::Borderless) {
        static const int RES[][2] = {{1280, 720}, {1366, 768}, {1440, 900}, {1600, 900}, {1680, 1050}, {1920, 1080}, {1920, 1200},
                                     {2560, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
        int m = std::clamp(pending.monitor, 0, mc - 1), mw = GetMonitorWidth(m), mh = GetMonitorHeight(m);
        row("Resolution");
        char cur[32]; std::snprintf(cur, sizeof cur, "%d x %d", pending.resW, pending.resH);
        if (ImGui::BeginCombo("##res", cur)) {
            for (auto& r : RES) {
                if (mw > 0 && (r[0] > mw || r[1] > mh)) continue;
                char l[32]; std::snprintf(l, sizeof l, "%d x %d", r[0], r[1]);
                if (ImGui::Selectable(l, pending.resW == r[0] && pending.resH == r[1])) { pending.resW = r[0]; pending.resH = r[1]; }
            }
            if (mw > 0) {
                char l[40]; std::snprintf(l, sizeof l, "%d x %d (monitor)", mw, mh);
                if (ImGui::Selectable(l)) { pending.resW = mw; pending.resH = mh; }
            }
            ImGui::EndCombo();
        }
    }
    bool changed = pending.windowMode != CFG.windowMode || pending.monitor != CFG.monitor || pending.resW != CFG.resW || pending.resH != CFG.resH;
    ImGui::Dummy({0, 2 * S()});
    ImGui::SetCursorPosX(230 * S() + ImGui::GetStyle().WindowPadding.x * 0);
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Apply", {110 * S(), 0})) {
        uiTick();
        beforeApply = CFG;
        CFG.windowMode = pending.windowMode; CFG.monitor = pending.monitor; CFG.resW = pending.resW; CFG.resH = pending.resH;
        if (CFG.windowMode != WindowMode::Windowed) CFG.lastFullMode = (int)CFG.windowMode;   // (what F11 goes back to)
        applyDisplay();
        confirmUntil = GetTime() + 15;
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Rendering");
    row("Vertical sync", "Matches the frame rate to your monitor and stops tearing.");
    if (ImGui::Checkbox("##vs", &CFG.vsync)) { applyDisplay(); dirty = true; }
    row("Frame rate limit");
    static const int caps[] = {30, 60, 120, 144, 165, 240, 0};
    int ci = 1;
    for (int i = 0; i < 7; i++) if (CFG.fpsCap == caps[i]) ci = i;
    if (ImGui::Combo("##fps", &ci, "30\0" "60\0" "120\0" "144\0" "165\0" "240\0" "Unlimited\0")) { CFG.fpsCap = caps[ci]; SetTargetFPS(CFG.fpsCap); dirty = true; }
    row("Anti-aliasing", "Smooths edges. Takes effect the next time you start the game.");
    if (ImGui::Checkbox("##aa", &CFG.msaa)) dirty = true;
    ImGui::SeparatorText("Workspace");
    row("Background", "What sits behind the gun in the designer and on the test bench. Also on the palette button in the top bar.");
    backdropCombo();
    if (CFG.backdrop == Backdrop::Solid) { row("Colour"); backdropColour(); }
    row("Drafting grid", "Lines every 1, 10 and 50 mm (or 0.1, 0.5 and 1 inch) behind the parts in the designer.");
    backdropGrid("##grid");
}

void audioPage() {
    ImGui::SeparatorText("Volume");
    row("Master"); dirty |= pctSlider("##vm", CFG.volMaster);
    row("Gunfire"); dirty |= pctSlider("##vg", CFG.volGunfire);
    row("Mechanical", "Clicks and clacks of the action: slides, bolts, magazines, cases."); dirty |= pctSlider("##vk", CFG.volMechanical);
    row("Interface"); dirty |= pctSlider("##vi", CFG.volInterface);
    ImGui::SeparatorText("Behaviour");
    row("Mute when in background"); dirty |= ImGui::Checkbox("##mu", &CFG.muteUnfocused);
}

void interfacePage() {
    ImGui::SeparatorText("Updates");
    row("Update automatically", "When the game starts it looks for a new version on GitHub, downloads it and installs it in the\n"
                                "background. It runs the next time you start the game (or at once with Restart now, on the main menu).\n"
                                "Your designs, maps and settings are never touched.");
    if (ImGui::Checkbox("##autoupd", &CFG.autoUpdate)) { saveSettings(); if (CFG.autoUpdate) update::start(); }
    row("Check for updates", "Looks for a new version right now (and installs it, as above).");
    if (ImGui::Button("Check now##updnow")) update::checkNow();
    {
        update::Status u = update::status();
        ImGui::SameLine();
        if (u.state == update::State::UpToDate) ImGui::TextDisabled("Up to date (%s).", u.latest.c_str());
        else if (u.state == update::State::Checking) ImGui::TextDisabled("Checking...");
        else if (u.state == update::State::Downloading || u.state == update::State::Installing) ImGui::TextDisabled("Getting %s...", u.latest.c_str());
        else if (u.state == update::State::Installed) ImGui::TextColored({0.55f, 0.85f, 0.55f, 1}, "%s is installed: it starts next time.", u.latest.c_str());
        else if (u.state == update::State::Failed) ImGui::TextDisabled("%s", u.note.c_str());
    }
    ImGui::SeparatorText("Performance");
    row("Performance overlay", "The frame rate, a graph of how long each frame takes, and with full details what the time goes on\n"
                                "(the gun's physics, NPCs, drawing) and the heaviest parts of a design. F3 changes it anywhere.");
    if (ImGui::Combo("##perfov", &CFG.perfOverlay, "Off\0Frame rate\0Full details\0")) saveSettings();
    ImGui::SeparatorText("Size");
    row("Interface scale", "Makes text, buttons and panels bigger or smaller. Auto picks a size for your screen.\nCtrl + and Ctrl - change it anywhere.");
    {
        static const float sizes[] = {0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
        char cur[48];
        if (CFG.uiScaleAuto) std::snprintf(cur, sizeof cur, "Auto (%d%%)", (int)std::lround(CFG.uiScale * 100));
        else std::snprintf(cur, sizeof cur, "%d%%", (int)std::lround(CFG.uiScale * 100));
        if (ImGui::BeginCombo("##uis", cur)) {
            char a[48]; std::snprintf(a, sizeof a, "Auto (%d%% for this window)", (int)std::lround(autoUiScale() * 100));
            if (ImGui::Selectable(a, CFG.uiScaleAuto)) { CFG.uiScaleAuto = true; setUiScale(autoUiScale(), false); }
            for (float z : sizes) {
                char l[16]; std::snprintf(l, sizeof l, "%d%%", (int)std::lround(z * 100));
                if (ImGui::Selectable(l, !CFG.uiScaleAuto && std::fabs(CFG.uiScale - z) < 0.01f)) setUiScale(z, true);
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SeparatorText("Information");
    row("Statistics shown", "Minimal: just what you need to know.\nStandard: adds recoil, travel and magazine details.\nEngineering: everything, including the motion trace and part speeds.");
    dirty |= enumCombo("##st", CFG.stats, "Minimal\0Standard\0Engineering\0");
    row("Tooltips"); dirty |= ImGui::Checkbox("##tt", &CFG.tooltips);
    row("How-to hints", "The short line over the 3D view that says what the current tool does."); dirty |= ImGui::Checkbox("##hi", &CFG.hints);
    ImGui::SeparatorText("Accessibility");
    row("Colour-blind friendly", "Blue and orange instead of green and red for good and bad states."); dirty |= ImGui::Checkbox("##cb", &CFG.colorblind);
}

void designerPage() {
    ImGui::SeparatorText("Workspace");
    row("Workbench", "A bench under the gun with a ruler, a hammer, a screwdriver and a mug: a sense of real size.");
    dirty |= ImGui::Checkbox("##wb", &CFG.workbench);
    ImGui::SeparatorText("Editing");
    row("Autosave");
    dirty |= enumCombo("##as", CFG.autosave, "After every change\0Every minute\0Every 5 minutes\0Off\0");
    ImGui::SeparatorText("Camera");
    row("Orbit with");
    int ob = CFG.orbitMiddle ? 1 : 0;
    if (ImGui::Combo("##ob", &ob, "Right mouse button (middle pans)\0Middle mouse button (right pans)\0")) { CFG.orbitMiddle = ob == 1; dirty = true; }
    row("Zoom speed"); dirty |= ImGui::SliderFloat("##zs", &CFG.zoomSpeed, 0.25f, 3.0f, "%.2fx"); typeable();
    row("Invert zoom"); dirty |= ImGui::Checkbox("##iz", &CFG.invertZoom);
}

void benchPage() {
    ImGui::SeparatorText("Speed");
    ImGui::TextDisabled("Speed and slow motion are on the test bench itself, in the Speed section of its panel.");
    ImGui::SeparatorText("Reliability test");
    row("Default rounds");
    static const int rr[] = {25, 50, 100, 200};
    int ri = 1;
    for (int i = 0; i < 4; i++) if (CFG.relRounds == rr[i]) ri = i;
    if (ImGui::Combo("##rr", &ri, "25\0" "50\0" "100\0" "200\0")) { CFG.relRounds = rr[ri]; dirty = true; }
    ImGui::SeparatorText("Spare magazines");
    row("Most lying out", "How many magazines can lie on the bench at once. A new one past this clears away the oldest. Fewer keeps the bench quick.");
    dirty |= ImGui::SliderInt("##ml", &CFG.maxLooseMags, 1, 20); typeable();
    row("Cleared after", "A magazine out of the gun that nobody touches for this long is cleared away, full or empty.");
    static const float ls[] = {10, 20, 30, 60, 120};
    int li = 1;
    for (int i = 0; i < 5; i++) if (std::fabs(CFG.magLife - ls[i]) < 0.5f) li = i;
    if (ImGui::Combo("##mlife", &li, "10 seconds\0" "20 seconds\0" "30 seconds\0" "1 minute\0" "2 minutes\0")) { CFG.magLife = ls[li]; dirty = true; }
}

// Where your files are kept, and choosing another folder for them
void filesPage() {
    namespace fs = std::filesystem;
    std::error_code ec;
    ImGui::SeparatorText("Your files");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Your blueprints, levels, rounds, characters, NPCs, textures, parts and groups are kept in one folder, each kind in its own folder inside it.");
    std::string cur = fs::absolute(userFolder(), ec).string();
    ImGui::TextUnformatted(cur.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Open the folder")) openFolder(userFolder());
    ImGui::SameLine();
    static char typed[512] = "";
    static std::string msg;
    auto useFolder = [&](const std::string& f) {
        std::string err;
        if (changeUserFolder(f, err)) {
            CFG.userDir = f == defaultUserFolder() ? "" : f;
            saveSettings();
            A.userCarts = loadUserCartridges();
            scanUserTextures();
            msg = err.empty() ? "Your files are now kept in " + f + ". What was in the old folder was copied there (it's still in the old one too)." : err;
        } else msg = err;
    };
    if (ImGui::Button("Use another folder...")) {
        std::string f;
        if (pickFolder(f, "Choose the folder for your Gunsmith3D files")) useFolder(f);
        else if (documentsFolder().empty()) msg = "Type the folder below and press Use it.";
    }
    if (tipHover()) ImGui::SetTooltip("Pick any folder (another drive, a cloud-synced folder). Your files are copied there,\nnothing in either folder is overwritten, and the game uses it from then on.");
    if (!CFG.userDir.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Back to the default")) useFolder(defaultUserFolder());
    }
    if (documentsFolder().empty()) {   // (no folder box here: type it)
        ImGui::SetNextItemWidth(-90 * S());
        ImGui::InputText("##udir", typed, sizeof typed);
        ImGui::SameLine();
        if (ImGui::Button("Use it") && typed[0]) useFolder(typed);
    }
    if (!msg.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(colAccent(), "%s", msg.c_str()); ImGui::PopTextWrapPos(); }
    ImGui::SeparatorText("Settings");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("These settings and the designer's autosave are kept in:");
    ImGui::TextUnformatted(fs::absolute(appFolder().empty() ? "." : appFolder(), ec).string().c_str());
    ImGui::PopTextWrapPos();
}
void controlsPage() {
    ImGui::SeparatorText("Mouse (first person)");
    row("Sensitivity");
    dirty |= ImGui::SliderFloat("##sens", &CFG.mouseSens, 0.1f, 4.0f, "%.2f"); typeable();
    row("Invert vertical"); dirty |= ImGui::Checkbox("##inv", &CFG.invertMouse);
    row("Field of view", "Vertical field of view in first person.");
    dirty |= ImGui::SliderFloat("##fov", &CFG.fov, 55.f, 105.f, "%.0f deg"); typeable();
    const char* group = nullptr;
    for (int i = 0; i < (int)Act::Count; i++) {
        const ActInfo& a = actInfo((Act)i);
        if (!group || std::string(group) != a.group) { group = a.group; ImGui::SeparatorText(group); }
        row(a.name);
        std::string label = capturing == i ? std::string("Press a key...") : (a.ctrl ? std::string("Ctrl + ") : std::string()) + keyName(keyOf((Act)i));
        ImGui::PushID(i);
        if (ImGui::Button(label.c_str(), {-1, 0})) { uiTick(); capturing = i; }
        ImGui::PopID();
    }
    if (capturing >= 0) {
        int k = GetKeyPressed();
        if (k == KEY_ESCAPE) capturing = -1;
        else if (k > 0 && k != KEY_LEFT_CONTROL && k != KEY_RIGHT_CONTROL) { bindKey((Act)capturing, k); capturing = -1; dirty = true; }
    }
    ImGui::Dummy({0, 6 * S()});
    if (ImGui::Button("Reset all to defaults")) { uiTick(); resetKeys(); dirty = true; }
    ImGui::TextDisabled("Escape always opens the menu. Ctrl combinations keep Ctrl; pick the letter.");
}
}  // namespace

void settingsPageIndex(int i) { page = (Page)std::clamp(i, 0, 6); }

void settingsWindow() {
    dimBackground(0.6f);
    centerNext(800 * S(), 580 * S());
    ImGui::Begin("##settings", nullptr, PLAIN);
    ImGui::PushFont(gFontBold, 20.0f);
    ImGui::TextUnformatted("Settings");
    ImGui::PopFont();
    ImGui::Dummy({0, 4 * S()});
    // pages down the left, the chosen page on the right
    ImGui::BeginChild("##nav", {170 * S(), -46 * S()});
    struct P { const char* l; Page p; } pages[] = {{"Display", Page::Display}, {"Audio", Page::Audio}, {"Interface", Page::Interface},
                                                   {"Designer", Page::Designer}, {"Test bench", Page::Bench}, {"Controls", Page::Controls}, {"Files", Page::Files}};
    for (auto& p : pages) {
        bool on = page == p.p;
        if (on) ImGui::PushStyleColor(ImGuiCol_Text, colAccent());
        if (ImGui::Selectable(p.l, on, 0, {0, 30 * S()})) { if (!on) uiTick(); page = p.p; capturing = -1; }
        if (on) ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 16 * S());
    ImGui::BeginChild("##page", {0, -46 * S()});
    switch (page) {
    case Page::Display: displayPage(); break;
    case Page::Audio: audioPage(); break;
    case Page::Interface: interfacePage(); break;
    case Page::Designer: designerPage(); break;
    case Page::Bench: benchPage(); break;
    case Page::Controls: controlsPage(); break;
    case Page::Files: filesPage(); break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextDisabled("Changes are saved automatically. None of them change how guns behave.");
    ImGui::SameLine(ImGui::GetWindowWidth() - 130 * S());
    bool close = ImGui::Button("Done", {116 * S(), 0}) || (IsKeyPressed(KEY_ESCAPE) && capturing < 0 && confirmUntil == 0);
    ImGui::End();

    // a display change waits for "keep", or goes back by itself
    if (confirmUntil > 0) {
        double left = confirmUntil - GetTime();
        centerNext(380 * S(), 0);
        ImGui::Begin("##keep", nullptr, PLAIN | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("Keep these display settings?");
        ImGui::TextDisabled("Going back to the previous ones in %d s.", (int)std::ceil(std::max(0.0, left)));
        if (ImGui::Button("Keep", {110 * S(), 0})) { uiTick(); confirmUntil = 0; dirty = true; }
        ImGui::SameLine();
        if (ImGui::Button("Revert", {110 * S(), 0}) || left <= 0) {
            CFG.windowMode = beforeApply.windowMode; CFG.monitor = beforeApply.monitor; CFG.resW = beforeApply.resW; CFG.resH = beforeApply.resH;
            applyDisplay();
            pending = CFG;
            confirmUntil = 0;
        }
        ImGui::End();
    }
    if (dirty) { saveSettings(); dirty = false; }
    if (close) { uiTick(); A.showSettings = false; pendingInit = false; capturing = -1; saveSettings(); }
}
