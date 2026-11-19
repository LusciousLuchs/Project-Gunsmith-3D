// Gunsmith3D - the 3D vertices tool. A part made a free 3D mesh (MeshDef) shows all its corners and edges: click a
// corner to pick it (Shift adds more), click a face to pick its corners; drag what's picked in the plane the view
// faces most (from the side: up and along; from the top: along and in depth; from the front: up and in depth; hold Z
// for depth from any view). Shift-click picks more faces; E pushes the picked faces out together, M joins the picked
// corners into one, J draws an edge across a face between two picked corners, Delete removes them, Ctrl+A picks all.
// The panel adds rings of corners round the part (to cave in or bulge out its sides), scales and moves the picked ones.
#include "app/meshtool.hpp"
#include "app/app.hpp"
#include "app/view.hpp"
#include "app/settings.hpp"
#include "app/gizmo.hpp"
#include "raylib.h"
#include "raymath.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace {
struct State {
    std::string part;          // the part the picks belong to (its id)
    std::vector<int> sel;      // picked corners
    std::vector<int> faces;    // picked faces (Shift-click adds more)
    int hoverV = -1, hoverF = -1;
    bool dragging = false, live = false;
    Vector2 start{};
    int plane = 2;             // the plane the drag moves in: 0 faces x (y-z), 1 faces y (x-z), 2 faces z (x-y)
    std::vector<P3d> orig;     // the corners as they were at the press
    P3d anchor{};              // the corner (or face middle) the mouse holds
    double zDrag = 0;
    double extrude = 5;
} M;

