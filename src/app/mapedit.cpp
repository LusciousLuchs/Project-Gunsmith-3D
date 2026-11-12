#include <cstdlib>
// Gunsmith3D - The map editor (see mapedit.hpp).
#include "app/paths.hpp"
#include "app/mapedit.hpp"
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/fonts.hpp"
#include "app/gfx.hpp"
#include "app/keys.hpp"
#include "app/mapgfx.hpp"
#include "app/npcedit.hpp"
#include "app/scene.hpp"
#include "app/settings.hpp"
#include "imgui.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using gmap::V3;
using gmap::dot;
using gmap::cross;
using gmap::norm;

MapEd ME;

static const char* MAP_DIR = MAPS_DIR;
static std::string mapAutosave() { return std::string(MAPS_DIR) + "/.autosave.gsmap"; }

// ---------------------------------------------------------------- small helpers
static Vector3 rv(V3 v) { return {(float)v.x, (float)v.y, (float)v.z}; }
static double snapG(double v) { return ME.grid > 0 ? std::round(v / ME.grid) * ME.grid : v; }
static V3 snap3(V3 p) { return {snapG(p.x), snapG(p.y), snapG(p.z)}; }

std::string meLen(double m) {
    char b[48];
    if (CFG.units == Units::Imperial) {
        double in = m / 0.0254;
        if (std::fabs(in) < 12 - 1e-6) std::snprintf(b, sizeof b, "%g in", std::round(in * 100) / 100);
        else std::snprintf(b, sizeof b, "%g ft", std::round(in / 12 * 100) / 100);
    } else {
        if (std::fabs(m) < 1 - 1e-9) std::snprintf(b, sizeof b, "%g cm", std::round(m * 1000) / 10);
        else std::snprintf(b, sizeof b, "%g m", std::round(m * 1000) / 1000);
    }
    return b;
}

// the part of the window the 3D view uses
struct VP { float x, y, w, h; };
static VP viewRect() {
    if (ME.walking) return {0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()};
    return {0, TOOLBAR_H, GetScreenWidth() - PANEL_W, GetScreenHeight() - TOOLBAR_H};
}

struct CamB { V3 pos, fwd, right, up; bool ortho; double fovy; };
static CamB camBasis() {
    CamB c;
    if (ME.top) {
        c.pos = ME.topC + V3{0, 500, 0}; c.fwd = {0, -1, 0}; c.right = {1, 0, 0}; c.up = {0, 0, -1};
        c.ortho = true; c.fovy = ME.topH;
    } else {
        c.pos = ME.cam;
        c.fwd = {std::cos(ME.pitch) * std::cos(ME.yaw), std::sin(ME.pitch), std::cos(ME.pitch) * std::sin(ME.yaw)};
        c.right = norm(cross(c.fwd, {0, 1, 0}));
        c.up = cross(c.right, c.fwd);
        c.ortho = false; c.fovy = 70;
    }
    return c;
}
static Camera3D toCamera(const CamB& b) {
    Camera3D c{};
    c.position = rv(b.pos); c.target = rv(b.pos + b.fwd); c.up = rv(b.up);
    c.fovy = (float)b.fovy;
    c.projection = b.ortho ? CAMERA_ORTHOGRAPHIC : CAMERA_PERSPECTIVE;
    return c;
}
static void mouseRay(V3& o, V3& d) {
    VP v = viewRect();
    CamB c = camBasis();
    Vector2 m = GetMousePosition();
    double nx = (m.x - v.x) / v.w * 2 - 1, ny = 1 - (m.y - v.y) / v.h * 2, aspect = v.w / std::max(1.f, v.h);
    if (c.ortho) { o = c.pos + c.right * (nx * c.fovy / 2 * aspect) + c.up * (ny * c.fovy / 2); d = c.fwd; }
    else { double th = std::tan(c.fovy * DEG2RAD / 2); o = c.pos; d = norm(c.fwd + c.right * (nx * th * aspect) + c.up * (ny * th)); }
}
static bool project(V3 p, Vector2& s) {
    VP v = viewRect();
    CamB c = camBasis();
    V3 r = p - c.pos;
    double aspect = v.w / std::max(1.f, v.h), sx, sy;
    if (c.ortho) { sx = dot(r, c.right) / (c.fovy / 2 * aspect); sy = dot(r, c.up) / (c.fovy / 2); }
    else {
        double z = dot(r, c.fwd);
        if (z < 0.05) return false;
        double th = std::tan(c.fovy * DEG2RAD / 2);
        sx = dot(r, c.right) / (z * th * aspect); sy = dot(r, c.up) / (z * th);
    }
    s = {(float)(v.x + (sx + 1) / 2 * v.w), (float)(v.y + (1 - sy) / 2 * v.h)};
    return true;
}
static bool mouseInView() {
    VP v = viewRect();
    Vector2 m = GetMousePosition();
    return m.x >= v.x && m.x < v.x + v.w && m.y >= v.y && m.y < v.y + v.h && !ImGui::GetIO().WantCaptureMouse;
}
static bool rayPlaneY(V3 o, V3 d, double y, V3& p) {
    if (std::fabs(d.y) < 1e-9) return false;
    double t = (y - o.y) / d.y;
    if (t < 0) return false;
    p = o + d * t;
    return true;
}
static bool rayPlane(V3 o, V3 d, V3 n, double pd, V3& p) {
    double den = dot(n, d);
    if (std::fabs(den) < 1e-9) return false;
    double t = (pd - dot(n, o)) / den;
    if (t < 0) return false;
    p = o + d * t;
    return true;
}
static bool isSel(int b) { return std::find(ME.sel.begin(), ME.sel.end(), b) != ME.sel.end(); }
static int startEntity() { for (size_t i = 0; i < ME.map.ents.size(); i++) if (ME.map.ents[i].type == "player_start") return (int)i; return -1; }

// ---------------------------------------------------------------- undo, files
void meSnapshot() {
    ME.undo.push_back(gmap::mapToJson(ME.map));
    if (ME.undo.size() > 100) ME.undo.erase(ME.undo.begin());
    ME.redo.clear();
}
static void writeAutosave() {
    if (!ME.careerFile.empty()) { std::ofstream(ME.careerFile) << gmap::mapToJson(ME.map); return; }   // (your workshop: kept as you go)
    std::error_code ec;
    fs::create_directories(MAP_DIR, ec);
    embedMapNpcs(ME.map);   // (the NPCs its spawn points use go with it)
    std::ofstream(mapAutosave()) << gmap::mapToJson(ME.map);
}
void meChanged() {
    ME.version++;
    ME.dirty = true;
    if (CFG.autosave != AutosaveEvery::Off) writeAutosave();
}
static void restore(const std::string& s) {
    gmap::Map m;
    if (gmap::mapFromJson(s, m)) { ME.map = m; ME.sel.clear(); ME.face = -1; ME.ent = -1; ME.version++; ME.dirty = true; }
}
void meUndo() {
    if (ME.undo.empty()) return;
    ME.redo.push_back(gmap::mapToJson(ME.map));
    restore(ME.undo.back()); ME.undo.pop_back();
}
void meRedo() {
    if (ME.redo.empty()) return;
    ME.undo.push_back(gmap::mapToJson(ME.map));
    restore(ME.redo.back()); ME.redo.pop_back();
}

