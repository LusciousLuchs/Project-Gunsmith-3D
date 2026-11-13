// Gunsmith3D - scaling parts (see scale.hpp).
#include "scale.hpp"
#include "carve.hpp"
#include <algorithm>
#include <cmath>

void scaleParts(Design& D, const std::vector<int>& parts, V2 c, double sx, double sy, double sz, const std::vector<int>& corners) {
    auto sc = [&](V2& p) { p = {c.x + (p.x - c.x) * sx, c.y + (p.y - c.y) * sy}; };
    if (!corners.empty() && parts.size() == 1) {   // just these corners of one part
        BodyDef& d = D.bodies[parts[0]];
        fixShape(d);
        freezeMagazine(d, D.breech.cart);   // (a fitted magazine keeps the shape you give it)
        for (int v : corners) if (v >= 0 && v < (int)d.pts.size()) sc(d.pts[v]);
        for (auto& p : d.pts) { p.x = std::round(p.x * 100) / 100; p.y = std::round(p.y * 100) / 100; }
        return;
    }
    double area = std::fabs(sx * sy), lin = std::sqrt(area);
    double z0 = 1e9, z1 = -1e9;   // the piece's middle in depth: parts beside each other spread apart with the thickness
    for (int i : parts) { z0 = std::min(z0, D.bodies[i].z - D.bodies[i].thick / 2); z1 = std::max(z1, D.bodies[i].z + D.bodies[i].thick / 2); }
    double zc = (z0 + z1) / 2;
    for (int i : parts) {
        BodyDef& d = D.bodies[i];
        fixShape(d);
        freezeMagazine(d, D.breech.cart);   // a fitted magazine keeps the shape you give it, and its rounds scale with it
        double zWas = d.z;
        d.z = zc + (d.z - zc) * sz;
        size_t n = d.pts.size();
        // each edge's bow scales with how far the shape stretches across that edge
        for (size_t k = 0; k < n; k++) {
            V2 a = d.pts[k], b = d.pts[(k + 1) % n];
            double ex = b.x - a.x, ey = b.y - a.y, l = std::hypot(ex, ey);
            if (l > 1e-9) d.bulge[k] *= std::hypot(-ey / l * sx, ex / l * sy);
        }
        for (auto& p : d.pts) { sc(p); p.x = std::round(p.x * 100) / 100; p.y = std::round(p.y * 100) / 100; }
        for (auto& r : d.rad) r *= std::min(std::fabs(sx), std::fabs(sy));
        for (auto& o : d.depthOff) o = {o.x * sx, o.y * sy};
        for (auto& zn : d.depthZn) zn *= sz;
        for (auto& zf : d.depthZf) zf *= sz;
        if (!d.depthRef.empty()) d.depthRef = d.pts;
        for (auto& p : d.roundPath) sc(p);
        if (!d.round) d.thick = std::max(0.2, d.thick * sz);
        if (meshShaped(d)) {   // a free 3D mesh: every corner scaled the same way
            for (auto& v : d.mesh.v) { V2 q{v.x, v.y}; sc(q); v.x = q.x; v.y = q.y; v.z = zc + (v.z - zc) * sz; }
            if (sx * sy * sz < 0) for (auto& f : d.mesh.f) std::reverse(f.begin(), f.end());
            d.meshRef = d.pts; d.meshRefZ = d.z; d.meshRefThick = d.thick;
        }
        for (auto& mt : d.mounts) { sc(mt.at); mt.atZ *= sz; }
        if (d.rail.on) { sc(d.rail.a); sc(d.rail.b); }
        for (auto& cv : d.carves) {
            sc(cv.at);
            cv.atZ *= sz;
            double L = std::sqrt(cv.dx * cv.dx + cv.dy * cv.dy + cv.dz * cv.dz);
            if (L < 1e-9) continue;
            double ux = cv.dx / L, uy = cv.dy / L, uz = cv.dz / L;
            double along = std::sqrt(ux * ux * sx * sx + uy * uy * sy * sy + uz * uz * sz * sz);   // how much the cut's direction stretches
            if (cv.depth > 0) cv.depth *= along;
            if (std::fabs(uz) > 0.7) { cv.w *= std::fabs(sx); cv.h *= std::fabs(sy); }          // cut from the side: its shape lies in the side view
            else { double across = std::fabs(ux) > std::fabs(uy) ? sy : sx; cv.w *= std::sqrt(std::fabs(across * sz)); cv.h *= std::sqrt(std::fabs(across * sz)); }
        }
        for (auto& m : d.marks) {   // engravings stay on the surface
            for (auto& p : m.pts) { V2 q{p.x, p.y}; sc(q); p.x = q.x; p.y = q.y; p.z = d.z + (p.z - zWas) * sz; }
            for (auto& nn : m.n) { double x = nn.x / sx, y = nn.y / sy, z = nn.z / sz, l = std::sqrt(x * x + y * y + z * z); if (l > 1e-12) nn = {x / l, y / l, z / l}; }
            m.size *= lin;
        }
        if (!d.marks.empty()) { d.markRef = d.pts; d.markRefZ = d.z; d.markRefThick = d.thick; }
        if (d.ctrl != "none") sc(d.ctrlPt);
        if (d.mass > 0) d.mass *= area * (d.round ? lin : sz);   // a weight you set grows with the part
        // what's attached at points on it
        const std::string& id = d.id;
        for (auto& j : D.joints) {
            if ((j.type == "pin" || j.type == "weld") && j.a == id) sc(j.p);
            if (j.type == "track") { if (j.a == id) sc(j.p); if (j.b == id) for (auto& q : j.path) sc(q); }
            if (j.type == "magwell" && j.a == id) { sc(j.p); for (auto& q : j.path) sc(q); }
            if (j.type == "spring") { if (j.a == id) sc(j.pa); if (j.b == id) sc(j.pb); }
        }
        auto& br = D.breech;
        if (br.on) {
            if (br.breech == id && br.hasFace) sc(br.face);
            if (br.chamber == id && br.hasMuzzle) sc(br.muzzle);
            for (auto& cm : br.more) { if ((cm.breech.empty() ? br.breech : cm.breech) == id) sc(cm.face); if (cm.chamber == id && cm.hasMuzzle) sc(cm.muzzle); }
            if (br.striker == id && br.ignition == "striker") sc(br.strikerTip);
        }
        for (HandHold* h : {&D.pose.firing, &D.pose.support, &D.pose.butt}) if (h->on && h->part == id) sc(h->at);
    }
    // pins and welds shared by two scaled parts move only once (above, with their first part): nothing more to do
}
