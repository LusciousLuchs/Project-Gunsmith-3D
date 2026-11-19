// Gunsmith3D - Maps (see map.hpp).
#include <cstdlib>
#include "map.hpp"
#include "defaults.hpp"
#include "netradio.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <mutex>

namespace gmap {
using json = nlohmann::json;

static constexpr double EPS = 1e-6;      // geometric tolerance (metres)
static constexpr double BIG = 16384;     // half-size of the square every face polygon starts from
static constexpr double PI = 3.14159265358979323846;

// ---------------------------------------------------------------- materials
// The built-in materials, then the player's own pictures ("user:<file>", from user/textures) as they turn up.
static std::vector<std::string>& registry() {
    static std::vector<std::string> n = {
        "dev_grid", "concrete", "concrete_dark", "plywood", "brick", "plaster", "metal_plate", "corrugated",
        "dirt", "grass", "sand", "gravel", "wood_planks", "asphalt", "tile", "steel", "paint_white", "paint_orange",
        // (0.8.4: added after the first 18, so older maps' materials keep their places)
        "institutional", "formwork", "cinderblock", "old_brick", "metal_panels", "rust", "wallpaper", "parquet", "carpet",
        "checker_tile", "stone_wall", "shingles", "marble", "door_wood", "door_metal",
        // (0.8.47) see-through: windows (bullets and sight go through it)
        "glass",
        // (0.8.49) invisible walls: solid to walk into, not drawn when playing, bullets and sight go through
        "invisible"};
    return n;
}
const std::vector<std::string>& materialNames() { return registry(); }
int builtInMaterials() { return 35; }
int glassMaterial() { return 33; }
int invisibleMaterial() { return 34; }
bool isGlass(const Brush& b) { for (auto& f : b.faces) if (f.mat == 33 || f.mat == 34) return true; return false; }
bool isInvisible(const Brush& b) { for (auto& f : b.faces) if (f.mat == 34) return true; return false; }
bool isUserMaterial(int m) { return m >= builtInMaterials() && m < (int)registry().size(); }
int materialIndex(const std::string& name) {
    auto& n = registry();
    for (size_t i = 0; i < n.size(); i++) if (n[i] == name) return (int)i;
    if (name.rfind("user:", 0) == 0 && name.size() > 5) { n.push_back(name); return (int)n.size() - 1; }  // a picture of the player's
    return 0;
}
double materialTile(int m) {
    static const double t[] = {1.0, 2.0, 2.0, 1.22, 1.0, 2.0, 1.0, 1.5, 3.0, 3.0, 3.0, 2.0, 1.6, 3.0, 1.0, 1.0, 1.0, 1.0};
    return (m >= 0 && m < (int)(sizeof t / sizeof t[0])) ? t[m] : 1.0;
}

// ---------------------------------------------------------------- brush geometry
static double comp(V3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
static void setComp(V3& v, int a, double s) { (a == 0 ? v.x : a == 1 ? v.y : v.z) = s; }

std::vector<V3> facePolygon(const Brush& b, int fi) {
    const Face& f = b.faces[fi];
    V3 up = std::fabs(f.n.y) < 0.9 ? V3{0, 1, 0} : V3{1, 0, 0};
    V3 u = norm(cross(f.n, up)), v = cross(f.n, u);  // u x v = n, so the square winds counter-clockwise about n
    V3 c = f.n * f.d;
    std::vector<V3> poly = {c + (-u - v) * BIG, c + (u - v) * BIG, c + (u + v) * BIG, c + (-u + v) * BIG};
    for (size_t j = 0; j < b.faces.size() && poly.size() >= 3; j++) {
        if ((int)j == fi) continue;
        const Face& g = b.faces[j];
        std::vector<V3> out;
        for (size_t k = 0; k < poly.size(); k++) {
            V3 p = poly[k], q = poly[(k + 1) % poly.size()];
            double dp = dot(g.n, p) - g.d, dq = dot(g.n, q) - g.d;
            bool pin = dp <= EPS, qin = dq <= EPS;
            if (pin) out.push_back(p);
            if (pin != qin) { double t = dp / (dp - dq); out.push_back(p + (q - p) * t); }
        }
        poly.swap(out);
    }
    // merge points that ended up on top of each other
    std::vector<V3> clean;
    for (auto& p : poly) if (clean.empty() || len(p - clean.back()) > 1e-7) clean.push_back(p);
    while (clean.size() > 1 && len(clean.front() - clean.back()) <= 1e-7) clean.pop_back();
    if (clean.size() < 3) return {};
    // a sliver with no area counts as gone
    double area = 0;
    for (size_t k = 1; k + 1 < clean.size(); k++) area += dot(cross(clean[k] - clean[0], clean[k + 1] - clean[0]), f.n) * 0.5;
    if (area < 1e-9) return {};
    return clean;
}

static double volume(const Brush& b) {
    double vol = 0;
    for (size_t i = 0; i < b.faces.size(); i++) {
        auto p = facePolygon(b, (int)i);
        double area = 0;
        for (size_t k = 1; k + 1 < p.size(); k++) area += dot(cross(p[k] - p[0], p[k + 1] - p[0]), b.faces[i].n) * 0.5;
        vol += area * b.faces[i].d / 3;
    }
    return vol;
}

bool brushValid(const Brush& b) {
    if (b.faces.size() < 4) return false;
    int real = 0;
    for (size_t i = 0; i < b.faces.size(); i++) if (!facePolygon(b, (int)i).empty()) real++;
    return real >= 4 && volume(b) > 1e-9;
}

void brushClean(Brush& b) {
    std::vector<Face> keep;
    for (size_t i = 0; i < b.faces.size(); i++) {
        if (facePolygon(b, (int)i).empty()) continue;
        bool dup = false;
        for (auto& k : keep) if (dot(k.n, b.faces[i].n) > 1 - 1e-9 && std::fabs(k.d - b.faces[i].d) < 1e-7) dup = true;
        if (!dup) keep.push_back(b.faces[i]);
    }
    b.faces.swap(keep);
}

void brushBounds(const Brush& b, V3& mn, V3& mx) {
    unsigned long long key = 1469598103934665603ull ^ b.faces.size();   // a fingerprint of the planes (whole words mixed)
    for (auto& f : b.faces)
        for (double v : {f.n.x, f.n.y, f.n.z, f.d}) {
            unsigned long long bits; std::memcpy(&bits, &v, 8);
            key = (key ^ bits) * 0x9E3779B97F4A7C15ull;
            key ^= key >> 29;
        }
    if (key == 0) key = 1;
    if (b.boundsKey == key) {
        mn = {b.boundsCache[0], b.boundsCache[1], b.boundsCache[2]}; mx = {b.boundsCache[3], b.boundsCache[4], b.boundsCache[5]};
        return;
    }
    mn = {1e30, 1e30, 1e30}; mx = {-1e30, -1e30, -1e30};
    for (size_t i = 0; i < b.faces.size(); i++)
        for (auto& p : facePolygon(b, (int)i)) {
            mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
            mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
        }
    double c[6] = {mn.x, mn.y, mn.z, mx.x, mx.y, mx.z};
    std::memcpy(b.boundsCache, c, sizeof c);
    b.boundsKey = key;
}
V3 brushCentre(const Brush& b) { V3 mn, mx; brushBounds(b, mn, mx); return (mn + mx) * 0.5; }

static Face face(V3 n, double d, int mat) { Face f; f.n = norm(n); f.d = d; f.mat = mat; return f; }

Brush makeBox(V3 mn, V3 mx, int mat) {
    Brush b;
    b.faces = {face({1, 0, 0}, mx.x, mat), face({-1, 0, 0}, -mn.x, mat), face({0, 1, 0}, mx.y, mat),
               face({0, -1, 0}, -mn.y, mat), face({0, 0, 1}, mx.z, mat), face({0, 0, -1}, -mn.z, mat)};
    return b;
}

Brush makeWedge(V3 mn, V3 mx, int dir, int mat) {
    Brush b = makeBox(mn, mx, mat);
    int a = (dir % 2 == 0) ? 0 : 2;
    double s = dir < 2 ? 1 : -1;
    double low = s > 0 ? comp(mn, a) : comp(mx, a), high = s > 0 ? comp(mx, a) : comp(mn, a);
    double H = mx.y - mn.y, L = std::fabs(high - low);
    V3 n; setComp(n, a, -H * s); n.y = L;
    n = norm(n);
    V3 p; setComp(p, a, low); p.y = mn.y;
    b.faces.push_back(face(n, dot(n, p), mat));
    brushClean(b);
    return b;
}

Brush makeCylinder(V3 mn, V3 mx, int sides, int mat) {
    sides = std::clamp(sides, 3, 64);
    Brush b;
    b.faces = {face({0, 1, 0}, mx.y, mat), face({0, -1, 0}, -mn.y, mat)};
    double cx = (mn.x + mx.x) / 2, cz = (mn.z + mx.z) / 2, rx = (mx.x - mn.x) / 2, rz = (mx.z - mn.z) / 2;
    auto pt = [&](int k) { double t = 2 * PI * k / sides + PI / sides; return V3{cx + rx * std::cos(t), 0, cz + rz * std::sin(t)}; };
    for (int k = 0; k < sides; k++) {
        V3 p = pt(k), q = pt(k + 1), e = q - p;
        V3 n = norm({e.z, 0, -e.x});
        if (dot(n, (p + q) * 0.5 - V3{cx, 0, cz}) < 0) n = -n;
        b.faces.push_back(face(n, dot(n, p), mat));
    }
    return b;
}

// every corner of a brush (each once)
std::vector<V3> brushVertices(const Brush& b) {
    std::vector<V3> out;
    for (size_t f = 0; f < b.faces.size(); f++)
        for (V3 p : facePolygon(b, (int)f)) {
            bool seen = false;
            for (auto& q : out) if (len(q - p) < 1e-5) { seen = true; break; }
            if (!seen) out.push_back(p);
        }
    return out;
}
// The smallest convex block around these points (every plane through three of them with all the rest behind it).
// Faces take the material, colour and texture settings of `like`'s face pointing the nearest way, else `mat`.
bool brushFromPoints(const std::vector<V3>& pts, int mat, Brush& out, const Brush* like) {
    if (pts.size() < 4 || pts.size() > 96) return false;
    std::vector<Face> fs;
    size_t n = pts.size();
    for (size_t a = 0; a < n; a++)
        for (size_t b = a + 1; b < n; b++)
            for (size_t c = b + 1; c < n; c++) {
                V3 nn = cross(pts[b] - pts[a], pts[c] - pts[a]);
                double L = len(nn);
                if (L < 1e-9) continue;
                nn = nn * (1 / L);
                double d = dot(nn, pts[a]);
                int above = 0, below = 0;
                for (auto& p : pts) { double e = dot(nn, p) - d; if (e > 1e-6) above++; else if (e < -1e-6) below++; }
                if (above && below) continue;
                if (above) { nn = -nn; d = -d; }
                if (!above && !below) continue;   // (all in one plane: no solid)
                bool dup = false;
                for (auto& f : fs) if (dot(f.n, nn) > 1 - 1e-7 && std::fabs(f.d - d) < 1e-6) { dup = true; break; }
                if (dup) continue;
                Face f = face(nn, d, mat);
                if (like && !like->faces.empty()) {
                    const Face* best = &like->faces[0]; double bd = -2;
                    for (auto& g : like->faces) { double k = dot(g.n, nn); if (k > bd) { bd = k; best = &g; } }
                    f.mat = best->mat; f.scale = best->scale; f.shiftU = best->shiftU; f.shiftV = best->shiftV; f.color = best->color;
                }
                fs.push_back(f);
            }
    Brush b; b.faces = fs;
    if (like) b.locked = like->locked;
    brushClean(b);
    if (!brushValid(b)) return false;
    out = b;
    return true;
}
Brush makePyramid(V3 mn, V3 mx, int mat) {
    Brush b;
    double cx = (mn.x + mx.x) / 2, cz = (mn.z + mx.z) / 2;
    brushFromPoints({{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, {cx, mx.y, cz}}, mat, b, nullptr);
    return b;
}
Brush makeCone(V3 mn, V3 mx, int sides, int mat) {
    sides = std::clamp(sides, 3, 32);
    std::vector<V3> p;
    double cx = (mn.x + mx.x) / 2, cz = (mn.z + mx.z) / 2, rx = (mx.x - mn.x) / 2, rz = (mx.z - mn.z) / 2;
    for (int k = 0; k < sides; k++) { double t = 2 * PI * k / sides + PI / sides; p.push_back({cx + rx * std::cos(t), mn.y, cz + rz * std::sin(t)}); }
    p.push_back({cx, mx.y, cz});
    Brush b; brushFromPoints(p, mat, b, nullptr);
    return b;
}
Brush makeSphere(V3 mn, V3 mx, int sides, int mat) {
    sides = std::clamp(sides, 6, 16);
    int rings = std::max(3, sides / 2);
    std::vector<V3> p;
    V3 c = (mn + mx) * 0.5, r = (mx - mn) * 0.5;
    p.push_back({c.x, mx.y, c.z}); p.push_back({c.x, mn.y, c.z});
    for (int i = 1; i < rings; i++) {
        double ph = PI * i / rings, y = std::cos(ph), s = std::sin(ph);
        for (int k = 0; k < sides; k++) { double t = 2 * PI * k / sides + (i % 2) * PI / sides; p.push_back({c.x + r.x * s * std::cos(t), c.y + r.y * y, c.z + r.z * s * std::sin(t)}); }
    }
    Brush b; brushFromPoints(p, mat, b, nullptr);
    return b;
}
std::vector<Brush> makeStairs(V3 mn, V3 mx, int steps, int dir, int mat) {
    steps = std::clamp(steps, 1, 64);
    std::vector<Brush> out;
    int a = (dir % 2 == 0) ? 0 : 2;
    bool up = dir < 2;
    double a0 = comp(mn, a), a1 = comp(mx, a), H = mx.y - mn.y;
    for (int k = 0; k < steps; k++) {
        double s0 = a0 + (a1 - a0) * k / steps, s1 = a0 + (a1 - a0) * (k + 1) / steps;
        int level = up ? k + 1 : steps - k;
        V3 lo = mn, hi = mx;
        setComp(lo, a, s0); setComp(hi, a, s1);
        hi.y = mn.y + H * level / steps;
        out.push_back(makeBox(lo, hi, mat));
    }
    return out;
}

void translate(Brush& b, V3 d) { for (auto& f : b.faces) f.d += dot(f.n, d); }

void rotateY(Brush& b, double r, V3 c) {
    double cs = std::cos(r), sn = std::sin(r);
    for (auto& f : b.faces) {
        V3 n{f.n.x * cs + f.n.z * sn, f.n.y, -f.n.x * sn + f.n.z * cs};
        f.d = f.d - dot(f.n, c) + dot(n, c);
        f.n = norm(n);
    }
}

void rotateAxis(Brush& b, V3 k, double r, V3 c) {   // the right-hand way about k (Rodrigues), round c
    k = norm(k);
    double cs = std::cos(r), sn = std::sin(r);
    for (auto& f : b.faces) {
        V3 n = f.n * cs + cross(k, f.n) * sn + k * (dot(k, f.n) * (1 - cs));
        n = norm(n);
        f.d = f.d - dot(f.n, c) + dot(n, c);
        f.n = n;
    }
}

void mirror(Brush& b, int axis, V3 c) {
    for (auto& f : b.faces) {
        V3 n = f.n;
        setComp(n, axis, -comp(n, axis));
        f.d = f.d - dot(f.n, c) + dot(n, c);
        f.n = n;
    }
}

bool pushFace(Brush& b, int fi, double amount) {
    if (fi < 0 || fi >= (int)b.faces.size()) return false;
    Brush t = b;
    t.faces[fi].d += amount;
    if (facePolygon(t, fi).empty() || !brushValid(t)) return false;
    // keep the face being dragged even if another one disappears
    b = t;
    std::vector<Face> keep;
    for (size_t i = 0; i < b.faces.size(); i++) if ((int)i == fi || !facePolygon(b, (int)i).empty()) keep.push_back(b.faces[i]);
    b.faces.swap(keep);
    return true;
}

bool clipBrush(Brush& b, V3 n, double d, int mat) {
    Brush t = b;
    t.faces.push_back(face(n, d, mat));
    brushClean(t);
    if (!brushValid(t)) return false;
    b = t;
    return true;
}

std::vector<Brush> hollow(const Brush& b, double wall) {
    std::vector<Brush> out;
    for (size_t i = 0; i < b.faces.size(); i++) {
        if (facePolygon(b, (int)i).empty()) continue;
        Brush w = b;
        w.faces.push_back(face(-b.faces[i].n, -(b.faces[i].d - wall), b.faces[i].mat));
        brushClean(w);
        if (brushValid(w)) out.push_back(w);
    }
    return out;
}

// ---------------------------------------------------------------- collision and picking
Hit rayBrush(const Brush& b, V3 o, V3 dir, double maxT) {
    Hit h;
    h.t = maxT;
    double tmin = 0, tmax = maxT;
    int enterFace = -1;
    for (size_t i = 0; i < b.faces.size(); i++) {
        const Face& f = b.faces[i];
        double denom = dot(f.n, dir), dist = f.d - dot(f.n, o);
        if (std::fabs(denom) < 1e-12) { if (dist < 0) return h; continue; }
        double t = dist / denom;
        if (denom < 0) { if (t > tmin) { tmin = t; enterFace = (int)i; } }
        else if (t < tmax) tmax = t;
        if (tmin > tmax) return h;
    }
    if (enterFace < 0) return h;  // started inside: not a hit for picking
    h.t = tmin; h.n = b.faces[enterFace].n; h.face = enterFace; h.brush = 0;
    return h;
}

Hit rayMap(const Map& m, V3 o, V3 dir, double maxT, bool throughGlass) {
    Hit best;
    best.t = maxT;
    for (size_t i = 0; i < m.brushes.size(); i++) {
        if (m.brushes[i].hidden || (throughGlass && isGlass(m.brushes[i]))) continue;
        Hit h = rayBrush(m.brushes[i], o, dir, best.t);
        if (h.brush >= 0 && h.t < best.t) { best = h; best.brush = (int)i; }
    }
    return best;
}

// The brush grown by the box: every face pushed out by the box's reach along its normal, plus the brush's own
// bounding planes so sharp edges don't let a box slip through (the "bevel" planes Quake's collision used).
static void expanded(const Brush& b, V3 half, std::vector<Face>& out) {
    out.clear();
    for (auto& f : b.faces) {
        Face e = f;
        e.d += std::fabs(f.n.x) * half.x + std::fabs(f.n.y) * half.y + std::fabs(f.n.z) * half.z;
        out.push_back(e);
    }
    V3 mn, mx;
    brushBounds(b, mn, mx);
    out.push_back(face({1, 0, 0}, mx.x + half.x, 0)); out.push_back(face({-1, 0, 0}, -mn.x + half.x, 0));
    out.push_back(face({0, 1, 0}, mx.y + half.y, 0)); out.push_back(face({0, -1, 0}, -mn.y + half.y, 0));
    out.push_back(face({0, 0, 1}, mx.z + half.z, 0)); out.push_back(face({0, 0, -1}, -mn.z + half.z, 0));
}

// ---- the blocks near a box, from above (Map::Broad): built when the blocks change, else reused
static std::mutex gBroadLock;
static void nearBlocks(const Map& m, V3 mn, V3 mx, std::vector<int>& out) {
    out.clear();
    std::lock_guard<std::mutex> lock(gBroadLock);   // (built once; a range and its NPCs ask from one thread, tools may from others)
    Map::Broad& B = m.broad;
    if (!B.ok || B.data != (const void*)m.brushes.data() || B.n != m.brushes.size() || B.geom != m.geom) {
        B = Map::Broad{};
        B.data = m.brushes.data(); B.n = m.brushes.size(); B.geom = m.geom; B.ok = true;
        double x0 = 1e30, z0 = 1e30, x1 = -1e30, z1 = -1e30;
        std::vector<std::pair<V3, V3>> bb(m.brushes.size());
        for (size_t i = 0; i < m.brushes.size(); i++) {
            brushBounds(m.brushes[i], bb[i].first, bb[i].second);
            x0 = std::min(x0, bb[i].first.x); z0 = std::min(z0, bb[i].first.z); x1 = std::max(x1, bb[i].second.x); z1 = std::max(z1, bb[i].second.z);
        }
        if (m.brushes.empty()) { x0 = z0 = 0; x1 = z1 = 1; }
        B.x0 = x0; B.z0 = z0;
        B.nx = std::clamp((int)std::ceil((x1 - x0) / B.cell), 1, 512); B.nz = std::clamp((int)std::ceil((z1 - z0) / B.cell), 1, 512);
        B.cell = std::max((x1 - x0) / B.nx, (z1 - z0) / B.nz) + 1e-6;
        B.cells.assign((size_t)B.nx * B.nz, {});
        for (size_t i = 0; i < m.brushes.size(); i++) {
            int ia = std::clamp((int)((bb[i].first.x - x0) / B.cell), 0, B.nx - 1), ib = std::clamp((int)((bb[i].second.x - x0) / B.cell), 0, B.nx - 1);
            int ja = std::clamp((int)((bb[i].first.z - z0) / B.cell), 0, B.nz - 1), jb = std::clamp((int)((bb[i].second.z - z0) / B.cell), 0, B.nz - 1);
            if ((long)(ib - ia + 1) * (jb - ja + 1) > 64) { B.big.push_back((int)i); continue; }
            for (int ii = ia; ii <= ib; ii++) for (int jj = ja; jj <= jb; jj++) B.cells[(size_t)ii * B.nz + jj].push_back((int)i);
        }
    }
    int ia = std::clamp((int)((mn.x - B.x0) / B.cell), 0, B.nx - 1), ib = std::clamp((int)((mx.x - B.x0) / B.cell), 0, B.nx - 1);
    int ja = std::clamp((int)((mn.z - B.z0) / B.cell), 0, B.nz - 1), jb = std::clamp((int)((mx.z - B.z0) / B.cell), 0, B.nz - 1);
    out = B.big;
    for (int ii = ia; ii <= ib; ii++) for (int jj = ja; jj <= jb; jj++) { const auto& c = B.cells[(size_t)ii * B.nz + jj]; out.insert(out.end(), c.begin(), c.end()); }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}
Hit sweepBox(const Map& m, V3 a, V3 b, V3 half) {
    static constexpr double SKIN = 0.001;  // stop a millimetre short of surfaces so the next move starts outside
    Hit best;
    std::vector<Face> planes;
    V3 smn{std::min(a.x, b.x) - half.x, std::min(a.y, b.y) - half.y, std::min(a.z, b.z) - half.z};
    V3 smx{std::max(a.x, b.x) + half.x, std::max(a.y, b.y) + half.y, std::max(a.z, b.z) + half.z};
    thread_local std::vector<int> near;
    nearBlocks(m, smn, smx, near);
    for (int bi : near) {
        const Brush& br = m.brushes[bi];
        V3 mn, mx;
        brushBounds(br, mn, mx);
        if (mx.x < smn.x || mn.x > smx.x || mx.y < smn.y || mn.y > smx.y || mx.z < smn.z || mn.z > smx.z) continue;
        expanded(br, half, planes);
        double enter = -1, leave = 1;
        V3 n;
        bool startsOut = false, endsOut = false;
        bool miss = false;
        for (auto& f : planes) {
            double d1 = dot(f.n, a) - f.d, d2 = dot(f.n, b) - f.d;
            if (d1 > 0) startsOut = true;
            if (d2 > 0) endsOut = true;
            if (d1 > 0 && d2 >= d1) { miss = true; break; }  // outside this plane and not getting closer
            if (d1 <= 0 && d2 <= 0) continue;
            if (d1 > d2) {  // entering
                double t = (d1 - SKIN) / (d1 - d2);
                if (t > enter) { enter = t; n = f.n; }
            } else {        // leaving
                double t = (d1 + SKIN) / (d1 - d2);
                if (t < leave) leave = t;
            }
        }
        if (miss) continue;
        if (!startsOut) {
            best.startSolid = true;
            if (!endsOut) { best.t = 0; best.brush = (int)bi; return best; }
            continue;
        }
        if (enter < leave && enter > -1 && std::max(0.0, enter) < best.t) {
            best.t = std::max(0.0, enter);
            best.n = n;
            best.brush = (int)bi;
        }
    }
    return best;
}

bool boxFree(const Map& m, V3 c, V3 half) {
    std::vector<Face> planes;
    thread_local std::vector<int> near;
    nearBlocks(m, c - half, c + half, near);
    for (int bi : near) {
        const Brush& br = m.brushes[bi];
        V3 mn, mx;
        brushBounds(br, mn, mx);
        if (mx.x < c.x - half.x || mn.x > c.x + half.x || mx.y < c.y - half.y || mn.y > c.y + half.y || mx.z < c.z - half.z || mn.z > c.z + half.z) continue;
        expanded(br, half, planes);
        bool inside = true;
        for (auto& f : planes) if (dot(f.n, c) - f.d > -1e-4) { inside = false; break; }
        if (inside) return false;
    }
    return true;
}

// ---------------------------------------------------------------- files
static json v3j(V3 v) { return json::array({v.x, v.y, v.z}); }
static V3 jv3(const json& j) { return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()}; }
static json c3j(const double* c) { return json::array({c[0], c[1], c[2]}); }
static void jc3(const json& j, double* c) { for (int i = 0; i < 3; i++) c[i] = j.at(i).get<double>(); }

LampAt lampOf(const Entity& e) {
    LampAt L;
    L.pos = e.pos;
    double y = e.yaw * 3.14159265358979 / 180, p = e.pitch * 3.14159265358979 / 180;
    L.dir = {std::cos(p) * std::cos(y), std::sin(p), std::cos(p) * std::sin(y)};
    for (int i = 0; i < 3; i++) L.color[i] = e.color[i];
    L.power = e.power; L.range = e.range;
    L.cosCone = e.cone > 0 ? std::cos(e.cone / 2 * 3.14159265358979 / 180) : -2;
    return L;
}
// one lamp on a point (the shader does the same): brightest close, gone at its reach; a spot fades at its cone's edge
static double lampFalloff(const LampAt& L, V3 p, V3 n) {
    V3 d = L.pos - p;
    double dist = len(d);
    if (dist > L.range) return 0;
    V3 l = dist > 1e-6 ? d * (1 / dist) : V3{0, 1, 0};
    double q = dist / L.range, win = std::max(0.0, 1 - q * q * q * q);   // (fades out only near its reach)
    double att = L.power * win * win / (1 + dist * dist);
    if (L.cosCone > -1.5) {
        double c = dot(l * -1, L.dir), inner = std::min(1.0, L.cosCone + 0.08);
        double t = std::clamp((c - L.cosCone) / std::max(1e-4, inner - L.cosCone), 0.0, 1.0);
        att *= t * t * (3 - 2 * t);
    }
    return att * std::max(0.0, dot(n, l));
}
void lightAt(const Map& m, V3 p, V3 n, double out[3]) {
    const Look& k = m.look;
    double day = std::clamp(k.daylight, 0.0, 1.0);
    double yw = k.sunYaw * 3.14159265358979 / 180, pt = k.sunPitch * 3.14159265358979 / 180;
    V3 sun{std::cos(pt) * std::cos(yw), std::sin(pt), std::cos(pt) * std::sin(yw)};
    double sd = std::max(0.0, dot(n, sun)) * day, hemi = 0.5 + 0.5 * n.y;
    double floorAmb = 0.035;   // (never pitch black: you can just make things out)
    for (int i = 0; i < 3; i++) {
        double amb = k.ambient[i] * (0.75 + 0.45 * hemi) * day;
        out[i] = 0.7 * std::max(amb, floorAmb) + 0.58 * k.sun[i] * 1.15 * sd;
    }
    double acc[3] = {0, 0, 0};
    for (auto& e : m.ents)
        if (e.type == "light") {
            LampAt L = lampOf(e);
            double a = lampFalloff(L, p, n);
            for (int i = 0; i < 3; i++) acc[i] += a * L.color[i];
        }
    for (int i = 0; i < 3; i++) out[i] += 0.9 * acc[i] / (1 + 0.45 * acc[i]) / 0.7;   // (as the shader: bright, not burnt out)
}

static json brushJ(const Brush& b) {
    json fs = json::array();
    for (auto& f : b.faces) {
        json jf = {{"n", v3j(f.n)}, {"d", f.d}, {"mat", materialNames()[std::clamp(f.mat, 0, (int)materialNames().size() - 1)]}};
        if (f.scale != 1) jf["scale"] = f.scale;
        if (f.shiftU != 0 || f.shiftV != 0) jf["shift"] = json::array({f.shiftU, f.shiftV});
        if (f.color != 0xFFFFFFFFu) { char hx[16]; std::snprintf(hx, sizeof hx, "%08X", f.color); jf["color"] = hx; }
        fs.push_back(jf);
    }
    json jb = {{"faces", fs}};
    if (b.locked) jb["locked"] = true;
    return jb;
}
static bool brushFromJ(const json& jb, Brush& b) {
    for (auto& jf : jb.at("faces")) {
        Face f;
        f.n = norm(jv3(jf.at("n")));
        f.d = jf.at("d").get<double>();
        f.mat = materialIndex(jf.value("mat", "dev_grid"));
        f.scale = jf.value("scale", 1.0);
        if (jf.contains("shift")) { f.shiftU = jf["shift"].at(0).get<double>(); f.shiftV = jf["shift"].at(1).get<double>(); }
        if (jf.contains("color") && jf["color"].is_string()) f.color = (unsigned)std::strtoul(jf["color"].get<std::string>().c_str(), nullptr, 16);
        b.faces.push_back(f);
    }
    b.locked = jb.value("locked", false);
    return brushValid(b);
}
static json entJ(const Entity& e) {
    json je = {{"type", e.type}, {"pos", v3j(e.pos)}, {"yaw", e.yaw}};
    if (!e.ref.empty()) je["ref"] = e.ref;
    if (e.count != 1) je["count"] = e.count;
    if (e.respawn > 0) je["respawn"] = e.respawn;
    if (e.type == "door") je["width"] = e.width;
    if (e.type == "target_paper" && e.ref == "moving") { je["travel"] = e.width; je["speed"] = e.range; }
    if (e.type == "light") je["light"] = {{"color", c3j(e.color)}, {"power", e.power}, {"range", e.range}, {"cone", e.cone}, {"pitch", e.pitch}};
    if (e.type == "radio") je["radio"] = {{"label", e.label}, {"volume", e.power}, {"reach", e.range}, {"on", e.playing}};
    if (!e.parts.empty()) { json ps = json::array(); for (auto& b : e.parts) ps.push_back(brushJ(b)); je["parts"] = ps; }
    if (!e.items.empty()) { json is = json::array(); for (auto& k : e.items) is.push_back(entJ(k)); je["items"] = is; }
    if (e.mass > 0) je["mass"] = e.mass;
    return je;
}
static Entity entFromJ(const json& je) {
    Entity e;
    e.type = je.value("type", "player_start");
    e.pos = jv3(je.at("pos"));
    e.yaw = je.value("yaw", 0.0);
    e.ref = je.value("ref", "");
    e.count = std::max(1, je.value("count", 1));
    e.respawn = std::max(0.0, je.value("respawn", 0.0));
    e.width = std::clamp(je.value("width", 0.9), 0.4, 4.0);
    if (je.contains("travel")) e.width = std::clamp(je.value("travel", 3.0), 0.2, 100.0);   // a moving target: how far it slides
    if (je.contains("speed")) e.range = std::clamp(je.value("speed", 1.2), 0.05, 30.0);     // ...and how fast (m/s)
    if (je.contains("light") && je["light"].is_object()) {
        auto& jl = je["light"];
        if (jl.contains("color")) jc3(jl["color"], e.color);
        e.power = std::clamp(jl.value("power", 3.0), 0.0, 1000.0);
        e.range = std::clamp(jl.value("range", 10.0), 0.5, 500.0);
        e.cone = std::clamp(jl.value("cone", 0.0), 0.0, 179.0);
        e.pitch = std::clamp(jl.value("pitch", -90.0), -90.0, 90.0);
    }
    if (je.contains("radio") && je["radio"].is_object()) {
        auto& jr = je["radio"];
        e.label = jr.value("label", "");
        e.power = std::clamp(jr.value("volume", 0.6), 0.0, 1.0);
        e.range = std::clamp(jr.value("reach", 20.0), 2.0, 200.0);
        e.playing = jr.value("on", true);
    }
    if (je.contains("parts") && je["parts"].is_array())
        for (auto& jb : je["parts"]) { Brush b; if (brushFromJ(jb, b)) e.parts.push_back(b); }
    if (je.contains("items") && je["items"].is_array())
        for (auto& k : je["items"]) e.items.push_back(entFromJ(k));
    e.mass = std::max(0.0, je.value("mass", 0.0));
    return e;
}
std::string entityToJson(const Entity& e) { json j = entJ(e); j["format"] = "gunsmith3d-furniture"; return j.dump(1); }
bool entityFromJson(const std::string& s, Entity& e) {
    try {
        json j = json::parse(s);
        if (j.value("format", "") != "gunsmith3d-furniture") return false;
        e = entFromJ(j);
        return !e.parts.empty();
    } catch (...) { return false; }
}

// ---------------------------------------------------------------- time of day
static double smooth01(double e0, double e1, double x) { double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3 - 2 * t); }
Look lookAtHour(const Look& b, double hour) {
    if (b.time < 0) return b;
    Look L = b;
    double h = std::fmod(std::fmod(hour, 24.0) + 24.0, 24.0);
    const double PI = 3.14159265358979;
    double el = 62 * std::sin(PI * (h - 6) / 12);                 // the sun's height, degrees (below 0 at night)
    double day = smooth01(-9, 10, el);                            // 0 night .. 1 day (twilight while the sun is under 9 degrees down)
    double dusk = std::clamp(1 - std::fabs(el - 3) / 20, 0.0, 1.0);   // the low, warm light either side of sunrise and sunset
    L.sunYaw = b.sunYaw + (h - 12) * 15;
    L.sunPitch = std::max(el, 6.0);                               // (at night the moon stands in for it, high-ish)
    if (el < 0) { L.sunYaw = b.sunYaw + 180 + (h - 24) * 15; L.sunPitch = 35; }
    auto mix = [](const double* a, const double* c, double t, double* out) { for (int i = 0; i < 3; i++) out[i] = a[i] + (c[i] - a[i]) * t; };
    const double warm[3] = {1.0, 0.58, 0.32}, moon[3] = {0.55, 0.65, 0.95};
    const double duskTop[3] = {0.32, 0.36, 0.58}, duskHor[3] = {0.98, 0.58, 0.36};
    const double nightTop[3] = {0.10, 0.13, 0.28}, nightHor[3] = {0.16, 0.20, 0.36}, nightAmb[3] = {0.30, 0.36, 0.55};
    double sun[3], top[3], hor[3], amb[3];
    mix(b.sun, warm, dusk, sun);
    mix(sun, moon, 1 - day, sun);
    mix(b.skyTop, duskTop, dusk * 0.8, top);   mix(top, nightTop, 1 - day, top);
    mix(b.skyHorizon, duskHor, dusk, hor);     mix(hor, nightHor, 1 - day, hor);
    mix(b.ambient, nightAmb, 1 - day, amb);
    for (int i = 0; i < 3; i++) { L.sun[i] = sun[i]; L.skyTop[i] = top[i]; L.skyHorizon[i] = hor[i]; L.ambient[i] = amb[i]; }
    L.daylight = std::clamp(b.daylight, 0.0, 1.0) * (0.07 + 0.93 * day);   // (a moonlit night: about a fourteenth of the day)
    if (el < 0) L.fogEnd = b.fogEnd * (0.55 + 0.45 * day);                  // (you see less far at night)
    return L;
}
std::string clockText(double hour) {
    double h = std::fmod(std::fmod(hour, 24.0) + 24.0, 24.0);
    int hh = (int)h, mm = (int)((h - hh) * 60);
    char buf[8]; std::snprintf(buf, sizeof buf, "%02d:%02d", hh, mm);
    return buf;
}

std::string mapToJson(const Map& m) {
    json j;
    j["format"] = "gunsmith3d-map";
    j["version"] = 1;
    j["name"] = m.name;
    j["look"] = {{"sunYaw", m.look.sunYaw}, {"sunPitch", m.look.sunPitch}, {"sun", c3j(m.look.sun)}, {"ambient", c3j(m.look.ambient)},
                 {"skyTop", c3j(m.look.skyTop)}, {"skyHorizon", c3j(m.look.skyHorizon)}, {"fogStart", m.look.fogStart}, {"fogEnd", m.look.fogEnd}};
    if (m.look.daylight != 1) j["look"]["daylight"] = m.look.daylight;
    if (m.look.time >= 0) { j["look"]["time"] = m.look.time; if (m.look.dayMinutes > 0) j["look"]["dayMinutes"] = m.look.dayMinutes; }
    json bs = json::array();
    for (auto& b : m.brushes) if (!b.furn) bs.push_back(brushJ(b));   // (furniture's blocks travel in the furniture)
    j["brushes"] = bs;
    if (m.build.on() || m.store.on()) {
        auto zj = [](const Zone& z) { return json::array({z.x0, z.z0, z.x1, z.z1}); };
        j["career"] = {{"build", zj(m.build)}, {"store", zj(m.store)}, {"roof", m.roofY}};
    }
    json es = json::array();
    for (auto& e : m.ents) es.push_back(entJ(e));
    j["entities"] = es;
    if (!m.npcs.empty()) {
        json np = json::object();
        for (auto& [name, text] : m.npcs) { json v = json::parse(text, nullptr, false); if (!v.is_discarded()) np[name] = v; }
        j["npcs"] = np;
    }
    if (!m.npcGuns.empty()) {
        json ng = json::object();
        for (auto& [name, text] : m.npcGuns) { json v = json::parse(text, nullptr, false); if (!v.is_discarded()) ng[name] = v; }
        j["npcGuns"] = ng;
    }
    return j.dump(1);
}

bool mapFromJson(const std::string& s, Map& out) {
    try {
        json j = json::parse(s);
        if (j.value("format", "") != "gunsmith3d-map") return false;
        Map m;
        m.name = j.value("name", "Untitled");
        if (j.contains("look")) {
            auto& l = j["look"];
            m.look.sunYaw = l.value("sunYaw", m.look.sunYaw); m.look.sunPitch = l.value("sunPitch", m.look.sunPitch);
            if (l.contains("sun")) jc3(l["sun"], m.look.sun);
            if (l.contains("ambient")) jc3(l["ambient"], m.look.ambient);
            if (l.contains("skyTop")) jc3(l["skyTop"], m.look.skyTop);
            if (l.contains("skyHorizon")) jc3(l["skyHorizon"], m.look.skyHorizon);
            m.look.fogStart = l.value("fogStart", m.look.fogStart); m.look.fogEnd = l.value("fogEnd", m.look.fogEnd);
            m.look.daylight = std::clamp(l.value("daylight", 1.0), 0.0, 1.0);
            m.look.time = l.contains("time") ? std::clamp(l.value("time", 12.0), 0.0, 24.0) : -1;
            m.look.dayMinutes = std::clamp(l.value("dayMinutes", 0.0), 0.0, 1440.0);
        }
        for (auto& jb : j.value("brushes", json::array())) {
            Brush b;
            if (brushFromJ(jb, b)) m.brushes.push_back(b);
        }
        if (j.contains("career") && j["career"].is_object()) {
            auto zr = [](const json& a, Zone& z) { if (a.is_array() && a.size() == 4) { z.x0 = a[0].get<double>(); z.z0 = a[1].get<double>(); z.x1 = a[2].get<double>(); z.z1 = a[3].get<double>(); } };
            if (j["career"].contains("build")) zr(j["career"]["build"], m.build);
            if (j["career"].contains("store")) zr(j["career"]["store"], m.store);
            m.roofY = j["career"].value("roof", 0.0);
        }
        for (auto& je : j.value("entities", json::array())) m.ents.push_back(entFromJ(je));
        if (j.contains("npcs") && j["npcs"].is_object())
            for (auto& [name, v] : j["npcs"].items()) m.npcs.push_back({name, v.dump()});
        if (j.contains("npcGuns") && j["npcGuns"].is_object())
            for (auto& [name, v] : j["npcGuns"].items()) m.npcGuns.push_back({name, v.dump()});
        bakeFurniture(m);
        out = m;
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------- the starter range
// Downrange is +X. The firing line is at x = 0 under a shed; lanes run out to a backstop berm at 100 m.
void addShopBasics(Map& m) {
    bool computer = false, door = false;
    for (auto& e : m.ents) { if (e.type == "shop_computer") computer = true; if (e.type == "shop_door") door = true; }
    auto lamp = [&](V3 at) { Entity e; e.type = "light"; e.pos = at; e.power = 6; e.range = 6; m.ents.push_back(e); };
    if (!computer) {   // against the back wall, with a light over it (and over the counter, where you serve customers)
        Entity c; c.type = "shop_computer"; c.pos = {-7.3, 0, 3.6}; c.yaw = 0; m.ents.push_back(c);
        lamp({-6.6, 2.7, 3.6});
        for (auto& e : m.ents) if (e.type == "shop_counter") { V3 at = e.pos; at.y = 2.7; lamp(at); break; }
    }
    if (!door) {   // just inside the front door (the default room's), else where you start
        Entity d; d.type = "shop_door"; d.pos = {7.4, 0, 0}; d.yaw = 180;
        bool defaultRoom = false;
        for (auto& e : m.ents) if (e.type == "shop_counter" && std::fabs(e.pos.x - 5.5) < 0.01 && std::fabs(e.pos.z - 3.2) < 0.01) defaultRoom = true;
        if (!defaultRoom) for (auto& e : m.ents) if (e.type == "player_start") { d.pos = e.pos; d.yaw = e.yaw; }
        m.ents.push_back(d);
    }
}
Map defaultWorkshop() {
    // a plain room to start a gun business in: a concrete floor, plywood walls with a door at the front, a roof, three
    // lamps, your first bench, and a counter by the door
    Map m;
    m.name = "My workshop";
    m.look.daylight = 0.05;
    int concrete = materialIndex("concrete"), ply = materialIndex("plywood"), corr = materialIndex("corrugated"), dark = materialIndex("concrete_dark");
    auto box = [&](V3 a, V3 b, int mat) { m.brushes.push_back(makeBox(a, b, mat)); };
    box({-8.2, -0.4, -6.2}, {8.2, 0, 6.2}, concrete);
    box({-12, -0.45, -10}, {12, -0.4, 10}, dark);                  // (the yard round it)
    box({-8.2, 0, -6.2}, {-8, 3, 6.2}, ply);                        // back wall
    box({-8, 0, -6.2}, {8, 3, -6}, ply);                            // side walls
    box({-8, 0, 6}, {8, 3, 6.2}, ply);
    box({8, 0, -6.2}, {8.2, 3, -1}, ply);                           // the front, with a door
    box({8, 0, 1}, {8.2, 3, 6.2}, ply);
    box({8, 2.2, -1}, {8.2, 3, 1}, ply);
    (void)corr;   // (no roof: so the level editor's camera sees in; the lamps hang from where it would be)
    auto lamp = [&](V3 at) { Entity e; e.type = "light"; e.pos = at; e.power = 8; e.range = 10; m.ents.push_back(e); };
    lamp({-4, 2.8, 0}); lamp({2, 2.8, -2.5}); lamp({2, 2.8, 2.5});
    Entity start; start.pos = {-2, 0, 0}; start.yaw = 0; m.ents.push_back(start);
    Entity bench; bench.type = "station_bench"; bench.pos = {-6.5, 0, -4.6}; bench.yaw = 90; m.ents.push_back(bench);
    Entity counter; counter.type = "shop_counter"; counter.pos = {5.5, 0, 3.2}; counter.yaw = 180; m.ents.push_back(counter);
    addShopBasics(m);
    return m;
}
namespace {
bool hasType(const Map& m, const char* t) { for (auto& e : m.ents) if (e.type == t) return true; return false; }
bool darkInside(const Map& m) { return m.look.daylight < 0.3 && !hasType(m, "light"); }
}  // namespace
std::vector<ShopNeed> shopMissing(const Map& m) {
    std::vector<ShopNeed> out;
    if (!hasType(m, "player_start")) out.push_back({"Where you start", "Player start tool"});
    if (!hasType(m, "station_bench")) out.push_back({"Your workbench (where you work on your guns)", "Workstation tool: Workbench"});
    if (!hasType(m, "shop_counter")) out.push_back({"The counter (where you serve customers)", "Workstation tool: Shop counter"});
    if (!hasType(m, "shop_computer")) out.push_back({"The computer (store, contracts, staff)", "Workstation tool: Office computer"});
    if (!hasType(m, "shop_door")) out.push_back({"The front door (where you end the day)", "Workstation tool: Front door"});
    if (darkInside(m)) out.push_back({"A light (it's dark inside)", "Light tool"});
    return out;
}
int shopRepair(Map& m) {
    int added = 0;
    // the middle of the room: where you start, else the middle of the walls, else the middle of everything
    V3 c{0, 0, 0};
    bool have = false;
    for (auto& e : m.ents) if (e.type == "player_start") { c = e.pos; have = true; break; }
    if (!have) {
        double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
        for (auto& b : m.brushes) {
            V3 mn, mx;
            brushBounds(b, mn, mx);
            if (mx.y < 1.5 || mn.y > 1.0) continue;
            x0 = std::min(x0, mn.x); x1 = std::max(x1, mx.x); z0 = std::min(z0, mn.z); z1 = std::max(z1, mx.z);
        }
        if (x1 > x0) c = {(x0 + x1) / 2, 0, (z0 + z1) / 2};
    }
    auto floorAt = [&](double x, double z) {   // the top of whatever's under (x, z) below head height
        Hit h = rayMap(m, {x, 2.0, z}, {0, -1, 0}, 20);
        return h.brush >= 0 ? 2.0 - h.t : 0.0;
    };
    auto put = [&](const char* type, double dx, double dz, double yaw) {
        if (hasType(m, type)) return;
        Entity e; e.type = type; e.pos = {c.x + dx, 0, c.z + dz}; e.pos.y = floorAt(e.pos.x, e.pos.z); e.yaw = yaw;
        m.ents.push_back(e);
        added++;
    };
    if (!have) { Entity e; e.type = "player_start"; e.pos = {c.x, floorAt(c.x, c.z), c.z}; m.ents.push_back(e); added++; }
    put("station_bench", -1.8, -1.6, 90);
    put("shop_counter", 1.8, 1.6, 180);
    put("shop_computer", -1.8, 1.6, 0);
    put("shop_door", 2.6, 0, 180);
    if (darkInside(m)) {
        Entity e; e.type = "light"; e.pos = {c.x, floorAt(c.x, c.z) + 2.6, c.z}; e.power = 8; e.range = 10;
        m.ents.push_back(e);
        added++;
    }
    return added;
}
bool isDisplay(const std::string& t) { return t == "display_case" || t == "display_rack" || t == "display_stand"; }
bool isDecor(const std::string& t) { return t.rfind("decor_", 0) == 0; }
double storeRating(const Map& m, std::vector<std::string>* tips) {
    double guns = 0, empty = 0, acc = 0, decor = 0, lights = 0;
    bool counter = false;
    std::map<std::string, int> kinds;
    for (auto& e : m.ents) {
        if (!m.store.has(e.pos, 0.3)) continue;
        if (e.type == "shop_counter") counter = true;
        else if (e.type == "display_stand") acc++;
        else if (isDisplay(e.type)) { if (e.ref.empty()) empty++; else guns++; }
        else if (isDecor(e.type)) { decor++; kinds[e.type]++; }
        else if (e.type == "light") lights++;
    }
    // each thing adds less than the one before (a shop full of plants isn't five stars), and variety counts
    auto soft = [](double n, double k) { return k * (1 - std::exp(-n / k)); };
    double score = soft(guns, 2.2) * 0.9 + soft(empty, 1.0) * 0.25 + soft(acc, 1.5) * 0.45 + soft(decor, 2.5) * 0.5
                 + std::min<double>((double)kinds.size(), 5) * 0.12 + soft(lights, 1.5) * 0.35 + (counter ? 0.4 : 0);
    double stars = std::clamp(score, 0.0, 5.0);
    if (tips) {
        tips->clear();
        if (!counter) tips->push_back("Put your counter in the storefront");
        if (guns < 2) tips->push_back(empty > 0 ? "Put guns on your displays (pick one in the display's panel)" : "Add gun displays: cases and wall racks");
        if (acc < 1) tips->push_back("An accessory stand");
        if (decor < 3) tips->push_back("Decorate: plants, posters, rugs, signs, seats");
        else if (kinds.size() < 3) tips->push_back("More kinds of decoration");
        if (lights < 2) tips->push_back("More light in the storefront");
    }
    return stars;
}

Map propertyShell(const std::string& kind) {
    {   // the owner's own map for it (set in the developer build), when there is one
        std::string t = defaults::mapText(kind == "shack" || kind == "retail" || kind == "warehouse" ? kind : std::string("garage"));
        Map d;
        if (!t.empty() && mapFromJson(t, d)) return d;
    }
    Map m;
    int concrete = materialIndex("concrete"), dark = materialIndex("concrete_dark"), ply = materialIndex("plywood"), plaster = materialIndex("plaster"),
        brick = materialIndex("brick"), corr = materialIndex("corrugated"), planks = materialIndex("wood_planks"), asphalt = materialIndex("asphalt"),
        grass = materialIndex("grass"), tile = materialIndex("tile"), metal = materialIndex("metal_plate"), white = materialIndex("paint_white");
    auto box = [&](V3 a, V3 b, int mat) { Brush br = makeBox(a, b, mat); br.locked = true; m.brushes.push_back(br); };
    auto ent = [&](const char* type, V3 at, double yaw) { Entity e; e.type = type; e.pos = at; e.yaw = yaw; m.ents.push_back(e); };
    auto lamp = [&](V3 at, double power, double range) { Entity e; e.type = "light"; e.pos = at; e.power = power; e.range = range; m.ents.push_back(e); };
    // a room from x0..x1 (front at x1, toward +x), z0..z1, walls t thick and h high; a gap in the front wall from gz0 to gz1
    // up to gh high
    struct Gap { double z0, z1, h; };
    auto roomG = [&](double x0, double x1, double z0, double z1, double h, double t, int wall, std::vector<Gap> gaps) {
        box({x0 - t, 0, z0 - t}, {x0, h, z1 + t}, wall);
        box({x0, 0, z0 - t}, {x1, h, z0}, wall);
        box({x0, 0, z1}, {x1, h, z1 + t}, wall);
        std::sort(gaps.begin(), gaps.end(), [](const Gap& a, const Gap& b) { return a.z0 < b.z0; });
        double at = z0 - t;
        for (auto& g : gaps) {   // the front wall in pieces between its openings, with a lintel over each
            if (g.z0 > at) box({x1, 0, at}, {x1 + t, h, g.z0}, wall);
            if (g.h < h) box({x1, g.h, g.z0}, {x1 + t, h, g.z1}, wall);
            at = g.z1;
        }
        if (z1 + t > at) box({x1, 0, at}, {x1 + t, h, z1 + t}, wall);
    };
    auto room = [&](double x0, double x1, double z0, double z1, double h, double t, int wall, double gz0, double gz1, double gh) {
        std::vector<Gap> g;
        if (gz1 > gz0) g.push_back({gz0, gz1, gh});
        roomG(x0, x1, z0, z1, h, t, wall, g);
    };
    m.look.daylight = 0.35;
    if (kind == "shack") {
        m.name = "Workshop shack";
        box({-30, -0.5, -20}, {30, -0.05, 20}, grass);
        box({-9, -0.05, -4}, {6, 0, 4}, dark);
        room(-8, 0, -3, 3, 2.7, 0.15, planks, -0.6, 0.6, 2.1);
        m.build = {-8, -3, 0, 3}; m.store = {-2.5, -3, 0, 3}; m.roofY = 2.7;
        ent("player_start", {-4, 0, 0}, 0); ent("station_bench", {-7.2, 0, -2.2}, 90); ent("shop_counter", {-1.4, 0, 1.6}, 180);
        ent("shop_computer", {-7.3, 0, 2.2}, 0); ent("shop_door", {-0.5, 0, 0}, 180);
        lamp({-5.5, 2.5, 0}, 7, 8); lamp({-1.5, 2.5, 0}, 6, 6);
    } else if (kind == "retail") {
        m.name = "Strip-mall unit";
        box({-40, -0.5, -30}, {30, -0.05, 30}, asphalt);
        box({-14, -0.05, -6}, {3, 0, 6}, tile);
        room(-12, 0, -5, 5, 3.4, 0.2, brick, -3.5, 3.5, 2.6);   // (a wide shop window, its glass left to you)
        box({-6.1, 0, -5}, {-5.9, 3.4, 2.8}, plaster);           // the wall between the store and the workshop, with a doorway
        box({-6.1, 2.2, 2.8}, {-5.9, 3.4, 5}, plaster);
        box({-26, 0, -6}, {-12.2, 4.0, 6}, brick); box({-12.2, 0, -18}, {0.2, 4.0, -5.2}, brick); box({-12.2, 0, 5.2}, {0.2, 4.0, 18}, brick);   // the neighbours
        box({0.3, 3.4, -18}, {2.2, 3.6, 18}, metal);           // the awning
        m.build = {-12, -5, 0, 5}; m.store = {-6, -5, 0, 5}; m.roofY = 3.4;
        ent("player_start", {-3, 0, 0}, 180); ent("station_bench", {-11.2, 0, -4.2}, 90); ent("shop_counter", {-4.2, 0, -3.2}, 90);
        ent("shop_computer", {-11.3, 0, 4.2}, 0); ent("shop_door", {-0.5, 0, 0}, 180);
        lamp({-9, 3.2, 0}, 8, 9); lamp({-3, 3.2, -2.5}, 7, 7); lamp({-3, 3.2, 2.5}, 7, 7);
    } else if (kind == "warehouse") {
        m.name = "Warehouse";
        box({-60, -0.5, -40}, {30, -0.05, 40}, asphalt);
        box({-31, -0.05, -11}, {0.5, 0, 11}, concrete);
        roomG(-30, 0, -10, 10, 6, 0.25, corr, {{-7, -1, 4.5}, {5.4, 6.6, 2.2}});   // a roll-up door, and the office's front door
        box({-8.1, 0, 2}, {-7.9, 3, 10}, plaster);             // the office store in the front corner, a doorway through
        box({-8, 0, 1.9}, {-2.5, 3, 2.1}, plaster);
        box({-1.3, 2.2, 1.9}, {0, 3, 2.1}, plaster);
        box({-8.1, 3, 1.9}, {0, 3.1, 10}, white);              // its ceiling
        m.build = {-30, -10, 0, 10}; m.store = {-8, 2, 0, 10}; m.roofY = 6.0;
        ent("player_start", {-4, 0, 5}, 180); ent("station_bench", {-28.5, 0, -8.5}, 90); ent("shop_counter", {-5, 0, 7.6}, 90);
        ent("shop_computer", {-7.3, 0, 9.2}, 0); ent("shop_door", {-0.6, 0, 6}, 180);
        lamp({-20, 5.6, -4}, 12, 14); lamp({-10, 5.6, -4}, 12, 14); lamp({-20, 5.6, 6}, 10, 12); lamp({-4, 2.8, 6}, 7, 7);
    } else {   // the home garage: a house behind, a driveway in front
        m.name = "Home garage";
        box({-30, -0.5, -25}, {25, -0.05, 25}, grass);
        box({-6, -0.05, -3}, {8, 0, 3}, concrete);              // the floor and the driveway
        room(-6, 0, -3, 3, 2.6, 0.15, plaster, -2.4, 2.4, 2.2);  // the big door, open
        box({-12, 0, 3.15}, {0.6, 5, 12}, brick);                // the house, beside the garage
        box({-12.4, 5, 2.9}, {1.0, 5.3, 12.4}, materialIndex("wood_planks"));     // its roof
        box({0.6, -0.05, 6}, {6, 0, 7.2}, concrete);             // the path to its door
        m.build = {-6, -3, 0, 3}; m.store = {-2, -3, 0, 3}; m.roofY = 2.6;
        ent("player_start", {-3, 0, 0}, 0); ent("station_bench", {-5.3, 0, -2.2}, 90); ent("shop_counter", {-1, 0, 1.8}, 180);
        ent("shop_computer", {-5.3, 0, 2.2}, 0); ent("shop_door", {-0.4, 0, -0.5}, 180);
        lamp({-3, 2.4, 0}, 7, 7);
        (void)ply;
    }
    return m;
}
std::map<std::string, int> workshopStations(const Map& m) {
    std::map<std::string, int> n;
    for (auto& e : m.ents) {
        if (e.type.rfind("station_", 0) == 0) n[e.type.substr(8)]++;
        else if (e.type == "shop_counter") n["counter"]++;
    }
    return n;
}

static Map builtinRangeInCode();
Map builtinRange() {   // the owner's own practice range (developer build), else the one made below
    std::string t = defaults::mapText("range");
    Map d;
    if (!t.empty() && mapFromJson(t, d)) return d;
    return builtinRangeInCode();
}
static Map builtinRangeInCode() {
    Map m;
    m.name = "Practice range";
    auto M = [](const char* n) { return materialIndex(n); };
    // a block, coloured over its texture (RGBA; white: as it is) and its texture scaled
    auto put = [&](Brush b, unsigned col = 0xFFFFFFFFu, double sc = 1) { for (auto& f : b.faces) { f.color = col; f.scale = sc; } m.brushes.push_back(b); };
    auto box = [&](V3 a, V3 b, const char* mat, unsigned col = 0xFFFFFFFFu, double sc = 1) { put(makeBox(a, b, M(mat)), col, sc); };
    auto cyl = [&](V3 a, V3 b, int sides, const char* mat, unsigned col = 0xFFFFFFFFu) { put(makeCylinder(a, b, sides, M(mat)), col); };
    auto ball = [&](V3 a, V3 b, int sides, const char* mat, unsigned col) { put(makeSphere(a, b, sides, M(mat)), col); };
    const double W = 12;   // half width of the range (the lanes are within it)
    const unsigned STEEL_GREEN = 0x3E5446FFu, CONC = 0xC4BFB5FFu, BLOCK = 0xD8D4CAFFu, BAND = 0x4F6272FFu, WOOD = 0xB08654FFu,
                   DARKWOOD = 0x5C4632FFu, MAT = 0x2B2D30FFu, YELLOW = 0xE2B12AFFu, RED = 0xB8342CFFu, GRASS = 0xC8D49CFFu,
                   DIRT = 0xB0926CFFu, TIE = 0x4E3B2BFFu, TAN = 0xCDAE80FFu, WHITE = 0xF2F0EAFFu;

    // ---- ground: the whole site, then the concrete pad under the shed, a gravel walk, the parking behind
    box({-42, -0.5, -46}, {126, -0.006, 46}, "grass", GRASS, 0.6);
    box({-8, -0.4, -W - 6}, {2, 0, W + 6}, "concrete", CONC, 0.7);
    box({2, -0.4, -W - 6}, {6, 0, W + 6}, "gravel", 0xD6D0C4FFu);
    box({6, -0.4, -W - 6}, {112, 0, W + 6}, "grass", GRASS, 0.6);
    box({-30, -0.4, -W - 6}, {-8, 0, W + 6}, "asphalt", 0x8D8C8AFFu);                    // parking behind the shed
    for (double z = -14; z <= 14.1; z += 3.5) box({-27, 0, z - 0.06}, {-21, 0.004, z + 0.06}, "paint_white", WHITE);   // its bays
    // walls nobody sees round the site (you can't walk off the world)
    for (int s = -1; s <= 1; s += 2) { box({-42, 0, s * 45.5 - 0.5}, {126, 4, s * 45.5 + 0.5}, "invisible"); }
    box({-42.5, 0, -46}, {-41.5, 4, 46}, "invisible"); box({125.5, 0, -46}, {126.5, 4, 46}, "invisible");

    // ---- the shed: block walls painted inside with a dark band, steel posts, a sloping roof on purlins
    box({-8, 0, -W - 2}, {-7.8, 3.3, W + 2}, "cinderblock", BLOCK, 0.6);
    box({-7.8, 0, -W - 2}, {1.46, 3.2, -W - 1.84}, "cinderblock", BLOCK, 0.6);
    box({-7.8, 0, W + 1.84}, {1.46, 3.2, W + 2}, "cinderblock", BLOCK, 0.6);
    box({-7.8, 0, -W - 1.84}, {-7.79, 1.05, W + 1.84}, "paint_white", BAND);                 // the band along the back wall
    for (int s = -1; s <= 1; s += 2) box({-7.79, 0, s * (W + 1.84) - (s > 0 ? 0.01 : 0)}, {1.46, 1.05, s * (W + 1.84) + (s < 0 ? 0.01 : 0)}, "paint_white", BAND);
    for (double z : {-11.2, -4.8, 4.8, 11.2}) {   // posts in the gaps between lanes, with base plates
        box({1.3, 0, z - 0.08}, {1.46, 2.75, z + 0.08}, "steel", STEEL_GREEN);
        box({1.25, 0, z - 0.14}, {1.51, 0.02, z + 0.14}, "steel", 0x55595EFFu);
    }
    auto roofY = [](double x) { return 2.85 + (2.2 - x) * 0.0625; };   // the roof's top
    {
        Brush roof = makeBox({-8.2, 2.6, -W - 2.3}, {2.2, 3.6, W + 2.3}, M("corrugated"));
        V3 n = norm({0.65, 10.4, 0});
        double top = dot(n, V3{2.2, 2.85, 0});
        clipBrush(roof, n, top, M("corrugated"));
        clipBrush(roof, -n, -(top - 0.15), M("corrugated"));
        put(roof, 0x9AA0A6FFu);
    }
    box({2.2, 2.62, -W - 2.4}, {2.32, 2.92, W + 2.4}, "steel", STEEL_GREEN);   // the fascia along the front
    for (double x : {-6.0, -3.0, 0.0}) { double y = roofY(x) - 0.15; box({x - 0.06, y - 0.18, -W - 1.84}, {x + 0.06, y, W + 1.84}, "steel", 0x5B6066FFu); }   // purlins
    box({1.32, 2.55, -W - 1.84}, {1.46, 2.78, W + 1.84}, "steel", STEEL_GREEN);   // the beam over the posts
    // the yellow firing line, and a lane mark on the floor behind each bench
    box({1.5, 0, -W - 0.6}, {1.62, 0.004, W + 0.6}, "paint_white", YELLOW);
    // benches: steel frames, a wooden top with a rubber mat, a shelf below; dividers between lanes faced with carpet
    for (int lane = -3; lane <= 3; lane++) {
        double z = lane * 3.2;
        box({0.2, 0.86, z - 0.8}, {1.0, 0.92, z + 0.8}, "wood_planks", WOOD);
        box({0.3, 0.92, z - 0.55}, {0.9, 0.935, z + 0.55}, "paint_white", MAT);
        for (double x : {0.28, 0.84}) for (double dz : {-0.72, 0.62}) box({x, 0, z + dz}, {x + 0.06, 0.86, z + dz + 0.1}, "steel", 0x3A3F44FFu);
        box({0.28, 0.25, z - 0.72}, {0.9, 0.28, z + 0.72}, "wood_planks", DARKWOOD);
        box({-0.1, 0.004, z - 0.06}, {0.2, 0.006, z + 0.06}, "paint_white", WHITE);
        if (lane < 3) {
            box({-1.4, 0, z + 1.55}, {0.1, 1.4, z + 1.65}, "carpet", 0x8A96A2FFu, 0.7);
            box({-1.42, 1.4, z + 1.53}, {0.12, 1.45, z + 1.67}, "wood_planks", WOOD);
            box({0.1, 0, z + 1.53}, {0.14, 1.45, z + 1.67}, "wood_planks", WOOD);
        }
        box({1.34, 2.22, z - 0.3}, {1.4, 2.55, z + 0.3}, "paint_white", WHITE);   // a lane board under the front beam, facing you
        box({1.33, 2.27, z - 0.22}, {1.34, 2.5, z + 0.22}, "paint_white", lane % 2 ? 0x2E5E8CFFu : RED);
    }
    // the back wall: acoustic panels, a rules board, a bench to sit on, a bin, an extinguisher
    for (double z = -10; z <= 10.1; z += 4) box({-7.79, 1.3, z - 1.2}, {-7.72, 2.6, z + 1.2}, "carpet", 0x9AA5B0FFu, 0.6);
    box({-7.78, 1.25, -1.0}, {-7.7, 2.45, 1.0}, "paint_white", WHITE);   // the rules board
    box({-7.7, 2.2, -0.95}, {-7.69, 2.4, 0.95}, "paint_white", RED);
    for (int r = 0; r < 6; r++) box({-7.7, 1.4 + r * 0.12, -0.85}, {-7.695, 1.43 + r * 0.12, 0.25 + (r % 3) * 0.2}, "paint_white", 0x3A3A3AFFu);
    box({-7.6, 0.42, 1.6}, {-7.2, 0.47, 5.6}, "wood_planks", WOOD);
    for (double z : {1.7, 5.4}) box({-7.55, 0, z}, {-7.25, 0.42, z + 0.12}, "steel", 0x3A3F44FFu);
    cyl({-7.5, 0, 6.2}, {-7.0, 0.85, 6.7}, 10, "steel", STEEL_GREEN);   // the bin
    cyl({-7.75, 0.9, -12.9}, {-7.6, 1.45, -12.75}, 10, "paint_white", RED);   // an extinguisher on the wall
    for (double x : {-3.2}) for (double z : {-8.0, 0.0, 8.0}) {   // light fittings under the roof (their lamps are below)
        double y = roofY(x) - 0.15;
        box({x - 0.6, y - 0.12, z - 0.12}, {x + 0.6, y - 0.05, z + 0.12}, "steel", 0xD9D6CFFFu);
        box({x - 0.55, y - 0.13, z - 0.08}, {x + 0.55, y - 0.12, z + 0.08}, "paint_white", 0xFFF8E6FFu);
    }
    // the range flag by the shed's corner: up when it's live
    cyl({1.9, 0, -W - 2.8}, {2.0, 6.5, -W - 2.7}, 8, "paint_white", WHITE);
    box({2.0, 5.7, -W - 2.77}, {3.1, 6.4, -W - 2.73}, "paint_white", RED);
    // a small office by the parking, with a door and a window
    box({-18, 0, -W - 9}, {-11, 2.8, -W - 4}, "metal_panels", 0xB8BFB4FFu, 0.8);
    box({-18.2, 2.8, -W - 9.2}, {-10.8, 2.95, -W - 3.8}, "corrugated", 0x7D838AFFu);
    box({-14.9, 0, -W - 4.02}, {-14.0, 2.1, -W - 3.97}, "door_metal", 0x6F7B85FFu);
    box({-13.4, 1.0, -W - 4.02}, {-11.8, 1.9, -W - 3.98}, "glass");
    box({-13.46, 0.95, -W - 4.04}, {-11.74, 1.0, -W - 3.96}, "paint_white", WHITE);
    // a couple of parked trucks
    auto truck = [&](double x, double z, unsigned col) {
        box({x - 2.6, 0.45, z - 0.95}, {x + 2.6, 1.15, z + 0.95}, "paint_white", col);
        box({x - 1.2, 1.15, z - 0.9}, {x + 0.6, 1.85, z + 0.9}, "paint_white", col);
        box({x - 1.1, 1.25, z - 0.92}, {x + 0.5, 1.75, z + 0.92}, "glass");
        for (double dx : {-1.7, 1.7}) for (double s : {-1.0, 1.0}) put(makeCylinder({x + dx - 0.38, 0, z + s * 0.9 - 0.14}, {x + dx + 0.38, 0.76, z + s * 0.9 + 0.14}, 12, M("steel")), 0x222222FFu);
    };
    truck(-24, -10.5, 0x7A2E26FFu); truck(-24, 7.0, 0x30485EFFu);

    // ---- downrange: berms with grass on top, a timber-faced backstop, overhead baffles, trees beyond
    m.brushes.push_back(makeWedge({2, 0, W + 2}, {108, 3.5, W + 7}, 1, M("dirt"))); for (auto& f : m.brushes.back().faces) f.color = DIRT;
    m.brushes.push_back(makeWedge({2, 0, -W - 7}, {108, 3.5, -W - 2}, 3, M("dirt"))); for (auto& f : m.brushes.back().faces) f.color = DIRT;
    m.brushes.push_back(makeWedge({100, 0, -W - 7}, {108, 7, W + 7}, 0, M("dirt"))); for (auto& f : m.brushes.back().faces) f.color = DIRT;
    box({108, 0, -W - 7}, {112, 7, W + 7}, "dirt", DIRT);
    box({2, 0, W + 7}, {112, 3.5, W + 10}, "grass", GRASS, 0.6);
    box({2, 0, -W - 10}, {112, 3.5, -W - 7}, "grass", GRASS, 0.6);
    box({112, 0, -W - 10}, {115, 7, W + 10}, "grass", GRASS, 0.6);
    for (int r = 0; r < 6; r++)   // railway ties stacked in front of the backstop, staggered
        for (double z = -W - 1.5 + (r % 2) * 1.2; z < W + 1.5; z += 2.4)
            box({99.2, r * 0.5, z}, {99.8, r * 0.5 + 0.46, std::min(z + 2.3, W + 1.5)}, "wood_planks", (r + (int)std::floor(z)) % 2 ? TIE : 0x5E4734FFu, 0.5);
    for (double x : {30.0, 60.0}) {   // overhead safety baffles (a stray shot up the range hits wood, not sky)
        box({x, 3.0, -W - 1.6}, {x + 0.3, 4.1, W + 1.6}, "wood_planks", WOOD, 0.6);
        for (int s = -1; s <= 1; s += 2) box({x + 0.05, 0, s * (W + 1.8) - 0.1}, {x + 0.25, 4.2, s * (W + 1.8) + 0.1}, "steel", STEEL_GREEN);
    }
    // distance boards: a white post with a coloured board facing the firing line, both sides
    const unsigned DIST[6] = {0xE8E3D6FFu, 0x3E8E4FFFu, 0x2E5E8CFFu, 0xC9A227FFu, 0xB8342CFFu, 0x222222FFu};
    int di = 0;
    for (double x : {7.0, 15.0, 25.0, 50.0, 75.0, 100.0}) {
        for (double s : {-1.0, 1.0}) {
            double zc = s * (W + 0.6);
            box({x - 0.06, 0, zc - 0.06}, {x + 0.06, 1.1, zc + 0.06}, "paint_white", WHITE);
            box({x - 0.08, 0.75, zc - 0.3}, {x - 0.06, 1.15, zc + 0.3}, "paint_white", DIST[di]);
        }
        di++;
    }
    // target stands: a tan backer on two timber posts set in concrete blocks
    for (int lane = -3; lane <= 3; lane++) {
        double z = lane * 3.2;
        double x = lane % 3 == 0 ? 25 : (std::abs(lane) == 1 ? 7 : 15);
        for (double s : {-1.0, 1.0}) {
            box({x + 0.02, 0, z + s * 0.38 - 0.035}, {x + 0.09, 1.92, z + s * 0.38 + 0.035}, "wood_planks", WOOD);
            box({x - 0.1, 0, z + s * 0.38 - 0.15}, {x + 0.2, 0.2, z + s * 0.38 + 0.15}, "cinderblock", BLOCK);
        }
        box({x - 0.01, 1.0, z - 0.45}, {x + 0.02, 1.9, z + 0.45}, "plywood", TAN);
        box({x - 0.012, 0.45, z - 0.4}, {x + 0.02, 0.6, z + 0.4}, "wood_planks", WOOD);   // a cross brace
        Entity t; t.type = "target_paper"; t.pos = {x - 0.01, 1.45, z}; t.yaw = 180;   // a paper target on the board, facing the firing line
        m.ents.push_back(t);
    }
    // two moving targets on rails, sliding across the range (closer and slower, further and faster), each with a
    // motor box at one end behind a low earth bank
    for (int k = 0; k < 2; k++) {
        double x = k ? 20 : 11, travel = k ? 8 : 5, sp = k ? 2.0 : 1.2;
        box({x - 0.05, 0, -travel / 2 - 0.4}, {x + 0.05, 0.12, travel / 2 + 0.4}, "steel", 0x55595EFFu);
        box({x - 0.25, 0, travel / 2 + 0.45}, {x + 0.25, 0.5, travel / 2 + 0.95}, "metal_panels", STEEL_GREEN);
        m.brushes.push_back(makeWedge({x - 0.9, 0, -travel / 2 - 0.6}, {x - 0.2, 0.35, travel / 2 + 1.1}, 0, M("dirt"))); for (auto& f : m.brushes.back().faces) f.color = DIRT;
        Entity t; t.type = "target_paper"; t.ref = "moving"; t.pos = {x, 1.45, 0}; t.yaw = 180; t.width = travel; t.range = sp;
        m.ents.push_back(t);
    }
    // steel plates on A-frame stands, swinging from chains under a crossbar (orange paint shows the hits)
    for (int k = -4; k <= 4; k++) {
        double z = k * 2.4, x = k % 2 == 0 ? 50 : 75;
        box({x, 0, z - 0.05}, {x + 0.1, 0.9, z + 0.05}, "steel", 0x4A4E53FFu);           // post
        box({x - 0.25, 0, z - 0.04}, {x + 0.35, 0.05, z + 0.04}, "steel", 0x4A4E53FFu);  // its foot
        box({x - 0.03, 0.9, z - 0.25}, {x + 0.0, 1.4, z + 0.25}, "paint_orange");          // plate
    }
    // trees beyond the berms and the backstop
    unsigned seed = 7;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
    auto tree = [&](double x, double z, double h) {
        cyl({x - 0.18, 0, z - 0.18}, {x + 0.18, h * 0.45, z + 0.18}, 7, "wood_planks", 0x4A3A2AFFu);
        double r = h * 0.32;
        unsigned g = 0x4E7A36FFu + ((unsigned)(rnd() * 24) << 16);
        ball({x - r, h * 0.35, z - r}, {x + r, h * 0.35 + 2 * r, z + r}, 8, "grass", g);
        ball({x - r * 0.7, h * 0.62, z - r * 0.7}, {x + r * 0.7, h * 0.62 + 1.4 * r, z + r * 0.7}, 8, "grass", g + 0x00080400u);
    };
    for (double x = 8; x < 112; x += 7) for (int s = -1; s <= 1; s += 2) tree(x + rnd() * 3, s * (W + 13 + rnd() * 8), 6 + rnd() * 3);
    for (double z = -W - 6; z <= W + 6; z += 6) tree(119 + rnd() * 3, z + rnd() * 2, 7 + rnd() * 3);
    for (double z : {-30.0, 30.0}) for (double x = -36; x < -2; x += 8) tree(x + rnd() * 2, z + rnd() * 4, 6 + rnd() * 2);
    // floodlight towers along the berms
    for (double x : {30.0, 65.0}) for (int s = -1; s <= 1; s += 2) {
        double zc = s * (W + 3.0);
        cyl({x - 0.12, 0, zc - 0.12}, {x + 0.12, 7.0, zc + 0.12}, 8, "steel", 0x7A7F85FFu);
        box({x - 0.35, 6.8, zc - 0.25}, {x + 0.35, 7.2, zc + 0.25}, "steel", 0x3A3F44FFu);
    }

    // ---- light: a fixed time of day (the range's own Day / Night choice sets it; no running clock)
    m.look.daylight = 1; m.look.time = 14; m.look.dayMinutes = 0;
    auto lamp = [&](V3 at, double power, double range, double cone, double yaw, double pitch, double r, double g, double b) {
        Entity e; e.type = "light"; e.pos = at; e.power = power; e.range = range; e.cone = cone; e.yaw = yaw; e.pitch = pitch;
        e.color[0] = r; e.color[1] = g; e.color[2] = b; m.ents.push_back(e);
    };
    for (double z : {-8.0, 0.0, 8.0}) lamp({-3.2, roofY(-3.2) - 0.35, z}, 3.5, 9, 0, 0, -90, 1.0, 0.85, 0.65);   // warm lamps under the roof
    for (int lane = -3; lane <= 3; lane++) {                                                         // a light over each paper target
        double z = lane * 3.2, x = lane % 3 == 0 ? 25 : (std::abs(lane) == 1 ? 7 : 15);
        lamp({x - 1.2, 2.8, z}, 6, 7, 90, 0, -50, 0.95, 0.97, 1.0);
    }
    for (double x : {30.0, 65.0}) for (int s = -1; s <= 1; s += 2)   // the towers, aimed down the range at the steel
        lamp({x, 6.7, s * (W + 3.0)}, 80, 50, 70, s * -55, -14, 0.9, 0.95, 1.0);
    lamp({1.3, 2.6, 0}, 30, 30, 50, 0, -8, 0.95, 0.97, 1.0);                                          // the firing line's floodlight
    Entity plates; plates.type = "armor_plate"; plates.pos = {-3.6, 0, 2.2}; plates.count = 3; plates.respawn = 20; plates.yaw = 20;   // to try the armour bar
    m.ents.push_back(plates);
    Entity crate; crate.type = "attachment_crate"; crate.pos = {-3.6, 0, -2.2}; crate.yaw = -20;   // to try attachments
    m.ents.push_back(crate);
    Entity ammo; ammo.type = "ammo_box"; ammo.pos = {-3.6, 0, 4.4}; ammo.yaw = 10;      // spare ammunition
    m.ents.push_back(ammo);
    Entity guns; guns.type = "weapon_crate"; guns.pos = {-3.8, 0, -4.6}; guns.yaw = -10;   // more guns
    m.ents.push_back(guns);
    {   // furniture against the back wall: a shelf of things to pick up (and knock over), a table with guns laid out on it
        auto piece = [](const std::vector<Furnishing>& list, const char* key) { for (auto& f : list) if (f.key == key) return f.e; return Entity(); };
        auto item = [&](const char* key, V3 at, double yaw) { Entity e = piece(builtinProps(), key); e.pos = at; e.yaw = yaw; return e; };
        Entity shelf = piece(builtinFurniture(), "shelf");
        shelf.pos = {-7.6, 0, -7.2}; shelf.yaw = 0;
        for (double z : {-0.32, -0.2, -0.08}) shelf.items.push_back(item("can", {0, 0.92, z}, 0));
        for (double z : {0.12, 0.26}) shelf.items.push_back(item("bottle", {0, 0.92, z}, 0));
        shelf.items.push_back(item("ammocan", {0, 0.5, -0.2}, 0));
        shelf.items.push_back(item("ammocan", {0, 0.5, 0.15}, 10));
        shelf.items.push_back(item("brick", {0, 1.34, -0.1}, 0));
        shelf.items.push_back(item("box", {0, 0.08, 0.1}, 0));
        shelf.items.push_back(item("bucket", {0, 1.34, 0.22}, 0));
        m.ents.push_back(shelf);
        Entity radio;   // on top of the shelf: a radio playing an internet station (F at it to tune it or switch it off)
        radio.type = "radio"; radio.pos = {-7.6, 1.78, -7.2}; radio.yaw = 0;
        radio.ref = netradio::builtinStations()[0].url; radio.label = netradio::builtinStations()[0].name;
        radio.power = 0.45; radio.range = 18; radio.playing = true;
        m.ents.push_back(radio);
        Entity table = piece(builtinFurniture(), "table");
        table.pos = {-6.9, 0, -9.6}; table.yaw = 0;
        Entity g; g.type = "gun_item"; g.ref = "preset:tilting"; g.pos = {0.15, 0.9, -0.5}; g.yaw = 90; table.items.push_back(g);
        g.ref = "preset:boltaction"; g.pos = {-0.12, 0.9, 0.15}; g.yaw = 90; table.items.push_back(g);
        m.ents.push_back(table);
        lamp({-6.4, 2.75, -8.4}, 3.5, 7, 0, 0, -90, 1.0, 0.85, 0.65);   // (a lamp over them)
        for (int k = 0; k < 3; k++) {   // crates stacked by the wall, and one on its own
            Entity c = item("crate", {-7.4, 0.45 * (k == 1), 8.0 + (k == 2 ? 0.7 : 0)}, k * 7.0);
            m.ents.push_back(c);
        }
    }
    Entity start;
    start.pos = {-3, 0, 0};
    start.yaw = 0;
    m.ents.push_back(start);
    bakeFurniture(m);
    return m;
}

}  // namespace gmap