static std::string fileName(const std::string& name) {
    std::string f;
    for (char c : name) {
        if (std::isalnum((unsigned char)c) || c == '-' || c == '_') f += c;
        else if (c == ' ') f += '_';
    }
    return f.empty() ? "untitled" : f;
}
bool meSave() {
    if (!ME.careerFile.empty()) { writeAutosave(); ME.dirty = false; toast("Workshop saved."); return true; }
    std::error_code ec;
    fs::create_directories(MAP_DIR, ec);
    std::string path = std::string(MAP_DIR) + "/" + fileName(ME.map.name) + ".gsmap";
    std::ofstream out(path);
    if (!out) { ME.status = "Couldn't write " + path; return false; }
    embedMapNpcs(ME.map);   // the NPCs its spawn points use travel in the file
    out << gmap::mapToJson(ME.map);
    ME.dirty = false;
    ME.status = "Saved to " + path;
    toast("Map saved.");
    return true;
}
bool meOpen(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss; ss << in.rdbuf();
    gmap::Map m;
    if (!gmap::mapFromJson(ss.str(), m)) { ME.status = "That file isn't a map this version can read."; return false; }
    meSnapshot();
    ME.map = m; ME.sel.clear(); ME.face = -1; ME.ent = -1;
    meChanged(); ME.dirty = false;
    meFocus();
    ME.status = "Opened " + path;
    if (int k = installMapNpcs(ME.map)) { ME.status += TextFormat(" (and %d of its NPCs, now in your NPCs folder)", k); toast(TextFormat("This map brought %d NPC%s with it: they're in the NPC editor now.", k, k == 1 ? "" : "s")); }
    return true;
}
bool meSaveGroup(const std::string& name) {
    if (ME.sel.empty() && ME.ent < 0) { toast("Select what goes in the group first (Shift-click adds brushes)."); return false; }
    gmap::Map g;
    g.name = name.empty() ? "Group" : name;
    for (int i : ME.sel) g.brushes.push_back(ME.map.brushes[i]);
    if (ME.ent >= 0 && ME.ent < (int)ME.map.ents.size() && ME.map.ents[ME.ent].type != "player_start") g.ents.push_back(ME.map.ents[ME.ent]);
    // stored round the middle of its floor, so it can be put down anywhere
    V3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
    for (auto& b : g.brushes) { V3 a, c; gmap::brushBounds(b, a, c); mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)}; mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)}; }
    for (auto& e : g.ents) { mn = {std::min(mn.x, e.pos.x), std::min(mn.y, e.pos.y), std::min(mn.z, e.pos.z)}; mx = {std::max(mx.x, e.pos.x), std::max(mx.y, e.pos.y), std::max(mx.z, e.pos.z)}; }
    V3 o{(mn.x + mx.x) / 2, mn.y, (mn.z + mx.z) / 2};
    for (auto& b : g.brushes) gmap::translate(b, -o);
    for (auto& e : g.ents) e.pos -= o;
    embedMapNpcs(g);
    std::error_code ec;
    fs::create_directories(GROUPS_DIR, ec);
    std::string file = safeFileName(g.name);
    std::ofstream out(std::string(GROUPS_DIR) + "/" + file + ".json");
    if (!out) { toast("Couldn't save the group."); return false; }
    out << gmap::mapToJson(g);
    toast("Saved the group " + file + " (" + std::to_string(g.brushes.size()) + " brushes" + (g.ents.empty() ? "" : ", 1 object") + "). Place it again from the panel.");
    return true;
}
std::vector<std::string> meGroupFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(GROUPS_DIR, ec)) if (e.path().extension() == ".json") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}
bool mePlaceGroup(const std::string& name) {
    std::ifstream in(std::string(GROUPS_DIR) + "/" + name + ".json");
    std::stringstream ss; ss << in.rdbuf();
    gmap::Map g;
    if (!gmap::mapFromJson(ss.str(), g)) { toast("That group couldn't be read."); return false; }
    // where: the floor in the middle of the view (or the ground a little ahead)
    CamB cb = camBasis();
    V3 o = ME.top ? ME.topC + V3{0, 50, 0} : ME.cam, d = ME.top ? V3{0, -1, 0} : cb.fwd;
    gmap::Hit h = gmap::rayMap(ME.map, o, d, 200);
    V3 at = h.brush >= 0 ? o + d * h.t : V3{o.x + cb.fwd.x * 8, 0, o.z + cb.fwd.z * 8};
    if (h.brush < 0 && std::fabs(d.y) > 1e-3) { double t = -o.y / d.y; if (t > 0 && t < 200) at = o + d * t; }
    at = {snapG(at.x), h.brush >= 0 ? at.y : 0, snapG(at.z)};
    meSnapshot();
    ME.sel.clear(); ME.ent = -1;
    for (auto b : g.brushes) { gmap::translate(b, at); ME.map.brushes.push_back(b); ME.sel.push_back((int)ME.map.brushes.size() - 1); }
    for (auto e : g.ents) { e.pos += at; ME.map.ents.push_back(e); if (ME.sel.empty()) ME.ent = (int)ME.map.ents.size() - 1; }
    for (auto& np : g.npcs) {   // the NPCs it uses come along
        bool have = false;
        for (auto& k : ME.map.npcs) if (k.first == np.first) have = true;
        if (!have) ME.map.npcs.push_back(np);
    }
    installMapNpcs(g);
    meChanged();
    toast("Placed " + name + ". It's selected: drag it where you want it.");
    return true;
}
std::vector<std::string> meMapFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(MAP_DIR, ec)) return out;
    for (auto& e : fs::directory_iterator(MAP_DIR, ec))
        if (e.path().extension() == ".gsmap" && e.path().filename().string()[0] != '.') out.push_back(e.path().string());
    std::sort(out.begin(), out.end());
    return out;
}
void meNew() {
    meSnapshot();
    ME.map = gmap::Map();
    ME.map.name = "New map";
    ME.map.look.daylight = 0.05;   // dark until you light it (Map > Light: Daylight; the Light tool)
    { gmap::Entity l; l.type = "light"; l.pos = {0, 4, 0}; l.power = 14; l.range = 22; ME.map.ents.push_back(l); }
    ME.map.brushes.push_back(gmap::makeBox({-16, -0.5, -16}, {16, 0, 16}, gmap::materialIndex("dev_grid")));
    gmap::Entity s; s.pos = {-6, 0, 0}; ME.map.ents.push_back(s);
    ME.sel.clear(); ME.face = -1; ME.ent = -1;
    meChanged();
    ME.cam = {-14, 5, 0}; ME.yaw = 0; ME.pitch = -0.3;
}
void meLoadBuiltin() {
    meSnapshot();
    ME.map = gmap::builtinRange();
    ME.sel.clear(); ME.face = -1; ME.ent = -1;
    meChanged();
    ME.cam = {-18, 12, -24}; ME.yaw = 0.46; ME.pitch = -0.32;
}

