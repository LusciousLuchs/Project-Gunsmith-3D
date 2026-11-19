// Gunsmith3D - Map editor: the top bar (tools, grid, walk) and the panel (edit, textures, map settings, files).
#include "app/radio.hpp"
#include "app/usertex.hpp"
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/mapedit.hpp"
#include "app/furnish.hpp"
#include "app/npcedit.hpp"
#include "app/mapgfx.hpp"
#include "app/careermode.hpp"
#include "app/shop3d.hpp"
#include "app/settings.hpp"
#include "app/paths.hpp"
#include "ui/ui.hpp"
#include "npcai.hpp"
#include "imgui.h"
#include "rlImGui.h"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using gmap::V3;
static float S() { return CFG.uiScale; }

static bool iconBtn(const char* icon, const char* tip, bool on = false) {
    float sz = TOOLBAR_H - 14 * S();
    if (on) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.48f, 0.13f, 0.22f)); ImGui::PushStyleColor(ImGuiCol_Text, colAccent()); }
    ImGui::PushID(tip);   // (two buttons can share an icon: the light tool and the fullbright view are both a bulb)
    bool r = ImGui::Button(icon, {sz, sz});
    ImGui::PopID();
    if (on) ImGui::PopStyleColor(2);
    if (tipHover()) ImGui::SetTooltip("%s", tip);
    return r;
}
static void gap(float px = 12) { ImGui::SameLine(0, px * S()); }

// grid steps: metric, or imperial in inches and feet
static const double GRID_M[] = {0.01, 0.05, 0.1, 0.25, 0.5, 1, 2};
static const double GRID_I[] = {0.0127, 0.0254, 0.0508, 0.1524, 0.3048, 0.6096, 1.2192};

// a length typed and shown in the player's units (metres or feet), stored in metres
// a length in metres, shown and dragged in the Level editor's unit (Ctrl+click to type a number in that unit)
static bool lenDrag(const char* id, double& m, double speedM = 0.01) {
    double k = meUnitPerMetre(), v = m * k;
    static const int dec[] = {3, 1, 2, 2, 3};
    char fmt[16]; std::snprintf(fmt, sizeof fmt, "%%.%df %s", dec[std::clamp(CFG.mapUnit, 0, 4)], meUnitName());
    bool r = ImGui::DragScalar(id, ImGuiDataType_Double, &v, (float)(speedM * k), nullptr, nullptr, fmt);
    if (r) m = v / k;
    return r;
}
static bool lenSlider(const char* id, double& m, double lo, double hi, bool logScale = false) {
    double k = meUnitPerMetre();
    float v = (float)(m * k);
    char fmt[16]; std::snprintf(fmt, sizeof fmt, "%%.%df %s", CFG.mapUnit == 1 || CFG.mapUnit == 3 ? 0 : 1, meUnitName());
    bool r = ImGui::SliderFloat(id, &v, (float)(lo * k), (float)(hi * k), fmt, logScale ? ImGuiSliderFlags_Logarithmic : 0);
    if (r) m = v / k;
    return r;
}
// the Level editor's unit, on its toolbar: metres, centimetres, feet (and inches), inches, yards
static void mapUnitPicker(float h) {
    static const char* names[] = {"m", "cm", "ft", "in", "yd"};
    ImGui::SetNextItemWidth(58 * S());
    int u = std::clamp(CFG.mapUnit, 0, 4);
    if (ImGui::Combo("##mapunit", &u, names, 5)) {
        CFG.mapUnit = u;
        CFG.units = u >= 2 ? Units::Imperial : Units::Metric;   // (the rest of the game follows: metric or imperial)
        saveSettings();
        // the grid moves to the nearest step of the new system
        const double* st = u >= 2 ? GRID_I : GRID_M; int best = 0;
        for (int k = 0; k < 7; k++) if (std::fabs(std::log(st[k] / ME.grid)) < std::fabs(std::log(st[best] / ME.grid))) best = k;
        ME.grid = st[best];
    }
    if (tipHover()) ImGui::SetTooltip("Units for lengths in the Level editor: metres, centimetres, feet and inches, inches or yards.\nThe grid steps change with them.");
    (void)h;
}

