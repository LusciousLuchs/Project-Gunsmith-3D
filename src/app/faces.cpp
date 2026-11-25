// Gunsmith3D - the Faces tool: point at any face of a part in 3D (the near or far face, or any side wall: top,
// bottom, ends) and drag to push or pull it; E extrudes it (a side wall gets two new corners and is drawn out; the
// near or far face grows a new piece, welded on, that you pull out to any depth).
#include "app/faces.hpp"
#include "app/app.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "app/units.hpp"
#include "carve.hpp"
#include "raylib.h"
#include "raymath.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>

Ray mouseRay();   // (view.cpp)

// ---------------------------------------------------------------- typed numbers (0.9.0, the building rework)
// While a face is pushed, or right after, typing a number and Enter sets how far, exactly (like SketchUp's
// measurements box). `gLast` keeps the push just done: the design before it, the face, extrude or not.
namespace {
std::string gTyped;
struct LastPush { bool on = false; std::string before, after; App::FacePick f; bool extrude = false; double amount = 0; } gLast;
bool typedValue(double& out) { return !gTyped.empty() && parseQ(Q::Length, gTyped.c_str(), out); }
// digits, a point, a minus, unit letters, a space or a slash (fractions), Backspace; true when Enter was pressed
bool typeKeys() {
    if (ImGui::GetIO().WantCaptureKeyboard) return false;
    for (int c = GetCharPressed(); c; c = GetCharPressed()) {
        bool digit = c >= '0' && c <= '9';
        if (!digit && gTyped.empty() && c != '-' && c != '.') continue;   // (a letter key alone is a shortcut, not a number)
        if (digit || c == '.' || c == '-' || c == '/' || c == ' ' || c == '"' || (c >= 'a' && c <= 'z')) if (gTyped.size() < 24) gTyped += (char)c;
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !gTyped.empty()) gTyped.pop_back();
    return (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && !gTyped.empty();
}
}  // namespace

namespace {
bool editable(const BodyDef& d) { return !d.round && !carve::tilted(d) && !meshShaped(d) && d.pts.size() >= 3; }

V2 outward(const BodyDef& d, int e) {   // the side wall's outward direction in the side view (design mm)
    const auto& P = d.pts;
    V2 a = P[e], b = P[(e + 1) % P.size()];
    double dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
    if (l < 1e-9) return {0, 0};
    V2 n{dy / l, -dx / l}, mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
    if (sim::inPoly(mid.x + n.x * 0.05, mid.y + n.y * 0.05, P)) n = {-n.x, -n.y};
    return n;
}
Vector3 worldNormal(const BodyDef& d, const App::FacePick& f) {
    if (f.kind == 1) return {0, 0, 1};
    if (f.kind == 2) return {0, 0, -1};
    V2 n = outward(d, f.edge);
    return {(float)n.x, (float)-n.y, 0};   // (world y is up, design y is down)
}
float nearZ(int i) { return bodyZ(i) + bodyDepth(A.design.bodies[i]) / 2; }
float farZ(int i) { return bodyZ(i) - bodyDepth(A.design.bodies[i]) / 2; }
// a corner of a part shaped in depth, in the world (its own depth, plus the part's small drawing offset)
Vector3 ringW(int i, const P3d& p) { return W3({p.x, p.y}, (float)(p.z * U) + bodyBias(i)); }
void ensureDepth(BodyDef& d) {   // start shaping it in depth: every corner where it is now
    if (depthShaped(d)) return;
    size_t n = d.pts.size();
    d.depthOff.assign(n, V2{0, 0}); d.depthZn.assign(n, 0.0); d.depthZf.assign(n, 0.0); d.depthRef = d.pts;
}

// the nearest face under the ray; only parts in `only` when it's set
App::FacePick pick(Ray r, int only = -1) {
    App::FacePick best; float bt = 1e30f;
    if (only < -1) return best;
    for (int i = 0; i < (int)A.design.bodies.size(); i++) {
        if (only >= 0 && i != only) continue;
        const BodyDef& d = A.design.bodies[i];
        if (!editable(d) || !partPickable(A.design, d)) continue;   // (a hidden, locked or faint layer's faces can't be pushed)
        if (depthShaped(d)) {   // its faces are pieces between the corners: test each triangle
            std::vector<P3d> v; std::vector<int> tri, tag;
            depthTriangles(d, v, tri, &tag);
            for (size_t t = 0; t < tri.size(); t += 3) {
                Vector3 a = ringW(i, v[tri[t]]), b = ringW(i, v[tri[t + 1]]), c = ringW(i, v[tri[t + 2]]);
                RayCollision rc = GetRayCollisionTriangle(r, a, c, b);   // (world y is up: the winding turns round)
                if (!rc.hit) rc = GetRayCollisionTriangle(r, a, b, c);
                if (!rc.hit || rc.distance >= bt) continue;
                int g = tag[t / 3];
                bt = rc.distance; best = {i, g == -1 ? 1 : g == -2 ? 2 : 0, g >= 0 ? g : -1, rc.point};
            }
            continue;
        }
        float z1 = nearZ(i), z0 = farZ(i);
        auto outline = outlineOf(d);
        for (int k : {1, 2}) {   // the near and far faces, from the side they face
            if ((k == 1) != (r.direction.z < 0)) continue;
            float zz = k == 1 ? z1 : z0;
            float t = (zz - r.position.z) / r.direction.z;
            if (t <= 0 || t >= bt) continue;
            Vector3 w = Vector3Add(r.position, Vector3Scale(r.direction, t));
            V2 mm = toMM(w);
            if (!sim::inPoly(mm.x, mm.y, outline)) continue;
            if ((!d.carves.empty() || carve::hollowOn(d)) && !solidAt(i, mm, depthMM(i, zz) + (k == 1 ? -0.05 : 0.05))) continue;   // a cut opens there: no face
            bt = t; best = {i, k, -1, w};
        }
        int n = (int)d.pts.size();
        for (int e = 0; e < n; e++) {   // the side walls, along the drawn edges
            App::FacePick f{i, 0, e, {}};
            Vector3 nw = worldNormal(d, f);
            float den = Vector3DotProduct(r.direction, nw);
            if (den >= -1e-4f) continue;   // facing away
            Vector3 a = W3(d.pts[e], 0), b = W3(d.pts[(e + 1) % n], 0);
            float t = Vector3DotProduct(Vector3Subtract({a.x, a.y, r.position.z}, r.position), nw) / den;
            if (t <= 0 || t >= bt) continue;
            Vector3 w = Vector3Add(r.position, Vector3Scale(r.direction, t));
            if (w.z < z0 - 1e-4f || w.z > z1 + 1e-4f) continue;
            float ex = b.x - a.x, ey = b.y - a.y, l2 = ex * ex + ey * ey;
            if (l2 < 1e-12f) continue;
            float s = ((w.x - a.x) * ex + (w.y - a.y) * ey) / l2;
            if (s < 0 || s > 1) continue;
            bt = t; f.at = w; best = f;
        }
    }
    return best;
}

// Which way "out" is on the screen: pixels per millimetre along the face's outward direction. A face that looks
// at you (or away) has no clear way on the screen: then up is out, at the view's scale.
Vector2 outOnScreen(const App::FacePick& f) {
    const BodyDef& d = A.design.bodies[f.body];
    Vector3 n = worldNormal(d, f);
    Vector3 view = Vector3Normalize(Vector3Subtract(A.cam.position, f.at));
    Vector2 s0 = toScreen(f.at), s1 = toScreen(Vector3Add(f.at, Vector3Scale(n, U)));   // a millimetre out, on screen
    Vector2 v = Vector2Subtract(s1, s0);
    if (std::fabs(Vector3DotProduct(n, view)) > 0.8f || v.x * v.x + v.y * v.y < 0.09f) {
        float perPx = (A.ortho ? A.orthoH : A.dist * 2 * std::tan(20 * DEG2RAD)) / std::max(1, A.vh) / U;
        return {0, -1 / perPx};
    }
    return v;
}
// how far (mm) the mouse has pushed along the face's outward direction since the drag began
double pushed(const App::FacePick& f) {
    Vector2 v = outOnScreen(f), m = Vector2Subtract(GetMousePosition(), A.faceStart);
    float l2 = v.x * v.x + v.y * v.y;
    return l2 > 0 ? (m.x * v.x + m.y * v.y) / l2 : 0;
}

void applyPush(double p) {
    auto& f = A.faceDrag;
    if (f.body < 0 || f.body >= (int)A.design.bodies.size()) return;
    BodyDef& d = A.design.bodies[f.body];
    if (f.kind == 0) {
        int n = (int)d.pts.size();
        if ((int)A.faceOrig.size() != n) return;
        // the wall's direction from the shape as it was when the drag began
        BodyDef was = d; was.pts = A.faceOrig;
        V2 nn = outward(was, f.edge);
        d.pts = A.faceOrig;
        for (int v : {f.edge, (f.edge + 1) % n}) { d.pts[v].x += nn.x * p; d.pts[v].y += nn.y * p; }
    } else {
        double t0 = A.faceExtruded ? 0 : A.faceOrigThick;
        double t = std::max(0.5, t0 + p);
        d.thick = t;
        double edgeZ = A.faceExtruded ? A.faceOrigZ : 0;   // extruded: grows out from the face it started on
        if (A.faceExtruded) d.z = edgeZ + (f.kind == 1 ? t / 2 : -t / 2);
        else d.z = A.faceOrigZ + (f.kind == 1 ? (t - t0) / 2 : -(t - t0) / 2);
        d.carves = A.faceOrigCarves;
        carve::rethick(d, A.faceOrigThick);
    }
}

double pushNow() {   // how far: the number typed, else the mouse
    double v;
    if (typedValue(v)) return v;
    return snapv(pushed(A.faceDrag), A.snapOn);
}
void apply() { applyPush(pushNow()); }
App::FacePick gBeganOn; bool gBeganExtrude = false;   // the face the push began on (an extrusion moves to its new piece)

void begin(const App::FacePick& f, bool follow, bool extrude) {
    gBeganOn = f; gBeganExtrude = extrude; gTyped.clear();
    BodyDef& d0 = A.design.bodies[f.body];
    A.faceDrag = f; A.faceDragging = true; A.faceFollow = follow; A.faceExtruded = false; A.faceExtrudedWall = false; A.faceLive = false;
    A.faceStart = GetMousePosition();
    if (extrude && f.kind == 0) {   // a side wall: two new corners where the edge was, drawn out as a new wall
        BodyDef& d = d0;
        fixShape(d);
        int n = (int)d.pts.size();
        if (n + 2 > MAX_CORNERS) { cornerLimitToast(); }
        else if (d.magazine && d.gen.on) toast("This magazine's shape comes from its settings: turn off its generated shape to extrude it.");
        else {
            int v = f.edge, at = v + 1;
            V2 a = d.pts[v], b = d.pts[(v + 1) % n];
            d.pts.insert(d.pts.begin() + at, {a, b});
            d.rad.insert(d.rad.begin() + at, {0.0, 0.0});
            d.bulge.insert(d.bulge.begin() + at, {0.0, 0.0});
            d.bulge[v] = 0;
            A.faceDrag.edge = at;
            A.faceExtrudedWall = true;
        }
    } else if (extrude) {   // the near or far face: a new piece grows out of it, welded on
        BodyDef c = d0;
        if (d0.weld.empty()) d0.weld = "w-" + uid();
        c.id = uid(); c.name = d0.name + " (extruded)"; c.weld = d0.weld;
        c.carves.clear(); c.mirror.clear(); c.magazine = false; c.magFixed = false; c.gen.on = false; c.barrel.on = false;
        c.depthOff.clear(); c.depthZn.clear(); c.depthZf.clear(); c.depthRef.clear();   // (a plain slab, whatever shape the face had)
        c.mass = 0; c.ctrl = "none"; c.ghost = false;
        double face = f.kind == 1 ? d0.z + d0.thick / 2 : d0.z - d0.thick / 2;
        c.thick = 0.5; c.z = face + (f.kind == 1 ? 0.25 : -0.25);
        A.design.bodies.push_back(c);
        A.faceDrag.body = (int)A.design.bodies.size() - 1;
        A.faceExtruded = true;
        A.faceOrigZ = face;
    }
    BodyDef& d = A.design.bodies[A.faceDrag.body];
    A.faceOrig = d.pts; A.faceOrigThick = d.thick; A.faceOrigCarves = d.carves;
    if (!A.faceExtruded) A.faceOrigZ = d.z;
    A.sel = {1, A.faceDrag.body, -1}; A.selVs.clear(); A.selCarve = -1;
}

void finish(bool keep) {
    A.faceDragging = false; A.faceFollow = false;
    double amount = pushNow();
    gTyped.clear();
    if (keep && !A.faceLive && !A.faceFollow && !A.faceExtruded && !A.faceExtrudedWall) {   // a click, not a push: nothing changed
        Design back;
        if (designFromJson(A.lastSnap, back)) { int sel = A.sel.idx; A.design = back; A.sel = {1, sel, -1}; }
        return;
    }
    if (!keep) {   // put everything back as it was
        Design back;
        if (designFromJson(A.lastSnap, back)) A.design = back;
        if (A.sel.idx >= (int)A.design.bodies.size()) A.sel = {};
        A.faceExtruded = false;
        return;
    }
    if (A.faceExtruded && A.design.bodies[A.faceDrag.body].thick <= 0.5 + 1e-9) {   // not pulled out: nothing made
        finish(false);
        return;
    }
    if (A.faceExtrudedWall) {   // a side wall drawn out: its new face is picked (push it further), and its corners (scale them to taper it)
        int n = (int)A.design.bodies[A.faceDrag.body].pts.size(), e = A.faceDrag.edge;
        A.sel = {1, A.faceDrag.body, -1};
        A.selVs = {e, (e + 1) % n};
        A.facePicked = A.faceDrag;
        toast("Extruded. Its new face is picked: drag it again to push it further, or drag its corners (or Scale them) to taper it.");
    }
    A.faceExtruded = false; A.faceExtrudedWall = false;
    std::string before = A.lastSnap;
    commit();
    gLast = {true, before, A.lastSnap, gBeganOn, gBeganExtrude, amount};
}
// do the last push again from where it began, by exactly `v` mm (it replaces the push in the undo history)
void redoLast(double v) {
    if (!gLast.on || A.lastSnap != gLast.after) return;
    Design back;
    if (!designFromJson(gLast.before, back)) return;
    A.design = back;
    if (!A.hist.empty() && A.hist.back() == gLast.before) A.hist.pop_back();
    A.lastSnap = gLast.before;
    LastPush keep = gLast;
    begin(keep.f, false, keep.extrude);
    A.faceLive = true;
    applyPush(v);
    gTyped = typedQ(Q::Length, v);   // (so finish() records this amount)
    finish(true);
    if (!gLast.on) gLast = keep;   // (a zero extrusion makes nothing)
}
}  // namespace

bool facesActive() { return A.faceDragging; }

namespace {
// the selected part's corners on both faces, on the screen
struct CornerDot { Vector2 s; int idx; bool far; };
std::vector<CornerDot> cornerDots() {
    std::vector<CornerDot> out;
    bool sel = A.tool == Tool::Select && A.mode == Mode::Build;
    if ((A.tool != Tool::Faces && !sel) || A.sel.kind != 1 || A.sel.idx < 0 || A.sel.idx >= (int)A.design.bodies.size()) return out;
    const BodyDef& d = A.design.bodies[A.sel.idx];
    if (!editable(d)) return out;
    std::vector<P3d> N, F;
    depthRings(d, N, F);
    // the Select tool has its own corners on the face toward you: here only the other face's (the Faces tool: both)
    bool camFront = A.cam.position.z >= bodyZ(A.sel.idx);
    // looking straight at the side (the flat view, or near it): the hidden face's corners sit exactly behind the
    // visible ones, so they're left out, or a click on a corner could pick the one at the back
    Vector3 fwd = Vector3Normalize(Vector3Subtract(A.cam.target, A.cam.position));
    bool headOn = A.ortho || std::fabs(fwd.z) > 0.97f;
    for (size_t k = 0; k < N.size(); k++) {
        bool nearN = camFront, nearF = !camFront;   // which face is toward you
        if ((!sel || !camFront) && !(headOn && !nearN)) out.push_back({toScreen(ringW(A.sel.idx, N[k])), (int)k, false});
        if ((!sel || camFront) && !(headOn && !nearF)) out.push_back({toScreen(ringW(A.sel.idx, F[k])), (int)k, true});
    }
    return out;
}
void cornerApply() {
    auto& c = A.cornerDrag;
    if (c.body < 0 || c.body >= (int)A.design.bodies.size()) return;
    BodyDef& d = A.design.bodies[c.body];
    if (!depthShaped(d) || c.idx >= (int)d.pts.size()) return;
    bool zKey = IsKeyDown(KEY_Z);
    if (zKey) {   // in depth: the mouse's up and down
        float perPx = (A.ortho ? A.orthoH : A.dist * 2 * std::tan(20 * DEG2RAD)) / std::max(1, A.vh) / U;
        A.cornerZDrag -= GetMouseDelta().y * perPx;
        double z = snapv(A.cornerZDrag, A.snapOn);
        (c.far ? d.depthZf : d.depthZn)[c.idx] = A.cornerOrigZ + z;
        return;
    }
    // in the plane the view looks at, through the corner: from the side x and y, from the top x and depth,
    // from the front or back y and depth
    std::vector<P3d> N, F;
    depthRings(d, N, F);
    const P3d& p = c.far ? F[c.idx] : N[c.idx];
    Vector3 pw = ringW(c.body, p);
    Ray r = mouseRay();
    int ax = A.cornerPlane;   // the axis the plane faces
    float dir = ax == 0 ? r.direction.x : ax == 1 ? r.direction.y : r.direction.z;
    float org = ax == 0 ? r.position.x : ax == 1 ? r.position.y : r.position.z;
    float at = ax == 0 ? pw.x : ax == 1 ? pw.y : pw.z;
    if (std::fabs(dir) < 0.05f) return;   // looking along the plane: no sure point
    float t = (at - org) / dir;
    if (t <= 0) return;
    Vector3 hit = Vector3Add(r.position, Vector3Scale(r.direction, t));
    V2 mm = toMM(hit);
    double depth = (hit.z - bodyBias(c.body)) / U;
    if (ax == 0) mm.x = p.x;
    if (ax == 1) mm.y = p.y;
    if (ax == 2) depth = p.z;
    mm = {snapv(mm.x, A.snapOn), snapv(mm.y, A.snapOn)};
    if (ax != 2) {   // the corner's depth from the face it belongs to
        double half = std::max(0.5, d.thick) / 2, face = c.far ? d.z - half : d.z + half;
        (c.far ? d.depthZf : d.depthZn)[c.idx] = snapv(depth - face, A.snapOn);
    }
    if (c.far) d.depthOff[c.idx] = {mm.x - d.pts[c.idx].x, mm.y - d.pts[c.idx].y};
    else {   // the near corner moves; its far partner stays where it is
        V2 farP{d.pts[c.idx].x + d.depthOff[c.idx].x, d.pts[c.idx].y + d.depthOff[c.idx].y};
        d.pts[c.idx] = mm;
        d.depthOff[c.idx] = {farP.x - mm.x, farP.y - mm.y};
        d.depthRef = d.pts;
    }
}
}  // namespace

static void pushTestHook() {   // GUNSMITH_PUSHTEST="edge,mm[,x]": push the selected part's side wall roughly, then type mm (x: extrude)
    static int frames = 0;
    const char* e = getenv("GUNSMITH_PUSHTEST");
    if (!e || ++frames != 20 || A.sel.kind != 1 || A.sel.idx < 0) return;
    int edge = 0; double mm = 0; char x = 0;
    sscanf(e, "%d,%lf,%c", &edge, &mm, &x);
    auto box = [](const BodyDef& d) { double a = 1e9, b = -1e9, c = 1e9, f = -1e9; for (auto& p : d.pts) { a = std::min(a, p.x); b = std::max(b, p.x); c = std::min(c, p.y); f = std::max(f, p.y); } return std::array<double, 4>{a, b, c, f}; };
    auto b0 = box(A.design.bodies[A.sel.idx]);
    size_t n0 = A.design.bodies.size(), h0 = A.hist.size();
    begin({A.sel.idx, 0, edge, {}}, false, x == 'x');
    A.faceLive = true; applyPush(7.3); finish(true);
    auto b1 = box(A.design.bodies[A.sel.idx]);
    redoLast(mm);
    auto b2 = box(A.design.bodies[A.sel.idx]);
    printf("PUSHTEST before x %.2f..%.2f y %.2f..%.2f | dragged x %.2f..%.2f y %.2f..%.2f | typed %.2f: x %.2f..%.2f y %.2f..%.2f | parts %zu->%zu corners %zu undo +%zu\n",
           b0[0], b0[1], b0[2], b0[3], b1[0], b1[1], b1[2], b1[3], mm, b2[0], b2[1], b2[2], b2[3], n0, A.design.bodies.size(), A.design.bodies[A.sel.idx].pts.size(), A.hist.size() - h0);
}
void facesInput(bool overUI) {
    pushTestHook();
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (A.cornerDragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (!A.faceLive && Vector2Distance(GetMousePosition(), A.faceStart) >= 5 * CFG.uiScale) A.faceLive = true;
            if (A.faceLive) cornerApply();
            return;
        }
        A.cornerDragging = false;
        commit();
        return;
    }
    A.cornerHover = {};
    if (!overUI && !A.faceDragging) {   // the selected part's corners come first: they sit on its faces
        Vector2 m = GetMousePosition();
        float best = 1e9f;   // within 8 px; of dots on top of each other, the one nearest the eye
        if (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size()) {
            std::vector<P3d> N, F; depthRings(A.design.bodies[A.sel.idx], N, F);
            for (auto& cd : cornerDots()) {
                float dd = Vector2Distance(cd.s, m);
                if (dd > 8) continue;
                float eye = Vector3Distance(A.cam.position, ringW(A.sel.idx, cd.far ? F[cd.idx] : N[cd.idx])) / U;
                float score = std::floor(dd / 3) * 1000 + eye;   // a few pixels nearer wins, then the nearer to the eye
                if (score < best) { best = score; A.cornerHover = {A.sel.idx, cd.idx, cd.far}; }
            }
        }
        if (A.cornerPicked && !(A.sel.kind == 1 && A.sel.idx == A.cornerDrag.body)) A.cornerPicked = false;
        if (A.cornerHover.body >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const auto& hv = A.cornerHover;
            if (A.tool != Tool::Faces && !(A.cornerPicked && A.cornerDrag.body == hv.body && A.cornerDrag.idx == hv.idx && A.cornerDrag.far == hv.far)) {
                A.cornerDrag = hv; A.cornerPicked = true; A.facePicked = {};   // picked: its X, Y, Z show in the panel
                return;
            }
            BodyDef& d = A.design.bodies[A.sel.idx];
            fixShape(d);
            ensureDepth(d);
            A.cornerDrag = A.cornerHover; A.cornerDragging = true; A.cornerZDrag = 0; A.faceStart = GetMousePosition(); A.faceLive = false;
            const auto& c = A.cornerDrag;
            A.cornerOrigZ = (c.far ? d.depthZf : d.depthZn)[c.idx];
            Vector3 v = mouseRay().direction;   // the plane to drag in: the one the view looks at most
            float vx = std::fabs(v.x), vy = std::fabs(v.y), vz = std::fabs(v.z);
            A.cornerPlane = vz >= vx && vz >= vy ? 2 : vy >= vx ? 1 : 0;
            return;
        }
        if (A.cornerHover.body >= 0) { A.faceHover = {}; return; }
    }
    if (A.faceDragging) {
        // a click that wobbles a little isn't a push: nothing moves until the mouse has gone a few pixels
        if (!A.faceFollow && !A.faceLive && Vector2Distance(GetMousePosition(), A.faceStart) >= 5 * CFG.uiScale) A.faceLive = true;
        bool enter = typeKeys();
        if (!gTyped.empty()) A.faceLive = true;
        if (A.faceFollow || A.faceLive) apply();
        if (enter) { finish(true); return; }
        bool cancel = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_ESCAPE);
        if (cancel) { finish(false); return; }
        if (A.faceFollow ? IsMouseButtonPressed(MOUSE_BUTTON_LEFT) : !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) finish(true);
        return;
    }
    if (A.tool == Tool::Faces) {   // a number typed now changes the push just done
        if (!gLast.on || A.lastSnap != gLast.after) { gLast.on = false; gTyped.clear(); }
        else if (typeKeys()) { double v; if (typedValue(v)) redoLast(v); gTyped.clear(); }
        if (IsKeyPressed(KEY_ESCAPE)) gTyped.clear();
    }
    A.faceHover = overUI ? App::FacePick{} : pick(mouseRay(), A.tool == Tool::Faces ? -1 : (A.sel.kind == 1 ? A.sel.idx : -2));
    if (A.faceHover.body < 0) {
        if (A.tool == Tool::Faces && !overUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {   // a part with no faces to push (round, sculpted, leaned), or nothing
            auto hits = bodiesUnderMouse();
            A.facePicked = {}; A.cornerPicked = false; A.selVs.clear();
            if (hits.empty()) A.sel = {};
            else {
                A.sel = {1, hits[0].body, -1};
                toast("This part's faces can't be pushed (it's round, sculpted or leaned): change its size with the numbers beside it, or reshape it in More tools.");
            }
        }
        return;
    }
    if (IsKeyPressed(KEY_E) && !ImGui::GetIO().WantCaptureKeyboard) { begin(A.faceHover, true, true); return; }
    if (A.tool == Tool::Faces && !overUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        // push and pull: press on any face of any part and drag it straight away (Ctrl or Shift: pull out a new piece)
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        A.sel = {1, A.faceHover.body, -1}; A.selVs.clear(); A.facePicked = A.faceHover; A.cornerPicked = false;
        begin(A.faceHover, false, shift || ctrl);
    }
}

