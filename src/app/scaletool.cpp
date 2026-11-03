// Gunsmith3D - the Scale tool (see scaletool.hpp).
#include "app/scaletool.hpp"
#include "app/app.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "app/fonts.hpp"
#include "app/units.hpp"
#include "carve.hpp"
#include "raymath.h"
#include <algorithm>
#include <cmath>

void scaleParts(Design& D, const std::vector<int>& parts, V2 c, double sx, double sy, double sz, const std::vector<int>& corners) {
    auto sc = [&](V2& p) { p = {c.x + (p.x - c.x) * sx, c.y + (p.y - c.y) * sy}; };
    if (!corners.empty() && parts.size() == 1) {   // just these corners of one part
        BodyDef& d = D.bodies[parts[0]];
        fixShape(d);
        freezeMagazine(d, D.breech.cart);   // (a fitted magazine keeps the shape you give it)
        for (int v : corners) if (v >= 0 && v < (int)d.pts.size()) sc(d.pts[v]);
        for (auto& p : d.pts) { p.x = std::round(p.x * 100) / 100; p.y = std::round(p.y * 100) / 100; }
        return;
    }
    double area = std::fabs(sx * sy), lin = std::sqrt(area);
    double z0 = 1e9, z1 = -1e9;   // the piece's middle in depth: parts beside each other spread apart with the thickness
    for (int i : parts) { z0 = std::min(z0, D.bodies[i].z - D.bodies[i].thick / 2); z1 = std::max(z1, D.bodies[i].z + D.bodies[i].thick / 2); }
    double zc = (z0 + z1) / 2;
    for (int i : parts) {
        BodyDef& d = D.bodies[i];
        fixShape(d);
        freezeMagazine(d, D.breech.cart);   // a fitted magazine keeps the shape you give it, and its rounds scale with it
        double zWas = d.z;
        d.z = zc + (d.z - zc) * sz;
        size_t n = d.pts.size();
        // each edge's bow scales with how far the shape stretches across that edge
        for (size_t k = 0; k < n; k++) {
            V2 a = d.pts[k], b = d.pts[(k + 1) % n];
            double ex = b.x - a.x, ey = b.y - a.y, l = std::hypot(ex, ey);
            if (l > 1e-9) d.bulge[k] *= std::hypot(-ey / l * sx, ex / l * sy);
        }
        for (auto& p : d.pts) { sc(p); p.x = std::round(p.x * 100) / 100; p.y = std::round(p.y * 100) / 100; }
        for (auto& r : d.rad) r *= std::min(std::fabs(sx), std::fabs(sy));
        for (auto& o : d.depthOff) o = {o.x * sx, o.y * sy};
        for (auto& zn : d.depthZn) zn *= sz;
        for (auto& zf : d.depthZf) zf *= sz;
        if (!d.depthRef.empty()) d.depthRef = d.pts;
        for (auto& p : d.roundPath) sc(p);
        if (!d.round) d.thick = std::max(0.2, d.thick * sz);
        for (auto& cv : d.carves) {
            sc(cv.at);
            cv.atZ *= sz;
            double L = std::sqrt(cv.dx * cv.dx + cv.dy * cv.dy + cv.dz * cv.dz);
            if (L < 1e-9) continue;
            double ux = cv.dx / L, uy = cv.dy / L, uz = cv.dz / L;
            double along = std::sqrt(ux * ux * sx * sx + uy * uy * sy * sy + uz * uz * sz * sz);   // how much the cut's direction stretches
            if (cv.depth > 0) cv.depth *= along;
            if (std::fabs(uz) > 0.7) { cv.w *= std::fabs(sx); cv.h *= std::fabs(sy); }          // cut from the side: its shape lies in the side view
            else { double across = std::fabs(ux) > std::fabs(uy) ? sy : sx; cv.w *= std::sqrt(std::fabs(across * sz)); cv.h *= std::sqrt(std::fabs(across * sz)); }
        }
        for (auto& m : d.marks) {   // engravings stay on the surface
            for (auto& p : m.pts) { V2 q{p.x, p.y}; sc(q); p.x = q.x; p.y = q.y; p.z = d.z + (p.z - zWas) * sz; }
            for (auto& nn : m.n) { double x = nn.x / sx, y = nn.y / sy, z = nn.z / sz, l = std::sqrt(x * x + y * y + z * z); if (l > 1e-12) nn = {x / l, y / l, z / l}; }
            m.size *= lin;
        }
        if (!d.marks.empty()) { d.markRef = d.pts; d.markRefZ = d.z; d.markRefThick = d.thick; }
        if (d.ctrl != "none") sc(d.ctrlPt);
        if (d.mass > 0) d.mass *= area * (d.round ? lin : sz);   // a weight you set grows with the part
        // what's attached at points on it
        const std::string& id = d.id;
        for (auto& j : D.joints) {
            if ((j.type == "pin" || j.type == "weld") && j.a == id) sc(j.p);
            if (j.type == "track") { if (j.a == id) sc(j.p); if (j.b == id) for (auto& q : j.path) sc(q); }
            if (j.type == "magwell" && j.a == id) { sc(j.p); for (auto& q : j.path) sc(q); }
            if (j.type == "spring") { if (j.a == id) sc(j.pa); if (j.b == id) sc(j.pb); }
        }
        auto& br = D.breech;
        if (br.on) {
            if (br.breech == id && br.hasFace) sc(br.face);
            if (br.chamber == id && br.hasMuzzle) sc(br.muzzle);
            for (auto& cm : br.more) { if ((cm.breech.empty() ? br.breech : cm.breech) == id) sc(cm.face); if (cm.chamber == id && cm.hasMuzzle) sc(cm.muzzle); }
            if (br.striker == id && br.ignition == "striker") sc(br.strikerTip);
        }
        for (HandHold* h : {&D.pose.firing, &D.pose.support, &D.pose.butt}) if (h->on && h->part == id) sc(h->at);
    }
    // pins and welds shared by two scaled parts move only once (above, with their first part): nothing more to do
}