void mapToolbar() {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)GetScreenWidth(), TOOLBAR_H});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10 * S(), 7 * S()});
    ImGui::Begin("##maptoolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    float sz = TOOLBAR_H - 14 * S();
    if (iconBtn(ICON_FA_BARS, "Menu (Esc)")) { uiTick(); A.showPause = true; }
    ImGui::SameLine(0, 4 * S());
    if (iconBtn(ICON_FA_GRADUATION_CAP, "Tutorials: short guided lessons on every mechanic")) { uiTick(); A.showTutorials = true; }
    gap();
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
    ImGui::TextDisabled(ME.careerFile.empty() ? "Map editor" : "Your workshop");
    gap(18);
    if (!ME.careerFile.empty()) {   // career mode: back to the office (it counts your workstations)
        if (accentButton(ICON_FA_PERSON_WALKING "  Walk (B)", true, {0, sz})) { shopWalk(); ImGui::End(); return; }
        if (tipHover()) ImGui::SetTooltip("Back to your office. Your workshop is kept as you go.");
        gap(12);
    }
    struct TB { const char* icon; const char* tip; MTool t; } tools[] = {
        {ICON_FA_ARROW_POINTER, "Select, move and resize", MTool::Select}, {ICON_FA_SQUARE, "Box brush", MTool::Box},
        {ICON_FA_CARET_UP, "Ramp (wedge) brush", MTool::Wedge}, {ICON_FA_CIRCLE, "Cylinder brush", MTool::Cylinder},
        {ICON_FA_STAIRS, "Stairs", MTool::Stairs}, {ICON_FA_PLAY, "Pyramid", MTool::Pyramid}, {ICON_FA_ICE_CREAM, "Cone", MTool::Cone},
        {ICON_FA_VOLLEYBALL, "Ball", MTool::Sphere}, {ICON_FA_SCISSORS, "Clip: cut brushes along a line", MTool::Clip},
        {ICON_FA_PERSON, "Player start", MTool::Start}, {ICON_FA_GHOST, "NPC: place an NPC from the NPC editor", MTool::Npc},
        {ICON_FA_BOX_OPEN, "Gun spawner: a stand players take guns from", MTool::Spawner},
        {ICON_FA_SHIELD_HALVED, "Armour plates: walk over them to fill the armour bar", MTool::Armor},
        {ICON_FA_TOOLBOX, "Attachment crate: fit attachments to the gun in your hands", MTool::Crate},
        {ICON_FA_LIGHTBULB, "Light: a lamp or a spotlight. Click a ceiling, a wall or the floor", MTool::Light},
        {ICON_FA_BOXES_STACKED, "Ammo box: press F by it to fill up the ammunition for every gun you carry", MTool::Ammo},
        {ICON_FA_BOX, "Weapon crate: press F by it to take any of your guns (like a gun spawner)", MTool::Weapons},
        {ICON_FA_INDUSTRY, "Workstation (career mode): a bench, a lathe, a mill, a test bench, or your shop counter", MTool::Station},
        {ICON_FA_DOOR_OPEN, "Door: click a floor in a doorway. F opens and shuts it on foot (it swings away from you)", MTool::Door},
        {ICON_FA_BULLSEYE, "Paper target: click the floor (it stands at chest height) or a wall. Pick it to make it move side to side", MTool::Target},
        {ICON_FA_COUCH, "Furniture: shelves, tables, racks, or your own pieces (make them from blocks in the Select panel)", MTool::Furniture},
        {ICON_FA_HAND_HOLDING, "Things to pick up: crates, cans, bottles, your own props, and guns. Put them on shelves and tables", MTool::Prop},
        {ICON_FA_RADIO, "Radio: plays an internet station from where it stands. On foot, F at it to tune it", MTool::Radio}};
    // the career shop is for building your business: no combat things (start, NPCs, gun spawners, armour, attachment
    // and weapon crates, ammo, targets); the sandbox has no workstations (they only mean something in career mode)
    const bool shop = !ME.careerFile.empty();
    auto shown = [&](MTool t) {
        if (shop) return !(t == MTool::Start || t == MTool::Npc || t == MTool::Spawner || t == MTool::Armor || t == MTool::Crate
                           || t == MTool::Ammo || t == MTool::Weapons || t == MTool::Target);
        return t != MTool::Station;
    };
    if (!shown(ME.tool)) ME.tool = MTool::Select;
    int i = 0;
    for (auto& t : tools) {
        if (!shown(t.t)) continue;
        if (iconBtn(t.icon, t.tip, ME.tool == t.t && !(t.t == MTool::Select && (ME.rotateMode || ME.cornerMode)))) { ME.tool = t.t; ME.clipPts.clear(); ME.rotateMode = ME.cornerMode = false; }
        if (t.t == MTool::Select) {   // Rotate: Select with rotation rings, as in the gun designer; Corners: drag the block's corners
            gap(3);
            if (iconBtn(ICON_FA_ARROWS_ROTATE, "Rotate: rings round the selection. Drag a ring to turn blocks (or a thing) about that axis. Ctrl: 15 degree steps", ME.tool == MTool::Select && ME.rotateMode)) {
                ME.tool = MTool::Select; ME.rotateMode = true; ME.cornerMode = false; ME.clipPts.clear();
            }
            gap(3);
            if (iconBtn(ICON_FA_DRAW_POLYGON, "Corners: drag the selected block's corners to any shape (Alt: up and down). Snaps to the grid", ME.tool == MTool::Select && ME.cornerMode)) {
                ME.tool = MTool::Select; ME.cornerMode = true; ME.rotateMode = false; ME.clipPts.clear();
            }
            gap(3);
            if (iconBtn(ICON_FA_ARROWS_TO_CIRCLE, CFG.editArrows ? "Move arrows: on (on the selection, and the picked corner in Corners). Click to hide them" : "Move arrows: off. Click to show them", CFG.editArrows)) { CFG.editArrows = !CFG.editArrows; saveSettings(); }
        }
        gap(t.t == MTool::Select || t.t == MTool::Clip ? 12 : 3);
        i++;
    }
    if (iconBtn(ICON_FA_ROTATE_LEFT, "Undo (Ctrl+Z)")) meUndo();
    gap(3);
    if (iconBtn(ICON_FA_ROTATE_RIGHT, "Redo (Ctrl+Y)")) meRedo();
    gap(18);
    bool imp = CFG.mapUnit >= 2;
    const double* steps = imp ? GRID_I : GRID_M;
    int gi = -1;
    for (int k = 0; k < 7; k++) if (std::fabs(ME.grid - steps[k]) < 1e-6) gi = k;
    if (gi < 0) {   // (a step from the other system: the nearest of this one's)
        gi = 0;
        for (int k = 0; k < 7; k++) if (std::fabs(std::log(steps[k] / ME.grid)) < std::fabs(std::log(steps[gi] / ME.grid))) gi = k;
        ME.grid = steps[gi];
    }
    ImGui::SetNextItemWidth(130 * S());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
    if (ImGui::BeginCombo("##grid", ("Grid " + meLen(ME.grid)).c_str())) {
        for (int k = 0; k < 7; k++) if (ImGui::Selectable(("Grid " + meLen(steps[k])).c_str(), k == gi)) ME.grid = steps[k];
        ImGui::EndCombo();
    }
    gap(8);
    ImGui::SetNextItemWidth(120 * S());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
    lenDrag("##height", ME.height, ME.grid);
    ME.height = std::max(ME.height, 0.01);
    if (tipHover()) ImGui::SetTooltip("Height of new brushes");
    gap(8);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
    mapUnitPicker(ImGui::GetFrameHeight());
    gap(8);
    {   // the camera's flying speed (also the wheel while flying)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
        ImGui::SetNextItemWidth(130 * S());
        float sp = (float)ME.flySpeed;
        if (ImGui::SliderFloat("##camspeed", &sp, 0.5f, 300.f, ICON_FA_GAUGE_HIGH " %.0f m/s", ImGuiSliderFlags_Logarithmic)) ME.flySpeed = sp;
        if (tipHover()) ImGui::SetTooltip("Camera speed when flying (right mouse + WASD, Q/E down and up).\nThe wheel while flying changes it; Shift goes 4x faster, Ctrl 5x slower.\nMiddle mouse orbits, Shift+middle pans, the wheel zooms toward the mouse.");
    }
    // right side
    float right = 5 * (sz + 4 * S()) + 110 * S() + 10 * S();
    ImGui::SameLine(GetScreenWidth() - right);
    {   // how the view is lit while you build: click to go round the three
        static const char* tips[] = {"As players will see it: lit only by its daylight and its own lights.\nClick: a work light", "Work light: dark maps still show while you build, the lamps on top.\nClick: fullbright",
                                     "Fullbright: every face in its own colour, no shading, no fog: easiest for placing things.\nClick: as players will see it"};
        const char* icons[] = {ICON_FA_MOON, ICON_FA_SUN, ICON_FA_LIGHTBULB};
        ImGui::PushID("viewlight");
        if (iconBtn(icons[ME.lightMode], tips[ME.lightMode], ME.lightMode > 0)) ME.lightMode = (ME.lightMode + 1) % 3;
        ImGui::PopID();
    }
    gap(4);
    if (iconBtn(ICON_FA_BORDER_ALL, "Ground grid", ME.showGrid)) ME.showGrid = !ME.showGrid;
    gap(4);
    if (iconBtn(ICON_FA_MAP, ME.top ? "3D view (V)" : "Top view (V)", ME.top)) { if (!ME.top) ME.topC = {ME.cam.x, 0, ME.cam.z}; ME.top = !ME.top; }
    gap(4);
    if (iconBtn(ICON_FA_EXPAND, "Focus on the selection (F)")) meFocus();
    gap(4);
    if (iconBtn(ICON_FA_FLOPPY_DISK, "Save (Ctrl+S)")) meSave();
    gap(8);
    if (!ME.careerFile.empty()) {   // (your workshop: Walk is on the left, and goes through the shop's checks)
        if (accentButton(ICON_FA_PERSON_WALKING "  Walk", true, {104 * S(), sz})) shopWalk();
        if (tipHover()) ImGui::SetTooltip("Back on foot in your shop (B).");
    } else {
        if (accentButton(ICON_FA_PERSON_WALKING "  Walk", true, {104 * S(), sz})) meWalk(IsKeyDown(KEY_LEFT_SHIFT));
        tutAnchor("map:walk");
        if (tipHover()) ImGui::SetTooltip("Walk the map from the player start (F5).\nShift: start where the camera is.");
    }
    ImGui::End();
}

// ---------------------------------------------------------------- panel
static int tab = -1;

