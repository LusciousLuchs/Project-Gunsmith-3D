// Gunsmith3D - parts made as free 3D meshes (see MeshDef): building one from a part, its triangles, where a plane at a
// depth cuts it (the physics' slabs), keeping the part's outline in step, and the editing operations (ring cuts,
// extrusions, removing corners).
#include "design.hpp"
#include "carve.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {
P3d sub(P3d a, P3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
P3d add(P3d a, P3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
P3d mul(P3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(P3d a, P3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P3d cross(P3d a, P3d b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double len(P3d a) { return std::sqrt(dot(a, a)); }
double coord(P3d p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
using Edge = std::pair<int, int>;
Edge key(int a, int b) { return a < b ? Edge{a, b} : Edge{b, a}; }

P3d newell(const MeshDef& m, const std::vector<int>& L) {
    P3d n{0, 0, 0};
    for (size_t i = 0; i < L.size(); i++) {
        const P3d& a = m.v[L[i]]; const P3d& b = m.v[L[(i + 1) % L.size()]];
        n.x += (a.y - b.y) * (a.z + b.z); n.y += (a.z - b.z) * (a.x + b.x); n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}
double area2(const std::vector<V2>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); i++) { const V2 &u = p[i], &v = p[(i + 1) % p.size()]; a += u.x * v.y - v.x * u.y; }
    return a;
}
// close every hole in the surface (edges only one face uses) with a new face round it
void fillHoles(MeshDef& m) {
    std::map<Edge, int> uses;
    std::map<int, std::vector<int>> next;   // a hole's edge, the way its loop goes (against the face beside it)
    for (auto& f : m.f) for (size_t i = 0; i < f.size(); i++) uses[key(f[i], f[(i + 1) % f.size()])]++;
    for (auto& f : m.f)
        for (size_t i = 0; i < f.size(); i++) {
            int a = f[i], b = f[(i + 1) % f.size()];
            if (uses[key(a, b)] == 1) next[b].push_back(a);
        }
    std::set<Edge> done;
    for (auto& [start, outs] : next)
        for (int o : outs) {
            if (done.count({start, o})) continue;
            std::vector<int> loop{start};
            int cur = start, nx = o;
            done.insert({start, o});
            for (int guard = 0; guard < 4096 && nx != start; guard++) {
                loop.push_back(nx);
                auto it = next.find(nx);
                if (it == next.end() || it->second.empty()) { loop.clear(); break; }
                int pick = -1;
                for (int c : it->second) if (!done.count({nx, c})) { pick = c; break; }
                if (pick < 0) { loop.clear(); break; }
                done.insert({nx, pick});
                cur = nx; nx = pick;
            }
            (void)cur;
            if (loop.size() >= 3) m.f.push_back(loop);
        }
}
}  // namespace

bool meshShaped(const BodyDef& b) { return b.mesh.v.size() >= 4 && b.mesh.f.size() >= 4; }

P3d meshFaceNormal(const MeshDef& m, int f) {
    P3d n = newell(m, m.f[f]);
    double l = len(n);
    return l > 1e-12 ? mul(n, 1 / l) : P3d{0, 0, 1};
}
P3d meshFaceCentre(const MeshDef& m, int f) {
    P3d c{0, 0, 0};
    for (int i : m.f[f]) c = add(c, m.v[i]);
    return m.f[f].empty() ? c : mul(c, 1.0 / m.f[f].size());
}

void meshOrient(MeshDef& m) {
    // neighbours agree: two faces sharing an edge run along it opposite ways; then the whole is turned outward
    std::map<Edge, std::vector<int>> byEdge;
    for (int f = 0; f < (int)m.f.size(); f++)
        for (size_t i = 0; i < m.f[f].size(); i++) byEdge[key(m.f[f][i], m.f[f][(i + 1) % m.f[f].size()])].push_back(f);
    auto runs = [&](int f, int a, int b) {   // does face f go a -> b?
        const auto& L = m.f[f];
        for (size_t i = 0; i < L.size(); i++) if (L[i] == a && L[(i + 1) % L.size()] == b) return true;
        return false;
    };
    std::vector<char> seen(m.f.size(), 0);
    for (int s = 0; s < (int)m.f.size(); s++) {
        if (seen[s]) continue;
        std::vector<int> todo{s}; seen[s] = 1;
        while (!todo.empty()) {
            int f = todo.back(); todo.pop_back();
            const auto L = m.f[f];
            for (size_t i = 0; i < L.size(); i++) {
                int a = L[i], b = L[(i + 1) % L.size()];
                for (int g : byEdge[key(a, b)]) {
                    if (g == f || seen[g]) continue;
                    if (runs(g, a, b)) std::reverse(m.f[g].begin(), m.f[g].end());
                    seen[g] = 1; todo.push_back(g);
                }
            }
        }
    }
    double vol = 0;   // (signed: positive when the faces wind outward)
    std::vector<int> tri;
    meshTriangles(m, tri);
    for (size_t t = 0; t + 2 < tri.size(); t += 3) vol += dot(m.v[tri[t]], cross(m.v[tri[t + 1]], m.v[tri[t + 2]]));
    if (vol < 0) for (auto& f : m.f) std::reverse(f.begin(), f.end());
}

void meshTriangles(const MeshDef& m, std::vector<int>& tri, std::vector<int>* faceOf) {
    tri.clear();
    if (faceOf) faceOf->clear();
    for (int f = 0; f < (int)m.f.size(); f++) {
        const auto& L = m.f[f];
        if (L.size() < 3) continue;
        P3d n = newell(m, L);
        auto push = [&](int a, int b, int c) {
            P3d tn = cross(sub(m.v[b], m.v[a]), sub(m.v[c], m.v[a]));
            if (dot(tn, n) < 0) std::swap(b, c);
            tri.push_back(a); tri.push_back(b); tri.push_back(c);
            if (faceOf) faceOf->push_back(f);
        };
        if (L.size() == 3) { push(L[0], L[1], L[2]); continue; }
        // flatten it onto the plane it faces most, and cut it into triangles there (it may be concave)
        double ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
        std::vector<V2> p;
        for (int i : L) {
            const P3d& q = m.v[i];
            p.push_back(az >= ax && az >= ay ? V2{q.x, q.y} : ay >= ax ? V2{q.x, q.z} : V2{q.y, q.z});
        }
        std::vector<int> t = triangulateOutline(p);
        if (t.size() < 3) { for (size_t i = 1; i + 1 < L.size(); i++) push(L[0], L[i], L[i + 1]); continue; }
        for (size_t k = 0; k + 2 < t.size(); k += 3) push(L[t[k]], L[t[k + 1]], L[t[k + 2]]);
    }
}

void meshSpan(const MeshDef& m, double& z0, double& z1) {
    z0 = 1e18; z1 = -1e18;
    for (auto& p : m.v) { z0 = std::min(z0, p.z); z1 = std::max(z1, p.z); }
    if (z0 > z1) z0 = z1 = 0;
}

std::vector<std::vector<V2>> meshSection(const MeshDef& m, double z) {
    std::vector<int> tri;
    meshTriangles(m, tri);
    // every triangle the plane crosses gives a piece of line between two of its edges; the pieces join up edge to edge
    auto side = [&](int i) { double s = m.v[i].z - z; return s >= 0 ? (s < 1e-9 ? 1e-9 : s) : s; };
    std::map<Edge, std::vector<Edge>> link;
    std::map<Edge, V2> at;
    for (size_t t = 0; t + 2 < tri.size(); t += 3) {
        int id[3] = {tri[t], tri[t + 1], tri[t + 2]};
        std::vector<Edge> cross;
        for (int k = 0; k < 3; k++) {
            int a = id[k], b = id[(k + 1) % 3];
            double sa = side(a), sb = side(b);
            if ((sa > 0) == (sb > 0)) continue;
            Edge e = key(a, b);
            if (!at.count(e)) {
                double u = sa / (sa - sb);
                const P3d &A = m.v[a], &B = m.v[b];
                at[e] = {A.x + (B.x - A.x) * u, A.y + (B.y - A.y) * u};
            }
            cross.push_back(e);
        }
        if (cross.size() == 2) { link[cross[0]].push_back(cross[1]); link[cross[1]].push_back(cross[0]); }
    }
    std::vector<std::vector<V2>> out;
    std::set<Edge> used;
    for (auto& [e0, nb] : link) {
        if (used.count(e0)) continue;
        std::vector<V2> loop;
        Edge prev{-1, -1}, cur = e0;
        bool closed = false;
        for (int guard = 0; guard < 100000; guard++) {
            used.insert(cur);
            loop.push_back(at[cur]);
            const auto& n = link[cur];
            Edge nx{-1, -1};
            for (auto& c : n) if (c != prev && (!used.count(c) || (c == e0 && loop.size() > 2))) { nx = c; break; }
            if (nx.first < 0) break;
            if (nx == e0) { closed = true; break; }
            prev = cur; cur = nx;
        }
        if (!closed || loop.size() < 3) continue;
        // drop corners that lie on a straight line (the triangles' own edges cross the plane there)
        std::vector<V2> clean;
        for (size_t i = 0; i < loop.size(); i++) {
            const V2 &a = loop[(i + loop.size() - 1) % loop.size()], &b = loop[i], &c = loop[(i + 1) % loop.size()];
            double cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
            double l = std::hypot(b.x - a.x, b.y - a.y) * std::hypot(c.x - b.x, c.y - b.y);
            if (l < 1e-12 || std::fabs(cr) > 1e-6 * l) clean.push_back(b);
        }
        if (clean.size() >= 3 && std::fabs(area2(clean)) > 0.02) out.push_back(clean);
    }
    return out;
}

void meshFromPart(BodyDef& b) {
    if (meshShaped(b) || b.pts.size() < 3) return;
    std::vector<P3d> N, F;
    if (depthShaped(b)) depthRings(b, N, F);
    else {
        double t = std::max(0.5, b.thick) / 2;
        for (auto& p : outlineOf(b)) { N.push_back({p.x, p.y, b.z + t}); F.push_back({p.x, p.y, b.z - t}); }
    }
    int n = (int)N.size();
    if (n < 3) return;
    MeshDef m;
    m.v = N;
    m.v.insert(m.v.end(), F.begin(), F.end());
    std::vector<int> nearF, farF;
    for (int i = 0; i < n; i++) { nearF.push_back(i); farF.push_back(2 * n - 1 - i); }
    m.f.push_back(nearF); m.f.push_back(farF);
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; m.f.push_back({i, j, n + j, n + i}); }
    meshOrient(m);
    b.mesh = m;
    b.depthOff.clear(); b.depthZn.clear(); b.depthZf.clear(); b.depthRef.clear();
    b.meshRef.clear();
    meshSync(b);
}

void meshSync(BodyDef& b) {
    if (!meshShaped(b)) { b.mesh = {}; b.meshRef.clear(); return; }
    MeshDef& m = b.mesh;
    // the outline moved, turned or flipped as a whole: the mesh goes with it (reshaping the outline does nothing)
    const auto& A = b.meshRef; const auto& B = b.pts;
    bool moved = A.size() == B.size() && A.size() >= 2;
    if (moved) { moved = false; for (size_t i = 0; i < A.size() && !moved; i++) moved = A[i].x != B[i].x || A[i].y != B[i].y; }
    if (moved) {
        V2 ca{0, 0}, cb{0, 0};
        for (size_t i = 0; i < A.size(); i++) { ca.x += A[i].x; ca.y += A[i].y; cb.x += B[i].x; cb.y += B[i].y; }
        ca.x /= A.size(); ca.y /= A.size(); cb.x /= B.size(); cb.y /= B.size();
        double best = 1e30, bc = 1, bs = 0; int bflip = 1;
        for (int flip : {1, -1}) {
            double sxx = 0, sxy = 0;
            for (size_t i = 0; i < A.size(); i++) {
                double ax = (A[i].x - ca.x) * flip, ay = A[i].y - ca.y, bx = B[i].x - cb.x, by = B[i].y - cb.y;
                sxx += ax * bx + ay * by; sxy += ax * by - ay * bx;
            }
            double th = std::atan2(sxy, sxx), c = std::cos(th), s = std::sin(th), worst = 0;
            for (size_t i = 0; i < A.size(); i++) {
                double ax = (A[i].x - ca.x) * flip, ay = A[i].y - ca.y;
                worst = std::fmax(worst, std::hypot(c * ax - s * ay + cb.x - B[i].x, s * ax + c * ay + cb.y - B[i].y));
            }
            if (worst < best) { best = worst; bc = c; bs = s; bflip = flip; }
        }
        if (best < 0.01) {
            for (auto& p : m.v) { double ax = (p.x - ca.x) * bflip, ay = p.y - ca.y; p.x = bc * ax - bs * ay + cb.x; p.y = bs * ax + bc * ay + cb.y; }
            if (bflip < 0) for (auto& f : m.f) std::reverse(f.begin(), f.end());
        }
    }
    if (carve::tilted(b)) {   // leaned (the rotation rings): the lean goes into the corners themselves
        double R[3][3]; carve::tiltMatrix(b, R);
        V2 c = centroidOf(b);
        for (auto& p : m.v) {
            double q[3] = {p.x - c.x, p.y - c.y, p.z - b.z}, r[3];
            for (int i = 0; i < 3; i++) r[i] = R[i][0] * q[0] + R[i][1] * q[1] + R[i][2] * q[2];
            p = {c.x + r[0], c.y + r[1], b.z + r[2]};
        }
        b.tiltX = b.tiltY = 0;
    }
    if (!b.meshRef.empty() && b.meshRefThick > 0 && (b.z != b.meshRefZ || b.thick != b.meshRefThick)) {   // moved in depth, or made thicker
        double k = std::max(0.5, b.thick) / b.meshRefThick;
        for (auto& p : m.v) p.z = b.z + (p.z - b.meshRefZ) * k;
    }
    // then the outline from the mesh: its depth, its thickness, and its biggest cross-section for the side view
    double z0, z1;
    meshSpan(m, z0, z1);
    b.z = (z0 + z1) / 2;
    b.thick = std::max(0.5, z1 - z0);
    std::vector<V2> best;
    double ba = 0;
    for (int k = 1; k <= 11; k++) {
        double z = z0 + (z1 - z0) * k / 12.0;
        for (auto& s : meshSection(m, z)) { double a = std::fabs(area2(s)); if (a > ba) { ba = a; best = s; } }
    }
    if (best.size() >= 3) {
        if (best.size() > (size_t)MAX_CORNERS) best.resize(MAX_CORNERS);
        b.pts = best;
    }
    b.rad.assign(b.pts.size(), 0.0);
    b.bulge.assign(b.pts.size(), 0.0);
    b.depthOff.clear(); b.depthZn.clear(); b.depthZf.clear(); b.depthRef.clear();
    b.meshRef = b.pts; b.meshRefZ = b.z; b.meshRefThick = b.thick;
}

std::vector<int> meshCut(MeshDef& m, int axis, double at) {
    std::map<Edge, int> made;
    std::vector<int> added;
    auto off = [&](int i) { return coord(m.v[i], axis) - at; };
    const double EPS = 1e-6;
    auto cutAt = [&](int a, int b) {
        Edge e = key(a, b);
        auto it = made.find(e);
        if (it != made.end()) return it->second;
        double sa = off(a), sb = off(b), u = sa / (sa - sb);
        m.v.push_back(add(m.v[a], mul(sub(m.v[b], m.v[a]), u)));
        int id = (int)m.v.size() - 1;
        made[e] = id; added.push_back(id);
        return id;
    };
    std::vector<std::vector<int>> out;
    for (auto& L : m.f) {
        std::vector<int> R;
        std::vector<int> onAt;   // positions in R that lie on the plane
        for (size_t i = 0; i < L.size(); i++) {
            int a = L[i], b = L[(i + 1) % L.size()];
            if (std::fabs(off(a)) < EPS) onAt.push_back((int)R.size());
            R.push_back(a);
            double sa = off(a), sb = off(b);
            if (std::fabs(sa) >= EPS && std::fabs(sb) >= EPS && (sa > 0) != (sb > 0)) { onAt.push_back((int)R.size()); R.push_back(cutAt(a, b)); }
        }
        int k = (int)R.size();
        if (onAt.size() == 2 && (onAt[1] - onAt[0]) > 1 && (onAt[1] - onAt[0]) < k - 1) {   // split in two along the cut
            std::vector<int> P, Q;
            for (int i = onAt[0]; i <= onAt[1]; i++) P.push_back(R[i]);
            for (int i = onAt[1]; i != onAt[0]; i = (i + 1) % k) Q.push_back(R[i]);
            Q.push_back(R[onAt[0]]);
            out.push_back(P); out.push_back(Q);
        } else out.push_back(R);
    }
    m.f = out;
    return added;
}

std::vector<int> meshExtrude(MeshDef& m, int face, double dist) {
    if (face < 0 || face >= (int)m.f.size()) return {};
    std::vector<int> L = m.f[face];
    P3d n = meshFaceNormal(m, face);
    std::vector<int> top;
    for (int i : L) { m.v.push_back(add(m.v[i], mul(n, dist))); top.push_back((int)m.v.size() - 1); }
    for (size_t i = 0; i < L.size(); i++) {
        size_t j = (i + 1) % L.size();
        m.f.push_back({L[i], L[j], top[j], top[i]});
    }
    m.f[face] = top;
    meshOrient(m);
    return top;
}

bool meshRemoveVerts(MeshDef& m, std::vector<int> verts) {
    std::set<int> gone(verts.begin(), verts.end());
    if (gone.empty() || m.v.size() - gone.size() < 4) return false;
    MeshDef r;
    std::vector<int> remap(m.v.size(), -1);
    for (int i = 0; i < (int)m.v.size(); i++) if (!gone.count(i)) { remap[i] = (int)r.v.size(); r.v.push_back(m.v[i]); }
    for (auto& f : m.f) {
        std::vector<int> L;
        for (int i : f) if (remap[i] >= 0) L.push_back(remap[i]);
        if (L.size() >= 3) r.f.push_back(L);
    }
    fillHoles(r);
    if (r.f.size() < 4) return false;
    meshOrient(r);
    m = r;
    return true;
}
