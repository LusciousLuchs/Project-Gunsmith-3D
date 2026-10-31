#include "design.hpp"
#include "version.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using json = nlohmann::json;
static const double PI_ = 3.14159265358979323846;

static json v2j(const V2& v) { return json::array({v.x, v.y}); }
static V2 j2v(const json& j) {
    V2 v;
    if (j.is_array() && j.size() >= 2) { v.x = j[0].get<double>(); v.y = j[1].get<double>(); }
    return v;
}
template <class T> static T opt(const json& j, const char* k, T def) {
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return def;
    try { return it->get<T>(); } catch (...) { return def; }
}

// ---------------------------------------------------------------- cartridges
static json cartToJson(const Cartridge& c) {
    return {{"name", c.name}, {"caseLen", c.caseLen}, {"rimDia", c.rimDia}, {"rimThick", c.rimThick}, {"baseDia", c.baseDia},
            {"shoulderDia", c.shoulderDia}, {"shoulderAt", c.shoulderAt}, {"neckDia", c.neckDia}, {"bulletDia", c.bulletDia},
            {"oal", c.oal}, {"noseLen", c.noseLen}, {"bulletMass", c.bulletMass}, {"velocity", c.velocity}, {"rimmed", c.rimmed}};
}
static Cartridge cartFromJson(const json& j) {
    Cartridge c;
    c.name = opt<std::string>(j, "name", c.name);
    c.caseLen = opt<double>(j, "caseLen", c.caseLen); c.rimDia = opt<double>(j, "rimDia", c.rimDia);
    c.rimThick = opt<double>(j, "rimThick", c.rimThick); c.baseDia = opt<double>(j, "baseDia", c.baseDia);
    c.shoulderDia = opt<double>(j, "shoulderDia", c.shoulderDia); c.shoulderAt = opt<double>(j, "shoulderAt", c.shoulderAt);
    c.neckDia = opt<double>(j, "neckDia", c.neckDia); c.bulletDia = opt<double>(j, "bulletDia", c.bulletDia);
    c.oal = opt<double>(j, "oal", c.oal); c.noseLen = opt<double>(j, "noseLen", c.noseLen);
    c.bulletMass = opt<double>(j, "bulletMass", c.bulletMass); c.velocity = opt<double>(j, "velocity", c.velocity);
    c.rimmed = opt<bool>(j, "rimmed", c.rimmed);
    return c;
}
static Cartridge mk(const char* n, double caseLen, double rim, double rimT, double base, double shDia, double shAt, double neck,
                    double bullet, double oal, double nose, double mass, double vel, bool rimmed) {
    Cartridge c;
    c.name = n; c.caseLen = caseLen; c.rimDia = rim; c.rimThick = rimT; c.baseDia = base; c.shoulderDia = shDia; c.shoulderAt = shAt;
    c.neckDia = neck; c.bulletDia = bullet; c.oal = oal; c.noseLen = nose; c.bulletMass = mass; c.velocity = vel; c.rimmed = rimmed;
    return c;
}
// Nominal published dimensions; bullet weights and velocities are typical loads, not a reloading reference.
std::vector<V2> cartridgeOutline(const Cartridge& c, bool caseOnly) {
    // (x along the axis, radius) from the head to the bullet tip, then mirrored for the underside
    double rim = c.rimDia / 2, base = c.baseDia / 2, sh = c.shoulderDia / 2, neck = c.neckDia / 2, bul = c.bulletDia / 2;
    double sAt = std::min(c.shoulderAt, c.caseLen), ogive = std::max(1.0, std::min(c.noseLen, c.oal - c.caseLen));
    std::vector<V2> top = {{0, rim}, {c.rimThick, rim}};
    if (!c.rimmed) top.push_back({c.rimThick + 0.6, base * 0.86}), top.push_back({c.rimThick + 1.4, base});  // extractor groove
    else top.push_back({c.rimThick, base});
    top.push_back({sAt, sh});
    if (sAt < c.caseLen - 0.5) top.push_back({std::min(c.caseLen - 0.3, sAt + (sh - neck) * 1.6 + 0.3), neck});
    top.push_back({c.caseLen, caseOnly ? neck : std::max(neck, bul)});
    if (caseOnly) top.push_back({c.caseLen, neck * 0.82});  // the empty mouth
    double bodyEnd = std::max(c.caseLen, c.oal - ogive);
    if (!caseOnly) top.push_back({bodyEnd, bul});
    for (int k = 1; k <= 4 && !caseOnly; k++) {  // ogive
        double t = k / 4.0;
        top.push_back({bodyEnd + ogive * t, bul * std::sqrt(std::max(0.0, 1 - t * t * 0.92))});
    }
    std::vector<V2> out;
    for (auto& p : top) out.push_back({p.x, -p.y});
    for (int k = (int)top.size() - 1; k >= 0; k--) out.push_back({top[k].x, top[k].y});
    return out;
}
double cartridgeMass(const Cartridge& c) {
    const double brass = 0.0085;  // g per mm^3
    double wall = 3.14159 * c.baseDia * c.caseLen * 0.35 * brass, head = 3.14159 * c.headDia() * c.headDia() / 4 * 3 * brass;
    return c.bulletMass + wall + head;
}

