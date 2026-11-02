// Gunsmith3D - combining two parts' side outlines: merge (union), keep the overlap (intersect), cut one away from the
// other (difference). Done on a fine grid (about a fifth of a millimetre for gun-sized parts), traced back into an
// outline and simplified to fit the corner limit. Robust for any shapes, at the cost of exact corners: the result is
// within a grid step or so of the true shape.
#include "design.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace {
std::vector<char> fill(const std::vector<V2>& poly, double x0, double y0, double r, int W, int H) {
    std::vector<char> g((size_t)W * H, 0);
    std::vector<double> xs;
    for (int j = 0; j < H; j++) {
        double y = y0 + (j + 0.5) * r;
        xs.clear();
        for (size_t i = 0; i < poly.size(); i++) {
            V2 a = poly[i], b = poly[(i + 1) % poly.size()];
            if ((a.y <= y) != (b.y <= y)) xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int i0 = std::max(0, (int)std::ceil((xs[k] - x0) / r - 0.5)), i1 = std::min(W - 1, (int)std::floor((xs[k + 1] - x0) / r - 0.5));
            for (int i = i0; i <= i1; i++) g[(size_t)j * W + i] = 1;
        }
    }
    return g;
}
double area(const std::vector<V2>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); i++) { const V2 &u = p[i], &v = p[(i + 1) % p.size()]; a += u.x * v.y - v.x * u.y; }
    return a / 2;
}
double segDist(V2 p, V2 a, V2 b) {
    double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0, 1.0) : 0;
    return std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t);
}
void dp(const std::vector<V2>& p, int i0, int i1, double tol, std::vector<char>& keep) {
    double best = -1; int bi = -1;
    for (int i = i0 + 1; i < i1; i++) { double d = segDist(p[i], p[i0], p[i1]); if (d > best) { best = d; bi = i; } }
    if (bi >= 0 && best > tol) { keep[bi] = 1; dp(p, i0, bi, tol, keep); dp(p, bi, i1, tol, keep); }
}
// Douglas-Peucker on a closed loop, keeping the forced points (true corners) and simplifying between them
std::vector<V2> simplifyLoop(const std::vector<V2>& p, double tol, const std::vector<char>& force) {
    int n = (int)p.size();
    if (n <= 4) return p;
    std::vector<int> anchors;
    for (int i = 0; i < n; i++) if (force[i]) anchors.push_back(i);
    if (anchors.size() < 2) {   // none to go by: split at the point farthest from the first
        int b = 0; double far = -1;
        for (int i = 0; i < n; i++) { double d = std::hypot(p[i].x - p[0].x, p[i].y - p[0].y); if (d > far) { far = d; b = i; } }
        anchors = {0, b};
        if (b == 0) return p;
    }
    // walk from the first anchor round the loop
    int s0 = anchors[0];
    std::vector<V2> q; std::vector<char> keep;
    for (int k = 0; k <= n; k++) { int i = (s0 + k) % n; q.push_back(p[i]); keep.push_back(k == 0 || k == n || force[i] || (anchors.size() == 2 && i == anchors[1])); }
    int last = 0;
    for (int k = 1; k <= n; k++) if (keep[k]) { dp(q, last, k, tol, keep); last = k; }
    std::vector<V2> out;
    for (int k = 0; k < n; k++) if (keep[k]) out.push_back(q[k]);
    return out;
}
}  // namespace

