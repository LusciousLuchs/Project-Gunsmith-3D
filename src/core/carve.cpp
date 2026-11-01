// Gunsmith3D - carves (see carve.hpp).
#include "carve.hpp"
#include <algorithm>
#include <cmath>

namespace carve {

using csg::cross;
using csg::dot;
using csg::unit;
static const double PI_ = 3.14159265358979323846;

void frame(const CarveDef& c, P3& u, P3& v, P3& d) {
    d = unit({c.dx, c.dy, c.dz});
    P3 u0 = P3{1, 0, 0} - d * d.x;          // forward, laid flat across the cutter
    if (dot(u0, u0) < 0.01) u0 = P3{0, 0, 1} - d * d.z;  // cutting along the bore: use the depth axis
    u0 = unit(u0);
    P3 v0 = cross(d, u0);
    double s = c.spin * PI_ / 180, cs = std::cos(s), sn = std::sin(s);
    u = u0 * cs + v0 * sn;
    v = v0 * cs - u0 * sn;
}

std::vector<V2> shape2D(const CarveDef& c, int segs) {
    double a = std::max(0.1, c.w) / 2, b = std::max(0.1, c.h) / 2;
    std::vector<V2> o;
    if (c.shape == "rect") o = {{-a, -b}, {a, -b}, {a, b}, {-a, b}};
    else if (c.shape == "tri") o = {{-a, -b}, {a, -b}, {0, b}};
    else if (c.shape == "half") {  // flat side along u at the bottom, round over the top
        int n = std::max(4, segs / 2);
        for (int i = 0; i <= n; i++) { double t = PI_ * i / n; o.push_back({a * std::cos(t), -b + 2 * b * std::sin(t)}); }
    } else {
        for (int i = 0; i < segs; i++) { double t = 2 * PI_ * i / segs; o.push_back({a * std::cos(t), b * std::sin(t)}); }
    }
    return o;
}

double leadIn(const CarveDef& c) { return 0.3 + 0.5 * std::max(c.w, c.h); }

Prism prism(const CarveDef& c, const BodyDef& b) {
    P3 u, v, d;
    frame(c, u, v, d);
    double lead = leadIn(c), deep = c.depth > 0 ? c.depth : 2000.0;
    P3 s = P3{c.at.x, c.at.y, b.z + c.atZ} - d * lead;
    Prism p;
    for (auto& q : shape2D(c)) {
        P3 a = s + u * q.x + v * q.y;
        p.lo.push_back(a);
        p.hi.push_back(a + d * (lead + deep));
    }
    return p;
}

std::vector<csg::Poly> prismPolys(const Prism& p, const CarveDef& c) {
    P3 u, v, d;
    frame(c, u, v, d);
    size_t n = p.lo.size();
    P3 mid{0, 0, 0};
    for (size_t i = 0; i < n; i++) mid = mid + (p.lo[i] + p.hi[i]) * (0.5 / n);
    std::vector<csg::Poly> out;
    auto add = [&](std::vector<csg::Vert> vs) {
        csg::Poly q = csg::makePoly(vs);
        P3 cc{0, 0, 0};
        for (auto& x : q.v) cc = cc + x.p * (1.0 / q.v.size());
        if (dot(q.pn, cc - mid) < 0) {  // face it outward
            std::reverse(vs.begin(), vs.end());
            q = csg::makePoly(vs);
        }
        out.push_back(q);
    };
    std::vector<csg::Vert> lo, hi;
    for (size_t i = 0; i < n; i++) { lo.push_back({p.lo[i], d * -1}); hi.push_back({p.hi[i], d}); }
    add(lo); add(hi);
    // side walls: smooth where the outline turns only a little (round cutters), flat across corners
    std::vector<P3> fn(n);
    for (size_t i = 0; i < n; i++) {
        P3 e = p.lo[(i + 1) % n] - p.lo[i];
        P3 nn = unit(cross(e, d));
        P3 em = (p.lo[i] + p.lo[(i + 1) % n]) * 0.5;
        P3 cl{0, 0, 0};
        for (auto& x : p.lo) cl = cl + x * (1.0 / n);
        if (dot(nn, em - cl) < 0) nn = nn * -1;
        fn[i] = nn;
    }
    auto vn = [&](size_t edge, size_t vert) {
        size_t other = vert == edge ? (edge + n - 1) % n : (edge + 1) % n;  // the wall on the other side of that corner
        P3 e = fn[edge], o = fn[other];
        if (dot(e, o) < std::cos(25 * PI_ / 180)) return e;
        return unit(e + o);
    };
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        add({{p.lo[i], vn(i, i)}, {p.lo[j], vn(i, j)}, {p.hi[j], vn(i, j)}, {p.hi[i], vn(i, i)}});
    }
    return out;
}