// ---------------------------------------------------------------- operations
void meDelete() {
    if (ME.ent >= 0) { meSnapshot(); ME.map.ents.erase(ME.map.ents.begin() + ME.ent); ME.ent = -1; meChanged(); return; }
    if (ME.sel.empty()) return;
    meSnapshot();
    std::vector<int> s = ME.sel;
    std::sort(s.rbegin(), s.rend());
    for (int i : s) ME.map.brushes.erase(ME.map.brushes.begin() + i);
    ME.sel.clear(); ME.face = -1;
    meChanged();
}
void meDuplicate() {
    if (ME.sel.empty()) return;
    meSnapshot();
    std::vector<int> ns;
    double off = std::max(ME.grid, 0.05);
    for (int i : ME.sel) {
        gmap::Brush b = ME.map.brushes[i];
        gmap::translate(b, {off, 0, off});
        ME.map.brushes.push_back(b);
        ns.push_back((int)ME.map.brushes.size() - 1);
    }
    ME.sel = ns; ME.face = -1;
    meChanged();
}
static V3 selCentre() {
    V3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
    for (int i : ME.sel) {
        V3 a, b; gmap::brushBounds(ME.map.brushes[i], a, b);
        mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)};
        mx = {std::max(mx.x, b.x), std::max(mx.y, b.y), std::max(mx.z, b.z)};
    }
    return (mn + mx) * 0.5;
}
void meRotate(double deg) {
    if (ME.ent >= 0) { meSnapshot(); ME.map.ents[ME.ent].yaw = std::fmod(ME.map.ents[ME.ent].yaw + deg + 360, 360); meChanged(); return; }
    if (ME.sel.empty()) return;
    meSnapshot();
    V3 c = selCentre();
    c.x = snapG(c.x); c.z = snapG(c.z);
    for (int i : ME.sel) gmap::rotateY(ME.map.brushes[i], deg * DEG2RAD, c);
    meChanged();
}
void meMirror(int axis) {
    if (ME.sel.empty()) return;
    meSnapshot();
    V3 c = selCentre();
    for (int i : ME.sel) gmap::mirror(ME.map.brushes[i], axis, c);
    meChanged();
}
void meHollow() {
    if (ME.sel.empty()) return;
    meSnapshot();
    std::vector<int> s = ME.sel;
    std::sort(s.rbegin(), s.rend());
    std::vector<gmap::Brush> made;
    for (int i : s) {
        auto w = gmap::hollow(ME.map.brushes[i], ME.wall);
        if (w.empty()) continue;
        ME.map.brushes.erase(ME.map.brushes.begin() + i);
        made.insert(made.end(), w.begin(), w.end());
    }
    ME.sel.clear();
    for (auto& b : made) { ME.map.brushes.push_back(b); ME.sel.push_back((int)ME.map.brushes.size() - 1); }
    ME.face = -1;
    meChanged();
}
void meApplyMaterial(int mat) {
    ME.mat = mat;
    if (ME.sel.empty()) return;
    meSnapshot();
    if (ME.face >= 0 && ME.sel.size() == 1) ME.map.brushes[ME.sel[0]].faces[ME.face].mat = mat;
    else for (int i : ME.sel) for (auto& f : ME.map.brushes[i].faces) f.mat = mat;
    meChanged();
}
void meFocus() {
    V3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
    auto add = [&](const gmap::Brush& b) {
        V3 a, c; gmap::brushBounds(b, a, c);
        mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)};
    };
    if (!ME.sel.empty()) for (int i : ME.sel) add(ME.map.brushes[i]);
    else if (ME.ent >= 0) { V3 p = ME.map.ents[ME.ent].pos; mn = p - V3{1, 0, 1}; mx = p + V3{1, 2, 1}; }
    else for (auto& b : ME.map.brushes) add(b);
    if (mn.x > mx.x) return;
    V3 c = (mn + mx) * 0.5;
    double r = std::max(1.0, gmap::len(mx - mn) / 2);
    if (ME.top) { ME.topC = c; ME.topH = std::max(6.0, r * 2.4); return; }
    CamB b = camBasis();
    ME.cam = c - b.fwd * (r * 1.6 + 1.5);
}
void meWalk(bool fromCamera) {
    gmap::playerSpawn(ME.pl, ME.map);
    if (fromCamera || startEntity() < 0) {
        V3 o = ME.top ? ME.topC + V3{0, 50, 0} : ME.cam;
        gmap::Hit h = gmap::rayMap(ME.map, o, {0, -1, 0}, 500);
        ME.pl.feet = h.brush >= 0 ? o + V3{0, -h.t + 0.02, 0} : V3{o.x, 0.02, o.z};
        if (!ME.top) ME.pl.yaw = ME.yaw;
    }
    if (const char* e = std::getenv("GUNSMITH_WALKYAW")) ME.pl.yaw = std::atof(e);   // (screenshots) which way you face
    ME.walking = true;
    DisableCursor();
}
static void stopWalking() {
    ME.walking = false;
    EnableCursor();
}

// ---------------------------------------------------------------- entering and leaving
void enterMapEditor() {
    mapGfxInit();
    A.screen = Screen::MapEditor;
    A.showPause = A.showSettings = A.showBenchPicker = false;
    if (ME.map.brushes.empty()) {
        std::ifstream in(mapAutosave());
        std::stringstream ss; ss << in.rdbuf();
        gmap::Map m;
        if (in && gmap::mapFromJson(ss.str(), m)) { ME.map = m; ME.dirty = false; }
        else { ME.map = gmap::builtinRange(); ME.cam = {-18, 12, -24}; ME.yaw = 0.46; ME.pitch = -0.32; }
    }
    ME.version++;
    ME.walking = false;
}
static std::string gSandboxStash;   // the sandbox map, set aside while you edit your career workshop
void enterCareerWorkshop(const std::string& file, bool walk) {
    mapGfxInit();
    if (ME.careerFile.empty()) gSandboxStash = gmap::mapToJson(ME.map);
    ME.careerFile = file;
    std::ifstream in(file);
    std::stringstream ss; ss << in.rdbuf();
    gmap::Map m;
    if (!in || !gmap::mapFromJson(ss.str(), m)) { m = gmap::defaultWorkshop(); std::error_code ec; fs::create_directories(fs::path(file).parent_path(), ec); std::ofstream(file) << gmap::mapToJson(m); }
    ME.map = m; ME.sel.clear(); ME.face = -1; ME.ent = -1; ME.undo.clear(); ME.redo.clear(); ME.dirty = false;
    A.screen = Screen::MapEditor;
    A.showPause = A.showSettings = A.showBenchPicker = false;
    ME.version++;
    ME.walking = false;
    ME.cam = {-10, 7, 9}; ME.yaw = -0.6; ME.pitch = -0.45;
    if (walk) meWalk(false);
}
void leaveCareerWorkshop() {
    if (ME.careerFile.empty()) return;
    if (ME.walking) stopWalking();
    writeAutosave();
    ME.careerFile.clear();
    gmap::Map m;
    if (gmap::mapFromJson(gSandboxStash, m)) ME.map = m; else ME.map = gmap::Map();
    ME.sel.clear(); ME.face = -1; ME.ent = -1; ME.undo.clear(); ME.redo.clear();
    ME.version++;
}
void leaveMapEditor() {
    leaveCareerWorkshop();
    if (ME.walking) stopWalking();
    if (CFG.autosave != AutosaveEvery::Off) writeAutosave();
    mapRestoreLook();
}

