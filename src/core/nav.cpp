// Gunsmith3D - Path-finding for NPCs (see nav.hpp).
#include "nav.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace nav {
using gmap::V3;

namespace {
constexpr int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};

struct BrushInfo {
    const gmap::Brush* b;
    V3 mn, mx;
};

// The brushes sorted into 2 m squares seen from above, so a spot only looks at the brushes near it
struct Bins {
    double x0 = 0, z0 = 0, size = 2;
    int nx = 1, nz = 1;
    std::vector<std::vector<int>> in;
    std::vector<BrushInfo> br;
    mutable std::vector<int> stamp;
    mutable int tick = 0;
    void build(const gmap::Map& m, double X0, double Z0, double X1, double Z1) {
        x0 = X0; z0 = Z0;
        nx = std::max(1, (int)std::ceil((X1 - X0) / size));
        nz = std::max(1, (int)std::ceil((Z1 - Z0) / size));
        in.assign((size_t)nx * nz, {});
        for (auto& b : m.brushes) {
            BrushInfo bi{&b, {}, {}};
            gmap::brushBounds(b, bi.mn, bi.mx);
            if (bi.mn.x > bi.mx.x) continue;   // (nothing left of it)
            int id = (int)br.size();
            br.push_back(bi);
            int ax = std::clamp((int)std::floor((bi.mn.x - x0) / size), 0, nx - 1), bx = std::clamp((int)std::floor((bi.mx.x - x0) / size), 0, nx - 1);
            int az = std::clamp((int)std::floor((bi.mn.z - z0) / size), 0, nz - 1), bz = std::clamp((int)std::floor((bi.mx.z - z0) / size), 0, nz - 1);
            for (int i = ax; i <= bx; i++)
                for (int k = az; k <= bz; k++) in[(size_t)i + (size_t)k * nx].push_back(id);
        }
        stamp.assign(br.size(), 0);
    }
    // the brushes whose squares overlap [x0..x1] x [z0..z1]
    void near(double ax0, double az0, double ax1, double az1, std::vector<int>& out) const {
        out.clear();
        tick++;
        int ax = std::clamp((int)std::floor((ax0 - x0) / size), 0, nx - 1), bx = std::clamp((int)std::floor((ax1 - x0) / size), 0, nx - 1);
        int az = std::clamp((int)std::floor((az0 - z0) / size), 0, nz - 1), bz = std::clamp((int)std::floor((az1 - z0) / size), 0, nz - 1);
        for (int i = ax; i <= bx; i++)
            for (int k = az; k <= bz; k++)
                for (int id : in[(size_t)i + (size_t)k * nx])
                    if (stamp[id] != tick) { stamp[id] = tick; out.push_back(id); }
    }
};

// Where an upright line at (x, z) passes through a brush: from `bot` up to `top`; `ny`: how flat its top is there
bool columnSpan(const gmap::Brush& b, double x, double z, double& bot, double& top, double& ny) {
    bot = -1e30; top = 1e30; ny = 0;
    for (auto& f : b.faces) {
        double rhs = f.d - f.n.x * x - f.n.z * z;
        if (std::fabs(f.n.y) < 1e-9) { if (rhs < 0) return false; continue; }
        double t = rhs / f.n.y;
        if (f.n.y > 0) { if (t < top) { top = t; ny = f.n.y; } }
        else bot = std::max(bot, t);
    }
    return top > bot + 1e-6 && top < 1e29;
}

// Is a box (centre, half extents) inside any of these brushes? (the same test as gmap::boxFree, bevels included)
bool boxHits(const Bins& B, const std::vector<int>& ids, V3 c, V3 h) {
    for (int id : ids) {
        const BrushInfo& bi = B.br[id];
        if (c.x - h.x > bi.mx.x - 1e-4 || c.x + h.x < bi.mn.x + 1e-4 || c.y - h.y > bi.mx.y - 1e-4 || c.y + h.y < bi.mn.y + 1e-4 ||
            c.z - h.z > bi.mx.z - 1e-4 || c.z + h.z < bi.mn.z + 1e-4)
            continue;
        bool inside = true;
        for (auto& f : bi.b->faces)
            if (gmap::dot(f.n, c) - (f.d + std::fabs(f.n.x) * h.x + std::fabs(f.n.y) * h.y + std::fabs(f.n.z) * h.z) > -1e-4) { inside = false; break; }
        if (inside) return true;
    }
    return false;
}