const std::vector<Cartridge>& cartridgePresets() {
    static std::vector<Cartridge> v = {
        mk("9x19mm", 19.15, 9.96, 1.27, 9.93, 9.65, 19.15, 9.65, 9.01, 29.69, 7.5, 7.45, 360, false),
        mk(".380 ACP", 17.30, 9.50, 1.10, 9.50, 9.50, 17.30, 9.50, 9.02, 25.00, 6.0, 6.15, 290, false),
        mk(".45 ACP", 22.80, 12.19, 1.24, 12.09, 12.01, 22.80, 12.01, 11.48, 32.40, 8.5, 14.90, 255, false),
        mk("5.56x45mm", 44.70, 9.60, 1.14, 9.58, 9.00, 36.50, 6.43, 5.70, 57.40, 14.0, 4.00, 940, false),
        mk("7.62x39mm", 38.70, 11.35, 1.50, 11.35, 10.07, 30.50, 8.60, 7.92, 56.00, 14.0, 7.90, 730, false),
        mk("7.62x54mmR", 53.72, 14.48, 1.60, 12.37, 11.61, 45.00, 8.53, 7.92, 77.16, 18.0, 9.60, 830, true),
    };
    return v;
}
std::vector<Cartridge> loadUserCartridges() {
    std::vector<Cartridge> out;
    std::ifstream in("cartridges.json");
    if (!in) return out;
    std::stringstream ss; ss << in.rdbuf();
    try {
        json j = json::parse(ss.str());
        if (j.is_array()) for (auto& c : j) out.push_back(cartFromJson(c));
    } catch (...) {}
    return out;
}
bool saveUserCartridges(const std::vector<Cartridge>& list) {
    json j = json::array();
    for (auto& c : list) j.push_back(cartToJson(c));
    std::ofstream out("cartridges.json");
    if (!out) return false;
    out << j.dump(2);
    return true;
}
void applyCartridge(BreechDef& b) {
    b.roundLen = b.cart.oal;
    b.roundDia = b.cart.headDia();
    if (b.cartImpulse) b.impulse = std::round(b.cart.impulse() * 100) / 100;
}

// ---------------------------------------------------------------- shapes
static bool inside(double x, double y, const std::vector<V2>& p) {
    bool c = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        double xi = p[i].x, yi = p[i].y, xj = p[j].x, yj = p[j].y;
        if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) c = !c;
    }
    return c;
}
void fixShape(BodyDef& b) {
    b.rad.resize(b.pts.size(), 0.0);
    b.bulge.resize(b.pts.size(), 0.0);
}
std::vector<V2> outlineOf(const BodyDef& b) {
    const auto& P = b.pts;
    size_t n = P.size();
    if (n < 3) return P;
    bool any = false;
    for (size_t i = 0; i < n; i++) {
        if (i < b.rad.size() && b.rad[i] > 0.01) any = true;
        if (i < b.bulge.size() && std::fabs(b.bulge[i]) > 0.01) any = true;
    }
    if (!any) return P;
    struct Corner { V2 t1, t2, c; double r = 0; bool arc = false; };
    std::vector<Corner> C(n);
    for (size_t i = 0; i < n; i++) {
        V2 p = P[i], a = P[(i + n - 1) % n], q = P[(i + 1) % n];
        double r = i < b.rad.size() ? b.rad[i] : 0;
        C[i].t1 = C[i].t2 = p;
        if (r <= 0.01) continue;
        double la = std::hypot(a.x - p.x, a.y - p.y), lb = std::hypot(q.x - p.x, q.y - p.y);
        if (la < 1e-6 || lb < 1e-6) continue;
        V2 u1{(a.x - p.x) / la, (a.y - p.y) / la}, u2{(q.x - p.x) / lb, (q.y - p.y) / lb};
        double ang = std::acos(std::clamp(u1.x * u2.x + u1.y * u2.y, -1.0, 1.0)), half = ang / 2;
        if (half < 1e-3 || half > PI_ / 2 - 1e-3) continue;
        double t = r / std::tan(half), maxT = 0.48 * std::min(la, lb);
        if (t > maxT) { t = maxT; r = t * std::tan(half); }
        V2 bis{u1.x + u2.x, u1.y + u2.y};
        double bl = std::hypot(bis.x, bis.y);
        bis = {bis.x / bl, bis.y / bl};
        double d = r / std::sin(half);
        C[i] = {{p.x + u1.x * t, p.y + u1.y * t}, {p.x + u2.x * t, p.y + u2.y * t}, {p.x + bis.x * d, p.y + bis.y * d}, r, true};
    }
    std::vector<V2> out;
    for (size_t i = 0; i < n; i++) {
        const Corner& c = C[i];
        out.push_back(c.t1);
        if (c.arc) {
            double a0 = std::atan2(c.t1.y - c.c.y, c.t1.x - c.c.x), a1 = std::atan2(c.t2.y - c.c.y, c.t2.x - c.c.x);
            double da = std::remainder(a1 - a0, 2 * PI_);
            int m = std::max(2, (int)std::ceil(std::fabs(da) / (10 * PI_ / 180)));
            for (int k = 1; k <= m; k++) { double aa = a0 + da * k / m; out.push_back({c.c.x + c.r * std::cos(aa), c.c.y + c.r * std::sin(aa)}); }
        }
        // edge to the next corner, optionally bowed
        double s = i < b.bulge.size() ? b.bulge[i] : 0;
        if (std::fabs(s) > 0.01) {
            V2 st = c.t2, en = C[(i + 1) % n].t1;
            double ex = en.x - st.x, ey = en.y - st.y, len = std::hypot(ex, ey);
            if (len > 1e-6) {
                V2 nn{ey / len, -ex / len};
                V2 mid{(st.x + en.x) / 2, (st.y + en.y) / 2};
                if (inside(mid.x + nn.x * 0.01 * len, mid.y + nn.y * 0.01 * len, P)) nn = {-nn.x, -nn.y};  // make nn point outward
                double as = std::min(std::fabs(s), len * 0.5);
                double R = (len * len / 4 + as * as) / (2 * as), sg = s > 0 ? 1 : -1;
                double sweep = 2 * std::asin(std::min(1.0, len / (2 * R)));
                int m = std::max(4, (int)std::ceil(sweep / (8 * PI_ / 180)));
                for (int k = 1; k < m; k++) {
                    double tt = (double)k / m, x = (tt - 0.5) * len, y = std::sqrt(std::max(0.0, R * R - x * x)) - (R - as);
                    out.push_back({st.x + ex * tt + nn.x * y * sg, st.y + ey * tt + nn.y * y * sg});
                }
            }
        }
    }
    // drop near-duplicate points
    std::vector<V2> clean;
    for (auto& p : out)
        if (clean.empty() || std::hypot(p.x - clean.back().x, p.y - clean.back().y) > 0.05) clean.push_back(p);
    if (clean.size() > 3 && std::hypot(clean.front().x - clean.back().x, clean.front().y - clean.back().y) < 0.05) clean.pop_back();
    return clean.size() >= 3 ? clean : P;
}
V2 centroidOf(const BodyDef& b) {
    auto p = outlineOf(b);
    double a = 0, cx = 0, cy = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        double f = p[j].x * p[i].y - p[i].x * p[j].y;
        a += f; cx += (p[j].x + p[i].x) * f; cy += (p[j].y + p[i].y) * f;
    }
    if (std::fabs(a) < 1e-12) { V2 s; for (auto& q : p) { s.x += q.x; s.y += q.y; } return {s.x / p.size(), s.y / p.size()}; }
    a /= 2;
    return {cx / (6 * a), cy / (6 * a)};
}
std::vector<V2> circlePts(V2 c, double r, std::vector<double>* bulge) {
    if (bulge) bulge->assign(4, r * (1 - std::cos(PI_ / 4)));
    return {{c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}, {c.x, c.y - r}};
}

