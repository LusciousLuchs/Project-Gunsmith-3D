// Gunsmith3D - Map editor: the top bar (tools, grid, walk) and the panel (edit, textures, map settings, files).
#include "app/usertex.hpp"
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/mapedit.hpp"
#include "app/npcedit.hpp"
#include "app/mapgfx.hpp"
#include "app/careermode.hpp"
#include "app/shop3d.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "npcai.hpp"
#include "imgui.h"
#include "rlImGui.h"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
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
    if (on) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.91f, 0.63f, 0.23f, 0.22f)); ImGui::PushStyleColor(ImGuiCol_Text, colAccent()); }
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
static const char* GRID_ML[] = {"Grid 1 cm", "Grid 5 cm", "Grid 10 cm", "Grid 25 cm", "Grid 50 cm", "Grid 1 m", "Grid 2 m"};
static const char* GRID_IL[] = {"Grid 1/2 in", "Grid 1 in", "Grid 2 in", "Grid 6 in", "Grid 1 ft", "Grid 2 ft", "Grid 4 ft"};

// a length typed and shown in the player's units (metres or feet), stored in metres
static bool lenDrag(const char* id, double& m, double speedM = 0.01) {
    bool imp = CFG.units == Units::Imperial;
    double k = imp ? 1 / 0.3048 : 1.0, v = m * k;
    float sp = (float)(speedM * k);
    bool r = ImGui::DragScalar(id, ImGuiDataType_Double, &v, sp, nullptr, nullptr, imp ? "%.3f ft" : "%.3f m");
    if (r) m = v / k;
    return r;
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
        {ICON_FA_STAIRS, "Stairs", MTool::Stairs}, {ICON_FA_SCISSORS, "Clip: cut brushes along a line", MTool::Clip},
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
        {ICON_FA_CARET_UP, "Cone or pyramid", MTool::Cone}, {ICON_FA_GEM, "Ball", MTool::Sphere}, {ICON_FA_CIRCLE_NOTCH, "Rounded corner (a quarter cylinder)", MTool::Round},
        {ICON_FA_BRIDGE, "Arch: a doorway with a curved top", MTool::Arch}, {ICON_FA_ELLIPSIS, "Pipe: a tube with a hole through it", MTool::Pipe},
        {ICON_FA_CHAIR, "Props: tables, chairs, shelves, crates, barrels and more, built from brushes", MTool::Prop}, {ICON_FA_RULER, "Measure: distance, run, rise and slope between two points", MTool::Measure}};
    auto find = [&](MTool k) -> const TB& { for (auto& t : tools) if (t.t == k) return t; return tools[0]; };
    auto toolBtn = [&](MTool k) { const TB& t = find(k); if (iconBtn(t.icon, t.tip, ME.tool == k)) { ME.tool = k; ME.clipPts.clear(); } };
    // the shapes and the things you place sit in two menus, so the bar fits a small window
    static const MTool shapes[] = {MTool::Box, MTool::Wedge, MTool::Cylinder, MTool::Stairs, MTool::Cone, MTool::Sphere, MTool::Round, MTool::Arch, MTool::Pipe};
    static const MTool things[] = {MTool::Start, MTool::Npc, MTool::Spawner, MTool::Armor, MTool::Crate, MTool::Light, MTool::Ammo, MTool::Weapons, MTool::Station, MTool::Door, MTool::Target};
    static const char* shapeNames[] = {"Box", "Ramp", "Cylinder", "Stairs", "Cone or pyramid", "Ball", "Rounded corner", "Arch", "Pipe"};
    static const char* thingNames[] = {"Player start", "NPC", "Gun spawner", "Armour plates", "Attachment crate", "Light", "Ammo box", "Weapon crate", "Workstation", "Door", "Paper target"};
    static MTool lastShape = MTool::Box, lastThing = MTool::Light;
    auto menuBtn = [&](const char* id, const MTool* list, const char** names, int n, MTool& last, const char* title) {
        bool on = false;
        for (int k = 0; k < n; k++) if (ME.tool == list[k]) { on = true; last = list[k]; }
        ImGui::PushID(id);
        if (iconBtn(find(last).icon, find(last).tip, on)) { ME.tool = last; ME.clipPts.clear(); }
        ImGui::SameLine(0, 0);
        if (iconBtn(ICON_FA_CARET_DOWN, title)) ImGui::OpenPopup("##menu");
        if (ImGui::BeginPopup("##menu")) {
            for (int k = 0; k < n; k++) {
                std::string l = std::string(find(list[k]).icon) + "   " + names[k];
                if (ImGui::Selectable(l.c_str(), ME.tool == list[k])) { ME.tool = list[k]; last = list[k]; ME.clipPts.clear(); }
                if (tipHover()) ImGui::SetTooltip("%s", find(list[k]).tip);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };
    toolBtn(MTool::Select);
    gap(12);
    menuBtn("shapes", shapes, shapeNames, 9, lastShape, "More shapes: box, ramp, cylinder, stairs, cone, ball, rounded corner, arch, pipe");
    gap(3);
    toolBtn(MTool::Clip);
    gap(3);
    toolBtn(MTool::Prop);
    gap(3);
    toolBtn(MTool::Measure);
    gap(12);
    menuBtn("things", things, thingNames, 11, lastThing, "Things to place: player start, NPCs, spawners, lights, doors, targets and more");
    gap(12);
    if (iconBtn(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Handles: move (1)", ME.xmode == XMode::Move && ME.tool == MTool::Select)) { ME.xmode = XMode::Move; ME.tool = MTool::Select; }
    gap(3);
    if (iconBtn(ICON_FA_ROTATE, "Handles: turn (2)", ME.xmode == XMode::Rotate && ME.tool == MTool::Select)) { ME.xmode = XMode::Rotate; ME.tool = MTool::Select; }
    gap(3);
    if (iconBtn(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Handles: stretch (3)", ME.xmode == XMode::Scale && ME.tool == MTool::Select)) { ME.xmode = XMode::Scale; ME.tool = MTool::Select; }
    gap(3);
    if (iconBtn(ICON_FA_MAGNET, "Snap to other things' corners and middles (Alt while dragging: off for that move)", ME.snapObjects)) ME.snapObjects = !ME.snapObjects;
    gap(12);
    if (iconBtn(ICON_FA_ROTATE_LEFT, "Undo (Ctrl+Z)")) meUndo();
    gap(3);
    if (iconBtn(ICON_FA_ROTATE_RIGHT, "Redo (Ctrl+Y)")) meRedo();
    gap(18);
    bool imp = CFG.units == Units::Imperial;
    const double* steps = imp ? GRID_I : GRID_M;
    int gi = 3;
    for (int k = 0; k < 7; k++) if (std::fabs(ME.grid - steps[k]) < 1e-6) gi = k;
    ImGui::SetNextItemWidth(118 * S());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
    if (ImGui::Combo("##grid", &gi, imp ? GRID_IL : GRID_ML, 7)) ME.grid = steps[gi];
    gap(8);
    ImGui::SetNextItemWidth(120 * S());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
    lenDrag("##height", ME.height, ME.grid);
    ME.height = std::max(ME.height, 0.01);
    if (tipHover()) ImGui::SetTooltip("Height of new brushes");
    gap(8);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
    unitsSwitch(ImGui::GetFrameHeight());
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
            if (ImGui::SliderFloat("##lrange", &rg, 1, 150, "%.1f m", ImGuiSliderFlags_Logarithmic)) { e.range = rg; tr(true); } else tr(false);
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
                if (ImGui::SliderFloat("How far (m)", &tr, 0.5f, 20.f, "%.1f")) { e.width = tr; meChanged(); }
                if (ImGui::SliderFloat("How fast (m/s)", &sp, 0.2f, 8.f, "%.1f")) { e.range = sp; meChanged(); }
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
        if (ImGui::SliderFloat("##yaw", &yaw, 0, 360, "%.0f deg")) { if (!editing) { meSnapshot(); editing = true; } e.yaw = yaw; ME.version++; } typeable();
        if (ImGui::IsItemDeactivatedAfterEdit()) { editing = false; meChanged(); }
        for (int a = 0; a < 3; a++) {
            static const char* L[] = {"X", "Height", "Z"};
            prop(L[a]);
            double* c = a == 0 ? &e.pos.x : a == 1 ? &e.pos.y : &e.pos.z;
            ImGui::PushID(a);
            if (lenDrag("##p", *c, ME.grid)) { if (!editing) { meSnapshot(); editing = true; } ME.version++; }
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
        ImGui::Dummy({0, 6 * S()});
        ImGui::TextDisabled("%zu brushes in this map", M.brushes.size());
        return;
    }
    if (ME.sel.size() == 1) heading(ME.face >= 0 ? "Face" : "Brush");
    else { char b[48]; std::snprintf(b, sizeof b, "%zu brushes", ME.sel.size()); heading(b); }

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
            ImGui::TextDisabled("Pick a new texture on the Textures tab.");
        }
    }
}


// ---------------------------------------------------------------- moving, turning, stretching, arranging
// a position or size: three typed fields that apply when you let go (so one undo step per change)
static bool vec3Field(const char* id, gmap::V3 cur, gmap::V3& out) {
    static std::map<std::string, std::pair<gmap::V3, bool>> state;
    auto& st = state[id];
    if (!st.second) st.first = cur;
    bool commit = false, active = false;
    ImGui::PushID(id);
    float w = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    const char* ax[3] = {"X", "Y", "Z"};
    double* v[3] = {&st.first.x, &st.first.y, &st.first.z};
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(w);
        char lab[8]; std::snprintf(lab, sizeof lab, "##%s", ax[i]);
        lenDrag(lab, *v[i], std::max(ME.grid, 0.01));
        if (ImGui::IsItemActive()) active = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
        if (tipHover()) ImGui::SetTooltip("%s: drag, or double-click to type", ax[i]);
        ImGui::PopID();
    }
    ImGui::PopID();
    st.second = active;
    if (commit) { out = st.first; st.second = false; return true; }
    return false;
}
static void transformTools() {
    gmap::Selection sel = meSelection();
    heading("Move, turn, stretch");
    {
        struct M { const char* icon; const char* label; const char* tip; XMode m; } modes[] = {
            {ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Move", "Arrows slide along one axis. Drag the brush itself to slide it over the ground (Alt: up and down). Key 1.", XMode::Move},
            {ICON_FA_ROTATE, "Turn", "Rings turn it about X, Y or Z in 15 degree steps (Alt: free). Key 2.", XMode::Rotate},
            {ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Stretch", "Squares stretch it along an axis, the far side stays put (Ctrl: about the middle). The middle square stretches all of it. Key 3.", XMode::Scale}};
        float w = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        for (int i = 0; i < 3; i++) {
            if (i) ImGui::SameLine();
            std::string l = std::string(modes[i].icon) + "  " + modes[i].label;
            if (accentButton(l.c_str(), ME.xmode == modes[i].m, {w, 0})) { ME.xmode = modes[i].m; ME.tool = MTool::Select; }
            if (tipHover()) ImGui::SetTooltip("%s", modes[i].tip);
        }
    }
    ImGui::Checkbox("Snap to other things", &ME.snapObjects);
    if (tipHover()) ImGui::SetTooltip("Land a corner, edge middle or face middle on another brush (or thing) when you drag.\nHold Alt while dragging to leave it off for that move.");
    if (sel.empty()) { ImGui::TextDisabled("Pick something to move, turn or stretch it."); }
    else {
        gmap::V3 mn, mx; gmap::selectionBounds(ME.map, sel, mn, mx);
        gmap::V3 c = (mn + mx) * 0.5, size = mx - mn, out;
        prop("Position", "The middle of what's picked. Drag a number or double-click to type (units follow the switch at the top).");
        if (vec3Field("##pos", c, out)) meSetCentre(out);
        if (!sel.brushes.empty()) {
            prop("Size", "Width, height and depth of what's picked, stretched about its middle.");
            if (vec3Field("##size", size, out)) meSetSize(out);
        }
        prop("Turn by");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragScalar("##turnby", ImGuiDataType_Double, &ME.turnBy, 1.0f, nullptr, nullptr, "%.0f degrees");
        ME.turnBy = std::clamp(ME.turnBy, 1.0, 180.0);
        const char* ax[3] = {"X", "Y", "Z"};
        float w = (ImGui::GetContentRegionAvail().x - 5 * ImGui::GetStyle().ItemSpacing.x) / 6;
        for (int sgn = 1; sgn >= -1; sgn -= 2) for (int a = 0; a < 3; a++) {
            ImGui::PushID(a * 2 + (sgn > 0));
            char l[8]; std::snprintf(l, sizeof l, "%s%s", sgn > 0 ? "+" : "-", ax[a]);
            if (ImGui::Button(l, {w, 0})) meTurnAbout(a, sgn * ME.turnBy);
            if (tipHover()) ImGui::SetTooltip("Turn about %s by the amount above (the right-hand way for +).", ax[a]);
            ImGui::PopID();
            if (!(sgn < 0 && a == 2)) ImGui::SameLine();
        }
        ImGui::NewLine();
        prop("Mirror", "Flip across the middle of what's picked.");
        float mw = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        for (int a = 0; a < 3; a++) { if (a) ImGui::SameLine(); char l[24]; std::snprintf(l, sizeof l, "%s##mir%d", ax[a], a); if (ImGui::Button(l, {mw, 0})) meMirror(a); }
    }
    if (meCanVertex()) {
        if (accentButton(ICON_FA_DRAW_POLYGON "  Edit corners (C)", ME.vertexMode, {-1, 0})) { ME.vertexMode = !ME.vertexMode; ME.vertSel.clear(); }
        if (tipHover()) ImGui::SetTooltip("Show the picked brush's corners (squares) and edge middles (circles). Drag them: the brush stays solid and convex.\nShift adds corners. Shift-drag a face's square to pull a new piece out of it.");
    }
    if (section("Line up and space out", false)) {
        const char* ax[3] = {"X", "Y", "Z"};
        const char* to[3] = {"Low", "Middle", "High"};
        ImGui::TextDisabled("Pick two or more things. Low and High line them up on their lowest or highest edge.");
        for (int a = 0; a < 3; a++) {
            for (int k = 0; k < 3; k++) {
                if (k) ImGui::SameLine();
                char l[24]; std::snprintf(l, sizeof l, "%s %s##al%d%d", ax[a], to[k], a, k);
                if (ImGui::Button(l, {(ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / (3 - k), 0})) meAlign(a, (gmap::AlignTo)k);
            }
        }
        prop("Space out", "Even gaps between the first and last of three or more.");
        float w = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        for (int a = 0; a < 3; a++) { if (a) ImGui::SameLine(); char l[24]; std::snprintf(l, sizeof l, "Along %s##dist%d", ax[a], a); if (ImGui::Button(l, {w, 0})) meDistribute(a); }
    }
    if (section("Copies and arrays", false)) {
        ImGui::TextDisabled("Copy and paste (Ctrl+C, Ctrl+V) work between maps too.");
        if (ImGui::Button(ICON_FA_COPY "  Copy", {(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2, 0})) meCopy(false);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PASTE "  Paste", {-1, 0})) mePaste();
        prop("A row of copies", "Each copy steps further along by the amount below.");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##arrn", &ME.arrCount, 1, 50, "%d copies"); typeable();
        gmap::V3 st = ME.arrStep, so;
        if (vec3Field("##arrstep", st, so)) ME.arrStep = so;
        if (ImGui::Button("Make the row", {-1, 0})) meArrayLinear();
        prop("A ring of copies", "Copies spread round the middle of what's picked. 360 degrees puts them evenly all the way round.");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##radn", &ME.radCount, 1, 50, "%d copies"); typeable();
        float tot = (float)ME.radTotal;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##radtot", &tot, 10, 360, "%.0f degrees round")) ME.radTotal = tot;
        typeable();
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##radaxis", &ME.radAxis, "Round the upright axis (Y)\0Round X\0Round Z\0");
        ImGui::Checkbox("Turn the copies as they go round", &ME.radTurn);
        if (ImGui::Button("Make the ring", {-1, 0})) {
            int ax = ME.radAxis == 0 ? 1 : ME.radAxis == 1 ? 0 : 2;
            int keep = ME.radAxis; ME.radAxis = ax; meArrayRadial(); ME.radAxis = keep;
        }
    }
    if (section("Hide, freeze, group", false)) {
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button(ICON_FA_EYE_SLASH "  Hide (Ctrl+H)", {w, 0})) meHide(true);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_EYE "  Show all", {-1, 0})) meHide(false);
        if (ImGui::Button(ICON_FA_LOCK "  Freeze (Ctrl+L)", {w, 0})) meFreeze(true);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LOCK_OPEN "  Unfreeze all", {-1, 0})) meFreeze(false);
        if (ImGui::Button(ICON_FA_OBJECT_GROUP "  Group (Ctrl+G)", {w, 0})) meGroup(true);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_OBJECT_UNGROUP "  Ungroup", {-1, 0})) meGroup(false);
        ImGui::TextDisabled("Hidden things are out of the way while you build (they're there when you play). Frozen things can't be picked. A group is picked together.");
    }
}