// ---------------------------------------------------------------- the Select tool, Sprocket-style
// Every face of the selected part works as its outline's edges do: hover it, click to pick it, press it again and drag
// to push or pull it (Shift: extrude). The corners on its far face (circles) are picked and dragged the same way.
bool selectFacesInput(bool overUI) {
    if (A.tool != Tool::Select || A.mode != Mode::Build) return false;
    bool selPart = A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size();
    if (!selPart || A.facePicked.body != A.sel.idx) A.facePicked = {};
    if (A.cornerDragging || A.faceDragging) { facesInput(overUI); return true; }
    A.faceHover = {};
    if (!selPart || overUI || A.dragKind != 0 || !editable(A.design.bodies[A.sel.idx])) { A.cornerHover = {}; return false; }
    // the far face's corners
    A.cornerHover = {};
    Vector2 m = GetMousePosition();
    float best = 1e9f;
    for (auto& cd : cornerDots()) {
        float dd = Vector2Distance(cd.s, m);
        if (dd < 8 && dd < best) { best = dd; A.cornerHover = {A.sel.idx, cd.idx, cd.far}; }
    }
    if (A.cornerHover.body >= 0) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) facesInput(overUI);   // (picks it, or starts dragging a picked one)
        return true;
    }
    A.faceHover = pick(mouseRay(), A.sel.idx);
    return false;
}
void facePushStart(const App::FacePick& f, bool extrude) { begin(f, false, extrude); }