// ---------------------------------------------------------------- input: camera
static void flyCamera(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    static bool flying = false;
    bool inView = mouseInView();
    if (ME.top) {
        Vector2 d = GetMouseDelta();
        if ((IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) && (inView || flying)) {
            flying = true;
            VP v = viewRect();
            double mpp = ME.topH / v.h;
            ME.topC.x -= d.x * mpp; ME.topC.z -= d.y * mpp;
        } else flying = false;
        float w = GetMouseWheelMove();
        if (w != 0 && inView) ME.topH = std::clamp(ME.topH * std::pow(0.88, (double)w), 2.0, 800.0);
        return;
    }
    CamB b = camBasis();
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && (inView || flying)) {
        flying = true;
        Vector2 d = GetMouseDelta();
        double s = 0.0022 * CFG.mouseSens;
        ME.yaw += d.x * s;
        ME.pitch = std::clamp(ME.pitch - d.y * s * (CFG.invertMouse ? -1 : 1), -1.55, 1.55);
        if (!io.WantCaptureKeyboard) {
            double sp = ME.flySpeed * (actDown(Act::Sprint) ? 3 : 1) * dt;
            V3 mv;
            if (actDown(Act::MoveFwd)) mv += b.fwd;
            if (actDown(Act::MoveBack)) mv -= b.fwd;
            if (actDown(Act::MoveRight)) mv += b.right;
            if (actDown(Act::MoveLeft)) mv -= b.right;
            if (actDown(Act::FlyUp)) mv += V3{0, 1, 0};
            if (actDown(Act::FlyDown)) mv -= V3{0, 1, 0};
            ME.cam += mv * sp;
        }
        float w = GetMouseWheelMove();
        if (w != 0) ME.flySpeed = std::clamp(ME.flySpeed * std::pow(1.25, (double)w), 1.0, 80.0);
    } else {
        flying = false;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && inView) {  // pan
            Vector2 d = GetMouseDelta();
            double s = 0.01 + 0.002 * ME.flySpeed;
            ME.cam += b.right * (-d.x * s) + b.up * (d.y * s);
        }
        float w = GetMouseWheelMove();
        if (w != 0 && inView) ME.cam += b.fwd * (w * std::max(0.5, ME.flySpeed * 0.25));
    }
}

// ---------------------------------------------------------------- input: tools
namespace {
enum class Drag { None, Move, MoveEnt, Face, Create };
Drag drag = Drag::None;
bool dragChanged = false, dragVertical = false;
V3 dragHit, dragPlaneN;
double dragPlaneD = 0;
std::vector<gmap::Brush> dragOrig;
V3 dragEntOrig;
int dragFace = -1;
Vector2 dragMouse0;
V3 createA, createB;
double createY = 0;
std::string preDrag;
gmap::Stance wantStance = gmap::Stance::Stand;
}

static void beginDrag(Drag k) { drag = k; dragChanged = false; preDrag = gmap::mapToJson(ME.map); dragMouse0 = GetMousePosition(); }
static void endDrag() {
    if (dragChanged) {
        ME.undo.push_back(preDrag);
        if (ME.undo.size() > 100) ME.undo.erase(ME.undo.begin());
        ME.redo.clear();
        meChanged();
    }
    drag = Drag::None;
}

// the face handle under the mouse, for a single selected brush
static int faceHandleAt(Vector2 m) {
    if (ME.sel.size() != 1 || ME.face >= 0) return -1;
    const gmap::Brush& b = ME.map.brushes[ME.sel[0]];
    CamB c = camBasis();
    for (size_t i = 0; i < b.faces.size(); i++) {
        auto p = gmap::facePolygon(b, (int)i);
        if (p.empty()) continue;
        V3 cen; for (auto& q : p) cen += q; cen = cen * (1.0 / p.size());
        bool facing = c.ortho ? dot(b.faces[i].n, c.fwd) < -0.2 || std::fabs(b.faces[i].n.y) < 0.2 : dot(b.faces[i].n, c.pos - cen) > 0;
        if (!facing) continue;
        Vector2 s;
        if (project(cen, s) && Vector2Distance(s, m) < 9) return (int)i;
    }
    return -1;
}

static int wedgeDir() {
    double fx = std::cos(ME.yaw), fz = std::sin(ME.yaw);
    if (std::fabs(fx) >= std::fabs(fz)) return fx > 0 ? 0 : 2;
    return fz > 0 ? 1 : 3;
}

static bool surfaceUnderMouse(V3& p, V3& n, int& brush, int& face) {
    V3 o, d; mouseRay(o, d);
    gmap::Hit h = gmap::rayMap(ME.map, o, d, 5000);
    if (h.brush >= 0) { p = o + d * h.t; n = h.n; brush = h.brush; face = h.face; return true; }
    brush = face = -1; n = {0, 1, 0};
    return rayPlaneY(o, d, 0, p);
}

static int entityUnderMouse(double* tOut) {
    V3 o, d; mouseRay(o, d);
    int best = -1; double bt = 1e30;
    for (size_t i = 0; i < ME.map.ents.size(); i++) {
        V3 p = ME.map.ents[i].pos;
        bool lamp = ME.map.ents[i].type == "light";   // (a lamp: a small ball where it hangs)
        gmap::Brush b = lamp ? gmap::makeBox(p - V3{0.22, 0.22, 0.22}, p + V3{0.22, 0.22, 0.22}, 0) : gmap::makeBox(p - V3{0.3, 0, 0.3}, p + V3{0.3, 1.8, 0.3}, 0);
        gmap::Hit h = gmap::rayBrush(b, o, d, 5000);
        if (h.brush >= 0 && h.t < bt) { bt = h.t; best = (int)i; }
    }
    if (tOut) *tOut = bt;
    return best;
}

static void clipPlane(V3& n, double& d, bool& ok) {
    ok = false;
    if (ME.clipPts.size() < 2) return;
    V3 a = ME.clipPts[0], b = ME.clipPts[1];
    V3 c = cross(b - a, ME.clipFaceN);
    if (gmap::len(c) < 1e-9) return;
    n = norm(c);
    if (ME.clipFlip) n = -n;
    d = dot(n, a);
    ok = true;
}
static void applyClip(bool split) {
    V3 n; double d; bool ok;
    clipPlane(n, d, ok);
    if (!ok || ME.sel.empty()) return;
    meSnapshot();
    int cut = 0;
    std::vector<int> ns = ME.sel;
    for (int i : ME.sel) {
        gmap::Brush keep = ME.map.brushes[i], other = ME.map.brushes[i];
        int mat = keep.faces.empty() ? ME.mat : keep.faces[0].mat;
        bool k = gmap::clipBrush(keep, n, d, mat), o = split && gmap::clipBrush(other, -n, -d, mat);
        if (!k && !o) continue;
        if (k) ME.map.brushes[i] = keep; else ME.map.brushes[i] = other;
        if (k && o) { ME.map.brushes.push_back(other); ns.push_back((int)ME.map.brushes.size() - 1); }
        cut++;
    }
    ME.sel = ns;
    ME.clipPts.clear();
    if (cut) meChanged(); else { ME.undo.pop_back(); ME.status = "That cut would remove the whole brush. Press Tab to keep the other side."; }
}

