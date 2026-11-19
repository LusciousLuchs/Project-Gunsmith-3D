// Gunsmith3D - Main menu, pause menu, settings, and choosing a gun for the test bench.
#include "app/paths.hpp"
#include "extras/IconsFontAwesome6.h"
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
#include "json.hpp"
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

// A main menu entry, in the style of the owner's mockup: an icon and the name in capitals; hovered, an orange bar with a
// slanted end behind it. A small note can follow the name.
static bool menuItem(const char* icon, const char* label, bool enabled = true, const char* note = nullptr, bool open = false, const char* after = nullptr) {
    ImGui::PushID(label);
    float h = 50 * S(), w = 420 * S();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##m", {w, h}) && enabled;
    bool hov = ImGui::IsItemHovered() && enabled;
    auto* dl = ImGui::GetWindowDrawList();
    if (hov || open) {   // the bar: square on the left, cut at a slant on the right
        float sl = h * 0.75f;
        ImU32 c0 = IM_COL32(238, 122, 32, hov ? 235 : 120), c1 = IM_COL32(200, 92, 18, hov ? 235 : 120);
        dl->AddRectFilledMultiColor({p.x, p.y + 3 * S()}, {p.x + w - sl, p.y + h - 3 * S()}, c0, c1, c1, c0);
        dl->AddTriangleFilled({p.x + w - sl, p.y + 3 * S()}, {p.x + w, p.y + 3 * S()}, {p.x + w - sl, p.y + h - 3 * S()}, c1);
    }
    ImU32 col = !enabled ? IM_COL32(110, 115, 125, 255) : hov ? IM_COL32(255, 255, 255, 255) : IM_COL32(214, 218, 226, 255);
    ImU32 icol = !enabled ? IM_COL32(90, 95, 105, 255) : hov ? IM_COL32(255, 255, 255, 255) : IM_COL32(176, 182, 192, 255);
    ImGui::PushFont(nullptr, 22.0f);
    float ifs = ImGui::GetFontSize();
    float iw = ImGui::CalcTextSize(icon).x;
    dl->AddText({p.x + 30 * S() + (28 * S() - iw) / 2, p.y + (h - ifs) / 2}, icol, icon);
    ImGui::PopFont();
    std::string up = label;
    for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
    ImGui::PushFont(gFontBold, 21.0f);
    float fs = ImGui::GetFontSize(), x = p.x + 82 * S();
    for (const char* c = up.c_str(); *c; c++) {   // (a little letter-spacing, like the logo's lettering)
        dl->AddText({x, p.y + (h - fs) / 2}, col, c, c + 1);
        x += ImGui::CalcTextSize(c, c + 1).x + 1.2f * S();
    }
    ImGui::PopFont();
    if (after) {   // (an icon after the name: a submenu's arrow)
        ImGui::PushFont(nullptr, 18.0f);
        float afs = ImGui::GetFontSize();
        dl->AddText({x + 10 * S(), p.y + (h - afs) / 2}, col, after);
        ImGui::PopFont();
        x += 30 * S();
    }
    if (note && !hov) {
        float nfs = ImGui::GetFontSize();
        dl->AddText({x + 12 * S(), p.y + (h - nfs) / 2 + 2 * S()}, IM_COL32(150, 156, 166, 220), note);
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
    static int open = -1;   // (shut, open; first time: open if this version's notes haven't been seen)
    if (open < 0) open = CFG.seenVersion != version::text() ? 1 : 0;
    ImGui::SetNextWindowPos({GetScreenWidth() - w - m, m});
    ImGui::SetNextWindowSize({w, older ? GetScreenHeight() - 2 * m : 0});
    ImGui::SetNextWindowSizeConstraints({w, 0}, {w, GetScreenHeight() * (older ? 1.0f : 0.62f) - 2 * m});
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("##whatsnew", nullptr, PLAIN | (older ? 0 : ImGuiWindowFlags_AlwaysAutoResize));
    {   // the card's head: the version and "What's new", an orange edge down its left
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(wp, {wp.x + 4 * S(), wp.y + ws.y}, IM_COL32(238, 122, 32, 255));
        ImGui::PushFont(gFontBold, 19.0f);
        ImGui::Text("v%s", R[0].title.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("What's new");
        ImGui::SameLine(w - 52 * S());
        if (ImGui::SmallButton(open ? ICON_FA_CHEVRON_UP : ICON_FA_CHEVRON_DOWN)) {
            open = !open;
            if (!open) { older = false; CFG.seenVersion = version::text(); saveSettings(); }
        }
        if (!open) { ImGui::End(); return; }
    }
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
// Updates aren't optional (the owner's choice): while a newer version is being checked for or fetched, the main menu
// waits behind this screen, and once it's installed the game restarts into it. Offline (or the check fails, or takes
// more than 8 seconds), you can play the version you have.
static bool forcedUpdateScreen() {
    update::Status u = update::status();
    static double since = -1;
    if (u.state == update::State::Checking && since < 0) since = GetTime();
    bool checking = u.state == update::State::Checking && GetTime() - since < 8;
    bool fetching = u.state == update::State::Downloading || u.state == update::State::Installing || u.state == update::State::Installed;
    if (!checking && !fetching) return false;
    static bool restartFailed = false;
    if (u.state == update::State::Installed) {   // restart into the new version (once)
        static bool asked = false;
        if (!asked) { asked = true; if (update::restart()) A.quit = true; else restartFailed = true; }
    }
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, {W, H}, IM_COL32(12, 14, 18, 245));
    ImGui::SetNextWindowPos({W / 2, H / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({460 * S(), 0});
    ImGui::Begin("##forcedupdate", nullptr, PLAIN | ImGuiWindowFlags_AlwaysAutoResize);
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(wp, {wp.x + 4 * S(), wp.y + ws.y}, IM_COL32(238, 122, 32, 255));
    ImGui::PushFont(gFontBold, 20.0f);
    ImGui::TextUnformatted(checking ? "Checking for updates" : "Updating Gunsmith 3D");
    ImGui::PopFont();
    if (checking) ImGui::TextDisabled("One moment...");
    else if (u.state == update::State::Downloading) ImGui::Text("Downloading %s...", u.latest.c_str());
    else if (u.state == update::State::Installing) ImGui::Text("Installing %s...", u.latest.c_str());
    else if (!restartFailed) ImGui::Text("%s is installed. Restarting...", u.latest.c_str());
    else {   // (it couldn't start the new version itself: the player does)
        ImGui::Text("%s is installed. Close the game and start it again to play it.", u.latest.c_str());
        if (ImGui::Button("Quit", {-1, 0})) A.quit = true;
    }
    float t = (float)GetTime(), bw = ImGui::GetContentRegionAvail().x;   // a bar sliding back and forth
    ImVec2 p = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + bw, p.y + 4 * S()}, IM_COL32(255, 255, 255, 25));
    float f = 0.5f + 0.5f * std::sin(t * 2.5f), seg = bw * 0.25f;
    dl->AddRectFilled({p.x + f * (bw - seg), p.y}, {p.x + f * (bw - seg) + seg, p.y + 4 * S()}, IM_COL32(238, 122, 32, 255));
    ImGui::Dummy({bw, 10 * S()});
    ImGui::TextDisabled("Your designs, maps and settings aren't touched.");
    ImGui::End();
    return true;
}

void mainMenu() {
    if (forcedUpdateScreen()) return;
    if (A.showSettings) { settingsWindow(); return; }
    if (A.showBenchPicker) { benchPicker(); return; }
    if (A.showTutorials) { tutorialsWindow(); return; }
    if (A.showRangePicker) { rangePicker(); return; }
    if (A.showAbout) { aboutWindow(); return; }
    if (A.showCareerPicker) { careerPicker(); return; }
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    float panelW = std::min(520 * S(), W * 0.42f);
    if (CFG.menuPicture) {   // the workshop picture on the right, the menu's dark panel fading into it on the left
        Texture2D pic = artTexture(Art::MenuBackground);
        drawCover(bg, pic, {panelW * 0.55f, 0}, {W, H}, 0.35f);
    }
    bg->AddRectFilled({0, 0}, {panelW * 0.62f, H}, IM_COL32(17, 20, 25, CFG.menuPicture ? 250 : 200));
    bg->AddRectFilledMultiColor({panelW * 0.62f, 0}, {panelW * 1.35f, H}, IM_COL32(17, 20, 25, CFG.menuPicture ? 250 : 200), IM_COL32(17, 20, 25, 0),
                                IM_COL32(17, 20, 25, 0), IM_COL32(17, 20, 25, CFG.menuPicture ? 250 : 200));
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({W, H});
    ImGui::SetNextWindowBgAlpha(0);
    ImGui::Begin("##menu", nullptr, PLAIN | ImGuiWindowFlags_NoBringToFrontOnFocus);
    float x = 44 * S();
    {   // the logo
        Texture2D logo = artTexture(Art::MenuLogo);
        float lw = 400 * S(), lh = logo.id ? lw * logo.height / logo.width : 0;
        float ly = std::max(16 * S(), std::min(H * 0.05f, H - (lh + 14 * 50 * S() + 90 * S())));
        if (logo.id) ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)logo.id, {x, ly}, {x + lw, ly + lh});
        else {
            ImGui::SetCursorPos({x, ly});
            ImGui::PushFont(gFontBold, 46.0f); ImGui::TextUnformatted("GUNSMITH 3D"); ImGui::PopFont();
            lh = 60 * S();
        }
        float sy = ly + lh + 10 * S();
        ImGui::GetWindowDrawList()->AddLine({x + 20 * S(), sy}, {x + 380 * S(), sy}, IM_COL32(255, 255, 255, 40), 1.0f);
        ImGui::SetCursorPos({0, sy + 12 * S()});
    }
    ImGui::BeginGroup();
    if (menuItem(ICON_FA_GRADUATION_CAP, "Tutorials")) A.showTutorials = true;
    if (menuItem(ICON_FA_BRIEFCASE, "Career")) A.showCareerPicker = true;
    if (menuItem(ICON_FA_CUBE, "Sandbox")) enterWorkshop(false);
    if (menuItem(ICON_FA_CROSSHAIRS, "Firing range")) A.showRangePicker = true;
    if (menuItem(ICON_FA_USERS, "Your character")) enterCharacterEditor();
    {   // the tools for making places to shoot and things to shoot at
        static bool mapTools = false;
        if (menuItem(ICON_FA_SCREWDRIVER_WRENCH, "Tools", true, nullptr, mapTools, mapTools ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT)) mapTools = !mapTools;
        if (mapTools) {
            ImGui::Indent(40 * S());
            if (menuItem(ICON_FA_MAP, "Level editor")) enterMapEditor();
            if (menuItem(ICON_FA_GHOST, "NPC editor")) enterNpcEditor();
            if (menuItem(ICON_FA_FLASK, "Test bench")) A.showBenchPicker = true;
            ImGui::Unindent(40 * S());
        }
    }
    {   // ask for a new version now (one found downloads and installs behind the Updating card, then restarts)
        update::Status u = update::status();
        const char* note = u.state == update::State::Checking ? "looking..." : u.state == update::State::UpToDate ? "you have the newest"
                         : u.state == update::State::Downloading || u.state == update::State::Installing ? "updating..."
                         : u.state == update::State::Failed ? "couldn't reach the server" : nullptr;   // (only once there's something to say)
        if (menuItem(ICON_FA_ROTATE, "Check for updates", u.state != update::State::Checking && u.state != update::State::Downloading && u.state != update::State::Installing, note))
            update::checkNow();
    }
    if (menuItem(ICON_FA_GEAR, "Settings")) A.showSettings = true;
    if (menuItem(ICON_FA_COMMENTS, "Discord")) OpenURL(DISCORD_URL);
    if (menuItem(ICON_FA_CIRCLE_INFO, "About")) A.showAbout = true;
    if (menuItem(ICON_FA_POWER_OFF, "Quit")) A.quit = true;
    ImGui::EndGroup();
    ImGui::SetCursorPos({x, H - 34 * S()});
    ImGui::TextDisabled("Gunsmith 3D  v%s", version::text());
    if (devBuild()) { ImGui::SameLine(); ImGui::TextColored({1, 0.75f, 0.3f, 1}, "  DEVELOPER TOOLS ON"); }
    {   // only when an update couldn't be fetched (offline): say so, and offer another go
        update::Status u = update::status();
        if (u.state == update::State::Failed) {
            ImGui::SameLine(0, 14 * S());
            ImGui::TextDisabled("Update: %s", u.note.c_str());
            ImGui::SameLine(); if (ImGui::SmallButton("Try again")) update::checkNow();
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
    if (A.screen == Screen::Range && ImGui::Button(rangeIsNight() ? ICON_FA_SUN "  Make it day" : ICON_FA_MOON "  Make it night", bw)) { uiTick(); rangeSetHour(rangeIsNight() ? 14 : 22); }
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
    // day or night: the practice range has no running clock, you choose; another map can keep its own
    static int lightPick = 0;   // practice range: 0 day, 1 night; other maps: 0 as the map has it, 1 day, 2 night
    {
        bool practice = mapPick == 0;
        const char* opts = practice ? "Day\0Night\0" : "As the map has it\0Day\0Night\0";
        if (practice) lightPick = std::min(lightPick, 1);
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Light"); ImGui::SameLine(90 * S()); ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##light", &lightPick, opts);
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
        int light = mapPick == 0 ? lightPick + 1 : lightPick;   // 0 as the map has it, 1 day, 2 night
        if (light == 1) rangeSetHour(14);
        if (light == 2) rangeSetHour(22);
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

// ---------------------------------------------------------------- career: which one
void careerPicker() {
    dimBackground(0.35f);
    centerNext(560 * S(), 720 * S());
    ImGui::Begin("##careers", nullptr, PLAIN);
    ImGui::PushFont(gFontBold, 20.0f);
    ImGui::TextUnformatted("Career");
    ImGui::PopFont();
    ImGui::TextDisabled("Carry on with a gun business, or start a new one.");
    ImGui::Dummy({0, 4 * S()});
    auto slots = careerSlots();
    static char name[64] = "";
    static std::string askDelete;
    static bool init = false;
    if (!init) { std::snprintf(name, sizeof name, "%s", slots.empty() ? "My shop" : TextFormat("Shop %d", (int)slots.size() + 1)); init = true; }
    ImGui::SeparatorText("New career");
    ImGui::SetNextItemWidth(-130 * S());
    ImGui::InputTextWithHint("##cname", "Your shop's name", name, sizeof name);
    ImGui::SameLine();
    static int city = 0;
    if (accentButton("Start", true, {-1, 0})) { uiTick(); A.showCareerPicker = false; init = false; careerBegin(name, city); ImGui::End(); return; }
    ImGui::TextDisabled("Where's your home garage? Click a city.");
    usMapPicker("##newcity", city, ImGui::GetContentRegionAvail().x);
    ImGui::SeparatorText("Your careers");
    ImGui::BeginChild("##clist", {0, -44 * S()});
    if (slots.empty()) ImGui::TextDisabled("None yet.");
    for (auto& c : slots) {
        ImGui::PushID(c.name.c_str());
        ImGui::BeginGroup();
        ImGui::PushFont(gFontBold, 0.0f);
        ImGui::TextUnformatted(c.name.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("Day %d    $%.0f    %s", c.day, c.money, career::tierName(c.reputation));
        ImGui::EndGroup();
        float bw = 80 * S();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 2 * bw - ImGui::GetStyle().ItemSpacing.x);
        float by = ImGui::GetCursorPosY() + 6 * S();
        ImGui::SetCursorPosY(by);
        if (ImGui::Button("Play", {bw, 0})) { uiTick(); A.showCareerPicker = false; init = false; careerContinue(c.name); ImGui::PopID(); ImGui::EndChild(); ImGui::End(); return; }
        ImGui::SameLine();
        ImGui::SetCursorPosY(by);
        if (askDelete == c.name) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1));
            if (ImGui::Button("Delete it", {bw, 0})) { careerDelete(c.name); askDelete.clear(); }
            ImGui::PopStyleColor();
            if (tipHover()) ImGui::SetTooltip("Deletes this career and its workshop for good.");
        } else if (ImGui::Button("Delete", {bw, 0})) askDelete = c.name;
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::Button("Back", {120 * S(), 32 * S()}) || IsKeyPressed(KEY_ESCAPE)) { uiTick(); A.showCareerPicker = false; askDelete.clear(); }
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
enum class Page { Display, Audio, Interface, Designer, Bench, Controls, Files, Cheats };
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
    row("Sun shadows", "Walls, roofs and furniture cast shadows in sunlight on maps. Off: a little faster on slow computers.");
    if (ImGui::Checkbox("##shadows", &CFG.shadows)) dirty = true;
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
    row("Radios", "Radios placed on maps and in your shop (internet stations)."); dirty |= pctSlider("##vr", CFG.volRadio);
    row("Interface"); dirty |= pctSlider("##vi", CFG.volInterface);
    ImGui::SeparatorText("Behaviour");
    row("Mute when in background"); dirty |= ImGui::Checkbox("##mu", &CFG.muteUnfocused);
}