// ---------------------------------------------------------------- knife, mirror, subdivide
namespace {
struct VR { V2 p; double rad = 0, bulge = 0; };  // a vertex with its corner radius and the curve of the edge after it
double distToBoundary(V2 q, const std::vector<VR>& P) {
    double best = 1e18;
    for (size_t i = 0; i < P.size(); i++) {
        V2 a = P[i].p, b = P[(i + 1) % P.size()].p;
        double dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
        double t = l > 0 ? std::clamp(((q.x - a.x) * dx + (q.y - a.y) * dy) / l, 0.0, 1.0) : 0;
        best = std::min(best, std::hypot(q.x - a.x - t * dx, q.y - a.y - t * dy));
    }
    return best;
}
void splitRec(const std::vector<VR>& P, V2 a, V2 c, int depth, std::vector<std::vector<VR>>& out) {
    size_t n = P.size();
    struct Hit { double t; size_t e; V2 p; };
    std::vector<Hit> hits;
    double sx = c.x - a.x, sy = c.y - a.y;
    for (size_t i = 0; i < n && depth < 10; i++) {
        V2 p = P[i].p, q = P[(i + 1) % n].p;
        double ex = q.x - p.x, ey = q.y - p.y, den = sx * ey - sy * ex;
        if (std::fabs(den) < 1e-12) continue;
        double t = ((p.x - a.x) * ey - (p.y - a.y) * ex) / den, u = ((p.x - a.x) * sy - (p.y - a.y) * sx) / den;
        if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u >= 1 - 1e-9) continue;
        hits.push_back({t, i, {a.x + sx * t, a.y + sy * t}});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.t < y.t; });
    std::vector<V2> poly;
    for (auto& v : P) poly.push_back(v.p);
    for (size_t k = 0; k + 1 < hits.size(); k++) {
        const Hit &h1 = hits[k], &h2 = hits[k + 1];
        if (h1.e == h2.e || std::hypot(h1.p.x - h2.p.x, h1.p.y - h2.p.y) < 0.05) continue;
        V2 mid{(h1.p.x + h2.p.x) / 2, (h1.p.y + h2.p.y) / 2};
        if (!inside(mid.x, mid.y, poly) || distToBoundary(mid, P) < 0.01) continue;
        // cut along the chord h1-h2: walk the outline both ways round
        auto piece = [&](const Hit& from, const Hit& to) {
            std::vector<VR> r;
            r.push_back({from.p, 0, 0});
            size_t i = (from.e + 1) % n;
            for (size_t guard = 0; guard < n; guard++) {
                VR v = P[i];
                if (i == to.e) v.bulge = 0;  // this edge is cut short
                r.push_back(v);
                if (i == to.e) break;
                i = (i + 1) % n;
            }
            r.push_back({to.p, 0, 0});
            std::vector<VR> clean;
            for (auto& v : r)
                if (clean.empty() || std::hypot(v.p.x - clean.back().p.x, v.p.y - clean.back().p.y) > 1e-3) clean.push_back(v);
            if (clean.size() > 2 && std::hypot(clean.front().p.x - clean.back().p.x, clean.front().p.y - clean.back().p.y) < 1e-3) clean.pop_back();
            return clean;
        };
        auto A1 = piece(h1, h2), A2 = piece(h2, h1);
        if (A1.size() >= 3 && A2.size() >= 3) {
            splitRec(A1, a, c, depth + 1, out);
            splitRec(A2, a, c, depth + 1, out);
            return;
        }
    }
    out.push_back(P);
}
}  // namespace