bool combineOutlines(const BodyDef& A, const BodyDef& B, int op, std::vector<V2>& out, std::string* why, std::vector<double>* radOut) {
    // Rounded corners are worked as sharp ones and handed back their rounding where they survive; curved edges (bulges)
    // are worked from the curve itself and come back as short straight pieces.
    auto bulged = [](const BodyDef& b) { for (double q : b.bulge) if (std::fabs(q) > 0.01) return true; return false; };
    bool sharpA = radOut && !bulged(A), sharpB = radOut && !bulged(B);   // worked from the corners themselves
    std::vector<V2> pa = sharpA ? A.pts : outlineOf(A), pb = sharpB ? B.pts : outlineOf(B);
    if (pa.size() < 3 || pb.size() < 3) { if (why) *why = "a part has no shape"; return false; }
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (auto* P : {&pa, &pb}) for (auto& p : *P) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    double r = std::max(0.05, std::max(x1 - x0, y1 - y0) / 2500);
    x0 -= 2 * r; y0 -= 2 * r;
    int W = (int)std::ceil((x1 - x0) / r) + 3, H = (int)std::ceil((y1 - y0) / r) + 3;
    auto ga = fill(pa, x0, y0, r, W, H), gb = fill(pb, x0, y0, r, W, H);
    std::vector<char> g((size_t)W * H);
    size_t both = 0, inA = 0;
    for (size_t k = 0; k < g.size(); k++) {
        both += ga[k] && gb[k]; inA += ga[k];
        g[k] = op == 0 ? (ga[k] || gb[k]) : op == 1 ? (ga[k] && gb[k]) : (ga[k] && !gb[k]);
    }
    bool touch = both > 0;   // or edge to edge: a cell of one next to a cell of the other
    for (int j = 0; j < H && !touch; j++)
        for (int i = 0; i + 1 < W && !touch; i++) {
            size_t k = (size_t)j * W + i;
            if ((ga[k] && gb[k + 1]) || (gb[k] && ga[k + 1])) touch = true;
            if (j + 1 < H && ((ga[k] && gb[k + W]) || (gb[k] && ga[k + W]))) touch = true;
        }
    if (op == 0 && !touch) { if (why) *why = "they don't touch"; return false; }
    if (op == 1 && both == 0) { if (why) *why = "they don't overlap"; return false; }
    if (op == 2 && both == 0) { if (why) *why = "the other part doesn't overlap it"; return false; }
    auto at = [&](int i, int j) { return i >= 0 && j >= 0 && i < W && j < H && g[(size_t)j * W + i]; };
    // the boundary between filled and empty cells, as directed edges round the filled ones (filled on the left),
    // corners on the grid; traced into loops, turning left first where two loops touch at a corner
    std::map<long long, std::vector<int>> from;
    struct E { int ax, ay, bx, by; bool used = false; };
    std::vector<E> es;
    auto key = [&](int x, int y) { return (long long)y * (W + 1) + x; };
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            if (!at(i, j)) continue;
            // y grows downward here; counter-clockwise on screen with the cell on the left
            if (!at(i, j - 1)) es.push_back({i + 1, j, i, j});
            if (!at(i - 1, j)) es.push_back({i, j, i, j + 1});
            if (!at(i, j + 1)) es.push_back({i, j + 1, i + 1, j + 1});
            if (!at(i + 1, j)) es.push_back({i + 1, j + 1, i + 1, j});
        }
    for (int k = 0; k < (int)es.size(); k++) from[key(es[k].ax, es[k].ay)].push_back(k);
    std::vector<std::vector<V2>> loops;
    for (int s = 0; s < (int)es.size(); s++) {
        if (es[s].used) continue;
        std::vector<V2> loop;
        int e = s;
        while (e >= 0 && !es[e].used) {
            es[e].used = true;
            loop.push_back({x0 + es[e].ax * r, y0 + es[e].ay * r});
            int dx = es[e].bx - es[e].ax, dy = es[e].by - es[e].ay, next = -1, bestTurn = 9;
            for (int c : from[key(es[e].bx, es[e].by)]) {
                if (es[c].used) continue;
                int cx = es[c].bx - es[c].ax, cy = es[c].by - es[c].ay;
                int cross = dx * cy - dy * cx, dot = dx * cx + dy * cy;
                int turn = cross < 0 ? 0 : dot > 0 ? 1 : 2;   // left first (y down: a negative cross is a left turn)
                if (turn < bestTurn) { bestTurn = turn; next = c; }
            }
            e = next;
        }
        if (loop.size() >= 4) loops.push_back(loop);
    }
    if (loops.empty()) { if (why) *why = "nothing is left"; return false; }
    // the outside of the biggest piece (holes and smaller pieces can't be one part's outline)
    size_t bi = 0; double ba = 0;
    for (size_t k = 0; k < loops.size(); k++) { double a = std::fabs(area(loops[k])); if (a > ba) { ba = a; bi = k; } }
    std::vector<V2> best = loops[bi];
    int holes = 0, pieces = 0;   // loops wound against the outside are holes in a piece
    for (auto& l : loops) {
        if (std::fabs(area(l)) <= 4 * r * r) continue;
        if (area(l) * area(best) < 0) holes++; else pieces++;
    }
    if (op == 2 && holes && pieces == 1 && loops.size() == (size_t)(1 + holes) && std::fabs(area(best)) > 0.98 * inA * r * r) {
        if (why) *why = "that would only make a hole in the middle, and an outline can't have one: use the Carve tool for holes";
        return false;
    }
    // The grid only finds the shape to within a cell. The true result's corners are the two outlines' own corners and
    // the points where their edges cross, and its edges lie on theirs: snap to those, so a mechanism keeps its exact
    // surfaces (a sear moved by a tenth of a millimetre stops a gun).
    std::vector<V2> cand(pa); cand.insert(cand.end(), pb.begin(), pb.end());
    std::vector<std::pair<V2, V2>> edges;
    for (auto* P : {&pa, &pb}) for (size_t i = 0; i < P->size(); i++) edges.push_back({(*P)[i], (*P)[(i + 1) % P->size()]});
    for (size_t i = 0; i < pa.size(); i++)
        for (size_t k = 0; k < pb.size(); k++) {
            V2 p = pa[i], p2 = pa[(i + 1) % pa.size()], q = pb[k], q2 = pb[(k + 1) % pb.size()];
            double rx = p2.x - p.x, ry = p2.y - p.y, sx = q2.x - q.x, sy = q2.y - q.y, den = rx * sy - ry * sx;
            if (std::fabs(den) < 1e-12) continue;
            double t = ((q.x - p.x) * sy - (q.y - p.y) * sx) / den, u = ((q.x - p.x) * ry - (q.y - p.y) * rx) / den;
            if (t >= -1e-9 && t <= 1 + 1e-9 && u >= -1e-9 && u <= 1 + 1e-9) cand.push_back({p.x + rx * t, p.y + ry * t});
        }
    auto snap = [&](std::vector<V2> s) {
        std::vector<V2> o;
        for (V2 v : s) {
            double bd = 2.6 * r; V2 bv = v; bool hit = false;
            for (V2 c : cand) { double d = std::hypot(c.x - v.x, c.y - v.y); if (d < bd) { bd = d; bv = c; hit = true; } }
            if (!hit) {   // on an edge, not at a corner: onto the nearest true edge
                bd = 2.0 * r;
                for (auto& e : edges) {
                    double dx = e.second.x - e.first.x, dy = e.second.y - e.first.y, l2 = dx * dx + dy * dy;
                    if (l2 < 1e-12) continue;
                    double t = std::clamp(((v.x - e.first.x) * dx + (v.y - e.first.y) * dy) / l2, 0.0, 1.0);
                    V2 pr{e.first.x + dx * t, e.first.y + dy * t};
                    double d = std::hypot(pr.x - v.x, pr.y - v.y);
                    if (d < bd) { bd = d; bv = pr; }
                }
            }
            if (o.empty() || std::hypot(o.back().x - bv.x, o.back().y - bv.y) > 1e-6) o.push_back(bv);
        }
        while (o.size() > 1 && std::hypot(o.front().x - o.back().x, o.front().y - o.back().y) <= 1e-6) o.pop_back();
        for (bool again = true; again && o.size() > 3;) {   // points on a straight line between their neighbours go
            again = false;
            for (size_t i = 0; i < o.size() && o.size() > 3; i++) {
                V2 a = o[(i + o.size() - 1) % o.size()], b = o[i], c = o[(i + 1) % o.size()];
                double cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x), l = std::hypot(c.x - a.x, c.y - a.y);
                if (l < 1e-9 || std::fabs(cr) / l < 1e-4) { o.erase(o.begin() + i); again = true; break; }
            }
        }
        return o;
    };
    // pin the true corners that lie on the traced edge (the grid passes within a cell and a half of them)
    std::vector<char> force(best.size(), 0);
    std::vector<double> fd(best.size(), 1e18);
    for (V2 c : cand) {
        size_t bi2 = 0; double bd = 1e18;
        for (size_t i = 0; i < best.size(); i++) { double d = std::hypot(best[i].x - c.x, best[i].y - c.y); if (d < bd) { bd = d; bi2 = i; } }
        if (bd < 1.5 * r && bd < fd[bi2]) { fd[bi2] = bd; best[bi2] = c; force[bi2] = 1; }
    }
    std::vector<V2> s = snap(simplifyLoop(best, r * 1.2, force));
    if ((int)s.size() > MAX_CORNERS) {   // too many: drop the corners that change the shape least
        BodyDef t; t.pts = s; fixShape(t);
        simplifyBody(t);
        s = t.pts;
    }
    if (s.size() < 3) { if (why) *why = "nothing is left"; return false; }
    if (area(s) * area(pa) < 0) std::reverse(s.begin(), s.end());   // the same way round as the part it replaces
    for (V2 first : pa) {   // and starting from the same corner, where its own corners survive
        auto it = std::find_if(s.begin(), s.end(), [&](V2 v) { return std::hypot(v.x - first.x, v.y - first.y) < 1e-9; });
        if (it != s.end()) { std::rotate(s.begin(), it, s.end()); break; }
    }
    out = s;
    if (radOut) {
        radOut->assign(s.size(), 0.0);
        for (size_t i = 0; i < s.size(); i++)
            for (const BodyDef* P : {&A, &B})
                if (P == &A ? sharpA : sharpB)
                    for (size_t k = 0; k < P->pts.size(); k++)
                        if (k < P->rad.size() && std::hypot(P->pts[k].x - s[i].x, P->pts[k].y - s[i].y) < 1e-6) (*radOut)[i] = std::max((*radOut)[i], P->rad[k]);
    }
    if (why) *why = pieces > 1 ? "it came out in more than one piece: only the biggest is kept"
                  : holes ? "the gap left inside is filled in (an outline can't have a hole: use the Carve tool for holes)" : "";
    return true;
}

