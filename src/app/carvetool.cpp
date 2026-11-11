// Gunsmith3D - the Carve tool: click a part's surface from any side to cut into it, or drag to size the cut.
#include "app/app.hpp"
#include "app/carvetool.hpp"
#include "app/view.hpp"
#include "app/fonts.hpp"
#include "app/settings.hpp"
#include "app/units.hpp"
#include "carve.hpp"
#include "raymath.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

// In the designer every part sits at its drawn place: design mm <-> world is a plain change of units.
static Vector3 toWorld(int i, double x, double y, double zRel) {
    return {(float)(x * U), (float)(-y * U), bodyZ(i) + (float)(zRel * U)};
}

bool carvePick(int& body, Vector3& p, Vector3& n) {
    Ray r = mouseRay();
    float best = 1e30f;
    body = -1;
    for (int pass = 0; pass < 2 && body < 0; pass++)  // see-through parts (X-ray) only if nothing solid is there
    for (size_t i = 0; i < A.design.bodies.size() && i < A.meshes.size(); i++) {
        if (!A.meshes[i].ok) continue;
        const BodyDef& d = A.design.bodies[i];
        if (A.cutaway && d.z - d.thick / 2 > 0.5) continue;      // cut away: not there to point at
        if (pass == 0 && A.xray && d.ghost) continue;
        V2 c = centroidOf(A.design.bodies[i]);
        Matrix M = MatrixTranslate((float)c.x * U, -(float)c.y * U, bodyZ((int)i));
        RayCollision h = GetRayCollisionMesh(r, A.meshes[i].mesh, M);
        if (h.hit && h.distance < best) { best = h.distance; body = (int)i; p = h.point; n = h.normal; }
    }
    if (body < 0) return false;
    n = Vector3Normalize(n);
    if (Vector3DotProduct(n, r.direction) > 0) n = Vector3Negate(n);  // the face we're looking at
    return true;
}

// ---------------------------------------------------------------- snapping
// A cut's entry point snaps along each design axis that lies in the face it goes into (x along the gun, y up and
// down, z across): to a line worth lining up with when the cursor is near one (the part's middle, another cut on the
// part, a corner of its outline), else to the grid step. Alt: no snapping. The lines it caught are drawn as guides.
struct CarveGuide { int axis; const char* what; };
static std::vector<CarveGuide> gGuides;
static int gGuideBody = -1;
static CarveDef gGuideCut;

