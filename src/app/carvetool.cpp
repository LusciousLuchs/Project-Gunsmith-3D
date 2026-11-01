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
    for (size_t i = 0; i < A.design.bodies.size() && i < A.meshes.size(); i++) {
        if (!A.meshes[i].ok) continue;
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
    auto snap = [](double x) { return std::max(0.5, std::round(x * 2) / 2); };
    if (c.shape == "circle") c.w = c.h = snap(2 * std::hypot(du, dv));
    else { c.w = snap(2 * du); c.h = snap(2 * dv); }
    return c;
}

void carveInput(bool overUI) {
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

void drawCarveOverlay() {
    auto& D = A.design;
    if (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)D.bodies.size()) {  // the part's carves: the selected one bright
        auto& b = D.bodies[A.sel.idx];
        for (int k = 0; k < (int)b.carves.size(); k++)
            if (k == A.selCarve) drawCarve(A.sel.idx, b.carves[k], Color{255, 190, 70, 255}, true);
            else if (A.tool == Tool::Carve) drawCarve(A.sel.idx, b.carves[k], Color{255, 190, 70, 90}, false);
    }
    if (A.tool != Tool::Carve || A.mode != Mode::Build) return;
    if (A.carveDrag) { drawCarve(A.carveBody, dragCarve(), Color{120, 220, 255, 255}, true); return; }
    if (!mouseInViewport() || ImGui::GetIO().WantCaptureMouse) return;
    int i; Vector3 p, n;
    if (carvePick(i, p, n)) drawCarve(i, carveAt(i, p, n), Color{120, 220, 255, 200}, true);
}