std::vector<BodyDef> splitBody(const BodyDef& b0, V2 a, V2 c) {
    BodyDef b = b0;
    fixShape(b);
    bool curved = false;
    for (double q : b.bulge) if (std::fabs(q) > 0.01) curved = true;
    if (curved) {  // bake curved edges into vertices first so the cut keeps their shape
        BodyDef flat = b;
        flat.pts = outlineOf(b);  // fillets and curves both become real vertices
        flat.rad.assign(flat.pts.size(), 0.0); flat.bulge.assign(flat.pts.size(), 0.0);
        b = flat;
    }
    std::vector<VR> P;
    for (size_t i = 0; i < b.pts.size(); i++) P.push_back({b.pts[i], b.rad[i], b.bulge[i]});
    std::vector<std::vector<VR>> pieces;
    splitRec(P, a, c, 0, pieces);
    std::vector<BodyDef> out;
    for (auto& pc : pieces) {
        BodyDef nb = b;
        nb.pts.clear(); nb.rad.clear(); nb.bulge.clear();
        for (auto& v : pc) {
            nb.pts.push_back({std::round(v.p.x * 100) / 100, std::round(v.p.y * 100) / 100});
            nb.rad.push_back(v.rad); nb.bulge.push_back(v.bulge);
        }
        nb.gen.on = false;
        out.push_back(nb);
    }
    return out;
}
void mirrorBody(BodyDef& b, V2 a, V2 c) {
    fixShape(b);
    double dx = c.x - a.x, dy = c.y - a.y, l2 = dx * dx + dy * dy;
    if (l2 < 1e-12) return;
    auto refl = [&](V2 p) {
        double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2;
        V2 f{a.x + dx * t, a.y + dy * t};
        return V2{2 * f.x - p.x, 2 * f.y - p.y};
    };
    size_t n = b.pts.size();
    std::vector<V2> pts(n);
    std::vector<double> rad(n), bulge(n);
    for (size_t j = 0; j < n; j++) {  // reflect and reverse so the outline keeps its winding
        pts[j] = refl(b.pts[n - 1 - j]);
        rad[j] = b.rad[n - 1 - j];
        bulge[j] = b.bulge[(2 * n - 2 - j) % n];
    }
    for (auto& p : pts) { p.x = std::round(p.x * 100) / 100; p.y = std::round(p.y * 100) / 100; }
    b.pts = pts; b.rad = rad; b.bulge = bulge;
    if (b.ctrl != "none") {
        b.ctrlPt = refl(b.ctrlPt);
        V2 d0{std::cos(b.ctrlDir * PI_ / 180), std::sin(b.ctrlDir * PI_ / 180)}, o = refl({0, 0}), e = refl(d0);
        b.ctrlDir = std::atan2(e.y - o.y, e.x - o.x) * 180 / PI_;
    }
    b.gen.on = false;
}
void flipBody(BodyDef& b, bool horizontal) {
    V2 c = centroidOf(b);
    if (horizontal) mirrorBody(b, c, {c.x, c.y + 1});
    else mirrorBody(b, c, {c.x + 1, c.y});
}
int splitEdge(BodyDef& b, int e) {
    fixShape(b);
    int n = (int)b.pts.size();
    if (n < 2 || e < 0 || e >= n) return -1;
    V2 p = b.pts[e], q = b.pts[(e + 1) % n];
    double ex = q.x - p.x, ey = q.y - p.y, len = std::hypot(ex, ey);
    V2 mid{(p.x + q.x) / 2, (p.y + q.y) / 2};
    double s = b.bulge[e], s2 = 0;
    if (std::fabs(s) > 0.01 && len > 1e-6) {  // a bowed edge: the new corner goes on the curve, and each half keeps the same arc
        V2 nn{ey / len, -ex / len};
        if (inside(mid.x + nn.x * 0.01 * len, mid.y + nn.y * 0.01 * len, b.pts)) nn = {-nn.x, -nn.y};  // outward, as outlineOf does
        double as = std::min(std::fabs(s), len * 0.5), c = len / 2;
        double R = (c * c + as * as) / (2 * as), c2 = std::sqrt(c * c + as * as) / 2;
        s2 = (R - std::sqrt(std::max(0.0, R * R - c2 * c2))) * (s > 0 ? 1 : -1);
        mid = {mid.x + nn.x * as * (s > 0 ? 1 : -1), mid.y + nn.y * as * (s > 0 ? 1 : -1)};
    }
    mid = {std::round(mid.x * 100) / 100, std::round(mid.y * 100) / 100};
    b.pts.insert(b.pts.begin() + e + 1, mid);
    b.rad.insert(b.rad.begin() + e + 1, 0.0);
    b.bulge[e] = s2;
    b.bulge.insert(b.bulge.begin() + e + 1, s2);
    return e + 1;
}
void subdivideBody(BodyDef& b) {
    fixShape(b);
    for (int i = (int)b.pts.size() - 1; i >= 0; i--) splitEdge(b, i);
}