int selMesh() {
    if (A.sel.kind != 1 || A.sel.idx < 0 || A.sel.idx >= (int)A.design.bodies.size()) return -1;
    return meshShaped(A.design.bodies[A.sel.idx]) ? A.sel.idx : -1;
}
Vector3 vw(int i, const P3d& p) { return W3({p.x, p.y}, (float)(p.z * U) + bodyBias(i)); }
void keepValid(int i) {
    const std::string id = i >= 0 ? A.design.bodies[i].id : (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size() ? A.design.bodies[A.sel.idx].id : "");
    if (id != M.part) { M.sel.clear(); M.faces.clear(); M.part = id; }
    if (i < 0) return;
    int n = (int)A.design.bodies[i].mesh.v.size();
    M.sel.erase(std::remove_if(M.sel.begin(), M.sel.end(), [n](int v) { return v < 0 || v >= n; }), M.sel.end());
    int nf = (int)A.design.bodies[i].mesh.f.size();
    M.faces.erase(std::remove_if(M.faces.begin(), M.faces.end(), [nf](int f) { return f < 0 || f >= nf; }), M.faces.end());
}
void hover(int i, bool overUI) {
    M.hoverV = M.hoverF = -1;
    if (i < 0 || overUI) return;
    const MeshDef& m = A.design.bodies[i].mesh;
    Vector2 mp = GetMousePosition();
    float best = 1e9f;
    for (int v = 0; v < (int)m.v.size(); v++) {   // a corner within 8 px; of corners on top of each other, the nearest
        Vector3 w = vw(i, m.v[v]);
        float d = Vector2Distance(toScreen(w), mp);
        if (d > 8) continue;
        float score = std::floor(d / 3) * 1000 + Vector3Distance(A.cam.position, w) / U;
        if (score < best) { best = score; M.hoverV = v; }
    }
    if (M.hoverV >= 0) return;
    std::vector<int> tri, faceOf;
    meshTriangles(m, tri, &faceOf);
    Ray r = mouseRay();
    float bt = 1e30f;
    for (size_t t = 0; t + 2 < tri.size(); t += 3) {
        Vector3 a = vw(i, m.v[tri[t]]), b = vw(i, m.v[tri[t + 1]]), c = vw(i, m.v[tri[t + 2]]);
        RayCollision rc = GetRayCollisionTriangle(r, a, b, c);
        if (!rc.hit) rc = GetRayCollisionTriangle(r, a, c, b);
        if (rc.hit && rc.distance < bt) { bt = rc.distance; M.hoverF = faceOf[t / 3]; }
    }
}
void beginDrag(int i, P3d anchor) {
    M.dragging = true; M.live = false; M.start = GetMousePosition(); M.zDrag = 0;
    M.orig = A.design.bodies[i].mesh.v; M.anchor = anchor;
    Vector3 v = mouseRay().direction;   // the plane to drag in: the one the view looks at most
    float vx = std::fabs(v.x), vy = std::fabs(v.y), vz = std::fabs(v.z);
    M.plane = vz >= vx && vz >= vy ? 2 : vy >= vx ? 1 : 0;
}
void applyDrag(int i) {
    BodyDef& d = A.design.bodies[i];
    P3d delta{0, 0, 0};
    if (IsKeyDown(KEY_Z)) {   // in depth: the mouse's up and down
        float perPx = (A.ortho ? A.orthoH : A.dist * 2 * std::tan(20 * DEG2RAD)) / std::max(1, A.vh) / U;
        M.zDrag -= GetMouseDelta().y * perPx;
        delta.z = snapv(M.zDrag, A.snapOn && !IsKeyDown(KEY_LEFT_ALT));
    } else {
        Vector3 pw = vw(i, M.anchor);
        Ray r = mouseRay();
        int ax = M.plane;
        float dir = ax == 0 ? r.direction.x : ax == 1 ? r.direction.y : r.direction.z;
        float org = ax == 0 ? r.position.x : ax == 1 ? r.position.y : r.position.z;
        float at = ax == 0 ? pw.x : ax == 1 ? pw.y : pw.z;
        if (std::fabs(dir) < 0.05f) return;
        float t = (at - org) / dir;
        if (t <= 0) return;
        Vector3 hit = Vector3Add(r.position, Vector3Scale(r.direction, t));
        V2 mm = toMM(hit);
        double z = (hit.z - bodyBias(i)) / U;
        bool snap = A.snapOn && !IsKeyDown(KEY_LEFT_ALT);
        delta = {snapv(mm.x - M.anchor.x, snap), snapv(mm.y - M.anchor.y, snap), snapv(z - M.anchor.z, snap)};
        if (ax == 0) delta.x = 0;
        if (ax == 1) delta.y = 0;
        if (ax == 2) delta.z = M.zDrag;
    }
    d.mesh.v = M.orig;
    for (int v : M.sel) if (v < (int)d.mesh.v.size()) { d.mesh.v[v].x += delta.x; d.mesh.v[v].y += delta.y; d.mesh.v[v].z += delta.z; }
}
P3d middleOf(const MeshDef& m, const std::vector<int>& vs) {
    P3d c{0, 0, 0};
    for (int v : vs) { c.x += m.v[v].x; c.y += m.v[v].y; c.z += m.v[v].z; }
    if (!vs.empty()) { c.x /= vs.size(); c.y /= vs.size(); c.z /= vs.size(); }
    return c;
}
}  // namespace

// ---------------------------------------------------------------- move arrows on the picked corners (Sprocket-style)
namespace {
Arrows MA;
std::vector<P3d> MAorig;
bool meshArrowsShown(int i) { return i >= 0 && !M.sel.empty() && !M.dragging && CFG.editArrows; }
void meshArrowsSetup(int i) {
    MA.c = vw(i, middleOf(A.design.bodies[i].mesh, M.sel));
    MA.ax[0] = {1, 0, 0}; MA.ax[1] = {0, 1, 0}; MA.ax[2] = {0, 0, 1};
    MA.label[0] = "Along"; MA.label[1] = "Up"; MA.label[2] = "Depth";
    arrowsFit(MA, [](Vector3 w) { return toScreen(w); }, 64);
}
// true while the arrows have the mouse
bool meshArrowsInput(int i, bool overUI) {
    ToScreen ts = [](Vector3 w) { return toScreen(w); };
    if (MA.axis >= 0) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || i < 0) { MA.axis = -1; commit(); return true; }
        MeshDef& m = A.design.bodies[i].mesh;
        bool snap = (A.snapOn && !IsKeyDown(KEY_LEFT_ALT)) || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        double mm = snapv(arrowsDrag(MA, GetMousePosition()) / U, snap);
        MA.moved = mm * U;
        if (MAorig.size() != m.v.size()) return true;
        m.v = MAorig;
        for (int v : M.sel) if (v >= 0 && v < (int)m.v.size()) {
            if (MA.axis == 0) m.v[v].x += mm;
            if (MA.axis == 1) m.v[v].y -= mm;   // (y runs down in the designer's numbers)
            if (MA.axis == 2) m.v[v].z += mm;
        }
        return true;
    }
    MA.hot = -1;
    if (!meshArrowsShown(i)) return false;
    meshArrowsSetup(i);
    MA.hot = overUI ? -1 : arrowsPick(MA, ts, GetMousePosition());
    if (MA.hot < 0 || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    MAorig = A.design.bodies[i].mesh.v;
    arrowsBegin(MA, MA.hot, ts, GetMousePosition());
    return true;
}
}  // namespace

