// Gunsmith3D - solid geometry by BSP trees (see csg.hpp). Coordinates in millimetres.
#include "csg.hpp"
#include <cmath>
#include <memory>

namespace csg {

double dot(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P3 cross(P3 a, P3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
P3 unit(P3 a) { double l = std::sqrt(dot(a, a)); return l > 1e-15 ? a * (1 / l) : P3{0, 0, 1}; }
P3 lerp(P3 a, P3 b, double t) { return a + (b - a) * t; }

static const double EPS = 1e-6;  // mm: points this close to a plane are on it

Poly makePoly(std::vector<Vert> v) {
    Poly p; p.v = std::move(v);
    P3 n{0, 0, 0};  // Newell's method: robust for slightly non-planar or sliver polygons
    for (size_t i = 0; i < p.v.size(); i++) {
        const P3& a = p.v[i].p; const P3& b = p.v[(i + 1) % p.v.size()].p;
        n.x += (a.y - b.y) * (a.z + b.z); n.y += (a.z - b.z) * (a.x + b.x); n.z += (a.x - b.x) * (a.y + b.y);
    }
    p.pn = unit(n);
    p.pw = p.v.empty() ? 0 : dot(p.pn, p.v[0].p);
    return p;
}
static void flip(Poly& p) {
    std::vector<Vert> r(p.v.rbegin(), p.v.rend());
    for (auto& v : r) v.n = v.n * -1;
    p.v = std::move(r);
    p.pn = p.pn * -1; p.pw = -p.pw;
}

struct Plane { P3 n; double w = 0; };
enum { COPLANAR = 0, FRONT = 1, BACK = 2, SPANNING = 3 };

// Sort a polygon against a plane, cutting it in two where it spans.
static void split(const Plane& pl, const Poly& poly, std::vector<Poly>& cf, std::vector<Poly>& cb,
                  std::vector<Poly>& f, std::vector<Poly>& b) {
    int type = 0;
    std::vector<int> types(poly.v.size());
    std::vector<double> ds(poly.v.size());
    for (size_t i = 0; i < poly.v.size(); i++) {
        double t = dot(pl.n, poly.v[i].p) - pl.w;
        ds[i] = t;
        int ty = t < -EPS ? BACK : t > EPS ? FRONT : COPLANAR;
        type |= ty; types[i] = ty;
    }
    switch (type) {
        case COPLANAR: (dot(pl.n, poly.pn) > 0 ? cf : cb).push_back(poly); break;
        case FRONT: f.push_back(poly); break;
        case BACK: b.push_back(poly); break;
        default: {
            std::vector<Vert> fv, bv;
            size_t n = poly.v.size();
            for (size_t i = 0; i < n; i++) {
                size_t j = (i + 1) % n;
                int ti = types[i], tj = types[j];
                const Vert &vi = poly.v[i], &vj = poly.v[j];
                if (ti != BACK) fv.push_back(vi);
                if (ti != FRONT) bv.push_back(vi);
                if ((ti | tj) == SPANNING) {
                    double t = ds[i] / (ds[i] - ds[j]);
                    Vert v{lerp(vi.p, vj.p, t), unit(lerp(vi.n, vj.n, t))};
                    fv.push_back(v); bv.push_back(v);
                }
            }
            if (fv.size() >= 3) { Poly q = poly; q.v = fv; f.push_back(q); }
            if (bv.size() >= 3) { Poly q = poly; q.v = bv; b.push_back(q); }
        }
    }
}

struct Node {
    bool hasPlane = false;
    Plane plane;
    std::unique_ptr<Node> front, back;
    std::vector<Poly> polys;

    void invert() {
        for (auto& p : polys) flip(p);
        plane.n = plane.n * -1; plane.w = -plane.w;
        if (front) front->invert();
        if (back) back->invert();
        std::swap(front, back);
    }
    // the parts of `ps` outside this solid
    std::vector<Poly> clip(const std::vector<Poly>& ps) const {
        if (!hasPlane) return ps;
        std::vector<Poly> f, b;
        for (auto& p : ps) split(plane, p, f, b, f, b);
        if (front) f = front->clip(f);
        if (back) b = back->clip(b);
        else b.clear();
        f.insert(f.end(), b.begin(), b.end());
        return f;
    }
    void clipTo(const Node& o) {
        polys = o.clip(polys);
        if (front) front->clipTo(o);
        if (back) back->clipTo(o);
    }
    void all(std::vector<Poly>& out) const {
        out.insert(out.end(), polys.begin(), polys.end());
        if (front) front->all(out);
        if (back) back->all(out);
    }
    void build(const std::vector<Poly>& ps) {
        if (ps.empty()) return;
        if (!hasPlane) { hasPlane = true; plane = {ps[0].pn, ps[0].pw}; }
        std::vector<Poly> f, b;
        for (auto& p : ps) split(plane, p, polys, polys, f, b);
        if (!f.empty()) { if (!front) front = std::make_unique<Node>(); front->build(f); }
        if (!b.empty()) { if (!back) back = std::make_unique<Node>(); back->build(b); }
    }
};

std::vector<Poly> subtract(const std::vector<Poly>& a, const std::vector<Poly>& b) {
    std::vector<Poly> pa, pb;
    for (auto& p : a) if (p.v.size() >= 3) pa.push_back(p);
    for (auto& p : b) if (p.v.size() >= 3) pb.push_back(p);
    if (pa.empty() || pb.empty()) return pa;
    Node A, B;
    A.build(pa); B.build(pb);
    A.invert(); A.clipTo(B);
    B.clipTo(A); B.invert(); B.clipTo(A); B.invert();
    std::vector<Poly> bp; B.all(bp);
    A.build(bp);
    A.invert();
    std::vector<Poly> out; A.all(out);
    return out;
}

}  // namespace csg