double flatDist(V3 a, V3 b) { return std::hypot(a.x - b.x, a.z - b.z); }
int colOf(const Grid& g, double x, double z) {
    int ix = (int)std::floor((x - g.x0) / g.cell), iz = (int)std::floor((z - g.z0) / g.cell);
    if (ix < 0 || iz < 0 || ix >= g.nx || iz >= g.nz) return -1;
    return ix + iz * g.nx;
}
// the node in column c a body at height y walks onto (no more than a step up or down), or -1
int walkOnto(const Grid& g, int c, double y) {
    if (c < 0) return -1;
    int best = -1; double bd = g.agent.step + 0.02;
    for (int n = g.colStart[c]; n < g.colStart[c + 1]; n++) {
        double d = std::fabs(g.y[n] - y);
        if (d <= bd) { bd = d; best = n; }
    }
    return best;
}
}  // namespace

V3 Grid::at(int n) const {
    int c = col[n];
    return {x0 + (c % nx + 0.5) * cell, (double)y[n], z0 + (c / nx + 0.5) * cell};
}

Grid build(const gmap::Map& m, const Agent& a) {
    Grid g;
    g.agent = a;
    double X0 = 1e30, Z0 = 1e30, X1 = -1e30, Z1 = -1e30;
    for (auto& b : m.brushes) {
        V3 mn, mx;
        gmap::brushBounds(b, mn, mx);
        if (mn.x > mx.x) continue;
        X0 = std::min(X0, mn.x); Z0 = std::min(Z0, mn.z); X1 = std::max(X1, mx.x); Z1 = std::max(Z1, mx.z);
    }
    if (X0 > X1) { g.colStart = {0}; return g; }
    double area = (X1 - X0) * (Z1 - Z0);
    g.cell = std::max(0.25, std::sqrt(area / 400000.0));
    g.x0 = X0; g.z0 = Z0;
    g.nx = std::max(1, (int)std::ceil((X1 - X0) / g.cell));
    g.nz = std::max(1, (int)std::ceil((Z1 - Z0) / g.cell));
    Bins B;
    B.build(m, X0, Z0, X1, Z1);

    // the floors in every column that the body fits on
    // the body standing there (straight walks keep a spot away from walls: see straightWalk)
    double lift = a.step - 0.02;   // (it stands over anything lower than a step: it would step onto it)
    double w = a.halfWidth;
    V3 half{w, std::max(0.05, (a.height - lift) / 2), w};
    std::vector<int> ids, near;
    struct Span { double bot, top, ny; };
    std::vector<Span> spans;
    std::vector<double> floors;
    g.colStart.assign((size_t)g.nx * g.nz + 1, 0);
    for (int iz = 0; iz < g.nz; iz++)
        for (int ix = 0; ix < g.nx; ix++) {
            int c = ix + iz * g.nx;
            g.colStart[c] = (int)g.y.size();
            double cx = g.x0 + (ix + 0.5) * g.cell, cz = g.z0 + (iz + 0.5) * g.cell;
            B.near(cx, cz, cx, cz, ids);
            spans.clear();
            for (int id : ids) {
                Span s;
                if (columnSpan(*B.br[id].b, cx, cz, s.bot, s.top, s.ny)) spans.push_back(s);
            }
            floors.clear();
            for (auto& s : spans) {
                if (s.ny < 0.7) continue;   // too steep to stand on
                bool buried = false;
                for (auto& o : spans)
                    if (o.bot < s.top + 1e-3 && o.top > s.top + 1e-3) { buried = true; break; }   // (a block standing on it covers it too)
                if (buried) continue;
                bool dup = false;
                for (double f : floors) if (std::fabs(f - s.top) < 0.03) dup = true;
                if (dup) continue;
                V3 body{cx, s.top + lift + half.y, cz};
                B.near(cx - half.x, cz - half.z, cx + half.x, cz + half.z, near);
                if (boxHits(B, near, body, half)) continue;
                floors.push_back(s.top);
            }
            std::sort(floors.begin(), floors.end());
            for (double f : floors) { g.y.push_back((float)f); g.col.push_back(c); }
        }
    g.colStart[(size_t)g.nx * g.nz] = (int)g.y.size();

    // what each floor leads to
    double maxDrop = std::clamp(a.height * 1.5, 1.2, 4.0);
    g.link.assign(g.y.size() * 8, -1);
    for (int n = 0; n < (int)g.y.size(); n++) {
        int c = g.col[n], ix = c % g.nx, iz = c / g.nx;
        double y = g.y[n];
        for (int d = 0; d < 8; d++) {
            int jx = ix + DX[d], jz = iz + DZ[d];
            if (jx < 0 || jz < 0 || jx >= g.nx || jz >= g.nz) continue;
            int c2 = jx + jz * g.nx;
            int to = walkOnto(g, c2, y);
            if (to >= 0 && d >= 4) {   // across a corner: only if both sides are open (no cutting round a wall's end)
                if (walkOnto(g, ix + DX[d] + iz * g.nx, y) < 0 || walkOnto(g, ix + (iz + DZ[d]) * g.nx, y) < 0) to = -1;
            }
            if (to < 0 && d < 4) {
                // off a ledge: out over the edge (the body fits there at this height), then the floor below, if it
                // isn't too far down (it lands a little way out: next to the ledge's side, not in it)
                int reachOut = 1 + (int)std::ceil(2 * w / g.cell);
                for (int k = 1; k <= reachOut && to < 0; k++) {
                    int kx = ix + DX[d] * k, kz = iz + DZ[d] * k;
                    if (kx < 0 || kz < 0 || kx >= g.nx || kz >= g.nz) break;
                    int ck = kx + kz * g.nx;
                    double cx = g.x0 + (kx + 0.5) * g.cell, cz = g.z0 + (kz + 0.5) * g.cell;
                    V3 body{cx, y + lift + half.y, cz};
                    B.near(cx - half.x, cz - half.z, cx + half.x, cz + half.z, near);
                    if (boxHits(B, near, body, half)) break;   // something in the way out there
                    if (walkOnto(g, ck, y) >= 0) break;          // (floor at this height: that's walking, not a drop)
                    for (int q = g.colStart[ck + 1] - 1; q >= g.colStart[ck]; q--) {
                        double dy = y - g.y[q];
                        if (dy <= a.step + 0.02) continue;
                        if (dy <= maxDrop) to = q;
                        break;
                    }
                }
            }
            g.link[(size_t)n * 8 + d] = to;
        }
    }
    return g;
}