// Everything in the map, to find, pick and hide things (blocks and things placed; furniture's own blocks come with it)
static void outliner() {
    auto& M = ME.map;
    if (!section("Everything in the map", false)) return;
    static char filt[64] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##outfilt", ICON_FA_MAGNIFYING_GLASS "  Find (a texture, a kind of thing...)", filt, sizeof filt);
    std::string f = filt; for (auto& c : f) c = (char)std::tolower((unsigned char)c);
    float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    ImGui::BeginDisabled(ME.sel.empty());
    if (ImGui::Button(ICON_FA_EYE_SLASH "  Hide selected", {w2, 0})) meHideSelected();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_EYE "  Show all", {w2, 0})) meShowAll();
    struct Row { std::string label; int brush = -1, ent = -1; };
    std::vector<Row> rows;
    for (size_t i = 0; i < M.ents.size(); i++) {
        const auto& e = M.ents[i];
        std::string t = e.type; for (auto& c : t) if (c == '_') c = ' ';
        if (!t.empty()) t[0] = (char)std::toupper((unsigned char)t[0]);
        if (!e.ref.empty() && e.type != "light") t += ": " + e.ref;
        rows.push_back({t, -1, (int)i});
    }
    for (size_t i = 0; i < M.brushes.size(); i++) {
        const auto& b = M.brushes[i];
        if (b.furn) continue;
        gmap::V3 a, c; gmap::brushBounds(b, a, c);
        std::string mat = b.faces.empty() ? "" : gmap::materialNames()[std::clamp(b.faces[0].mat, 0, (int)gmap::materialNames().size() - 1)];
        rows.push_back({TextFormat("Block %zu  %s  %.1f x %.1f x %.1f m%s", i + 1, mat.c_str(), c.x - a.x, c.y - a.y, c.z - a.z, b.locked ? "  (locked)" : ""), (int)i, -1});
    }
    if (!f.empty()) {
        std::vector<Row> keep;
        for (auto& r : rows) { std::string l = r.label; for (auto& c : l) c = (char)std::tolower((unsigned char)c); if (l.find(f) != std::string::npos) keep.push_back(r); }
        rows.swap(keep);
    }
    ImGui::TextDisabled("%zu shown. Click to pick, double-click to fly there.", rows.size());
    ImGui::BeginChild("##outlist", {0, 260 * S()}, ImGuiChildFlags_Borders);
    ImGuiListClipper clip;
    clip.Begin((int)rows.size());
    while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
            const Row& r = rows[k];
            ImGui::PushID(k);
            if (r.brush >= 0) {   // the eye: hide or show this block
                bool hid = M.brushes[r.brush].hidden;
                if (ImGui::SmallButton(hid ? ICON_FA_EYE_SLASH : ICON_FA_EYE)) { M.brushes[r.brush].hidden = !hid; ME.version++; if (!hid) ME.sel.erase(std::remove(ME.sel.begin(), ME.sel.end(), r.brush), ME.sel.end()); }
                ImGui::SameLine();
            } else { ImGui::TextDisabled(ICON_FA_LOCATION_DOT); ImGui::SameLine(); }
            bool on = r.brush >= 0 ? std::find(ME.sel.begin(), ME.sel.end(), r.brush) != ME.sel.end() : ME.ent == r.ent;
            bool dim = r.brush >= 0 && M.brushes[r.brush].hidden;
            if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(r.label.c_str(), on, ImGuiSelectableFlags_AllowDoubleClick)) {
                bool add = ImGui::GetIO().KeyShift;
                if (r.brush >= 0 && !M.brushes[r.brush].hidden) {
                    if (!add) ME.sel.clear();
                    if (!on) ME.sel.push_back(r.brush);
                    ME.ent = -1; ME.face = -1;
                } else if (r.ent >= 0) { ME.sel.clear(); ME.face = -1; ME.ent = r.ent; }
                if (ImGui::IsMouseDoubleClicked(0)) meFocus();
            }
            if (dim) ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

