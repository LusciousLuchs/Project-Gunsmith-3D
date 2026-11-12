// Gunsmith3D - Camera, screen/world conversion and picking.
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "app/view.hpp"
#include "app/render.hpp"
#include "rlgl.h"
#include "carve.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <vector>

static Vector2 mouseLocal() { Vector2 m = GetMousePosition(); return {m.x, m.y - TOOLBAR_H}; }
bool mouseInViewport() { Vector2 m = GetMousePosition(); return m.x < A.vw && m.y > TOOLBAR_H && m.y < TOOLBAR_H + A.vh; }
Vector2 toScreen(Vector3 p) { Vector2 s = GetWorldToScreenEx(p, A.cam, A.vw, A.vh); return {s.x, s.y + TOOLBAR_H}; }
Ray mouseRay() { return GetScreenToWorldRayEx(mouseLocal(), A.cam, A.vw, A.vh); }
static bool rayZ(const Ray& r, float z, Vector3& out, float* tOut = nullptr) {
    if (std::fabs(r.direction.z) < 1e-6f) return false;
    float t = (z - r.position.z) / r.direction.z;
    if (t < 0) return false;
    out = Vector3Add(r.position, Vector3Scale(r.direction, t));
    if (tOut) *tOut = t;
    return true;
}
V2 mouseMM(float z) {
    // A ray almost along the drawing plane (looking at it edge on, or above the horizon) would put the point kilometres
    // away, or nowhere: keep the last good point instead, and never more than 5 m from the middle.
    static V2 last{0, 0};
    Ray r = mouseRay();
    Vector3 w;
    float along = std::fabs(Vector3DotProduct(Vector3Normalize(r.direction), {0, 0, 1}));
    if (along < 0.05f || !rayZ(r, z, w)) return last;
    V2 p = toMM(w);
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return last;
    p.x = std::clamp(p.x, -5000.0, 5000.0); p.y = std::clamp(p.y, -5000.0, 5000.0);
    last = p;
    return p;
}
V2 snapMM(V2 p) { return {snapv(p.x, A.snapOn), snapv(p.y, A.snapOn)}; }