// ---------------------------------------------------------------- magazine maker
V2 magCentre(const MagGen& g, double s, double* phiDeg) {
    V2 p = g.top;
    int N = 40;
    double H = g.H > 1 ? g.H : 1, du = s / N;
    double phi = g.angle;
    for (int k = 0; k < N; k++) {
        double u = (k + 0.5) * du;
        phi = g.angle + g.curve * (u / H);
        p.x += std::sin(phi * PI_ / 180) * du;
        p.y += std::cos(phi * PI_ / 180) * du;
    }
    if (phiDeg) *phiDeg = g.angle + g.curve * (s / H);
    return p;
}
void generateMagazine(Design& D, int bi) {
    if (bi < 0 || bi >= (int)D.bodies.size()) return;
    BodyDef& b = D.bodies[bi];
    MagGen& g = b.gen;
    const Cartridge& c = D.breech.cart;
    int wi = -1;
    for (size_t i = 0; i < D.joints.size(); i++) if (D.joints[i].type == "magwell" && D.joints[i].a == b.id) wi = (int)i;
    if (!g.on) {  // first time: keep the top of the existing part where it is
        auto o = outlineOf(b);
        double x0 = 1e9, x1 = -1e9, y0 = 1e9;
        for (auto& p : o) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); }
        g.top = {(x0 + x1) / 2, y0};
        g.on = true;
    }
    if (wi >= 0 && D.joints[wi].path.size() >= 2) {
        auto& j = D.joints[wi];
        g.angle = std::atan2(j.path.back().x - j.path.front().x, j.path.back().y - j.path.front().y) * 180 / PI_;
    }
    b.magCap = std::clamp(b.magCap, 1, 250);
    double dia = c.headDia() * 1.02;
    double pitch = g.stack == 2 ? dia * 0.56 : dia * 0.98;
    double stackH = g.stack == 2 ? b.magCap * pitch + dia * 0.5 : b.magCap * pitch;
    g.H = 3 + stackH + 12 + 5;  // lips, rounds, follower and spring, floorplate
    g.W = c.oal + 2 * g.wall + 3;
    int N = std::max(4, (int)std::ceil(std::fabs(g.curve) / 6) + 4);
    std::vector<V2> rear, front;
    for (int k = 0; k <= N; k++) {
        double s = g.H * k / N, phi;
        V2 m = magCentre(g, s, &phi);
        V2 nrm{std::cos(phi * PI_ / 180), -std::sin(phi * PI_ / 180)};  // points forward
        rear.push_back({m.x - nrm.x * g.W / 2, m.y - nrm.y * g.W / 2});
        front.push_back({m.x + nrm.x * g.W / 2, m.y + nrm.y * g.W / 2});
    }
    b.pts.clear();
    for (auto& p : rear) b.pts.push_back(p);
    for (int k = N; k >= 0; k--) b.pts.push_back(front[k]);
    for (auto& p : b.pts) { p.x = std::round(p.x * 100) / 100; p.y = std::round(p.y * 100) / 100; }
    b.rad.assign(b.pts.size(), 0.0);
    b.bulge.assign(b.pts.size(), 0.0);
    b.rad[N] = 2.5; b.rad[N + 1] = 2.5;  // rounded floorplate corners
    b.mass = std::round(45 + 0.35 * g.H);
    b.thick = std::round(std::max(12.0, c.headDia() * (g.stack == 2 ? 1.75 : 1.15) + 2));
    b.magazine = true;
    b.ghost = true;
    // the well: straight, along the top of the magazine, long enough for it to drop clear
    double a = g.angle * PI_ / 180;
    V2 d0{std::sin(a), std::cos(a)};
    V2 p{g.top.x + d0.x * 10, g.top.y + d0.y * 10};
    double travel = g.H + 6;
    if (wi < 0) {
        JointDef j; j.type = "magwell"; j.a = b.id; j.b = "world";
        for (int i = (int)D.bodies.size() - 1; i >= 0; i--)
            if (D.bodies[i].fixed && inside(g.top.x, g.top.y + 2, outlineOf(D.bodies[i]))) { j.b = D.bodies[i].id; break; }
        if (j.b == "world") for (auto& o : D.bodies) if (o.fixed) { j.b = o.id; break; }
        D.joints.push_back(j);
        wi = (int)D.joints.size() - 1;
    }
    auto& j = D.joints[wi];
    j.p = p;
    j.path = {p, {p.x + d0.x * travel, p.y + d0.y * travel}};
}

