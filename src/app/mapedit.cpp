#include <cstdlib>
// Gunsmith3D - The map editor (see mapedit.hpp).
#include "app/paths.hpp"
#include "app/mapedit.hpp"
#include "app/doors.hpp"
#include "app/shop3d.hpp"
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/fonts.hpp"
#include "app/gfx.hpp"
#include "app/keys.hpp"
#include "app/mapgfx.hpp"
#include "app/npcedit.hpp"
#include "app/gizmo.hpp"
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
// a career property: its walls come with it, and you build inside it
static bool careerZoned() { return !ME.careerFile.empty() && ME.map.build.on(); }
static bool insideProperty(const gmap::Map& m) {
    const double pad = 0.35;
    for (auto& b : m.brushes) {
        if (b.locked) continue;
        V3 mn, mx; gmap::brushBounds(b, mn, mx);
        if (mn.x < m.build.x0 - pad || mx.x > m.build.x1 + pad || mn.z < m.build.z0 - pad || mx.z > m.build.z1 + pad) return false;
    }
    for (auto& e : m.ents) if (!m.build.has(e.pos, pad)) return false;
    return true;
}
void meChanged() {
    if (careerZoned() && !insideProperty(ME.map) && !ME.undo.empty()) {   // (outside the property: put it back)
        gmap::Map m;
        if (gmap::mapFromJson(ME.undo.back(), m)) { ME.map = m; ME.undo.pop_back(); ME.sel.clear(); ME.ent = -1; ME.face = -1; }
        toast("That's outside your property: build inside its walls.");
    }
    ME.version++;
    ME.dirty = true;
    if (CFG.autosave != AutosaveEvery::Off) writeAutosave();
}
static void restore(const std::string& s) {
    gmap::Map m;
    if (gmap::mapFromJson(s, m)) { ME.map = m; ME.sel.clear(); ME.face = -1; ME.ent = -1; ME.version++; ME.dirty = true; }
}
void meReplaceMap(const gmap::Map& m) {
    meSnapshot();
    ME.map = m; ME.sel.clear(); ME.face = -1; ME.ent = -1;
    meChanged();
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
void meSaveQuiet() { writeAutosave(); ME.dirty = false; }
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
gmap::Selection meSelection() {
    gmap::Selection s;
    for (int i : ME.sel) if (i >= 0 && i < (int)ME.map.brushes.size()) s.brushes.push_back(i);
    for (int i : ME.entSel) if (i >= 0 && i < (int)ME.map.ents.size()) s.ents.push_back(i);
    if (ME.ent >= 0 && ME.ent < (int)ME.map.ents.size() && std::find(s.ents.begin(), s.ents.end(), ME.ent) == s.ents.end()) s.ents.insert(s.ents.begin(), ME.ent);
    return s;
}
void meSetSelection(const gmap::Selection& s) {
    ME.sel = s.brushes; ME.entSel = s.ents; ME.ent = s.ents.empty() ? -1 : s.ents[0];
    ME.face = -1; ME.vertSel.clear();
}
void meDelete() {
    gmap::Selection s = meSelection();
    if (s.empty()) return;
    meSnapshot();
    std::vector<int> bs = s.brushes, es = s.ents;
    std::sort(bs.rbegin(), bs.rend()); std::sort(es.rbegin(), es.rend());
    for (int i : bs) if (!ME.map.brushes[i].frozen) ME.map.brushes.erase(ME.map.brushes.begin() + i);
    for (int i : es) if (!ME.map.ents[i].frozen) ME.map.ents.erase(ME.map.ents.begin() + i);
    meSetSelection({});
    meChanged();
}
void meDuplicate() {
    gmap::Selection s = meSelection();
    if (s.empty()) return;
    meSnapshot();
    double off = std::max(ME.grid, 0.05);
    gmap::Piece p = gmap::copyPiece(ME.map, s);
    meSetSelection(gmap::pastePiece(ME.map, p, {off, 0, off}));
    meChanged();
}
static V3 selCentre() {   // (the old way, for the focus)
    V3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
    for (int i : ME.sel) {
        V3 a, b; gmap::brushBounds(ME.map.brushes[i], a, b);
        mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)};
        mx = {std::max(mx.x, b.x), std::max(mx.y, b.y), std::max(mx.z, b.z)};
    }
    return (mn + mx) * 0.5;
}
void meTurnAbout(int axis, double deg) {
    gmap::Selection s = meSelection();
    if (s.empty()) return;
    meSnapshot();
    V3 c = gmap::selectionCentre(ME.map, s);
    if (axis == 1) { c.x = snapG(c.x); c.z = snapG(c.z); }
    gmap::rotateSelection(ME.map, s, axis, deg * DEG2RAD, c);
    meChanged();
}
void meRotate(double deg) { meTurnAbout(1, deg); }
void meMirror(int axis) {
    gmap::Selection s = meSelection();
    if (s.empty()) return;
    meSnapshot();
    gmap::mirrorSelection(ME.map, s, axis, gmap::selectionCentre(ME.map, s));
    meChanged();
}
void meSelectAll() {
    gmap::Selection s;
    for (size_t i = 0; i < ME.map.brushes.size(); i++) { const auto& b = ME.map.brushes[i]; if (!b.hidden && !b.frozen && !b.locked) s.brushes.push_back((int)i); }
    for (size_t i = 0; i < ME.map.ents.size(); i++) { const auto& e = ME.map.ents[i]; if (!e.hidden && !e.frozen) s.ents.push_back((int)i); }
    meSetSelection(s);
}
void meCopy(bool cut) {
    gmap::Selection s = meSelection();
    if (s.empty()) { toast("Nothing picked to copy."); return; }
    SetClipboardText(gmap::pieceToJson(gmap::copyPiece(ME.map, s)).c_str());
    toast(cut ? "Cut. Ctrl+V pastes it here or in any other map." : "Copied. Ctrl+V pastes it here or in any other map.");
    if (cut) meDelete();
}
bool mePaste() {
    const char* t = GetClipboardText();
    gmap::Piece p;
    if (!t || !gmap::pieceFromJson(t, p)) { toast("The clipboard has no copied brushes."); return false; }
    // where: in front of the camera (the middle of the view), on the grid
    gmap::Selection none;
    V3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
    for (auto& b : p.brushes) { V3 a, c; gmap::brushBounds(b, a, c); mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)}; mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)}; }
    for (auto& e : p.ents) { mn = {std::min(mn.x, e.pos.x), std::min(mn.y, e.pos.y), std::min(mn.z, e.pos.z)}; mx = {std::max(mx.x, e.pos.x), std::max(mx.y, e.pos.y), std::max(mx.z, e.pos.z)}; }
    V3 here = ME.top ? ME.topC : ME.cam + camBasis().fwd * 8;
    V3 move{snapG(here.x - (mn.x + mx.x) / 2), snapG(-mn.y), snapG(here.z - (mn.z + mx.z) / 2)};
    meSnapshot();
    meSetSelection(gmap::pastePiece(ME.map, p, move));
    meChanged();
    return true;
}
void meGroup(bool on) {
    gmap::Selection s = meSelection();
    if (s.empty()) return;
    meSnapshot();
    int id = 0;
    if (on) { for (auto& b : ME.map.brushes) id = std::max(id, b.group); for (auto& e : ME.map.ents) id = std::max(id, e.group); id++; }
    for (int i : s.brushes) ME.map.brushes[i].group = id;
    for (int i : s.ents) ME.map.ents[i].group = id;
    meChanged();
    toast(on ? "Grouped: clicking one picks them all." : "Ungrouped.");
}
void meHide(bool hide) {
    meSnapshot();
    if (hide) {
        gmap::Selection s = meSelection();
        for (int i : s.brushes) ME.map.brushes[i].hidden = true;
        for (int i : s.ents) ME.map.ents[i].hidden = true;
        meSetSelection({});
    } else {
        for (auto& b : ME.map.brushes) b.hidden = false;
        for (auto& e : ME.map.ents) e.hidden = false;
    }
    meChanged();
}
void meFreeze(bool freeze) {
    meSnapshot();
    if (freeze) {
        gmap::Selection s = meSelection();
        for (int i : s.brushes) ME.map.brushes[i].frozen = true;
        for (int i : s.ents) ME.map.ents[i].frozen = true;
        meSetSelection({});
    } else {
        for (auto& b : ME.map.brushes) b.frozen = false;
        for (auto& e : ME.map.ents) e.frozen = false;
    }
    meChanged();
}
void meAlign(int axis, gmap::AlignTo to) {
    gmap::Selection s = meSelection();
    if (s.brushes.size() + s.ents.size() < 2) { toast("Pick two or more things to line up."); return; }
    meSnapshot(); gmap::alignSelection(ME.map, s, axis, to); meChanged();
}
void meDistribute(int axis) {
    gmap::Selection s = meSelection();
    if (s.brushes.size() + s.ents.size() < 3) { toast("Pick three or more things to space out."); return; }
    meSnapshot(); gmap::distributeSelection(ME.map, s, axis); meChanged();
}
void meArrayLinear() {
    gmap::Selection s = meSelection();
    if (s.empty()) { toast("Pick something to make copies of."); return; }
    meSnapshot();
    gmap::Selection n = gmap::linearArray(ME.map, s, ME.arrCount, ME.arrStep);
    meSetSelection(n);
    meChanged();
}
void meArrayRadial() {
    gmap::Selection s = meSelection();
    if (s.empty()) { toast("Pick something to make copies of."); return; }
    meSnapshot();
    V3 c = gmap::selectionCentre(ME.map, s);
    V3 pivot = ME.radAxis == 1 ? V3{snapG(c.x), c.y, snapG(c.z)} : c;
    gmap::Selection n = gmap::radialArray(ME.map, s, ME.radCount, pivot, ME.radAxis, ME.radTotal, ME.radTurn);
    meSetSelection(n);
    meChanged();
}
void meSetCentre(V3 c) {
    gmap::Selection s = meSelection();
    if (s.empty()) return;
    V3 now = gmap::selectionCentre(ME.map, s), d = c - now;
    if (gmap::len(d) < 1e-9) return;
    meSnapshot(); gmap::moveSelection(ME.map, s, d); meChanged();
}
void meSetSize(V3 size) {
    gmap::Selection s = meSelection();
    if (s.brushes.empty()) return;
    V3 mn, mx; gmap::selectionBounds(ME.map, s, mn, mx);
    V3 cur = mx - mn, f{cur.x > 1e-6 ? size.x / cur.x : 1, cur.y > 1e-6 ? size.y / cur.y : 1, cur.z > 1e-6 ? size.z / cur.z : 1};
    if (f.x < 0.001 || f.y < 0.001 || f.z < 0.001) return;
    meSnapshot();
    if (gmap::scaleSelection(ME.map, s, f, (mn + mx) * 0.5)) meChanged(); else ME.undo.pop_back();
}
void meCopyMapCode() { SetClipboardText(gmap::mapShareCode(ME.map).c_str()); toast("Map code copied. Paste it anywhere to share this map."); }
bool mePasteMapCode() {
    const char* t = GetClipboardText();
    gmap::Map m;
    if (!t || !gmap::mapFromShareText(t, m)) { toast("The clipboard has no map code."); return false; }
    meReplaceMap(m);
    toast("Map pasted from a code.");
    return true;
}
bool meCanVertex() { return ME.sel.size() == 1 && ME.ent < 0; }
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
    if (const char* e = std::getenv("GUNSMITH_WALKAT"); e && std::string(e) == "counter") {   // (screenshots) behind the counter, looking down at it
        const gmap::Entity *c = nullptr, *d = nullptr;
        for (auto& en : ME.map.ents) { if (en.type == "shop_counter") c = &en; if (en.type == "shop_door") d = &en; }
        if (c) {
            double a = c->yaw * PI / 180; gmap::V3 f{std::cos(a), 0, std::sin(a)};
            if (d && (d->pos.x - c->pos.x) * f.x + (d->pos.z - c->pos.z) * f.z < 0) f = f * -1.0;   // (the customers' side)
            ME.pl.feet.x = c->pos.x - f.x * 0.75; ME.pl.feet.z = c->pos.z - f.z * 0.75;
            ME.pl.yaw = std::atan2(f.z, f.x); ME.pl.pitch = -0.55;
        }
    } else if (const char* e = std::getenv("GUNSMITH_WALKAT")) { double x = 0, z = 0; if (std::sscanf(e, "%lf,%lf", &x, &z) == 2) { ME.pl.feet.x = x; ME.pl.feet.z = z; } }   // (screenshots) where you stand
    ME.walking = true;
    ME.version++;   // (rebuild: on foot, hidden brushes are there)
    DisableCursor();
}
static void stopWalking() {
    ME.walking = false;
    ME.version++;
    EnableCursor();
}
void meStopWalking() { stopWalking(); }

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
    if (ME.map.build.on()) {   // a property: from out front and above, looking in
        const gmap::Zone& z = ME.map.build;
        double cx = (z.x0 + z.x1) / 2, cz = (z.z0 + z.z1) / 2, span = std::max(z.x1 - z.x0, z.z1 - z.z0);
        ME.cam = {z.x1 + span * 0.55, std::max(ME.map.roofY, 2.5) + span * 0.55, cz - span * 0.45};
        double dx = cx - ME.cam.x, dy = 0 - ME.cam.y, dz = cz - ME.cam.z;
        ME.yaw = std::atan2(dz, dx); ME.pitch = -std::atan2(-dy, std::hypot(dx, dz));
    }
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
enum class Drag { None, Move, Face, Create, Gizmo, Vertex, Extrude };
Drag drag = Drag::None;
bool dragChanged = false, dragVertical = false;
V3 dragHit, dragPlaneN;
double dragPlaneD = 0;
std::vector<gmap::Brush> dragOrig;
std::vector<gmap::Entity> dragEnts;     // the picked things' starting places (with their numbers in dragEntIdx)
std::vector<int> dragEntIdx;
int dragFace = -1;
Vector2 dragMouse0;
V3 createA, createB;
double createY = 0;
std::string preDrag;
gmap::Stance wantStance = gmap::Stance::Stand;
// the handles on a selection (move arrows, turning rings, stretch squares)
int gizAxis = -1;
XMode gizMode = XMode::Move;
V3 gizC, gizPivot, gizMn, gizMx;
double gizT0 = 0;
Vector2 gizPress{};
gmap::Selection gizSel;
std::vector<gmap::Brush> gizBrushes;
std::vector<gmap::Entity> gizEnts;
Rings gizRings;
// corners
gmap::Brush vertOrig;
V3 vertBase;
int extrudeIdx = -1;
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