bool scaleBox(const Design& D, const std::vector<int>& parts, const std::vector<int>& corners, V2& lo, V2& hi) {
    lo = {1e9, 1e9}; hi = {-1e9, -1e9};
    auto take = [&](V2 p) { lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); };
    if (!corners.empty() && parts.size() == 1) { for (int v : corners) if (v >= 0 && v < (int)D.bodies[parts[0]].pts.size()) take(D.bodies[parts[0]].pts[v]); }
    else for (int i : parts) for (auto& p : outlineOf(D.bodies[i])) take(p);
    return hi.x > lo.x - 1e-9 && hi.y > lo.y - 1e-9 && lo.x < 1e8;
}

// ---------------------------------------------------------------- dragging the box's handles
namespace {
bool partPicked() { return A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size(); }
std::vector<int> scaledParts() { return partPicked() ? weldedWith(A.design, A.sel.idx) : std::vector<int>{}; }
std::vector<int> scaledCorners() { return A.selVs.size() >= 2 ? A.selVs : std::vector<int>{}; }
// handle k: 0..3 corners (top left, top right, bottom right, bottom left), 4..7 edges (top, right, bottom, left)
V2 handleAt(int k, V2 lo, V2 hi) {
    V2 m{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2};
    switch (k) {
    case 0: return lo; case 1: return {hi.x, lo.y}; case 2: return hi; case 3: return {lo.x, hi.y};
    case 4: return {m.x, lo.y}; case 5: return {hi.x, m.y}; case 6: return {m.x, hi.y}; default: return {lo.x, m.y};
    }
}
float planeZ() { return partPicked() ? bodyFront(A.sel.idx) : 0.f; }
struct Drag { bool on = false, live = false; int k = -1; Design orig; V2 lo, hi; Vector2 press; double sx = 1, sy = 1; };
Drag G;
}  // namespace