// ---------------------------------------------------------------- picking
namespace {
// A part's slices through its depth (the outline less its cuts at each depth), kept until its shape changes
struct SliceCache { std::string key; std::vector<carve::Slice> sl; };
std::map<std::string, SliceCache> gSlices;
const std::vector<carve::Slice>& slicesOf(const BodyDef& d) {
    std::ostringstream k;
    k << d.z << ' ' << d.thick << ' ' << d.round << d.roundSides << ' ' << d.roundTurn << ' ' << d.roundSweep << ' ' << d.roundAxis << ' ' << d.tiltX << ' ' << d.tiltY;
    for (auto& p : d.pts) k << ',' << p.x << ' ' << p.y;
    for (double r : d.rad) k << 'r' << r;
    for (double b : d.bulge) k << 'b' << b;
    for (size_t q = 0; q < d.depthZn.size() && q < d.depthZf.size() && q < d.depthOff.size(); q++) k << 'D' << d.depthOff[q].x << ' ' << d.depthOff[q].y << ' ' << d.depthZn[q] << ' ' << d.depthZf[q];
    for (auto& c : d.carves) k << 'c' << c.shape << c.at.x << ' ' << c.at.y << ' ' << c.atZ << ' ' << c.dx << ' ' << c.dy << ' ' << c.dz << ' ' << c.spin << ' ' << c.w << ' ' << c.h << ' ' << c.depth;
    SliceCache& c = gSlices[d.id];
    std::string key = k.str();
    if (c.key != key) { c.key = key; c.sl = carve::slices(d); }
    return c.sl;
}
}  // namespace
double depthMM(int i, float worldZ) {
    const BodyDef& d = A.design.bodies[i];
    float bias = bodyZ(i) - (float)d.z * U;
    return (worldZ - bias) / U;
}
bool solidAt(int i, V2 mm, double zmm) {
    if (i < 0 || i >= (int)A.design.bodies.size()) return false;
    const BodyDef& d = A.design.bodies[i];
    const auto& sl = slicesOf(d);
    if (sl.empty()) return std::fabs(zmm - d.z) <= d.thick / 2 + 1e-3 && sim::inPoly(mm.x, mm.y, outlineOf(d));
    for (auto& s : sl) {
        if (zmm < s.z0 - 1e-3 || zmm > s.z1 + 1e-3) continue;
        bool in = false;
        if (s.outlines.empty()) in = sim::inPoly(mm.x, mm.y, outlineOf(d));
        else for (auto& o : s.outlines) if (sim::inPoly(mm.x, mm.y, o)) { in = true; break; }
        if (!in) continue;
        bool cut = false;
        for (auto& c : s.cav) if (carve::inConvex(c, mm)) { cut = true; break; }
        if (!cut) return true;
    }
    return false;
}
std::vector<Hit> bodiesUnderMouse() {  // Build mode: design parts
    std::vector<Hit> hits;
    Ray r = mouseRay();
    for (int i = 0; i < (int)A.design.bodies.size(); i++) {
        const BodyDef& d = A.design.bodies[i];
        const auto& sl = slicesOf(d);
        if (sl.empty() || std::fabs(r.direction.z) < 0.02f) {   // a plain slab (or seen edge on): its front face
            float z = bodyFront(i), t;
            Vector3 w;
            if (!rayZ(r, z, w, &t)) continue;
            V2 mm = toMM(w);
            if (sim::inPoly(mm.x, mm.y, outlineOf(d))) hits.push_back({i, t, mm});
            continue;
        }
        // cut, turned or shaped in depth: walk the ray through the depth it spans and take the first point with
        // material there, so a hole shows (and picks) whatever is behind it
        double z0 = sl.front().z0, z1 = sl.front().z1;
        for (auto& s : sl) { z0 = std::min(z0, s.z0); z1 = std::max(z1, s.z1); }
        float bias = bodyZ(i) - (float)d.z * U;
        float ta = ((float)z0 * U + bias - r.position.z) / r.direction.z, tb = ((float)z1 * U + bias - r.position.z) / r.direction.z;
        if (ta > tb) std::swap(ta, tb);
        if (tb <= 0) continue;
        ta = std::max(ta, 0.f);
        const int N = 48;
        for (int k = 0; k <= N; k++) {
            float t = ta + (tb - ta) * (k + 0.5f) / (N + 1);
            Vector3 w = Vector3Add(r.position, Vector3Scale(r.direction, t));
            V2 mm = toMM(w);
            if (solidAt(i, mm, depthMM(i, w.z))) { hits.push_back({i, t, mm}); break; }
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.t < b.t; });  // nearest first
    return hits;
}
TestHit testPick() {  // Test mode: a bolt handle, else the nearest movable live body or loose round under the cursor
    TestHit best; float bt = 1e30f;
    if (!A.S) return best;
    Ray r = mouseRay();
    Vector2 m = GetMousePosition();
    auto list = renderList();
    for (auto& rb : list) {  // handles first: they stick out in front of everything
        BoltHandle h;
        if (!rb.sb || rb.sb->fixed || !boltHandle(rb, h)) continue;
        Vector2 a = toScreen(h.base), b = toScreen(h.tip);
        double d2; sim::closestOnSeg({m.x, m.y}, {a.x, a.y}, {b.x, b.y}, &d2);
        if (Vector2Distance(m, b) < 16 || (std::sqrt(d2) < 8 && Vector2Distance(m, a) > 12)) {
            best.b = rb.sb; best.tmpl = rb.tmpl; best.handle = true;
            return best;
        }
    }
    for (auto& b : A.S->bodies) {  // loose rounds, drawn at their depth (on the bore plane unless thrown aside)
        if (!b.isRound || b.gone) continue;
        Vector3 w; float t;
        if (!rayZ(r, (float)(b.z * 1000) * U, w, &t) || t >= bt) continue;
        V2 mm = toMM(w), l = sim::lpt(b, {mm.x / 1000, mm.y / 1000});
        double best2 = 1e9;  // rounds are small: accept a click within 2.5 mm of the outline
        for (size_t i = 0; i < b.lp.size(); i++) { double d2; sim::closestOnSeg(l, b.lp[i], b.lp[(i + 1) % b.lp.size()], &d2); best2 = std::min(best2, d2); }
        if (sim::inPoly(l.x, l.y, b.lp) || best2 < 0.0025 * 0.0025) { bt = t - 0.5f; best = {&b, -1, mm, 0, false}; }
    }
    for (auto& rb : list) {
        if (!rb.sb || rb.sb->fixed) continue;
        const BodyDef& d = A.design.bodies[rb.tmpl];
        float z = bodyFront(rb.tmpl), t;
        Vector3 w;
        if (!rayZ(r, z, w, &t) || t >= bt) continue;
        V2 mm = toMM(w);
        V2 l = sim::lpt(*rb.sb, {mm.x / 1000, mm.y / 1000});
        V2 c = centroidOf(d);
        if (sim::inPoly(l.x * 1000 + c.x, l.y * 1000 + c.y, outlineOf(d))) { bt = t; best = {rb.sb, rb.tmpl, mm, z, false}; }
    }
    return best;
}
const Handle* handleAt(Vector2 m) {
    for (auto& h : A.handles)
        if (Vector2Distance(h.s, m) < 9) return &h;
    return nullptr;
}