// ---------------------------------------------------------------- handles, snapping and picking helpers
static V3 axisVec(int a) { return a == 0 ? V3{1, 0, 0} : a == 1 ? V3{0, 1, 0} : V3{0, 0, 1}; }
static Color axisColour(int a, bool hot = false) {
    Color c = a == 0 ? Color{236, 84, 84, 255} : a == 1 ? Color{112, 214, 112, 255} : Color{96, 146, 244, 255};
    return hot ? Color{255, 226, 120, 255} : c;
}
static ToScreen toScreenFn() { return [](Vector3 v) { Vector2 s; return project({v.x, v.y, v.z}, s) ? s : Vector2{-9999, -9999}; }; }
static double pxPerMetre(V3 at) {
    Vector2 a, b;
    CamB c = camBasis();
    if (!project(at, a) || !project(at + c.right, b)) return 0;
    return Vector2Distance(a, b);
}
static double snapRadius(V3 at) {   // about 14 pixels, in metres
    double px = pxPerMetre(at);
    return px > 0 ? std::clamp(14.0 / px, 0.04, 1.5) : 0.3;
}
static double gizLen(V3 c) { double px = pxPerMetre(c); return px > 0 ? 90.0 / px : 1.0; }
static double segDist(Vector2 p, Vector2 a, Vector2 b) {
    Vector2 ab = Vector2Subtract(b, a);
    float l2 = Vector2LengthSqr(ab);
    float t = l2 > 0 ? std::clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / l2, 0.f, 1.f) : 0.f;
    return Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, t)));
}
// where along the line p0 + t*a the mouse ray comes closest (false when the line points straight along the view)
static bool axisParam(V3 o, V3 d, V3 p0, V3 a, double& t) {
    V3 w0 = p0 - o;
    double b = dot(a, d), c = dot(d, d), dd = dot(a, w0), e = dot(d, w0);
    double den = c - b * b;
    if (std::fabs(den) < 1e-6 * c) return false;
    t = (b * e - c * dd) / den;
    return true;
}
// a ray against the brushes you can pick (not hidden, and not frozen unless `frozenToo`)
static gmap::Hit edRay(V3 o, V3 d, bool pickable) {
    gmap::Hit best;
    best.brush = -1; best.t = 1e30;
    for (size_t i = 0; i < ME.map.brushes.size(); i++) {
        const auto& b = ME.map.brushes[i];
        if (b.hidden || (pickable && (b.frozen || b.locked))) continue;
        gmap::Hit h = gmap::rayBrush(b, o, d, 5000);
        if (h.brush >= 0 && h.t < best.t) { best = h; best.brush = (int)i; }
    }
    return best;
}
static void normalizeSelection() {
    auto okB = [](int i) { return i >= 0 && i < (int)ME.map.brushes.size(); };
    ME.sel.erase(std::remove_if(ME.sel.begin(), ME.sel.end(), [&](int i) { return !okB(i); }), ME.sel.end());
    if (ME.ent >= (int)ME.map.ents.size()) ME.ent = -1;
    ME.entSel.erase(std::remove_if(ME.entSel.begin(), ME.entSel.end(), [&](int i) { return i < 0 || i >= (int)ME.map.ents.size(); }), ME.entSel.end());
    if (ME.ent < 0) ME.entSel.clear();
    else if (std::find(ME.entSel.begin(), ME.entSel.end(), ME.ent) == ME.entSel.end()) ME.entSel = {ME.ent};
    if (ME.vertexMode && !meCanVertex()) { ME.vertexMode = false; ME.vertSel.clear(); }
}
// pick a brush, and everything grouped with it; `toggle` adds it to (or takes it from) what's picked
static void pickBrush(int bi, bool toggle) {
    gmap::Selection s = toggle ? meSelection() : gmap::Selection{};
    const auto& b = ME.map.brushes[bi];
    std::vector<int> bs, es;
    if (b.group) {
        for (size_t i = 0; i < ME.map.brushes.size(); i++) if (ME.map.brushes[i].group == b.group && !ME.map.brushes[i].hidden && !ME.map.brushes[i].frozen) bs.push_back((int)i);
        for (size_t i = 0; i < ME.map.ents.size(); i++) if (ME.map.ents[i].group == b.group && !ME.map.ents[i].hidden && !ME.map.ents[i].frozen) es.push_back((int)i);
    } else bs.push_back(bi);
    bool had = std::find(s.brushes.begin(), s.brushes.end(), bi) != s.brushes.end();
    for (int i : bs) {
        auto it = std::find(s.brushes.begin(), s.brushes.end(), i);
        if (toggle && had) { if (it != s.brushes.end()) s.brushes.erase(it); }
        else if (it == s.brushes.end()) s.brushes.push_back(i);
    }
    for (int i : es) if (std::find(s.ents.begin(), s.ents.end(), i) == s.ents.end()) s.ents.push_back(i);
    meSetSelection(s);
}
static void pickEnt(int ei, bool toggle) {
    gmap::Selection s = toggle ? meSelection() : gmap::Selection{};
    const auto& e = ME.map.ents[ei];
    std::vector<int> bs, es;
    if (e.group) {
        for (size_t i = 0; i < ME.map.brushes.size(); i++) if (ME.map.brushes[i].group == e.group && !ME.map.brushes[i].hidden && !ME.map.brushes[i].frozen) bs.push_back((int)i);
        for (size_t i = 0; i < ME.map.ents.size(); i++) if (ME.map.ents[i].group == e.group && !ME.map.ents[i].hidden && !ME.map.ents[i].frozen) es.push_back((int)i);
    } else es.push_back(ei);
    bool had = std::find(s.ents.begin(), s.ents.end(), ei) != s.ents.end();
    for (int i : es) {
        auto it = std::find(s.ents.begin(), s.ents.end(), i);
        if (toggle && had) { if (it != s.ents.end()) s.ents.erase(it); }
        else if (it == s.ents.end()) s.ents.push_back(i);
    }
    for (int i : bs) if (std::find(s.brushes.begin(), s.brushes.end(), i) == s.brushes.end()) s.brushes.push_back(i);
    meSetSelection(s);
}
static V3 snapOnGround(V3 p, double y, bool alt) {   // a point on the ground plane: onto another brush's corner or the grid
    if (ME.snapObjects && !alt) {
        gmap::SnapHit h = gmap::snapPoint(ME.map, {}, {p.x, y, p.z}, snapRadius({p.x, y, p.z}), {1, 0, 1});
        if (h.hit) { ME.snapShown = true; ME.snapFrom = {p.x, y, p.z}; ME.snapTo = h.to; return {h.to.x, y, h.to.z}; }
    }
    return {snapG(p.x), y, snapG(p.z)};
}
static Rings makeRings(V3 c) {
    Rings g;
    g.c = rv(c);
    g.ax[0] = {1, 0, 0}; g.ax[1] = {0, 1, 0}; g.ax[2] = {0, 0, 1};
    g.label[0] = "X"; g.label[1] = "Y"; g.label[2] = "Z";
    ringsFit(g, toScreenFn(), 78);
    return g;
}
// the handle under the mouse for the selection: 0..2 an arrow (or stretch square), 3 the middle (stretch all), 10..12 a ring; -1 none
static int gizmoPick(Vector2 m) {
    gmap::Selection s = meSelection();
    if (s.empty()) return -1;
    V3 c = gmap::selectionCentre(ME.map, s);
    if (ME.xmode == XMode::Rotate) { int r = ringsPick(makeRings(c), toScreenFn(), m); return r >= 0 ? 10 + r : -1; }
    Vector2 sc;
    if (!project(c, sc)) return -1;
    double L = gizLen(c);
    int best = -1;
    double bd = 9;
    for (int a = 0; a < 3; a++) {
        Vector2 st;
        if (!project(c + axisVec(a) * L, st)) continue;
        double d = segDist(m, sc, st);
        if (d < bd) { bd = d; best = a; }
    }
    if (ME.xmode == XMode::Scale && Vector2Distance(m, sc) < 11) return 3;
    return best;
}
static void startGizmo(int ga, V3 o, V3 d) {
    gmap::Selection s = meSelection();
    gizSel = s;
    gizMode = ME.xmode;
    gizAxis = ga >= 10 ? ga - 10 : ga;
    gizBrushes.clear(); gizEnts.clear();
    for (int i : s.brushes) gizBrushes.push_back(ME.map.brushes[i]);
    for (int i : s.ents) gizEnts.push_back(ME.map.ents[i]);
    gmap::selectionBounds(ME.map, s, gizMn, gizMx);
    gizC = (gizMn + gizMx) * 0.5;
    gizPress = GetMousePosition();
    beginDrag(Drag::Gizmo);
    gizPivot = gizC;
    if (gizMode == XMode::Rotate) { gizRings = makeRings(gizC); ringsBegin(gizRings, gizAxis, toScreenFn(), gizPress); }
    else if (gizMode == XMode::Move) { if (!axisParam(o, d, gizC, axisVec(gizAxis), gizT0)) gizT0 = 0; }
    else if (gizAxis < 3) {   // stretching along an axis: the far side stays put (Ctrl: the middle does)
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (!ctrl) { V3 mn = gizMn; double lo = gizAxis == 0 ? mn.x : gizAxis == 1 ? mn.y : mn.z; if (gizAxis == 0) gizPivot.x = lo; else if (gizAxis == 1) gizPivot.y = lo; else gizPivot.z = lo; }
        if (!axisParam(o, d, gizPivot, axisVec(gizAxis), gizT0) || std::fabs(gizT0) < 1e-4) gizT0 = 1;
    }
}
static void startBodyDrag(V3 o, V3 d, V3 hit, bool alt) {
    beginDrag(Drag::Move);
    dragHit = hit; dragVertical = alt;
    gmap::Selection s = meSelection();
    dragOrig.clear(); dragEnts.clear(); dragEntIdx = s.ents;
    for (int i : s.brushes) dragOrig.push_back(ME.map.brushes[i]);
    for (int i : s.ents) dragEnts.push_back(ME.map.ents[i]);
    (void)o; (void)d;
}
// the corners and edge middles of the one picked brush: which is under the mouse (corner: index; edge: 1000 + index)
static int vertexHandleAt(Vector2 m, const gmap::Brush& b, std::vector<V3>& verts, std::vector<std::pair<int, int>>& edges) {
    verts = gmap::brushVertices(b);
    edges = gmap::brushEdges(b, verts);
    int best = -1;
    double bd = 9;
    for (size_t i = 0; i < verts.size(); i++) { Vector2 s; if (project(verts[i], s) && Vector2Distance(s, m) < bd) { bd = Vector2Distance(s, m); best = (int)i; } }
    if (best >= 0) return best;
    bd = 8;
    for (size_t i = 0; i < edges.size(); i++) {
        Vector2 s;
        if (project((verts[edges[i].first] + verts[edges[i].second]) * 0.5, s) && Vector2Distance(s, m) < bd) { bd = Vector2Distance(s, m); best = 1000 + (int)i; }
    }
    return best;
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
    gmap::Hit h = edRay(o, d, false);   // (frozen brushes are still surfaces to build on; hidden ones aren't there)
    if (h.brush >= 0) { p = o + d * h.t; n = h.n; brush = h.brush; face = h.face; return true; }
    brush = face = -1; n = {0, 1, 0};
    return rayPlaneY(o, d, 0, p);
}

