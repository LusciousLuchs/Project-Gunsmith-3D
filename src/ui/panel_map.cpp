// Gunsmith3D - Map editor: the top bar (tools, grid, walk) and the panel (edit, textures, map settings, files).
#include "app/usertex.hpp"
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/mapedit.hpp"
#include "app/npcedit.hpp"
#include "app/mapgfx.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "npcai.hpp"
#include "imgui.h"
#include "rlImGui.h"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using gmap::V3;
static float S() { return CFG.uiScale; }

static bool iconBtn(const char* icon, const char* tip, bool on = false) {
    float sz = TOOLBAR_H - 14 * S();
    if (on) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.91f, 0.63f, 0.23f, 0.22f)); ImGui::PushStyleColor(ImGuiCol_Text, colAccent()); }
    bool r = ImGui::Button(icon, {sz, sz});
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
    ImGui::TextDisabled("Map editor");
    gap(18);
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
        {ICON_FA_BOX, "Weapon crate: press F by it to take any of your guns (like a gun spawner)", MTool::Weapons}};
    int i = 0;
    for (auto& t : tools) {
        if (iconBtn(t.icon, t.tip, ME.tool == t.t)) { ME.tool = t.t; ME.clipPts.clear(); }
        gap(i == 0 || i == 4 || i == 5 ? 12 : 3);
        i++;
    }
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
        if (iconBtn(icons[ME.lightMode], tips[ME.lightMode], ME.lightMode > 0)) ME.lightMode = (ME.lightMode + 1) % 3;
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
    if (accentButton(ICON_FA_PERSON_WALKING "  Walk", true, {104 * S(), sz})) meWalk(IsKeyDown(KEY_LEFT_SHIFT));
    tutAnchor("map:walk");
    if (tipHover()) ImGui::SetTooltip("Walk the map from the player start (F5).\nShift: start where the camera is.");
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
    if (ME.tool == MTool::Cylinder) { prop("Sides"); ImGui::SliderInt("##sides", &ME.cylSides, 3, 32); typeable(); }
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

static void fileTab() {
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
    case 0: editTab(); toolOptions(); ImGui::Dummy({0, 6 * S()}); groupTools(); break;
    case 1: texturesTab(); break;
    case 2: mapTab(); break;
    case 3: fileTab(); break;
    }
    if (!ME.status.empty()) { ImGui::Dummy({0, 8 * S()}); ImGui::TextDisabled("%s", ME.status.c_str()); }
    ImGui::EndChild();
    ImGui::End();
}