std::vector<int>& meshSel() { return M.sel; }
std::vector<int>& meshFaces() { return M.faces; }
double& meshExtrudeBy() { return M.extrude; }

void meshMakeFromSelected() {
    if (A.sel.kind != 1 || A.sel.idx < 0 || A.sel.idx >= (int)A.design.bodies.size()) return;
    BodyDef& b = A.design.bodies[A.sel.idx];
    if (b.round || b.tiltX != 0 || b.tiltY != 0) { toast("Turned and leaned parts can't be made 3D meshes yet."); return; }
    fixShape(b);
    meshFromPart(b);
    if (!meshShaped(b)) { toast("This part couldn't be made a 3D mesh."); return; }
    M.sel.clear(); M.faces.clear(); M.part = b.id;
    A.tool = Tool::Mesh;
    commit();
    toast(TextFormat("%s is a 3D mesh now (%d corners): drag corners anywhere, Shift-click faces and press E to push them out. Back to a flat part in the panel undoes it.", b.name.c_str(), (int)b.mesh.v.size()));
}
void meshFlatten() {
    int i = selMesh();
    if (i < 0) return;
    BodyDef& b = A.design.bodies[i];
    b.mesh = {}; b.meshRef.clear();
    M.sel.clear(); M.faces.clear();
    commit();
}
void meshAddRing(int axis, double at) {
    int i = selMesh();
    if (i < 0) return;
    std::vector<int> added = meshCut(A.design.bodies[i].mesh, axis, at);
    if (added.empty()) { toast("That plane doesn't cross the part."); return; }
    M.sel = added; M.faces.clear(); M.part = A.design.bodies[i].id;
    commit();
}
void meshExtrudePicked() {
    int i = selMesh();
    if (i < 0 || M.faces.empty()) { toast("Pick a face first (click it; Shift-click picks more)."); return; }
    MeshDef& m = A.design.bodies[i].mesh;
    M.sel = meshExtrudeFaces(m, M.faces, M.extrude);   // (the picked faces keep their places in the list: still picked)
    M.part = A.design.bodies[i].id;
    commit();
    toast(TextFormat("Pushed out %d face%s by %.1f mm. Drag them to pull further.", (int)M.faces.size(), M.faces.size() == 1 ? "" : "s", M.extrude));
}
void meshRemovePicked() {
    int i = selMesh();
    if (i < 0 || M.sel.empty()) return;
    if (!meshRemoveVerts(A.design.bodies[i].mesh, M.sel)) { toast("A part needs at least four corners."); return; }
    M.sel.clear(); M.faces.clear();
    commit();
}
void meshJoinPicked() {
    int i = selMesh();
    if (i < 0 || M.sel.size() < 2) { toast("Pick two or more corners to join (Shift-click them), then press M."); return; }
    MeshDef& m = A.design.bodies[i].mesh;
    int n = (int)M.sel.size();
    int v = meshMergeVerts(m, M.sel, middleOf(m, M.sel));
    if (v < 0) { toast("Joining those would leave too little of the part."); return; }
    M.sel = {v}; M.faces.clear();
    commit();
    toast(TextFormat("Joined %d corners into one, half way between them.", n));
}
void meshConnectPicked() {
    int i = selMesh();
    if (i < 0 || M.sel.size() != 2) { toast("Pick exactly two corners on the same face, then press J to draw an edge between them."); return; }
    if (!meshConnect(A.design.bodies[i].mesh, M.sel[0], M.sel[1])) { toast("Those two corners aren't across a face from each other (or they're already joined by an edge)."); return; }
    M.faces.clear();
    commit();
    toast("A new edge across the face: it's two faces now.");
}
void meshScalePicked(double sx, double sy, double sz) {
    int i = selMesh();
    if (i < 0 || M.sel.empty()) return;
    MeshDef& m = A.design.bodies[i].mesh;
    P3d c = middleOf(m, M.sel);
    for (int v : M.sel) { auto& p = m.v[v]; p.x = c.x + (p.x - c.x) * sx; p.y = c.y + (p.y - c.y) * sy; p.z = c.z + (p.z - c.z) * sz; }
    commit();
}
void meshMovePicked(double dx, double dy, double dz) {
    int i = selMesh();
    if (i < 0 || M.sel.empty()) return;
    for (int v : M.sel) { auto& p = A.design.bodies[i].mesh.v[v]; p.x += dx; p.y += dy; p.z += dz; }
    commit();
}