// ---------------------------------------------------------------- camera
void fitView() {
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (auto& b : A.design.bodies) for (auto& p : b.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    if (x0 > x1) { x0 = 0; x1 = 200; y0 = 0; y1 = 100; }
    A.target = {(float)(x0 + x1) / 2 * U, -(float)(y0 + y1) / 2 * U, 0};
    float span = (float)std::max(x1 - x0, (y1 - y0) * 1.6) * U;
    A.dist = span * 1.2f + 8;
    A.orthoH = (float)std::max((y1 - y0) * 1.5, (x1 - x0) * 1.15 * A.vh / std::max(1, A.vw)) * U + 2;
}
void updateCamera() {
    // one orbit for both projections: orthographic just drops the perspective, so it works from any angle.
    // The camera's own up keeps straight-down and straight-up views steady.
    float sy = std::sin(A.yaw), cy = std::cos(A.yaw), sp = std::sin(A.pitch), cp = std::cos(A.pitch);
    A.cam.up = {-sp * sy, cp, -sp * cy};
    A.cam.target = A.target;
    Vector3 dir{sy * cp, sp, cy * cp};
    if (A.ortho) {
        A.cam.projection = CAMERA_ORTHOGRAPHIC;
        A.cam.fovy = A.orthoH;
        A.cam.position = Vector3Add(A.target, Vector3Scale(dir, 200));
    } else {
        A.cam.projection = CAMERA_PERSPECTIVE;
        A.cam.fovy = 40;
        A.cam.position = Vector3Add(A.target, Vector3Scale(dir, A.dist));
    }
}
void setOrtho(bool on) {  // keep what you see the same size either way
    float k = 2 * std::tan(20 * DEG2RAD);
    if (on && !A.ortho) A.orthoH = std::clamp(A.dist * k, 2.f, 400.f);
    if (!on && A.ortho) A.dist = std::clamp(A.orthoH / k, 3.f, 600.f);
    A.ortho = on;
}
void snapView(View v) {
    const float H = PI / 2;
    switch (v) {
    case View::Right: A.yaw = 0; A.pitch = 0; break;
    case View::Left: A.yaw = PI; A.pitch = 0; break;
    case View::Top: A.yaw = 0; A.pitch = H; break;
    case View::Bottom: A.yaw = 0; A.pitch = -H; break;
    case View::Front: A.yaw = H; A.pitch = 0; break;
    case View::Back: A.yaw = -H; A.pitch = 0; break;
    }
    setOrtho(true);
}
void sideView() { snapView(View::Right); }
void beginViewport3D() {
    BeginMode3D(A.cam);
    float sx = (float)GetRenderWidth() / GetScreenWidth(), sy = (float)GetRenderHeight() / GetScreenHeight();
    rlViewport((int)(A.viewX * sx), (int)(GetRenderHeight() - (TOOLBAR_H + A.vh) * sy), (int)(A.vw * sx), (int)(A.vh * sy));
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    double aspect = (double)A.vw / std::max(1, A.vh);
    if (A.ortho) {
        double top = A.cam.fovy / 2.0, right = top * aspect;
        rlOrtho(-right, right, -top, top, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    } else {
        double top = RL_CULL_DISTANCE_NEAR * std::tan(A.cam.fovy * 0.5 * DEG2RAD), right = top * aspect;
        rlFrustum(-right, right, -top, top, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    }
    rlMatrixMode(RL_MODELVIEW);
    float vp[3] = {A.cam.position.x, A.cam.position.y, A.cam.position.z};
    SetShaderValue(G.lit, G.locView, vp, SHADER_UNIFORM_VEC3);
}
void endViewport3D() {
    EndMode3D();
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
}
