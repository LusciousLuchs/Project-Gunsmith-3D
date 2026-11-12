// Gunsmith3D - 3D scene: parts, rounds, springs, magazines, effects.
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "app/render.hpp"
#include "app/fitbench.hpp"
#include "app/settings.hpp"
#include "carve.hpp"
#include "app/usertex.hpp"
#include "app/scene.hpp"
#include "app/engrave.hpp"
#include "rlgl.h"

#include <algorithm>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
        if (b.gone) continue;   // tidied away, or a round that went into the chamber
        int t = A.design.bodyIndex(b.tmpl.empty() ? b.id : b.tmpl);
        if (t < 0) continue;
        Matrix M = poseMatrix(t, b.x * 1000, b.y * 1000, b.a, b.roll);
        if (b.isMag) {   // a side magazine sliding in or out across the gun
            float dz = (float)(b.z * 1000 - A.design.bodies[t].z) * U;
            if (std::fabs(dz) > 1e-5f) M = MatrixMultiply(M, MatrixTranslate(0, 0, dz));
        }
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
    if (b.fixed || b.isMag || b.slice || !b.tmpl.empty()) return false;
    if (b.d->ctrl != "none" || b.m < 0.05) return false;
    if (b.root) return false;  // a welded-on piece: its piece is graphed once
    return true;
}