static int entityUnderMouse(double* tOut) {
    V3 o, d; mouseRay(o, d);
    int best = -1; double bt = 1e30;
    for (size_t i = 0; i < ME.map.ents.size(); i++) {
        if (ME.map.ents[i].hidden || ME.map.ents[i].frozen) continue;
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
            gmap::Selection cs = meSelection();
            // the handles on what's picked: arrows to move along an axis, rings to turn, squares to stretch
            if (!ME.vertexMode && !cs.empty()) { int ga = gizmoPick(m); if (ga >= 0) { startGizmo(ga, o, d); break; } }
            // corner editing: a corner or an edge of the one picked brush
            if (ME.vertexMode && meCanVertex()) {
                std::vector<V3> verts; std::vector<std::pair<int, int>> edges;
                int h = vertexHandleAt(m, ME.map.brushes[ME.sel[0]], verts, edges);
                if (h >= 0) {
                    std::vector<int> pick;
                    if (h >= 1000) pick = {edges[h - 1000].first, edges[h - 1000].second};
                    else {
                        pick = ME.vertSel;
                        auto it = std::find(pick.begin(), pick.end(), h);
                        if (shift) { if (it != pick.end()) pick.erase(it); else pick.push_back(h); }
                        else if (it == pick.end()) pick = {h};
                    }
                    ME.vertSel = pick;
                    vertOrig = ME.map.brushes[ME.sel[0]];
                    vertBase = {0, 0, 0};
                    for (int i : pick) vertBase += verts[i];
                    vertBase = vertBase * (1.0 / std::max<size_t>(1, pick.size()));
                    beginDrag(Drag::Vertex);
                    dragHit = vertBase; dragVertical = alt;
                    break;
                }
            }
            // the first click on a face's square picks it; press on it again to drag it (Shift: pull a new piece out of it)
            static int pickedFace = -1;
            int fh = ME.vertexMode ? -1 : faceHandleAt(m);
            if (fh >= 0 && fh != pickedFace && !shift) { pickedFace = fh; ME.status = "Face picked: drag it to resize. Shift+drag pulls a new piece out of it."; break; }
            if (fh < 0) pickedFace = -1;
            if (fh >= 0) {
                beginDrag(shift ? Drag::Extrude : Drag::Face);
                dragFace = fh; dragOrig = {ME.map.brushes[ME.sel[0]]}; extrudeIdx = -1;
                break;
            }
            double te; int e = entityUnderMouse(&te);
            gmap::Hit h = edRay(o, d, true);
            if (e >= 0 && (h.brush < 0 || te < h.t)) {
                bool inSel = std::find(cs.ents.begin(), cs.ents.end(), e) != cs.ents.end();
                if (shift) { pickEnt(e, true); break; }
                if (!inSel) { pickEnt(e, false); break; }
                startBodyDrag(o, d, o + d * te, alt);
                break;
            }
            if (h.brush < 0) { if (!shift) { meSetSelection({}); ME.vertSel.clear(); } break; }
            if (ctrl) { ME.sel = {h.brush}; ME.entSel.clear(); ME.ent = -1; ME.face = h.face; ME.vertSel.clear(); ME.mat = ME.map.brushes[h.brush].faces[h.face].mat; break; }
            if (shift) { pickBrush(h.brush, true); break; }
            if (!isSel(h.brush)) { pickBrush(h.brush, false); break; }
            ME.face = -1;
            startBodyDrag(o, d, o + d * h.t, alt);
            break;
        }
        case MTool::Box: case MTool::Wedge: case MTool::Cylinder: case MTool::Stairs:
        case MTool::Cone: case MTool::Sphere: case MTool::Round: case MTool::Arch: case MTool::Pipe: {
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            createY = (b >= 0 && n.y > 0.7) ? p.y : 0;
            if (b >= 0 && n.y > 0.7) createY = std::round(createY / 0.001) * 0.001;
            ME.snapShown = false;
            createA = createB = snapOnGround(p, createY, alt);
            beginDrag(Drag::Create);
            break;
        }
        case MTool::Prop: {   // furniture and clutter, made of brushes: click where it stands, facing you
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            const auto& kinds = gmap::propKinds();
            std::string kind = kinds[std::clamp(ME.propKind, 0, (int)kinds.size() - 1)];
            double yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 90) * 90 + 180 + 720, 360);
            V3 base{snapG(p.x), n.y > 0.7 ? p.y : 0, snapG(p.z)};
            meSnapshot();
            auto made = gmap::makeProp(kind, base, yaw);
            gmap::Selection s;
            for (auto& br : made) { ME.map.brushes.push_back(br); s.brushes.push_back((int)ME.map.brushes.size() - 1); }
            int gid = 0;
            for (auto& br : ME.map.brushes) gid = std::max(gid, br.group);
            for (auto& e : ME.map.ents) gid = std::max(gid, e.group);
            for (int i : s.brushes) { ME.map.brushes[i].group = gid + 1; ME.map.brushes[i].name = kind; }
            meSetSelection(s);
            meChanged();
            break;
        }
        case MTool::Measure: {   // two points: how far, and how steep
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            V3 q = snap3(p);
            if (ME.snapObjects && !alt) { gmap::SnapHit h = gmap::snapPoint(ME.map, {}, p, snapRadius(p)); if (h.hit) q = h.to; }
            if (ME.measurePts.size() >= 2) ME.measurePts.clear();
            ME.measurePts.push_back(q);
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
            if (ME.stationKind.rfind("display_", 0) == 0 || ME.stationKind.rfind("decor_", 0) == 0) e.type = ME.stationKind;
            else e.type = (ME.stationKind == "counter" || ME.stationKind == "computer" || ME.stationKind == "door" ? "shop_" : "station_") + ME.stationKind;
            e.pos = {snapG(p.x), n.y > 0.7 ? p.y : 0, snapG(p.z)};
            e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);
            ME.map.ents.push_back(e);
            meSetSelection({{}, {(int)ME.map.ents.size() - 1}});
            meChanged();
            break;
        }
        case MTool::Door: {   // a door: in a doorway on the floor, across the way you're looking
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            meSnapshot();
            gmap::Entity e;
            e.type = "door"; e.ref = ME.doorStyle; e.width = ME.doorWidth;
            e.pos = {snapG(p.x), n.y > 0.7 ? p.y : 0, snapG(p.z)};
            e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 90) * 90 + 720, 360);
            ME.map.ents.push_back(e);
            meSetSelection({{}, {(int)ME.map.ents.size() - 1}});
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
            meSetSelection({{}, {(int)ME.map.ents.size() - 1}});
            meChanged();
            break;
        }
        case MTool::Target: {   // a paper target: standing over the floor clicked, or flat on a wall, facing you
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            meSnapshot();
            gmap::Entity e; e.type = "target_paper";
            if (n.y > 0.7) {
                e.pos = {snapG(p.x), p.y + 1.45, snapG(p.z)};
                e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);
            } else {
                e.pos = p + n * 0.01;
                e.yaw = std::atan2(n.z, n.x) * RAD2DEG;
            }
            e.width = 3; e.range = 1.2;
            ME.map.ents.push_back(e);
            meSetSelection({{}, {(int)ME.map.ents.size() - 1}});
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
            meSetSelection({{}, {(int)ME.map.ents.size() - 1}});
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
            meSetSelection({{}, {e}});
            meChanged();
            break;
        }
        }
    }

    if (drag != Drag::None && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        bool moved = Vector2Distance(m, dragMouse0) > 5;
        ME.snapShown = false;
        if (drag == Drag::Move) {
            V3 p;
            bool ok;
            if (dragVertical && !ME.top) {
                CamB c = camBasis();
                V3 n = norm({c.fwd.x, 0, c.fwd.z});
                ok = rayPlane(o, d, n, dot(n, dragHit), p);
                if (ok) p = {dragHit.x, p.y, dragHit.z};
            } else ok = ME.top ? (p = o + d * ((o.y - dragHit.y) / -d.y), true) : rayPlaneY(o, d, dragHit.y, p);
            if (ok && moved) {
                gmap::Selection s;
                s.ents = dragEntIdx;
                for (size_t k = 0; k < ME.sel.size() && k < dragOrig.size(); k++) { s.brushes.push_back(ME.sel[k]); ME.map.brushes[ME.sel[k]] = dragOrig[k]; }
                for (size_t k = 0; k < dragEntIdx.size() && k < dragEnts.size(); k++) ME.map.ents[dragEntIdx[k]] = dragEnts[k];
                V3 dl = p - dragHit;
                dl = {snapG(dl.x), snapG(dl.y), snapG(dl.z)};
                if (ME.snapObjects && !alt) {   // land a corner, edge middle or face middle on something else
                    V3 mask = (dragVertical && !ME.top) ? V3{0, 1, 0} : V3{1, 0, 1};
                    V3 c = gmap::selectionCentre(ME.map, s);
                    gmap::SnapHit h = gmap::snapMove(ME.map, s, dl, snapRadius(c), mask);
                    if (h.hit) { dl = h.delta; ME.snapShown = true; ME.snapFrom = h.from; ME.snapTo = h.to; }
                }
                gmap::moveSelection(ME.map, s, dl);
                if (gmap::len(dl) > 0) { dragChanged = true; ME.version++; }
            }
        } else if (drag == Drag::Gizmo) {
            for (size_t k = 0; k < gizSel.brushes.size(); k++) ME.map.brushes[gizSel.brushes[k]] = gizBrushes[k];
            for (size_t k = 0; k < gizSel.ents.size(); k++) ME.map.ents[gizSel.ents[k]] = gizEnts[k];
            V3 ax = axisVec(std::max(gizAxis, 0));
            if (gizMode == XMode::Move) {
                double t;
                if (axisParam(o, d, gizC, ax, t)) {
                    V3 dl = ax * (t - gizT0);
                    dl = {snapG(dl.x), snapG(dl.y), snapG(dl.z)};
                    if (ME.snapObjects && !alt) {
                        gmap::SnapHit h = gmap::snapMove(ME.map, gizSel, dl, snapRadius(gizC), V3{gizAxis == 0 ? 1.0 : 0.0, gizAxis == 1 ? 1.0 : 0.0, gizAxis == 2 ? 1.0 : 0.0});
                        if (h.hit) { dl = h.delta; ME.snapShown = true; ME.snapFrom = h.from; ME.snapTo = h.to; }
                    }
                    gmap::moveSelection(ME.map, gizSel, dl);
                    dragChanged = gmap::len(dl) > 0;
                }
            } else if (gizMode == XMode::Rotate) {
                double deg = ringsDrag(gizRings, m, !alt);   // 15 degree steps (Alt: free)
                gmap::rotateSelection(ME.map, gizSel, gizAxis, deg * DEG2RAD, gizC);
                dragChanged = std::fabs(deg) > 1e-6;
            } else {
                double s = 1;
                V3 f{1, 1, 1};
                if (gizAxis == 3) {
                    Vector2 sc;
                    if (project(gizC, sc)) { float d0 = std::max(10.f, Vector2Distance(gizPress, sc)); s = Vector2Distance(m, sc) / d0; }
                    s = std::clamp(s, 0.05, 40.0);
                    if (ME.grid > 0) { V3 sz = gizMx - gizMn; double big = std::max({sz.x, sz.y, sz.z}); if (big > 1e-6) s = snapG(big * s) / big; }
                    f = {s, s, s};
                } else {
                    double t;
                    if (axisParam(o, d, gizPivot, ax, t)) {
                        s = std::clamp(t / gizT0, 0.02, 80.0);
                        V3 sz = gizMx - gizMn;
                        double size0 = gizAxis == 0 ? sz.x : gizAxis == 1 ? sz.y : sz.z;
                        if (size0 > 1e-6 && ME.grid > 0) s = std::max(ME.grid, snapG(size0 * s)) / size0;
                        (gizAxis == 0 ? f.x : gizAxis == 1 ? f.y : f.z) = s;
                    }
                }
                if (f.x > 0 && f.y > 0 && f.z > 0) gmap::scaleSelection(ME.map, gizSel, f, gizPivot);
                dragChanged = std::fabs(s - 1) > 1e-6;
            }
            ME.version++;
        } else if (drag == Drag::Vertex) {
            V3 p;
            bool ok;
            if (dragVertical && !ME.top) {
                CamB c = camBasis();
                V3 n = norm({c.fwd.x, 0, c.fwd.z});
                ok = rayPlane(o, d, n, dot(n, dragHit), p);
                if (ok) p = {dragHit.x, p.y, dragHit.z};
            } else if (ME.top) ok = rayPlaneY(o, d, dragHit.y, p);
            else { CamB c = camBasis(); ok = rayPlane(o, d, c.fwd * -1.0, dot(c.fwd * -1.0, dragHit), p); }
            if (ok && moved && !ME.sel.empty()) {
                V3 target = p;
                if (ME.snapObjects && !alt) {
                    gmap::Selection skip{{ME.sel[0]}, {}};
                    gmap::SnapHit h = gmap::snapPoint(ME.map, skip, p, snapRadius(p));
                    if (h.hit) { target = h.to; ME.snapShown = true; ME.snapFrom = p; ME.snapTo = h.to; }
                    else target = snap3(p);
                } else if (!alt) target = snap3(p);
                gmap::Brush nb;
                if (gmap::moveVertices(vertOrig, ME.vertSel, target - vertBase, nb)) { ME.map.brushes[ME.sel[0]] = nb; dragChanged = true; ME.version++; }
            }
        } else if ((drag == Drag::Face || drag == Drag::Extrude) && !dragOrig.empty()) {
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
                    if (drag == Drag::Face) {
                        if (gmap::pushFace(b, dragFace, amt)) { ME.map.brushes[ME.sel[0]] = b; ME.version++; dragChanged = amt != 0; }
                    } else {   // a new brush pulled out of the face
                        gmap::Brush nb;
                        if (amt > 1e-6 && gmap::extrudeFace(dragOrig[0], dragFace, amt, nb)) {
                            if (extrudeIdx < 0) { ME.map.brushes.push_back(nb); extrudeIdx = (int)ME.map.brushes.size() - 1; }
                            else ME.map.brushes[extrudeIdx] = nb;
                            ME.version++; dragChanged = true;
                        } else if (extrudeIdx >= 0) { ME.map.brushes.erase(ME.map.brushes.begin() + extrudeIdx); extrudeIdx = -1; ME.version++; dragChanged = false; }
                    }
                }
            }
        } else if (drag == Drag::Create) {
            V3 p;
            bool ok = rayPlaneY(o, d, createY, p);
            if (ok) createB = snapOnGround(p, createY, alt);
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
                else if (ME.tool == MTool::Cone) made.push_back(gmap::makeCone(mn, mx, ME.shapeSides, ME.mat));
                else if (ME.tool == MTool::Sphere) made.push_back(gmap::makeSphere(mn, mx, std::max(3, ME.shapeSides / 2), ME.shapeSides, ME.mat));
                else if (ME.tool == MTool::Round) made.push_back(gmap::makeQuarterRound(mn, mx, ME.shapeSides, wedgeDir(), ME.mat));
                else if (ME.tool == MTool::Arch) made = gmap::makeArch(mn, mx, ME.archSegs, ME.archWall, wedgeDir(), ME.mat);
                else if (ME.tool == MTool::Pipe) made = gmap::makePipe(mn, mx, std::max(6, ME.shapeSides), ME.pipeWall, ME.mat);
                else made = gmap::makeStairs(mn, mx, ME.stairSteps, wedgeDir(), ME.mat);
                ME.sel.clear(); ME.entSel.clear();
                for (auto& b : made) if (gmap::brushValid(b)) { ME.map.brushes.push_back(b); ME.sel.push_back((int)ME.map.brushes.size() - 1); }
                if (ME.sel.size() > 1) {   // an arch or a pipe is one thing to pick
                    int gid = 0;
                    for (auto& br : ME.map.brushes) gid = std::max(gid, br.group);
                    for (auto& e : ME.map.ents) gid = std::max(gid, e.group);
                    for (int i : ME.sel) ME.map.brushes[i].group = gid + 1;
                }
                ME.face = -1; ME.ent = -1;
                dragChanged = !made.empty();
            }
        } else if (drag == Drag::Extrude && extrudeIdx >= 0) {
            ME.sel = {extrudeIdx}; ME.face = -1; ME.entSel.clear(); ME.ent = -1;   // the new piece is what's picked now
        } else if (drag == Drag::Vertex) {
            ME.vertSel.clear();   // (the corners are numbered again after the change)
        }
        ME.snapShown = false;
        extrudeIdx = -1;
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
    if (ctrl && IsKeyPressed(KEY_C)) meCopy(false);
    if (ctrl && IsKeyPressed(KEY_X)) meCopy(true);
    if (ctrl && IsKeyPressed(KEY_V)) mePaste();
    if (ctrl && IsKeyPressed(KEY_A)) meSelectAll();
    if (ctrl && IsKeyPressed(KEY_G)) meGroup(!shift);
    if (ctrl && IsKeyPressed(KEY_H)) meHide(!shift);
    if (ctrl && IsKeyPressed(KEY_L)) meFreeze(!shift);
    if (!ctrl && !shift && IsKeyPressed(KEY_ONE)) ME.xmode = XMode::Move;
    if (!ctrl && !shift && IsKeyPressed(KEY_TWO)) ME.xmode = XMode::Rotate;
    if (!ctrl && !shift && IsKeyPressed(KEY_THREE)) ME.xmode = XMode::Scale;
    if (!ctrl && IsKeyPressed(KEY_C)) { if (meCanVertex()) { ME.vertexMode = !ME.vertexMode; ME.vertSel.clear(); } else toast("Pick one brush to edit its corners."); }
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
    if (gmap::len(nud) > 0 && !meSelection().empty()) {
        meSnapshot();
        gmap::moveSelection(ME.map, meSelection(), nud);
        meChanged();
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (drag != Drag::None) { restore(preDrag); drag = Drag::None; ME.version++; }
        else if (!ME.clipPts.empty()) ME.clipPts.clear();
        else if (!ME.measurePts.empty()) ME.measurePts.clear();
        else if (ME.vertexMode) { ME.vertexMode = false; ME.vertSel.clear(); }
        else if (ME.tool != MTool::Select) ME.tool = MTool::Select;
        else if (!meSelection().empty()) meSetSelection({});
        else A.showPause = true;
    }
}