// ---------------------------------------------------------------- JSON
std::string designToJson(const Design& d, bool pretty) {
    json o;
    o["format"] = GUNSMITH_BLUEPRINT_FORMAT;
    o["savedWith"] = std::string("Gunsmith3D ") + version::text();
    o["name"] = d.name;
    o["speed"] = d.speed;
    o["friction"] = d.friction;
    o["bodies"] = json::array();
    for (auto& b : d.bodies) {
        json jb = {{"id", b.id}, {"name", b.name}, {"fixed", b.fixed}, {"layer", b.layer},
                   {"thick", b.thick}, {"z", b.z}, {"color", b.color}, {"damp", b.damp}, {"ghost", b.ghost}};
        if (b.smooth > 0) jb["smooth"] = b.smooth;
        if (b.mass > 0) jb["mass"] = b.mass;
        if (b.magazine) {
            jb["magazine"] = {{"cap", b.magCap}};
            if (b.gen.on) jb["magazine"]["gen"] = {{"stack", b.gen.stack}, {"curve", b.gen.curve}, {"wall", b.gen.wall}, {"angle", b.gen.angle}, {"top", v2j(b.gen.top)}, {"H", b.gen.H}, {"W", b.gen.W}};
        }
        if (b.rollOn) jb["roll"] = {{"on", true}, {"min", b.rollMin}, {"max", b.rollMax}, {"handle", b.handle}};
        if (b.ctrl != "none") jb["control"] = {{"kind", b.ctrl}, {"key", b.key}, {"at", v2j(b.ctrlPt)}, {"dir", b.ctrlDir}, {"force", b.ctrlForce}};
        json pts = json::array();
        for (auto& p : b.pts) pts.push_back(v2j(p));
        jb["pts"] = pts;
        bool anyR = false, anyB = false;
        for (double r : b.rad) if (r > 0) anyR = true;
        for (double s : b.bulge) if (s != 0) anyB = true;
        if (anyR) jb["rad"] = b.rad;
        if (anyB) jb["bulge"] = b.bulge;
        o["bodies"].push_back(jb);
    }
    o["joints"] = json::array();
    for (auto& j : d.joints) {
        json jj = {{"type", j.type}, {"a", j.a}, {"b", j.b}};
        if (j.type == "pin" || j.type == "weld") jj["p"] = v2j(j.p);
        if (j.type == "track" || j.type == "magwell") {
            jj["p"] = v2j(j.p);
            json p = json::array();
            for (auto& q : j.path) p.push_back(v2j(q));
            jj["path"] = p;
            jj["lockRot"] = j.lockRot;
            if (j.type == "magwell" && !j.rel.empty()) { jj["rel"] = j.rel; jj["releaseAt"] = j.releaseAt; }
        }
        if (j.type == "spring") {
            jj["pa"] = v2j(j.pa); jj["pb"] = v2j(j.pb); jj["k"] = j.k; jj["rest"] = j.rest; jj["damp"] = j.damp;
            jj["kind"] = j.kind;
            if (j.kind == "coil") { jj["guide"] = j.guide; jj["coilDia"] = j.coilDia; jj["wire"] = j.wire; jj["turns"] = j.turns; }
            else { jj["preload"] = j.preload; jj["flip"] = j.flip; }
        }
        if (j.type == "cam") { json t = json::array(); for (auto& q : j.table) t.push_back(v2j(q)); jj["table"] = t; }
        if (j.type == "lug") { jj["unlock"] = j.unlock; jj["blockOpen"] = j.blockOpen; }
        if (j.type == "latch") { jj["catchAt"] = j.catchAt; jj["rel"] = j.rel; jj["releaseAt"] = j.releaseAt; }
        o["joints"].push_back(jj);
    }
    if (d.breech.on) {
        auto& r = d.breech;
        json jb = {{"breech", r.breech}, {"chamber", r.chamber}, {"impulse", r.impulse}, {"dur", r.dur}, {"feed", r.feed},
                   {"rounds", r.rounds}, {"auto", r.autoFire}, {"loaded", r.loaded}, {"round", {r.roundLen, r.roundDia}},
                   {"cartridge", cartToJson(r.cart)}, {"cartImpulse", r.cartImpulse}, {"feedMode", r.feedMode}, {"physicalFeed", r.physicalFeed},
                   {"ignition", r.ignition}, {"striker", r.striker}, {"strikerTip", v2j(r.strikerTip)}, {"igniteSpeed", r.igniteSpeed},
                   {"hammer", r.hammer}, {"cockAt", r.cockAt}, {"fireMode", r.fireMode}, {"burst", r.burst},
                   {"doubleAction", r.doubleAction}, {"lockTime", r.lockTime}};
        jb["auto"] = r.fullAuto();
        jb["face"] = r.hasFace ? v2j(r.face) : json(nullptr);
        jb["muzzle"] = r.hasMuzzle ? v2j(r.muzzle) : json(nullptr);
        o["breech"] = jb;
    } else o["breech"] = nullptr;
    return pretty ? o.dump(2) : o.dump();
}

std::string blueprintVersionNote(const Design& d) {
    if (d.format <= GUNSMITH_BLUEPRINT_FORMAT) return "";
    return "This blueprint was saved by " + (d.savedWith.empty() ? std::string("a newer Gunsmith3D") : d.savedWith) +
           ", which is newer than this one (" + version::text() + "). Some of it may not load correctly.";
}