// ---------------------------------------------------------------- 3D scene
// A part's solid, centred on its outline's middle with y up (world units): its outline extruded (or turned, or shaped in
// depth), its cuts made (not while `rough`), its picture laid on, and leaned if tilted. Shared by the gun and attachments.
Mesh buildPartMesh(const BodyDef& dIn, bool rough) {
    BodyDef d = dIn;   // (a round part's shape is brought up to date on the copy)
    Mesh mesh{};
    V2 c = centroidOf(d);
    std::vector<V2> local;
    for (auto& p : outlineOf(d)) local.push_back({p.x - c.x, p.y - c.y});
    if (d.round) {  // turned on a lathe about its long middle line
        carve::syncRound(d);
        carve::RoundAxis ax = carve::roundAxis(d);
        std::vector<char> sm;
        std::vector<V2> S = carve::roundShape(d, 48, &sm);
        mesh = latheMesh(carve::roundProfile(d, ax), {ax.o.x - c.x, ax.o.y - c.y}, ax.dir, S, sm);
    } else if (depthShaped(d)) mesh = depthMesh(d, c);   // shaped in depth: corners on both faces
    else mesh = extrude(local, bodyDepth(d), (float)(d.smooth * std::max(1.0, d.thick) / 2));
    std::vector<CarveDef> cuts = rough ? std::vector<CarveDef>{} : d.carves;
    V2 rear, muz;
    if (!rough && barrelEnds(d, rear, muz)) {  // a barrel: its chamber, its bore, and the rifling's grooves (for the eye: the
        // chamber's walls are the cartridge physics' business, not the solid's)
        const Cartridge& ct = d.barrel.cart;
        double L = std::hypot(muz.x - rear.x, muz.y - rear.y);
        V2 u{(muz.x - rear.x) / L, (muz.y - rear.y) / L}, n{-u.y, u.x};
        CarveDef ch; ch.shape = "circle"; ch.at = rear; ch.atZ = 0; ch.dx = u.x; ch.dy = u.y; ch.dz = 0;
        ch.w = ch.h = std::max(ct.baseDia, ct.shoulderDia) + 0.4; ch.depth = ct.caseLen;
        cuts.push_back(ch);
        CarveDef bo = ch; bo.at = muz; bo.dx = -u.x; bo.dy = -u.y; bo.w = bo.h = ct.bulletDia; bo.depth = std::max(1.0, L - ct.caseLen + 0.5);
        cuts.push_back(bo);
        // (the rifling is drawn as a spiral along the bore: see drawRifling)
    }
    if (!cuts.empty()) {  // cut the carves out of the solid, in the mesh's frame (centred, y up)
        std::vector<std::vector<csg::Poly>> cut;
        for (auto& cv : cuts) {
            std::vector<csg::Poly> ps;
            for (auto& q : carve::prismPolys(carve::prism(cv, d), cv)) {
                std::vector<csg::Vert> vs;
                for (auto it = q.v.rbegin(); it != q.v.rend(); ++it)  // mirroring y turns the winding round
                    vs.push_back({{it->p.x - c.x, -(it->p.y - c.y), it->p.z - d.z}, {it->n.x, -it->n.y, it->n.z}});
                ps.push_back(csg::makePoly(vs));
            }
            cut.push_back(ps);
        }
        Mesh carved = carveMesh(mesh, cut);
        UnloadMesh(mesh);
        mesh = carved;
    }
    if (!d.texture.empty()) {  // lay the picture on every face from the side it faces (box mapping), in mm
        Mesh& m = mesh;
        double sc = std::max(1.0, d.texScale) * U;
        for (int v = 0; v < m.vertexCount; v++) {
            float x = m.vertices[v * 3], y = m.vertices[v * 3 + 1], z = m.vertices[v * 3 + 2];
            float nx = std::fabs(m.normals[v * 3]), ny = std::fabs(m.normals[v * 3 + 1]), nz = std::fabs(m.normals[v * 3 + 2]);
            float a, c;
            if (nz >= nx && nz >= ny) { a = x; c = -y; } else if (ny >= nx) { a = x; c = z; } else { a = z; c = -y; }
            m.texcoords[v * 2] = (float)(a / sc); m.texcoords[v * 2 + 1] = (float)(c / sc);
        }
        UpdateMeshBuffer(m, 1, m.texcoords, m.vertexCount * 2 * sizeof(float), 0);
    }
    if (carve::tilted(d)) {  // leaned out of the side view: turn the solid about its middle (mesh axes have y up)
        double R[3][3]; carve::tiltMatrix(d, R);
        const double f[3] = {1, -1, 1};
        Mesh& m = mesh;
        for (int v = 0; v < m.vertexCount; v++)
            for (float* arr : {m.vertices, m.normals}) {
                if (!arr) continue;
                double p[3] = {arr[v * 3], arr[v * 3 + 1], arr[v * 3 + 2]}, q[3];
                for (int r = 0; r < 3; r++) { q[r] = 0; for (int c = 0; c < 3; c++) q[r] += f[r] * R[r][c] * f[c] * p[c]; }
                for (int r = 0; r < 3; r++) arr[v * 3 + r] = (float)q[r];
            }
        UpdateMeshBuffer(m, 0, m.vertices, m.vertexCount * 3 * sizeof(float), 0);
        if (m.normals) UpdateMeshBuffer(m, 2, m.normals, m.vertexCount * 3 * sizeof(float), 0);
    }
    return mesh;
}
static void ensureMeshes() {
    auto& D = A.design;
    A.meshes.resize(D.bodies.size());
    for (size_t i = 0; i < D.bodies.size(); i++) {
        auto& d = D.bodies[i];
        if (!A.S) followMarks(d);   // engravings go with a part being moved, turned or flipped as a whole
        std::ostringstream k;
        k << (d.round ? 'R' : 'F') << (d.round ? 0.0 : d.thick) << 's' << d.smooth << 'n' << d.roundSides << 't' << d.roundTurn << 'w' << d.roundSweep << 'a' << d.roundAxis << 'T' << d.texture << ' ' << d.texScale << 'X' << d.tiltX << 'Y' << d.tiltY;
        for (auto& p : d.pts) k << ',' << p.x << ' ' << p.y;
        for (double r : d.rad) k << 'r' << r;
        for (double b : d.bulge) k << 'b' << b;
        if (depthShaped(d)) for (size_t q = 0; q < d.pts.size(); q++) k << 'D' << d.depthOff[q].x << ' ' << d.depthOff[q].y << ' ' << d.depthZn[q] << ' ' << d.depthZf[q];
        if (d.barrel.on) k << 'B' << d.barrel.cart.caseLen << ' ' << d.barrel.cart.headDia() << ' ' << d.barrel.cart.bulletDia << ' ' << d.barrel.rifled << d.barrel.grooves << d.barrel.muzzleEnd;
        for (auto& c : d.carves) k << 'c' << c.shape << c.at.x << ' ' << c.at.y << ' ' << c.atZ << ' ' << c.dx << ' ' << c.dy << ' ' << c.dz << ' ' << c.spin << ' ' << c.w << ' ' << c.h << ' ' << c.depth;
        std::string key = k.str();
        // while a part is being changed (dragged, typed) its cuts (carves, a barrel's bore and chamber) are left out, and
        // made once it has been still for a moment: cutting the solid every frame is what makes dragging slow
        const double SETTLE = 0.6;
        double now = GetTime();
        bool hasCuts = !d.carves.empty() || d.barrel.on;
        if (A.meshes[i].ok && A.meshes[i].key == key) {
            if (!(A.meshes[i].rough && now - A.meshes[i].changedAt >= SETTLE)) continue;   // (rough and settled: cut it now)
        } else if (A.meshes[i].ok) A.meshes[i].changedAt = now;
        bool rough = hasCuts && A.meshes[i].ok && now - A.meshes[i].changedAt < SETTLE && A.mode == Mode::Build;
        if (A.meshes[i].ok) UnloadMesh(A.meshes[i].mesh);
        if (d.round) carve::syncRound(d);   // (a round part's thickness is its width across)
        double tb = GetTime();
        A.meshes[i].mesh = buildPartMesh(d, rough);
        if (std::getenv("GUNSMITH_CARVEPERF")) std::printf("CARVEPERF part %zu (%d cuts, %s): %.1f ms, %d triangles\n", i, (int)d.carves.size(), rough ? "rough" : "cut", (GetTime() - tb) * 1000, A.meshes[i].mesh.triangleCount);
        A.meshes[i].key = key;
        A.meshes[i].ok = true;
        A.meshes[i].rough = rough;
    }
}
// A barrel's rifling: its grooves as lines spiralling down the bore, one turn per twist length, from the chamber's
// mouth to the muzzle. (Drawn rather than cut: a spiral cut would be slow, and only the eye sees it.)
static void drawRifling(int tmpl, Matrix M) {
    const BodyDef& d = A.design.bodies[tmpl];
    V2 rear, muz;
    if (!d.barrel.on || !d.barrel.rifled || !barrelEnds(d, rear, muz)) return;
    const Cartridge& ct = d.barrel.cart;
    double L = std::hypot(muz.x - rear.x, muz.y - rear.y);
    if (L <= ct.caseLen + 1) return;
    V2 u{(muz.x - rear.x) / L, (muz.y - rear.y) / L}, c = centroidOf(d);
    auto local = [&](double s, double ang, double r) {   // a point s mm from the breech end, at angle ang round the bore, radius r
        V2 p{rear.x + u.x * s, rear.y + u.y * s};
        V2 nI{-u.y, u.x};                                 // across the bore in the side view
        double cy = std::cos(ang) * r, cz = std::sin(ang) * r;
        return Vector3{(float)((p.x + nI.x * cy - c.x) * U), (float)(-(p.y + nI.y * cy - c.y) * U), (float)(cz * U)};
    };
    double r = ct.bulletDia / 2 * 0.985, twist = std::max(20.0, d.barrel.twist);
    int steps = (int)std::clamp((L - ct.caseLen) / 2.0, 8.0, 400.0);
    Color col{28, 30, 34, 255};
    for (int g = 0; g < d.barrel.grooves; g++) {
        double a0 = 2 * PI * g / d.barrel.grooves;
        for (int k = 0; k < steps; k++) {
            double s0 = ct.caseLen + (L - ct.caseLen) * k / steps, s1 = ct.caseLen + (L - ct.caseLen) * (k + 1) / steps;
            Vector3 p0 = Vector3Transform(local(s0, a0 + 2 * PI * (s0 - ct.caseLen) / twist, r), M);
            Vector3 p1 = Vector3Transform(local(s1, a0 + 2 * PI * (s1 - ct.caseLen) / twist, r), M);
            DrawLine3D(p0, p1, col);
        }
    }
}
float plateY() { return -(float)groundLevelMM(A.design) * U; }
static void drawOutline(int i, Matrix M, Color c) {
    const BodyDef& d = A.design.bodies[i];
    V2 cen = centroidOf(d);
    if (depthShaped(d) && !d.round) {   // shaped in depth: its own corners on each face, and the edges between them
        std::vector<P3d> N, F;
        depthRings(d, N, F);
        size_t n = N.size();
        auto at = [&](const P3d& p) { return Vector3Transform({(float)(p.x - cen.x) * U, -(float)(p.y - cen.y) * U, (float)(p.z - d.z) * U}, M); };
        for (size_t k = 0; k < n; k++) {
            DrawLine3D(at(N[k]), at(N[(k + 1) % n]), c);
            DrawLine3D(at(F[k]), at(F[(k + 1) % n]), c);
            DrawLine3D(at(N[k]), at(F[k]), c);
        }
        return;
    }
    auto o = outlineOf(d);
    float h = d.round ? 0.f : bodyDepth(d) / 2 + 0.003f;  // a round part: its silhouette, on its middle line
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
        // depth edges at real corners, not along curves: where the outline turns, and at every corner drawn (even in a
        // straight line, like the seam an extrusion leaves)
        V2 a = o[(k + n - 1) % n], p = o[k], q = o[(k + 1) % n];
        double t1 = std::atan2(p.y - a.y, p.x - a.x), t2 = std::atan2(q.y - p.y, q.x - p.x);
        bool corner = std::fabs(std::remainder(t2 - t1, 2 * PI)) > 25 * DEG2RAD;
        if (!corner) for (auto& dp : d.pts) if (std::fabs(dp.x - p.x) < 1e-3 && std::fabs(dp.y - p.y) < 1e-3) { corner = true; break; }
        if (corner) DrawLine3D(f[k], b[k], c);
    }
}
// Lathe-turned cartridge meshes, built from the cartridge's profile. Axis along +X, head at x = 0.
struct RoundMesh { std::string key; Mesh caseM{}, bulletM{}, hullM{}, hullFiredM{}; bool ok = false, bullet = false, hull = false; };
std::vector<V2> hullProfile(const Cartridge& c, bool fired) {  // a shotshell's hull, from inside its brass head to the crimp
    if (c.kind != "shotshell") return {};
    double base = c.baseDia / 2, cr = std::min(3.0, c.caseLen * 0.08), x0 = std::min(c.rimThick, c.caseLen - cr - 0.5);
    if (fired) return {{x0, 0}, {x0, base}, {c.caseLen, base}, {c.caseLen, base * 0.88}};
    return {{x0, 0}, {x0, base}, {c.caseLen - cr, base}, {c.caseLen, base * 0.62}, {c.caseLen - 0.8, base * 0.2}, {c.caseLen - 0.8, 0}};
}
std::vector<V2> caseProfile(const Cartridge& c) {  // (x along the axis, radius), mm
    double rim = c.rimDia / 2, base = c.baseDia / 2, sh = c.shoulderDia / 2, neck = c.neckDia / 2;
    if (c.kind == "shotshell") {  // the brass head, a touch proud of the hull it holds
        double hl = std::clamp(c.headLen, c.rimThick + 0.5, c.caseLen - 1);
        return {{0, 0}, {0, rim}, {c.rimThick, rim}, {c.rimThick, base + 0.08}, {hl, base + 0.08}, {hl, base * 0.9}};
    }
    if (c.kind == "caseless") return {{0, 0}, {0, base}, {c.caseLen, base}, {c.caseLen, std::min(base, c.bulletDia / 2)}};
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
    if (c.kind == "shotshell") return {};  // the shot or slug is inside the hull
    auto shape = bulletShape(c);
    if (!shape.empty()) {  // the designed shape, closed on the axis at its base
        std::vector<V2> p = {{shape.front().x, 0}};
        for (auto& q : shape) p.push_back(q);
        if (p.back().y > 0.001) p.push_back({p.back().x, 0});
        return p;
    }
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
            // wound so the outside faces out (the other way round, only the inside was drawn: rounds looked hollow)
            V2 q[6][2] = {{a, {t0, 0}}, {b, {t1, 0}}, {b, {t0, 0}}, {a, {t0, 0}}, {a, {t1, 0}}, {b, {t1, 0}}};
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
static RoundMesh& ensureRoundMesh(const Cartridge& c) {  // one set of meshes per cartridge (barrels can each take their own)
    char buf[320];
    std::snprintf(buf, sizeof buf, "%g/%g/%g/%g/%g/%g/%g/%g/%g/%g/%g/%d/%s/%g/%g/%g/%g", c.caseLen, c.rimDia, c.rimThick, c.baseDia, c.shoulderDia, c.shoulderAt, c.neckDia, c.bulletDia, c.oal, c.noseLen, c.bulletMass, (int)c.rimmed,
                  c.nose.c_str(), c.meplat, c.tailLen, c.tailDia, c.seat);
    std::string key = buf;
    for (auto& q : c.custom) key += "/" + std::to_string(q.x) + "," + std::to_string(q.y);
    key += "/" + c.kind + "/" + std::to_string(c.headLen);
    static std::map<std::string, RoundMesh> cache;
    RoundMesh& RM = cache[key];
    if (RM.ok) return RM;
    RM.caseM = lathe(caseProfile(c));
    auto bp = bulletProfile(c);
    RM.bullet = bp.size() >= 2;
    if (RM.bullet) RM.bulletM = lathe(bp);
    RM.hull = c.kind == "shotshell";
    if (RM.hull) { RM.hullM = lathe(hullProfile(c, false)); RM.hullFiredM = lathe(hullProfile(c, true)); }
    RM.key = key; RM.ok = true;
    return RM;
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
static void drawCartridgeAt(Vector3 o, Vector3 dir, bool caseOnly = false, float bright = 1.f, const Cartridge* cart = nullptr) {
    const Cartridge& c = cart ? *cart : A.design.breech.cart;
    RoundMesh& RM = ensureRoundMesh(c);
    Matrix M = MatrixMultiply(alignX(dir), MatrixTranslate(o.x, o.y, o.z));
    setSpecular(0.75f);  // polished brass and copper
    if (c.kind == "caseless") {  // a block of propellant: matt
        setSpecular(0.15f);
        if (!caseOnly) drawMesh(RM.caseM, M, shade(Color{122, 98, 70, 255}, bright));
        setSpecular(0.75f);
    } else drawMesh(RM.caseM, M, shade(Color{214, 168, 78, 255}, bright));
    if (!caseOnly && RM.bullet) drawMesh(RM.bulletM, M, shade(Color{184, 102, 62, 255}, bright));
    if (RM.hull) {
        unsigned h = c.hullColor;
        setSpecular(0.35f);  // plastic
        drawMesh(caseOnly ? RM.hullFiredM : RM.hullM, M, shade(Color{(unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), 255}, bright));
    }
    setSpecular(0.3f);
}
static void drawCartridge(const std::string& bodyId, V2 face, float zc, bool caseOnly = false, const Cartridge* cart = nullptr, const V2* muzzle = nullptr) {
    V2 to{face.x + 1, face.y};   // along the bore: toward the muzzle when there is one (a barrel can point any way)
    if (muzzle) { double dx = muzzle->x - face.x, dy = muzzle->y - face.y, l = std::hypot(dx, dy); if (l > 1) to = {face.x + dx / l, face.y + dy / l}; }
    V2 f = curMM(bodyId, face), ax = curMM(bodyId, to);
    Vector3 o = W3(f, zc), dir = Vector3Normalize(Vector3Subtract(W3(ax, zc), o));
    drawCartridgeAt(o, dir, caseOnly, 1.f, cart);
}
// a round's direction in a magazine whose round angle is set by hand: from the bore, + nose up
static V2 setRoundDir(const BreechDef& br, const BodyDef& d) {
    double gx = br.hasMuzzle && br.hasFace ? br.muzzle.x - br.face.x : 1, gy = br.hasMuzzle && br.hasFace ? br.muzzle.y - br.face.y : 0, l = std::hypot(gx, gy);
    V2 g = l > 1e-9 ? V2{gx / l, gy / l} : V2{1, 0}, up{g.y, -g.x};
    double a = d.roundAngle * DEG2RAD;
    return {g.x * std::cos(a) + up.x * std::sin(a), g.y * std::cos(a) + up.y * std::sin(a)};
}
static void drawMagRounds(const RB& r, int rounds, int skip = 0) {  // skip: top slots shown elsewhere  // rounds stacked inside a magazine, top round at the lips
    auto& br = A.design.breech;
    if (!br.on || rounds <= 0) return;
    const BodyDef& d = A.design.bodies[r.tmpl];
    V2 c = centroidOf(d);
    if (!d.feedPawl.empty() && d.roundPath.size() >= 2 && d.roundStyle != "line") {
        // a belt: the next round waits in the tray at the first path point, the rest hang out of the left side, linked
        double dia = br.cart.headDia() * 1.02, oal = br.cart.oal, pitch = dia * 1.25;
        double mx = br.hasMuzzle && br.hasFace ? br.muzzle.x - br.face.x : 1, my = br.hasMuzzle && br.hasFace ? br.muzzle.y - br.face.y : 0;
        V2 p0 = d.roundPath[0], p1 = d.roundPath[1];
        double l = std::max(1e-9, std::hypot(p1.x - p0.x, p1.y - p0.y));
        V2 u{(p1.x - p0.x) / l, (p1.y - p0.y) / l}, fwd{-u.y, u.x};
        if (fwd.x * mx + fwd.y * my < 0) fwd = {-fwd.x, -fwd.y};
        V2 head{p0.x - fwd.x * oal / 2, p0.y - fwd.y * oal / 2};
        bool waiting = r.sb && !r.sb->top && !r.sb->indexReady;  // just stripped: the tray is empty until the pawl moves the belt
        int n = std::min(rounds, 40);
        for (int k = skip; k < n; k++) {
            float z = -(float)((k + (waiting ? 1 : 0)) * pitch) * U;
            Vector3 o = Vector3Transform({(float)(head.x - c.x) * U, -(float)(head.y - c.y) * U, z}, r.M);
            Vector3 o2 = Vector3Transform({(float)(head.x + fwd.x - c.x) * U, -(float)(head.y + fwd.y - c.y) * U, z}, r.M);
            drawCartridgeAt(o, Vector3Normalize(Vector3Subtract(o2, o)));
            // its link: a band round the case, joined to the next
            Vector3 lk = Vector3Transform({(float)(head.x + fwd.x * oal * 0.35 - c.x) * U, -(float)(head.y + fwd.y * oal * 0.35 - c.y) * U, z}, r.M);
            Vector3 lk2 = Vector3Transform({(float)(head.x + fwd.x * oal * 0.35 - c.x) * U, -(float)(head.y + fwd.y * oal * 0.35 - c.y) * U, z - (float)pitch * U}, r.M);
            if (k + 1 < n) drawCyl(lk, lk2, (float)(dia * 0.16) * U, Color{70, 72, 78, 255});
        }
        return;
    }
    if (d.roundPath.size() >= 2) {  // along the path you drew: side by side across it, or nose to tail along it
        bool line = d.roundStyle == "line";
        double dia = br.cart.headDia() * 1.02, oal = br.cart.oal;
        bool dbl = magStackOf(d) == 2;
        double pitch = line ? oal * 1.01 : (dbl ? dia * 0.56 : dia * 0.98);
        double mx = br.hasMuzzle && br.hasFace ? br.muzzle.x - br.face.x : 1, my = br.hasMuzzle && br.hasFace ? br.muzzle.y - br.face.y : 0;
        std::vector<double> cum{0};
        for (size_t i = 0; i + 1 < d.roundPath.size(); i++) cum.push_back(cum.back() + std::hypot(d.roundPath[i + 1].x - d.roundPath[i].x, d.roundPath[i + 1].y - d.roundPath[i].y));
        int n = std::min(rounds, d.magCap);
        for (int k = skip; k < n; k++) {
            double s = (line ? 0 : dia / 2) + k * pitch;
            if (s > cum.back() + 1e-6) break;   // no room left on the path
            size_t i = 0;
            while (i + 2 < d.roundPath.size() && s > cum[i + 1]) i++;
            V2 a = d.roundPath[i], b = d.roundPath[i + 1];
            double l = std::max(1e-9, cum[i + 1] - cum[i]);
            V2 u{(b.x - a.x) / l, (b.y - a.y) / l}, m{a.x + u.x * (s - cum[i]), a.y + u.y * (s - cum[i])};
            V2 fwd, head;
            if (line) { fwd = u; head = m; }   // its head towards the mouth, nose pointing in
            else { fwd = {-u.y, u.x}; if (fwd.x * mx + fwd.y * my < 0) fwd = {-fwd.x, -fwd.y}; if (d.roundSet) fwd = setRoundDir(br, d); head = {m.x - fwd.x * oal / 2, m.y - fwd.y * oal / 2}; }
            float z = (!line && dbl) ? (k % 2 ? 1.f : -1.f) * (float)(dia * 0.25) * U : 0;
            Vector3 o = Vector3Transform({(float)(head.x - c.x) * U, -(float)(head.y - c.y) * U, z}, r.M);
            Vector3 o2 = Vector3Transform({(float)(head.x + fwd.x - c.x) * U, -(float)(head.y + fwd.y - c.y) * U, z}, r.M);
            drawCartridgeAt(o, Vector3Normalize(Vector3Subtract(o2, o)));
        }
        return;
    }
    if (d.gen.on && d.gen.side) {   // across the gun: stacked from the mouth outward, staggered for double stack
        double dia = br.cart.headDia() * 1.02, pitch = d.gen.stack == 2 ? dia * 0.56 : dia * 0.98;
        double mx = br.hasMuzzle && br.hasFace ? br.muzzle.x - br.face.x : 1, my = br.hasMuzzle && br.hasFace ? br.muzzle.y - br.face.y : 0, ml = std::hypot(mx, my);
        V2 fwd = ml > 0 ? V2{mx / ml, my / ml} : V2{1, 0};
        V2 mid{d.gen.top.x, d.gen.top.y + 3 + dia / 2};
        float sd = d.gen.side > 0 ? 1.f : -1.f;
        int n = std::min(rounds, d.magCap);
        for (int k = skip; k < n; k++) {
            double across = 3 + dia / 2 + k * pitch - d.thick / 2;   // from the magazine's middle, toward its far end
            V2 m{mid.x, mid.y + (d.gen.stack == 2 ? (k % 2 ? 1 : -1) * dia * 0.25 : 0)};
            V2 head{m.x - fwd.x * br.cart.oal / 2, m.y - fwd.y * br.cart.oal / 2};
            float z = sd * (float)across * U;
            Vector3 o = Vector3Transform({(float)(head.x - c.x) * U, -(float)(head.y - c.y) * U, z}, r.M);
            Vector3 o2 = Vector3Transform({(float)(head.x + fwd.x - c.x) * U, -(float)(head.y + fwd.y - c.y) * U, z}, r.M);
            drawCartridgeAt(o, Vector3Normalize(Vector3Subtract(o2, o)));
        }
        return;
    }
    if (d.gen.on) {  // generated magazine: rounds follow its centreline, staggered for double stack
        double dia = br.cart.headDia() * 1.02, pitch = magStackOf(d) == 2 ? dia * 0.56 : dia * 0.98;
        double mx = br.hasMuzzle && br.hasFace ? br.muzzle.x - br.face.x : 1, my = br.hasMuzzle && br.hasFace ? br.muzzle.y - br.face.y : 0;
        int n = std::min(rounds, d.magCap);
        for (int k = skip; k < n; k++) {
            double s = 3 + dia / 2 + k * pitch, phi;
            V2 m = magCentre(d.gen, s, &phi);
            V2 fwd{std::cos(phi * DEG2RAD), -std::sin(phi * DEG2RAD)};
            if (fwd.x * mx + fwd.y * my < 0) fwd = {-fwd.x, -fwd.y};   // a magazine on top: they still point at the muzzle
            if (d.roundSet) fwd = setRoundDir(br, d);
            V2 head{m.x - fwd.x * br.cart.oal / 2, m.y - fwd.y * br.cart.oal / 2};
            float z = magStackOf(d) == 2 ? (k % 2 ? 1.f : -1.f) * (float)(dia * 0.25) * U : 0;
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
// ---------------------------------------------------------------- springs
// Each spring is one mesh: a coil is a single smooth tube along its helix, a leaf a single strip. The shape is
// recomputed only when the spring moves and written into the mesh's existing GPU buffers, so a spring costs one
// draw call instead of a hundred little cylinders.
struct DynMesh { Mesh m{}; int verts = 0, tris = 0; bool ok = false; std::vector<float> key; };
static std::vector<DynMesh> gSpringMeshes;   // by slot: three per joint (whole, and the two halves of a buckled coil)
static DynMesh& springSlot(size_t i) { if (gSpringMeshes.size() <= i) gSpringMeshes.resize(i + 1); return gSpringMeshes[i]; }
static bool prepare(DynMesh& d, int verts, int tris) {  // make room for this many vertices and triangles; true if new
    if (d.ok && d.verts == verts && d.tris == tris) return false;
    if (d.ok) UnloadMesh(d.m);
    d.m = Mesh{};
    d.m.vertexCount = verts; d.m.triangleCount = tris;
    d.m.vertices = (float*)MemAlloc(verts * 3 * sizeof(float));
    d.m.normals = (float*)MemAlloc(verts * 3 * sizeof(float));
    d.m.texcoords = (float*)MemAlloc(verts * 2 * sizeof(float));
    d.m.indices = (unsigned short*)MemAlloc(tris * 3 * sizeof(unsigned short));
    d.verts = verts; d.tris = tris; d.ok = true; d.key.clear();
    return true;
}
static void finish(DynMesh& d, bool fresh) {
    if (fresh) { UploadMesh(&d.m, true); return; }
    UpdateMeshBuffer(d.m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, d.m.vertices, d.verts * 3 * sizeof(float), 0);
    UpdateMeshBuffer(d.m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, d.m.normals, d.verts * 3 * sizeof(float), 0);
}
static void drawDyn(const DynMesh& d, Color c) {
    rlDisableBackfaceCulling();   // thin wire and strips: never let a wrong winding hide a face
    drawMesh(d.m, MatrixIdentity(), c);
    rlEnableBackfaceCulling();
}
// A coil spring from a to b: `turns` active coils plus a closed coil at each end, the way real springs are wound,
// so it looks solid at its solid height. The wire stays round however far it's compressed. `bow`: a buckled spring
// bends into a smooth arc, out by this much (world units, and that way) at its middle.
static void drawCoil(size_t slot, Vector3 a, Vector3 b, float coilR, float wireR, double activeTurns, Color c, Vector3 bow = {0, 0, 0}) {
    Vector3 d = Vector3Subtract(b, a);
    float L = Vector3Length(d);
    if (L < 1e-4f) return;
    int turns = std::clamp((int)std::lround(activeTurns), 1, 60), total = turns + 2;
    const int SEG = 14, SIDES = 8;
    int rings = total * SEG + 1;
    if (rings * SIDES > 65000) return;
    DynMesh& D = springSlot(slot);
    bool fresh = prepare(D, rings * SIDES, (rings - 1) * SIDES * 2);
    std::vector<float> key = {a.x, a.y, a.z, b.x, b.y, b.z, coilR, wireR, bow.x, bow.y, bow.z};
    if (fresh || key != D.key) {
        D.key = key;
        Vector3 ax0 = Vector3Scale(d, 1 / L);
        float bowL = Vector3Length(bow);
        Vector3 up = bowL > 1e-6f ? Vector3Scale(bow, 1 / bowL) : std::fabs(ax0.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        Vector3 w = Vector3Normalize(Vector3CrossProduct(ax0, up));   // across the bend: the same all along
        float wd = 2 * wireR, span = std::max(L - wd, 1e-4f);
        float e = std::min(wd, span / total), act = (span - 2 * e) / turns;   // pitch of the closed end coils and the active ones
        float* P = D.m.vertices; float* N = D.m.normals;
        for (int r = 0; r < rings; r++) {
            float s = (float)r / SEG;   // coils from the a end
            float z = wireR + e * std::min(s, 1.f) + act * std::clamp(s - 1, 0.f, (float)turns) + e * std::max(0.f, s - 1 - turns);
            float pitch = (s < 1 || s > 1 + turns) ? e : act;
            float ang = s * 2 * PI, ca = std::cos(ang), sa = std::sin(ang);
            // the middle line: straight, or a half wave out to `bow` at the middle, with the coil square to it
            float tt = std::clamp(z / L, 0.f, 1.f);
            Vector3 mid = Vector3Add(Vector3Add(a, Vector3Scale(ax0, z)), Vector3Scale(bow, std::sin(PI * tt)));
            Vector3 ax = Vector3Normalize(Vector3Add(ax0, Vector3Scale(bow, PI / L * std::cos(PI * tt))));
            Vector3 v = Vector3Normalize(Vector3CrossProduct(ax, w)), u = Vector3CrossProduct(v, ax);
            Vector3 rad = Vector3Add(Vector3Scale(u, ca), Vector3Scale(v, sa));
            Vector3 ctr = Vector3Add(mid, Vector3Scale(rad, coilR));
            Vector3 T = Vector3Normalize(Vector3Add(Vector3Scale(ax, pitch), Vector3Scale(Vector3Add(Vector3Scale(u, -sa), Vector3Scale(v, ca)), coilR * 2 * PI)));
            Vector3 B = Vector3Normalize(Vector3CrossProduct(T, rad)), Nn = Vector3CrossProduct(B, T);
            for (int k = 0; k < SIDES; k++) {
                float ph = 2 * PI * k / SIDES;
                Vector3 n = Vector3Add(Vector3Scale(Nn, std::cos(ph)), Vector3Scale(B, std::sin(ph)));
                int i = (r * SIDES + k) * 3;
                P[i] = ctr.x + n.x * wireR; P[i + 1] = ctr.y + n.y * wireR; P[i + 2] = ctr.z + n.z * wireR;
                N[i] = n.x; N[i + 1] = n.y; N[i + 2] = n.z;
            }
        }
        if (fresh) {
            unsigned short* I = D.m.indices;
            int t = 0;
            for (int r = 0; r + 1 < rings; r++)
                for (int k = 0; k < SIDES; k++) {
                    unsigned short i0 = r * SIDES + k, i1 = r * SIDES + (k + 1) % SIDES, i2 = (r + 1) * SIDES + k, i3 = (r + 1) * SIDES + (k + 1) % SIDES;
                    I[t++] = i0; I[t++] = i1; I[t++] = i2;
                    I[t++] = i1; I[t++] = i3; I[t++] = i2;
                }
        }
        finish(D, fresh);
    }
    drawDyn(D, c);
}
// A leaf spring: a flat strip bent along a quadratic curve from its root, through mid, to its tip.
static void drawLeaf(size_t slot, Vector3 a, Vector3 mid, Vector3 b, float thick, float width, Color c) {
    const int K = 12;   // points along the leaf
    DynMesh& D = springSlot(slot);
    bool fresh = prepare(D, (K + 1) * 8, K * 8);
    std::vector<float> key = {a.x, a.y, a.z, mid.x, mid.y, mid.z, b.x, b.y, b.z, thick, width};
    if (fresh || key != D.key) {
        D.key = key;
        float* P = D.m.vertices; float* N = D.m.normals;
        auto at = [&](float t) {
            return Vector3Add(Vector3Add(Vector3Scale(a, (1 - t) * (1 - t)), Vector3Scale(mid, 2 * (1 - t) * t)), Vector3Scale(b, t * t));
        };
        Vector3 Z{0, 0, 1};
        for (int k = 0; k <= K; k++) {
            float t = (float)k / K;
            Vector3 q = at(t), tg = Vector3Subtract(at(std::min(1.f, t + 0.01f)), at(std::max(0.f, t - 0.01f)));
            Vector3 m = Vector3Normalize(Vector3CrossProduct(Z, tg));   // across the strip, in the side-view plane
            Vector3 c0 = Vector3Add(q, Vector3Add(Vector3Scale(m, thick / 2), Vector3Scale(Z, width / 2)));
            Vector3 c1 = Vector3Add(q, Vector3Add(Vector3Scale(m, thick / 2), Vector3Scale(Z, -width / 2)));
            Vector3 c2 = Vector3Add(q, Vector3Add(Vector3Scale(m, -thick / 2), Vector3Scale(Z, -width / 2)));
            Vector3 c3 = Vector3Add(q, Vector3Add(Vector3Scale(m, -thick / 2), Vector3Scale(Z, width / 2)));
            Vector3 face[4][3] = {{c0, c1, m}, {c2, c3, Vector3Negate(m)}, {c3, c0, Z}, {c1, c2, Vector3Negate(Z)}};
            for (int f = 0; f < 4; f++)
                for (int e = 0; e < 2; e++) {
                    int i = (k * 8 + f * 2 + e) * 3;
                    Vector3 p = face[f][e], n = face[f][2];
                    P[i] = p.x; P[i + 1] = p.y; P[i + 2] = p.z;
                    N[i] = n.x; N[i + 1] = n.y; N[i + 2] = n.z;
                }
        }
        if (fresh) {
            unsigned short* I = D.m.indices;
            int t = 0;
            for (int k = 0; k < K; k++)
                for (int f = 0; f < 4; f++) {
                    unsigned short p0 = k * 8 + f * 2, q0 = p0 + 1, p1 = p0 + 8, q1 = q0 + 8;
                    I[t++] = p0; I[t++] = q0; I[t++] = p1;
                    I[t++] = q0; I[t++] = q1; I[t++] = p1;
                }
        }
        finish(D, fresh);
    }
    drawDyn(D, c);
}
static float partZ(const std::string& id) { int i = A.design.bodyIndex(id); return i >= 0 ? bodyZ(i) : 0.f; }
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
BenchLayout benchLayout() {
    BenchLayout L;
    const Design& D = A.design;
    double x0 = 1e9, x1 = -1e9, y1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (auto& b : D.bodies) {
        for (auto& p : b.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
        z0 = std::min(z0, b.z - b.thick / 2); z1 = std::max(z1, b.z + b.thick / 2);
    }
    if (x0 > x1) { x0 = 0; x1 = 200; y1 = 60; z0 = -10; z1 = 10; }
    L.x0 = x0 - 320; L.x1 = x1 + 320;
    L.top = y1 + 25;                         // just under the gun's lowest point
    L.zFront = z1 + 160; L.zBack = z0 - 420;
    L.rulerLen = (x1 - x0) > 380 ? 600 : 300;
    L.rulerX0 = std::floor(x0 / 10) * 10;
    L.rulerZ = z1 + 70;                      // standing on its edge in front of the gun, its scale facing you
    L.ok = true;
    return L;
}
static void drawWorkbench() {
    BenchLayout L = benchLayout();
    auto P = [&](double x, double y, double z) { return Vector3{(float)x * U, -(float)y * U, (float)z * U}; };
    const Color wood{132, 94, 62, 255}, woodDark{104, 72, 48, 255}, steel{184, 188, 196, 255}, ink{30, 32, 36, 255};
    // the top, and its legs
    drawBox(P((L.x0 + L.x1) / 2, L.top + 22, (L.zFront + L.zBack) / 2), {(float)(L.x1 - L.x0) * U, 44 * U, (float)(L.zFront - L.zBack) * U}, wood);
    for (double lx : {L.x0 + 70, L.x1 - 70})
        for (double lz : {L.zFront - 70, L.zBack + 70}) drawBox(P(lx, L.top + 44 + 360, lz), {70 * U, 720 * U, 70 * U}, woodDark);
    // the ruler: a steel rule on its edge, 20 mm tall, marked every millimetre (every 1/16" in imperial)
    bool imp = CFG.units == Units::Imperial;
    double len = L.rulerLen, rz = L.rulerZ, rtop = L.top - 20;
    drawBox(P(L.rulerX0 + len / 2, L.top - 10, rz), {(float)len * U, 20 * U, 1.2f * U}, steel);
    double step = imp ? 25.4 / 16 : 1;
    int n = (int)(len / step);
    for (int k = 0; k <= n; k++) {
        double x = L.rulerX0 + k * step;
        int big = imp ? 16 : 10, mid = imp ? 8 : 5;
        double h = k % big == 0 ? 9 : k % mid == 0 ? 6 : (imp && k % 4 == 0) ? 4.5 : 3;
        drawBox(P(x, rtop + h / 2, rz + 0.7), {0.35f * U, (float)h * U, 0.2f * U}, ink);
    }
    // dressing (Smiffy's layout): a gun mount under the gun, ammo boxes, loose cases and a magazine rack on the left,
    // oil cans on the right; past the gun's ends so the side view stays clear
    double ty = L.top, gx0 = L.x0 + 320, gx1 = L.x1 - 320, gl = gx1 - gx0, zm = (L.zFront + L.zBack) / 2;
    double zg = ((L.zFront - 160) + (L.zBack + 420)) / 2;  // under the middle of the gun
    const Color blue{58, 112, 196, 255}, maroon{122, 34, 34, 255}, brass{214, 168, 78, 255}, grey{150, 154, 160, 255};
    // the gun mount: a blue base along the gun with a padded rest at each end, just under it
    drawBox(P(gx0 + gl / 2, ty - 6, zg), {(float)(gl * 0.8) * U, 12 * U, 70 * U}, blue);
    for (double f : {0.18, 0.82}) {
        drawBox(P(gx0 + gl * f, ty - 16, zg), {26 * U, 20 * U, 60 * U}, blue);
        drawBox(P(gx0 + gl * f, ty - 25, zg), {30 * U, 3 * U, 44 * U}, Color{40, 40, 44, 255});  // rubber pads
    }
    // ammo boxes
    drawBox(P(gx0 - 210, ty - 30, L.zBack + 230), {130 * U, 60 * U, 90 * U}, maroon);
    drawBox(P(gx0 - 210, ty - 61, L.zBack + 230), {134 * U, 3 * U, 94 * U}, shade(maroon, 0.8f));
    drawBox(P(gx0 - 230, ty - 25, L.zBack + 360), {70 * U, 50 * U, 120 * U}, maroon);
    // a few empty cases, lying about and standing
    const double cs[][4] = {{-120, 300, 0.4, 0}, {-95, 330, 1.9, 0}, {-150, 270, 2.8, 0}, {-110, 380, 0, 1}};
    for (auto& c : cs) {
        double x = gx0 + c[0], z = L.zBack + c[1];
        if (c[3] > 0) drawCyl(P(x, ty, z), P(x, ty - 19, z), 4.8f * U, brass);
        else drawCyl(P(x, ty - 4.8, z), P(x + 19 * std::cos(c[2]), ty - 4.8, z + 19 * std::sin(c[2])), 4.8f * U, brass);
    }
    // a magazine rack: a board with slots, a few magazines standing in it
    drawBox(P(gx0 - 210, ty - 8, L.rulerZ - 30), {150 * U, 16 * U, 50 * U}, Color{120, 84, 54, 255});
    for (int k = 0; k < 4; k++) drawBox(P(gx0 - 270 + k * 38, ty - 16 - 60, L.rulerZ - 30), {28 * U, 120 * U, 12 * U}, Color{48, 52, 60, 255});
    // gun oil cans
    drawCyl(P(gx1 + 160, ty, zm - 60), P(gx1 + 160, ty - 120, zm - 60), 32 * U, grey);
    drawCyl(P(gx1 + 160, ty - 120, zm - 60), P(gx1 + 160, ty - 150, zm - 60), 4 * U, Color{60, 60, 64, 255});
    drawCyl(P(gx1 + 230, ty, zm + 20), P(gx1 + 230, ty - 90, zm + 20), 26 * U, Color{176, 60, 48, 255});
    drawCyl(P(gx1 + 230, ty - 90, zm + 20), P(gx1 + 230, ty - 100, zm + 20), 9 * U, grey);
}
// ---------------------------------------------------------------- attachments on the gun
struct AttMeshes { std::string key; std::vector<Mesh> m; };
static std::map<std::string, AttMeshes> gAttMeshes;
static const std::vector<Mesh>& attMeshes(const std::string& file, const Design& att) {
    std::string key = att.name + "#" + std::to_string(att.bodies.size()) + "#" + std::to_string(std::hash<std::string>()(designToJson(att)));
    auto& e = gAttMeshes[file];
    if (e.key != key) {
        for (auto& m : e.m) UnloadMesh(m);
        e.m.clear();
        for (auto& b : att.bodies) e.m.push_back(buildPartMesh(b, false));
        e.key = key;
    }
    return e.m;
}
static void drawAttachment(const std::string& file, const Design& att, Matrix Mfit, float alpha, Color tint) {
    const auto& ms = attMeshes(file, att);
    for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) rlDisableDepthMask();
    for (size_t i = 0; i < att.bodies.size() && i < ms.size(); i++) {
        const BodyDef& b = att.bodies[i];
        V2 c = centroidOf(b);
        Matrix M = MatrixMultiply(MatrixTranslate((float)c.x * U, -(float)c.y * U, (float)(b.z * U)), Mfit);
        Color col = tint.a ? tint : hexColor(b.color);
        if (b.opacity < 0.99 && pass == 0) continue;   // (clear parts after the solid ones, so what's behind them shows)
        if (b.opacity >= 0.99 && pass == 1) continue;
        float a = alpha * (float)b.opacity;
        if (!b.texture.empty() && !tint.a) drawMeshTex(ms[i], M, col, userTexture(b.texture), a);
        else drawMesh(ms[i], M, col, a);
    }
    if (pass == 1) rlEnableDepthMask();
    }
}
void drawDesignModel(const std::string& key, const Design& d, Matrix M) {
    // (the same design object as last time: its meshes are still good; comparing whole blueprints every frame is slow)
    static std::map<std::string, const Design*> seen;
    auto it = gAttMeshes.find(key);
    if (it == gAttMeshes.end() || seen[key] != &d || it->second.m.size() != d.bodies.size()) { attMeshes(key, d); seen[key] = &d; }
    const auto& ms = gAttMeshes[key].m;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) rlDisableDepthMask();
        for (size_t i = 0; i < d.bodies.size() && i < ms.size(); i++) {
            const BodyDef& b = d.bodies[i];
            if ((b.opacity < 0.99) != (pass == 1)) continue;
            V2 c = centroidOf(b);
            Matrix Mb = MatrixMultiply(MatrixTranslate((float)c.x * U, -(float)c.y * U, (float)(b.z * U)), M);
            if (!b.texture.empty()) drawMeshTex(ms[i], Mb, hexColor(b.color), userTexture(b.texture), (float)b.opacity);
            else drawMesh(ms[i], Mb, hexColor(b.color), (float)b.opacity);
        }
        if (pass == 1) rlEnableDepthMask();
    }
}
static float gOpticAlpha = 1;   // the optic you're aiming through fades (you look through it, not at it)
void setOpticSeeThrough(float alpha) { gOpticAlpha = alpha; }
void drawAttachments(const std::vector<RB>& list) {
    auto delta = [&](const std::string& carrier) {   // a part it rides on that has moved (a slide with a rail): with it
        int bi = A.design.bodyIndex(carrier);
        if (bi < 0) return MatrixIdentity();
        for (auto& r : list) if (r.tmpl == bi) {
            V2 c = centroidOf(A.design.bodies[bi]);
            return MatrixMultiply(MatrixInvert(MatrixTranslate((float)c.x * U, -(float)c.y * U, bodyZ(bi))), r.M);
        }
        return MatrixIdentity();
    };
    for (auto& it : A.fitItems)
        drawAttachment(it.file, it.att, MatrixMultiply(fitMatrix(it), delta(fitCarrier(it))), it.att.attach.job == "optic" ? gOpticAlpha : 1.f, Color{0, 0, 0, 0});
    if (A.fitting && !A.fitPick.empty() && A.fitHover.on) {   // where the one being placed would go: green if it can, red if not
        auto ss = fit::sites(A.design, A.fitItems);
        if (A.fitHover.site >= 0 && A.fitHover.site < (int)ss.size()) {
            fit::Item g; g.file = A.fitPick; g.att = A.fitPickD; g.site = ss[A.fitHover.site].key(); g.along = A.fitHover.along;
            g.xf = fit::place(g.att, ss[A.fitHover.site], g.along);
            bool ok = A.fitHover.why.empty();
            drawAttachment(g.file, g.att, MatrixMultiply(fitMatrix(g), delta(fitCarrier(g))), 0.55f, ok ? Color{110, 230, 140, 255} : Color{255, 100, 80, 255});
        }
    }
}

static void drawSceneImpl(bool gunOnly);
void drawScene() { drawSceneImpl(false); }
void drawGunOnly() { drawSceneImpl(true); }   // (the firing range: just the gun, every part solid)
static void drawSceneImpl(bool gunOnly) {
    auto& D = A.design;
    ensureMeshes();
    float py = plateY();
    SceneLook look = sceneLook();
    if (!gunOnly) setFog(A.showcase ? Color{16, 18, 22, 255} : look.fog);
    bool building = A.mode == Mode::Build && !A.showcase;
    if (!A.showcase && !A.posing && !gunOnly) {
        // the bench always has a floor and a stand; the designer shows them only in the classic studio look
        if (!building || look.studs) {
            if (look.studs) DrawMesh(G.plate, G.plateMat, MatrixTranslate(A.target.x, py, 0));
            else drawFloor(py);
            drawStands(py);
        }
    }
    if (building && CFG.workbench && !A.showcase && !gunOnly) drawWorkbench();
    // drafting board behind the part plane
    if (building && !gunOnly) {
        float back = 0;
        for (size_t i = 0; i < D.bodies.size(); i++) back = std::min(back, bodyZ((int)i) - bodyDepth(D.bodies[i]) / 2);
        drawBoard(back - 0.6f);
    }
    auto list = renderList();
    auto isGhost = [&](const RB& r) {
        if (D.bodies[r.tmpl].opacity < 0.99) return true;   // clear: always drawn see-through
        if (A.showcase || gunOnly) return false;  // on the menu it looks like a finished gun
        const BodyDef& d = D.bodies[r.tmpl];
        if (building && A.sel.kind == 1 && A.sel.idx == r.tmpl && !d.magazine) return false;   // a selected magazine stays see-through: its rounds show
        if (A.cutaway && d.z - d.thick / 2 > 0.5) return true;  // wholly on the near side: cut away to show the inside
        return A.xray && d.ghost;
    };
    // opaque parts
    for (auto& r : list) {
        if (isGhost(r)) continue;
        Color c = hexColor(D.bodies[r.tmpl].color);
        if (building && A.sel.kind == 1 && A.sel.idx == r.tmpl) c = shade(c, 1.12f);
        else if (building && A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size() && !D.bodies[r.tmpl].weld.empty() &&
                 D.bodies[r.tmpl].weld == D.bodies[A.sel.idx].weld) c = shade(c, 1.07f);  // the rest of its welded piece
        if (!building && r.sb && r.sb == A.hover) c = shade(c, 1.18f);
        if (!D.bodies[r.tmpl].texture.empty()) drawMeshTex(A.meshes[r.tmpl].mesh, r.M, c, userTexture(D.bodies[r.tmpl].texture));
        else drawMesh(A.meshes[r.tmpl].mesh, r.M, c);
    }
    if (A.mode == Mode::Test) drawAttachments(list);   // attachments on the gun (never in the designer: they aren't part of it)
    for (auto& r : list) if (!isGhost(r)) drawMarks(r.tmpl, r.M);   // engravings, on the parts they're cut into
    for (auto& r : list) drawRifling(r.tmpl, r.M);
    drawEngravePreview();
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
            // a pin is drawn at its own size; a weld just makes the parts one, so there is nothing to draw
            if (j.type == "pin") drawCyl(W3(p, -z), W3(p, z), (float)(j.pinDia / 2) * U, steel);
        } else if (j.type == "track") {
            // a track is a path, not a part: on the bench it isn't drawn at all, and in the designer it's faint unless
            // it belongs to what's selected (a slot in a moving part can reach well outside it)
            if (!building) continue;
            bool mine = selJ || (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)D.bodies.size() &&
                                 (D.bodies[A.sel.idx].id == j.a || D.bodies[A.sel.idx].id == j.b));
            float zb = frontZ(j.b) + 0.02f, w = mine ? 0.26f : 0.12f, al = mine ? 1.f : 0.3f;
            for (size_t k = 0; k + 1 < j.path.size(); k++) {
                Vector3 a = W3(curMM(j.b, j.path[k]), zb), b = W3(curMM(j.b, j.path[k + 1]), zb);
                Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f), dd = Vector3Subtract(b, a);
                float len = Vector3Length(dd) + w, ang = std::atan2(dd.y, dd.x);
                drawBox(mid, {len, w, 0.04f}, selJ ? Color{120, 90, 30, 255} : Color{28, 32, 40, 255}, MatrixRotateZ(ang), al);
            }
            V2 f = curMM(j.a, j.p);
            float z = std::max(frontZ(j.a), frontZ(j.b)) + 0.2f;
            if (mine) drawCyl(W3(f, -z), W3(f, z), 0.14f, Color{240, 170, 60, 255});
        } else if (j.type == "spring" && j.kind == "leaf") {
            V2 root = curMM(j.a, j.pa), tipDesign = curMM(j.a, j.pb), contact = curMM(j.b, j.pb);
            // the leaf bends from its clamped root to where it touches the other part
            bool ba = A.design.bodyIndex(j.a) >= 0, bb = A.design.bodyIndex(j.b) >= 0;   // between the two parts' depths
            float z = (ba && bb ? (partZ(j.a) + partZ(j.b)) / 2 : ba ? partZ(j.a) : partZ(j.b)) + 0.3f;
            Vector3 a = W3(root, z), b = W3(contact, z), mid0 = W3({(root.x + tipDesign.x) / 2, (root.y + tipDesign.y) / 2}, z);
            Vector3 mid = Vector3Lerp(mid0, Vector3Scale(Vector3Add(a, b), 0.5f), 0.6f);
            Color lc = selJ ? Color{255, 200, 60, 255} : Color{176, 190, 208, 255};
            drawLeaf(ji * 3, a, mid, b, 0.06f, 0.5f, lc);
        } else if (j.type == "spring") {
            // at the depth of the part it pushes (the first one, or the second if the first is the ground)
            float z = A.design.bodyIndex(j.a) >= 0 ? partZ(j.a) : partZ(j.b);
            Vector3 a = W3(curMM(j.a, j.pa), z), b = W3(curMM(j.b, j.pb), z);
            bool buckled = false; double bowM = 0;
            if (A.S) for (auto& J : A.S->joints) if (J.defIndex == (int)ji) { buckled = J.buckled; bowM = J.bow; }
            Color sc = selJ ? Color{255, 200, 60, 255} : buckled ? Color{235, 120, 90, 255} : Color{176, 190, 208, 255};
            float cr = (float)j.coilDia / 2 * U, wr = std::max(0.02f, (float)j.wire / 2 * U);
            Vector3 bow{0, 0, 0};
            if (buckled && bowM > 0) {   // bowed out sideways in a smooth arc, in the side view so you can see it
                Vector3 ax = Vector3Normalize(Vector3Subtract(b, a)), side = Vector3CrossProduct(ax, {0, 0, 1});
                if (Vector3Length(side) > 1e-4f) {
                    side = Vector3Normalize(side);
                    if (side.y > 0) side = Vector3Negate(side);
                    bow = Vector3Scale(side, (float)(bowM * 1000) * U);
                }
            }
            drawCoil(ji * 3, a, b, cr, wr, j.turns, sc, bow);
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
            const V2* mz = br.hasMuzzle ? &br.muzzle : nullptr;
            if (A.S->chambered) drawCartridge(br.chamber, br.face, 0, false, nullptr, mz);
            if (A.S->inTransit) drawCartridge(br.breech, br.face, 0, false, nullptr, mz);
            if (A.S->spent && A.S->physicalFeed) drawCartridge(br.chamber, br.face, 0, true, nullptr, mz);  // empty case still in the chamber
        } else if (br.loaded) drawCartridge(br.chamber, br.face, 0, false, nullptr, br.hasMuzzle ? &br.muzzle : nullptr);
        for (size_t k = 0; k < br.more.size(); k++) {  // the further chambers (the simulation keeps chamber 0 swapped in between steps)
            auto& c = br.more[k];
            if (A.S && k + 1 < A.S->chambers.size()) {
                auto& st = A.S->chambers[k + 1];
                const Cartridge* cc = c.ownCart ? &c.cart : nullptr;
                const V2* mz = c.hasMuzzle ? &c.muzzle : nullptr;
                if (st.chambered) drawCartridge(c.chamber, c.face, 0, false, cc, mz);
                if (st.spent && A.S->physicalFeed) drawCartridge(c.chamber, c.face, 0, true, cc, mz);
            } else if (!A.S && c.loaded) drawCartridge(c.chamber, c.face, 0, false, c.ownCart ? &c.cart : nullptr, c.hasMuzzle ? &c.muzzle : nullptr);
        }
    }
    for (auto& s : A.shells) drawCartridgeAt(Vector3Subtract(s.p, Vector3Scale(s.dir, s.len / 2)), s.dir, true);
    // loose rounds: on the bench, in the hand, or sitting in the open action
    if (A.S) for (auto& b : A.S->bodies) {
        if (!b.isRound || b.gone) continue;
        if (A.posing && b.rs == sim::RS::Loose) continue;  // (no bench to lie on while posing)
        V2 h = sim::wpt(b, {b.headLx, 0}), t = sim::wpt(b, {b.headLx + 0.001, 0});
        float z = (float)((b.z + b.zVis) * 1000) * U;  // cases fly out sideways through the port; double-position feed comes in from the side
        Vector3 o = W3({h.x * 1000, h.y * 1000}, z), o2 = W3({t.x * 1000, t.y * 1000}, z);
        const Cartridge* rc = nullptr;  // a round from a further chamber is that chamber's cartridge
        if (b.slot > 0 && b.slot < (int)A.S->chambers.size()) rc = &A.S->chambers[b.slot].cart;
        drawCartridgeAt(o, Vector3Normalize(Vector3Subtract(o2, o)), b.isCase, &b == A.hover ? 1.3f : 1.f, rc);
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
        {
            Color gc = (!building && r.sb && r.sb == A.hover) ? shade(c, 1.4f) : c;
            float ga = r.sb && r.sb == A.hover ? 0.42f : 0.26f;
            double op = D.bodies[r.tmpl].opacity;
            bool xr = !(A.showcase || gunOnly) && ((A.xray && D.bodies[r.tmpl].ghost) || (A.cutaway && D.bodies[r.tmpl].z - D.bodies[r.tmpl].thick / 2 > 0.5));
            if (op < 0.99) ga = xr ? std::min(ga, (float)op) : (float)op;   // (a clear part: its own opacity; x-rayed, whichever is clearer)
            if (!D.bodies[r.tmpl].texture.empty()) drawMeshTex(A.meshes[r.tmpl].mesh, r.M, gc, userTexture(D.bodies[r.tmpl].texture), ga);
            else drawMesh(A.meshes[r.tmpl].mesh, r.M, gc, ga);
        }
    }
    rlEnableDepthMask();
    for (auto& r : list) {
        bool selB = building && A.sel.kind == 1 && A.sel.idx == r.tmpl;
        bool hov = !building && r.sb && r.sb == A.hover;
        drawOutline(r.tmpl, r.M, (selB || hov) ? Color{255, 205, 60, 255} : isGhost(r) ? look.ink : Color{18, 22, 30, 170});
    }
    // muzzle flash (unlit)
    // (at the muzzle of the chamber that fired last)
    std::string fc = br.chamber; V2 fm = br.muzzle; bool fh = br.hasMuzzle;
    if (A.S && br.on) {
        double last = A.S->chambers.empty() ? -1e9 : A.S->chambers[0].shotT;
        for (size_t k = 0; k < br.more.size() && k + 1 < A.S->chambers.size(); k++)
            if (A.S->chambers[k + 1].shotT > last) { last = A.S->chambers[k + 1].shotT; fc = br.more[k].chamber; fm = br.more[k].muzzle; fh = br.more[k].hasMuzzle; }
    }
    if (A.flash > 0 && br.on && fh) {
        Vector3 m = W3(curMM(fc, fm), 0);
        Vector3 ax = Vector3Normalize(Vector3Subtract(W3(curMM(fc, {fm.x + 1, fm.y}), 0), m));
        float k = A.flash / 0.12f;
        DrawSphere(Vector3Add(m, Vector3Scale(ax, 1.4f)), 1.4f * k + 0.4f, Color{255, 236, 170, 230});
        DrawSphere(Vector3Add(m, Vector3Scale(ax, 3.0f)), 1.0f * k + 0.3f, Color{255, 170, 60, 200});
        DrawSphere(Vector3Add(m, Vector3Scale(ax, 4.6f)), 0.6f * k + 0.2f, Color{255, 120, 40, 160});
    }
}
