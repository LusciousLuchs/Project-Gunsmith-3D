// Gunsmith3D - Maps (see map.hpp).
#include "map.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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
        "dirt", "grass", "sand", "gravel", "wood_planks", "asphalt", "tile", "steel", "paint_white", "paint_orange"};
    return n;
}
const std::vector<std::string>& materialNames() { return registry(); }
int builtInMaterials() { return 18; }
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

Hit rayMap(const Map& m, V3 o, V3 dir, double maxT) {
    Hit best;
    best.t = maxT;
    for (size_t i = 0; i < m.brushes.size(); i++) {
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

Hit sweepBox(const Map& m, V3 a, V3 b, V3 half) {
    static constexpr double SKIN = 0.001;  // stop a millimetre short of surfaces so the next move starts outside
    Hit best;
    std::vector<Face> planes;
    V3 smn{std::min(a.x, b.x) - half.x, std::min(a.y, b.y) - half.y, std::min(a.z, b.z) - half.z};
    V3 smx{std::max(a.x, b.x) + half.x, std::max(a.y, b.y) + half.y, std::max(a.z, b.z) + half.z};
    for (size_t bi = 0; bi < m.brushes.size(); bi++) {
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
    for (auto& br : m.brushes) {
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

std::string mapToJson(const Map& m) {
    json j;
    j["format"] = "gunsmith3d-map";
    j["version"] = 1;
    j["name"] = m.name;
    j["look"] = {{"sunYaw", m.look.sunYaw}, {"sunPitch", m.look.sunPitch}, {"sun", c3j(m.look.sun)}, {"ambient", c3j(m.look.ambient)},
                 {"skyTop", c3j(m.look.skyTop)}, {"skyHorizon", c3j(m.look.skyHorizon)}, {"fogStart", m.look.fogStart}, {"fogEnd", m.look.fogEnd}};
    if (m.look.daylight != 1) j["look"]["daylight"] = m.look.daylight;
    json bs = json::array();
    for (auto& b : m.brushes) {
        json fs = json::array();
        for (auto& f : b.faces) {
            json jf = {{"n", v3j(f.n)}, {"d", f.d}, {"mat", materialNames()[std::clamp(f.mat, 0, (int)materialNames().size() - 1)]}};
            if (f.scale != 1) jf["scale"] = f.scale;
            if (f.shiftU != 0 || f.shiftV != 0) jf["shift"] = json::array({f.shiftU, f.shiftV});
            fs.push_back(jf);
        }
        json jb = {{"faces", fs}};
        if (b.locked) jb["locked"] = true;
        bs.push_back(jb);
    }
    j["brushes"] = bs;
    if (m.build.on() || m.store.on()) {
        auto zj = [](const Zone& z) { return json::array({z.x0, z.z0, z.x1, z.z1}); };
        j["career"] = {{"build", zj(m.build)}, {"store", zj(m.store)}, {"roof", m.roofY}};
    }
    json es = json::array();
    for (auto& e : m.ents) {
        json je = {{"type", e.type}, {"pos", v3j(e.pos)}, {"yaw", e.yaw}};
        if (!e.ref.empty()) je["ref"] = e.ref;
        if (e.count != 1) je["count"] = e.count;
        if (e.respawn > 0) je["respawn"] = e.respawn;
        if (e.type == "light") je["light"] = {{"color", c3j(e.color)}, {"power", e.power}, {"range", e.range}, {"cone", e.cone}, {"pitch", e.pitch}};
        es.push_back(je);
    }
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
        }
        for (auto& jb : j.value("brushes", json::array())) {
            Brush b;
            for (auto& jf : jb.at("faces")) {
                Face f;
                f.n = norm(jv3(jf.at("n")));
                f.d = jf.at("d").get<double>();
                f.mat = materialIndex(jf.value("mat", "dev_grid"));
                f.scale = jf.value("scale", 1.0);
                if (jf.contains("shift")) { f.shiftU = jf["shift"].at(0).get<double>(); f.shiftV = jf["shift"].at(1).get<double>(); }
                b.faces.push_back(f);
            }
            b.locked = jb.value("locked", false);
            if (brushValid(b)) m.brushes.push_back(b);
        }
        if (j.contains("career") && j["career"].is_object()) {
            auto zr = [](const json& a, Zone& z) { if (a.is_array() && a.size() == 4) { z.x0 = a[0].get<double>(); z.z0 = a[1].get<double>(); z.x1 = a[2].get<double>(); z.z1 = a[3].get<double>(); } };
            if (j["career"].contains("build")) zr(j["career"]["build"], m.build);
            if (j["career"].contains("store")) zr(j["career"]["store"], m.store);
            m.roofY = j["career"].value("roof", 0.0);
        }
        for (auto& je : j.value("entities", json::array())) {
            Entity e;
            e.type = je.value("type", "player_start");
            e.pos = jv3(je.at("pos"));
            e.yaw = je.value("yaw", 0.0);
            e.ref = je.value("ref", "");
            e.count = std::max(1, je.value("count", 1));
            e.respawn = std::max(0.0, je.value("respawn", 0.0));
            if (je.contains("light") && je["light"].is_object()) {
                auto& jl = je["light"];
                if (jl.contains("color")) jc3(jl["color"], e.color);
                e.power = std::clamp(jl.value("power", 3.0), 0.0, 1000.0);
                e.range = std::clamp(jl.value("range", 10.0), 0.5, 500.0);
                e.cone = std::clamp(jl.value("cone", 0.0), 0.0, 179.0);
                e.pitch = std::clamp(jl.value("pitch", -90.0), -90.0, 90.0);
            }
            m.ents.push_back(e);
        }
        if (j.contains("npcs") && j["npcs"].is_object())
            for (auto& [name, v] : j["npcs"].items()) m.npcs.push_back({name, v.dump()});
        if (j.contains("npcGuns") && j["npcGuns"].is_object())
            for (auto& [name, v] : j["npcGuns"].items()) m.npcGuns.push_back({name, v.dump()});
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

Map builtinRange() {
    Map m;
    m.name = "Practice range";
    int concrete = materialIndex("concrete"), grass = materialIndex("grass"), dirt = materialIndex("dirt"), ply = materialIndex("plywood"),
        steel = materialIndex("steel"), corr = materialIndex("corrugated"), planks = materialIndex("wood_planks"), gravel = materialIndex("gravel"),
        dark = materialIndex("concrete_dark"), orange = materialIndex("paint_orange");
    auto box = [&](V3 a, V3 b, int mat) { m.brushes.push_back(makeBox(a, b, mat)); };
    const double W = 12;   // half width of the range
    // ground: a concrete pad under the shed, gravel walkway, grass out to the berm
    box({-8, -0.4, -W - 6}, {2, 0, W + 6}, concrete);
    box({2, -0.4, -W - 6}, {6, 0, W + 6}, gravel);
    box({6, -0.4, -W - 6}, {112, 0, W + 6}, grass);
    // the shed: back wall, posts, a sloped corrugated roof
    box({-8, 0, -W - 2}, {-7.8, 3.2, W + 2}, ply);
    for (double z : {-11.2, -4.8, 4.8, 11.2}) box({1.3, 0, z - 0.08}, {1.46, 2.75, z + 0.08}, steel);  // posts in the gaps between lanes
    box({-7.8, 0, -W - 2}, {1.46, 3.2, -W - 1.84}, ply);   // side walls
    box({-7.8, 0, W + 1.84}, {1.46, 3.2, W + 2}, ply);
    {
        // a 15 cm slab sloping from 3.5 m at the back down to 2.85 m at the front
        Brush roof = makeBox({-8.2, 2.6, -W - 2.3}, {2.2, 3.6, W + 2.3}, corr);
        V3 n = norm({0.65, 10.4, 0});
        double top = dot(n, V3{2.2, 2.85, 0});
        clipBrush(roof, n, top, corr);
        clipBrush(roof, -n, -(top - 0.15), corr);
        m.brushes.push_back(roof);
    }
    // shooting benches along the firing line, one per lane
    for (int lane = -3; lane <= 3; lane++) {
        double z = lane * 3.2;
        box({0.2, 0.86, z - 0.8}, {1.0, 0.92, z + 0.8}, planks);     // top
        box({0.3, 0, z - 0.72}, {0.42, 0.86, z - 0.6}, planks);       // legs
        box({0.3, 0, z + 0.6}, {0.42, 0.86, z + 0.72}, planks);
        box({0.78, 0, z - 0.72}, {0.9, 0.86, z - 0.6}, planks);
        box({0.78, 0, z + 0.6}, {0.9, 0.86, z + 0.72}, planks);
        if (lane < 3) box({-1.4, 0, z + 1.55}, {0.1, 1.4, z + 1.65}, ply);  // divider between lanes, short of the benches
    }
    // side berms the length of the range, and the backstop
    m.brushes.push_back(makeWedge({2, 0, W + 2}, {108, 3.5, W + 7}, 1, dirt));
    m.brushes.push_back(makeWedge({2, 0, -W - 7}, {108, 3.5, -W - 2}, 3, dirt));
    m.brushes.push_back(makeWedge({100, 0, -W - 7}, {108, 7, W + 7}, 0, dirt));
    box({108, 0, -W - 7}, {112, 7, W + 7}, dirt);
    // distance markers: a short concrete post at each distance, on both sides
    for (double x : {7.0, 15.0, 25.0, 50.0, 75.0, 100.0})
        for (double s : {-1.0, 1.0}) box({x - 0.15, 0, s * (W + 0.6) - 0.15}, {x + 0.15, 0.6, s * (W + 0.6) + 0.15}, dark);
    // target stands: paper boards close in, steel plates further out
    for (int lane = -3; lane <= 3; lane++) {
        double z = lane * 3.2;
        double x = lane % 3 == 0 ? 25 : (std::abs(lane) == 1 ? 7 : 15);
        box({x, 0, z - 0.4}, {x + 0.08, 1.1, z - 0.33}, planks);
        box({x, 0, z + 0.33}, {x + 0.08, 1.1, z + 0.4}, planks);
        box({x - 0.01, 1.0, z - 0.45}, {x + 0.02, 1.9, z + 0.45}, ply);
        Entity t; t.type = "target_paper"; t.pos = {x - 0.01, 1.45, z}; t.yaw = 180;   // a paper target on the board, facing the firing line
        m.ents.push_back(t);
    }
    for (int k = -4; k <= 4; k++) {
        double z = k * 2.4, x = k % 2 == 0 ? 50 : 75;
        box({x, 0, z - 0.05}, {x + 0.1, 0.9, z + 0.05}, steel);           // post
        box({x - 0.03, 0.9, z - 0.25}, {x + 0.0, 1.4, z + 0.25}, orange);  // plate
    }
    // evening: the range is lit by its own lamps (under the roof, over each target, on the steel)
    m.look.daylight = 0.05;
    auto lamp = [&](V3 at, double power, double range, double cone, double yaw, double pitch, double r, double g, double b) {
        Entity e; e.type = "light"; e.pos = at; e.power = power; e.range = range; e.cone = cone; e.yaw = yaw; e.pitch = pitch;
        e.color[0] = r; e.color[1] = g; e.color[2] = b; m.ents.push_back(e);
    };
    for (double z : {-8.0, 0.0, 8.0}) lamp({-3.2, 2.75, z}, 3.5, 9, 0, 0, -90, 1.0, 0.85, 0.65);       // warm lamps under the roof
    for (int lane = -3; lane <= 3; lane++) {                                                         // a light over each paper target
        double z = lane * 3.2, x = lane % 3 == 0 ? 25 : (std::abs(lane) == 1 ? 7 : 15);
        lamp({x - 1.2, 2.8, z}, 6, 7, 90, 0, -50, 0.95, 0.97, 1.0);
    }
    lamp({58, 4.5, -13.5}, 70, 40, 70, 60, -12, 0.9, 0.95, 1.0);                                     // floodlights on the steel
    lamp({58, 4.5, 13.5}, 70, 40, 70, -60, -12, 0.9, 0.95, 1.0);
    lamp({1.3, 2.6, 0}, 30, 30, 50, 0, -8, 0.95, 0.97, 1.0);                                          // the firing line's floodlight
    Entity plates; plates.type = "armor_plate"; plates.pos = {-3.6, 0, 2.2}; plates.count = 3; plates.respawn = 20; plates.yaw = 20;   // to try the armour bar
    m.ents.push_back(plates);
    Entity crate; crate.type = "attachment_crate"; crate.pos = {-3.6, 0, -2.2}; crate.yaw = -20;   // to try attachments
    m.ents.push_back(crate);
    Entity ammo; ammo.type = "ammo_box"; ammo.pos = {-3.6, 0, 4.4}; ammo.yaw = 10;      // spare ammunition
    m.ents.push_back(ammo);
    Entity guns; guns.type = "weapon_crate"; guns.pos = {-3.8, 0, -4.6}; guns.yaw = -10;   // more guns
    m.ents.push_back(guns);
    Entity start;
    start.pos = {-3, 0, 0};
    start.yaw = 0;
    m.ents.push_back(start);
    return m;
}

}  // namespace gmap
