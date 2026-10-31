// Gunsmith3D - 3D scene: parts, rounds, springs, magazines, effects.
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "app/render.hpp"
#include "app/scene.hpp"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------- what to draw: design parts (Build) or live bodies (Test)
static Matrix poseMatrix(int tmpl, double xmm, double ymm, double a, double roll) {
    return MatrixMultiply(MatrixMultiply(MatrixRotateX(-(float)roll), MatrixRotateZ(-(float)a)),
                          MatrixTranslate((float)xmm * U, -(float)ymm * U, bodyZ(tmpl)));
}
std::vector<RB> renderList() {
    std::vector<RB> out;
    if (!A.S) {
        for (size_t i = 0; i < A.design.bodies.size(); i++) {
            V2 c = centroidOf(A.design.bodies[i]);
            out.push_back({(int)i, poseMatrix((int)i, c.x, c.y, 0, 0), 0, nullptr});
        }
        return out;
    }
    for (auto& b : A.S->bodies) {
        int t = A.design.bodyIndex(b.tmpl.empty() ? b.id : b.tmpl);
        if (t < 0) continue;
        Matrix M = poseMatrix(t, b.x * 1000, b.y * 1000, b.a, b.roll);
        if (b.rollParent) {  // a piece of a rotating bolt turns about the bolt's axis, not its own centre
            float ay = (float)(b.axisDy * 1000) * U, az = -(float)(b.axisDz * 1000) * U;
            Matrix R = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -ay, -az), MatrixRotateX(-(float)b.rollParent->roll)), MatrixTranslate(0, ay, az));
            M = MatrixMultiply(R, M);
        }
        out.push_back({t, M, (float)(b.rollParent ? b.rollParent->roll : b.roll), &b});
    }
    return out;
}

bool mainPart(const sim::Body& b) {  // moving parts worth graphing: not magazines, not welded-on pieces, not light controls
    if (b.fixed || b.isMag || !b.tmpl.empty()) return false;
    if (b.d->ctrl != "none" || b.m < 0.05) return false;
    for (auto& j : A.design.joints) if (j.type == "weld" && j.a == b.id) return false;
    return true;
}