void interfacePage() {
    ImGui::SeparatorText("Look");
    row("Title splash", "The Gunsmith 3D picture when the game starts (after the studio's). A key or a click skips it.");
    if (ImGui::Checkbox("##titlesplash", &CFG.titleSplash)) saveSettings();
    row("Main menu background", "The workshop picture, or your last gun turning in 3D.");
    {
        int k = CFG.menuPicture ? 0 : 1;
        if (ImGui::Combo("##menupic", &k, "The workshop\0Your gun in 3D\0")) { CFG.menuPicture = k == 0; saveSettings(); }
    }
    ImGui::SeparatorText("Updates");
    {
    row("Check for updates", "The game always updates itself when it starts. This looks for a new version right now.");
    if (ImGui::Button("Check now##updnow")) update::checkNow();
    }
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
    row("Paperwork help (career)", "At your shop's counter, selling a gun takes paperwork: reading their form and ID and typing their details into the check.\n"
        "Fill it in for me: the check is typed in for you, and anything wrong is spelled out in plain words.\n"
        "Do it for me: one button does the whole lot; you just read the answer and decide.");
    dirty |= enumCombo("##ph", CFG.paperHelp, "By hand\0Fill it in for me\0Do it for me\0");
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
static void devToolsRow();
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
    devToolsRow();
}
static void devToolsRow() {   // the owner's tools for making the game
    ImGui::SeparatorText("Developer tools");
    row("Developer tools", "For making the game itself: the Level editor's File tab can make a map the built-in one for a career\n"
        "property or the practice range (with its build area, storefront and locked walls), and Settings can make your\n"
        "settings the ones new players start with. Saved in your Defaults folder; send the files in to ship them.\n"
        "While this is on, the game uses your Defaults folder's maps and settings before its own.");
    if (ImGui::Checkbox("##devtools", &CFG.devTools)) { saveSettings(); applyDevTools(); }
    if (CFG.devTools) { ImGui::SameLine(); if (ImGui::SmallButton("Open the Defaults folder")) openFolder(defaultsFolder()); }
}
void cheatsPage() {
    ImGui::SeparatorText("Cheats");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Not realistic, just fun. They work everywhere: the test bench, the range, reliability tests and career mode. "
                        "Anything tested with a cheat on says so in its report.");
    ImGui::PopTextWrapPos();
    bool ch = false;
    row("Auto feed", "Every time the action opens far enough, a round goes from the magazine into the chamber, whatever the\n"
                     "magazine and the feed path are like (and the empty case is thrown clear). The action still has to move.");
    ch |= ImGui::Checkbox("##cheatfeed", &CFG.cheatAutoFeed);
    row("Brute force", "An action that didn't cycle after a shot, or that jams, is worked at once by an unseen strong hand:\n"
                       "self-loaders that can't run themselves keep going, and a bolt action or a pump works itself.");
    ch |= ImGui::Checkbox("##cheatforce", &CFG.cheatBruteForce);
    if (ch) { dirty = true; applyCheats(); }
}
void controlsPage() {
    ImGui::SeparatorText("Mouse (first person)");
    row("Sensitivity");
    dirty |= ImGui::SliderFloat("##sens", &CFG.mouseSens, 0.1f, 4.0f, "%.2f"); typeable();
    row("Invert vertical"); dirty |= ImGui::Checkbox("##inv", &CFG.invertMouse);
    row("Field of view", "Vertical field of view in first person.");
    dirty |= ImGui::SliderFloat("##fov", &CFG.fov, 55.f, 105.f, "%.0f deg"); typeable();
    row("Show your arms", "On the range: your arms and hands holding the gun, as the gun's pose has them.");
    dirty |= ImGui::Checkbox("##fparms", &CFG.fpArms);
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

void settingsPageIndex(int i) { page = (Page)std::clamp(i, 0, (int)Page::Cheats); }

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
                                                   {"Designer", Page::Designer}, {"Test bench", Page::Bench}, {"Controls", Page::Controls}, {"Files", Page::Files}, {"Cheats", Page::Cheats}};
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
    case Page::Cheats: cheatsPage(); break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (devBuild()) {   // the developer build: these settings become what a new player starts with
        if (ImGui::Button(ICON_FA_SCREWDRIVER_WRENCH "  Developer: make these the defaults for new players")) {
            saveSettings();
            std::ifstream in(settingsFile());
            nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_object()) {
                for (const char* k : {"userDir", "eulaAgreed", "seenVersion", "lastFullMode", "monitor"}) j.erase(k);   // (yours alone)
                std::error_code ec; std::filesystem::create_directories(defaultsFolder(), ec);
                std::ofstream(defaultsFolder() + "/settings.json") << j.dump(1);
                toast("Saved as new players' defaults (Defaults/settings.json). Send me the file to ship it.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Defaults folder")) openFolder(defaultsFolder());
    }
    ImGui::TextDisabled("Changes are saved automatically. Only the cheats change how guns behave.");
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