int chamferCorners(BodyDef& b, double size, const std::vector<int>& which) {
    fixShape(b);
    size_t n = b.pts.size();
    if (n < 3 || size <= 0) return 0;
    std::vector<char> pick(n, which.empty() ? 1 : 0);
    for (int w : which) if (w >= 0 && w < (int)n) pick[w] = 1;
    int room = MAX_CORNERS - (int)n, done = 0;
    std::vector<V2> pts; std::vector<double> rad, bulge;
    for (size_t i = 0; i < n; i++) {
        V2 p = b.pts[i], a = b.pts[(i + n - 1) % n], q = b.pts[(i + 1) % n];
        double la = std::hypot(a.x - p.x, a.y - p.y), lb = std::hypot(q.x - p.x, q.y - p.y);
        double t = std::min(size, 0.45 * std::min(la, lb));
        double turn = la > 1e-6 && lb > 1e-6 ? ((a.x - p.x) * (q.x - p.x) + (a.y - p.y) * (q.y - p.y)) / (la * lb) : 1;
        if (pick[i] && room > 0 && t > 0.05 && turn > -0.995) {   // a straight-through corner has nothing to cut
            pts.push_back({p.x + (a.x - p.x) / la * t, p.y + (a.y - p.y) / la * t});
            pts.push_back({p.x + (q.x - p.x) / lb * t, p.y + (q.y - p.y) / lb * t});
            rad.push_back(0); rad.push_back(0);
            bulge.push_back(0); bulge.push_back(b.bulge[i]);   // the chamfer itself is straight
            room--; done++;
        } else { pts.push_back(p); rad.push_back(b.rad[i]); bulge.push_back(b.bulge[i]); }
    }
    // bulge[k] belongs to the edge from corner k to k+1: the new short chamfer edge is straight, the edges either side keep theirs
    b.pts = pts; b.rad = rad; b.bulge = bulge;
    return done;
}
