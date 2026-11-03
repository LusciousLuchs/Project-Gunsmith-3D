// Gunsmith3D - parts shaped in depth (see BodyDef::depthOff): every corner a point on the near face and one on the far
// face, flat pieces between. The display builds its solid from depthTriangles; the physics cuts it into slabs through
// its depth with depthSection, the way it does carved and tilted parts.
#include "design.hpp"
#include <algorithm>
#include <cmath>
#include <map>

bool depthShaped(const BodyDef& b) {
    size_t n = b.pts.size();
    return n >= 3 && b.depthOff.size() == n && b.depthZn.size() == n && b.depthZf.size() == n;
}

void normalizeDepth(BodyDef& b) {
    size_t n = b.pts.size();
    if (b.depthOff.empty() && b.depthZn.empty() && b.depthZf.empty()) { b.depthRef.clear(); return; }
    auto clear = [&]() { b.depthOff.clear(); b.depthZn.clear(); b.depthZf.clear(); b.depthRef.clear(); };
    size_t m = b.depthOff.size();
    if (b.depthZn.size() != m || b.depthZf.size() != m) { clear(); return; }
    if (m != n) {
        // corners were added or removed: each new corner takes the shape of where it lies on the old outline
        if (b.depthRef.size() != m || m < 2) { clear(); return; }
        std::vector<V2> off(n); std::vector<double> zn(n), zf(n);
        for (size_t i = 0; i < n; i++) {
            V2 p = b.pts[i];
            double best = 1e18; size_t be = 0; double bt = 0;
            for (size_t e = 0; e < m; e++) {
                V2 a = b.depthRef[e], c = b.depthRef[(e + 1) % m];
                double dx = c.x - a.x, dy = c.y - a.y, l2 = dx * dx + dy * dy;
                double t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0, 1.0) : 0;
                double d = std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t);
                if (d < best) { best = d; be = e; bt = t; }
            }
            size_t e2 = (be + 1) % m;
            off[i] = {b.depthOff[be].x + (b.depthOff[e2].x - b.depthOff[be].x) * bt, b.depthOff[be].y + (b.depthOff[e2].y - b.depthOff[be].y) * bt};
            zn[i] = b.depthZn[be] + (b.depthZn[e2] - b.depthZn[be]) * bt;
            zf[i] = b.depthZf[be] + (b.depthZf[e2] - b.depthZf[be]) * bt;
        }
        b.depthOff = off; b.depthZn = zn; b.depthZf = zf;
    }
    bool any = false;   // nothing left to shape: straight through again
    for (size_t i = 0; i < n; i++)
        if (std::hypot(b.depthOff[i].x, b.depthOff[i].y) > 1e-6 || std::fabs(b.depthZn[i]) > 1e-6 || std::fabs(b.depthZf[i]) > 1e-6) any = true;
    if (!any) { clear(); return; }
    b.depthRef = b.pts;
}

void depthRings(const BodyDef& b, std::vector<P3d>& nearR, std::vector<P3d>& farR) {
    nearR.clear(); farR.clear();
    double t = std::max(0.5, b.thick) / 2;
    bool s = depthShaped(b);
    for (size_t i = 0; i < b.pts.size(); i++) {
        V2 p = b.pts[i], o = s ? b.depthOff[i] : V2{0, 0};
        nearR.push_back({p.x, p.y, b.z + t + (s ? b.depthZn[i] : 0)});
        farR.push_back({p.x + o.x, p.y + o.y, b.z - t + (s ? b.depthZf[i] : 0)});
    }
}

void depthSpan(const BodyDef& b, double& z0, double& z1) {
    std::vector<P3d> N, F;
    depthRings(b, N, F);
    z0 = 1e18; z1 = -1e18;
    for (auto* R : {&N, &F}) for (auto& p : *R) { z0 = std::min(z0, p.z); z1 = std::max(z1, p.z); }
    if (z0 > z1) { z0 = b.z - b.thick / 2; z1 = b.z + b.thick / 2; }
}

namespace {
bool inTri(V2 p, V2 a, V2 b, V2 c) {
    auto s = [](V2 p1, V2 p2, V2 p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); };
    double d1 = s(p, a, b), d2 = s(p, b, c), d3 = s(p, c, a);
    return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0));
}
double area2(const std::vector<V2>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); i++) { const V2 &u = p[i], &v = p[(i + 1) % p.size()]; a += u.x * v.y - v.x * u.y; }
    return a;
}
}  // namespace

std::vector<int> triangulateOutline(const std::vector<V2>& p0) {
    std::vector<V2> p = p0;
    bool flip = area2(p) < 0;   // worked counter-clockwise, handed back in the outline's own order
    if (flip) std::reverse(p.begin(), p.end());
    int n = (int)p.size();
    std::vector<int> idx(n), out;
    for (int i = 0; i < n; i++) idx[i] = i;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 20000) {
        bool cut = false;
        for (size_t i = 0; i < idx.size(); i++) {
            int i0 = idx[(i + idx.size() - 1) % idx.size()], i1 = idx[i], i2 = idx[(i + 1) % idx.size()];
            V2 a = p[i0], b = p[i1], c = p[i2];
            if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 1e-12) continue;
            bool inside = false;
            for (int k : idx) if (k != i0 && k != i1 && k != i2 && inTri(p[k], a, b, c)) { inside = true; break; }
            if (inside) continue;
            out.insert(out.end(), {i0, i1, i2});
            idx.erase(idx.begin() + i);
            cut = true;
            break;
        }
        if (!cut) break;
    }
    if (idx.size() == 3) out.insert(out.end(), {idx[0], idx[1], idx[2]});
    else for (size_t i = 1; i + 1 < idx.size(); i++) out.insert(out.end(), {idx[0], idx[i], idx[i + 1]});
    if (flip) for (auto& k : out) k = n - 1 - k;   // back to the original numbering
    return out;
}

