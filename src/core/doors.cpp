// Gunsmith3D - doors on maps (see doors.hpp).
#include "doors.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace doors {
namespace {
using gmap::V3;
constexpr double PI_D = 3.14159265358979;
struct State { double open = 0, target = 0, sign = 1; };   // sign: which way it swings
std::map<int, State> gState;
const gmap::Map* gMap = nullptr;
size_t gCount = 0;
State& state(const gmap::Map& m, int i) {
    if (&m != gMap || m.ents.size() != gCount) { gState.clear(); gMap = &m; gCount = m.ents.size(); }   // (another map, or it changed)
    return gState[i];
}
V3 facingDir(const gmap::Entity& e) { double a = e.yaw * PI_D / 180; return {std::cos(a), 0, std::sin(a)}; }
V3 across(const gmap::Entity& e) { V3 f = facingDir(e); return {-f.z, 0, f.x}; }
bool isDoor(const gmap::Map& m, int i) { return i >= 0 && i < (int)m.ents.size() && m.ents[i].type == "door"; }
}  // namespace

bool solid(const gmap::Entity& e) { return e.ref != "glass"; }
void slab(const gmap::Map& m, int i, V3& hinge, V3& end) {
    const auto& e = m.ents[i];
    const State& s = state(m, i);
    V3 f = facingDir(e), t = across(e);
    hinge = e.pos - t * (e.width / 2);
    double a = s.open * PI_D / 2;
    end = hinge + (t * std::cos(a) + f * (std::sin(a) * s.sign)) * e.width;
}
int facing(const gmap::Map& m, V3 eye, V3 look) {
    int best = -1; double bd = 1e9;
    double ll = std::hypot(look.x, look.z);
    if (ll < 1e-6) return -1;
    for (int i = 0; i < (int)m.ents.size(); i++) {
        if (!isDoor(m, i)) continue;
        V3 h, q; slab(m, i, h, q);
        V3 mid = (h + q) * 0.5;
        double dx = mid.x - eye.x, dz = mid.z - eye.z, d = std::hypot(dx, dz);
        if (d > 2.0 || std::fabs(mid.y + 1.0 - eye.y) > 2) continue;
        double lk = d < 1e-6 ? 1 : (dx * look.x + dz * look.z) / (d * ll);
        if (lk < 0.5) continue;
        if (d - lk < bd) { bd = d - lk; best = i; }
    }
    return best;
}
bool isOpen(const gmap::Map& m, int i) { return isDoor(m, i) && state(m, i).target > 0.5; }
double openness(const gmap::Map& m, int i) { return isDoor(m, i) ? state(m, i).open : 0; }
void toggle(const gmap::Map& m, int i, V3 from) {
    if (!isDoor(m, i)) return;
    State& s = state(m, i);
    if (s.target > 0.5) { s.target = 0; return; }
    V3 f = facingDir(m.ents[i]);
    double side = (from.x - m.ents[i].pos.x) * f.x + (from.z - m.ents[i].pos.z) * f.z;
    if (s.open < 0.02) s.sign = side >= 0 ? -1 : 1;   // (away from whoever opens it)
    s.target = 1;
}
void swing(const gmap::Map& m, double dt) {
    for (int i = 0; i < (int)m.ents.size(); i++) {
        if (!isDoor(m, i)) continue;
        State& s = state(m, i);
        double sp = 2.2 * dt;
        s.open += std::clamp(s.target - s.open, -sp, sp);
    }
}
bool pushOut(const gmap::Map& m, V3& feet, double R, double tall, int* which) {
    bool pushed = false;
    for (int i = 0; i < (int)m.ents.size(); i++) {
        if (!isDoor(m, i)) continue;
        const auto& e = m.ents[i];
        if (feet.y > e.pos.y + HEIGHT || feet.y + tall < e.pos.y) continue;
        V3 h, q; slab(m, i, h, q);
        double ux = q.x - h.x, uz = q.z - h.z, L2 = ux * ux + uz * uz;
        double t = L2 > 0 ? std::clamp(((feet.x - h.x) * ux + (feet.z - h.z) * uz) / L2, 0.0, 1.0) : 0;
        double cx = h.x + ux * t, cz = h.z + uz * t, dx = feet.x - cx, dz = feet.z - cz, d = std::hypot(dx, dz);
        if (d < R) {
            if (d < 1e-6) { dx = -uz; dz = ux; d = std::hypot(dx, dz); }
            feet.x = cx + dx / d * R; feet.z = cz + dz / d * R;
            pushed = true;
            if (which) *which = i;
        }
    }
    return pushed;
}
void step(const gmap::Map& m, gmap::Player& p, float dt) {
    swing(m, dt);
    pushOut(m, p.feet, 0.3, 1.0);
}
double ray(const gmap::Map& m, V3 o, V3 dir, double maxT, int* ent) {
    double best = -1;
    for (int i = 0; i < (int)m.ents.size(); i++) {
        if (!isDoor(m, i) || !solid(m.ents[i])) continue;
        V3 h, q; slab(m, i, h, q);
        // the slab as a vertical rectangle: hinge..end along the floor, HEIGHT tall
        V3 u{q.x - h.x, 0, q.z - h.z};
        V3 n{-u.z, 0, u.x};
        double nl = std::hypot(n.x, n.z);
        if (nl < 1e-9) continue;
        n = n * (1 / nl);
        double dn = gmap::dot(dir, n);
        if (std::fabs(dn) < 1e-9) continue;
        double t = gmap::dot(h - o, n) / dn;
        if (t < 0 || t > maxT || (best >= 0 && t >= best)) continue;
        V3 p = o + dir * t;
        double along = gmap::dot(p - h, u) / gmap::dot(u, u);
        if (along < 0 || along > 1 || p.y < m.ents[i].pos.y || p.y > m.ents[i].pos.y + HEIGHT) continue;
        best = t;
        if (ent) *ent = i;
    }
    return best;
}
}  // namespace doors