static double coord(const CarveDef& c, int k) { return k == 0 ? c.at.x : k == 1 ? c.at.y : c.atZ; }
static void setCoord(CarveDef& c, int k, double v) { (k == 0 ? c.at.x : k == 1 ? c.at.y : c.atZ) = v; }
// the part's extent along an axis (x, y: its outline; z: its thickness about its middle)
static void extent(const BodyDef& b, int k, double& lo, double& hi) {
    if (k == 2) { lo = -b.thick / 2; hi = b.thick / 2; return; }
    lo = 1e30; hi = -1e30;
    for (auto& p : b.pts) { double v = k == 0 ? p.x : p.y; lo = std::min(lo, v); hi = std::max(hi, v); }
}
static void snapCarve(int i, CarveDef& c, int skip) {
    gGuides.clear(); gGuideBody = -1;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) return;
    const BodyDef& b = A.design.bodies[i];
    double d[3] = {c.dx, c.dy, c.dz}, L = std::sqrt(c.dx * c.dx + c.dy * c.dy + c.dz * c.dz);
    if (L < 1e-9) return;
    Vector2 s0 = toScreen(toWorld(i, c.at.x, c.at.y, c.atZ));
    for (int k = 0; k < 3; k++) {
        if (std::fabs(d[k] / L) > 0.15) continue;   // (this axis runs into the part, not across the face)
        // how many mm the snapping reaches: about 7 pixels on screen
        Vector2 s1 = toScreen(toWorld(i, c.at.x + (k == 0), c.at.y + (k == 1), c.atZ + (k == 2)));
        double ppm = std::max(1e-3, (double)Vector2Distance(s0, s1)), tol = std::clamp(7 / ppm, 0.05, 25.0);
        double v = coord(c, k), best = v, bd = tol;
        const char* what = nullptr;
        auto consider = [&](double t, const char* w) { if (std::fabs(t - v) < bd) { bd = std::fabs(t - v); best = t; what = w; } };
        double lo, hi;
        extent(b, k, lo, hi);
        if (hi >= lo) consider((lo + hi) / 2, "the middle");
        if (b.round && k < 2) { carve::RoundAxis ax = carve::roundAxis(b); if (std::fabs(k == 0 ? ax.dir.x : ax.dir.y) < 0.2) consider(k == 0 ? ax.o.x : ax.o.y, "the middle"); }
        for (int q = 0; q < (int)b.carves.size(); q++) if (q != skip) consider(coord(b.carves[q], k), "in line with a cut");
        if (k < 2) for (auto& p : b.pts) consider(k == 0 ? p.x : p.y, "in line with a corner");
        if (what) { setCoord(c, k, best); gGuides.push_back({k, what}); }
        else if (A.snapOn && gSnap > 0) setCoord(c, k, std::round(v / gSnap) * gSnap);
    }
    if (!gGuides.empty()) { gGuideBody = i; gGuideCut = c; }
}
// The guides: a line across the part's face through the cut, for each axis it lined up on
static void drawGuides() {
    if (gGuideBody < 0 || gGuideBody >= (int)A.design.bodies.size()) return;
    const BodyDef& b = A.design.bodies[gGuideBody];
    const CarveDef& c = gGuideCut;
    double d[3] = {c.dx, c.dy, c.dz}, L = std::sqrt(c.dx * c.dx + c.dy * c.dy + c.dz * c.dz);
    Color col{235, 90, 220, 230};
    for (auto& g : gGuides) {
        // along the other axis that lies in the face
        int m = -1;
        for (int k = 0; k < 3; k++) if (k != g.axis && std::fabs(d[k] / L) <= 0.15) m = k;
        if (m < 0) continue;
        double lo, hi;
        extent(b, m, lo, hi);
        double pad = (hi - lo) * 0.08 + 2;
        CarveDef a = c, e = c;
        setCoord(a, m, lo - pad); setCoord(e, m, hi + pad);
        DrawLineEx(toScreen(toWorld(gGuideBody, a.at.x, a.at.y, a.atZ)), toScreen(toWorld(gGuideBody, e.at.x, e.at.y, e.atZ)), 1.5f, col);
    }
    if (!gGuides.empty()) {
        Vector2 s = toScreen(toWorld(gGuideBody, c.at.x, c.at.y, c.atZ));
        std::string t = gGuides[0].what;
        if (gGuides.size() > 1 && std::string(gGuides[1].what) != t) t += ", " + std::string(gGuides[1].what);
        viewText(t.c_str(), s.x + 10, s.y - 22, 12, col);
    }
}

// The carve a press at (p, n) on part i makes: straight into that face, the tool's shape, size and depth.
static CarveDef carveAt(int i, Vector3 p, Vector3 n) {
    CarveDef c;
    c.shape = A.carveShape;
    c.w = A.carveW; c.h = c.shape == "circle" ? A.carveW : A.carveH;
    c.at = {p.x / U, -p.y / U};
    c.atZ = (p.z - bodyZ(i)) / U;
    c.dx = -n.x; c.dy = n.y; c.dz = -n.z;  // into the surface (design y points down)
    auto r3 = [](double& v) { if (std::fabs(v) < 1e-4) v = 0; };
    r3(c.dx); r3(c.dy); r3(c.dz);
    c.depth = A.carveThrough ? 0 : A.carveDepth;
    // into the end of a round part (a barrel, a rod), close to its middle: right on its centre line, so a bore lines up
    const BodyDef& d = A.design.bodies[i];
    if (d.round) {
        carve::RoundAxis ax = carve::roundAxis(d);
        double l = std::sqrt(c.dx * c.dx + c.dy * c.dy + c.dz * c.dz);
        if (l > 0 && std::fabs((c.dx * ax.dir.x + c.dy * ax.dir.y) / l) > 0.95) {
            double s = (c.at.x - ax.o.x) * ax.dir.x + (c.at.y - ax.o.y) * ax.dir.y;
            V2 onAxis{ax.o.x + ax.dir.x * s, ax.o.y + ax.dir.y * s};
            double off = std::hypot(std::hypot(c.at.x - onAxis.x, c.at.y - onAxis.y), c.atZ);
            if (off < std::max(1.5, ax.R * 0.35)) {
                double sgn = c.dx * ax.dir.x + c.dy * ax.dir.y > 0 ? 1 : -1;   // straight down the axis, the way it was going in
                c.at = onAxis; c.atZ = 0; c.dx = ax.dir.x * sgn; c.dy = ax.dir.y * sgn; c.dz = 0;
            }
        }
    }
    snapCarve(i, c, -1);
    return c;
}