static std::vector<V2> hull2(std::vector<V2> p) {
    std::sort(p.begin(), p.end(), [](V2 a, V2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    if (p.size() < 3) return {};
    auto cr = [](V2 o, V2 a, V2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    std::vector<V2> h(2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); i++) { while (k >= 2 && cr(h[k - 2], h[k - 1], p[i]) <= 1e-12) k--; h[k++] = p[i]; }
    for (size_t i = p.size() - 1, t = k + 1; i-- > 0;) { while (k >= t && cr(h[k - 2], h[k - 1], p[i]) <= 1e-12) k--; h[k++] = p[i]; }
    h.resize(k - 1);
    return h.size() >= 3 ? h : std::vector<V2>{};
}

std::vector<V2> section(const Prism& p, double z) {
    std::vector<V2> pts;
    auto edge = [&](const P3& a, const P3& b) {
        double da = a.z - z, db = b.z - z;
        if (std::fabs(da) < 1e-9) pts.push_back({a.x, a.y});
        if (da * db < 0) { double t = da / (da - db); pts.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}); }
    };
    size_t n = p.lo.size();
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        edge(p.lo[i], p.lo[j]); edge(p.hi[i], p.hi[j]); edge(p.lo[i], p.hi[i]);
        if (std::fabs(p.hi[i].z - z) < 1e-9) pts.push_back({p.hi[i].x, p.hi[i].y});
    }
    return hull2(pts);
}

static double area2(const std::vector<V2>& q) {
    double a = 0;
    for (size_t i = 0; i < q.size(); i++) { const V2 &p = q[i], &r = q[(i + 1) % q.size()]; a += p.x * r.y - r.x * p.y; }
    return a / 2;
}

bool inConvex(const std::vector<V2>& q, V2 p) {
    if (q.size() < 3) return false;
    bool pos = false, neg = false;
    for (size_t i = 0; i < q.size(); i++) {
        const V2 &a = q[i], &b = q[(i + 1) % q.size()];
        double c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (c > 0) pos = true; else if (c < 0) neg = true;
        if (pos && neg) return false;
    }
    return true;
}

// A cut's fingerprint, to tell whether the cuts change between two depths.
struct Sig { double a, x0, y0, x1, y1; };
static Sig sigOf(const std::vector<V2>& q) {
    Sig s{std::fabs(area2(q)), 1e9, 1e9, -1e9, -1e9};
    for (auto& p : q) { s.x0 = std::min(s.x0, p.x); s.y0 = std::min(s.y0, p.y); s.x1 = std::max(s.x1, p.x); s.y1 = std::max(s.y1, p.y); }
    if (q.empty()) s = {0, 0, 0, 0, 0};
    return s;
}
static bool same(const std::vector<std::vector<V2>>& A, const std::vector<std::vector<V2>>& B, double tol) {
    if (A.size() != B.size()) return false;
    for (size_t i = 0; i < A.size(); i++) {
        Sig a = sigOf(A[i]), b = sigOf(B[i]);
        if (std::fabs(a.x0 - b.x0) > tol || std::fabs(a.y0 - b.y0) > tol || std::fabs(a.x1 - b.x1) > tol || std::fabs(a.y1 - b.y1) > tol) return false;
        if (std::fabs(a.a - b.a) > std::max(0.02, 0.02 * std::max(a.a, b.a))) return false;
    }
    return true;
}