// ---------------------------------------------------------------- the list of everything
static std::string brushLabel(int i) {
    const auto& b = ME.map.brushes[i];
    gmap::V3 mn, mx; gmap::brushBounds(b, mn, mx);
    std::string mat = b.faces.empty() ? "" : gmap::materialNames()[std::clamp(b.faces[0].mat, 0, (int)gmap::materialNames().size() - 1)];
    std::string n = b.name.empty() ? "Brush " + std::to_string(i + 1) : b.name;
    return n + "   " + meLen(mx.x - mn.x) + " x " + meLen(mx.y - mn.y) + " x " + meLen(mx.z - mn.z) + "   " + mat + (b.group ? "   [group " + std::to_string(b.group) + "]" : "");
}
static void objectsTab() {
    heading("Everything in the map");
    ImGui::TextDisabled("Click to pick (Ctrl adds). The eye hides, the lock freezes. Double-click a name to rename it.");
    static char filter[64] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##objfilter", "Filter by name or material", filter, sizeof filter);
    std::string f = filter;
    for (auto& ch : f) ch = (char)std::tolower((unsigned char)ch);
    auto match = [&](const std::string& s) { if (f.empty()) return true; std::string l = s; for (auto& ch : l) ch = (char)std::tolower((unsigned char)ch); return l.find(f) != std::string::npos; };
    gmap::Selection cur = meSelection();
    static int renaming = -1; static bool renamingEnt = false; static char rbuf[64];
    bool ctrl = ImGui::GetIO().KeyCtrl;
    if (section("Things", true)) {
        for (int i = 0; i < (int)ME.map.ents.size(); i++) {
            auto& e = ME.map.ents[i];
            std::string label = e.name.empty() ? e.type : e.name + "  (" + e.type + ")";
            if (!match(label)) continue;
            ImGui::PushID(100000 + i);
            bool selected = std::find(cur.ents.begin(), cur.ents.end(), i) != cur.ents.end();
            if (ImGui::SmallButton(e.hidden ? ICON_FA_EYE_SLASH : ICON_FA_EYE)) { meSnapshot(); e.hidden = !e.hidden; meChanged(); }
            ImGui::SameLine();
            if (ImGui::SmallButton(e.frozen ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN)) { meSnapshot(); e.frozen = !e.frozen; meChanged(); }
            ImGui::SameLine();
            if (renaming == i && renamingEnt) {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##rn", rbuf, sizeof rbuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) { meSnapshot(); e.name = rbuf; meChanged(); renaming = -1; }
                if (ImGui::IsItemDeactivated() && renaming == i) renaming = -1;
            } else {
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) { renaming = i; renamingEnt = true; std::snprintf(rbuf, sizeof rbuf, "%s", e.name.c_str()); }
                    else if (ctrl) { gmap::Selection s = cur; auto it = std::find(s.ents.begin(), s.ents.end(), i); if (it == s.ents.end()) s.ents.push_back(i); else s.ents.erase(it); meSetSelection(s); }
                    else meSetSelection({{}, {i}});
                }
            }
            ImGui::PopID();
        }
    }
    if (section("Brushes", true)) {
        std::vector<int> shown;
        for (int i = 0; i < (int)ME.map.brushes.size(); i++) if (f.empty() || match(brushLabel(i))) shown.push_back(i);
        ImGuiListClipper clip;
        clip.Begin((int)shown.size());
        while (clip.Step()) for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
            int i = shown[k];
            auto& b = ME.map.brushes[i];
            ImGui::PushID(i);
            bool selected = std::find(cur.brushes.begin(), cur.brushes.end(), i) != cur.brushes.end();
            if (ImGui::SmallButton(b.hidden ? ICON_FA_EYE_SLASH : ICON_FA_EYE)) { meSnapshot(); b.hidden = !b.hidden; meChanged(); }
            ImGui::SameLine();
            if (ImGui::SmallButton(b.frozen ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN)) { meSnapshot(); b.frozen = !b.frozen; meChanged(); }
            ImGui::SameLine();
            if (renaming == i && !renamingEnt) {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##rn", rbuf, sizeof rbuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) { meSnapshot(); b.name = rbuf; meChanged(); renaming = -1; }
                if (ImGui::IsItemDeactivated() && renaming == i) renaming = -1;
            } else {
                std::string label = brushLabel(i);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) { renaming = i; renamingEnt = false; std::snprintf(rbuf, sizeof rbuf, "%s", b.name.c_str()); }
                    else if (ctrl) { gmap::Selection s = cur; auto it = std::find(s.brushes.begin(), s.brushes.end(), i); if (it == s.brushes.end()) s.brushes.push_back(i); else s.brushes.erase(it); meSetSelection(s); }
                    else if (!b.locked) { meSetSelection({{i}, {}}); }
                }
            }
            ImGui::PopID();
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
    if (ME.tool == MTool::Cylinder) { prop("Sides"); ImGui::SliderInt("##sides", &ME.cylSides, 3, 32); typeable(); }
    if (ME.tool == MTool::Cone || ME.tool == MTool::Sphere || ME.tool == MTool::Round || ME.tool == MTool::Pipe) {
        prop(ME.tool == MTool::Round ? "Steps in the curve" : "Sides");
        ImGui::SliderInt("##shsides", &ME.shapeSides, ME.tool == MTool::Cone ? 3 : 4, 32); typeable();
        if (ME.tool == MTool::Cone) ImGui::TextDisabled("4 sides makes a pyramid.");
    }
    if (ME.tool == MTool::Arch) {
        prop("Blocks in the curve"); ImGui::SliderInt("##archsegs", &ME.archSegs, 3, 24); typeable();
        prop("Wall thickness"); lenDrag("##archwall", ME.archWall, 0.05); ME.archWall = std::clamp(ME.archWall, 0.05, 2.0);
        ImGui::TextDisabled("Its opening runs the way you're looking along; the height is the top bar's.");
    }
    if (ME.tool == MTool::Pipe) { prop("Wall thickness"); lenDrag("##pipewall", ME.pipeWall, 0.02); ME.pipeWall = std::clamp(ME.pipeWall, 0.02, 2.0); }
    if (ME.tool == MTool::Prop) {
        const auto& kinds = gmap::propKinds();
        ME.propKind = std::clamp(ME.propKind, 0, (int)kinds.size() - 1);
        prop("Which");
        if (ImGui::BeginCombo("##propkind", kinds[ME.propKind].c_str())) {
            for (int i = 0; i < (int)kinds.size(); i++) if (ImGui::Selectable(kinds[i].c_str(), ME.propKind == i)) ME.propKind = i;
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Built from brushes, picked as one, so it blocks like a wall. Click where it stands; it faces you.");
    }
    if (ME.tool == MTool::Measure) {
        ImGui::TextDisabled("Click two points. They snap to corners and middles (Alt: not).");
        if (ImGui::Button("Clear", {-1, 0})) ME.measurePts.clear();
    }
    if (ME.tool == MTool::Stairs) { prop("Steps"); ImGui::SliderInt("##steps", &ME.stairSteps, 2, 32); typeable(); }
}