int nearestNode(const Grid& g, V3 p, double reach) {
    if (g.nodes() == 0) return -1;
    int r = std::max(1, (int)std::ceil(reach / g.cell));
    int cx = (int)std::floor((p.x - g.x0) / g.cell), cz = (int)std::floor((p.z - g.z0) / g.cell);
    int best = -1; double bs = 1e30;
    for (int iz = cz - r; iz <= cz + r; iz++)
        for (int ix = cx - r; ix <= cx + r; ix++) {
            if (ix < 0 || iz < 0 || ix >= g.nx || iz >= g.nz) continue;
            int c = ix + iz * g.nx;
            for (int n = g.colStart[c]; n < g.colStart[c + 1]; n++) {
                double dy = p.y - g.y[n];
                if (dy < -(g.agent.step + 0.1) || dy > 1.2) continue;   // (above its head, or too far below its feet)
                V3 q = g.at(n);
                double h = flatDist(p, q);
                if (h > reach + g.cell * 0.75) continue;
                double s = h + std::fabs(dy) * 2;
                if (s < bs) { bs = s; best = n; }
            }
        }
    return best;
}

bool straightWalk(const Grid& g, V3 a, V3 b) {
    int n0 = nearestNode(g, a, g.cell);
    if (n0 < 0) return false;
    double cur = g.y[n0];
    double L = flatDist(a, b);
    int steps = std::max(1, (int)std::ceil(L / (g.cell * 0.5)));
    int prev = g.col[n0];
    for (int s = 1; s <= steps; s++) {
        double t = (double)s / steps;
        int c = colOf(g, a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t);
        if (c < 0) return false;
        if (c == prev) continue;
        int n = walkOnto(g, c, cur);
        if (n < 0) return false;
        int px = prev % g.nx, pz = prev / g.nx, qx = c % g.nx, qz = c / g.nx;
        // a spot with walkable floor all round it: the line keeps a spot's width off any wall, so the body doesn't
        // catch a corner between spots
        for (int d = 0; d < 4; d++) {
            int ax = qx + DX[d], az = qz + DZ[d];
            if (ax < 0 || az < 0 || ax >= g.nx || az >= g.nz || walkOnto(g, ax + az * g.nx, g.y[n]) < 0) return false;
        }
        if (px != qx && pz != qz)   // crossed a corner: both sides must be open
            if (walkOnto(g, qx + pz * g.nx, cur) < 0 || walkOnto(g, px + qz * g.nx, cur) < 0) return false;
        cur = g.y[n];
        prev = c;
    }
    return true;
}