static void toolInput() {
    if (!mouseInView() && drag == Drag::None) return;
    Vector2 m = GetMousePosition();
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    V3 o, d; mouseRay(o, d);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseInView()) {
        switch (ME.tool) {
        case MTool::Select: {
            // the first click picks (a brush, a thing, a face's square); press on it again to drag it
            static int pickedFace = -1;
            int fh = faceHandleAt(m);
            if (fh >= 0 && fh != pickedFace) { pickedFace = fh; ME.status = "Face picked: drag it to resize."; break; }
            if (fh < 0) pickedFace = -1;
            if (fh >= 0) {
                beginDrag(Drag::Face);
                dragFace = fh; dragOrig = {ME.map.brushes[ME.sel[0]]};
                break;
            }
            double te; int e = entityUnderMouse(&te);
            gmap::Hit h = gmap::rayMap(ME.map, o, d, 5000);
            if (e >= 0 && (h.brush < 0 || te < h.t)) {
                if (ME.ent != e) { ME.sel.clear(); ME.face = -1; ME.ent = e; break; }
                ME.sel.clear(); ME.face = -1; ME.ent = e;
                beginDrag(Drag::MoveEnt);
                dragEntOrig = ME.map.ents[e].pos;
                dragHit = o + d * te; dragVertical = alt;
                break;
            }
            if (h.brush < 0) { if (!shift) { ME.sel.clear(); ME.face = -1; ME.ent = -1; } break; }
            ME.ent = -1;
            if (ctrl) { ME.sel = {h.brush}; ME.face = h.face; ME.mat = ME.map.brushes[h.brush].faces[h.face].mat; break; }
            if (shift) {
                if (isSel(h.brush)) ME.sel.erase(std::find(ME.sel.begin(), ME.sel.end(), h.brush));
                else ME.sel.push_back(h.brush);
                ME.face = -1;
                break;
            }
            if (!isSel(h.brush)) { ME.sel = {h.brush}; ME.face = -1; break; }
            ME.face = -1;
            beginDrag(Drag::Move);
            dragHit = o + d * h.t; dragVertical = alt;
            dragOrig.clear();
            for (int i : ME.sel) dragOrig.push_back(ME.map.brushes[i]);
            break;
        }
        case MTool::Box: case MTool::Wedge: case MTool::Cylinder: case MTool::Stairs: {
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            createY = (b >= 0 && n.y > 0.7) ? p.y : 0;
            if (b >= 0 && n.y > 0.7) createY = std::round(createY / 0.001) * 0.001;
            createA = createB = {snapG(p.x), createY, snapG(p.z)};
            beginDrag(Drag::Create);
            break;
        }
        case MTool::Clip: {
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            if (ME.sel.empty() && b >= 0) ME.sel = {b};
            V3 s = snap3(p);
            if (b >= 0) { const auto& fc = ME.map.brushes[b].faces[f]; s = s - fc.n * (dot(fc.n, s) - fc.d); }  // back onto the face
            if (ME.clipPts.size() >= 2) ME.clipPts.clear();
            if (ME.clipPts.empty()) ME.clipFaceN = n;
            ME.clipPts.push_back(s);
            break;
        }
        case MTool::Station: {   // a workstation (career mode): it stands on the floor, facing you
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            meSnapshot();
            gmap::Entity e;
            e.type = ME.stationKind == "counter" ? "shop_counter" : "station_" + ME.stationKind;
            e.pos = {snapG(p.x), n.y > 0.7 ? p.y : 0, snapG(p.z)};
            e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);
            ME.map.ents.push_back(e);
            ME.ent = (int)ME.map.ents.size() - 1; ME.sel.clear();
            meChanged();
            break;
        }
        case MTool::Npc: case MTool::Spawner: case MTool::Armor: case MTool::Crate: case MTool::Ammo: case MTool::Weapons: {   // an NPC standing here, a stand to take guns from, armour plates
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            if (ME.tool == MTool::Npc && ME.npcPick.empty()) { toast("Pick which NPC in the panel first (make one in the NPC editor)."); break; }
            meSnapshot();
            gmap::Entity e;
            e.type = ME.tool == MTool::Npc ? "npc_spawn" : ME.tool == MTool::Armor ? "armor_plate" : ME.tool == MTool::Crate ? "attachment_crate"
                   : ME.tool == MTool::Ammo ? "ammo_box" : ME.tool == MTool::Weapons ? "weapon_crate" : "gun_spawner";
            e.ref = ME.tool == MTool::Npc ? ME.npcPick : "";
            e.pos = {snapG(p.x), n.y > 0.7 ? p.y : 0, snapG(p.z)};
            e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);   // facing you
            ME.map.ents.push_back(e);
            ME.ent = (int)ME.map.ents.size() - 1; ME.sel.clear();
            meChanged();
            break;
        }
        case MTool::Light: {   // a lamp: a little out from the surface clicked (under a ceiling, on a wall, over a floor)
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            meSnapshot();
            gmap::Entity e; e.type = "light";
            double out = n.y > 0.7 ? 2.4 : 0.15;   // on a floor: up at head height and more
            e.pos = p + n * out;
            if (n.y < -0.7) { e.cone = 0; e.pitch = -90; }                         // under a ceiling: all round
            else if (std::fabs(n.y) <= 0.7) {                                       // on a wall: a spot, out and a little down
                e.cone = 90; e.pitch = -30; e.yaw = std::atan2(n.z, n.x) * RAD2DEG;
            }
            ME.map.ents.push_back(e);
            ME.ent = (int)ME.map.ents.size() - 1; ME.sel.clear();
            meChanged();
            break;
        }
        case MTool::Start: {
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            meSnapshot();
            int e = startEntity();
            if (e < 0) { ME.map.ents.push_back(gmap::Entity()); e = (int)ME.map.ents.size() - 1; }
            ME.map.ents[e].pos = {snapG(p.x), n.y > 0.7 ? p.y : 0, snapG(p.z)};
            ME.map.ents[e].yaw = ME.top ? ME.map.ents[e].yaw : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 720, 360);
            ME.ent = e; ME.sel.clear();
            meChanged();
            break;
        }
        }
    }

    if (drag != Drag::None && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        bool moved = Vector2Distance(m, dragMouse0) > 5;
        if (drag == Drag::Move || drag == Drag::MoveEnt) {
            V3 p;
            bool ok;
            if (dragVertical && !ME.top) {
                CamB c = camBasis();
                V3 n = norm({c.fwd.x, 0, c.fwd.z});
                ok = rayPlane(o, d, n, dot(n, dragHit), p);
                if (ok) p = {dragHit.x, p.y, dragHit.z};
            } else ok = ME.top ? (p = o + d * ((o.y - dragHit.y) / -d.y), true) : rayPlaneY(o, d, dragHit.y, p);
            if (ok && moved) {
                V3 dl = p - dragHit;
                dl = {snapG(dl.x), snapG(dl.y), snapG(dl.z)};
                if (drag == Drag::Move) for (size_t k = 0; k < ME.sel.size() && k < dragOrig.size(); k++) { ME.map.brushes[ME.sel[k]] = dragOrig[k]; gmap::translate(ME.map.brushes[ME.sel[k]], dl); }
                else if (ME.ent >= 0) ME.map.ents[ME.ent].pos = dragEntOrig + dl;
                if (gmap::len(dl) > 0) { dragChanged = true; ME.version++; }
            }
        } else if (drag == Drag::Face && !dragOrig.empty()) {
            gmap::Brush b = dragOrig[0];
            auto poly = gmap::facePolygon(b, dragFace);
            V3 cen; for (auto& q : poly) cen += q; if (!poly.empty()) cen = cen * (1.0 / poly.size());
            Vector2 s0, s1;
            if (project(cen, s0) && project(cen + b.faces[dragFace].n, s1)) {
                Vector2 sv = Vector2Subtract(s1, s0);
                float l2 = Vector2LengthSqr(sv);
                if (l2 > 1) {
                    Vector2 md = Vector2Subtract(m, dragMouse0);
                    double amt = snapG((md.x * sv.x + md.y * sv.y) / l2);
                    if (gmap::pushFace(b, dragFace, amt)) { ME.map.brushes[ME.sel[0]] = b; ME.version++; dragChanged = amt != 0; }
                }
            }
        } else if (drag == Drag::Create) {
            V3 p;
            bool ok = rayPlaneY(o, d, createY, p);
            if (ok) createB = {snapG(p.x), createY, snapG(p.z)};
        }
    }

    if (drag != Drag::None && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (drag == Drag::Create) {
            double minW = std::max(ME.grid, 0.01) * 0.5;
            V3 mn{std::min(createA.x, createB.x), createY, std::min(createA.z, createB.z)};
            V3 mx{std::max(createA.x, createB.x), createY + std::max(ME.height, 0.01), std::max(createA.z, createB.z)};
            if (mx.x - mn.x >= minW && mx.z - mn.z >= minW) {
                std::vector<gmap::Brush> made;
                if (ME.tool == MTool::Box) made.push_back(gmap::makeBox(mn, mx, ME.mat));
                else if (ME.tool == MTool::Wedge) made.push_back(gmap::makeWedge(mn, mx, wedgeDir(), ME.mat));
                else if (ME.tool == MTool::Cylinder) made.push_back(gmap::makeCylinder(mn, mx, ME.cylSides, ME.mat));
                else made = gmap::makeStairs(mn, mx, ME.stairSteps, wedgeDir(), ME.mat);
                ME.sel.clear();
                for (auto& b : made) if (gmap::brushValid(b)) { ME.map.brushes.push_back(b); ME.sel.push_back((int)ME.map.brushes.size() - 1); }
                ME.face = -1; ME.ent = -1;
                dragChanged = !made.empty();
            }
        }
        endDrag();
    }
}