// While dragging: the carve sized by how far the cursor has gone across the surface.
static CarveDef dragCarve() {
    CarveDef c = carveAt(A.carveBody, A.carveP, A.carveN);
    if (Vector2Distance(GetMousePosition(), A.pressPos) < 4) return c;
    Ray r = mouseRay();
    float den = Vector3DotProduct(r.direction, A.carveN);
    if (std::fabs(den) < 1e-5f) return c;
    float t = Vector3DotProduct(Vector3Subtract(A.carveP, r.position), A.carveN) / den;
    Vector3 q = Vector3Add(r.position, Vector3Scale(r.direction, t));
    Vector3 o = Vector3Subtract(q, A.carveP);
    carve::P3 off{o.x / U, -o.y / U, o.z / U}, u, v, d;
    carve::frame(c, u, v, d);
    double du = std::fabs(csg::dot(off, u)), dv = std::fabs(csg::dot(off, v));
    bool free = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    double step = free ? 0.1 : A.snapOn && gSnap > 0 ? gSnap : 0.5;
    auto snap = [&](double x) { return std::max(step, std::round(x / step) * step); };
    if (c.shape == "circle") c.w = c.h = snap(2 * std::hypot(du, dv));
    else { c.w = snap(2 * du); c.h = snap(2 * dv); }
    return c;
}

// The dot of one of the selected part's cuts under the cursor (-1: none)
static std::vector<Vector2> outline(int i, const CarveDef& c, int k);
static int cutDotUnderMouse() {
    if (A.sel.kind != 1 || A.sel.idx < 0 || A.sel.idx >= (int)A.design.bodies.size()) return -1;
    const BodyDef& b = A.design.bodies[A.sel.idx];
    Vector2 m = GetMousePosition();
    int best = -1; float bd = 9;
    for (int k = 0; k < (int)b.carves.size(); k++) {
        const CarveDef& c = b.carves[k];
        float dd = Vector2Distance(toScreen(toWorld(A.sel.idx, c.at.x, c.at.y, c.atZ)), m);
        if (dd < bd) { bd = dd; best = k; }
    }
    return best;
}
// Where the cursor meets the face a cut goes into (the plane through its entry point, across its direction), in
// design mm (x, y, z from the part's middle)
static bool onCutPlane(int i, const CarveDef& c, double out[3]) {
    Ray r = mouseRay();
    Vector3 o = toWorld(i, c.at.x, c.at.y, c.atZ), n{(float)c.dx, (float)-c.dy, (float)c.dz};
    float den = Vector3DotProduct(r.direction, n);
    if (std::fabs(den) < 1e-5f) return false;
    float t = Vector3DotProduct(Vector3Subtract(o, r.position), n) / den;
    Vector3 q = Vector3Add(r.position, Vector3Scale(r.direction, t));
    out[0] = q.x / U; out[1] = -q.y / U; out[2] = (q.z - bodyZ(i)) / U;
    return true;
}
static CarveDef movedCut() {
    CarveDef c = A.carveMoving;
    double p[3];
    if (Vector2Distance(GetMousePosition(), A.pressPos) >= 3 && onCutPlane(A.sel.idx, c, p)) {
        c.at.x = p[0] - A.carveGrab[0]; c.at.y = p[1] - A.carveGrab[1]; c.atZ = p[2] - A.carveGrab[2];
        snapCarve(A.sel.idx, c, A.carveMoveIdx);
    } else { gGuides.clear(); gGuideBody = -1; }
    return c;
}

void carveInput(bool overUI) {
    if (A.carveMove) {   // dragging a cut by its dot, across the face it goes into
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;
        A.carveMove = false;
        if (A.sel.kind != 1 || A.sel.idx < 0 || A.sel.idx >= (int)A.design.bodies.size()) return;
        auto& cs = A.design.bodies[A.sel.idx].carves;
        if (A.carveMoveIdx < 0 || A.carveMoveIdx >= (int)cs.size()) return;
        if (Vector2Distance(GetMousePosition(), A.pressPos) < 3) return;   // a click: it's just picked
        cs[A.carveMoveIdx] = movedCut();
        gGuides.clear(); gGuideBody = -1;
        commit();
        return;
    }
    if (A.carveDrag) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;
        A.carveDrag = false;
        if (A.carveBody < 0 || A.carveBody >= (int)A.design.bodies.size()) return;
        CarveDef c = dragCarve();
        A.carveW = c.w; if (c.shape != "circle") A.carveH = c.h;  // the next plain click uses this size
        auto& b = A.design.bodies[A.carveBody];
        b.carves.push_back(c);
        A.sel = {1, A.carveBody, -1};
        A.selCarve = (int)b.carves.size() - 1;
        commit();
        return;
    }
    if (overUI || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    int dot = cutDotUnderMouse();
    if (dot >= 0) {   // on a cut's dot: pick it, and a drag moves it
        A.selCarve = dot; A.carveMove = true; A.carveMoveIdx = dot;
        A.carveMoving = A.design.bodies[A.sel.idx].carves[dot];
        A.pressPos = GetMousePosition();
        double p[3];
        if (onCutPlane(A.sel.idx, A.carveMoving, p)) { A.carveGrab[0] = p[0] - A.carveMoving.at.x; A.carveGrab[1] = p[1] - A.carveMoving.at.y; A.carveGrab[2] = p[2] - A.carveMoving.atZ; }
        else A.carveGrab[0] = A.carveGrab[1] = A.carveGrab[2] = 0;
        return;
    }
    int i; Vector3 p, n;
    if (!carvePick(i, p, n)) { toast("Click on a part's surface to carve into it."); return; }
    A.carveDrag = true; A.carveBody = i; A.carveP = p; A.carveN = n;
    A.pressPos = GetMousePosition();
}

