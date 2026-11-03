// Gunsmith3D - the Faces tool: point at any face of a part in 3D (the near or far face, or any side wall: top,
// bottom, ends) and drag to push or pull it; E extrudes it (a side wall gets two new corners and is drawn out; the
// near or far face grows a new piece, welded on, that you pull out to any depth).
#include "app/faces.hpp"
#include "app/app.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "carve.hpp"
#include "raylib.h"
#include "raymath.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

Ray mouseRay();   // (view.cpp)

namespace {
bool editable(const BodyDef& d) { return !d.round && !carve::tilted(d) && d.pts.size() >= 3; }

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

// the nearest face under the ray; only parts in `only` when it's set
App::FacePick pick(Ray r, int only = -1) {
    App::FacePick best; float bt = 1e30f;
    if (only < -1) return best;
    for (int i = 0; i < (int)A.design.bodies.size(); i++) {
        if (only >= 0 && i != only) continue;
        const BodyDef& d = A.design.bodies[i];
        if (!editable(d)) continue;
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

void apply() {
    auto& f = A.faceDrag;
    if (f.body < 0 || f.body >= (int)A.design.bodies.size()) return;
    BodyDef& d = A.design.bodies[f.body];
    double p = snapv(pushed(f), A.snapOn);
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

void begin(const App::FacePick& f, bool follow, bool extrude) {
    BodyDef& d0 = A.design.bodies[f.body];
    A.faceDrag = f; A.faceDragging = true; A.faceFollow = follow; A.faceExtruded = false;
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
        }
    } else if (extrude) {   // the near or far face: a new piece grows out of it, welded on
        BodyDef c = d0;
        if (d0.weld.empty()) d0.weld = "w-" + uid();
        c.id = uid(); c.name = d0.name + " (extruded)"; c.weld = d0.weld;
        c.carves.clear(); c.mirror.clear(); c.magazine = false; c.magFixed = false; c.gen.on = false; c.barrel.on = false;
        c.mass = 0; c.ctrl = "none";
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
    A.faceExtruded = false;
    commit();
}
}  // namespace

bool facesActive() { return A.faceDragging; }

void facesInput(bool overUI) {
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (A.faceDragging) {
        apply();
        bool cancel = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_ESCAPE);
        if (cancel) { finish(false); return; }
        if (A.faceFollow ? IsMouseButtonPressed(MOUSE_BUTTON_LEFT) : !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) finish(true);
        return;
    }
    A.faceHover = overUI ? App::FacePick{} : pick(mouseRay(), A.tool == Tool::Faces ? -1 : (A.sel.kind == 1 ? A.sel.idx : -2));
    if (A.faceHover.body < 0) return;
    if (IsKeyPressed(KEY_E) && !ImGui::GetIO().WantCaptureKeyboard) { begin(A.faceHover, true, true); return; }
    if (A.tool == Tool::Faces && !overUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) begin(A.faceHover, false, shift);
}

void drawFaces() {
    if (A.mode != Mode::Build) return;
    const App::FacePick& f = A.faceDragging ? A.faceDrag : A.faceHover;
    if (f.body < 0 || f.body >= (int)A.design.bodies.size()) return;
    if (!A.faceDragging && A.tool != Tool::Faces) return;   // (the Select tool shows it only while E is extruding)
    const BodyDef& d = A.design.bodies[f.body];
    Color fill{255, 205, 60, 70}, line{255, 205, 60, 255};
    if (f.kind == 0) {
        int n = (int)d.pts.size();
        if (f.edge < 0 || f.edge >= n) return;
        V2 a = d.pts[f.edge], b = d.pts[(f.edge + 1) % n];
        Vector2 q[4] = {toScreen(W3(a, nearZ(f.body))), toScreen(W3(b, nearZ(f.body))), toScreen(W3(b, farZ(f.body))), toScreen(W3(a, farZ(f.body)))};
        DrawTriangle(q[0], q[1], q[2], fill); DrawTriangle(q[0], q[2], q[1], fill);
        DrawTriangle(q[0], q[2], q[3], fill); DrawTriangle(q[0], q[3], q[2], fill);
        for (int k = 0; k < 4; k++) DrawLineEx(q[k], q[(k + 1) % 4], 2, line);
    } else {
        float z = f.kind == 1 ? nearZ(f.body) : farZ(f.body);
        auto o = outlineOf(d);
        for (size_t k = 0; k < o.size(); k++) DrawLineEx(toScreen(W3(o[k], z)), toScreen(W3(o[(k + 1) % o.size()], z)), 2.5f, line);
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
        std::string t = A.faceDrag.kind == 0 ? TextFormat("%+.1f mm", snapv(pushed(A.faceDrag), A.snapOn)) : TextFormat("%.1f mm thick", b.thick);
        Vector2 m = GetMousePosition();   // above and left of the cursor, clear of the part's name
        int tw = MeasureText(t.c_str(), 16) + 10, x = (int)m.x - tw - 12, y = (int)m.y - 34;
        DrawRectangle(x, y, tw, 22, Color{20, 24, 32, 220});
        DrawText(t.c_str(), x + 5, y + 3, 16, line);
    }
}