void depthTriangles(const BodyDef& b, std::vector<P3d>& v, std::vector<int>& tri, std::vector<int>* tag) {
    std::vector<P3d> N, F;
    depthRings(b, N, F);
    int n = (int)N.size();
    v = N; v.insert(v.end(), F.begin(), F.end());
    tri.clear();
    if (tag) tag->clear();
    if (n < 3) return;
    // wound outward in these very coordinates (x, y, z as stored): the cap triangles come out counter-clockwise in x-y,
    // which faces +z; the side walls follow the outline's own direction
    std::vector<V2> pn;
    for (auto& p : N) pn.push_back({p.x, p.y});
    bool ccw = area2(pn) > 0;
    auto cap = triangulateOutline(pn);
    for (size_t t = 0; t < cap.size(); t += 3) {
        int a = cap[t], c1 = cap[t + 1], c2 = cap[t + 2];
        tri.insert(tri.end(), {a, c1, c2});            // near face, out toward +z
        if (tag) tag->push_back(-1);
        tri.insert(tri.end(), {n + a, n + c2, n + c1}); // far face, out toward -z
        if (tag) tag->push_back(-2);
    }
    for (int i = 0; i < n; i++) {   // the side walls: two triangles each, between the corners of edge i on both faces
        int j = (i + 1) % n;
        int a = i, b2 = j, c = n + j, d = n + i;
        if (ccw) { tri.insert(tri.end(), {a, d, c}); tri.insert(tri.end(), {a, c, b2}); }
        else { tri.insert(tri.end(), {a, b2, c}); tri.insert(tri.end(), {a, c, d}); }
        if (tag) { tag->push_back(i); tag->push_back(i); }
    }
}

std::vector<std::vector<V2>> depthSection(const BodyDef& b, double z) {
    std::vector<P3d> v; std::vector<int> tri;
    depthTriangles(b, v, tri);
    // each triangle the plane crosses leaves a segment; the points are worked out per edge of the solid, the same
    // way from both triangles that share it, so the segments join up exactly
    std::map<std::pair<int, int>, int> edgePt;
    std::vector<V2> pts;
    auto cut = [&](int a, int c) -> int {
        int lo = std::min(a, c), hi = std::max(a, c);
        auto key = std::make_pair(lo, hi);
        auto it = edgePt.find(key);
        if (it != edgePt.end()) return it->second;
        const P3d &A = v[lo], &C = v[hi];
        double t = (z - A.z) / (C.z - A.z);
        pts.push_back({A.x + (C.x - A.x) * t, A.y + (C.y - A.y) * t});
        edgePt[key] = (int)pts.size() - 1;
        return (int)pts.size() - 1;
    };
    std::map<int, int> next;   // segment start -> end, going round each loop
    for (size_t t = 0; t < tri.size(); t += 3) {
        int k[3] = {tri[t], tri[t + 1], tri[t + 2]};
        bool above[3];
        for (int s = 0; s < 3; s++) above[s] = v[k[s]].z > z;
        int ups = above[0] + above[1] + above[2];
        if (ups == 0 || ups == 3) continue;
        // the lone corner on its side of the plane; the segment runs between its two edges, in a consistent direction
        int lone = 0;
        for (int s = 0; s < 3; s++) if ((ups == 1) == above[s]) lone = s;
        int p1 = k[(lone + 1) % 3], p2 = k[(lone + 2) % 3], L = k[lone];
        int s1 = cut(L, p1), s2 = cut(L, p2);
        if (ups == 1) next[s1] = s2; else next[s2] = s1;
    }
    std::vector<std::vector<V2>> loops;
    std::map<int, bool> used;
    for (auto& [st, en] : next) {
        if (used[st]) continue;
        std::vector<V2> loop;
        int cur = st, guard = 0;
        while (!used[cur] && next.count(cur) && guard++ < 100000) { used[cur] = true; loop.push_back(pts[cur]); cur = next[cur]; }
        if (loop.size() >= 3 && std::fabs(area2(loop)) > 1e-4) loops.push_back(loop);
    }
    // the outside of each piece (a hole would be wound the other way: an outline can't have one, so it's dropped)
    if (loops.size() > 1) {
        double big = 0; int bi = 0;
        for (int i = 0; i < (int)loops.size(); i++) if (std::fabs(area2(loops[i])) > big) { big = std::fabs(area2(loops[i])); bi = i; }
        double s = area2(loops[bi]);
        std::vector<std::vector<V2>> keep;
        for (auto& l : loops) if (area2(l) * s > 0) keep.push_back(l);
        loops = keep;
    }
    return loops;
}