static void texturesTab() {
    {   // the player's own pictures: add one, or open the folder they live in
        std::string none;
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
#ifdef _WIN32
        if (ImGui::Button("Add a picture...", {w2, 0})) {
            std::string p;
            if (pickImageFile(p)) { std::string n = importTexture(p); if (!n.empty()) toast("Added " + n + "."); else toast("That file isn't a picture the game can read."); }
        }
        ImGui::SameLine();
#endif
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
static void fileTab() {
    if (!ME.careerFile.empty()) { workshopFileTab(); return; }
    heading("Save");
    ImGui::TextDisabled("Saved in your Levels folder as <name>.gsmap (Settings > Files shows where). Maps are plain text, so they're easy to share and edit.");
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save", {-1, 30 * S()})) meSave();
    if (ME.dirty) ImGui::TextColored(colWarn(), "Unsaved changes (an autosave is kept).");
    ImGui::Dummy({0, 6 * S()});
    heading("Share");
    ImGui::TextDisabled("A map code is the whole map as one line of text: paste it in a message, or into another copy of the game.");
    if (ImGui::Button(ICON_FA_SHARE_NODES "  Copy a code for this map", {-1, 0})) meCopyMapCode();
    if (ImGui::Button(ICON_FA_PASTE "  Open a map from a code on the clipboard", {-1, 0})) mePasteMapCode();
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
}

void mapPanel() {
    ImGui::SetNextWindowPos({GetScreenWidth() - PANEL_W, TOOLBAR_H});
    ImGui::SetNextWindowSize({PANEL_W, GetScreenHeight() - TOOLBAR_H});
    ImGui::Begin("##mappanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushFont(gFontBold, 0.0f);
    ImGui::TextUnformatted(ME.map.name.c_str());
    ImGui::PopFont();
    if (ImGui::BeginTabBar("##maptabs")) {
        const char* names[] = {"Edit", "Objects", "Textures", "Map", "File"};
        static int forced = -2;
        if (forced == -2) { const char* e = std::getenv("GUNSMITH_MAPTAB"); forced = e ? std::atoi(e) : -1; }
        for (int i = 0; i < 5; i++) {
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
    case 0: if (!ME.careerFile.empty()) shopChecklist(); editTab(); toolOptions(); ImGui::Dummy({0, 6 * S()}); transformTools(); ImGui::Dummy({0, 6 * S()}); groupTools(); break;
    case 1: objectsTab(); break;
    case 2: texturesTab(); break;
    case 3: mapTab(); break;
    case 4: fileTab(); break;
    }
    if (!ME.status.empty()) { ImGui::Dummy({0, 8 * S()}); ImGui::TextDisabled("%s", ME.status.c_str()); }
    ImGui::EndChild();
    ImGui::End();
}