static void keyInput() {
    if (ImGui::GetIO().WantCaptureKeyboard || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) return;
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (actPressed(Act::Undo)) meUndo();
    if (actPressed(Act::Redo) || (ctrl && shift && IsKeyPressed(KEY_Z))) meRedo();
    if (actPressed(Act::Delete) || (IsKeyPressed(KEY_BACKSPACE) && !ctrl)) meDelete();
    if (actPressed(Act::Duplicate)) meDuplicate();
    if (ctrl && IsKeyPressed(KEY_S)) meSave();
    if (actPressed(Act::MapRotate)) meRotate(shift ? -90 : 90);
    if (actPressed(Act::MapHollow)) meHollow();
    if (actPressed(Act::MapTopView)) { if (!ME.top) { ME.topC = {ME.cam.x, 0, ME.cam.z}; } ME.top = !ME.top; }
    if (actPressed(Act::MapFocus)) meFocus();
    if (actPressed(Act::MapWalk)) meWalk(shift);
    // the clip tool
    if (ME.tool == MTool::Clip && ME.clipPts.size() == 2) {
        if (IsKeyPressed(KEY_TAB)) ME.clipFlip = !ME.clipFlip;
        if (actPressed(Act::Finish) || IsKeyPressed(KEY_KP_ENTER)) applyClip(shift);
    }
    // nudge with the arrow keys, by one grid step, relative to the view
    V3 nud;
    double g = std::max(ME.grid, 0.01);
    V3 fx, rx;
    if (ME.top) { fx = {0, 0, -1}; rx = {1, 0, 0}; }
    else {
        double cx = std::cos(ME.yaw), cz = std::sin(ME.yaw);
        fx = std::fabs(cx) >= std::fabs(cz) ? V3{cx > 0 ? 1.0 : -1.0, 0, 0} : V3{0, 0, cz > 0 ? 1.0 : -1.0};
        rx = cross(fx, {0, 1, 0});
    }
    if (IsKeyPressed(KEY_UP)) nud += fx * g;
    if (IsKeyPressed(KEY_DOWN)) nud -= fx * g;
    if (IsKeyPressed(KEY_RIGHT)) nud += rx * g;
    if (IsKeyPressed(KEY_LEFT)) nud -= rx * g;
    if (IsKeyPressed(KEY_PAGE_UP)) nud.y += g;
    if (IsKeyPressed(KEY_PAGE_DOWN)) nud.y -= g;
    if (gmap::len(nud) > 0 && (!ME.sel.empty() || ME.ent >= 0)) {
        meSnapshot();
        for (int i : ME.sel) gmap::translate(ME.map.brushes[i], nud);
        if (ME.ent >= 0) ME.map.ents[ME.ent].pos += nud;
        meChanged();
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (drag != Drag::None) { restore(preDrag); drag = Drag::None; ME.version++; }
        else if (!ME.clipPts.empty()) ME.clipPts.clear();
        else if (ME.tool != MTool::Select) ME.tool = MTool::Select;
        else if (!ME.sel.empty() || ME.ent >= 0) { ME.sel.clear(); ME.face = -1; ME.ent = -1; }
        else A.showPause = true;
    }
}

// ---------------------------------------------------------------- walking
static void walkUpdate(float dt) {
    if (IsKeyPressed(KEY_ESCAPE)) { stopWalking(); return; }
    Vector2 md = GetMouseDelta();
    double s = 0.0021 * CFG.mouseSens;
    ME.pl.yaw += md.x * s;
    ME.pl.pitch = std::clamp(ME.pl.pitch - md.y * s * (CFG.invertMouse ? -1 : 1), -1.5, 1.5);
    gmap::PlayerInput in;
    in.forward = (actDown(Act::MoveFwd) ? 1 : 0) - (actDown(Act::MoveBack) ? 1 : 0);
    in.right = (actDown(Act::MoveRight) ? 1 : 0) - (actDown(Act::MoveLeft) ? 1 : 0);
    in.sprint = actDown(Act::Sprint);
    in.jump = actPressed(Act::Jump);
    if (actPressed(Act::Crouch)) wantStance = wantStance == gmap::Stance::Crouch ? gmap::Stance::Stand : gmap::Stance::Crouch;
    if (actPressed(Act::Prone)) wantStance = wantStance == gmap::Stance::Prone ? gmap::Stance::Stand : gmap::Stance::Prone;
    if (in.jump && wantStance != gmap::Stance::Stand) { wantStance = gmap::Stance::Stand; in.jump = false; }
    in.want = wantStance;
    in.lean = (actDown(Act::LeanRight) ? 1 : 0) - (actDown(Act::LeanLeft) ? 1 : 0);
    gmap::playerStep(ME.pl, ME.map, in, dt);
}