void meshInput(bool overUI) {
    int i = selMesh();
    keepValid(i);
    if (M.dragging) {
        if (i < 0) { M.dragging = false; return; }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (!M.live && Vector2Distance(GetMousePosition(), M.start) >= 5 * CFG.uiScale) M.live = true;
            if (M.live) applyDrag(i);
            if (IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { A.design.bodies[i].mesh.v = M.orig; M.dragging = false; }
            return;
        }
        M.dragging = false;
        if (M.live) commit();
        return;
    }
    if (meshArrowsInput(i, overUI)) { M.hoverV = M.hoverF = -1; return; }
    hover(i, overUI);
    bool typing = ImGui::GetIO().WantCaptureKeyboard;
    if (i >= 0 && !typing) {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (IsKeyPressed(KEY_E)) { meshExtrudePicked(); return; }
        if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) { meshRemovePicked(); return; }
        if (ctrl && IsKeyPressed(KEY_A)) { M.sel.clear(); for (int v = 0; v < (int)A.design.bodies[i].mesh.v.size(); v++) M.sel.push_back(v); return; }
        if (IsKeyPressed(KEY_M)) { meshJoinPicked(); return; }
        if (IsKeyPressed(KEY_J)) { meshConnectPicked(); return; }
        if (IsKeyPressed(KEY_ESCAPE)) { M.sel.clear(); M.faces.clear(); return; }
    }
    if (overUI || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (i >= 0 && M.hoverV >= 0) {
        auto it = std::find(M.sel.begin(), M.sel.end(), M.hoverV);
        if (shift) { if (it != M.sel.end()) { M.sel.erase(it); return; } M.sel.push_back(M.hoverV); }
        else if (it == M.sel.end()) M.sel = {M.hoverV};
        M.faces.clear();
        beginDrag(i, A.design.bodies[i].mesh.v[M.hoverV]);
        return;
    }
    if (i >= 0 && M.hoverF >= 0) {
        const MeshDef& m = A.design.bodies[i].mesh;
        auto fit = std::find(M.faces.begin(), M.faces.end(), M.hoverF);
        if (shift && fit != M.faces.end()) {   // Shift-click a picked face: let it go (and the corners no other picked face has)
            M.faces.erase(fit);
            M.sel.clear();
            for (int f : M.faces) for (int v : m.f[f]) if (std::find(M.sel.begin(), M.sel.end(), v) == M.sel.end()) M.sel.push_back(v);
            return;
        }
        if (shift) { for (int v : m.f[M.hoverF]) if (std::find(M.sel.begin(), M.sel.end(), v) == M.sel.end()) M.sel.push_back(v); M.faces.push_back(M.hoverF); }
        else if (fit == M.faces.end()) { M.sel = m.f[M.hoverF]; M.faces = {M.hoverF}; }
        beginDrag(i, meshFaceCentre(m, M.hoverF));
        return;
    }
    // anywhere else: pick the part there (or nothing)
    auto hits = bodiesUnderMouse();
    if (!hits.empty()) { A.sel = {1, hits.front().body, -1}; A.selVs.clear(); }
    else if (!shift) { A.sel = {}; }
    M.sel.clear(); M.faces.clear();
    if (!hits.empty() && !meshShaped(A.design.bodies[hits.front().body])) meshMakeFromSelected();   // (straight into 3D: no extra step)
}