std::vector<Slice> slices(const BodyDef& b, int maxSlices) {
    if (b.carves.empty()) return {};
    std::vector<Prism> P;
    for (auto& c : b.carves) P.push_back(prism(c, b));
    double t = std::max(0.5, b.thick), zA = b.z - t / 2, zB = b.z + t / 2;
    double ox0 = 1e9, oy0 = 1e9, ox1 = -1e9, oy1 = -1e9;
    for (auto& p : b.pts) { ox0 = std::min(ox0, p.x); oy0 = std::min(oy0, p.y); ox1 = std::max(ox1, p.x); oy1 = std::max(oy1, p.y); }
    ox0 -= 20; oy0 -= 20; ox1 += 20; oy1 += 20;  // (bowed edges reach a little outside the control points)
    auto cutsAt = [&](double z) {
        std::vector<std::vector<V2>> cav;
        for (auto& p : P) {
            auto s = section(p, z);
            if (s.empty()) continue;
            Sig g = sigOf(s);
            if (g.a < 1e-4 || g.x1 < ox0 || g.x0 > ox1 || g.y1 < oy0 || g.y0 > oy1) continue;
            cav.push_back(s);
        }
        return cav;
    };
    std::vector<double> br = {zA, zB};
    for (auto& p : P)
        for (auto* ring : {&p.lo, &p.hi})
            for (auto& q : *ring) if (q.z > zA && q.z < zB) br.push_back(q.z);
    std::sort(br.begin(), br.end());
    std::vector<double> bp;
    for (double z : br) if (bp.empty() || z - bp.back() > 1e-4) bp.push_back(z);
    if (bp.back() < zB - 1e-9) bp.back() = zB;
    std::vector<Slice> out;
    for (size_t i = 0; i + 1 < bp.size(); i++) {
        double p = bp[i], q = bp[i + 1], e = std::min(1e-4, (q - p) / 4);
        bool vary = !same(cutsAt(p + e), cutsAt((p + q) / 2), 0.03) || !same(cutsAt(q - e), cutsAt((p + q) / 2), 0.03);
        int n = vary ? std::max(1, (int)std::ceil((q - p) / 0.5)) : 1;
        for (int k = 0; k < n; k++) {
            Slice sl; sl.z0 = p + (q - p) * k / n; sl.z1 = p + (q - p) * (k + 1) / n;
            sl.cav = cutsAt((sl.z0 + sl.z1) / 2);
            if (!out.empty() && same(out.back().cav, sl.cav, 0.05)) out.back().z1 = sl.z1;  // no change: one slice
            else out.push_back(sl);
        }
    }
    // too many: merge the neighbours that differ least, weighted by how thick they are
    auto diff = [](const std::vector<std::vector<V2>>& A, const std::vector<std::vector<V2>>& B) {
        double d = 0;
        if (A.size() != B.size()) { for (auto& q : A) d += sigOf(q).a; for (auto& q : B) d += sigOf(q).a; return d + 1; }
        for (size_t i = 0; i < A.size(); i++) {
            Sig a = sigOf(A[i]), b = sigOf(B[i]);
            d += std::fabs(a.a - b.a) + std::fabs(a.x0 - b.x0) + std::fabs(a.x1 - b.x1) + std::fabs(a.y0 - b.y0) + std::fabs(a.y1 - b.y1);
        }
        return d;
    };
    while ((int)out.size() > std::max(1, maxSlices)) {
        size_t best = 0; double bc = 1e18;
        for (size_t i = 0; i + 1 < out.size(); i++) {
            double c = diff(out[i].cav, out[i + 1].cav) * (out[i + 1].z1 - out[i].z0);
            if (c < bc) { bc = c; best = i; }
        }
        out[best].z1 = out[best + 1].z1;
        out[best].cav = cutsAt((out[best].z0 + out[best].z1) / 2);
        out.erase(out.begin() + best + 1);
    }
    return out;
}

