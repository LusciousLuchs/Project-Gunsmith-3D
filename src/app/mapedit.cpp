#include <cstdlib>
// Gunsmith3D - The map editor (see mapedit.hpp).
#include "app/paths.hpp"
#include "app/mapedit.hpp"
#include "app/gizmo.hpp"
#include "app/doors.hpp"
#include "app/furnish.hpp"
#include "app/shop3d.hpp"
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/fonts.hpp"
#include "app/gfx.hpp"
#include "app/keys.hpp"
#include "app/mapgfx.hpp"
#include "app/radio.hpp"
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

const char* meUnitName() { static const char* n[] = {"m", "cm", "ft", "in", "yd"}; return n[std::clamp(CFG.mapUnit, 0, 4)]; }
double meUnitPerMetre() { static const double k[] = {1, 100, 1 / 0.3048, 1 / 0.0254, 1 / 0.9144}; return k[std::clamp(CFG.mapUnit, 0, 4)]; }
std::string meLen(double m) {
    char b[64];
    auto ftIn = [&](double metres) {   // 5 ft 3 1/4 in: feet, then inches to the quarter
        double in = std::round(std::fabs(metres) / 0.0254 * 4) / 4;
        int ft = (int)(in / 12); double rest = in - ft * 12;
        const char* sg = metres < 0 ? "-" : "";
        if (ft == 0) std::snprintf(b, sizeof b, "%s%g in", sg, rest);
        else if (rest < 1e-9) std::snprintf(b, sizeof b, "%s%d ft", sg, ft);
        else std::snprintf(b, sizeof b, "%s%d ft %g in", sg, ft, rest);
    };
    switch (std::clamp(CFG.mapUnit, 0, 4)) {
    case 1: std::snprintf(b, sizeof b, "%g cm", std::round(m * 1000) / 10); break;
    case 2: ftIn(m); break;
    case 3: std::snprintf(b, sizeof b, "%g in", std::round(m / 0.0254 * 100) / 100); break;
    case 4: if (std::fabs(m) < 0.9144) ftIn(m); else std::snprintf(b, sizeof b, "%g yd", std::round(m / 0.9144 * 100) / 100); break;
    default: std::snprintf(b, sizeof b, "%g m", std::round(m * 1000) / 1000); break;
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
// furniture's blocks are put back into the map after every change (they're its own): the selection is kept
static void rebake() {
    std::vector<int> ordOf(ME.map.brushes.size(), -1);
    int k = 0;
    for (size_t i = 0; i < ME.map.brushes.size(); i++) if (!ME.map.brushes[i].furn) ordOf[i] = k++;
    std::vector<int> ns;
    for (int i : ME.sel) if (i >= 0 && i < (int)ordOf.size() && ordOf[i] >= 0) ns.push_back(ordOf[i]);
    gmap::bakeFurniture(ME.map);
    ME.sel = ns;
    if (ME.sel.size() != 1) ME.face = -1;
}
void meRebake() { rebake(); }
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
    rebake();
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
// ---------------------------------------------------------------- furniture
bool meMakeFurniture(bool prop, const std::string& name) {
    if (ME.sel.empty()) { toast("Select the blocks it's made of first (Shift-click adds more)."); return false; }
    V3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
    std::vector<gmap::Brush> bs;
    for (int i : ME.sel) {
        if (ME.map.brushes[i].locked) { toast("A property's own walls can't go into furniture."); return false; }
        bs.push_back(ME.map.brushes[i]);
        V3 a, c; gmap::brushBounds(ME.map.brushes[i], a, c);
        mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)};
    }
    meSnapshot();
    V3 at{(mn.x + mx.x) / 2, mn.y, (mn.z + mx.z) / 2};
    gmap::Entity e = gmap::makeFurniture(bs, at, prop ? "prop" : "furniture", name.empty() ? (prop ? "Prop" : "Furniture") : name);
    if (!prop) {   // what stands on it comes with it
        const double pad = 0.05;
        for (int i = (int)ME.map.ents.size() - 1; i >= 0; i--) {
            const auto& k = ME.map.ents[i];
            if (k.type != "prop" && k.type != "gun_item") continue;
            if (k.pos.x < mn.x - pad || k.pos.x > mx.x + pad || k.pos.z < mn.z - pad || k.pos.z > mx.z + pad || k.pos.y < mn.y - pad || k.pos.y > mx.y + pad) continue;
            gmap::Entity loc = k; loc.pos = k.pos - at;
            e.items.insert(e.items.begin(), loc);
            ME.map.ents.erase(ME.map.ents.begin() + i);
        }
    }
    std::vector<int> s = ME.sel;
    std::sort(s.rbegin(), s.rend());
    for (int i : s) ME.map.brushes.erase(ME.map.brushes.begin() + i);
    ME.sel.clear(); ME.face = -1;
    ME.map.ents.push_back(e);
    ME.ent = (int)ME.map.ents.size() - 1;
    meChanged();
    toast(prop ? "Made a prop: it can be picked up on the range." : TextFormat("Made a piece of furniture%s. It moves and turns as one now.", e.items.empty() ? "" : TextFormat(" (with the %d thing%s on it)", (int)e.items.size(), e.items.size() == 1 ? "" : "s")));
    return true;
}
void meBreakApart() {
    if (ME.ent < 0 || ME.ent >= (int)ME.map.ents.size()) return;
    gmap::Entity e = ME.map.ents[ME.ent];
    if (e.type != "furniture" && e.type != "prop") return;
    meSnapshot();
    ME.map.ents.erase(ME.map.ents.begin() + ME.ent);
    ME.ent = -1;
    ME.sel.clear(); ME.face = -1;
    gmap::bakeFurniture(ME.map);   // (its blocks out of the map before they go back in as plain ones)
    int keep = 0;
    for (auto& b : ME.map.brushes) if (!b.furn) keep++;
    std::vector<gmap::Brush> plain, furn;
    for (auto& b : ME.map.brushes) (b.furn ? furn : plain).push_back(b);
    for (auto& p : e.parts) { plain.push_back(gmap::placedBrush(e, p)); ME.sel.push_back((int)plain.size() - 1); }
    plain.insert(plain.end(), furn.begin(), furn.end());
    ME.map.brushes = plain;
    if (e.type == "furniture") for (auto& k : gmap::placedItems(e)) ME.map.ents.push_back(k);
    (void)keep;
    meChanged();
    toast("Back into blocks: they're selected.");
}
bool meSaveFurniture() {
    if (ME.ent < 0 || ME.ent >= (int)ME.map.ents.size()) return false;
    const auto& e = ME.map.ents[ME.ent];
    if (e.type != "furniture" && e.type != "prop") return false;
    std::string file;
    if (!saveFurniture(e, file)) { toast("Couldn't save it."); return false; }
    toast("Saved " + file + " in your Furniture folder: place it again from the " + (e.type == "prop" ? std::string("Prop") : std::string("Furniture")) + " tool, in any map.");
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
// a career property's own walls can be painted but not moved, turned, copied or removed
static bool selLocked() {
    for (int i : ME.sel) if (i >= 0 && i < (int)ME.map.brushes.size() && ME.map.brushes[i].locked) { toast("Your property's walls stay put (you can paint them: Ctrl+click a face)."); return true; }
    return false;
}
void meDelete() {
    if (ME.ent >= 0) { meSnapshot(); ME.map.ents.erase(ME.map.ents.begin() + ME.ent); ME.ent = -1; meChanged(); return; }
    if (ME.sel.empty() || selLocked()) return;
    meSnapshot();
    std::vector<int> s = ME.sel;
    std::sort(s.rbegin(), s.rend());
    for (int i : s) ME.map.brushes.erase(ME.map.brushes.begin() + i);
    ME.sel.clear(); ME.face = -1;
    meChanged();
}
void meDuplicate() {
    if (ME.ent >= 0 && ME.sel.empty()) {   // a thing: a copy beside it
        if (ME.map.ents[ME.ent].type == "player_start") { toast("A map has one player start."); return; }
        meSnapshot();
        gmap::Entity e = ME.map.ents[ME.ent];
        double off = std::max(ME.grid, 0.25);
        e.pos += V3{off, 0, off};
        ME.map.ents.push_back(e);
        ME.ent = (int)ME.map.ents.size() - 1;
        meChanged();
        return;
    }
    if (ME.sel.empty() || selLocked()) return;
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
void meHideSelected() {
    for (int i : ME.sel) ME.map.brushes[i].hidden = true;
    if (!ME.sel.empty()) ME.status = std::to_string(ME.sel.size()) + " hidden (in the editor only). Show all brings them back.";
    ME.sel.clear(); ME.face = -1; ME.version++;
}
void meShowAll() { int n = 0; for (auto& b : ME.map.brushes) { n += b.hidden; b.hidden = false; } if (n) ME.status = std::to_string(n) + " shown again."; ME.version++; }
void meRotate(double deg) {
    if (ME.ent >= 0) { meSnapshot(); ME.map.ents[ME.ent].yaw = std::fmod(ME.map.ents[ME.ent].yaw + deg + 360, 360); meChanged(); return; }
    if (ME.sel.empty() || selLocked()) return;
    meSnapshot();
    V3 c = selCentre();
    c.x = snapG(c.x); c.z = snapG(c.z);
    for (int i : ME.sel) gmap::rotateY(ME.map.brushes[i], deg * DEG2RAD, c);
    meChanged();
}
void meMirror(int axis) {
    if (ME.sel.empty() || selLocked()) return;
    meSnapshot();
    V3 c = selCentre();
    for (int i : ME.sel) gmap::mirror(ME.map.brushes[i], axis, c);
    meChanged();
}
void meHollow() {
    if (ME.sel.empty() || selLocked()) return;
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
    DisableCursor();
}
static void stopWalking() {
    ME.walking = false;
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
    // where the mouse points in the world (a block, else the ground), for zooming toward it and turning round it
    auto pointUnder = [&](V3& at) {
        V3 o, d; mouseRay(o, d);
        gmap::Hit h = gmap::rayMap(ME.map, o, d, 2000);
        if (h.brush >= 0) { at = o + d * h.t; return true; }
        if (rayPlaneY(o, d, 0, at) && gmap::len(at - o) < 2000) return true;
        at = o + d * 20; return false;
    };
    static V3 vel;                 // flying: eased toward where you're steering (no jerks, no crawl)
    static V3 orbitAt; static double orbitDist = 10;
    static bool orbiting = false, panning = false; static double panDist = 10;
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && (inView || flying)) {   // fly: look with the mouse, WASD Q/E (or Space/C) to move
        flying = true;
        Vector2 d = GetMouseDelta();
        double s = 0.0022 * CFG.mouseSens;
        ME.yaw += d.x * s;
        ME.pitch = std::clamp(ME.pitch - d.y * s * (CFG.invertMouse ? -1 : 1), -1.55, 1.55);
        b = camBasis();
        V3 want;
        if (!io.WantCaptureKeyboard) {
            if (actDown(Act::MoveFwd)) want += b.fwd;
            if (actDown(Act::MoveBack)) want -= b.fwd;
            if (actDown(Act::MoveRight)) want += b.right;
            if (actDown(Act::MoveLeft)) want -= b.right;
            if (actDown(Act::FlyUp) || IsKeyDown(KEY_E)) want += V3{0, 1, 0};
            if (actDown(Act::FlyDown) || IsKeyDown(KEY_Q)) want -= V3{0, 1, 0};
        }
        double k = shift ? 4 : ctrl ? 0.2 : 1;   // Shift: fast, Ctrl: slow and careful
        if (gmap::len(want) > 1e-9) want = gmap::norm(want) * (ME.flySpeed * k);
        double ease = 1 - std::exp(-dt * 12);
        vel = vel + (want - vel) * ease;
        ME.cam += vel * dt;
        float w = GetMouseWheelMove();
        if (w != 0) { ME.flySpeed = std::clamp(ME.flySpeed * std::pow(1.3, (double)w), 0.5, 300.0); ME.status = TextFormat("Camera speed %s/s (wheel while flying; Shift x4, Ctrl x0.2).", meLen(ME.flySpeed).c_str()); }
        orbiting = panning = false;
        return;
    }
    flying = false;
    vel = {};
    // the middle button turns round the point you're looking at; Shift+middle slides the view
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && inView) {
        V3 at;
        if (shift) { panning = true; pointUnder(at); panDist = std::max(0.5, gmap::len(at - ME.cam)); }
        else {
            orbiting = true;
            VP v = viewRect();   // (round the middle of the view: no jump)
            V3 o = b.pos, d = b.fwd;
            gmap::Hit h = gmap::rayMap(ME.map, o, d, 2000);
            if (h.brush >= 0) orbitAt = o + d * h.t;
            else if (!rayPlaneY(o, d, 0, orbitAt) || gmap::len(orbitAt - o) > 300) orbitAt = o + d * 12;
            orbitDist = std::max(0.5, gmap::len(orbitAt - ME.cam));
            (void)v;
        }
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) orbiting = panning = false;
    Vector2 md = GetMouseDelta();
    if (orbiting) {
        double s = 0.005 * CFG.mouseSens;
        ME.yaw += md.x * s;
        ME.pitch = std::clamp(ME.pitch - md.y * s, -1.5, 1.5);
        ME.cam = orbitAt - camBasis().fwd * orbitDist;
    } else if (panning) {
        VP v = viewRect();
        double mpp = 2 * panDist * std::tan(b.fovy * DEG2RAD / 2) / std::max(1.f, v.h);   // metres per pixel at that distance
        ME.cam += b.right * (-md.x * mpp) + b.up * (md.y * mpp);
    }
    // the wheel: toward (or away from) what the mouse points at, a share of the way there (fast far off, fine up close)
    float w = GetMouseWheelMove();
    if (w != 0 && inView) {
        V3 at; pointUnder(at);
        V3 o, d; mouseRay(o, d);
        double dist = gmap::len(at - ME.cam);
        double step = std::max(0.15, dist * 0.2) * w * (shift ? 3 : ctrl ? 0.3 : 1);
        if (w > 0) step = std::min(step, std::max(0.0, dist - 0.3));
        ME.cam += d * step;
    }
}