// ---------------------------------------------------------------- drawing
static void drawBrushEdges(const gmap::Brush& b, Color c) {
    for (size_t i = 0; i < b.faces.size(); i++) {
        auto p = gmap::facePolygon(b, (int)i);
        for (size_t k = 0; k < p.size(); k++) DrawLine3D(rv(p[k]), rv(p[(k + 1) % p.size()]), c);
    }
}
static void drawFaceFill(const gmap::Brush& b, int fi, Color c) {
    auto p = gmap::facePolygon(b, fi);
    if (p.size() < 3) return;
    V3 off = b.faces[fi].n * 0.004;
    for (size_t k = 1; k + 1 < p.size(); k++) DrawTriangle3D(rv(p[0] + off), rv(p[k] + off), rv(p[k + 1] + off), c);
}
static void drawLampMarker(const gmap::Entity& e, bool sel) {   // a glowing bulb in its colour; a spot's aim; its reach when picked
    Color c{(unsigned char)std::clamp(e.color[0] * 255, 0.0, 255.0), (unsigned char)std::clamp(e.color[1] * 255, 0.0, 255.0), (unsigned char)std::clamp(e.color[2] * 255, 0.0, 255.0), 255};
    rlDrawRenderBatchActive();
    DrawSphere(rv(e.pos), 0.09f, c);   // (unlit: it's the light itself)
    DrawSphereWires(rv(e.pos), 0.14f, 6, 8, sel ? Color{255, 205, 60, 255} : Color{40, 44, 52, 255});
    gmap::LampAt L = gmap::lampOf(e);
    if (e.cone > 0) {
        double len = std::min(1.5, e.range * 0.3), half = e.cone / 2 * DEG2RAD;
        V3 d = L.dir, s = gmap::norm(gmap::cross(d, std::fabs(d.y) > 0.9 ? V3{1, 0, 0} : V3{0, 1, 0})), t = gmap::cross(d, s);
        V3 tip = e.pos + d * len;
        DrawLine3D(rv(e.pos), rv(tip), c);
        for (int k = 0; k < 8; k++) {
            double a = 2 * PI * k / 8;
            V3 rim = tip + (s * std::cos(a) + t * std::sin(a)) * (len * std::tan(std::min(half, 1.4)));
            DrawLine3D(rv(e.pos), rv(rim), Color{c.r, c.g, c.b, 120});
        }
    }
    if (sel) {   // how far it reaches: a ring on the floor below it and round it
        const int N = 48;
        for (int k = 0; k < N; k++) {
            double a0 = 2 * PI * k / N, a1 = 2 * PI * (k + 1) / N;
            DrawLine3D(rv(e.pos + V3{std::cos(a0) * e.range, 0, std::sin(a0) * e.range}), rv(e.pos + V3{std::cos(a1) * e.range, 0, std::sin(a1) * e.range}), Color{c.r, c.g, c.b, 140});
        }
    }
}
static void drawSpawner(const gmap::Entity& e, bool sel) {   // a crate on legs with a screen: the gun spawner
    Color c = sel ? Color{255, 205, 60, 255} : Color{70, 120, 170, 255};
    V3 p = e.pos;
    double y = e.yaw * DEG2RAD;
    Matrix R = MatrixRotateY((float)-y);
    drawBox(rv(p + V3{0, 0.45, 0}), {0.9f, 0.9f, 0.6f}, Color{60, 66, 74, 255}, R);
    drawBox(rv(p + V3{0, 0.92, 0}), {0.95f, 0.06f, 0.65f}, c, R);
    V3 f{std::cos(y), 0, std::sin(y)};
    drawBox(rv(p + V3{0, 1.25, 0} + f * 0.05), {0.7f, 0.45f, 0.04f}, Color{30, 40, 52, 255}, MatrixMultiply(MatrixRotateZ(0), R));
}
static void drawNpcSpawn(const gmap::Entity& e, bool sel) {
    NpcDef n;
    if (npcByName(ME.map, e.ref, n)) drawNpcAt(n, rv(e.pos), (float)e.yaw, 0.001f, -1);
    else drawCyl(rv(e.pos), rv(e.pos + V3{0, 1.7, 0}), 0.25f, Color{200, 80, 70, 255}, 0.6f, nullptr);   // (its NPC is missing)
    if (sel) {
        rlDisableDepthMask();
        drawCyl(rv(e.pos), rv(e.pos + V3{0, 0.05, 0}), 0.6f, Color{255, 205, 60, 255}, 0.5f, nullptr);
        rlEnableDepthMask();
    }
}
static void drawStart(const gmap::Entity& e, bool sel) {
    Color c = sel ? Color{255, 205, 60, 255} : Color{90, 220, 140, 255};
    V3 p = e.pos;
    rlDisableDepthMask();
    drawCyl(rv(p), rv(p + V3{0, 1.8, 0}), 0.25f, c, 0.45f, nullptr);
    rlEnableDepthMask();
    double y = e.yaw * DEG2RAD;
    V3 dir{std::cos(y), 0, std::sin(y)};
    V3 a = p + V3{0, 1.62, 0};
    drawCyl(rv(a), rv(a + dir * 0.7), 0.04f, c);
    drawCyl(rv(a + dir * 0.6), rv(a + dir * 0.85), 0.1f, c, 1, &G.cone);
}

static const char* HINT[] = {
    "Click a brush to select (Shift adds, Ctrl picks one face). Drag to move, Alt+drag up and down. Drag the squares to resize. Right mouse: look and fly with WASD.",
    "Drag on the ground, or on top of a brush, to draw a box. Its height is set in the top bar.",
    "Drag to draw a ramp. It rises away from where you're looking.",
    "Drag to draw a cylinder. Its number of sides is in the panel.",
    "Drag to draw stairs. They climb away from where you're looking.",
    "Click two points on a face to cut along that line. Tab keeps the other side, Enter cuts, Shift+Enter splits in two.",
    "Click to place the player start, facing the way you're looking.",
    "Click to place the NPC chosen in the panel. It stands there facing you; set how many in the panel.",
    "Click to place a gun spawner: on the map, walk up to it and press F to take any of your guns.",
    "Click to place armour plates: players walk over them to fill their armour bar. Set how many and whether they come back in the panel.",
    "Click to place an attachment crate: on the map, walk up to it and press F to fit attachments to the gun in your hands.",
};