// The carve's outline on screen: where it goes in (k = 0), or where it ends (k = 1).
static std::vector<Vector2> outline(int i, const CarveDef& c, int k) {
    carve::P3 u, v, d;
    carve::frame(c, u, v, d);
    double deep = k == 0 ? 0 : (c.depth > 0 ? c.depth : 0);
    std::vector<Vector2> o;
    for (auto& q : carve::shape2D(c, 40)) {
        carve::P3 p = carve::P3{c.at.x, c.at.y, c.atZ} + u * q.x + v * q.y + d * deep;
        o.push_back(toScreen(toWorld(i, p.x, p.y, p.z)));
    }
    return o;
}
static void drawCarve(int i, const CarveDef& c, Color col, bool label) {
    auto a = outline(i, c, 0);
    for (size_t k = 0; k < a.size(); k++) DrawLineEx(a[k], a[(k + 1) % a.size()], 2, col);
    carve::P3 u, v, d;
    carve::frame(c, u, v, d);
    Vector2 s = toScreen(toWorld(i, c.at.x, c.at.y, c.atZ));
    if (c.depth > 0) {  // the far end, and the line in
        auto b = outline(i, c, 1);
        Color dim = col; dim.a = 110;
        for (size_t k = 0; k < b.size(); k++) DrawLineEx(b[k], b[(k + 1) % b.size()], 1, dim);
        carve::P3 e = carve::P3{c.at.x, c.at.y, c.atZ} + d * c.depth;
        DrawLineEx(s, toScreen(toWorld(i, e.x, e.y, e.z)), 1.5f, col);
    } else {
        carve::P3 e = carve::P3{c.at.x, c.at.y, c.atZ} + d * 8;
        DrawLineEx(s, toScreen(toWorld(i, e.x, e.y, e.z)), 1.5f, col);
    }
    DrawCircleV(s, 3, col);
    if (label) {
        std::string t = c.shape == "circle" && c.w == c.h ? fmtQ(Q::Length, c.w) : fmtQ(Q::Length, c.w) + " x " + fmtQ(Q::Length, c.h);
        t += c.depth > 0 ? ", " + fmtQ(Q::Length, c.depth) + " deep" : ", right through";
        viewText(t.c_str(), s.x + 10, s.y + 8, 12, col);
    }
}

std::string cutLabel(const CarveDef& c, int k) { return c.name.empty() ? "Cut " + std::to_string(k + 1) : c.name; }

// The selected part's cut under the cursor: on its dot, or inside where it goes in or where it ends, the smallest
// on screen winning (a slot inside a pocket picks the slot). -1 if none.
static float polyArea(const std::vector<Vector2>& o) {
    float a = 0;
    for (size_t k = 0; k < o.size(); k++) { const Vector2 &p = o[k], &q = o[(k + 1) % o.size()]; a += p.x * q.y - q.x * p.y; }
    return std::fabs(a) / 2;
}
static bool inside(const std::vector<Vector2>& o, Vector2 m) {
    bool in = false;
    for (size_t k = 0, j = o.size() - 1; k < o.size(); j = k++)
        if ((o[k].y > m.y) != (o[j].y > m.y) && m.x < (o[j].x - o[k].x) * (m.y - o[k].y) / (o[j].y - o[k].y) + o[k].x) in = !in;
    return in;
}
static int cutUnderMouse() {
    int dot = cutDotUnderMouse();
    if (dot >= 0) return dot;
    if (A.sel.kind != 1 || A.sel.idx < 0 || A.sel.idx >= (int)A.design.bodies.size()) return -1;
    const BodyDef& b = A.design.bodies[A.sel.idx];
    Vector2 m = GetMousePosition();
    int best = -1; float ba = 1e30f;
    for (int k = 0; k < (int)b.carves.size(); k++)
        for (int e = 0; e < 2; e++) {
            if (e == 1 && b.carves[k].depth <= 0) continue;
            auto o = outline(A.sel.idx, b.carves[k], e);
            float a = std::max(polyArea(o), 4.f);
            if (inside(o, m) && a < ba) { ba = a; best = k; }
        }
    return best;
}

