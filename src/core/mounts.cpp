// Gunsmith3D - Picatinny rails and attachment points (see mounts.hpp).
#include "mounts.hpp"
#include "carve.hpp"

#include <algorithm>
#include <cmath>

namespace mounts {

static constexpr double PI_M = 3.14159265358979323846;

int railSlotCount(double L) { return L < SLOT_WIDTH + 2 ? 0 : (int)std::floor((L - SLOT_WIDTH - 2) / SLOT_PITCH) + 1; }

std::vector<double> railSlots(const BodyDef& r) {
    double L = std::hypot(r.rail.b.x - r.rail.a.x, r.rail.b.y - r.rail.a.y);
    int n = railSlotCount(L);
    std::vector<double> s;
    double s0 = (L - (n - 1) * SLOT_PITCH) / 2;
    for (int k = 0; k < n; k++) s.push_back(s0 + k * SLOT_PITCH);
    return s;
}

// a cut going in along d whose shape's u axis lies along t (both 3D, design axes: x forward, y down, z right)
static CarveDef cutAlong(carve::P3 d, carve::P3 t) {
    CarveDef c;
    c.shape = "rect"; c.dx = d.x; c.dy = d.y; c.dz = d.z; c.spin = 0;
    carve::P3 u0, v0, dd;
    carve::frame(c, u0, v0, dd);
    c.spin = std::atan2(t.x * v0.x + t.y * v0.y + t.z * v0.z, t.x * u0.x + t.y * u0.y + t.z * u0.z) * 180 / PI_M;
    return c;
}

BodyDef makeRail(V2 a, V2 b, int face, double z) {
    BodyDef r;
    double L = std::hypot(b.x - a.x, b.y - a.y);
    if (L < 1e-6) { b = {a.x + 20, a.y}; L = 20; }
    V2 t{(b.x - a.x) / L, (b.y - a.y) / L};
    V2 up{t.y, -t.x};                      // perpendicular, in the side view: the one pointing up (y down) ...
    if (up.y > 1e-9 || (std::fabs(up.y) < 1e-9 && up.x < 0)) up = {-up.x, -up.y};   // ... or, for an upright rail, forward
    face = std::clamp(face, 0, 3);
    r.rail.on = true; r.rail.a = a; r.rail.b = b; r.rail.face = face;
    r.name = "Picatinny rail"; r.color = "#2a2d31"; r.layer = "ABC";
    const double W = RAIL_WIDTH, H = RAIL_HEIGHT;
    auto at = [&](double s, V2 n, double off) { return V2{a.x + t.x * s + n.x * off, a.y + t.y * s + n.y * off}; };
    carve::P3 t3{t.x, t.y, 0};
    std::vector<double> slots;
    {
        int n = railSlotCount(L);
        double s0 = (L - (n - 1) * SLOT_PITCH) / 2;
        for (int k = 0; k < n; k++) slots.push_back(s0 + k * SLOT_PITCH);
    }
    if (face <= 1) {   // on top (or underneath): the drawn line is the surface it stands on; its mounting face is H out
        V2 n = face == 0 ? up : V2{-up.x, -up.y};   // out of the mounting face
        r.pts = {at(0, n, 0), at(L, n, 0), at(L, n, H), at(0, n, H)};
        r.thick = W; r.z = z;
        for (double s : slots) {   // the cross slots, from the mounting face in
            CarveDef c = cutAlong({-n.x, -n.y, 0}, t3);
            c.at = at(s, n, H); c.atZ = 0; c.w = SLOT_WIDTH; c.h = W + 2; c.depth = SLOT_DEPTH; c.name = "Slot";
            r.carves.push_back(c);
        }
        for (int sd : {1, -1}) {   // the dovetail: a groove along each side under the top
            CarveDef c = cutAlong({0, 0, -1.0 * sd}, t3);
            c.at = at(L / 2, n, H - 4.8); c.atZ = sd * W / 2; c.w = L + 2; c.h = 2.8; c.depth = 2.8; c.name = "Dovetail";
            r.carves.push_back(c);
        }
    } else {           // on a side: the drawn line is its middle, the bar lies flat against the gun's side
        double sd = face == 2 ? 1 : -1;  // which side its mounting face looks
        r.pts = {at(0, up, W / 2), at(L, up, W / 2), at(L, up, -W / 2), at(0, up, -W / 2)};
        r.thick = H; r.z = z;
        for (double s : slots) {
            CarveDef c = cutAlong({0, 0, -sd}, t3);
            c.at = at(s, up, 0); c.atZ = sd * H / 2; c.w = SLOT_WIDTH; c.h = W + 2; c.depth = SLOT_DEPTH; c.name = "Slot";
            r.carves.push_back(c);
        }
        for (int e : {1, -1}) {
            V2 n{up.x * e, up.y * e};
            CarveDef c = cutAlong({-n.x, -n.y, 0}, t3);
            c.at = at(L / 2, up, e * W / 2); c.atZ = sd * (H / 2 - 4.8); c.w = L + 2; c.h = 2.8; c.depth = 2.8; c.name = "Dovetail";
            r.carves.push_back(c);
        }
    }
    r.mass = L * H * W * 0.0027 * 0.8;   // aluminium, less the slots and grooves
    return r;
}

void regenRail(BodyDef& r) {
    BodyDef n = makeRail(r.rail.a, r.rail.b, r.rail.face, r.z);
    n.id = r.id; n.name = r.name; n.color = r.color; n.texture = r.texture; n.texScale = r.texScale;
    n.fixed = r.fixed; n.weld = r.weld; n.layer = r.layer; n.mounts = r.mounts; n.rail.base = r.rail.base;
    n.marks = r.marks;
    r = n;
}

double sideRailZ(const BodyDef& base, int face) {
    if (face <= 1) return base.z;
    return base.z + (face == 2 ? 1 : -1) * (base.thick / 2 + RAIL_HEIGHT / 2);
}

const std::vector<std::string>& commonMountTypes() {
    static const std::vector<std::string> t = {"Bayonet lug", "Warsaw Pact side rail", "Muzzle thread", "Sling point",
                                               "Optic cut", "M-LOK", "KeyMod", "Dovetail (11 mm)", "Dovetail (3/8 in)"};
    return t;
}

void moveMounts(BodyDef& d, double dx, double dy) {
    for (auto& m : d.mounts) { m.at.x += dx; m.at.y += dy; }
    if (d.rail.on) { d.rail.a.x += dx; d.rail.a.y += dy; d.rail.b.x += dx; d.rail.b.y += dy; }
}
void rotateMounts(BodyDef& d, V2 c, double deg) {
    double a = deg * PI_M / 180, cs = std::cos(a), sn = std::sin(a);
    auto rot = [&](V2& p) { double x = p.x - c.x, y = p.y - c.y; p = {c.x + x * cs - y * sn, c.y + x * sn + y * cs}; };
    for (auto& m : d.mounts) {
        rot(m.at);
        double nx = m.nx * cs - m.ny * sn, ny = m.nx * sn + m.ny * cs;
        m.nx = nx; m.ny = ny;
    }
    if (d.rail.on) { rot(d.rail.a); rot(d.rail.b); }
}
}  // namespace mounts