static void drawEditor3D() {
    CamB b = camBasis();
    // the ground grid, centred under the camera
    if (ME.showGrid) {
        float steps[3] = {(float)ME.grid, (float)(ME.grid * 4), (float)(ME.grid * 16)};
        V3 c = ME.top ? ME.topC : ME.cam;
        float rad = ME.top ? (float)ME.topH * 1.2f : (float)std::clamp(std::fabs(ME.cam.y) * 8 + 20, 30.0, 300.0);
        drawWorldGrid(0.003f, (float)c.x, (float)c.z, rad, steps, Color{255, 255, 255, 14}, Color{255, 255, 255, 34}, Color{255, 255, 255, 70});
    }
    // selection
    for (int i : ME.sel) {
        const auto& br = ME.map.brushes[i];
        if (ME.face >= 0 && ME.sel.size() == 1) drawFaceFill(br, ME.face, Color{255, 205, 60, 70});
        drawBrushEdges(br, Color{255, 205, 60, 255});
    }
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (int i : ME.sel) drawBrushEdges(ME.map.brushes[i], Color{255, 205, 60, 70});  // hidden edges, faintly
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    // brush being drawn
    if (drag == Drag::Create) {
        V3 mn{std::min(createA.x, createB.x), createY, std::min(createA.z, createB.z)};
        V3 mx{std::max(createA.x, createB.x), createY + ME.height, std::max(createA.z, createB.z)};
        if (mx.x - mn.x > 1e-6 && mx.z - mn.z > 1e-6) {
            std::vector<gmap::Brush> pv;
            if (ME.tool == MTool::Box) pv.push_back(gmap::makeBox(mn, mx, 0));
            else if (ME.tool == MTool::Wedge) pv.push_back(gmap::makeWedge(mn, mx, wedgeDir(), 0));
            else if (ME.tool == MTool::Cylinder) pv.push_back(gmap::makeCylinder(mn, mx, ME.cylSides, 0));
            else pv = gmap::makeStairs(mn, mx, ME.stairSteps, wedgeDir(), 0);
            for (auto& p : pv) drawBrushEdges(p, Color{120, 230, 200, 255});
        }
    }
    // clip preview: what stays in green, what goes in red
    if (ME.tool == MTool::Clip) {
        for (auto& p : ME.clipPts) DrawSphere(rv(p), 0.06f, Color{255, 90, 70, 255});
        if (ME.clipPts.size() == 2) {
            DrawLine3D(rv(ME.clipPts[0]), rv(ME.clipPts[1]), Color{255, 90, 70, 255});
            V3 n; double d; bool ok;
            clipPlane(n, d, ok);
            if (ok) {
                rlDrawRenderBatchActive();
                rlDisableDepthTest();
                for (int i : ME.sel) {
                    gmap::Brush k = ME.map.brushes[i], r = ME.map.brushes[i];
                    if (gmap::clipBrush(k, n, d, 0)) drawBrushEdges(k, Color{120, 230, 140, 255});
                    if (gmap::clipBrush(r, -n, -d, 0)) drawBrushEdges(r, Color{255, 90, 70, 200});
                }
                rlDrawRenderBatchActive();
                rlEnableDepthTest();
            }
        }
    }
    // entities
    for (size_t i = 0; i < ME.map.ents.size(); i++) {
        const auto& e = ME.map.ents[i];
        if (e.type == "player_start") drawStart(e, (int)i == ME.ent);
        else if (e.type == "npc_spawn") drawNpcSpawn(e, (int)i == ME.ent);
        else if (e.type == "gun_spawner") drawSpawner(e, (int)i == ME.ent);
        else if (e.type == "light") drawLampMarker(e, (int)i == ME.ent);
        else if (e.type.rfind("station_", 0) == 0 || e.type == "shop_counter") drawStation(e.type, rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "ammo_box") drawAmmoBox(rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "weapon_crate") drawWeaponCrate(rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "attachment_crate") drawAttachmentCrate(rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "armor_plate") for (int k = 0; k < std::max(1, e.count); k++)
            drawArmorPlate(rv(e.pos + V3{0, 0.03 * k, 0}), (float)(e.yaw + 9 * k), (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
    }
    (void)b;
}

static void drawEditorOverlay() {
    VP v = viewRect();
    // face handles on the single selected brush
    if (ME.sel.size() == 1 && ME.face < 0 && ME.tool == MTool::Select) {
        const auto& b = ME.map.brushes[ME.sel[0]];
        CamB c = camBasis();
        int hot = drag == Drag::Face ? dragFace : faceHandleAt(GetMousePosition());
        for (size_t i = 0; i < b.faces.size(); i++) {
            auto p = gmap::facePolygon(b, (int)i);
            if (p.empty()) continue;
            V3 cen; for (auto& q : p) cen += q; cen = cen * (1.0 / p.size());
            bool facing = c.ortho ? dot(b.faces[i].n, c.fwd) < -0.2 || std::fabs(b.faces[i].n.y) < 0.2 : dot(b.faces[i].n, c.pos - cen) > 0;
            if (!facing) continue;
            Vector2 s;
            if (!project(cen, s)) continue;
            float r = (int)i == hot ? 6.f : 4.5f;
            DrawRectangleRec({s.x - r, s.y - r, 2 * r, 2 * r}, (int)i == hot ? Color{255, 205, 60, 255} : Color{20, 24, 32, 230});
            DrawRectangleLinesEx({s.x - r, s.y - r, 2 * r, 2 * r}, 1.5f, Color{255, 205, 60, 255});
        }
    }
    // measurements while drawing a brush
    if (drag == Drag::Create) {
        Vector2 m = GetMousePosition();
        std::string t = meLen(std::fabs(createB.x - createA.x)) + " x " + meLen(std::fabs(createB.z - createA.z)) + " x " + meLen(ME.height);
        float w = viewTextWidth(t.c_str(), 12);
        DrawRectangleRounded({m.x + 14, m.y + 14, w + 14, 20}, 0.4f, 6, Color{20, 23, 28, 200});
        viewText(t.c_str(), m.x + 21, m.y + 18, 12, Color{236, 238, 242, 255});
    }
    if (CFG.hints) {
        const char* h = HINT[(int)ME.tool];
        float w = viewTextWidth(h, 12);
        DrawRectangleRounded({v.x + 12, v.y + v.h - 30, w + 20, 22}, 0.4f, 6, Color{20, 23, 28, 170});
        viewText(h, v.x + 22, v.y + v.h - 25, 12, Color{220, 224, 230, 255});
    }
}

static void drawWalkHud() {
    float cx = GetScreenWidth() / 2.f, cy = GetScreenHeight() / 2.f;
    DrawCircleV({cx, cy}, 2.5f, Color{255, 255, 255, 200});
    DrawCircleLinesV({cx, cy}, 2.5f, Color{0, 0, 0, 120});
    const char* st = ME.pl.stance == gmap::Stance::Stand ? "Standing" : ME.pl.stance == gmap::Stance::Crouch ? "Crouching" : "Prone";
    char b[160];
    double sp = std::hypot(ME.pl.vel.x, ME.pl.vel.z);
    if (CFG.units == Units::Imperial) std::snprintf(b, sizeof b, "%s   %.1f mph", st, sp * 2.23694);
    else std::snprintf(b, sizeof b, "%s   %.1f km/h", st, sp * 3.6);
    viewText(b, 18, GetScreenHeight() - 30.f, 13, Color{235, 238, 242, 230});
    const char* help = "Walking the map. Esc goes back to the editor.";
    float w = viewTextWidth(help, 13);
    DrawRectangleRounded({cx - w / 2 - 12, 14, w + 24, 24}, 0.4f, 6, Color{20, 23, 28, 150});
    viewText(help, cx - w / 2, 19, 13, Color{235, 238, 242, 230});
}

// ---------------------------------------------------------------- the frame
void mapEditorUpdate(float dt, bool modal) {
    if (!modal) {
        if (ME.walking) walkUpdate(dt);
        else { flyCamera(dt); toolInput(); keyInput(); }
    }
    if (ME.built != ME.version) { mapBuildMeshes(ME.map); ME.built = ME.version; }
}

void mapEditorRender() {
    VP v = viewRect();
    Camera3D cam;
    float pitch = 0, fov = 70;
    if (ME.walking) {
        double roll;
        V3 eye = gmap::playerEye(ME.pl, ME.map, &roll);
        V3 f = gmap::playerForward(ME.pl), r = norm(cross(f, {0, 1, 0})), u = cross(r, f);
        V3 up = u * std::cos(roll) + r * std::sin(roll);
        cam = Camera3D{};
        cam.position = rv(eye); cam.target = rv(eye + f); cam.up = rv(up); cam.fovy = CFG.fov; cam.projection = CAMERA_PERSPECTIVE;
        pitch = (float)ME.pl.pitch; fov = CFG.fov;
    } else {
        cam = toCamera(camBasis());
        pitch = (float)ME.pitch;
    }
    A.cam = cam;  // the grid shader reads the camera from here
    if (ME.top && !ME.walking) DrawRectangle((int)v.x, (int)v.y, (int)v.w, (int)v.h, Color{38, 42, 49, 255});
    else mapDrawSky(ME.map.look, v.x, v.y, v.w, v.h, pitch, fov);
    mapBeginViewport(cam, v.x, v.y, v.w, v.h);
    gmap::Look look = ME.map.look;
    if (ME.top && !ME.walking) { look.fogStart = 1e6; look.fogEnd = 2e6; }  // the top-view camera sits far above the map
    mapWorkLight(ME.walking ? 0 : ME.lightMode);
    mapApplyLook(look);
    mapSetLamps(ME.map, ME.walking ? gmap::playerEye(ME.pl, ME.map) : ME.cam);
    mapDrawWorld();
    mapWorkLight(false);
    if (!ME.walking) drawEditor3D();
    else for (auto& e : ME.map.ents) {   // on foot: the things you'd see (not the editor's markers)
        if (e.type.rfind("station_", 0) == 0 || e.type == "shop_counter") drawStation(e.type, rv(e.pos), (float)e.yaw);
        else if (e.type == "attachment_crate") drawAttachmentCrate(rv(e.pos), (float)e.yaw);
        else if (e.type == "ammo_box") drawAmmoBox(rv(e.pos), (float)e.yaw);
        else if (e.type == "weapon_crate") drawWeaponCrate(rv(e.pos), (float)e.yaw);
        else if (e.type == "armor_plate") for (int k = 0; k < std::max(1, e.count); k++) drawArmorPlate(rv(e.pos + V3{0, 0.03 * k, 0}), (float)(e.yaw + 9 * k));
    }
    EndMode3D();
    mapEndViewport();
    mapRestoreLook();
    if (ME.walking) drawWalkHud();
    else drawEditorOverlay();
}