static void editTab() {
    auto& M = ME.map;
    if (ME.ent >= 0 && ME.ent < (int)M.ents.size()) {
        auto& e = M.ents[ME.ent];
        if (e.type == "npc_spawn") {
            heading("NPC");
            ImGui::TextDisabled("NPCs start here.");
            prop("Which");
            if (ImGui::BeginCombo("##npcref", e.ref.empty() ? "Choose..." : e.ref.c_str())) {
                for (auto& f : npcFiles()) if (ImGui::Selectable(f.c_str(), e.ref == f)) { meSnapshot(); e.ref = f; meChanged(); }
                ImGui::EndCombo();
            }
            double cnt = e.count;
            prop("How many");
            if (ImGui::InputDouble("##npccount", &cnt, 1, 5, "%.0f")) { meSnapshot(); e.count = std::clamp((int)std::lround(cnt), 1, npcai::NPC_LIMIT); meChanged(); }
            int total = npcai::npcCount(M);
            if (total > npcai::NPC_LIMIT) ImGui::TextColored({1.0f, 0.55f, 0.3f, 1}, "This map has %d NPCs: only %d come at once. The first spawn points placed come first.", total, npcai::NPC_LIMIT);
            else ImGui::TextDisabled("%d of the %d NPCs a map can have.", total, npcai::NPC_LIMIT);
            ImGui::TextDisabled("Saved with the map: a copy of this NPC goes in the map file.");
        } else if (e.type == "gun_spawner") {
            heading("Gun spawner");
            ImGui::TextDisabled("Walk up to it on the map and press F: take any of your guns.");
        } else if (e.type == "light") {
            heading("Light");
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Lights everything within its reach. It doesn't cast shadows yet: it shines through walls, so keep its reach to its room.");
            ImGui::PopTextWrapPos();
            static bool ed = false;
            auto tr = [&](bool changed) {
                if (changed) { if (!ed) { meSnapshot(); ed = true; } ME.version++; }
                if (ImGui::IsItemDeactivatedAfterEdit()) { ed = false; meChanged(); }
            };
            float col[3] = {(float)e.color[0], (float)e.color[1], (float)e.color[2]};
            prop("Colour");
            if (ImGui::ColorEdit3("##lcol", col, ImGuiColorEditFlags_NoInputs)) { for (int k = 0; k < 3; k++) e.color[k] = col[k]; tr(true); } else tr(false);
            ImGui::SameLine(0, 8 * S());
            struct Pre { const char* n; double c[3]; } pres[] = {{"Warm", {1.0, 0.82, 0.58}}, {"White", {0.95, 0.97, 1.0}}, {"Blue", {0.72, 0.84, 1.0}}, {"Red", {1.0, 0.18, 0.12}}};
            for (auto& q : pres) { if (ImGui::SmallButton(q.n)) { meSnapshot(); for (int k = 0; k < 3; k++) e.color[k] = q.c[k]; meChanged(); } ImGui::SameLine(0, 2 * S()); }
            ImGui::NewLine();
            float pw = (float)e.power, rg = (float)e.range;
            prop("Brightness", "How bright it is close up: 3 a room lamp, 30 a work light, 100 and up a floodlight.");
            if (ImGui::SliderFloat("##lpow", &pw, 0, 200, "%.1f", ImGuiSliderFlags_Logarithmic)) { e.power = pw; tr(true); } else tr(false);
            typeable();
            prop("Reach", "Nothing past this distance gets its light.");
            { double rgm = rg; if (lenSlider("##lrange", rgm, 1, 150, true)) { e.range = rgm; tr(true); } else tr(false); }
            typeable();
            bool spot = e.cone > 0;
            prop("Spotlight", "A cone of light, aimed (a floodlight, a ceiling spot). Off: it shines all round.");
            if (ImGui::Checkbox("##lspot", &spot)) { meSnapshot(); e.cone = spot ? 60 : 0; meChanged(); }
            if (spot) {
                float cn = (float)e.cone, pt = (float)e.pitch;
                prop("Cone");
                if (ImGui::SliderFloat("##lcone", &cn, 5, 170, "%.0f deg")) { e.cone = cn; tr(true); } else tr(false);
                typeable();
                prop("Aim up / down", "-90 straight down, 0 level, 90 straight up. Facing (below) turns it.");
                if (ImGui::SliderFloat("##lpitch", &pt, -90, 90, "%.0f deg")) { e.pitch = pt; tr(true); } else tr(false);
                typeable();
            }
        } else if (e.type == "radio") {
            heading("Radio");
            gmap::Entity t = e;
            if (radioControls(t)) { meSnapshot(); ME.map.ents[ME.ent] = t; meChanged(); }
        } else if (e.type == "door") {
            heading("Door");
            static const char* styles[][2] = {{"wood", "Wood, panelled"}, {"metal", "Steel, with a window"}, {"glass", "Glass"}};
            prop("Style");
            const char* cur = "Wood, panelled"; for (auto& k : styles) if (e.ref == k[0]) cur = k[1];
            if (ImGui::BeginCombo("##edstyle", cur)) { for (auto& k : styles) if (ImGui::Selectable(k[1], e.ref == k[0])) { meSnapshot(); e.ref = k[0]; meChanged(); } ImGui::EndCombo(); }
            prop("Width");
            double w = e.width;
            if (lenDrag("##edwidth", w, 0.05)) { e.width = std::clamp(w, 0.4, 4.0); ME.version++; }
            if (ImGui::IsItemDeactivatedAfterEdit()) meChanged();
            ImGui::TextDisabled("On foot, F opens and shuts it. NPCs and bullets aren't stopped by doors yet.");
        } else if (gmap::isDisplay(e.type) || gmap::isDecor(e.type)) {
            static const char* names[][2] = {{"display_case", "Gun display case"}, {"display_rack", "Gun wall rack"}, {"display_stand", "Accessory stand"},
                                             {"decor_plant", "Plant"}, {"decor_poster", "Poster"}, {"decor_rug", "Rug"}, {"decor_sign", "Shop sign"}, {"decor_seat", "Bench seat"}};
            const char* nm = "Display"; for (auto& p : names) if (e.type == p[0]) nm = p[1];
            heading(nm);
            bool inStore = ME.map.store.has(e.pos, 0.3);
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled(inStore ? "In your storefront: it counts toward the store's rating." : "Outside your storefront: it doesn't count toward the store's rating.");
            ImGui::PopTextWrapPos();
            if (e.type == "display_case" || e.type == "display_rack") {
                prop("Gun shown", "One of your guns on show. Customers who see it may ask for it (and pay a little more for it).");
                if (ImGui::BeginCombo("##dispgun", e.ref.empty() ? "Empty" : e.ref.c_str())) {
                    if (ImGui::Selectable("Empty", e.ref.empty())) { meSnapshot(); e.ref.clear(); meChanged(); }
                    for (auto& g : careerGuns()) if (ImGui::Selectable(g.name.c_str(), e.ref == g.name)) { meSnapshot(); e.ref = g.name; meChanged(); }
                    ImGui::EndCombo();
                }
            }
        } else if (e.type.rfind("station_", 0) == 0 || e.type.rfind("shop_", 0) == 0) {
            static const char* names[][2] = {{"station_bench", "Workbench"}, {"station_lathe", "Lathe"}, {"station_mill", "Milling machine"}, {"station_qc", "Test bench"}, {"shop_counter", "Shop counter"}, {"shop_computer", "Office computer"}, {"shop_door", "Front door"}};
            const char* nm = "Workstation"; for (auto& p : names) if (e.type == p[0]) nm = p[1];
            heading(nm);
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled(e.type == "shop_counter" ? "Career mode: customers come to your counter each day, if you have one."
                                : e.type == "shop_computer" ? "Career mode: your online store, contracts, staff and building."
                                : e.type == "shop_door" ? "Career mode: close up here to end the day."
                                : e.type == "station_qc" ? "Career mode: a finisher here test-fires copies and catches most flaws."
                                : e.type == "station_bench" ? "Career mode: where you and your assemblers work. Machinists can use one too, slower."
                                : "Career mode: where a machinist works fastest (turned and cut parts).");
            ImGui::PopTextWrapPos();
        } else if (e.type == "target_paper") {
            heading("Paper target");
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Scores rings 1 to 10 on the range. Shots go through the paper.");
            ImGui::PopTextWrapPos();
            bool moving = e.ref == "moving";
            if (ImGui::Checkbox("Moves side to side", &moving)) { meSnapshot(); e.ref = moving ? "moving" : ""; if (e.width < 0.2) e.width = 3; if (e.range < 0.05) e.range = 1.2; meChanged(); }
            if (moving) {
                float tr = (float)e.width, sp = (float)e.range;
                { double trm = tr; prop("How far"); if (lenSlider("##mvfar", trm, 0.5, 20)) { e.width = trm; meChanged(); } }
                { double spm = sp; prop("How fast (per second)"); if (lenSlider("##mvfast", spm, 0.2, 8)) { e.range = spm; meChanged(); } }
                ImGui::TextDisabled("It slides along its face, slowing at each end. A walk is about 1.4 m/s, a run 4.");
            }
        } else if (e.type == "ammo_box") {
            heading("Ammo box");
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Walk up to it and press F: every gun you carry gets its spare ammunition filled (ten magazines' worth). It never runs out.");
            ImGui::PopTextWrapPos();
        } else if (e.type == "weapon_crate") {
            heading("Weapon crate");
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Walk up to it and press F: take any of your guns, as from a gun spawner.");
            ImGui::PopTextWrapPos();
        } else if (e.type == "attachment_crate") {
            heading("Attachment crate");
            ImGui::TextDisabled("Walk up to it on the map and press F: fit attachments from your Attachments folder\nto the gun in your hands (or load a loadout).");
        } else if (e.type == "furniture" || e.type == "prop") {
            bool isProp = e.type == "prop";
            heading(isProp ? "Prop" : "Furniture");
            static char nm[64];
            static int forEnt = -1;
            if (forEnt != ME.ent || !ImGui::IsAnyItemActive()) { std::snprintf(nm, sizeof nm, "%s", e.ref.c_str()); forEnt = ME.ent; }
            prop("Name");
            if (ImGui::InputText("##fname", nm, sizeof nm, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) { meSnapshot(); e.ref = nm; meChanged(); }
            ImGui::PushTextWrapPos(0);
            if (isProp) {
                ImGui::TextDisabled("Players pick it up with F, carry it, put it down or throw it. Shots knock it about.");
                double kg = gmap::propMass(e);
                prop("Weight", "Heavier things are thrown less far and take more to knock over.");
                if (ImGui::DragScalar("##pmass", ImGuiDataType_Double, &kg, 0.05f, nullptr, nullptr, "%.2f kg")) { e.mass = std::clamp(kg, 0.05, 200.0); ME.dirty = true; }
                if (ImGui::IsItemActivated()) meSnapshot();
                if (ImGui::IsItemDeactivatedAfterEdit()) meChanged();
            } else {
                ImGui::TextDisabled("%zu block%s, solid like the rest of the map: players walk round it and stand on it.", e.parts.size(), e.parts.size() == 1 ? "" : "s");
                if (!e.items.empty()) ImGui::TextDisabled("%zu thing%s on it (props and guns players can pick up).", e.items.size(), e.items.size() == 1 ? "" : "s");
            }
            ImGui::PopTextWrapPos();
            if (ImGui::Button(isProp ? "Save in my props" : "Save in my furniture", {-1, 0})) meSaveFurniture();
            if (tipHover()) ImGui::SetTooltip("Into your Furniture folder: place it again with the %s tool in any map.", isProp ? "Prop" : "Furniture");
            if (ImGui::Button("Break it apart into blocks", {-1, 0})) { meBreakApart(); return; }
            if (tipHover()) ImGui::SetTooltip("Back into the blocks it was made of (and its things), to change it. Make it again from the selection.");
        } else if (e.type == "gun_item") {
            heading("Gun");
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Lies here for players to pick up with F (it goes in your hands, as from a spawner).");
            ImGui::PopTextWrapPos();
            prop("Which");
            if (ImGui::BeginCombo("##gunref", gunItemLabel(e.ref).c_str())) {
                for (auto& c : furnitureChoices(true)) {
                    if (c.pick.rfind("gun:", 0) != 0) continue;
                    if (ImGui::Selectable(c.label.c_str() + 5, e.ref == c.pick.substr(4))) { meSnapshot(); e.ref = c.pick.substr(4); meChanged(); }
                }
                ImGui::EndCombo();
            }
            if (!gunItemDesign(e.ref)) ImGui::TextColored({1.0f, 0.55f, 0.3f, 1}, "That gun's blueprint is missing.");
        } else if (e.type == "armor_plate") {
            heading("Armour plates");
            double cnt = e.count;
            prop("Plates here", "How many plates lie in this stack. Each fills 50 of the armour bar (150 at most); a full bar leaves the rest lying.");
            if (ImGui::InputDouble("##platecount", &cnt, 1, 1, "%.0f")) { meSnapshot(); e.count = std::clamp((int)std::lround(cnt), 1, 9); meChanged(); }
            bool back = e.respawn > 0;
            prop("Comes back", "Taken plates come back after a while: for arenas and long fights.");
            if (ImGui::Checkbox("##plateback", &back)) { meSnapshot(); e.respawn = back ? 30 : 0; meChanged(); }
            if (back) {
                prop("After");
                if (ImGui::InputDouble("##platesec", &e.respawn, 5, 30, "%.0f s")) { e.respawn = std::clamp(e.respawn, 1.0, 3600.0); meChanged(); }
            }
            ImGui::TextDisabled("Players walk over them to pick them up.");
        } else {
        heading("Player start");
        ImGui::TextDisabled("Where you appear when you walk the map.");
        }
        static bool editing = false;
        prop("Facing");
        float yaw = (float)e.yaw;
        if (ImGui::SliderFloat("##yaw", &yaw, 0, 360, "%.0f deg")) { if (!editing) { meSnapshot(); editing = true; } e.yaw = yaw; meRebake(); ME.version++; } typeable();
        if (ImGui::IsItemDeactivatedAfterEdit()) { editing = false; meChanged(); }
        for (int a = 0; a < 3; a++) {
            static const char* L[] = {"X", "Height", "Z"};
            prop(L[a]);
            double* c = a == 0 ? &e.pos.x : a == 1 ? &e.pos.y : &e.pos.z;
            ImGui::PushID(a);
            if (lenDrag("##p", *c, ME.grid)) { if (!editing) { meSnapshot(); editing = true; } meRebake(); ME.version++; }
            if (ImGui::IsItemDeactivatedAfterEdit()) { editing = false; meChanged(); }
            ImGui::PopID();
        }
        ImGui::Dummy({0, 6 * S()});
        if (dangerButton("Delete", {-1, 0})) meDelete();
        return;
    }
    if (ME.sel.empty()) {
        heading("Nothing selected");
        ImGui::TextWrapped("Pick a tool in the top bar and drag in the view to build. Click a brush to select it, or Ctrl+click to pick one of its faces.");
        ImGui::Dummy({0, 4 * S()});
        ImGui::TextDisabled("Arrows nudge (Shift: x4), Page Up/Down lift\nR turns 90 (Alt: 15), H hollows\nCtrl+C / Ctrl+V copy and paste at the mouse\nCtrl+D duplicates, Ctrl+A selects all\nF focuses, V top view, F5 walks");
        ImGui::Dummy({0, 6 * S()});
        ImGui::TextDisabled("%zu brushes in this map", M.brushes.size());
        return;
    }
    if (ME.sel.size() == 1) heading(ME.face >= 0 ? "Face" : "Brush");
    else { char b[48]; std::snprintf(b, sizeof b, "%zu brushes", ME.sel.size()); heading(b); }
    if (ME.face < 0 && section("Make furniture", false)) {
        static char fn[64] = "My shelf";
        prop("Name");
        ImGui::InputText("##mkfname", fn, sizeof fn);
        if (ImGui::Button("Make furniture", {-1, 0})) meMakeFurniture(false, fn);
        if (tipHover()) ImGui::SetTooltip("The selected blocks become one piece: it moves and turns as one, and stays solid.\nProps and guns standing on it come with it. Save it in your Furniture folder from its panel.");
        if (ImGui::Button("Make a prop (players pick it up)", {-1, 0})) meMakeFurniture(true, fn);
        if (tipHover()) ImGui::SetTooltip("The selected blocks become one thing players can pick up, carry and throw.\nKeep it small: a box, a can, a tool.");
    }

    // position and size of a single brush (its bounding box)
    if (ME.sel.size() == 1 && ME.face < 0) {
        gmap::Brush& b = M.brushes[ME.sel[0]];
        V3 mn, mx;
        gmap::brushBounds(b, mn, mx);
        static bool editing = false;
        static const char* AX[] = {"X", "Y (up)", "Z"};
        if (section("Position and size", true)) {
            for (int a = 0; a < 3; a++) {
                double lo = a == 0 ? mn.x : a == 1 ? mn.y : mn.z, hi = a == 0 ? mx.x : a == 1 ? mx.y : mx.z;
                double pos = lo, size = hi - lo;
                ImGui::PushID(a);
                prop(AX[a]);
                float w = (ImGui::GetContentRegionAvail().x - 6 * S()) / 2;
                ImGui::SetNextItemWidth(w);
                if (lenDrag("##pos", pos, ME.grid)) {
                    if (!editing) { meSnapshot(); editing = true; }
                    V3 d; (a == 0 ? d.x : a == 1 ? d.y : d.z) = pos - lo;
                    gmap::translate(b, d); ME.version++;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) { editing = false; meChanged(); }
                if (tipHover()) ImGui::SetTooltip("Lowest corner");
                ImGui::SameLine(0, 6 * S());
                ImGui::SetNextItemWidth(w);
                if (lenDrag("##size", size, ME.grid)) {
                    // grow from the far side: push the face pointing along this axis, if the brush has one
                    V3 axis; (a == 0 ? axis.x : a == 1 ? axis.y : axis.z) = 1;
                    for (size_t f = 0; f < b.faces.size(); f++)
                        if (gmap::dot(b.faces[f].n, axis) > 0.999) {
                            if (!editing) { meSnapshot(); editing = true; }
                            if (gmap::pushFace(b, (int)f, std::max(size, 0.005) - (hi - lo))) ME.version++;
                            break;
                        }
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) { editing = false; meChanged(); }
                if (tipHover()) ImGui::SetTooltip("Size");
                ImGui::PopID();
            }
        }
    }
    if (section("Arrange", true)) {
        float w = (ImGui::GetContentRegionAvail().x - 3 * 4 * S()) / 4;
        if (ImGui::Button("-90", {w, 0})) meRotate(-90);
        ImGui::SameLine(0, 4 * S()); if (ImGui::Button("-15", {w, 0})) meRotate(-15);
        ImGui::SameLine(0, 4 * S()); if (ImGui::Button("+15", {w, 0})) meRotate(15);
        ImGui::SameLine(0, 4 * S()); if (ImGui::Button("+90", {w, 0})) meRotate(90);
        if (tipHover()) ImGui::SetTooltip("Turn around the vertical axis (R turns 90)");
        float w2 = (ImGui::GetContentRegionAvail().x - 4 * S()) / 2;
        if (ImGui::Button("Mirror X", {w2, 0})) meMirror(0);
        ImGui::SameLine(0, 4 * S()); if (ImGui::Button("Mirror Z", {w2, 0})) meMirror(2);
        if (ImGui::Button("Duplicate", {w2, 0})) meDuplicate();
        ImGui::SameLine(0, 4 * S()); if (dangerButton("Delete", {w2, 0})) meDelete();
        prop("Wall thickness", "Used by Make hollow.");
        lenDrag("##wall", ME.wall, 0.01);
        ME.wall = std::max(ME.wall, 0.01);
        if (ImGui::Button("Make hollow (H)", {-1, 0})) meHollow();
        if (tipHover()) ImGui::SetTooltip("Turns each selected brush into walls around its inside: a quick room.");
    }
    if (section("Texture", true)) {
        // the selected face, or every face of the selection
        std::vector<gmap::Face*> faces;
        if (ME.face >= 0 && ME.sel.size() == 1) faces.push_back(&M.brushes[ME.sel[0]].faces[ME.face]);
        else for (int i : ME.sel) for (auto& f : M.brushes[i].faces) faces.push_back(&f);
        if (!faces.empty()) {
            int m = faces[0]->mat;
            ImGui::AlignTextToFramePadding();
            Texture2D t = mapTexture(m);
            rlImGuiImageSize(&t, (int)(28 * S()), (int)(28 * S()));
            ImGui::SameLine();
            ImGui::TextUnformatted(gmap::materialNames()[m].c_str());
            static bool editing = false;
            double sc = faces[0]->scale, su = faces[0]->shiftU, sv = faces[0]->shiftV;
            auto edit = [&](bool changed, auto apply) {
                if (changed) { if (!editing) { meSnapshot(); editing = true; } for (auto* f : faces) apply(*f); ME.version++; }
                if (ImGui::IsItemDeactivatedAfterEdit()) { editing = false; meChanged(); }
            };
            prop("Scale", "Bigger numbers stretch the texture.");
            edit(ImGui::DragScalar("##sc", ImGuiDataType_Double, &sc, 0.01f, nullptr, nullptr, "%.2f"), [&](gmap::Face& f) { f.scale = std::max(0.05, sc); });
            prop("Shift across");
            edit(lenDrag("##su", su, 0.01), [&](gmap::Face& f) { f.shiftU = su; });
            prop("Shift up");
            edit(lenDrag("##sv", sv, 0.01), [&](gmap::Face& f) { f.shiftV = sv; });
            {   // a colour over the texture (white: none)
                unsigned c0 = faces[0]->color;
                float rgb[3] = {((c0 >> 24) & 255) / 255.f, ((c0 >> 16) & 255) / 255.f, ((c0 >> 8) & 255) / 255.f};
                prop("Colour", "Tints the texture: pick white for the texture as it is.\nPaints the picked face (Ctrl+click), or every face of the selected blocks.");
                auto pack = [&](const float* c) { return ((unsigned)std::lround(c[0] * 255) << 24) | ((unsigned)std::lround(c[1] * 255) << 16) | ((unsigned)std::lround(c[2] * 255) << 8) | 255u; };
                ImGui::SetNextItemWidth(-60 * S());
                edit(ImGui::ColorEdit3("##fcol", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel), [&](gmap::Face& f) { f.color = pack(rgb); });
                ImGui::SameLine();
                if (ImGui::Button("None##fcolw")) { meSnapshot(); for (auto* f : faces) f->color = 0xFFFFFFFFu; meChanged(); }
                if (tipHover()) ImGui::SetTooltip("Back to the texture's own colour.");
                static const unsigned SW[] = {0xFFFFFFFFu, 0xD9D2C3FFu, 0x8C8C8CFFu, 0x3A3D42FFu, 0xB5442EFFu, 0xD98E2BFFu, 0xE3C64AFFu, 0x5E8C4AFFu, 0x3E7CA8FFu, 0x6A4FA3FFu, 0x8A5A3CFFu, 0xEFA7B6FFu};
                for (int i = 0; i < 12; i++) {
                    if (i % 6) ImGui::SameLine(0, 3);
                    unsigned c = SW[i];
                    ImGui::PushID(i);
                    if (ImGui::ColorButton("##sw", ImVec4(((c >> 24) & 255) / 255.f, ((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, 1), ImGuiColorEditFlags_NoTooltip, {18 * S(), 18 * S()})) {
                        meSnapshot(); for (auto* f : faces) f->color = c; meChanged();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::TextDisabled("Pick a new texture on the Textures tab.");
        }
    }
}

static void groupTools() {   // groups of objects: save the selection as one, place it again
    heading("Groups");
    static char gname[64] = "My group";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##gname", gname, sizeof gname);
    if (ImGui::Button("Save the selection as a group", {-1, 0})) meSaveGroup(gname);
    if (tipHover()) ImGui::SetTooltip("The selected brushes (Shift-click adds more) and the selected object (an NPC, a gun spawner),\nsaved as one group in your Groups folder. Place it again in this map or any other.");
    auto groups = meGroupFiles();
    if (!groups.empty()) {
        prop("Place");
        if (ImGui::BeginCombo("##gplace", "Choose a group...")) {
            for (auto& g : groups) if (ImGui::Selectable(g.c_str())) mePlaceGroup(g);
            ImGui::EndCombo();
        }
    }
}
static void toolOptions() {
    if (ME.tool == MTool::Furniture || ME.tool == MTool::Prop) {
        bool props = ME.tool == MTool::Prop;
        std::string& pick = props ? ME.propPick : ME.furnPick;
        auto list = furnitureChoices(props);
        std::string cur = "Choose...";
        for (auto& c : list) if (c.pick == pick) cur = c.label;
        prop(props ? "Place" : "Piece");
        if (ImGui::BeginCombo("##furnpick", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            bool mine = false, guns = false;
            for (auto& c : list) {
                if (!mine && c.pick.rfind("user:", 0) == 0) { ImGui::SeparatorText("Yours"); mine = true; }
                if (!guns && c.pick.rfind("gun:", 0) == 0) { ImGui::SeparatorText("Guns"); guns = true; }
                if (ImGui::Selectable(c.label.c_str(), c.pick == pick)) pick = c.pick;
            }
            ImGui::EndCombo();
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(props ? "Click a shelf, a table or the floor. Make your own from blocks: select them, then Make a prop."
                                  : "Click the floor (it faces you) or a wall (its back to it). Make your own from blocks: select them, then Make furniture.");
        ImGui::PopTextWrapPos();
    }
    if (ME.tool == MTool::Npc) {
        prop("NPC");
        if (ImGui::BeginCombo("##npcpick", ME.npcPick.empty() ? "Choose..." : ME.npcPick.c_str())) {
            auto files = npcFiles();
            if (files.empty()) ImGui::TextDisabled("None yet: make one in the NPC editor.");
            for (auto& f : files) if (ImGui::Selectable(f.c_str(), ME.npcPick == f)) ME.npcPick = f;
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("A copy of each NPC you place is saved in the map file.");
    }
    if (ME.tool == MTool::Door) {
        static const char* styles[][2] = {{"wood", "Wood, panelled"}, {"metal", "Steel, with a window"}, {"glass", "Glass"}};
        prop("Style");
        const char* cur = "Wood, panelled"; for (auto& k : styles) if (ME.doorStyle == k[0]) cur = k[1];
        if (ImGui::BeginCombo("##dstyle", cur)) { for (auto& k : styles) if (ImGui::Selectable(k[1], ME.doorStyle == k[0])) ME.doorStyle = k[0]; ImGui::EndCombo(); }
        prop("Width");
        lenDrag("##dwidth", ME.doorWidth, 0.05);
        ME.doorWidth = std::clamp(ME.doorWidth, 0.4, 4.0);
        ImGui::TextDisabled("It stands across the doorway, facing you: turn the view to turn it. 2.1 m tall.");
    }
    if (ME.tool == MTool::Station) {
        static const char* kinds[][2] = {{"bench", "Workbench"}, {"lathe", "Lathe"}, {"mill", "Milling machine"}, {"qc", "Test bench"}, {"counter", "Shop counter"}, {"computer", "Office computer"}, {"door", "Front door"},
                                         {"display_case", "Gun display case"}, {"display_rack", "Gun wall rack"}, {"display_stand", "Accessory stand"},
                                         {"decor_plant", "Plant"}, {"decor_poster", "Poster"}, {"decor_rug", "Rug"}, {"decor_sign", "Shop sign"}, {"decor_seat", "Bench seat"}};
        prop("Which", "Career mode: your staff work at these (a machinist fastest at a lathe or mill, a finisher at a test bench).\nThe counter is where customers come in; the computer runs your online store, contracts and staff;\nthe front door is where you close up at the end of the day. Your building holds only so many workstations.\n"
                      "Displays and decoration in the storefront raise its rating: more customers, bigger budgets. Put your guns on the displays.");
        const char* cur = "Workbench"; for (auto& k : kinds) if (ME.stationKind == k[0]) cur = k[1];
        if (ImGui::BeginCombo("##station", cur)) {
            for (auto& k : kinds) if (ImGui::Selectable(k[1], ME.stationKind == k[0])) ME.stationKind = k[0];
            ImGui::EndCombo();
        }
    }
    if (ME.tool == MTool::Cylinder || ME.tool == MTool::Cone || ME.tool == MTool::Sphere) { prop("Sides"); ImGui::SliderInt("##sides", &ME.cylSides, 3, 32); typeable(); }
    if (ME.tool == MTool::Stairs) { prop("Steps"); ImGui::SliderInt("##steps", &ME.stairSteps, 2, 32); typeable(); }
}

static void texturesTab() {
    {   // the player's own pictures: add one, or open the folder they live in
        std::string none;
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Add a picture...", {w2, 0})) {
            std::string p;
            if (pickImageFile(p)) { std::string n = importTexture(p); if (!n.empty()) toast("Added " + n + "."); else toast("That file isn't a picture the game can read."); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open textures folder", {w2, 0})) { scanUserTextures(); openTexturesFolder(); }
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Your pictures (your Textures folder) come after the built-in ones. You can drag pictures onto the window too.");
        ImGui::PopTextWrapPos();
    }
    ImGui::TextDisabled(ME.sel.empty() ? "Click one to use it for new brushes." : ME.face >= 0 ? "Click one to put it on the selected face." : "Click one to put it on the selection.");
    ImGui::Dummy({0, 4 * S()});
    float cell = 62 * S(), pad = ImGui::GetStyle().FramePadding.x * 2;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 8 * S()) / (cell + pad + 8 * S())));
    auto& names = gmap::materialNames();
    for (int m = 0; m < (int)names.size(); m++) {
        ImGui::PushID(m);
        ImGui::BeginGroup();
        bool cur = m == ME.mat;
        if (cur) ImGui::PushStyleColor(ImGuiCol_Button, colAccent());
        Texture2D t = mapTexture(m);
        if (rlImGuiImageButtonSize("##t", &t, {cell, cell})) { uiTick(); meApplyMaterial(m); }
        if (cur) ImGui::PopStyleColor();
        std::string label = gmap::isUserMaterial(m) ? names[m].substr(5) : names[m];
        if (!gmap::isUserMaterial(m)) std::replace(label.begin(), label.end(), '_', ' ');
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell);
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::PopID();
        if ((m + 1) % perRow != 0) ImGui::SameLine(0, 8 * S());
    }
}

static bool colorRow(const char* label, double* c) {
    prop(label);
    float f[3] = {(float)c[0], (float)c[1], (float)c[2]};
    ImGui::PushID(label);
    bool r = ImGui::ColorEdit3("##c", f, ImGuiColorEditFlags_NoInputs);
    ImGui::PopID();
    if (r) for (int i = 0; i < 3; i++) c[i] = f[i];
    return r;
}

static void mapTab() {
    auto& L = ME.map.look;
    static bool editing = false;
    auto track = [&](bool changed) {
        if (changed) { if (!editing) { meSnapshot(); editing = true; } ME.version++; }
        if (ImGui::IsItemDeactivatedAfterEdit()) { editing = false; meChanged(); }
    };
    heading("Map");
    prop("Name");
    std::string name = ME.map.name;
    if (inputText("##name", name)) { ME.map.name = name; ME.dirty = true; }
    if (section("Light", true)) {
        float day = (float)L.daylight;
        prop("Daylight", "1: broad daylight. 0: night, or indoors with no windows: only the map's own lights (the Light tool) and a gun's light show anything.");
        track(ImGui::SliderFloat("##day", &day, 0, 1, "%.2f")); typeable(); L.daylight = std::clamp((double)day, 0.0, 1.0);
        bool timed = L.time >= 0;
        prop("Time of day", "The sun moves with the hour: rising at 6, high at noon, setting at 18; dawn and dusk glow, nights are dark\n"
                            "(lit by a faint moon and your lights). Off: the sun stays where Sun and sky puts it.");
        if (ImGui::Checkbox("##timed", &timed)) { meSnapshot(); L.time = timed ? 12 : -1; if (!timed) L.dayMinutes = 0; meChanged(); }
        if (timed) {
            float hr = (float)L.time;
            prop("Hour", "The time the map starts at (and stays at, unless the day runs).");
            track(ImGui::SliderFloat("##hour", &hr, 0, 24, gmap::clockText(hr).c_str())); typeable(); L.time = std::clamp((double)hr, 0.0, 24.0);
            bool runs = L.dayMinutes > 0;
            prop("The day runs", "Time passes while you play: the sun crosses the sky and night falls. Off: it stays at the hour you set (for a mood).");
            if (ImGui::Checkbox("##runs", &runs)) { meSnapshot(); L.dayMinutes = runs ? 24 : 0; meChanged(); }
            if (runs) {
                float mins = (float)L.dayMinutes;
                prop("A whole day takes", "Minutes of play for 24 hours.");
                track(ImGui::SliderFloat("##daymin", &mins, 2, 240, "%.0f min", ImGuiSliderFlags_Logarithmic)); typeable(); L.dayMinutes = std::clamp((double)mins, 1.0, 1440.0);
            }
            ImGui::TextDisabled("Daylight is now the light at noon.");
        }
        int lamps = 0; for (auto& e : ME.map.ents) if (e.type == "light") lamps++;
        ImGui::TextDisabled("%d light%s on this map (the nearest %d shine at once).", lamps, lamps == 1 ? "" : "s", MAX_LAMPS);
        prop("View while building", "How the editor lights the view (also the button at the top right). Players always see it as played.");
        ImGui::Combo("##lmode", &ME.lightMode, "As played\0Work light\0Fullbright\0");
    }
    if (section("Sun and sky", true)) {
        float yw = (float)L.sunYaw, pt = (float)L.sunPitch;
        prop("Sun direction");
        track(ImGui::SliderFloat("##sy", &yw, 0, 360, "%.0f deg")); typeable(); L.sunYaw = yw;
        prop("Sun height");
        track(ImGui::SliderFloat("##sp", &pt, 5, 90, "%.0f deg")); typeable(); L.sunPitch = pt;
        track(colorRow("Sunlight", L.sun));
        track(colorRow("Shade", L.ambient));
        track(colorRow("Sky above", L.skyTop));
        track(colorRow("Horizon and fog", L.skyHorizon));
        prop("Fog starts");
        track(lenDrag("##fs", L.fogStart, 1));
        prop("Fully fogged");
        track(lenDrag("##fe", L.fogEnd, 1));
        L.fogEnd = std::max(L.fogEnd, L.fogStart + 1);
    }
    if (section("Statistics", false)) {
        size_t faces = 0;
        for (auto& b : ME.map.brushes) faces += b.faces.size();
        ImGui::TextDisabled("%zu brushes, %zu faces", ME.map.brushes.size(), faces);
    }
}

// career mode: what your shop still needs, at the top of the Edit tab
static void shopChecklist() {
    auto miss = gmap::shopMissing(ME.map);
    heading("Your shop");
    if (miss.empty()) { ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1), ICON_FA_CHECK "  Everything it needs is here."); ImGui::Dummy({0, 6 * S()}); return; }
    ImGui::TextColored(colWarn(), "Still needs:");
    for (auto& m : miss) {
        ImGui::BulletText("%s", m.what.c_str());
        if (tipHover()) ImGui::SetTooltip("%s", m.tool.c_str());
    }
    if (ImGui::Button("Put them in for me", {-1, 0})) { meSnapshot(); gmap::shopRepair(ME.map); meChanged(); }
    if (tipHover()) ImGui::SetTooltip("Placed near where you start (else the middle of the room). Move them where you like.");
    ImGui::Dummy({0, 6 * S()});
}
// career mode: your workshop's saved copies, so a mistake can be undone later
static void workshopFileTab() {
    heading("Your workshop");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Kept as you go. Save a copy before big changes, and load it back if you don't like them.");
    ImGui::PopStyleColor();
    static char name[64] = "";
    ImGui::SetNextItemWidth(-90 * S());
    ImGui::InputTextWithHint("##copyname", "Name of the copy", name, sizeof name);
    ImGui::SameLine();
    if (ImGui::Button("Save copy", {-1, 0})) {
        std::string n = name[0] ? name : TextFormat("Day %d", CAREER.day);
        std::string f = careerWorkshopCopyFile(n);
        embedMapNpcs(ME.map);
        std::ofstream(f) << gmap::mapToJson(ME.map);
        toast(TextFormat("Saved a copy: %s", std::filesystem::path(f).stem().string().c_str()));
        name[0] = 0;
    }
    ImGui::Dummy({0, 6 * S()});
    heading("Saved copies");
    auto copies = careerWorkshopCopies();
    if (copies.empty()) ImGui::TextDisabled("None yet.");
    static std::string askDelete;
    for (auto& c : copies) {
        ImGui::PushID(c.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(c.c_str());
        float bw = 60 * S();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 2 * bw - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("Load", {bw, 0})) {
            std::ifstream in(careerWorkshopCopyFile(c)); std::stringstream ss; ss << in.rdbuf();
            gmap::Map m;
            if (in && gmap::mapFromJson(ss.str(), m)) { meReplaceMap(m); toast(TextFormat("Loaded %s. Ctrl+Z puts back what you had.", c.c_str())); }
            else toast("That copy couldn't be read.");
        }
        ImGui::SameLine();
        if (askDelete == c) { if (ImGui::Button("Sure?", {bw, 0})) { std::error_code ec; std::filesystem::remove(careerWorkshopCopyFile(c), ec); askDelete.clear(); } }
        else if (ImGui::Button("Delete", {bw, 0})) askDelete = c;
        ImGui::PopID();
    }
    ImGui::Dummy({0, 6 * S()});
    heading("Start again");
    static bool askFresh = false;
    if (!askFresh) { if (ImGui::Button("The empty workshop you started with", {-1, 0})) askFresh = true; }
    else {
        ImGui::TextColored(colWarn(), "Replace your workshop? (Ctrl+Z undoes it)");
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Replace it", {w, 0})) { meReplaceMap(gmap::defaultWorkshop()); askFresh = false; }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {w, 0})) askFresh = false;
    }
}
// The developer build only: make the map you're editing one of the game's built-in maps (a career property's building,
// or the practice range), with the career areas and locked walls it needs. Saved in Defaults (send it in to ship it).
static void devMapTools() {
    if (!devBuild()) return;
    ImGui::Dummy({0, 8 * S()});
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.75f, 0.3f, 1));
    heading(ICON_FA_SCREWDRIVER_WRENCH "  Developer: built-in maps");
    ImGui::PopStyleColor();
    auto& M = ME.map;
    static const char* kinds[][2] = {{"garage", "Home garage"}, {"shack", "Workshop shack"}, {"retail", "Strip-mall unit"}, {"warehouse", "Warehouse"}, {"range", "Practice range"}};
    static int k = 0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##devkind", kinds[k][1])) { for (int i = 0; i < 5; i++) if (ImGui::Selectable(kinds[i][1], i == k)) k = i; ImGui::EndCombo(); }
    bool property = k < 4;
    if (ImGui::Button(("Open the built-in " + std::string(kinds[k][1])).c_str(), {-1, 0})) {
        gmap::Map d = property ? gmap::propertyShell(kinds[k][0]) : gmap::builtinRange();
        meReplaceMap(d);
        ME.status = std::string("Opened the built-in ") + kinds[k][1] + ".";
    }
    if (property) {   // a career building: where you may build, the storefront, the ceiling, the locked walls
        auto selBox = [&](gmap::V3& mn, gmap::V3& mx) {
            mn = {1e30, 1e30, 1e30}; mx = {-1e30, -1e30, -1e30};
            for (int i : ME.sel) { gmap::V3 a, b; gmap::brushBounds(M.brushes[i], a, b);
                mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)}; mx = {std::max(mx.x, b.x), std::max(mx.y, b.y), std::max(mx.z, b.z)}; }
            return !ME.sel.empty();
        };
        ImGui::TextDisabled("Build area %s, storefront %s, ceiling %s.", M.build.on() ? TextFormat("%.1f x %.1f m", M.build.x1 - M.build.x0, M.build.z1 - M.build.z0) : "anywhere",
                            M.store.on() ? TextFormat("%.1f x %.1f m", M.store.x1 - M.store.x0, M.store.z1 - M.store.z0) : "none", M.roofY > 0 ? meLen(M.roofY).c_str() : "from the walls");
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        gmap::V3 mn, mx;
        ImGui::BeginDisabled(ME.sel.empty());
        if (ImGui::Button("Build area = selection", {w2, 0}) && selBox(mn, mx)) { meSnapshot(); M.build = {mn.x, mn.z, mx.x, mx.z}; meChanged(); }
        if (tipHover()) ImGui::SetTooltip("The floor area players may build in: the footprint of the selected blocks (select the floor).");
        ImGui::SameLine();
        if (ImGui::Button("Storefront = selection", {w2, 0}) && selBox(mn, mx)) { meSnapshot(); M.store = {mn.x, mn.z, mx.x, mx.z}; meChanged(); }
        if (tipHover()) ImGui::SetTooltip("Where customers shop (the store rating counts displays and decoration here).");
        if (ImGui::Button("Ceiling = top of selection", {w2, 0}) && selBox(mn, mx)) { meSnapshot(); M.roofY = mx.y; meChanged(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Lock all outside the build area", {w2, 0})) {
            meSnapshot();
            int n = 0;
            for (auto& b : M.brushes) {
                if (b.furn) continue;
                gmap::V3 a, c; gmap::brushBounds(b, a, c);
                bool inside = M.build.on() && a.x >= M.build.x0 - 0.01 && c.x <= M.build.x1 + 0.01 && a.z >= M.build.z0 - 0.01 && c.z <= M.build.z1 + 0.01;
                b.locked = !inside; n += b.locked;
            }
            meChanged();
            ME.status = TextFormat("%d blocks locked (the building, the street); the ones inside the build area are the player's.", n);
        }
        if (tipHover()) ImGui::SetTooltip("Players can't move or delete locked blocks (they can still paint their faces).\nBlocks wholly inside the build area stay theirs.");
    }
    if (accentButton(TextFormat(ICON_FA_FLOPPY_DISK "  Make this the built-in %s", kinds[k][1]), true, {-1, 0})) {
        std::error_code ec;
        std::filesystem::create_directories(defaultsFolder() + "/maps", ec);
        gmap::Map out = M;
        embedMapNpcs(out);
        std::string path = defaultsFolder() + "/maps/" + kinds[k][0] + ".gsmap";
        std::ofstream(path) << gmap::mapToJson(out);
        toast(std::string("Saved: new ") + kinds[k][1] + " careers and the game use it now. Send me the file to ship it.");
        ME.status = "Saved to " + path;
    }
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open the Defaults folder", {-1, 0})) openFolder(defaultsFolder());
}

static void fileTab() {
    if (!ME.careerFile.empty()) { workshopFileTab(); devMapTools(); return; }
    heading("Save");
    ImGui::TextDisabled("Saved in your Levels folder as <name>.gsmap (Settings > Files shows where). Maps are plain text, so they're easy to share and edit.");
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save", {-1, 30 * S()})) meSave();
    if (ME.dirty) ImGui::TextColored(colWarn(), "Unsaved changes (an autosave is kept).");
    ImGui::Dummy({0, 6 * S()});
    heading("Start from");
    if (ImGui::Button("Practice range (built in)", {-1, 0})) meLoadBuiltin();
    if (ImGui::Button("Empty map", {-1, 0})) meNew();
    ImGui::Dummy({0, 6 * S()});
    heading("Your maps");
    auto files = meMapFiles();
    if (files.empty()) ImGui::TextDisabled("None saved yet.");
    for (auto& f : files) {
        std::string stem = std::filesystem::path(f).stem().string();
        if (ImGui::Selectable(stem.c_str())) { uiTick(); meOpen(f); }
        if (tipHover()) ImGui::SetTooltip("%s", f.c_str());
    }
    devMapTools();
}

void mapPanel() {
    ImGui::SetNextWindowPos({GetScreenWidth() - PANEL_W, TOOLBAR_H});
    ImGui::SetNextWindowSize({PANEL_W, GetScreenHeight() - TOOLBAR_H});
    ImGui::Begin("##mappanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushFont(gFontBold, 0.0f);
    ImGui::TextUnformatted(ME.map.name.c_str());
    ImGui::PopFont();
    if (ImGui::BeginTabBar("##maptabs")) {
        const char* names[] = {"Edit", "Textures", "Map", "File"};
        static int forced = -2;
        if (forced == -2) { const char* e = std::getenv("GUNSMITH_MAPTAB"); forced = e ? std::atoi(e) : -1; }
        for (int i = 0; i < 4; i++) {
            ImGuiTabItemFlags fl = (forced == i) ? ImGuiTabItemFlags_SetSelected : 0;
            bool tabOpen = ImGui::BeginTabItem(names[i], nullptr, fl);
            tutAnchor((std::string("map:") + names[i]).c_str());
            if (tabOpen) { tab = i; ImGui::EndTabItem(); }
        }
        forced = -1;
        ImGui::EndTabBar();
    }
    ImGui::BeginChild("##mapbody");
    switch (tab) {
    case 0: if (!ME.careerFile.empty()) shopChecklist(); editTab(); toolOptions(); ImGui::Dummy({0, 6 * S()}); outliner(); ImGui::Dummy({0, 6 * S()}); groupTools(); break;
    case 1: texturesTab(); break;
    case 2: mapTab(); break;
    case 3: fileTab(); break;
    }
    if (!ME.status.empty()) { ImGui::Dummy({0, 8 * S()}); ImGui::TextDisabled("%s", ME.status.c_str()); }
    ImGui::EndChild();
    ImGui::End();
}
