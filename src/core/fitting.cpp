// Gunsmith3D - Fitting attachments (see fitting.hpp).
#include "fitting.hpp"
#include "mounts.hpp"
#include "solver.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

using json = nlohmann::json;

namespace fit {

static P3 add(P3 a, P3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static P3 sub(P3 a, P3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static P3 mul(P3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
static double dot(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static P3 cross(P3 a, P3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static P3 unit(P3 a) { double l = std::sqrt(dot(a, a)); return l > 1e-12 ? mul(a, 1 / l) : P3{1, 0, 0}; }

P3 Xf::dir(P3 v) const { return {R[0][0] * v.x + R[0][1] * v.y + R[0][2] * v.z, R[1][0] * v.x + R[1][1] * v.y + R[1][2] * v.z, R[2][0] * v.x + R[2][1] * v.y + R[2][2] * v.z}; }
P3 Xf::apply(P3 p, P3 spot) const { return add(O, dir(sub(p, spot))); }
P3 Xf::unapply(P3 g, P3 spot) const {
    P3 d = sub(g, O);
    return add(spot, {R[0][0] * d.x + R[1][0] * d.y + R[2][0] * d.z, R[0][1] * d.x + R[1][1] * d.y + R[2][1] * d.z, R[0][2] * d.x + R[1][2] * d.y + R[2][2] * d.z});
}

std::string Site::key() const {
    std::string o = owner < 0 ? "g" : "a" + std::to_string(owner);
    return o + (rail ? ":rail:" + part : ":pt:" + part + ":" + std::to_string(index));
}

bool kindsMatch(const std::string& a, const std::string& b) {
    auto norm = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        size_t i = 0; while (i < s.size() && s[i] == ' ') i++;
        s = s.substr(i);
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    return norm(a) == norm(b);
}

// a rail's and a point's frame, in a design's own mm
static void addSites(const Design& d, int owner, const Xf* xf, P3 spot, std::vector<Site>& out) {
    for (auto& b : d.bodies) {
        if (b.rail.on) {
            Site s; s.rail = true; s.type = "Picatinny"; s.part = b.id; s.owner = owner;
            V2 a = b.rail.a, e = b.rail.b;
            double L = std::hypot(e.x - a.x, e.y - a.y);
            if (L < 1e-6) continue;
            P3 t{(e.x - a.x) / L, (e.y - a.y) / L, 0};
            P3 up{t.y, -t.x, 0};   // as makeRail: the perpendicular pointing up (y down), or forward for an upright rail
            if (up.y > 1e-9 || (std::fabs(up.y) < 1e-9 && up.x < 0)) up = mul(up, -1);
            const double H = mounts::RAIL_HEIGHT;
            P3 n, o{a.x, a.y, b.z};
            if (b.rail.face == 0) { n = up; o = add(o, mul(n, H)); }
            else if (b.rail.face == 1) { n = mul(up, -1); o = add(o, mul(n, H)); }
            else { n = {0, 0, b.rail.face == 2 ? 1.0 : -1.0}; o.z += n.z * H / 2; }
            s.at = o; s.along = t; s.out = n; s.length = L;
            s.slots = mounts::railSlots(b);
            if (xf) { s.at = xf->apply(s.at, spot); s.along = xf->dir(s.along); s.out = xf->dir(s.out); }
            out.push_back(s);
        }
        for (size_t k = 0; k < b.mounts.size(); k++) {
            const MountDef& m = b.mounts[k];
            Site s; s.rail = false; s.type = m.type; s.part = b.id; s.owner = owner; s.index = (int)k;
            s.at = {m.at.x, m.at.y, b.z + m.atZ};
            s.out = unit({m.nx, m.ny, m.nz});
            // its forward: the gun's forward laid flat on the surface, turned by the point's own turn
            P3 f = sub(P3{1, 0, 0}, mul(s.out, s.out.x));
            if (dot(f, f) < 1e-6) f = sub(P3{0, -1, 0}, mul(s.out, -s.out.y));   // (a point facing forward: its "forward" is up)
            f = unit(f);
            double sp = m.spin * 3.14159265358979 / 180;
            P3 g = cross(s.out, f);
            s.along = unit(add(mul(f, std::cos(sp)), mul(g, std::sin(sp))));
            if (xf) { s.at = xf->apply(s.at, spot); s.along = xf->dir(s.along); s.out = xf->dir(s.out); }
            out.push_back(s);
        }
    }
}

std::vector<Site> sites(const Design& gun, const std::vector<Item>& items) {
    std::vector<Site> out;
    addSites(gun, -1, nullptr, {}, out);
    for (size_t k = 0; k < items.size(); k++)
        if (items[k].att.attach.job == "adapter") addSites(items[k].att, (int)k, &items[k].xf, items[k].spot(), out);
    return out;
}

double snapAlong(const Site& s, double along) {
    if (!s.rail || s.slots.empty()) return std::clamp(along, 0.0, s.length);
    double best = s.slots[0];
    for (double v : s.slots) if (std::fabs(v - along) < std::fabs(best - along)) best = v;
    return best;
}

Xf place(const Design& att, const Site& s, double along) {
    // the attachment's side that meets the mount faces into it, and its forward lies along the rail (or the point's
    // forward); for one meeting it with an end (a muzzle device), its up lies along the point's forward instead
    const std::string& face = att.attach.face;
    P3 m, aux, M = mul(s.out, -1), T;
    if (face == "rear") { m = {-1, 0, 0}; aux = {0, -1, 0}; }
    else if (face == "front") { m = {1, 0, 0}; aux = {0, -1, 0}; }
    else if (face == "top") { m = {0, -1, 0}; aux = {1, 0, 0}; }
    else { m = {0, 1, 0}; aux = {1, 0, 0}; }
    T = s.along;
    if (face == "rear" || face == "front") {   // up: the gun's up laid flat across the point (else its forward)
        P3 u = sub(P3{0, -1, 0}, mul(M, -M.y));
        if (dot(u, u) < 1e-6) u = sub(P3{1, 0, 0}, mul(M, M.x));
        T = unit(u);
    }
    T = unit(sub(T, mul(M, dot(T, M))));   // square to the mount's direction
    P3 c1 = cross(m, aux), c2 = cross(M, T);
    Xf x;
    // R = [M T c2] [m aux c1]^T: takes m to M, aux to T
    P3 A[3] = {M, T, c2}, B[3] = {m, aux, c1};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            double v = 0;
            for (int k = 0; k < 3; k++) {
                double a = r == 0 ? A[k].x : r == 1 ? A[k].y : A[k].z;
                double b = c == 0 ? B[k].x : c == 1 ? B[k].y : B[k].z;
                v += a * b;
            }
            x.R[r][c] = std::fabs(v) < 1e-12 ? 0 : v;
        }
    x.O = s.rail ? add(s.at, mul(s.along, along)) : s.at;
    return x;
}

// ---------------------------------------------------------------- will it go there?
static double edgeDist(const std::vector<V2>& p, double x, double y) {
    double best = 1e30;
    for (size_t i = 0; i < p.size(); i++) {
        V2 a = p[i], b = p[(i + 1) % p.size()];
        double vx = b.x - a.x, vy = b.y - a.y, L2 = vx * vx + vy * vy;
        double t = L2 > 0 ? std::clamp(((x - a.x) * vx + (y - a.y) * vy) / L2, 0.0, 1.0) : 0;
        best = std::min(best, std::hypot(x - a.x - vx * t, y - a.y - vy * t));
    }
    return best;
}
// a point well inside a part (by more than `inset` mm), its depth within the part's thickness
static bool insidePart(const BodyDef& b, P3 p, double inset = 0.4) {
    if (std::fabs(p.z - b.z) > b.thick / 2 - inset) return false;
    if (!sim::inPoly(p.x, p.y, b.pts)) return false;
    return edgeDist(b.pts, p.x, p.y) > inset;
}
// points spread through an attachment's parts (its own mm): corners, edge middles, a grid inside, at three depths
static std::vector<P3> samples(const Design& att) {
    std::vector<P3> out;
    for (auto& b : att.bodies) {
        if (b.pts.size() < 3) continue;
        std::vector<V2> pl;
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (auto& q : b.pts) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
        double step = std::max(1.5, std::max(x1 - x0, y1 - y0) / 14);
        for (size_t i = 0; i < b.pts.size(); i++) {   // just inside the outline: corners and along the edges
            V2 a = b.pts[i], c = b.pts[(i + 1) % b.pts.size()];
            double L = std::hypot(c.x - a.x, c.y - a.y);
            int n = std::max(1, (int)(L / step));
            for (int k = 0; k < n; k++) pl.push_back({a.x + (c.x - a.x) * k / n, a.y + (c.y - a.y) * k / n});
        }
        V2 cen{(x0 + x1) / 2, (y0 + y1) / 2};
        for (auto& q : pl) { double dx = cen.x - q.x, dy = cen.y - q.y, l = std::hypot(dx, dy); if (l > 0.6) { q.x += dx / l * 0.6; q.y += dy / l * 0.6; } }
        for (double x = x0 + step / 2; x < x1; x += step)
            for (double y = y0 + step / 2; y < y1; y += step) if (sim::inPoly(x, y, b.pts)) pl.push_back({x, y});
        double hz = std::max(0.0, b.thick / 2 - 0.6);
        for (auto& q : pl) for (double dz : {-hz, 0.0, hz}) out.push_back({q.x, q.y, b.z + dz});
    }
    return out;
}

std::string check(const Design& gun, const std::vector<Item>& items, const Design& att, const Site& s, double along, int skipItem) {
    const AttachDef& a = att.attach;
    if (s.rail ? a.mount != "Picatinny" : !kindsMatch(a.mount, s.type))
        return s.rail ? "It doesn't clamp on a Picatinny rail (it fits " + a.mount + ")" :
                        (a.mount == "Picatinny" ? "It clamps on a Picatinny rail; this is a " + s.type : "It fits a " + a.mount + "; this is a " + s.type);
    if (s.rail && (along - a.clamp / 2 < -0.5 || along + a.clamp / 2 > s.length + 0.5)) return "Its clamp runs off the end of the rail";
    Xf xf = place(att, s, along);
    P3 spot{a.at.x, a.at.y, a.atZ};
    std::vector<P3> pts;
    for (auto& p : samples(att)) pts.push_back(xf.apply(p, spot));
    // other attachments (not the adapter it sits on): either one's points inside the other
    for (size_t k = 0; k < items.size(); k++) {
        if ((int)k == skipItem || (int)k == s.owner) continue;
        const Item& it = items[k];
        for (auto& g : pts) {
            P3 q = it.xf.unapply(g, it.spot());
            for (auto& b : it.att.bodies) if (insidePart(b, q)) return "It runs into the " + (it.att.name.empty() ? it.file : it.att.name);
        }
        for (auto& p : samples(it.att)) {
            P3 q = xf.unapply(it.xf.apply(p, it.spot()), spot);
            for (auto& b : att.bodies) if (insidePart(b, q)) return "It runs into the " + (it.att.name.empty() ? it.file : it.att.name);
        }
    }
    // the gun's parts: solid ones where they are, moving ones along all their travel (slides, bolts, carriers on tracks)
    std::string carrier = s.owner < 0 ? s.part : "";
    for (auto& b : gun.bodies) {
        if (b.id == carrier || b.ghost || b.magazine) continue;
        if (b.layer == "none" && b.fixed) continue;   // (parts that don't touch anything: grips, stocks drawn over others)
        std::vector<P3> offs{{0, 0, 0}};
        for (auto& j : gun.joints) {
            if (j.type != "track") continue;
            int ja = gun.bodyIndex(j.a);
            if (ja < 0) continue;
            const BodyDef& jb = gun.bodies[ja];
            if (!(j.a == b.id || (!jb.weld.empty() && jb.weld == b.weld))) continue;
            for (size_t k = 0; k + 1 < j.path.size() || (k == 0 && j.path.size() == 1); k++) {   // along the track, 2 mm at a time
                V2 p0 = j.path[k], p1 = j.path.size() > 1 ? j.path[k + 1] : j.path[k];
                double L = std::hypot(p1.x - p0.x, p1.y - p0.y);
                int n = std::max(1, (int)(L / 2));
                for (int q = 0; q <= n; q++) offs.push_back({p0.x + (p1.x - p0.x) * q / n - j.p.x, p0.y + (p1.y - p0.y) * q / n - j.p.y, 0});
            }
        }
        for (auto& g : pts)
            for (size_t o = 0; o < offs.size(); o++)
                if (insidePart(b, sub(g, offs[o]))) {
                    std::string nm = b.name.empty() ? b.id : b.name;
                    return o == 0 ? "It runs into the " + nm : "It's in the way of the " + nm + " (it moves there)";
                }
    }
    return "";
}

// ---------------------------------------------------------------- loadouts
std::string loadoutToJson(const std::string& gun, const std::vector<Item>& items) {
    json o = {{"gunsmithLoadout", 1}, {"gun", gun}};
    json a = json::array();
    for (auto& it : items) {
        json j = {{"file", it.file}, {"site", it.site}, {"along", it.along}};
        if (!it.on) j["on"] = false;
        a.push_back(j);
    }
    o["items"] = a;
    return o.dump(1);
}
bool loadoutFromJson(const std::string& text, std::string& gun, std::vector<LoadoutEntry>& out) {
    json o = json::parse(text, nullptr, false);
    if (o.is_discarded() || !o.is_object() || !o.contains("gunsmithLoadout")) return false;
    gun = o.value("gun", "");
    out.clear();
    if (o.contains("items") && o["items"].is_array())
        for (auto& j : o["items"]) if (j.is_object()) out.push_back({j.value("file", ""), j.value("site", ""), j.value("along", 0.0), j.value("on", true)});
    return true;
}
}  // namespace fit