// ---------------------------------------------------------------- input: tools
namespace {
enum class Drag { None, Move, MoveEnt, Face, Create, Rotate, Axis, Box, Corner };
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
Rings gRot;                       // the rotation rings (Rotate)
int gAxis = -1, gAxisHot = -1;    // the move arrows: the one dragged, the one under the mouse
V3 gAxisO; double gAxisT0 = 0;    // where the arrows stood at the press, and how far along the axis the mouse was
Vector2 gBoxA;                    // a box-select's first corner (screen)
int gCorner = -1, gCornerHot = -1;   // corner editing: the corner dragged, the one under the mouse
std::vector<V3> gCornerPts;       // the block's corners at the press
int gCornerPicked = -1, gCornerAxis = -1, gCornerAxisHot = -1;   // the corner clicked last (its move arrows), the arrow dragged, the one under the mouse
double gCornerT0 = 0;
V3 gCornerNow{};                  // where the dragged corner is now
V3 rotPivot;
double rotYaw0 = 0;               // a thing's yaw at the press
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

// where a thing placed at p (on a surface with normal n) stands: on that floor, or the floor below a wall clicked
static double floorBelow(V3 p, V3 n) {
    if (n.y > 0.7) return p.y;
    gmap::Hit h = gmap::rayMap(ME.map, p + V3{n.x, 0, n.z} * 0.3 + V3{0, 0.05, 0}, {0, -1, 0}, 200);
    return h.brush >= 0 ? p.y + 0.05 - h.t : 0;
}
static int entityUnderMouse(double* tOut) {
    V3 o, d; mouseRay(o, d);
    int best = -1; double bt = 1e30;
    for (size_t i = 0; i < ME.map.ents.size(); i++) {
        V3 p = ME.map.ents[i].pos;
        const auto& en = ME.map.ents[i];
        if (en.type == "furniture") continue;   // (picked by its blocks)
        bool lamp = en.type == "light";   // (a lamp: a small ball where it hangs)
        gmap::Brush b = lamp ? gmap::makeBox(p - V3{0.22, 0.22, 0.22}, p + V3{0.22, 0.22, 0.22}, 0) : gmap::makeBox(p - V3{0.3, 0, 0.3}, p + V3{0.3, 1.8, 0.3}, 0);
        if (en.type == "prop") { V3 a, c; gmap::entityBounds(en, a, c); b = gmap::makeBox(a - V3{0.01, 0.01, 0.01}, c + V3{0.01, 0.01, 0.01}, 0); }
        if (en.type == "gun_item") {
            const Design* gd = gunItemDesign(en.ref);
            double L = gd ? gunItemLength(*gd) / 2 : 0.15;
            V3 f{std::cos(en.yaw * DEG2RAD), 0, std::sin(en.yaw * DEG2RAD)};
            V3 a = p - f * L, c = p + f * L;
            b = gmap::makeBox({std::min(a.x, c.x) - 0.04, p.y, std::min(a.z, c.z) - 0.04}, {std::max(a.x, c.x) + 0.04, p.y + 0.06, std::max(a.z, c.z) + 0.04}, 0);
        }
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

// ---- move arrows: X (red), Y (green, up), Z (blue) from the selection's middle; drag one to slide along it
static bool gizmoOn() {
    if (ME.tool != MTool::Select || ME.rotateMode || ME.cornerMode || ME.walking || !CFG.editArrows) return false;
    if (ME.ent >= 0) return true;
    return !ME.sel.empty() && ME.face < 0 && !selLocked();
}
static V3 gizmoAt() { return ME.ent >= 0 ? ME.map.ents[ME.ent].pos + V3{0, 0.4, 0} : selCentre(); }
static double gizmoLen(V3 at) {   // world length that shows about 85 pixels long
    CamB c = camBasis(); VP v = viewRect();
    double dist = c.ortho ? 1 : std::max(0.3, dot(at - c.pos, c.fwd));
    double span = c.ortho ? c.fovy : 2 * dist * std::tan(c.fovy * DEG2RAD / 2);
    return 85 * span / std::max(1.f, v.h);
}
static const V3 AXES[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
static int gizmoPick(Vector2 m) {
    if (!gizmoOn()) return -1;
    V3 at = gizmoAt(); double L = gizmoLen(at);
    Vector2 a; if (!project(at, a)) return -1;
    int best = -1; float bd = 10;
    for (int k = 0; k < 3; k++) {
        Vector2 e; if (!project(at + AXES[k] * L, e)) continue;
        Vector2 ab = Vector2Subtract(e, a); float l2 = Vector2LengthSqr(ab);
        if (l2 < 1) continue;
        float t = std::clamp(Vector2DotProduct(Vector2Subtract(m, a), ab) / l2, 0.15f, 1.1f);
        float d = Vector2Distance(m, Vector2Add(a, Vector2Scale(ab, t)));
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
static double axisParam(V3 O, V3 A) {   // how far along the line O + t A the mouse ray passes closest
    V3 o, d; mouseRay(o, d);
    V3 w0 = O - o;
    double b = dot(A, d), dd = dot(A, w0), e = dot(d, w0), den = 1 - b * b;
    if (std::fabs(den) < 1e-6) return 0;
    return (b * e - dd) / den;
}
static void drawGizmo() {
    if (!gizmoOn()) return;
    V3 at = gizmoAt(); double L = gizmoLen(at);
    Vector2 a; if (!project(at, a)) return;
    static const Color col[3] = {{235, 80, 70, 255}, {110, 210, 90, 255}, {80, 140, 240, 255}};
    static const char* lab[3] = {"X", "Y", "Z"};
    for (int k = 0; k < 3; k++) {
        Vector2 e; if (!project(at + AXES[k] * L, e)) continue;
        bool hot = k == gAxisHot || k == gAxis;
        Color c = hot ? Color{255, 225, 90, 255} : col[k];
        DrawLineEx(a, e, hot ? 4.f : 3.f, c);
        Vector2 dir = Vector2Normalize(Vector2Subtract(e, a)), nrm{-dir.y, dir.x};
        DrawTriangle(Vector2Add(e, Vector2Scale(dir, 12)), Vector2Add(e, Vector2Scale(nrm, -6)), Vector2Add(e, Vector2Scale(nrm, 6)), c);
        DrawTriangle(Vector2Add(e, Vector2Scale(dir, 12)), Vector2Add(e, Vector2Scale(nrm, 6)), Vector2Add(e, Vector2Scale(nrm, -6)), c);
        viewText(lab[k], e.x + dir.x * 16 - 4, e.y + dir.y * 16 - 7, 12, c);
    }
    DrawCircleV(a, 4, Color{255, 255, 255, 200});
}
// ---- corners: the selected block's corners, to drag into any shape
static bool cornersOn() { return ME.cornerMode && ME.tool == MTool::Select && ME.sel.size() == 1 && !selLocked() && !ME.walking; }
static int cornerPick(Vector2 m) {
    if (!cornersOn()) return -1;
    auto v = gmap::brushVertices(ME.map.brushes[ME.sel[0]]);
    int best = -1; float bd = 10;
    for (size_t i = 0; i < v.size(); i++) { Vector2 s; if (project(v[i], s)) { float d = Vector2Distance(s, m); if (d < bd) { bd = d; best = (int)i; } } }
    return best;
}
static void drawCorners() {
    if (!cornersOn()) return;
    auto v = gmap::brushVertices(ME.map.brushes[ME.sel[0]]);
    for (size_t i = 0; i < v.size(); i++) {
        Vector2 s; if (!project(v[i], s)) continue;
        bool hot = (int)i == gCornerHot || (drag == Drag::Corner && (int)i == gCorner);
        DrawCircleV(s, hot ? 7.f : 5.f, hot ? Color{255, 225, 90, 255} : Color{20, 24, 32, 230});
        DrawCircleLinesV(s, hot ? 7.f : 5.f, Color{255, 205, 60, 255});
    }
}
// the picked corner's move arrows (Sprocket-style): drag one to slide that corner along X, Y or Z only
static bool cornerArrowsOn() {
    if (!cornersOn() || !CFG.editArrows) return false;
    auto v = gmap::brushVertices(ME.map.brushes[ME.sel[0]]);
    return gCornerPicked >= 0 && gCornerPicked < (int)v.size();
}
static int cornerArrowPick(Vector2 m) {
    if (!cornerArrowsOn()) return -1;
    V3 at = gmap::brushVertices(ME.map.brushes[ME.sel[0]])[gCornerPicked];
    double L = gizmoLen(at) * 0.8;
    Vector2 a; if (!project(at, a)) return -1;
    int best = -1; float bd = 9;
    for (int k = 0; k < 3; k++) {
        Vector2 e; if (!project(at + AXES[k] * L, e)) continue;
        Vector2 ab = Vector2Subtract(e, a); float l2 = Vector2LengthSqr(ab);
        if (l2 < 1) continue;
        float t = std::clamp(Vector2DotProduct(Vector2Subtract(m, a), ab) / l2, 0.25f, 1.1f);
        float d = Vector2Distance(m, Vector2Add(a, Vector2Scale(ab, t)));
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
static void drawCornerArrows() {
    if (!cornerArrowsOn() && !(drag == Drag::Corner && gCornerAxis >= 0)) return;
    if (ME.sel.size() != 1) return;
    auto v = gmap::brushVertices(ME.map.brushes[ME.sel[0]]);
    int idx = drag == Drag::Corner && gCorner >= 0 ? gCorner : gCornerPicked;
    if (idx < 0 || idx >= (int)v.size()) return;
    V3 at = drag == Drag::Corner && gCornerAxis >= 0 ? gCornerNow : v[idx];
    double L = gizmoLen(at) * 0.8;
    Vector2 a; if (!project(at, a)) return;
    static const Color col[3] = {{235, 80, 70, 255}, {110, 210, 90, 255}, {80, 140, 240, 255}};
    for (int k = 0; k < 3; k++) {
        Vector2 e; if (!project(at + AXES[k] * L, e)) continue;
        bool hot = k == gCornerAxisHot || k == gCornerAxis;
        Color c = hot ? Color{255, 225, 90, 255} : col[k];
        if (gCornerAxis >= 0 && k != gCornerAxis) c.a = 70;
        Vector2 dir = Vector2Normalize(Vector2Subtract(e, a)), nrm{-dir.y, dir.x};
        DrawLineEx(Vector2Add(a, Vector2Scale(dir, 6)), e, hot ? 3.5f : 2.5f, c);
        DrawTriangle(Vector2Add(e, Vector2Scale(dir, 11)), Vector2Add(e, Vector2Scale(nrm, -5)), Vector2Add(e, Vector2Scale(nrm, 5)), c);
        DrawTriangle(Vector2Add(e, Vector2Scale(dir, 11)), Vector2Add(e, Vector2Scale(nrm, 5)), Vector2Add(e, Vector2Scale(nrm, -5)), c);
    }
}
// the rotation rings round the selection (Rotate mode): world X, Y, Z for blocks; a thing turns about Y only
static Vector2 ringScreen(Vector3 p) { Vector2 s{-1e6f, -1e6f}; project(V3{p.x, p.y, p.z}, s); return s; }
static bool ringsActive() {
    if (!ME.rotateMode || ME.tool != MTool::Select || ME.walking) return false;
    if (ME.ent >= 0) return true;
    return !ME.sel.empty() && ME.face < 0 && !selLocked();
}
static void ringsSetup() {
    if (drag == Drag::Rotate) return;   // (kept as at the press while dragging)
    V3 c = ME.ent >= 0 ? ME.map.ents[ME.ent].pos + V3{0, 0.5, 0} : selCentre();
    rotPivot = c;
    gRot.c = rv(c);
    bool ent = ME.ent >= 0;
    gRot.ax[0] = ent ? Vector3{0, 0, 0} : Vector3{1, 0, 0}; gRot.label[0] = "X ";
    gRot.ax[1] = {0, 1, 0}; gRot.label[1] = "Y ";
    gRot.ax[2] = ent ? Vector3{0, 0, 0} : Vector3{0, 0, 1}; gRot.label[2] = "Z ";
    ringsFit(gRot, ringScreen, 80);
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
            if (ringsActive()) {   // Rotate: a ring under the mouse turns the selection
                ringsSetup();
                int r = ringsPick(gRot, ringScreen, m);
                if (r >= 0) {
                    beginDrag(Drag::Rotate);
                    ringsBegin(gRot, r, ringScreen, m);
                    dragOrig.clear();
                    for (int i : ME.sel) dragOrig.push_back(ME.map.brushes[i]);
                    if (ME.ent >= 0) rotYaw0 = ME.map.ents[ME.ent].yaw;
                    break;
                }
            }
            {   // a move arrow: slide the selection along it
                int k = gizmoPick(m);
                if (k >= 0) {
                    beginDrag(Drag::Axis);
                    gAxis = k; gAxisO = gizmoAt(); gAxisT0 = axisParam(gAxisO, AXES[k]);
                    dragOrig.clear(); for (int i : ME.sel) dragOrig.push_back(ME.map.brushes[i]);
                    if (ME.ent >= 0) dragEntOrig = ME.map.ents[ME.ent].pos;
                    break;
                }
            }
            if (cornersOn()) {   // the picked corner's arrows, else a corner of the selected block
                int k = cornerArrowPick(m);
                if (k >= 0) {
                    beginDrag(Drag::Corner);
                    gCorner = gCornerPicked; gCornerAxis = k; dragOrig = {ME.map.brushes[ME.sel[0]]}; gCornerPts = gmap::brushVertices(dragOrig[0]);
                    dragHit = gCornerPts[gCorner]; gCornerNow = dragHit; dragVertical = false;
                    gCornerT0 = axisParam(dragHit, AXES[k]);
                    break;
                }
                int c = cornerPick(m);
                if (c >= 0) {
                    beginDrag(Drag::Corner);
                    gCorner = c; gCornerPicked = c; gCornerAxis = -1; dragOrig = {ME.map.brushes[ME.sel[0]]}; gCornerPts = gmap::brushVertices(dragOrig[0]);
                    dragHit = gCornerPts[c]; gCornerNow = dragHit; dragVertical = alt;
                    break;
                }
            }
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
            if (h.brush >= 0 && ME.map.brushes[h.brush].furn && (e < 0 || h.t <= te)) { e = ME.map.brushes[h.brush].furn - 1; te = h.t; }   // (a furniture's block: the whole piece)
            if (e >= 0 && (h.brush < 0 || te <= h.t)) {
                if (ME.ent != e) { ME.sel.clear(); ME.face = -1; ME.ent = e; break; }
                ME.sel.clear(); ME.face = -1; ME.ent = e;
                beginDrag(Drag::MoveEnt);
                dragEntOrig = ME.map.ents[e].pos;
                dragHit = o + d * te; dragVertical = alt;
                break;
            }
            if (h.brush < 0) {   // empty space: drag a box round blocks to pick them all (Shift adds)
                if (!shift) { ME.sel.clear(); ME.face = -1; ME.ent = -1; }
                beginDrag(Drag::Box); gBoxA = m;
                break;
            }
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
        case MTool::Box: case MTool::Wedge: case MTool::Cylinder: case MTool::Stairs: case MTool::Pyramid: case MTool::Cone: case MTool::Sphere: {
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
            if (ME.stationKind.rfind("display_", 0) == 0 || ME.stationKind.rfind("decor_", 0) == 0) e.type = ME.stationKind;
            else e.type = (ME.stationKind == "counter" || ME.stationKind == "computer" || ME.stationKind == "door" ? "shop_" : "station_") + ME.stationKind;
            e.pos = {snapG(p.x), floorBelow(p, n), snapG(p.z)};
            e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);
            ME.map.ents.push_back(e);
            ME.ent = (int)ME.map.ents.size() - 1; ME.sel.clear();
            meChanged();
            break;
        }
        case MTool::Door: {   // a door: in a doorway on the floor, across the way you're looking
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            meSnapshot();
            gmap::Entity e;
            e.type = "door"; e.ref = ME.doorStyle; e.width = ME.doorWidth;
            e.pos = {snapG(p.x), floorBelow(p, n), snapG(p.z)};
            e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 90) * 90 + 720, 360);
            ME.map.ents.push_back(e);
            ME.ent = (int)ME.map.ents.size() - 1; ME.sel.clear();
            meChanged();
            break;
        }
        case MTool::Furniture: case MTool::Prop: {   // a piece of furniture on the floor, facing you (its back to a wall you click); a thing to pick up on any surface
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            gmap::Entity e;
            const std::string& pick = ME.tool == MTool::Prop ? ME.propPick : ME.furnPick;
            if (!furnitureFromPick(pick, e)) { toast("Pick what to place in the panel first."); break; }
            double facing = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);
            if (ME.tool == MTool::Furniture && std::fabs(n.y) < 0.3) {   // a wall: its back against it, standing on the floor below
                double back = 0;
                for (auto& q : e.parts) { V3 a, c; gmap::brushBounds(q, a, c); back = std::min(back, a.x); }
                V3 at = p + V3{n.x, 0, n.z} * (-back + 0.003);
                gmap::Hit fl = gmap::rayMap(ME.map, at + V3{0, 0.05, 0}, {0, -1, 0}, 50);
                e.pos = {at.x, fl.brush >= 0 ? at.y + 0.05 - fl.t : 0, at.z};
                e.yaw = std::atan2(n.z, n.x) * RAD2DEG;
            } else {
                bool fine = ME.tool == MTool::Prop;   // (on a shelf: to the centimetre, not the grid)
                e.pos = fine ? V3{std::round(p.x * 100) / 100, floorBelow(p, n), std::round(p.z * 100) / 100} : V3{snapG(p.x), floorBelow(p, n), snapG(p.z)};
                e.yaw = facing;
                if (e.type == "gun_item") e.yaw = std::fmod(facing + 90, 360);   // (lying across in front of you)
            }
            meSnapshot();
            ME.map.ents.push_back(e);
            ME.ent = (int)ME.map.ents.size() - 1; ME.sel.clear(); ME.face = -1;
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
            e.pos = {snapG(p.x), floorBelow(p, n), snapG(p.z)};
            e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);   // facing you
            ME.map.ents.push_back(e);
            ME.ent = (int)ME.map.ents.size() - 1; ME.sel.clear();
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
        case MTool::Radio: {   // a radio standing on the floor or a shelf, facing you
            V3 p, n; int b, f;
            if (!surfaceUnderMouse(p, n, b, f)) break;
            meSnapshot();
            gmap::Entity e = newRadio();
            e.pos = {std::round(p.x * 100) / 100, floorBelow(p, n), std::round(p.z * 100) / 100};
            e.yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);
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
            ME.map.ents[e].pos = {snapG(p.x), floorBelow(p, n), snapG(p.z)};
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
                else if (ME.ent >= 0) {
                    gmap::Entity& en = ME.map.ents[ME.ent];
                    V3 np = dragEntOrig + dl;
                    bool onSurface = !dragVertical && !ME.top && en.type != "light" && en.type != "target_paper";
                    if (onSurface) {   // stand it on whatever is under the mouse (skipping its own blocks and anything it carries)
                        V3 so = o; double gone = 0;
                        for (int k = 0; k < 6; k++) {
                            gmap::Hit sh = gmap::rayMap(ME.map, so, d, 5000);
                            if (sh.brush < 0) break;
                            if (ME.map.brushes[sh.brush].furn == ME.ent + 1) { so = so + d * (sh.t + 0.002); gone += sh.t + 0.002; continue; }
                            if (sh.n.y > 0.7) { V3 q = so + d * sh.t; np = {dragEntOrig.x + snapG(q.x - dragHit.x), q.y, dragEntOrig.z + snapG(q.z - dragHit.z)}; }
                            break;
                        }
                        (void)gone;
                    }
                    en.pos = np;
                    if (en.type == "furniture") rebake();
                }
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
        } else if (drag == Drag::Axis && gAxis >= 0) {
            double t = axisParam(gAxisO, AXES[gAxis]) - gAxisT0;
            t = snapG(t);
            V3 dl = AXES[gAxis] * t;
            if (ME.ent >= 0) { ME.map.ents[ME.ent].pos = dragEntOrig + dl; if (ME.map.ents[ME.ent].type == "furniture") rebake(); }
            else for (size_t k = 0; k < ME.sel.size() && k < dragOrig.size(); k++) { ME.map.brushes[ME.sel[k]] = dragOrig[k]; gmap::translate(ME.map.brushes[ME.sel[k]], dl); }
            ME.status = TextFormat("Moved %s along %c.", meLen(t).c_str(), "XYZ"[gAxis]);
            if (std::fabs(t) > 1e-9) { dragChanged = true; ME.version++; }
        } else if (drag == Drag::Corner && gCorner >= 0 && !dragOrig.empty()) {
            V3 p; bool ok;
            if (gCornerAxis >= 0) { ok = true; p = dragHit + AXES[gCornerAxis] * snapG(axisParam(dragHit, AXES[gCornerAxis]) - gCornerT0); gCornerNow = p; }
            else if (dragVertical || ME.top) {   // up and down (Alt), or across in the top view
                if (ME.top) { ok = true; p = o + d * ((o.y - dragHit.y) / -d.y); }
                else { CamB c = camBasis(); V3 nrm = norm({c.fwd.x, 0, c.fwd.z}); ok = rayPlane(o, d, nrm, dot(nrm, dragHit), p); if (ok) p = {dragHit.x, p.y, dragHit.z}; }
            } else ok = rayPlaneY(o, d, dragHit.y, p);
            if (ok) {
                if (gCornerAxis < 0) p = {snapG(p.x), dragVertical ? snapG(p.y) : dragHit.y, snapG(p.z)};
                gCornerNow = p;
                auto pts = gCornerPts; pts[gCorner] = p;
                gmap::Brush nb;
                if (gmap::brushFromPoints(pts, ME.mat, nb, &dragOrig[0])) { ME.map.brushes[ME.sel[0]] = nb; ME.version++; dragChanged = gmap::len(p - gCornerPts[gCorner]) > 1e-9; }
            }
        } else if (drag == Drag::Rotate) {
            double deg = ringsDrag(gRot, m, ctrl);
            Vector3 a = gRot.ax[gRot.axis];
            if (ME.ent >= 0) {   // a thing: its yaw (the right-hand way about up turns +X toward -Z: yaw goes down)
                ME.map.ents[ME.ent].yaw = std::fmod(rotYaw0 - deg + 3600, 360);
                if (ME.map.ents[ME.ent].type == "furniture") rebake();
            } else for (size_t k = 0; k < ME.sel.size() && k < dragOrig.size(); k++) {
                ME.map.brushes[ME.sel[k]] = dragOrig[k];
                gmap::rotateAxis(ME.map.brushes[ME.sel[k]], V3{a.x, a.y, a.z}, deg * DEG2RAD, rotPivot);
            }
            ME.status = TextFormat("Turned %.1f degrees about %c.", deg, "XYZ"[std::clamp(gRot.axis, 0, 2)]);
            if (std::fabs(deg) > 1e-6) { dragChanged = true; ME.version++; }
        } else if (drag == Drag::Create) {
            V3 p;
            bool ok = rayPlaneY(o, d, createY, p);
            if (ok) createB = {snapG(p.x), createY, snapG(p.z)};
        }
    }

    if (drag != Drag::None && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (drag == Drag::Box) {   // every block whose middle is inside the box
            Rectangle r{std::min(gBoxA.x, m.x), std::min(gBoxA.y, m.y), std::fabs(m.x - gBoxA.x), std::fabs(m.y - gBoxA.y)};
            if (r.width > 4 && r.height > 4) {
                for (size_t i = 0; i < ME.map.brushes.size(); i++) {
                    const auto& br = ME.map.brushes[i];
                    if (br.hidden || br.furn || (careerZoned() && br.locked) || isSel((int)i)) continue;
                    Vector2 sc; if (project(gmap::brushCentre(br), sc) && CheckCollisionPointRec(sc, r)) ME.sel.push_back((int)i);
                }
                if (!ME.sel.empty()) { ME.ent = -1; ME.face = -1; ME.status = std::to_string(ME.sel.size()) + " blocks selected."; }
            }
            drag = Drag::None;
            return;
        }
        if (gCorner >= 0 && ME.sel.size() == 1) {   // (the block is rebuilt from its corners: keep the moved one picked)
            auto v = gmap::brushVertices(ME.map.brushes[ME.sel[0]]);
            double bd = 1e18; for (size_t q = 0; q < v.size(); q++) { double dd = gmap::len(v[q] - gCornerNow); if (dd < bd) { bd = dd; gCornerPicked = (int)q; } }
        }
        gAxis = -1; gCorner = -1; gCornerAxis = -1;
        if (drag == Drag::Create) {
            double minW = std::max(ME.grid, 0.01) * 0.5;
            V3 mn{std::min(createA.x, createB.x), createY, std::min(createA.z, createB.z)};
            V3 mx{std::max(createA.x, createB.x), createY + std::max(ME.height, 0.01), std::max(createA.z, createB.z)};
            if (mx.x - mn.x >= minW && mx.z - mn.z >= minW) {
                std::vector<gmap::Brush> made;
                if (ME.tool == MTool::Box) made.push_back(gmap::makeBox(mn, mx, ME.mat));
                else if (ME.tool == MTool::Wedge) made.push_back(gmap::makeWedge(mn, mx, wedgeDir(), ME.mat));
                else if (ME.tool == MTool::Cylinder) made.push_back(gmap::makeCylinder(mn, mx, ME.cylSides, ME.mat));
                else if (ME.tool == MTool::Pyramid) made.push_back(gmap::makePyramid(mn, mx, ME.mat));
                else if (ME.tool == MTool::Cone) made.push_back(gmap::makeCone(mn, mx, ME.cylSides, ME.mat));
                else if (ME.tool == MTool::Sphere) made.push_back(gmap::makeSphere(mn, mx, std::clamp(ME.cylSides, 6, 16), ME.mat));
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

// ---------------------------------------------------------------- select all, copy and paste
static struct { std::vector<gmap::Brush> brushes; std::vector<gmap::Entity> ents; V3 base; bool any = false; } gClip;
void meSelectAll() {
    ME.sel.clear(); ME.ent = -1; ME.face = -1;
    for (size_t i = 0; i < ME.map.brushes.size(); i++) if (!ME.map.brushes[i].furn && !ME.map.brushes[i].locked) ME.sel.push_back((int)i);
    ME.status = std::to_string(ME.sel.size()) + " blocks selected.";
}
void meCopy() {
    if (ME.sel.empty() && ME.ent < 0) return;
    gClip = {};
    V3 mn{1e30, 1e30, 1e30}, mx{-1e30, -1e30, -1e30};
    auto grow = [&](V3 a, V3 c) { mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)}; mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)}; };
    for (int i : ME.sel) { const auto& b = ME.map.brushes[i]; if (b.locked || b.furn) continue; gClip.brushes.push_back(b); V3 a, c; gmap::brushBounds(b, a, c); grow(a, c); }
    if (ME.ent >= 0 && ME.map.ents[ME.ent].type != "player_start") { gClip.ents.push_back(ME.map.ents[ME.ent]); grow(ME.map.ents[ME.ent].pos, ME.map.ents[ME.ent].pos); }
    if (gClip.brushes.empty() && gClip.ents.empty()) return;
    gClip.base = {(mn.x + mx.x) / 2, mn.y, (mn.z + mx.z) / 2};   // (pasted standing where you point)
    gClip.any = true;
    ME.status = "Copied. Ctrl+V puts a copy where the mouse points.";
}
static bool surfaceUnderMouse(V3& p, V3& n, int& brush, int& face);
static bool mouseInView();
void mePaste() {
    if (!gClip.any) return;
    V3 at = gClip.base + V3{std::max(ME.grid, 0.25), 0, std::max(ME.grid, 0.25)};
    V3 p, n; int b, f;
    if (mouseInView() && surfaceUnderMouse(p, n, b, f)) at = {snapG(p.x), floorBelow(p, n), snapG(p.z)};
    V3 dl = at - gClip.base;
    meSnapshot();
    ME.sel.clear(); ME.ent = -1; ME.face = -1;
    for (auto bb : gClip.brushes) { gmap::translate(bb, dl); bb.locked = false; ME.map.brushes.push_back(bb); ME.sel.push_back((int)ME.map.brushes.size() - 1); }
    // (blocks first, then rebake moves furniture blocks after them: keep the selection by pasting before the baked ones)
    for (auto e : gClip.ents) { e.pos += dl; ME.map.ents.push_back(e); ME.ent = (int)ME.map.ents.size() - 1; }
    if (!ME.sel.empty()) ME.ent = -1;
    meChanged();
}

static void keyInput() {
    if (ImGui::GetIO().WantCaptureKeyboard || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) return;
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (actPressed(Act::Undo)) meUndo();
    if (actPressed(Act::Redo) || (ctrl && shift && IsKeyPressed(KEY_Z))) meRedo();
    if (actPressed(Act::Delete) || (IsKeyPressed(KEY_BACKSPACE) && !ctrl)) meDelete();
    if (actPressed(Act::Duplicate)) meDuplicate();
    if (ctrl && IsKeyPressed(KEY_S)) meSave();
    if (actPressed(Act::MapRotate)) { double a = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT) ? 15 : 90; meRotate(shift ? -a : a); }
    if (ctrl && IsKeyPressed(KEY_A)) meSelectAll();
    if (ctrl && IsKeyPressed(KEY_C)) meCopy();
    if (ctrl && IsKeyPressed(KEY_V)) mePaste();
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
    double g = std::max(ME.grid, 0.01) * (shift ? 4 : 1);
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
    if (gmap::len(nud) > 0 && (!ME.sel.empty() || ME.ent >= 0) && !(ME.ent < 0 && selLocked())) {
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
static double gWalkHour = -1;   // walking a map with a running day: its clock (from the map's time when you start)
static void walkUpdate(float dt) {
    if (shopActive()) shopClockTick(dt);
    else if (ME.map.look.time >= 0 && ME.map.look.dayMinutes > 0) {
        if (gWalkHour < 0) gWalkHour = ME.map.look.time;
        gWalkHour = std::fmod(gWalkHour + dt * 24.0 / (ME.map.look.dayMinutes * 60), 24.0);
    }
    if (radioWindowOpen()) return;   // (tuning a radio: its window has the mouse; Esc closes it)
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
    if (actPressed(Act::Use)) {
        int r = radioFacing(ME.map, gmap::playerEye(ME.pl, ME.map), gmap::playerForward(ME.pl));
        if (r >= 0) { radioOpen(&ME.map, r); return; }
        int d = doors::facing(ME.map, gmap::playerEye(ME.pl, ME.map), gmap::playerForward(ME.pl));
        if (d >= 0) doors::toggle(ME.map, d, ME.pl.feet);
    }
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
    "Click to select (Shift adds, Ctrl picks a face to paint), drag on empty space to box-select. Drag a block, or its coloured arrows, to move it; squares resize. Right mouse + WASD flies (wheel: speed), middle mouse orbits, Shift+middle pans, wheel zooms to the mouse.",
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
    "Click a ceiling, a wall or the floor to place a light. Its colour, brightness and reach are in the panel.",
    "Click to place an ammo box: press F by it on the map to fill up every gun you carry.",
    "Click to place a weapon crate: press F by it on the map to take any of your guns.",
    "Click to place what's chosen in the panel: a workstation, the counter, a display or decoration. It faces you.",
    "Click a floor in a doorway to place a door. F opens and shuts it on foot.",
    "Click the floor (it stands at chest height) or a wall to place a paper target.",
    "Click the floor to place the furniture chosen in the panel, facing you. Click a wall to stand it with its back to the wall.",
    "Click any surface (a shelf, a table, the floor) to place the thing chosen in the panel. Players pick it up with F.",
    "Click a floor, a shelf or a table to place a radio. Pick its station in the panel; on foot, F at it tunes it.",
    "Drag to draw a pyramid. Its height is set in the top bar.",
    "Drag to draw a cone. Its number of sides is in the panel.",
    "Drag to draw a ball (its roundness is the number of sides in the panel).",
};
static_assert(sizeof(HINT) / sizeof(HINT[0]) == (int)MTool::Sphere + 1, "a hint for every tool");

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
    // what a click would pick (Select) or where a placed thing would land (the placing tools)
    if (drag == Drag::None && mouseInView() && !ImGui::GetIO().WantCaptureMouse) {
        V3 o, d; mouseRay(o, d);
        if (ME.tool == MTool::Select) {
            double te; int e = entityUnderMouse(&te);
            gmap::Hit h = gmap::rayMap(ME.map, o, d, 5000);
            if (h.brush >= 0 && (e < 0 || h.t < te)) {
                const auto& hb = ME.map.brushes[h.brush];
                bool locked = careerZoned() && hb.locked;
                if (hb.furn) { for (auto& fb : ME.map.brushes) if (fb.furn == hb.furn) drawBrushEdges(fb, Color{255, 255, 255, 110}); }
                else if (!locked && !isSel(h.brush)) drawBrushEdges(hb, Color{255, 255, 255, 110});
            }
        } else if (ME.tool != MTool::Box && ME.tool != MTool::Wedge && ME.tool != MTool::Cylinder && ME.tool != MTool::Stairs && ME.tool != MTool::Clip
                   && ME.tool != MTool::Pyramid && ME.tool != MTool::Cone && ME.tool != MTool::Sphere) {
            V3 p, n; int bh, fh;
            if (surfaceUnderMouse(p, n, bh, fh)) {
                bool wall = std::fabs(n.y) <= 0.7;
                V3 at = ME.tool == MTool::Light ? p + n * (n.y > 0.7 ? 2.4 : 0.15)
                      : ME.tool == MTool::Target ? (n.y > 0.7 ? V3{snapG(p.x), p.y + 1.45, snapG(p.z)} : p + n * 0.01)
                      : ME.tool == MTool::Prop || ME.tool == MTool::Radio ? V3{std::round(p.x * 100) / 100, floorBelow(p, n), std::round(p.z * 100) / 100}
                      : V3{snapG(p.x), floorBelow(p, n), snapG(p.z)};
                Color gc{120, 230, 200, 220};
                double yaw = ME.top ? 0 : std::fmod(std::round(ME.yaw * RAD2DEG / 45) * 45 + 180 + 720, 360);
                if (wall && (ME.tool == MTool::Furniture || ME.tool == MTool::Light || ME.tool == MTool::Target)) yaw = std::atan2(n.z, n.x) * RAD2DEG;
                V3 f{std::cos(yaw * DEG2RAD), 0, std::sin(yaw * DEG2RAD)};
                if (ME.tool == MTool::Light || ME.tool == MTool::Target) { DrawSphereWires(rv(at), 0.12f, 6, 8, gc); DrawLine3D(rv(p), rv(at), gc); }
                else {
                    DrawCircle3D(rv(at + V3{0, 0.01, 0}), 0.3f, {1, 0, 0}, 90, gc);
                    DrawLine3D(rv(at + V3{0, 0.01, 0}), rv(at + V3{0, 0.01, 0} + f * 0.45), gc);
                    DrawLine3D(rv(at), rv(at + V3{0, 1.0, 0}), Color{gc.r, gc.g, gc.b, 90});
                }
            }
        }
    }
    // brush being drawn
    if (drag == Drag::Create) {
        V3 mn{std::min(createA.x, createB.x), createY, std::min(createA.z, createB.z)};
        V3 mx{std::max(createA.x, createB.x), createY + ME.height, std::max(createA.z, createB.z)};
        if (mx.x - mn.x > 1e-6 && mx.z - mn.z > 1e-6) {
            std::vector<gmap::Brush> pv;
            if (ME.tool == MTool::Box) pv.push_back(gmap::makeBox(mn, mx, 0));
            else if (ME.tool == MTool::Wedge) pv.push_back(gmap::makeWedge(mn, mx, wedgeDir(), 0));
            else if (ME.tool == MTool::Cylinder) pv.push_back(gmap::makeCylinder(mn, mx, ME.cylSides, 0));
            else if (ME.tool == MTool::Pyramid) pv.push_back(gmap::makePyramid(mn, mx, 0));
            else if (ME.tool == MTool::Cone) pv.push_back(gmap::makeCone(mn, mx, ME.cylSides, 0));
            else if (ME.tool == MTool::Sphere) pv.push_back(gmap::makeSphere(mn, mx, std::clamp(ME.cylSides, 6, 16), 0));
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
        else if (e.type.rfind("station_", 0) == 0 || e.type.rfind("shop_", 0) == 0 || gmap::isDisplay(e.type) || gmap::isDecor(e.type)) drawStation(e.type, rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "ammo_box") drawAmmoBox(rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "radio") drawRadio(rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0}, e.playing);
        else if (e.type == "target_paper") drawPaperTargetMarker(e, (int)i == ME.ent);
        else if (e.type == "weapon_crate") drawWeaponCrate(rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "attachment_crate") drawAttachmentCrate(rv(e.pos), (float)e.yaw, (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
        else if (e.type == "armor_plate") for (int k = 0; k < std::max(1, e.count); k++)
            drawArmorPlate(rv(e.pos + V3{0, 0.03 * k, 0}), (float)(e.yaw + 9 * k), (int)i == ME.ent ? Color{255, 205, 60, 255} : Color{0, 0, 0, 0});
    }
    drawMapThings(ME.map, ME.ent);
    (void)b;
}

static void drawEditorOverlay() {
    VP v = viewRect();
    if (drag == Drag::None && mouseInView()) { gAxisHot = gizmoPick(GetMousePosition()); gCornerHot = cornerPick(GetMousePosition()); gCornerAxisHot = cornerArrowPick(GetMousePosition()); if (gCornerAxisHot >= 0) gCornerHot = -1; }
    else if (drag == Drag::None) gAxisHot = gCornerHot = -1;
    drawGizmo();
    drawCorners();
    drawCornerArrows();
    if (drag == Drag::Box) {
        Vector2 m = GetMousePosition();
        Rectangle r{std::min(gBoxA.x, m.x), std::min(gBoxA.y, m.y), std::fabs(m.x - gBoxA.x), std::fabs(m.y - gBoxA.y)};
        DrawRectangleRec(r, Color{255, 205, 60, 30});
        DrawRectangleLinesEx(r, 1.5f, Color{255, 205, 60, 220});
    }
    if (ringsActive()) {
        ringsSetup();
        if (drag != Drag::Rotate) gRot.hot = ringsPick(gRot, ringScreen, GetMousePosition());
        ringsDraw(gRot, ringScreen, rv(camBasis().pos));
    }
    // face handles on the single selected brush
    if (ME.sel.size() == 1 && ME.face < 0 && ME.tool == MTool::Select && !ME.rotateMode && !ME.cornerMode) {
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
        const char* h = ME.tool == MTool::Select && ME.rotateMode ? "Rotate: click a block or a thing, then drag a ring to turn it about that axis (Ctrl: 15 degree steps). R turns 90 degrees about up."
                      : ME.tool == MTool::Select && ME.cornerMode ? "Corners: click a block, then drag its corner dots to reshape it (Alt: up and down). It stays a solid, convex shape; snaps to the grid."
                                                                   : HINT[(int)ME.tool];
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
    if (const char* e = std::getenv("GUNSMITH_MAPROT")) {   // (screenshots) "axis,deg": a block by the start, selected in Rotate, turned that much
        static int fr = 0;
        if (++fr == 10) {
            int ax = 1; double dg = 0; std::sscanf(e, "%d,%lf", &ax, &dg);
            V3 at{-3, 0, 0};
            for (auto& en : ME.map.ents) if (en.type == "player_start") at = en.pos;
            meSnapshot();
            ME.map.brushes.push_back(gmap::makeBox(at + V3{0.6, 0, -0.4}, at + V3{1.6, 0.6, 0.4}, ME.mat));
            ME.sel = {(int)ME.map.brushes.size() - 1}; ME.ent = -1; ME.face = -1;
            ME.tool = MTool::Select; ME.rotateMode = true;
            if (const char* md = std::getenv("GUNSMITH_MAPMODE")) { std::string mm = md; ME.rotateMode = mm == "rotate"; ME.cornerMode = mm == "corners"; }
            if (const char* mc = std::getenv("GUNSMITH_MAPCORNER")) gCornerPicked = std::atoi(mc);   // (screenshots) this corner picked   // (move, rotate, corners)
            V3 axis{ax == 0 ? 1.0 : 0.0, ax == 1 ? 1.0 : 0.0, ax == 2 ? 1.0 : 0.0};
            if (dg != 0) gmap::rotateAxis(ME.map.brushes.back(), axis, dg * DEG2RAD, selCentre());
            for (auto& f : ME.map.brushes.back().faces) f.color = 0x3E7CA8FFu;   // (painted blue: the colour too)
            meChanged();
        }
    }
    if (std::getenv("GUNSMITH_MECLIPTEST")) {   // (test) select all, copy, paste, duplicate a thing: counts before and after
        static int fr = 0;
        if (++fr == 20) {
            meSnapshot(); ME.map.brushes.push_back(gmap::makeBox({-3, 0, 0}, {-2.5, 0.5, 0.5}, ME.mat)); meChanged();
            size_t b0 = ME.map.brushes.size(), e0 = ME.map.ents.size();
            meSelectAll(); size_t picked = ME.sel.size(); meCopy(); mePaste();
            size_t b1 = ME.map.brushes.size();
            int lockedSel = 0; for (int i : ME.sel) if (ME.map.brushes[i].locked) lockedSel++;
            int ent = -1; for (size_t i = 0; i < ME.map.ents.size(); i++) if (ME.map.ents[i].type.rfind("station_", 0) == 0) ent = (int)i;
            ME.sel.clear(); ME.ent = ent; meDuplicate();
            std::printf("MECLIP brushes %zu -> %zu (picked %zu, pasted selected %zu, locked in it %d) ents %zu -> %zu undo %zu\n", b0, b1, picked, ME.sel.size(), lockedSel, e0, ME.map.ents.size(), ME.undo.size());
            meUndo(); meUndo();
            std::printf("MECLIP after two undos: brushes %zu ents %zu\n", ME.map.brushes.size(), ME.map.ents.size());
        }
    }
    if (careerZoned() && !(ME.face >= 0 && ME.sel.size() == 1))   // (the building's own walls can't be picked, only one face to paint)
        ME.sel.erase(std::remove_if(ME.sel.begin(), ME.sel.end(), [](int b) { return b < 0 || b >= (int)ME.map.brushes.size() || ME.map.brushes[b].locked; }), ME.sel.end());
    if (!modal) {
        if (ME.walking) radioListen(ME.map, gmap::playerEye(ME.pl, ME.map), gmap::playerForward(ME.pl));
        else { CamB cb = camBasis(); radioListen(ME.map, ME.top ? ME.topC + V3{0, 1.6, 0} : ME.cam, cb.fwd); }
        if (ME.walking) walkUpdate(dt);
        else {
            if (shopActive() && IsKeyPressed(KEY_B) && !ImGui::GetIO().WantCaptureKeyboard) { shopWalk(); return; }   // (career: back on foot)
            flyCamera(dt); toolInput(); keyInput();
        }
    }
    if (ME.built != ME.version) { mapBuildMeshes(ME.map); ME.built = ME.version; }
}

// people's sun shadows on foot: you, and in the career shop the customers and staff (plain outlines, the owner's)
static void walkShadows() {
    if (!ME.walking) return;
    if (ME.pl.stance == gmap::Stance::Prone) shadowBody(rv(ME.pl.feet), 1.75f, {(float)std::cos(ME.pl.yaw), 0, (float)std::sin(ME.pl.yaw)});
    else shadowBody(rv(ME.pl.feet), (float)std::max(0.6, ME.pl.eye + 0.12));
    if (shopActive()) shopShadowCasters();
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
    gmap::Look look = ME.map.look;
    if (!ME.walking) gWalkHour = -1;
    if (shopActive() && ME.walking) { if (look.time < 0) look.time = 12; look = gmap::lookAtHour(look, shopHour()); }   // (career: the shop's clock)
    else if (look.time >= 0) look = gmap::lookAtHour(look, ME.walking && gWalkHour >= 0 ? gWalkHour : look.time);
    mapWorkLight(ME.walking ? 0 : ME.lightMode);
    if (!(ME.top && !ME.walking)) {   // sun shadows round the camera (none in the top view or under the work light)
        V3 at = ME.walking ? gmap::playerEye(ME.pl, ME.map) : ME.cam;
        CamB cb = camBasis();
        V3 ahead = ME.walking ? gmap::norm(V3{std::cos(ME.pl.yaw), 0, std::sin(ME.pl.yaw)}) : gmap::norm(V3{cb.fwd.x, 0, cb.fwd.z});
        mapShadowPass(look, rv(at + ahead * 25), walkShadows);
    } else mapShadowsOff();
    if (ME.top && !ME.walking) DrawRectangle((int)v.x, (int)v.y, (int)v.w, (int)v.h, Color{38, 42, 49, 255});
    else mapDrawSky(look, v.x, v.y, v.w, v.h, pitch, fov);
    mapBeginViewport(cam, v.x, v.y, v.w, v.h);
    if (ME.top && !ME.walking) { look.fogStart = 1e6; look.fogEnd = 2e6; }  // the top-view camera sits far above the map
    mapApplyLook(look);
    mapSetLamps(ME.map, ME.walking ? gmap::playerEye(ME.pl, ME.map) : ME.cam);
    mapShowInvisible(!ME.walking);
    mapDrawWorld();
    mapShowInvisible(false);
    mapWorkLight(false);
    if (!ME.walking) drawEditor3D();
    else for (auto& e : ME.map.ents) {   // on foot: the things you'd see (not the editor's markers)
        if (e.type.rfind("station_", 0) == 0 || e.type.rfind("shop_", 0) == 0 || gmap::isDisplay(e.type) || gmap::isDecor(e.type)) drawStation(e.type, rv(e.pos), (float)e.yaw);
        else if (e.type == "attachment_crate") drawAttachmentCrate(rv(e.pos), (float)e.yaw);
        else if (e.type == "ammo_box") drawAmmoBox(rv(e.pos), (float)e.yaw);
        else if (e.type == "weapon_crate") drawWeaponCrate(rv(e.pos), (float)e.yaw);
        else if (e.type == "radio") drawRadio(rv(e.pos), (float)e.yaw, Color{0, 0, 0, 0}, e.playing);
        else if (e.type == "armor_plate") for (int k = 0; k < std::max(1, e.count); k++) drawArmorPlate(rv(e.pos + V3{0, 0.03 * k, 0}), (float)(e.yaw + 9 * k));
    }
    if (ME.walking) drawMapThings(ME.map, -1);
    doors::draw(ME.map, ME.walking ? -1 : ME.ent);
    if (shopActive()) shopDrawPeople();   // (career: customers at the counter, staff at their workstations)
    EndMode3D();
    mapEndViewport();
    mapRestoreLook();
    if (ME.walking) { if (shopActive()) shopHud(); else drawWalkHud(); }
    else drawEditorOverlay();
    mapShadowsOff();
}