void drawFaces() {
    if (A.mode != Mode::Build) return;
    {   // the selected part's corners on both faces, joined front to back
        auto dots = cornerDots();
        for (size_t k = 0; k + 1 < dots.size(); k += 2) DrawLineEx(dots[k].s, dots[k + 1].s, 1.2f, Color{255, 205, 60, 110});
        for (auto& cd : dots) {
            bool hot = A.cornerHover.body >= 0 && A.cornerHover.idx == cd.idx && A.cornerHover.far == cd.far;
            bool dragged = (A.cornerDragging || A.cornerPicked) && A.cornerDrag.idx == cd.idx && A.cornerDrag.far == cd.far;
            float r = hot || dragged ? 6.f : 4.f;
            Color c = cd.far ? Color{110, 200, 255, 255} : Color{255, 205, 60, 255};
            if (cd.far) { DrawCircleV(cd.s, r, Color{20, 24, 32, 220}); DrawCircleLinesV(cd.s, r, c); }
            else { DrawRectangleRec({cd.s.x - r, cd.s.y - r, 2 * r, 2 * r}, Color{20, 24, 32, 220}); DrawRectangleLinesEx({cd.s.x - r, cd.s.y - r, 2 * r, 2 * r}, 1.5f, c); }
            if (hot && !A.cornerDragging) {
                const char* t = cd.far ? "Back corner: click to pick it, then drag it" : "Front corner: click to pick it, then drag it";
                DrawText(t, (int)cd.s.x + 10, (int)cd.s.y - 22, 14, c);
            }
        }
        if (A.cornerDragging) {
            const BodyDef& d = A.design.bodies[A.cornerDrag.body];
            if (depthShaped(d) && A.cornerDrag.idx < (int)d.pts.size()) {
                std::vector<P3d> N, F; depthRings(d, N, F);
                const P3d& p = A.cornerDrag.far ? F[A.cornerDrag.idx] : N[A.cornerDrag.idx];
                std::string t = TextFormat("X %.1f  Y %.1f  Z %.1f mm", p.x, -p.y, p.z);
                Vector2 m = GetMousePosition();
                int tw = MeasureText(t.c_str(), 16) + 10;
                DrawRectangle((int)m.x - tw - 12, (int)m.y - 34, tw, 22, Color{20, 24, 32, 220});
                DrawText(t.c_str(), (int)m.x - tw - 7, (int)m.y - 31, 16, Color{255, 205, 60, 255});
            }
        }
    }
    bool selTool = A.tool == Tool::Select && !A.faceDragging;
    if (selTool && A.facePicked.body < 0 && A.faceHover.body >= 0 && A.faceHover.body < (int)A.design.bodies.size()) {
        // the Select tool: the face under the mouse, just outlined (a click picks it)
        const App::FacePick& h = A.faceHover;
        const BodyDef& d = A.design.bodies[h.body];
        std::vector<P3d> RN, RF; depthRings(d, RN, RF);
        Color thin{255, 205, 60, 200}, wash{255, 205, 60, 34};
        if (h.kind == 0 && h.edge >= 0 && h.edge < (int)RN.size()) {
            int e2 = (h.edge + 1) % (int)RN.size();
            Vector2 q[4] = {toScreen(ringW(h.body, RN[h.edge])), toScreen(ringW(h.body, RN[e2])), toScreen(ringW(h.body, RF[e2])), toScreen(ringW(h.body, RF[h.edge]))};
            DrawTriangle(q[0], q[1], q[2], wash); DrawTriangle(q[0], q[2], q[1], wash);
            DrawTriangle(q[0], q[2], q[3], wash); DrawTriangle(q[0], q[3], q[2], wash);
            for (int k = 0; k < 4; k++) DrawLineEx(q[k], q[(k + 1) % 4], 2.f, thin);
        } else if (h.kind != 0) {
            const auto& R = h.kind == 1 ? RN : RF;
            for (size_t k = 0; k < R.size(); k++) DrawLineEx(toScreen(ringW(h.body, R[k])), toScreen(ringW(h.body, R[(k + 1) % R.size()])), 2.f, thin);
        }
        Vector2 m = GetMousePosition();
        DrawText("Click to pick this face", (int)m.x + 14, (int)m.y + 14, 13, thin);
    }
    const App::FacePick& f = A.faceDragging ? A.faceDrag : selTool ? A.facePicked : A.faceHover.body >= 0 ? A.faceHover : A.facePicked;
    if (f.body < 0 || f.body >= (int)A.design.bodies.size()) return;
    if (!A.faceDragging && A.tool != Tool::Faces && A.tool != Tool::Select) return;
    const BodyDef& d = A.design.bodies[f.body];
    Color fill{255, 205, 60, 70}, line{255, 205, 60, 255};
    std::vector<P3d> RN, RF;
    depthRings(d, RN, RF);
    if (f.kind == 0) {
        int n = (int)d.pts.size();
        if (f.edge < 0 || f.edge >= n || (int)RN.size() != n) return;
        int e2 = (f.edge + 1) % n;
        Vector2 q[4] = {toScreen(ringW(f.body, RN[f.edge])), toScreen(ringW(f.body, RN[e2])), toScreen(ringW(f.body, RF[e2])), toScreen(ringW(f.body, RF[f.edge]))};
        DrawTriangle(q[0], q[1], q[2], fill); DrawTriangle(q[0], q[2], q[1], fill);
        DrawTriangle(q[0], q[2], q[3], fill); DrawTriangle(q[0], q[3], q[2], fill);
        for (int k = 0; k < 4; k++) DrawLineEx(q[k], q[(k + 1) % 4], 2, line);
    } else {
        if (depthShaped(d)) {   // its corners on that face
            const auto& R = f.kind == 1 ? RN : RF;
            for (size_t k = 0; k < R.size(); k++) DrawLineEx(toScreen(ringW(f.body, R[k])), toScreen(ringW(f.body, R[(k + 1) % R.size()])), 2.5f, line);
        } else {
            float z = f.kind == 1 ? nearZ(f.body) : farZ(f.body);
            auto o = outlineOf(d);
            for (size_t k = 0; k < o.size(); k++) DrawLineEx(toScreen(W3(o[k], z)), toScreen(W3(o[(k + 1) % o.size()], z)), 2.5f, line);
        }
    }
    {   // an arrow out of the face: the way to move the mouse to pull it out
        Vector2 v = outOnScreen(f), s0 = toScreen(f.at);
        float l = std::sqrt(v.x * v.x + v.y * v.y);
        if (l > 1e-6f) {
            Vector2 u{v.x / l, v.y / l}, tip{s0.x + u.x * 46, s0.y + u.y * 46};
            DrawLineEx(s0, tip, 3, line);
            Vector2 side{-u.y * 7, u.x * 7}, back{tip.x - u.x * 12, tip.y - u.y * 12};
            DrawTriangle(tip, {back.x + side.x, back.y + side.y}, {back.x - side.x, back.y - side.y}, line);
            DrawTriangle(tip, {back.x - side.x, back.y - side.y}, {back.x + side.x, back.y + side.y}, line);
            DrawCircleV(s0, 4, line);
        }
    }
    if (A.faceDragging) {   // how far
        const BodyDef& b = A.design.bodies[A.faceDrag.body];
        std::string t = !gTyped.empty() ? "Typed: " + gTyped + "   (Enter)"
                      : A.faceDrag.kind == 0 ? std::string(TextFormat("%+.1f mm   (or type a number)", snapv(pushed(A.faceDrag), A.snapOn))) : std::string(TextFormat("%.1f mm thick   (or type a number)", b.thick));
        Vector2 m = GetMousePosition();   // above and left of the cursor, clear of the part's name
        int tw = MeasureText(t.c_str(), 16) + 10, x = (int)m.x - tw - 12, y = (int)m.y - 34;
        DrawRectangle(x, y, tw, 22, Color{20, 24, 32, 220});
        DrawText(t.c_str(), x + 5, y + 3, 16, line);
    }
}