// ---------------------------------------------------------------- 3D scene
static void ensureMeshes() {
    auto& D = A.design;
    A.meshes.resize(D.bodies.size());
    for (size_t i = 0; i < D.bodies.size(); i++) {
        auto& d = D.bodies[i];
        std::ostringstream k;
        k << d.thick;
        for (auto& p : d.pts) k << ',' << p.x << ' ' << p.y;
        for (double r : d.rad) k << 'r' << r;
        for (double b : d.bulge) k << 'b' << b;
        std::string key = k.str();
        if (A.meshes[i].ok && A.meshes[i].key == key) continue;
        if (A.meshes[i].ok) UnloadMesh(A.meshes[i].mesh);
        V2 c = centroidOf(d);
        std::vector<V2> local;
        for (auto& p : outlineOf(d)) local.push_back({p.x - c.x, p.y - c.y});
        A.meshes[i].mesh = extrude(local, bodyDepth(d));
        A.meshes[i].key = key;
        A.meshes[i].ok = true;
    }
}
float plateY() { return -(float)groundLevelMM(A.design) * U; }
static void drawOutline(int i, Matrix M, Color c) {
    const BodyDef& d = A.design.bodies[i];
    V2 cen = centroidOf(d);
    auto o = outlineOf(d);
    float h = bodyDepth(d) / 2 + 0.003f;
    size_t n = o.size();
    std::vector<Vector3> f(n), b(n);
    for (size_t k = 0; k < n; k++) {
        Vector3 l{(float)(o[k].x - cen.x) * U, -(float)(o[k].y - cen.y) * U, 0};
        f[k] = Vector3Transform({l.x, l.y, h}, M);
        b[k] = Vector3Transform({l.x, l.y, -h}, M);
    }
    for (size_t k = 0; k < n; k++) {
        DrawLine3D(f[k], f[(k + 1) % n], c);
        DrawLine3D(b[k], b[(k + 1) % n], c);
        // depth edges only at real corners, not along curves
        V2 a = o[(k + n - 1) % n], p = o[k], q = o[(k + 1) % n];
        double t1 = std::atan2(p.y - a.y, p.x - a.x), t2 = std::atan2(q.y - p.y, q.x - p.x);
        if (std::fabs(std::remainder(t2 - t1, 2 * PI)) > 25 * DEG2RAD) DrawLine3D(f[k], b[k], c);
    }
}
// Lathe-turned cartridge meshes, built from the cartridge's profile. Axis along +X, head at x = 0.
struct RoundMesh { std::string key; Mesh caseM{}, bulletM{}; bool ok = false; } RM;
std::vector<V2> caseProfile(const Cartridge& c) {  // (x along the axis, radius), mm
    double rim = c.rimDia / 2, base = c.baseDia / 2, sh = c.shoulderDia / 2, neck = c.neckDia / 2;
    std::vector<V2> p = {{0, 0}, {0, rim}, {c.rimThick, rim}};
    if (!c.rimmed) { double g = base * 0.84; p.push_back({c.rimThick, g}); p.push_back({c.rimThick + 0.9, g}); p.push_back({c.rimThick + 1.6, base}); }
    else p.push_back({c.rimThick, base});
    double shAt = std::min(c.shoulderAt, c.caseLen);
    p.push_back({shAt, sh});
    if (shAt < c.caseLen - 0.3) {
        double shLen = std::min(std::max(0.5, (sh - neck) * 1.6), (c.caseLen - shAt) * 0.6);
        p.push_back({shAt + shLen, neck});
    }
    p.push_back({c.caseLen, neck});
    p.push_back({c.caseLen, std::min(neck, c.bulletDia / 2)});
    return p;
}
std::vector<V2> bulletProfile(const Cartridge& c) {
    double r = c.bulletDia / 2, start = std::max(0.0, c.caseLen - 2), ogStart = std::max(start + 0.5, c.oal - c.noseLen);
    std::vector<V2> p = {{start, 0}, {start, r}, {ogStart, r}};
    for (int k = 1; k <= 8; k++) { double t = k / 8.0; p.push_back({ogStart + (c.oal - ogStart) * t, std::max(r * 0.12, r * std::sqrt(std::max(0.0, 1 - std::pow(t, 1.7))))}); }
    p.push_back({c.oal, 0});
    return p;
}
static Mesh lathe(const std::vector<V2>& prof) {
    const int N = 18;
    int segs = (int)prof.size() - 1;
    Mesh m{};
    m.vertexCount = segs * N * 6; m.triangleCount = segs * N * 2;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    int k = 0;
    for (int s = 0; s < segs; s++) {
        V2 a = prof[s], b = prof[s + 1];
        double dx = b.x - a.x, dr = b.y - a.y, l = std::hypot(dx, dr);
        double nx = l > 0 ? -dr / l : 0, nr = l > 0 ? dx / l : 1;
        for (int i = 0; i < N; i++) {
            double t0 = 2 * PI * i / N, t1 = 2 * PI * (i + 1) / N;
            V2 q[6][2] = {{a, {t0, 0}}, {b, {t0, 0}}, {b, {t1, 0}}, {a, {t0, 0}}, {b, {t1, 0}}, {a, {t1, 0}}};
            for (auto& v : q) {
                double th = v[1].x;
                m.vertices[k * 3] = (float)(v[0].x * U); m.vertices[k * 3 + 1] = (float)(v[0].y * std::cos(th) * U); m.vertices[k * 3 + 2] = (float)(v[0].y * std::sin(th) * U);
                m.normals[k * 3] = (float)nx; m.normals[k * 3 + 1] = (float)(nr * std::cos(th)); m.normals[k * 3 + 2] = (float)(nr * std::sin(th));
                m.texcoords[k * 2] = 0; m.texcoords[k * 2 + 1] = 0; k++;
            }
        }
    }
    UploadMesh(&m, false);
    return m;
}
static void ensureRoundMesh() {
    const Cartridge& c = A.design.breech.cart;
    char key[256];
    std::snprintf(key, sizeof key, "%g/%g/%g/%g/%g/%g/%g/%g/%g/%g/%g/%d", c.caseLen, c.rimDia, c.rimThick, c.baseDia, c.shoulderDia, c.shoulderAt, c.neckDia, c.bulletDia, c.oal, c.noseLen, c.bulletMass, (int)c.rimmed);
    if (RM.ok && RM.key == key) return;
    if (RM.ok) { UnloadMesh(RM.caseM); UnloadMesh(RM.bulletM); }
    RM.caseM = lathe(caseProfile(c));
    RM.bulletM = lathe(bulletProfile(c));
    RM.key = key; RM.ok = true;
}
static Matrix alignX(Vector3 dir) {  // rotation taking +X onto dir
    Vector3 x{1, 0, 0}, d = Vector3Normalize(dir);
    float dot = Vector3DotProduct(x, d);
    if (dot > 0.9999f) return MatrixIdentity();
    if (dot < -0.9999f) return MatrixRotateY(PI);
    return MatrixRotate(Vector3Normalize(Vector3CrossProduct(x, d)), std::acos(dot));
}
bool boltHandle(const RB& r, BoltHandle& h) {
    const BodyDef& d = A.design.bodies[r.tmpl];
    if (!d.rollOn || d.handle <= 0) return false;
    V2 c = centroidOf(d);
    double x0 = 1e9, x1 = -1e9;
    for (auto& p : d.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); }
    float hd = bodyDepth(d) / 2;
    h.localMM = x0 + (x1 - x0) * 0.78 - c.x;
    float hx = (float)h.localMM * U;
    h.base = Vector3Transform({hx, 0, 0}, r.M);
    Vector3 dirL{0, -std::sin(h.restDeg * DEG2RAD), std::cos(h.restDeg * DEG2RAD)};  // drawn 20 degrees below level at roll 0
    h.radius = (float)d.handle * U + hd;
    Vector3 tipL = Vector3Scale(dirL, h.radius);
    h.tip = Vector3Transform({hx + tipL.x, tipL.y, tipL.z}, r.M);
    h.axis = Vector3Normalize(Vector3Subtract(Vector3Transform({hx + 1, 0, 0}, r.M), h.base));
    return true;
}
static void drawCartridgeAt(Vector3 o, Vector3 dir, bool caseOnly = false, float bright = 1.f) {
    ensureRoundMesh();
    Matrix M = MatrixMultiply(alignX(dir), MatrixTranslate(o.x, o.y, o.z));
    setSpecular(0.75f);  // polished brass and copper
    drawMesh(RM.caseM, M, shade(Color{214, 168, 78, 255}, bright));
    if (!caseOnly) drawMesh(RM.bulletM, M, shade(Color{184, 102, 62, 255}, bright));
    setSpecular(0.3f);
}
static void drawCartridge(const std::string& bodyId, V2 face, float zc, bool caseOnly = false) {
    V2 f = curMM(bodyId, face), ax = curMM(bodyId, {face.x + 1, face.y});
    Vector3 o = W3(f, zc), dir = Vector3Normalize(Vector3Subtract(W3(ax, zc), o));
    drawCartridgeAt(o, dir, caseOnly);
}
static void drawMagRounds(const RB& r, int rounds, int skip = 0) {  // skip: top slots shown elsewhere  // rounds stacked inside a magazine, top round at the lips
    auto& br = A.design.breech;
    if (!br.on || rounds <= 0) return;
    const BodyDef& d = A.design.bodies[r.tmpl];
    V2 c = centroidOf(d);
    if (d.gen.on) {  // generated magazine: rounds follow its centreline, staggered for double stack
        double dia = br.cart.headDia() * 1.02, pitch = d.gen.stack == 2 ? dia * 0.56 : dia * 0.98;
        int n = std::min(rounds, d.magCap);
        for (int k = skip; k < n; k++) {
            double s = 3 + dia / 2 + k * pitch, phi;
            V2 m = magCentre(d.gen, s, &phi);
            V2 fwd{std::cos(phi * DEG2RAD), -std::sin(phi * DEG2RAD)};
            V2 head{m.x - fwd.x * br.cart.oal / 2, m.y - fwd.y * br.cart.oal / 2};
            float z = d.gen.stack == 2 ? (k % 2 ? 1.f : -1.f) * (float)(dia * 0.25) * U : 0;
            Vector3 o = Vector3Transform({(float)(head.x - c.x) * U, -(float)(head.y - c.y) * U, z}, r.M);
            Vector3 o2 = Vector3Transform({(float)(head.x + fwd.x - c.x) * U, -(float)(head.y + fwd.y - c.y) * U, z}, r.M);
            drawCartridgeAt(o, Vector3Normalize(Vector3Subtract(o2, o)));
        }
        return;
    }
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& p : d.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    double sp = br.roundDia * 0.92, room = y1 - y0 - 3;
    int n = std::min(rounds, std::max(0, (int)(room / sp)));
    double xc = (x0 + x1) / 2 - br.roundLen / 2;
    Vector3 o0 = Vector3Transform({0, 0, 0}, r.M), ox = Vector3Transform({1, 0, 0}, r.M);
    Vector3 dir = Vector3Normalize(Vector3Subtract(ox, o0));
    for (int k = skip; k < n; k++) {
        double ly = y0 + 1.5 + br.roundDia / 2 + k * sp, lx = xc + ((k % 2) ? 0.6 : -0.6);
        // follow the slant of the magazine body: shift each round by the part's local slope
        Vector3 o = Vector3Transform({(float)(lx - c.x) * U, -(float)(ly - c.y) * U, 0}, r.M);
        drawCartridgeAt(o, dir);
    }
}
static void drawSpring(Vector3 a, Vector3 b, float coilR, float wireR, Color c) {
    Vector3 d = Vector3Subtract(b, a);
    float L = Vector3Length(d);
    if (L < 1e-4f) return;
    Vector3 ax = Vector3Scale(d, 1 / L);
    Vector3 up = std::fabs(ax.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    Vector3 u = Vector3Normalize(Vector3CrossProduct(ax, up)), v = Vector3CrossProduct(ax, u);
    int turns = 11, seg = 10, N = turns * seg;
    Vector3 prev{};
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N, ang = t * turns * 2 * PI;
        Vector3 p = Vector3Add(Vector3Add(a, Vector3Scale(d, t)), Vector3Add(Vector3Scale(u, std::cos(ang) * coilR), Vector3Scale(v, std::sin(ang) * coilR)));
        if (i) drawCyl(prev, p, wireR, c);
        prev = p;
    }
}
static void drawStands(float py) {  // one slim post behind the main fixed part, holding it above the floor
    auto& D = A.design;
    int best = -1; double bestA = 0;
    for (size_t i = 0; i < D.bodies.size(); i++) {
        if (!D.bodies[i].fixed) continue;
        double ar = std::fabs(sim::polyArea(outlineOf(D.bodies[i])));
        if (ar > bestA) { bestA = ar; best = (int)i; }
    }
    if (best < 0) return;
    auto& d = D.bodies[best];
    V2 c = centroidOf(d);
    float top = -(float)c.y * U, h = top - py;
    float back = 0;  // behind the furthest part
    for (size_t i = 0; i < D.bodies.size(); i++) back = std::min(back, bodyZ((int)i) - bodyDepth(D.bodies[i]) / 2);
    float z = back - 1.0f, x = (float)c.x * U;
    Color steel{64, 69, 78, 255};
    setSpecular(0.55f);
    if (h > 0.05f) {
        drawCyl({x, py, z}, {x, top, z}, 0.55f, steel);
        drawCyl({x, py, z}, {x, py + 0.35f, z}, 3.2f, steel);  // foot
    }
    drawCyl({x, top, z}, {x, top, z + 0.9f}, 0.9f, steel);    // the clamp that holds the frame
    setSpecular(0.3f);
}
void drawScene() {
    auto& D = A.design;
    ensureMeshes();
    float py = plateY();
    SceneLook look = sceneLook();
    setFog(A.showcase ? Color{16, 18, 22, 255} : look.fog);
    bool building = A.mode == Mode::Build && !A.showcase;
    if (!A.showcase) {
        // the bench always has a floor and a stand; the designer shows them only in the classic studio look
        if (!building || look.studs) {
            if (look.studs) DrawMesh(G.plate, G.plateMat, MatrixTranslate(A.target.x, py, 0));
            else drawFloor(py);
            drawStands(py);
        }
    }
    // drafting board behind the part plane
    if (building) {
        float back = 0;
        for (size_t i = 0; i < D.bodies.size(); i++) back = std::min(back, bodyZ((int)i) - bodyDepth(D.bodies[i]) / 2);
        drawBoard(back - 0.6f);
    }
    auto list = renderList();
    auto isGhost = [&](const RB& r) {
        if (A.showcase) return false;  // on the menu it looks like a finished gun
        if (building && A.sel.kind == 1 && A.sel.idx == r.tmpl) return false;
        const BodyDef& d = D.bodies[r.tmpl];
        if (A.cutaway && d.z - d.thick / 2 > 0.5) return true;  // wholly on the near side: cut away to show the inside
        return A.xray && d.ghost;
    };
    // opaque parts
    for (auto& r : list) {
        if (isGhost(r)) continue;
        Color c = hexColor(D.bodies[r.tmpl].color);
        if (building && A.sel.kind == 1 && A.sel.idx == r.tmpl) c = shade(c, 1.12f);
        if (!building && r.sb && r.sb == A.hover) c = shade(c, 1.18f);
        drawMesh(A.meshes[r.tmpl].mesh, r.M, c);
    }
    // rotating-bolt lugs and handles
    for (auto& r : list) {
        auto& d = D.bodies[r.tmpl];
        if (!d.rollOn) continue;
        V2 c = centroidOf(d);
        double x0 = 1e9, x1 = -1e9;
        for (auto& p : d.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); }
        float hd = bodyDepth(d) / 2;
        Color lc = shade(hexColor(d.color), 0.8f);
        float lx = (float)(x1 - 7 - c.x) * U;
        for (float sgn : {1.f, -1.f}) {
            Matrix L = MatrixMultiply(MatrixScale(0.7f, 0.9f, 0.55f), MatrixTranslate(lx, 0, sgn * (hd + 0.25f)));
            drawMesh(G.cube, MatrixMultiply(L, r.M), lc);
        }
        BoltHandle h;
        if (boltHandle(r, h)) {
            bool hot = !building && r.sb && (A.handleBody == r.sb || (A.hover == r.sb && A.hoverHandle));
            Color hc = hot ? Color{255, 226, 150, 255} : Color{226, 196, 124, 255};
            drawCyl(h.base, h.tip, 0.32f, hc);
            drawSphereLit(h.tip, hot ? 1.0f : 0.85f, hc);
        }
    }
    // magazine contents
    for (auto& r : list) {
        if (!D.bodies[r.tmpl].magazine) continue;
        // with physical feeding the top round is a real part of its own, held under the lips
        drawMagRounds(r, r.sb ? r.sb->rounds : D.bodies[r.tmpl].magCap, r.sb && r.sb->top ? 1 : 0);
    }
    // joints
    for (size_t ji = 0; ji < D.joints.size(); ji++) {
        auto& j = D.joints[ji];
        bool selJ = building && A.sel.kind == 2 && A.sel.idx == (int)ji;
        Color steel = selJ ? Color{255, 200, 60, 255} : Color{196, 202, 212, 255};
        if (j.type == "pin" || j.type == "weld") {
            V2 p = curMM(j.a, j.p);
            float z = std::max(frontZ(j.a), frontZ(j.b)) + 0.25f;
            if (j.type == "pin") drawCyl(W3(p, -z), W3(p, z), 0.16f, steel);
            else drawBox(W3(p, 0), {0.45f, 0.45f, z * 2}, steel);
        } else if (j.type == "track") {
            float zb = frontZ(j.b) + 0.02f;
            for (size_t k = 0; k + 1 < j.path.size(); k++) {
                Vector3 a = W3(curMM(j.b, j.path[k]), zb), b = W3(curMM(j.b, j.path[k + 1]), zb);
                Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f), dd = Vector3Subtract(b, a);
                float len = Vector3Length(dd) + 0.26f, ang = std::atan2(dd.y, dd.x);
                drawBox(mid, {len, 0.26f, 0.04f}, selJ ? Color{120, 90, 30, 255} : Color{28, 32, 40, 255}, MatrixRotateZ(ang));
            }
            V2 f = curMM(j.a, j.p);
            float z = std::max(frontZ(j.a), frontZ(j.b)) + 0.2f;
            drawCyl(W3(f, -z), W3(f, z), 0.14f, Color{240, 170, 60, 255});
        } else if (j.type == "spring" && j.kind == "leaf") {
            V2 root = curMM(j.a, j.pa), tipDesign = curMM(j.a, j.pb), contact = curMM(j.b, j.pb);
            // the leaf bends from its clamped root to where it touches the other part
            Vector3 a = W3(root, 0.3f), b = W3(contact, 0.3f), mid0 = W3({(root.x + tipDesign.x) / 2, (root.y + tipDesign.y) / 2}, 0.3f);
            Vector3 mid = Vector3Lerp(mid0, Vector3Scale(Vector3Add(a, b), 0.5f), 0.6f);
            Color lc = selJ ? Color{255, 200, 60, 255} : Color{176, 190, 208, 255};
            Vector3 prev = a;
            for (int k = 1; k <= 10; k++) {
                float t = k / 10.f;
                Vector3 q = Vector3Add(Vector3Add(Vector3Scale(a, (1 - t) * (1 - t)), Vector3Scale(mid, 2 * (1 - t) * t)), Vector3Scale(b, t * t));
                drawBox(Vector3Scale(Vector3Add(prev, q), 0.5f), {Vector3Distance(prev, q) + 0.02f, 0.06f, 0.5f}, lc, MatrixRotateZ(std::atan2(q.y - prev.y, q.x - prev.x)));
                prev = q;
            }
        } else if (j.type == "spring") {
            Vector3 a = W3(curMM(j.a, j.pa), 0), b = W3(curMM(j.b, j.pb), 0);
            bool buckled = false;
            if (A.S) for (auto& J : A.S->joints) if (J.defIndex == (int)ji) buckled = J.buckled;
            Color sc = selJ ? Color{255, 200, 60, 255} : buckled ? Color{235, 120, 90, 255} : Color{176, 190, 208, 255};
            float cr = (float)j.coilDia / 2 * U, wr = std::max(0.02f, (float)j.wire / 2 * U);
            if (buckled) {  // draw it bowed out sideways
                Vector3 mid = Vector3Add(Vector3Scale(Vector3Add(a, b), 0.5f), {0, -cr * 2.5f, 0});
                drawSpring(a, mid, cr, wr, sc); drawSpring(mid, b, cr, wr, sc);
            } else drawSpring(a, b, cr, wr, sc);
        } else if (j.type == "magwell" && building && j.path.size() >= 2) {
            float z = frontZ(j.a) + 0.15f;
            Vector3 a = W3(j.path.front(), z), b = W3(j.path.back(), z);
            Color mc = selJ ? Color{255, 200, 60, 255} : Color{110, 220, 150, 255};
            int n = 14;
            for (int k = 0; k < n; k += 2) DrawLine3D(Vector3Lerp(a, b, (float)k / n), Vector3Lerp(a, b, (float)(k + 1) / n), mc);
            drawSphereLit(a, 0.35f, mc);
            drawSphereLit(b, 0.35f, mc);
        }
    }
    // controls: an arrow where the finger pushes
    for (auto& r : list) {
        auto& d = D.bodies[r.tmpl];
        if (d.ctrl == "none" || (!building && !(r.sb && r.sb == A.hover))) continue;
        V2 c = centroidOf(d);
        Vector3 p = Vector3Transform({(float)(d.ctrlPt.x - c.x) * U, -(float)(d.ctrlPt.y - c.y) * U, bodyDepth(d) / 2 + 0.2f}, r.M);
        Vector3 dir{(float)std::cos(d.ctrlDir * DEG2RAD), -(float)std::sin(d.ctrlDir * DEG2RAD), 0};
        drawCyl(Vector3Subtract(p, Vector3Scale(dir, 1.6f)), Vector3Subtract(p, Vector3Scale(dir, 0.35f)), 0.06f, Color{120, 230, 200, 255});
        drawCyl(Vector3Subtract(p, Vector3Scale(dir, 0.45f)), p, 0.17f, Color{120, 230, 200, 255}, 1, &G.cone);
    }
    if (building && D.breech.on && D.breech.ignition == "striker" && !D.breech.striker.empty()) drawSphereLit(W3(D.breech.strikerTip, frontZ(D.breech.striker) + 0.2f), 0.3f, Color{255, 122, 89, 255});
    // chamber and feeding
    auto& br = D.breech;
    if (br.on && br.hasFace) {
        if (A.S) {
            if (A.S->chambered) drawCartridge(br.chamber, br.face, 0);
            if (A.S->inTransit) drawCartridge(br.breech, br.face, 0);
            if (A.S->spent && A.S->physicalFeed) drawCartridge(br.chamber, br.face, 0, true);  // empty case still in the chamber
        } else if (br.loaded) drawCartridge(br.chamber, br.face, 0);
    }
    for (auto& s : A.shells) drawCartridgeAt(Vector3Subtract(s.p, Vector3Scale(s.dir, s.len / 2)), s.dir, true);
    // loose rounds: on the bench, in the hand, or sitting in the open action
    if (A.S) for (auto& b : A.S->bodies) {
        if (!b.isRound || b.gone) continue;
        V2 h = sim::wpt(b, {b.headLx, 0}), t = sim::wpt(b, {b.headLx + 0.001, 0});
        float z = (float)(b.z * 1000) * U;  // cases fly out sideways through the port
        Vector3 o = W3({h.x * 1000, h.y * 1000}, z), o2 = W3({t.x * 1000, t.y * 1000}, z);
        drawCartridgeAt(o, Vector3Normalize(Vector3Subtract(o2, o)), b.isCase, &b == A.hover ? 1.3f : 1.f);
    }
    // the hand: a line from where you grabbed the part to the cursor
    if (A.S && A.S->hand.on && A.S->hand.b && A.dragKind == 3) {
        V2 g = sim::wpt(*A.S->hand.b, A.S->hand.l);
        float z = A.grabZ + 0.05f;
        Vector3 a = W3({g.x * 1000, g.y * 1000}, z), b = W3({A.S->hand.t.x * 1000, A.S->hand.t.y * 1000}, z);
        drawCyl(a, b, 0.07f, Color{255, 210, 80, 255});
        drawSphereLit(a, 0.32f, Color{255, 210, 80, 255});
        drawSphereLit(b, 0.45f, Color{255, 236, 190, 255});
    }
    // ghosts drawn last, see-through
    rlDisableDepthMask();
    for (auto& p : A.puffs) drawSphereLit(p.p, p.r, Color{215, 215, 220, 255}, std::clamp(p.life / p.max, 0.f, 1.f) * 0.28f);
    for (auto& r : list) {
        if (!isGhost(r)) continue;
        Color c = hexColor(D.bodies[r.tmpl].color);
        drawMesh(A.meshes[r.tmpl].mesh, r.M, (!building && r.sb && r.sb == A.hover) ? shade(c, 1.4f) : c, r.sb && r.sb == A.hover ? 0.42f : 0.26f);
    }
    rlEnableDepthMask();
    for (auto& r : list) {
        bool selB = building && A.sel.kind == 1 && A.sel.idx == r.tmpl;
        bool hov = !building && r.sb && r.sb == A.hover;
        drawOutline(r.tmpl, r.M, (selB || hov) ? Color{255, 205, 60, 255} : isGhost(r) ? look.ink : Color{18, 22, 30, 170});
    }
    // muzzle flash (unlit)
    if (A.flash > 0 && br.on && br.hasMuzzle) {
        Vector3 m = W3(curMM(br.chamber, br.muzzle), 0);
        Vector3 ax = Vector3Normalize(Vector3Subtract(W3(curMM(br.chamber, {br.muzzle.x + 1, br.muzzle.y}), 0), m));
        float k = A.flash / 0.12f;
        DrawSphere(Vector3Add(m, Vector3Scale(ax, 1.4f)), 1.4f * k + 0.4f, Color{255, 236, 170, 230});
        DrawSphere(Vector3Add(m, Vector3Scale(ax, 3.0f)), 1.0f * k + 0.3f, Color{255, 170, 60, 200});
        DrawSphere(Vector3Add(m, Vector3Scale(ax, 4.6f)), 0.6f * k + 0.2f, Color{255, 120, 40, 160});
    }
}