bool designFromJson(const std::string& text, Design& out, std::string* err) {
    json o;
    try { o = json::parse(text); } catch (std::exception& e) { if (err) *err = e.what(); return false; }
    if (!o.is_object() || !o.contains("bodies") || !o["bodies"].is_array() || !o.contains("joints") || !o["joints"].is_array()) {
        if (err) *err = "missing bodies or joints";
        return false;
    }
    Design d;
    d.format = opt<int>(o, "format", 1);
    d.savedWith = opt<std::string>(o, "savedWith", "");
    d.name = opt<std::string>(o, "name", "Untitled action");
    d.speed = opt<double>(o, "speed", 0.02);
    d.friction = opt<double>(o, "friction", 0.12);
    int n = 0;
    for (auto& jb : o["bodies"]) {
        BodyDef b;
        b.id = opt<std::string>(jb, "id", "b" + std::to_string(n++));
        b.name = opt<std::string>(jb, "name", b.id);
        b.fixed = opt<bool>(jb, "fixed", false);
        b.layer = opt<std::string>(jb, "layer", "none");
        b.z = opt<double>(jb, "z", 0.0);
        b.thick = opt<double>(jb, "thick", 10);
        b.mass = opt<double>(jb, "mass", 0);
        b.damp = opt<double>(jb, "damp", 0);
        b.color = opt<std::string>(jb, "color", "#8e9ab4");
        b.ghost = opt<bool>(jb, "ghost", b.fixed);
        b.smooth = std::clamp(opt<double>(jb, "smooth", 0.0), 0.0, 1.0);
        if (jb.contains("roll") && jb["roll"].is_object()) {
            auto& r = jb["roll"];
            b.rollOn = opt<bool>(r, "on", false);
            b.rollMin = opt<double>(r, "min", 0);
            b.rollMax = opt<double>(r, "max", 90);
            b.handle = opt<double>(r, "handle", 0);
        }
        if (jb.contains("magazine") && jb["magazine"].is_object()) {
            auto& m = jb["magazine"];
            b.magazine = true; b.magCap = opt<int>(m, "cap", 10);
            if (m.contains("gen") && m["gen"].is_object()) {
                auto& g = m["gen"];
                b.gen.on = true; b.gen.stack = opt<int>(g, "stack", 2); b.gen.curve = opt<double>(g, "curve", 0);
                b.gen.wall = opt<double>(g, "wall", 1.2); b.gen.angle = opt<double>(g, "angle", 0);
                if (g.contains("top")) b.gen.top = j2v(g["top"]);
                b.gen.H = opt<double>(g, "H", 0); b.gen.W = opt<double>(g, "W", 0);
            }
        }
        if (jb.contains("control") && jb["control"].is_object()) {
            auto& c = jb["control"];
            b.ctrl = opt<std::string>(c, "kind", "button"); b.key = opt<std::string>(c, "key", "none");
            if (c.contains("at")) b.ctrlPt = j2v(c["at"]);
            b.ctrlDir = opt<double>(c, "dir", 180); b.ctrlForce = opt<double>(c, "force", 25);
        }
        if (jb.contains("pts")) for (auto& p : jb["pts"]) b.pts.push_back(j2v(p));
        if (jb.contains("rad") && jb["rad"].is_array()) for (auto& r : jb["rad"]) b.rad.push_back(r.get<double>());
        if (jb.contains("bulge") && jb["bulge"].is_array()) for (auto& r : jb["bulge"]) b.bulge.push_back(r.get<double>());
        fixShape(b);
        if (b.pts.size() >= 3) d.bodies.push_back(b);
    }
    for (auto& jj : o["joints"]) {
        JointDef j;
        j.type = opt<std::string>(jj, "type", "");
        j.a = opt<std::string>(jj, "a", "");
        j.b = opt<std::string>(jj, "b", "world");
        if (jj.contains("p")) j.p = j2v(jj["p"]);
        if (jj.contains("pa")) j.pa = j2v(jj["pa"]);
        if (jj.contains("pb")) j.pb = j2v(jj["pb"]);
        if (jj.contains("path")) for (auto& p : jj["path"]) j.path.push_back(j2v(p));
        if (jj.contains("table")) for (auto& p : jj["table"]) j.table.push_back(j2v(p));
        j.lockRot = opt<bool>(jj, "lockRot", false);
        j.k = opt<double>(jj, "k", 1);
        j.rest = opt<double>(jj, "rest", 0);
        j.damp = opt<double>(jj, "damp", 0);
        j.kind = opt<std::string>(jj, "kind", "coil");
        j.guide = opt<std::string>(jj, "guide", "");
        j.coilDia = opt<double>(jj, "coilDia", 8);
        j.wire = opt<double>(jj, "wire", 1);
        j.turns = opt<double>(jj, "turns", 12);
        j.preload = opt<double>(jj, "preload", 1);
        j.flip = opt<bool>(jj, "flip", false);
        j.unlock = opt<double>(jj, "unlock", 45);
        j.blockOpen = opt<bool>(jj, "blockOpen", false);
        j.catchAt = opt<double>(jj, "catchAt", -30);
        j.rel = opt<std::string>(jj, "rel", "");
        j.releaseAt = opt<double>(jj, "releaseAt", j.type == "magwell" ? 2.0 : -2.0);
        static const char* ok[] = {"pin", "weld", "track", "spring", "cam", "lug", "latch", "magwell"};
        bool known = false;
        for (auto k : ok) if (j.type == k) known = true;
        if (known) d.joints.push_back(j);
    }
    if (o.contains("breech") && o["breech"].is_object()) {
        auto& jb = o["breech"];
        auto& r = d.breech;
        r.on = true;
        r.breech = opt<std::string>(jb, "breech", "");
        r.chamber = opt<std::string>(jb, "chamber", "world");
        if (jb.contains("face") && jb["face"].is_array()) { r.hasFace = true; r.face = j2v(jb["face"]); }
        if (jb.contains("muzzle") && jb["muzzle"].is_array()) { r.hasMuzzle = true; r.muzzle = j2v(jb["muzzle"]); }
        r.impulse = opt<double>(jb, "impulse", 3);
        r.dur = opt<double>(jb, "dur", 1);
        r.feed = opt<double>(jb, "feed", 20);
        r.rounds = opt<int>(jb, "rounds", 10);
        r.autoFire = opt<bool>(jb, "auto", false);
        r.loaded = opt<bool>(jb, "loaded", true);
        if (jb.contains("round") && jb["round"].is_array() && jb["round"].size() >= 2) {
            r.roundLen = jb["round"][0].get<double>();
            r.roundDia = jb["round"][1].get<double>();
        }
        bool hasWell = false;
        for (auto& j : d.joints) if (j.type == "magwell") hasWell = true;
        if (jb.contains("cartridge") && jb["cartridge"].is_object()) {
            r.cart = cartFromJson(jb["cartridge"]);
            r.cartImpulse = opt<bool>(jb, "cartImpulse", true);
        } else if (jb.contains("cartridge") && jb["cartridge"].is_string()) {  // by preset name
            std::string nm = jb["cartridge"].get<std::string>();
            for (auto& c : cartridgePresets()) if (c.name == nm) r.cart = c;
            r.cartImpulse = opt<bool>(jb, "cartImpulse", true);
        } else {  // older blueprints: keep their own impulse and round size
            r.cartImpulse = false;
            r.cart.oal = r.roundLen; r.cart.rimDia = r.cart.baseDia = r.roundDia;
            r.cart.name = "Custom";
        }
        r.feedMode = opt<std::string>(jb, "feedMode", hasWell ? "magazine" : "internal");
        r.physicalFeed = opt<bool>(jb, "physicalFeed", false);
        r.ignition = opt<std::string>(jb, "ignition", "button");
        r.striker = opt<std::string>(jb, "striker", "");
        if (jb.contains("strikerTip")) r.strikerTip = j2v(jb["strikerTip"]);
        r.igniteSpeed = opt<double>(jb, "igniteSpeed", 1.5);
        r.hammer = opt<std::string>(jb, "hammer", "striker");
        r.cockAt = opt<double>(jb, "cockAt", 0);   // older blueprints: always cocked, as before
        r.fireMode = opt<std::string>(jb, "fireMode", r.autoFire ? "auto" : "semi");
        if (r.fireMode != "semi" && r.fireMode != "auto" && r.fireMode != "burst") r.fireMode = "semi";
        r.autoFire = r.fireMode == "auto";
        r.burst = std::max(2, opt<int>(jb, "burst", 3));
        r.doubleAction = opt<bool>(jb, "doubleAction", false);
        r.lockTime = std::max(0.0, opt<double>(jb, "lockTime", 0));
        if (r.cartImpulse) applyCartridge(r);
    }
    out = d;
    return true;
}

