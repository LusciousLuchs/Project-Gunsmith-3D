// Gunsmith3D - round (turned) parts: the side outline spun about its long middle line, like bar stock turned down
// on a lathe. The cross-section is a circle, or a polygon with flats touching the drawn outline (4 sides is the bar
// as drawn, 8 an octagon), turned to any angle, and it can go only part of the way round (a wedge: a lug).
// The display builds the solid; the physics sees it as slabs through its depth (see carve.hpp, slices()).
#include "carve.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace carve {

static const double PI_R = 3.14159265358979323846;
static double rad(double deg) { return deg * PI_R / 180; }

static V2 centroid2(const std::vector<V2>& q) {
    double a = 0, cx = 0, cy = 0;
    for (size_t i = 0; i < q.size(); i++) {
        const V2 &p = q[i], &r = q[(i + 1) % q.size()];
        double c = p.x * r.y - r.x * p.y;
        a += c; cx += (p.x + r.x) * c; cy += (p.y + r.y) * c;
    }
    if (std::fabs(a) < 1e-12) return q.empty() ? V2{} : q[0];
    return {cx / (3 * a), cy / (3 * a)};
}

RoundAxis roundAxis(const BodyDef& b) {
    RoundAxis ax;
    ax.dir = {1, 0};
    std::vector<V2> o = outlineOf(b);
    if (o.size() < 3) return ax;
    ax.o = centroid2(o);
    double sxx = 0, syy = 0, sxy = 0;  // the outline's spread, by edge length: its long direction is the axis
    for (size_t i = 0; i < o.size(); i++) {
        const V2 &p = o[i], &q = o[(i + 1) % o.size()];
        double l = std::hypot(q.x - p.x, q.y - p.y), mx = (p.x + q.x) / 2 - ax.o.x, my = (p.y + q.y) / 2 - ax.o.y;
        sxx += l * mx * mx; syy += l * my * my; sxy += l * mx * my;
    }
    double th = 0.5 * std::atan2(2 * sxy, sxx - syy), deg = th * 180 / PI_R;
    if (std::fabs(deg) < 2) th = 0;                                   // drawn level: keep it exactly level
    else if (std::fabs(std::fabs(deg) - 90) < 2) th = PI_R / 2;
    if (b.roundAxis == 0) th = 0;                                     // level, front to back (most parts)
    else if (b.roundAxis == 1) th = PI_R / 2;                         // upright
    ax.dir = {std::cos(th), std::sin(th)};
    for (auto& p : o) ax.R = std::max(ax.R, std::fabs(ax.dir.x * (p.y - ax.o.y) - ax.dir.y * (p.x - ax.o.x)));
    return ax;
}

// The outline in the axis's own terms: u along it, v across it (v points down the side view when the axis is level).
static std::vector<V2> toUV(const std::vector<V2>& o, const RoundAxis& ax) {
    std::vector<V2> q;
    for (auto& p : o) {
        double dx = p.x - ax.o.x, dy = p.y - ax.o.y;
        q.push_back({ax.dir.x * dx + ax.dir.y * dy, ax.dir.x * dy - ax.dir.y * dx});
    }
    return q;
}
static V2 fromUV(const RoundAxis& ax, double u, double v) {
    return {ax.o.x + ax.dir.x * u - ax.dir.y * v, ax.o.y + ax.dir.y * u + ax.dir.x * v};
}
static double radiusAt(const std::vector<V2>& q, double u) {  // the furthest the outline reaches from the axis, at u
    double r = 0;
    for (size_t i = 0; i < q.size(); i++) {
        const V2 &a = q[i], &b = q[(i + 1) % q.size()];
        if ((a.x <= u && u < b.x) || (b.x <= u && u < a.x)) r = std::max(r, std::fabs(a.y + (u - a.x) * (b.y - a.y) / (b.x - a.x)));
    }
    return r;
}