void scaleInput(bool overUI) {
    if (G.on) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            G.on = false;
            if (G.live) { commit(); toast(TextFormat("Scaled to %.0f%% x %.0f%%.", G.sx * 100, G.sy * 100)); }
            return;
        }
        if (!G.live) { if (Vector2Distance(GetMousePosition(), G.press) < 5 * CFG.uiScale) return; G.live = true; }
        V2 w = snapMM(mouseMM(planeZ()));
        bool mid = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        bool free = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        V2 h0 = handleAt(G.k, G.lo, G.hi), anchor = mid ? V2{(G.lo.x + G.hi.x) / 2, (G.lo.y + G.hi.y) / 2} : handleAt(G.k < 4 ? (G.k + 2) % 4 : 4 + (G.k - 2) % 4, G.lo, G.hi);
        double sx = 1, sy = 1;
        auto ratio = [](double now, double was) { return std::fabs(was) < 1e-6 ? 1.0 : now / was; };
        if (G.k == 5 || G.k == 7) sx = ratio(w.x - anchor.x, h0.x - anchor.x);
        else if (G.k == 4 || G.k == 6) sy = ratio(w.y - anchor.y, h0.y - anchor.y);
        else if (free) { sx = ratio(w.x - anchor.x, h0.x - anchor.x); sy = ratio(w.y - anchor.y, h0.y - anchor.y); }
        else {   // a corner: the same both ways, by how far along the diagonal the mouse is
            double dx = h0.x - anchor.x, dy = h0.y - anchor.y, l2 = dx * dx + dy * dy;
            sx = sy = l2 > 1e-9 ? ((w.x - anchor.x) * dx + (w.y - anchor.y) * dy) / l2 : 1;
        }
        sx = std::clamp(sx, 0.02, 50.0); sy = std::clamp(sy, 0.02, 50.0);
        double sz = (G.k < 4 && !free && A.scaleDepth) ? sx : 1;
        A.design = G.orig;
        scaleParts(A.design, scaledParts(), anchor, sx, sy, sz, scaledCorners());
        G.sx = sx; G.sy = sy;
        return;
    }
    if (overUI || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    Vector2 m = GetMousePosition();
    if (partPicked()) {   // on one of the box's handles? the first click picks it, a second press drags it
        V2 lo, hi;
        if (scaleBox(A.design, scaledParts(), scaledCorners(), lo, hi)) {
            int best = -1; float bd = 11;
            for (int k = 0; k < 8; k++) { float d = Vector2Distance(toScreen(W3(handleAt(k, lo, hi), planeZ())), m); if (d < bd) { bd = d; best = k; } }
            if (best >= 0) {
                if (A.scaleHandle != best) { A.scaleHandle = best; return; }
                G.on = true; G.live = false; G.k = best; G.orig = A.design; G.lo = lo; G.hi = hi; G.press = m; G.sx = G.sy = 1;
                return;
            }
        }
    }
    // otherwise: pick the part under the mouse (or nothing)
    auto hits = bodiesUnderMouse();
    int hit = hits.empty() ? -1 : hits[0].body;
    for (auto& h : hits) if (A.sel.kind == 1 && h.body == A.sel.idx) hit = h.body;   // the selected part wins
    A.scaleHandle = -1;
    if (hit >= 0) { if (!(A.sel.kind == 1 && A.sel.idx == hit)) { A.sel = {1, hit, -1}; A.selVs.clear(); } }
    else { A.sel = {}; A.selVs.clear(); }
}

void drawScaleOverlay() {
    if (A.tool != Tool::Scale || A.mode != Mode::Build || !partPicked()) return;
    V2 lo, hi;
    if (!scaleBox(A.design, scaledParts(), scaledCorners(), lo, hi)) return;
    float z = planeZ();
    Color c{233, 161, 59, 255}, cf{233, 161, 59, 70};
    Vector2 a = toScreen(W3(lo, z)), b = toScreen(W3({hi.x, lo.y}, z)), d = toScreen(W3(hi, z)), e = toScreen(W3({lo.x, hi.y}, z));
    DrawLineEx(a, b, 1.5f, c); DrawLineEx(b, d, 1.5f, c); DrawLineEx(d, e, 1.5f, c); DrawLineEx(e, a, 1.5f, c);
    Vector2 mm = GetMousePosition();
    for (int k = 0; k < 8; k++) {
        Vector2 s = toScreen(W3(handleAt(k, lo, hi), z));
        bool hot = Vector2Distance(s, mm) < 11 || A.scaleHandle == k || (G.on && G.k == k);
        float r = k < 4 ? 6.f : 5.f;
        if (k < 4) { DrawRectangleV({s.x - r, s.y - r}, {2 * r, 2 * r}, hot ? c : Color{24, 28, 36, 235}); DrawRectangleLinesEx({s.x - r, s.y - r, 2 * r, 2 * r}, 1.5f, c); }
        else { DrawCircleV(s, r, hot ? c : Color{24, 28, 36, 235}); DrawCircleLinesV(s, r, c); }
        (void)cf;
    }
    // the size, and what a drag does
    Vector2 top{(a.x + b.x) / 2, std::min(a.y, b.y) - 22 * CFG.uiScale};
    const char* sz = TextFormat("%s x %s", fmtQ(Q::Length, hi.x - lo.x, 1).c_str(), fmtQ(Q::Length, hi.y - lo.y, 1).c_str());
    viewText(sz, top.x - MeasureText(sz, 12) / 2.f, top.y, 12, c);
    if (G.on && G.live) viewText(TextFormat("%.0f%% x %.0f%%", G.sx * 100, G.sy * 100), mm.x + 14, mm.y + 10, 12, c);
    else if (A.scaleHandle >= 0) viewText("Press it again and drag. Shift: free  Ctrl: about the middle", mm.x + 14, mm.y + 10, 11, c);
}
