// Gunsmith3D - rotation rings (see gizmo.hpp).
#include "app/gizmo.hpp"
#include "app/fonts.hpp"
#include "raymath.h"

#include <cmath>
#include <cstdio>

namespace {
const Color COL[3] = {{235, 80, 80, 255}, {100, 210, 110, 255}, {80, 140, 245, 255}};
const int SEG = 72;
void basis(Vector3 a, Vector3& u, Vector3& v) {   // two directions across the axis
    Vector3 t = std::fabs(a.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    u = Vector3Normalize(Vector3CrossProduct(a, t));
    v = Vector3CrossProduct(a, u);
}
Vector3 ringPt(const Rings& g, int k, int i) {
    Vector3 u, v; basis(g.ax[k], u, v);
    float t = 2 * PI * i / SEG;
    return Vector3Add(g.c, Vector3Add(Vector3Scale(u, g.r * std::cos(t)), Vector3Scale(v, g.r * std::sin(t))));
}
bool has(const Rings& g, int k) { return Vector3Length(g.ax[k]) > 0.5f; }
float segDist(Vector2 p, Vector2 a, Vector2 b) {
    Vector2 ab = Vector2Subtract(b, a);
    float l2 = Vector2LengthSqr(ab);
    float t = l2 > 0 ? std::fmax(0.f, std::fmin(1.f, Vector2DotProduct(Vector2Subtract(p, a), ab) / l2)) : 0;
    return Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, t)));
}
bool onScreen(Vector2 s) { return s.x > -1e4f && s.y > -1e4f && s.x < 1e5f && s.y < 1e5f; }
}  // namespace

void ringsFit(Rings& g, const ToScreen& ts, float px) {
    Vector2 c = ts(g.c);
    float best = 0;
    for (int k = 0; k < 3; k++) {   // how many pixels a unit (world) across the view makes, from the axes that show most
        if (!has(g, k)) continue;
        Vector3 u, v; basis(g.ax[k], u, v);
        for (Vector3 d : {u, v}) { Vector2 s = ts(Vector3Add(g.c, d)); if (onScreen(s)) best = std::fmax(best, Vector2Distance(s, c)); }
    }
    g.r = best > 1e-4f ? px / best : 1;
}

int ringsPick(const Rings& g, const ToScreen& ts, Vector2 m) {
    int best = -1; float bd = 7;
    for (int k = 0; k < 3; k++) {
        if (!has(g, k)) continue;
        Vector2 prev = ts(ringPt(g, k, 0));
        for (int i = 1; i <= SEG; i++) {
            Vector2 s = ts(ringPt(g, k, i));
            if (onScreen(s) && onScreen(prev)) { float d = segDist(m, prev, s); if (d < bd) { bd = d; best = k; } }
            prev = s;
        }
    }
    return best;
}

void ringsBegin(Rings& g, int axis, const ToScreen& ts, Vector2 m) {
    g.axis = axis; g.press = m; g.deg = 0;
    // the point on the ring nearest the mouse, and which way round the ring goes there on the screen
    int bi = 0; float bd = 1e9f;
    for (int i = 0; i < SEG; i++) { float d = Vector2Distance(ts(ringPt(g, axis, i)), m); if (d < bd) { bd = d; bi = i; } }
    Vector3 p = ringPt(g, axis, bi);
    Vector3 t = Vector3CrossProduct(g.ax[axis], Vector3Subtract(p, g.c));   // the right-hand way round, a radius long
    Vector2 a = ts(p), b = ts(Vector3Add(p, Vector3Scale(t, 0.05f)));      // 0.05 rad on
    g.tan = Vector2Scale(Vector2Subtract(b, a), 1 / 0.05f);                  // pixels per radian along the ring
}

double ringsDrag(Rings& g, Vector2 m, bool snap) {
    Vector2 d = Vector2Subtract(m, g.press);
    float l = Vector2Length(g.tan);
    double rad;
    if (l < 15) rad = d.x * 0.01;   // (the ring seen edge on there: across the screen turns it)
    else rad = Vector2DotProduct(d, Vector2Scale(g.tan, 1 / l)) / std::fmax(l, 40.f);
    double deg = rad * 180 / PI;
    if (snap) deg = std::round(deg / 15) * 15;
    g.deg = deg;
    return deg;
}