std::vector<V2> roundProfile(const BodyDef& b, const RoundAxis& ax) {
    std::vector<V2> q = toUV(outlineOf(b), ax);
    std::vector<double> us;
    for (auto& p : q) us.push_back(p.x);
    std::sort(us.begin(), us.end());
    std::vector<double> u2;
    for (double u : us) if (u2.empty() || u - u2.back() > 1e-6) u2.push_back(u);
    std::vector<V2> st;
    const double e = 1e-5;
    auto add = [&](double u, double r) {
        if (st.empty() || std::fabs(st.back().x - u) > 1e-9 || std::fabs(st.back().y - r) > 1e-6) st.push_back({u, r});
    };
    std::function<void(double, double, double, double, int)> fill = [&](double ua, double ra, double ub, double rb, int depth) {
        double um = (ua + ub) / 2, rm = radiusAt(q, um);  // where the radius bends between corners, add stations
        if (depth < 7 && ub - ua > 0.2 && std::fabs(rm - (ra + rb) / 2) > 0.01) {
            fill(ua, ra, um, rm, depth + 1); add(um, rm); fill(um, rm, ub, rb, depth + 1);
        }
    };
    for (size_t k = 0; k < u2.size(); k++) {
        double u = u2[k];
        double before = k == 0 ? 0 : radiusAt(q, u - e), after = k + 1 == u2.size() ? 0 : radiusAt(q, u + e);
        if (k > 0) { V2 last = st.back(); fill(last.x, last.y, u, before, 0); add(u, before); }
        if (k + 1 < u2.size()) add(u, after);
    }
    return st;
}

std::vector<V2> roundShape(const BodyDef& b, int segs, std::vector<char>* smooth) {
    bool poly = b.roundSides >= 3;
    int N = poly ? b.roundSides : std::max(8, segs);
    double turn = b.roundTurn, sweep = std::clamp(b.roundSweep, 1.0, 360.0), step = 360.0 / N;
    // distance to the edge at angle a (deg, from up toward the near side): flats sit at 1 from the axis
    auto rho = [&](double a) {
        if (!poly) return 1.0;
        double d = std::remainder(a - turn, step);  // from the nearest flat's middle
        return 1 / std::cos(rad(d));
    };
    std::vector<V2> o;
    std::vector<char> sm;
    auto put = [&](double a, bool s) { double r = rho(a); o.push_back({r * std::cos(rad(a)), r * std::sin(rad(a))}); sm.push_back(s); };
    double first = poly ? turn + step / 2 : turn;  // polygons: corners between the flats
    if (sweep >= 359.999) {
        for (int k = 0; k < N; k++) put(first + k * step, !poly);
    } else {
        double a0 = turn - sweep / 2, a1 = turn + sweep / 2;
        o.push_back({0, 0}); sm.push_back(0);  // the axis: a wedge comes to a point there
        put(a0, !poly);
        double k0 = std::ceil((a0 - first) / step + 1e-9);
        for (double a = first + k0 * step; a < a1 - 1e-9; a += step) put(a, !poly);
        put(a1, !poly);
    }
    if (smooth) *smooth = sm;
    return o;
}

void syncRound(BodyDef& b) {
    if (!b.round) return;
    double zmax = 0;
    for (auto& p : roundShape(b, 64)) zmax = std::max(zmax, std::fabs(p.y));
    double d = std::round(roundAxis(b).R * zmax * 2 * 100) / 100;
    if (d > 0.1) b.thick = d;
}

// Where a line at depth z (unit shape) crosses the cross-section: spans in "up", low to high.
static std::vector<V2> chords(const std::vector<V2>& S, double z) {
    std::vector<double> xs;
    for (size_t i = 0; i < S.size(); i++) {
        const V2 &a = S[i], &c = S[(i + 1) % S.size()];
        if ((a.y <= z && z < c.y) || (c.y <= z && z < a.y)) xs.push_back(a.x + (z - a.y) * (c.x - a.x) / (c.y - a.y));
    }
    std::sort(xs.begin(), xs.end());
    std::vector<V2> out;
    for (size_t i = 0; i + 1 < xs.size(); i += 2) if (xs[i + 1] - xs[i] > 1e-9) out.push_back({xs[i], xs[i + 1]});
    return out;
}