void drawMeshTool() {
    if (A.mode != Mode::Build || A.tool != Tool::Mesh) return;
    int i = selMesh();
    Vector2 mp = GetMousePosition();
    if (i < 0) {
        const char* t = "Click a part to shape it in 3D";
        DrawText(t, (int)mp.x + 16, (int)mp.y + 16, 14, Color{255, 205, 60, 220});
        return;
    }
    const MeshDef& m = A.design.bodies[i].mesh;
    std::vector<Vector2> s(m.v.size());
    for (size_t v = 0; v < m.v.size(); v++) s[v] = toScreen(vw(i, m.v[v]));
    auto wash = [&](int f, Color c) {
        std::vector<int> tri, faceOf;
        meshTriangles(m, tri, &faceOf);
        for (size_t t = 0; t + 2 < tri.size(); t += 3) {
            if (faceOf[t / 3] != f) continue;
            DrawTriangle(s[tri[t]], s[tri[t + 1]], s[tri[t + 2]], c);
            DrawTriangle(s[tri[t]], s[tri[t + 2]], s[tri[t + 1]], c);
        }
    };
    if (M.hoverF >= 0 && M.hoverF < (int)m.f.size() && !M.dragging) wash(M.hoverF, Color{255, 205, 60, 40});
    for (int f : M.faces) if (f >= 0 && f < (int)m.f.size()) wash(f, Color{255, 205, 60, 70});
    std::set<std::pair<int, int>> edges;
    for (auto& f : m.f) for (size_t k = 0; k < f.size(); k++) { int a = f[k], b = f[(k + 1) % f.size()]; edges.insert({std::min(a, b), std::max(a, b)}); }
    std::set<int> picked(M.sel.begin(), M.sel.end());
    for (auto& [a, b] : edges) {
        bool hot = picked.count(a) && picked.count(b);
        DrawLineEx(s[a], s[b], hot ? 2.f : 1.f, hot ? Color{255, 205, 60, 255} : Color{120, 200, 255, 150});
    }
    for (size_t v = 0; v < m.v.size(); v++) {
        bool on = picked.count((int)v), hot = (int)v == M.hoverV;
        float r = hot ? 6.f : on ? 4.5f : 3.5f;
        DrawCircleV(s[v], r, on ? Color{255, 205, 60, 255} : Color{20, 24, 32, 220});
        DrawCircleLinesV(s[v], r, on ? Color{255, 240, 180, 255} : Color{120, 200, 255, 255});
    }
    if (MA.axis >= 0 || meshArrowsShown(i)) { meshArrowsSetup(i); arrowsDraw(MA, [](Vector3 w) { return toScreen(w); }, "mm", 1 / U); }
    if (M.dragging && M.live && !M.sel.empty()) {
        P3d c = middleOf(m, M.sel);
        std::string t = TextFormat("X %.1f  Y %.1f  Z %.1f mm", c.x, -c.y, c.z);
        int tw = MeasureText(t.c_str(), 16) + 10;
        DrawRectangle((int)mp.x - tw - 12, (int)mp.y - 34, tw, 22, Color{20, 24, 32, 220});
        DrawText(t.c_str(), (int)mp.x - tw - 7, (int)mp.y - 31, 16, Color{255, 205, 60, 255});
    } else if (M.hoverV >= 0) DrawText("Corner: click to pick, drag to move (Shift adds)", (int)mp.x + 14, (int)mp.y + 14, 13, Color{255, 205, 60, 220});
    else if (M.hoverF >= 0) DrawText("Face: click to pick, Shift-click to pick more, drag to move; E pushes them out", (int)mp.x + 14, (int)mp.y + 14, 13, Color{255, 205, 60, 220});
}