void ringsDraw(const Rings& g, const ToScreen& ts, Vector3 eye) {
    for (int k = 0; k < 3; k++) {
        if (!has(g, k)) continue;
        bool hot = g.axis == k || (g.axis < 0 && g.hot == k);
        Color c = COL[k];
        Vector2 prev = ts(ringPt(g, k, 0));
        Vector3 pprev = ringPt(g, k, 0);
        for (int i = 1; i <= SEG; i++) {
            Vector3 p = ringPt(g, k, i);
            Vector2 s = ts(p);
            Vector3 mid = Vector3Scale(Vector3Add(p, pprev), 0.5f);
            bool near = Vector3DotProduct(Vector3Subtract(mid, g.c), Vector3Subtract(eye, g.c)) >= -1e-6f;
            if (onScreen(s) && onScreen(prev))
                DrawLineEx(prev, s, hot ? 3.5f : 2.f, near ? c : Color{c.r, c.g, c.b, (unsigned char)(hot ? 120 : 60)});
            prev = s; pprev = p;
        }
    }
    if (g.axis >= 0) {   // how far, while turning
        Vector2 s = ts(g.c);
        char b[64];
        std::snprintf(b, sizeof b, "%s%+.0f\xc2\xb0", g.label[g.axis] ? g.label[g.axis] : "", g.deg);
        viewText(b, s.x + 14, s.y - 26, 13, COL[g.axis]);
    } else if (g.hot >= 0 && g.label[g.hot]) {
        Vector2 s = ts(g.c);
        viewText(g.label[g.hot], s.x + 14, s.y - 26, 12, COL[g.hot]);
    }
}

double ringsSign(const std::function<Vector3(double)>& probe, Vector3 c, Vector3 axis) {
    Vector3 p0 = probe(0), p1 = probe(1);
    Vector3 want = Vector3CrossProduct(axis, Vector3Subtract(p0, c));
    return Vector3DotProduct(Vector3Subtract(p1, p0), want) >= 0 ? 1 : -1;
}

double ringsGain(const std::function<Vector3(double)>& probe, Vector3 c, Vector3 axis) {
    // how far round the axis the probe goes for a few degrees of the value, per degree (signed)
    const double step = 4;
    Vector3 p0 = Vector3Subtract(probe(0), c), p1 = Vector3Subtract(probe(step), c);
    p0 = Vector3Subtract(p0, Vector3Scale(axis, Vector3DotProduct(p0, axis)));
    p1 = Vector3Subtract(p1, Vector3Scale(axis, Vector3DotProduct(p1, axis)));
    if (Vector3Length(p0) < 1e-6f || Vector3Length(p1) < 1e-6f) return 1;
    double turned = std::atan2(Vector3DotProduct(Vector3CrossProduct(p0, p1), axis), Vector3DotProduct(p0, p1)) * 180 / PI;
    double g = turned / step;
    if (std::fabs(g) < 0.05) return g < 0 ? -1 : 1;   // (it hardly shows round this axis: one for one)
    return g;
}

namespace {
double turnedBy(Vector3 p0, Vector3 p1, Vector3 c, Vector3 axis) {
    p0 = Vector3Subtract(p0, c); p1 = Vector3Subtract(p1, c);
    p0 = Vector3Subtract(p0, Vector3Scale(axis, Vector3DotProduct(p0, axis)));
    p1 = Vector3Subtract(p1, Vector3Scale(axis, Vector3DotProduct(p1, axis)));
    return std::atan2(Vector3DotProduct(Vector3CrossProduct(p0, p1), axis), Vector3DotProduct(p0, p1)) * 180 / PI;
}
}  // namespace

double ringsSolve(const std::function<Vector3(double)>& probe, Vector3 c, Vector3 axis, double deg, double gain) {
    if (std::fabs(deg) < 1e-9 || std::fabs(gain) < 1e-9) return 0;
    Vector3 p0 = probe(0);
    double v0 = 0, t0 = 0, v1 = deg / gain, t1 = turnedBy(p0, probe(v1), c, axis);
    for (int i = 0; i < 4 && std::fabs(t1 - deg) > 0.05; i++) {   // secant steps toward the asked turn
        double dt = t1 - t0;
        if (std::fabs(dt) < 1e-6) break;
        double v2 = v1 + (deg - t1) * (v1 - v0) / dt;
        if (!std::isfinite(v2) || std::fabs(v2) > 4 * std::fabs(deg / gain) + 30) break;   // (it won't go that far: keep the last)
        v0 = v1; t0 = t1; v1 = v2; t1 = turnedBy(p0, probe(v1), c, axis);
    }
    return v1;
}