double groundLevelMM(const Design& d) {
    double y1 = 60, extra = 0;
    for (auto& b : d.bodies) for (auto& p : outlineOf(b)) y1 = std::max(y1, p.y);
    for (auto& j : d.joints) {
        if (j.type != "magwell" || j.path.size() < 2) continue;
        for (auto& b : d.bodies) {
            if (b.id != j.a) continue;
            double ymax = -1e9;
            for (auto& p : outlineOf(b)) ymax = std::max(ymax, p.y);
            double dropBottom = ymax + (j.path.back().y - j.path.front().y);
            extra = std::max(extra, dropBottom - y1);
        }
    }
    return y1 + std::max(0.0, extra) + 45;
}

// ---------------------------------------------------------------- presets (schematic, not copies of real parts)
#include "presets.inc"

const std::vector<PresetInfo>& presetList() {
    static std::vector<PresetInfo> v = [] {
        std::vector<PresetInfo> l;
        for (auto& p : PRESETS) if (p.menu) l.push_back({p.key, p.label});
        return l;
    }();
    return v;
}

Design loadPreset(const std::string& key) {
    const char* src = PRESET_BLANK;
    for (auto& p : PRESETS) if (key == p.key) src = p.json;
    Design d;
    designFromJson(src, d);
    for (size_t i = 0; i < d.bodies.size(); i++)
        if (d.bodies[i].magazine && d.bodies[i].gen.on) generateMagazine(d, (int)i);
    return d;
}