bool cutPickInput(bool overUI) {
    if (!A.cutPick || overUI || A.mode != Mode::Build || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    int k = cutUnderMouse();
    if (k < 0) return false;   // not on a cut: the click does what it always does
    A.selCarve = k; A.carveMove = true; A.carveMoveIdx = k;
    A.carveMoving = A.design.bodies[A.sel.idx].carves[k];
    A.pressPos = GetMousePosition();
    double p[3];
    if (onCutPlane(A.sel.idx, A.carveMoving, p)) { A.carveGrab[0] = p[0] - A.carveMoving.at.x; A.carveGrab[1] = p[1] - A.carveMoving.at.y; A.carveGrab[2] = p[2] - A.carveMoving.atZ; }
    else A.carveGrab[0] = A.carveGrab[1] = A.carveGrab[2] = 0;
    return true;
}

void drawCarveOverlay() {
    auto& D = A.design;
    if (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)D.bodies.size()) {  // the part's carves: the selected one bright
        auto& b = D.bodies[A.sel.idx];
        bool moving = A.carveMove;
        bool picking = A.cutPick && A.mode == Mode::Build;
        int hot = picking && !moving && mouseInViewport() && !ImGui::GetIO().WantCaptureMouse ? cutUnderMouse() : -1;
        for (int k = 0; k < (int)b.carves.size(); k++)
            if (moving && k == A.carveMoveIdx) drawCarve(A.sel.idx, b.carves[k], Color{255, 190, 70, 70}, false);   // where it was
            else if (k == A.selCarve) drawCarve(A.sel.idx, b.carves[k], Color{255, 190, 70, 255}, true);
            else if (k == hot) drawCarve(A.sel.idx, b.carves[k], Color{120, 220, 255, 255}, false);
            else if (A.tool == Tool::Carve || picking) drawCarve(A.sel.idx, b.carves[k], Color{255, 190, 70, 90}, false);
        for (int k = 0; k < (int)b.carves.size(); k++)   // names, when picking: the selected one and the one under the cursor
            if ((picking && (k == hot || k == A.selCarve)) && !(moving && k == A.carveMoveIdx)) {
                const CarveDef& c = b.carves[k];
                Vector2 sp = toScreen(toWorld(A.sel.idx, c.at.x, c.at.y, c.atZ));
                viewText(cutLabel(c, k).c_str(), sp.x + 10, sp.y - 20, 13, k == A.selCarve ? Color{255, 190, 70, 255} : Color{120, 220, 255, 255});
            }
        if (picking && moving && A.tool != Tool::Carve) { drawCarve(A.sel.idx, movedCut(), Color{120, 220, 255, 255}, true); drawGuides(); }
    }
    if (A.tool != Tool::Carve || A.mode != Mode::Build) return;
    if (A.carveMove) { drawCarve(A.sel.idx, movedCut(), Color{120, 220, 255, 255}, true); drawGuides(); return; }
    if (A.carveDrag) { CarveDef c = dragCarve(); drawCarve(A.carveBody, c, Color{120, 220, 255, 255}, true); drawGuides(); return; }
    if (!mouseInViewport() || ImGui::GetIO().WantCaptureMouse) return;
    int dot = cutDotUnderMouse();
    if (dot >= 0) {   // a ring round the dot: it can be picked up and moved
        const CarveDef& c = A.design.bodies[A.sel.idx].carves[dot];
        Vector2 s = toScreen(toWorld(A.sel.idx, c.at.x, c.at.y, c.atZ));
        DrawCircleLines((int)s.x, (int)s.y, 8, Color{120, 220, 255, 255});
        viewText("Drag to move this cut", s.x + 12, s.y - 22, 12, Color{120, 220, 255, 255});
        return;
    }
    int i; Vector3 p, n;
    if (carvePick(i, p, n)) { drawCarve(i, carveAt(i, p, n), Color{120, 220, 255, 200}, true); drawGuides(); }
}