bool material(const std::vector<V2>& o, const std::vector<std::vector<V2>>& cav, double& area, V2& cen, double& J) {
    double y0 = 1e9, y1 = -1e9;
    for (auto& p : o) { y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    if (o.size() < 3 || y1 <= y0) return false;
    int rows = std::clamp((int)((y1 - y0) / 0.05), 200, 4000);
    double dy = (y1 - y0) / rows, A = 0, Sx = 0, Sy = 0, Sxx = 0, Syy = 0;
    std::vector<double> xs;
    std::vector<std::pair<double, double>> in, cut;
    auto crossings = [&](const std::vector<V2>& q, double y) {
        xs.clear();
        for (size_t i = 0; i < q.size(); i++) {
            const V2 &a = q[i], &b = q[(i + 1) % q.size()];
            if ((a.y <= y && y < b.y) || (b.y <= y && y < a.y)) xs.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(xs.begin(), xs.end());
    };
    for (int r = 0; r < rows; r++) {
        double y = y0 + (r + 0.5) * dy;
        crossings(o, y);
        in.clear();
        for (size_t i = 0; i + 1 < xs.size(); i += 2) in.push_back({xs[i], xs[i + 1]});
        cut.clear();
        for (auto& c : cav) { crossings(c, y); if (xs.size() >= 2) cut.push_back({xs.front(), xs.back()}); }
        for (auto& c : cut) {  // take each cut out of the solid spans
            std::vector<std::pair<double, double>> nx;
            for (auto& s : in) {
                if (c.second <= s.first || c.first >= s.second) { nx.push_back(s); continue; }
                if (c.first > s.first) nx.push_back({s.first, c.first});
                if (c.second < s.second) nx.push_back({c.second, s.second});
            }
            in.swap(nx);
        }
        for (auto& s : in) {
            double a = s.first, b = s.second, L = b - a;
            A += L * dy; Sx += (b * b - a * a) / 2 * dy; Sy += y * L * dy;
            Sxx += (b * b * b - a * a * a) / 3 * dy; Syy += (y * y * L + L * dy * dy / 12) * dy;
        }
    }
    if (A < 1e-6) return false;
    area = A; cen = {Sx / A, Sy / A};
    J = std::max(0.0, Sxx + Syy - A * (cen.x * cen.x + cen.y * cen.y));
    return true;
}

double volume(const BodyDef& b) {
    std::vector<V2> o = outlineOf(b);
    double a = 0;
    for (size_t i = 0; i < o.size(); i++) { const V2 &p = o[i], &r = o[(i + 1) % o.size()]; a += p.x * r.y - r.x * p.y; }
    if (b.carves.empty()) return std::fabs(a / 2) * b.thick;
    double v = 0;
    for (auto& s : slices(b)) {
        double ar, J; V2 c;
        if (material(o, s.cav, ar, c, J)) v += ar * (s.z1 - s.z0);
    }
    return v;
}
void moveCarves(BodyDef& b, double dx, double dy) { for (auto& c : b.carves) { c.at.x += dx; c.at.y += dy; } }
void mirrorCarves(BodyDef& b, V2 a, V2 c) {
    double lx = c.x - a.x, ly = c.y - a.y, l = std::hypot(lx, ly);
    if (l < 1e-12) return;
    lx /= l; ly /= l;
    double th = std::atan2(ly, lx);
    for (auto& k : b.carves) {
        double t = (k.at.x - a.x) * lx + (k.at.y - a.y) * ly;
        V2 f{a.x + lx * t, a.y + ly * t};
        k.at = {2 * f.x - k.at.x, 2 * f.y - k.at.y};
        double dl = k.dx * lx + k.dy * ly;
        k.dx = 2 * dl * lx - k.dx; k.dy = 2 * dl * ly - k.dy;
        // the shape's "up" (v) as seen from the side, reflected the same way (exact for cuts from the side)
        double s = k.spin * PI_ / 180, ph = std::atan2(-std::cos(s), -std::sin(s)), ph2 = 2 * th - ph;
        k.spin = std::atan2(-std::cos(ph2), -std::sin(ph2)) * 180 / PI_;
    }
}
void flipCarvesZ(BodyDef& b) { for (auto& c : b.carves) { c.atZ = -c.atZ; c.dz = -c.dz; c.spin = 180 - c.spin; } }
void rethick(BodyDef& b, double oldThick) {
    double o = std::max(0.5, oldThick) / 2, n = std::max(0.5, b.thick) / 2;
    for (auto& c : b.carves) {
        if (std::fabs(c.atZ - o) < 0.05) c.atZ = n;
        else if (std::fabs(c.atZ + o) < 0.05) c.atZ = -n;
    }
}

}  // namespace carve