// the measurements box at the bottom right of the view (Push and pull): the last push's amount, or what's being typed
void drawPushBox() {
    if (A.mode != Mode::Build || A.tool != Tool::Faces) return;
    bool live = A.faceDragging;
    bool last = gLast.on && A.lastSnap == gLast.after;
    std::string label, value;
    if (live) { label = "Pushing"; value = gTyped.empty() ? fmtQ(Q::Length, snapv(pushed(A.faceDrag), A.snapOn)) : gTyped; }
    else if (!gTyped.empty()) { label = "Make it"; value = gTyped + "   Enter"; }
    else if (last) { label = gLast.extrude ? "Pulled out" : "Pushed"; value = fmtQ(Q::Length, gLast.amount) + "   type to change"; }
    else { label = "Push and pull"; value = "drag any face, then type a number"; }
    int fs = 16, pad = 8;
    std::string t = label + ":  " + value;
    int tw = MeasureText(t.c_str(), fs) + 2 * pad;
    int x = A.vw - tw - 16, y = TOOLBAR_H + A.vh - 84;
    DrawRectangleRounded({(float)x, (float)y, (float)tw, (float)(fs + 2 * pad)}, 0.25f, 6, Color{20, 24, 32, 225});
    DrawRectangleRoundedLines({(float)x, (float)y, (float)tw, (float)(fs + 2 * pad)}, 0.25f, 6, Color{238, 122, 32, (unsigned char)(live || !gTyped.empty() ? 255 : 120)});
    DrawText(t.c_str(), x + pad, y + pad, fs, Color{235, 235, 240, 255});
}