// ---------------------------------------------------------------- walking
static void walkUpdate(float dt) {
    if (shopActive() && shopWalkUpdate(dt)) return;   // (your shop in career mode: F uses things, B builds, Esc is the menu)
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
    doors::step(ME.map, ME.pl, dt);
    if (actPressed(Act::Use)) { int d = doors::facing(ME.map, gmap::playerEye(ME.pl, ME.map), gmap::playerForward(ME.pl)); if (d >= 0) doors::toggle(ME.map, d, ME.pl.feet); }
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
static void drawPaperTargetMarker(const gmap::Entity& e, bool sel) {   // a paper target: the sheet, and a moving one's track
    Matrix R = MatrixRotateY(-(float)e.yaw * DEG2RAD);
    Vector3 c = rv(e.pos);
    drawBox(c, {0.01f, 0.6f, 0.6f}, sel ? Color{255, 205, 60, 255} : Color{238, 234, 222, 255}, R);
    drawBox(Vector3Add(c, Vector3Transform({-0.008f, 0, 0}, R)), {0.004f, 0.36f, 0.36f}, Color{30, 30, 32, 255}, R);
    drawBox(Vector3Add(c, Vector3Transform({-0.012f, 0, 0}, R)), {0.004f, 0.08f, 0.08f}, Color{200, 60, 40, 255}, R);
    if (e.ref == "moving") {
        Vector3 side = Vector3Transform({0, 0, 1}, R);
        float h = (float)e.width / 2;
        Vector3 a = Vector3Add(c, Vector3Scale(side, -h)), b = Vector3Add(c, Vector3Scale(side, h));
        DrawLine3D(a, b, Color{255, 205, 60, 255});
        drawSphereLit(a, 0.04f, Color{255, 205, 60, 255}); drawSphereLit(b, 0.04f, Color{255, 205, 60, 255});
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
    "Click a brush to select (Shift adds, Ctrl picks one face). Drag to move. Arrows, rings and squares turn and stretch it (keys 1 move, 2 turn, 3 stretch). C edits corners. Right mouse: look and fly with WASD.",
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
    "Click a ceiling, a wall or the floor to hang a lamp there. Pick it to change its colour, power and reach.",
    "Click to place an ammo box: press F by it on foot to fill up the ammunition for every gun you carry.",
    "Click to place a weapon crate: press F by it on foot to take any of your guns.",
    "Click to place a workstation (career mode).",
    "Click a floor in a doorway to place a door. It swings away from you on foot.",
    "Click the floor (a target at chest height) or a wall to place a paper target.",
    "Drag to draw a cone, or a pyramid with 4 sides. Its sides are in the panel.",
    "Drag to draw a ball (or an egg) that fills the box.",
    "Drag to draw a rounded corner: a quarter of a cylinder. It curves away from where you're looking.",
    "Drag to draw an arch (a doorway with a curved top): two legs and a curve of blocks, picked together.",
    "Drag to draw a pipe: an upright tube with a hole through it.",
    "Click to place furniture or clutter (chosen in the panel), made of brushes so it blocks like a wall. It faces you.",
    "Click two points to measure between them: distance, run, rise and slope. They snap to corners.",
};
static const char* hintFor(MTool t) { size_t i = (size_t)t; return i < sizeof HINT / sizeof HINT[0] ? HINT[i] : ""; }

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
            else if (ME.tool == MTool::Cone) pv.push_back(gmap::makeCone(mn, mx, ME.shapeSides, 0));
            else if (ME.tool == MTool::Sphere) pv.push_back(gmap::makeSphere(mn, mx, std::max(3, ME.shapeSides / 2), ME.shapeSides, 0));
            else if (ME.tool == MTool::Round) pv.push_back(gmap::makeQuarterRound(mn, mx, ME.shapeSides, wedgeDir(), 0));
            else if (ME.tool == MTool::Arch) pv = gmap::makeArch(mn, mx, ME.archSegs, ME.archWall, wedgeDir(), 0);
            else if (ME.tool == MTool::Pipe) pv = gmap::makePipe(mn, mx, std::max(6, ME.shapeSides), ME.pipeWall, 0);
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
        if (e.hidden) continue;
        bool selE = std::find(ME.entSel.begin(), ME.entSel.end(), (int)i) != ME.entSel.end() || (int)i == ME.ent;
        if (e.type == "player_start") drawStart(e, selE);
        else if (e.type == "npc_spawn") drawNpcSpawn(e, selE);
        else if (e.type == "gun_spawner") drawSpawner(e, selE);
        else if (e.type == "light") drawLampMarker(e, selE);
        else if (e.type.rfind("station_", 0) == 0 || e.type.rfind("shop_", 0) == 0 || gmap::isDisplay(e.type) || gmap::isDecor(e.type)) drawStation(e.type, rv(e.pos), (float)e.yaw, selE ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "ammo_box") drawAmmoBox(rv(e.pos), (float)e.yaw, selE ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "target_paper") drawPaperTargetMarker(e, selE);
        else if (e.type == "weapon_crate") drawWeaponCrate(rv(e.pos), (float)e.yaw, selE ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "attachment_crate") drawAttachmentCrate(rv(e.pos), (float)e.yaw, selE ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "armor_plate") for (int k = 0; k < std::max(1, e.count); k++)
            drawArmorPlate(rv(e.pos + V3{0, 0.03 * k, 0}), (float)(e.yaw + 9 * k), selE ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
    }
    (void)b;
}

static std::string selectionInfo() {
    gmap::Selection s = meSelection();
    if (s.empty()) return "";
    V3 mn, mx; gmap::selectionBounds(ME.map, s, mn, mx);
    V3 sz = mx - mn;
    std::string t;
    if (!s.brushes.empty()) t += std::to_string(s.brushes.size()) + (s.brushes.size() == 1 ? " brush" : " brushes");
    if (!s.ents.empty()) t += std::string(t.empty() ? "" : ", ") + std::to_string(s.ents.size()) + (s.ents.size() == 1 ? " thing" : " things");
    if (!s.brushes.empty()) t += "   " + meLen(sz.x) + " x " + meLen(sz.y) + " x " + meLen(sz.z);
    return t;
}
static void drawHandlesOverlay() {
    Vector2 mouse = GetMousePosition();
    // the move arrows, turning rings and stretch squares on what's picked
    if (ME.tool == MTool::Select && !ME.vertexMode) {
        gmap::Selection s = meSelection();
        if (!s.empty()) {
            V3 c = drag == Drag::Gizmo ? gizC : gmap::selectionCentre(ME.map, s);
            Vector2 sc;
            if (project(c, sc)) {
                if (ME.xmode == XMode::Rotate) {
                    Rings g = (drag == Drag::Gizmo && gizMode == XMode::Rotate) ? gizRings : makeRings(c);
                    if (drag != Drag::Gizmo) g.hot = ringsPick(g, toScreenFn(), mouse);
                    ringsDraw(g, toScreenFn(), rv(ME.top ? V3{c.x, c.y + 100, c.z} : ME.cam));
                    if (drag == Drag::Gizmo && gizMode == XMode::Rotate) {
                        char b[32]; std::snprintf(b, sizeof b, "%.0f deg", gizRings.deg);
                        viewText(b, mouse.x + 16, mouse.y + 14, 13, Color{255, 226, 120, 255});
                    }
                } else {
                    double L = gizLen(c);
                    int hot = drag == Drag::Gizmo ? gizAxis : gizmoPick(mouse);
                    for (int a = 0; a < 3; a++) {
                        Vector2 st;
                        if (!project(c + axisVec(a) * L, st)) continue;
                        Color col = axisColour(a, hot == a);
                        DrawLineEx(sc, st, 3, col);
                        Vector2 dir = Vector2Normalize(Vector2Subtract(st, sc)), side{-dir.y, dir.x};
                        if (ME.xmode == XMode::Move) DrawTriangle(Vector2Add(st, Vector2Scale(dir, 14)), Vector2Add(st, Vector2Scale(side, 6)), Vector2Subtract(st, Vector2Scale(side, 6)), col);
                        else DrawRectangle((int)st.x - 6, (int)st.y - 6, 12, 12, col);
                    }
                    if (ME.xmode == XMode::Scale) DrawRectangle((int)sc.x - 6, (int)sc.y - 6, 12, 12, hot == 3 ? Color{255, 226, 120, 255} : Color{235, 238, 242, 255});
                    if (drag == Drag::Gizmo) {
                        V3 now = gmap::selectionCentre(ME.map, s), mn, mx;
                        gmap::selectionBounds(ME.map, s, mn, mx);
                        std::string t = gizMode == XMode::Move ? meLen(dot(now - gizC, axisVec(gizAxis))) : meLen(mx.x - mn.x) + " x " + meLen(mx.y - mn.y) + " x " + meLen(mx.z - mn.z);
                        viewText(t.c_str(), mouse.x + 16, mouse.y + 14, 13, Color{255, 226, 120, 255});
                    }
                }
            }
        }
    }
    // corner editing
    if (ME.vertexMode && meCanVertex() && ME.tool == MTool::Select) {
        std::vector<V3> verts; std::vector<std::pair<int, int>> edges;
        int hot = drag == Drag::None ? vertexHandleAt(mouse, ME.map.brushes[ME.sel[0]], verts, edges) : -1;
        if (drag != Drag::Vertex) {
            if (verts.empty()) { verts = gmap::brushVertices(ME.map.brushes[ME.sel[0]]); edges = gmap::brushEdges(ME.map.brushes[ME.sel[0]], verts); }
            for (size_t i = 0; i < edges.size(); i++) {
                Vector2 s;
                if (project((verts[edges[i].first] + verts[edges[i].second]) * 0.5, s)) DrawCircleV(s, hot == 1000 + (int)i ? 6.f : 4.f, hot == 1000 + (int)i ? Color{255, 226, 120, 255} : Color{110, 210, 150, 255});
            }
            for (size_t i = 0; i < verts.size(); i++) {
                Vector2 s;
                if (!project(verts[i], s)) continue;
                bool picked = std::find(ME.vertSel.begin(), ME.vertSel.end(), (int)i) != ME.vertSel.end();
                float r = hot == (int)i ? 7.f : 5.5f;
                DrawRectangleRec({s.x - r, s.y - r, 2 * r, 2 * r}, picked || hot == (int)i ? Color{255, 226, 120, 255} : Color{90, 150, 250, 255});
                DrawRectangleLinesEx({s.x - r, s.y - r, 2 * r, 2 * r}, 1.5f, Color{20, 24, 32, 255});
            }
        }
    }
    // where a drag landed on something else
    if (ME.snapShown) {
        Vector2 a, b;
        if (project(ME.snapTo, b)) {
            if (project(ME.snapFrom, a) && Vector2Distance(a, b) > 3) DrawLineEx(a, b, 1.5f, Color{255, 226, 120, 160});
            DrawCircleLinesV(b, 9, Color{255, 226, 120, 255});
            DrawLineEx({b.x - 13, b.y}, {b.x + 13, b.y}, 1.5f, Color{255, 226, 120, 255});
            DrawLineEx({b.x, b.y - 13}, {b.x, b.y + 13}, 1.5f, Color{255, 226, 120, 255});
        }
    }
    // measuring
    if (!ME.measurePts.empty()) {
        Vector2 a;
        if (project(ME.measurePts[0], a)) DrawCircleV(a, 5, Color{255, 255, 255, 255});
        if (ME.measurePts.size() == 2) {
            Vector2 b;
            if (project(ME.measurePts[1], b) && project(ME.measurePts[0], a)) {
                DrawLineEx(a, b, 2, Color{255, 255, 255, 230});
                DrawCircleV(b, 5, Color{255, 255, 255, 255});
                gmap::Measure ms = gmap::measure(ME.measurePts[0], ME.measurePts[1]);
                char sl[24]; std::snprintf(sl, sizeof sl, "%.1f deg", ms.slopeDegrees);
                std::string t = meLen(ms.distance) + "   run " + meLen(ms.flat) + "   rise " + meLen(ms.dy) + "   (x " + meLen(ms.dx) + ", z " + meLen(ms.dz) + ", slope " + sl + ")";
                float w = viewTextWidth(t.c_str(), 13);
                Vector2 mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
                DrawRectangleRounded({mid.x - w / 2 - 8, mid.y - 24, w + 16, 22}, 0.4f, 6, Color{20, 23, 28, 220});
                viewText(t.c_str(), mid.x - w / 2, mid.y - 20, 13, Color{255, 255, 255, 255});
            }
        }
    }
}

static void drawEditorOverlay() {
    VP v = viewRect();
    // face handles on the single selected brush
    if (ME.sel.size() == 1 && ME.face < 0 && ME.tool == MTool::Select && !ME.vertexMode) {
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
    drawHandlesOverlay();
    {   // what's picked, and how big
        std::string info = selectionInfo();
        if (!info.empty()) {
            float w = viewTextWidth(info.c_str(), 12);
            DrawRectangleRounded({v.x + 12, v.y + v.h - (CFG.hints ? 58.f : 30.f), w + 20, 22}, 0.4f, 6, Color{20, 23, 28, 170});
            viewText(info.c_str(), v.x + 22, v.y + v.h - (CFG.hints ? 53.f : 25.f), 12, Color{255, 226, 120, 255});
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
        const char* h = hintFor(ME.tool);
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
    {   // a door you're facing
        int d = doors::facing(ME.map, gmap::playerEye(ME.pl, ME.map), gmap::playerForward(ME.pl));
        if (d >= 0) {
            const char* p = TextFormat("%s  %s the door", keyName(keyOf(Act::Use)), doors::isOpen(ME.map, d) ? "Shut" : "Open");
            float pw = viewTextWidth(p, 15);
            viewText(p, cx - pw / 2, cy + 50, 15, Color{255, 214, 120, 255});
        }
    }
    const char* help = "Walking the map. Esc goes back to the editor.";
    float w = viewTextWidth(help, 13);
    DrawRectangleRounded({cx - w / 2 - 12, 14, w + 24, 24}, 0.4f, 6, Color{20, 23, 28, 150});
    viewText(help, cx - w / 2, 19, 13, Color{235, 238, 242, 230});
}

// ---------------------------------------------------------------- the frame
void mapEditorUpdate(float dt, bool modal) {
    if (careerZoned())   // (the building's own walls can't be picked: they come with the property)
        ME.sel.erase(std::remove_if(ME.sel.begin(), ME.sel.end(), [](int b) { return b < 0 || b >= (int)ME.map.brushes.size() || ME.map.brushes[b].locked; }), ME.sel.end());
    if (!modal) {
        if (ME.walking) walkUpdate(dt);
        else {
            if (shopActive() && IsKeyPressed(KEY_B) && !ImGui::GetIO().WantCaptureKeyboard) { shopWalk(); return; }   // (career: back on foot)
            flyCamera(dt); toolInput(); keyInput();
        }
    }
    normalizeSelection();
    if (ME.built != ME.version) {
        bool anyHidden = false;
        for (auto& b : ME.map.brushes) if (b.hidden) { anyHidden = true; break; }
        if (!anyHidden || ME.walking) mapBuildMeshes(ME.map);
        else { gmap::Map vis = ME.map; vis.brushes.erase(std::remove_if(vis.brushes.begin(), vis.brushes.end(), [](const gmap::Brush& b) { return b.hidden; }), vis.brushes.end()); mapBuildMeshes(vis); }
        ME.built = ME.version;
    }
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
        if (e.type.rfind("station_", 0) == 0 || e.type.rfind("shop_", 0) == 0 || gmap::isDisplay(e.type) || gmap::isDecor(e.type)) drawStation(e.type, rv(e.pos), (float)e.yaw);
        else if (e.type == "attachment_crate") drawAttachmentCrate(rv(e.pos), (float)e.yaw);
        else if (e.type == "ammo_box") drawAmmoBox(rv(e.pos), (float)e.yaw);
        else if (e.type == "weapon_crate") drawWeaponCrate(rv(e.pos), (float)e.yaw);
        else if (e.type == "armor_plate") for (int k = 0; k < std::max(1, e.count); k++) drawArmorPlate(rv(e.pos + V3{0, 0.03 * k, 0}), (float)(e.yaw + 9 * k));
    }
    doors::draw(ME.map, ME.walking ? -1 : ME.ent);
    if (shopActive()) shopDrawPeople();   // (career: customers at the counter, staff at their workstations)
    EndMode3D();
    mapEndViewport();
    mapRestoreLook();
    if (ME.walking) { if (shopActive()) shopHud(); else drawWalkHud(); }
    else drawEditorOverlay();
}