std::vector<std::vector<V2>> roundSection(const BodyDef& b, double dz) {
    RoundAxis ax = roundAxis(b);
    std::vector<V2> st = roundProfile(b, ax), S = roundShape(b, 48);
    // the spans across the axis (in v) the part has at station radius r, at this depth
    auto spans = [&](double r) {
        std::vector<V2> o;
        if (r < 1e-6) return o;
        for (auto& c : chords(S, dz / r)) {
            double lo = -c.y * r, hi = -c.x * r;  // up is -v
            if (hi - lo > 0.04) o.push_back({lo, hi});
        }
        std::sort(o.begin(), o.end(), [](V2 p, V2 q) { return p.x < q.x; });
        return o;
    };
    std::vector<std::vector<V2>> out;
    struct Run { std::vector<V2> top, bot; };  // (u, v) along the low and high edge of one span
    std::vector<Run> runs;
    auto flush = [&]() {
        for (auto& rn : runs) {
            if (rn.top.size() < 2) continue;
            std::vector<V2> poly;
            for (auto& p : rn.top) poly.push_back(fromUV(ax, p.x, p.y));
            for (size_t i = rn.bot.size(); i-- > 0;) poly.push_back(fromUV(ax, rn.bot[i].x, rn.bot[i].y));
            std::vector<V2> clean;  // drop corners on a straight line, and repeats
            for (size_t i = 0; i < poly.size(); i++) {
                const V2& c = poly[i];
                const V2& p = clean.empty() ? poly[(i + poly.size() - 1) % poly.size()] : clean.back();
                const V2& n = poly[(i + 1) % poly.size()];
                if (std::hypot(c.x - p.x, c.y - p.y) < 1e-6) continue;
                double cr = (c.x - p.x) * (n.y - c.y) - (c.y - p.y) * (n.x - c.x), l = std::hypot(n.x - p.x, n.y - p.y);
                if (l < 1e-9 || std::fabs(cr) > 0.005 * l) clean.push_back(c);
            }
            if (clean.size() >= 3) out.push_back(clean);
        }
        runs.clear();
    };
    auto extend = [&](double u, const std::vector<V2>& sp) {
        if (runs.size() != sp.size()) { flush(); runs.resize(sp.size()); }
        for (size_t k = 0; k < sp.size(); k++) { runs[k].top.push_back({u, sp[k].x}); runs[k].bot.push_back({u, sp[k].y}); }
    };
    // walk along the part; where the number of spans changes, find the place and start new pieces there
    std::function<void(double, double, const std::vector<V2>&, double, double, const std::vector<V2>&)> walk =
        [&](double ua, double ra, const std::vector<V2>& A, double ub, double rb, const std::vector<V2>& B) {
            if (A.size() == B.size()) { extend(ub, B); return; }
            if (ub - ua < 0.02) { flush(); if (!B.empty()) extend(ub, B); return; }
            double um = (ua + ub) / 2, rm = (ra + rb) / 2;
            std::vector<V2> M = spans(rm);
            walk(ua, ra, A, um, rm, M);
            walk(um, rm, M, ub, rb, B);
        };
    if (st.empty()) return out;
    std::vector<V2> prev = spans(st[0].y);
    if (!prev.empty()) extend(st[0].x, prev);
    for (size_t i = 1; i < st.size(); i++) {
        std::vector<V2> cur = spans(st[i].y);
        walk(st[i - 1].x, st[i - 1].y, prev, st[i].x, st[i].y, cur);
        prev = cur;
    }
    flush();
    return out;
}

}  // namespace carve