bool findPath(const Grid& g, V3 from, V3 to, std::vector<V3>& out, bool* reached) {
    out.clear();
    if (reached) *reached = false;
    int start = nearestNode(g, from, 0.6 + g.cell);
    if (start < 0) return false;
    int goal = nearestNode(g, to, 0.4 + g.cell);
    size_t N = g.nodes();
    std::vector<float> cost(N, 1e30f);
    std::vector<int> from_(N, -1);
    std::vector<char> done(N, 0);
    auto h = [&](int n) { V3 p = g.at(n); return std::sqrt((p.x - to.x) * (p.x - to.x) + (p.z - to.z) * (p.z - to.z) + (p.y - to.y) * (p.y - to.y)); };
    using QE = std::pair<double, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    cost[start] = 0;
    open.push({h(start), start});
    int best = start; double bestH = h(start);
    int expanded = 0;
    while (!open.empty() && expanded < 120000) {
        int n = open.top().second;
        open.pop();
        if (done[n]) continue;
        done[n] = 1;
        expanded++;
        double hn = h(n);
        if (hn < bestH) { bestH = hn; best = n; }
        if (n == goal) { best = n; break; }
        V3 p = g.at(n);
        for (int d = 0; d < 8; d++) {
            int m = g.link[(size_t)n * 8 + d];
            if (m < 0 || done[m]) continue;
            V3 q = g.at(m);
            double step = flatDist(p, q) + std::fabs(q.y - p.y) * (q.y < p.y - g.agent.step ? 0.5 : 1.0);
            double c = cost[n] + step;
            if (c < cost[m]) { cost[m] = (float)c; from_[m] = n; open.push({c + h(m), m}); }
        }
    }
    std::vector<V3> pts;
    for (int n = best; n >= 0; n = from_[n]) pts.push_back(g.at(n));
    std::reverse(pts.begin(), pts.end());
    bool got = best == goal && goal >= 0;
    if (reached) *reached = got;
    pts[0] = V3{from.x, pts[0].y, from.z};
    if (got) pts.back() = V3{to.x, pts.back().y, to.z};
    // pull it straight: from each point, on to the furthest one it can walk to in a straight line
    out.push_back(pts[0]);
    size_t i = 0;
    while (i + 1 < pts.size()) {
        size_t j = i + 1;
        while (j + 1 < pts.size() && straightWalk(g, pts[i], pts[j + 1])) j++;
        out.push_back(pts[j]);
        i = j;
    }
    return true;
}

const Grid& Cache::get(const gmap::Map& m, Agent a) {
    if (map != &m || brushes != m.brushes.size()) { grids.clear(); map = &m; brushes = m.brushes.size(); }
    a.halfWidth = std::ceil(a.halfWidth / 0.05 - 1e-6) * 0.05;
    a.height = std::ceil(a.height / 0.1 - 1e-6) * 0.1;
    a.step = std::max(0.1, std::floor(a.step / 0.05 + 1e-6) * 0.05);
    for (auto& [k, gp] : grids)
        if (std::fabs(k.halfWidth - a.halfWidth) < 1e-6 && std::fabs(k.height - a.height) < 1e-6 && std::fabs(k.step - a.step) < 1e-6) return *gp;
    grids.push_back({a, std::make_shared<Grid>(build(m, a))});
    return *grids.back().second;
}

}  // namespace nav
