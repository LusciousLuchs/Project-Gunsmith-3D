// Gunsmith3D - NPCs alive on a map (see npcai.hpp).
#include "npcai.hpp"
#include "armor.hpp"
#include "doors.hpp"

#include <algorithm>
#include <cmath>

namespace npcai {
using gmap::V3;
namespace {
constexpr double PI_A = 3.14159265358979323846;
double rnd(unsigned& s) { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xFFFFFF) / double(0x1000000); }   // 0..1
V3 flat(V3 v) { v.y = 0; return v; }
double angleTo(double from, double to) { return std::remainder(to - from, 2 * PI_A); }
V3 eyeOf(const Actor& a) { return a.body.feet + V3{0, a.tune.standEye, 0}; }
bool clearLine(const gmap::Map& m, V3 a, V3 b) {
    V3 d = b - a; double L = gmap::len(d);
    if (L < 1e-6) return true;
    gmap::Hit h = gmap::rayMap(m, a, d * (1 / L), L);
    if (h.brush >= 0 && h.t < L - 0.15) return false;
    double dt = doors::ray(m, a, d * (1 / L), L);   // (a shut wood or steel door hides you; glass doesn't)
    return dt < 0 || dt >= L - 0.15;
}
// Is the way clear for its body (above the height it can step up) for `L` metres along `dir`?
bool wayClear(const Actor& a, const gmap::Map& m, V3 dir, double L) {
    double lift = a.tune.stepHeight + 0.03, hh = std::max(0.05, (a.tune.standH - lift) / 2 - 0.02);
    V3 half{a.tune.halfWidth + 0.02, hh, a.tune.halfWidth + 0.02};
    V3 c = a.body.feet + V3{0, lift + hh + 0.01, 0};
    gmap::Hit h = gmap::sweepBox(m, c, c + dir * L, half);
    return !h.startSolid && (h.brush < 0 || h.t >= 0.999);
}
V3 turnFlat(V3 d, double ang) { double c = std::cos(ang), s = std::sin(ang); return {d.x * c - d.z * s, 0, d.x * s + d.z * c}; }
// Which way to walk to get to `to` (flat, unit): straight there if nothing's in the way, else the smallest turn to one
// side that is clear, keeping to the side it chose for a while so it works its way round instead of dithering
V3 steer(Actor& a, const gmap::Map& m, V3 to, double dist, double dt) {
    a.sideT = std::max(0.0, a.sideT - dt);
    if (a.sideT <= 0) a.side = 0;
    double L = std::clamp(dist, 0.3, 1.0);
    if (wayClear(a, m, to, L)) return to;
    // the side it chose first (all the way round), then the other
    int first = a.side != 0 ? a.side : ((int(std::floor(a.home.x * 7 + a.home.z * 13)) & 1) ? 1 : -1);
    for (int sgn : {first, -first})
        for (int k = 1; k <= 6; k++) {
            V3 d = turnFlat(to, sgn * k * 25 * PI_A / 180);
            if (wayClear(a, m, d, L)) { a.side = sgn; a.sideT = 1.5; return d; }
        }
    return to;
}
}  // namespace

void hear(std::vector<Actor>& actors, V3 at, double radius) {
    for (Actor& a : actors)
        if (!a.dead && gmap::len(a.body.feet - at) <= radius) a.heard = true;
}

int npcCount(const gmap::Map& m) {
    int n = 0;
    for (auto& e : m.ents) if (e.type == "npc_spawn" && !e.ref.empty()) n += std::max(0, e.count);
    return n;
}

Actor spawn(const NpcDef& def, V3 pos, double yawDeg, const Design* gun) {
    Actor a;
    a.def = def;
    a.health = std::max(1.0, def.health);
    a.armor = std::max(0.0, def.armorBar);
    // its size: as tall as it stands, as wide as it reaches
    auto P = npcPose(def);
    double h = npcHeight(def) / 1000, w = 0.2;
    for (size_t i = 0; i < P.size(); i++)
        for (const double* p : {P[i].a, P[i].b}) w = std::max(w, std::max(std::fabs(p[0]), std::fabs(p[2])) / 1000 * 0.6);
    a.tune.standH = std::clamp(h, 0.3, 5.0);
    a.tune.standEye = a.tune.standH * 0.88;
    a.tune.halfWidth = std::clamp(w, 0.18, std::clamp(h * 0.5, 0.25, 0.9));   // (its body, not its legs' sprawl: a crawler squeezes through doors)
    a.tune.walk = a.tune.sprint = std::max(0.0, def.speed);
    a.tune.stepHeight = std::clamp(a.tune.standH * 0.25, 0.25, 0.6);   // (even a small one takes a stair)
    a.tune.accel = 10;
    a.body.feet = pos + V3{0, 0.02, 0};   // (a hair above the floor, or it starts stuck in it)
    a.body.yaw = yawDeg * PI_A / 180;
    a.body.eye = a.tune.standEye;
    a.home = pos;
    a.wanderTo = pos;
    if (gun && gun->npcGun.on) { a.hasGun = true; a.gun = gun->npcGun; a.cart = gun->breech.cart; }
    return a;
}

double bulletDamage(double massKg, double speed, bool vital, double armor) {
    double e = 0.5 * massKg * speed * speed;
    return e / 20 * (vital ? 3 : 1) * (1 - std::clamp(armor, 0.0, 1.0));
}

bool hurt(Actor& a, double damage, V3 dir) {
    if (a.dead) return false;
    a.health -= armor::absorb(a.armor, damage);   // its armour bar soaks most of it while it lasts
    a.hitFlash = 0.15;
    a.heard = true;   // it knows you're there now
    if (a.health > 0) return false;
    a.dead = true; a.deadT = 0;
    V3 f = flat(dir);
    a.fallDir = gmap::len(f) > 1e-6 ? gmap::norm(f) : V3{1, 0, 0};
    return true;
}

double height(const Actor& a) { return a.tune.standH; }
double fallAngle(const Actor& a) { return a.dead ? std::min(1.0, a.deadT / 0.7) * 1.45 : 0; }
nav::Agent agentOf(const Actor& a) { return {a.tune.halfWidth, a.tune.standH, a.tune.stepHeight}; }

NpcDef posed(const Actor& a) {
    if (a.dead) return a.def;
    return walking(a.def, a.walkPhase, gmap::len(flat(a.body.vel)));
}
NpcDef walking(const NpcDef& def, double walkPhase, double speed) {
    NpcDef n = def;
    if (speed < 0.05) return n;
    struct { double walkPhase; } a{walkPhase};
    // its legs: every chain of bones that reaches the ground swings from the bone where it hangs off the root
    auto P = npcPose(n);
    double h = npcHeight(n);
    std::vector<bool> swung(n.bones.size(), false);
    double amp = 28 * std::min(1.0, speed / 1.2);
    for (size_t i = 1; i < n.bones.size(); i++) {
        if (std::min(P[i].a[1], P[i].b[1]) > 0.15 * h) continue;
        int top = (int)i;
        while (top > 0 && n.bones[top].parent > 0) top = n.bones[top].parent;   // up to the bone on the root
        if (top <= 0 || swung[top]) continue;
        swung[top] = true;
        const NpcBone& b = n.bones[top];
        double off = (b.side < 0 ? 0 : PI_A) + (b.at > 0.5 ? PI_A : 0);   // left against right, front against back
        n.bones[top].bend += std::sin(a.walkPhase + off) * amp;
    }
    return n;
}

std::vector<BoneAt> bones(const Actor& a) {
    std::vector<BoneAt> out;
    NpcDef n = posed(a);
    auto P = npcPose(n);
    double yawDeg = a.body.yaw * 180 / PI_A;
    double th = (90 - yawDeg) * PI_A / 180, c0 = std::cos(th), s0 = std::sin(th);
    // fallen: turned about its feet, toward where the hit came from
    double fall = fallAngle(a);
    V3 axis = gmap::cross(V3{0, 1, 0}, a.fallDir);
    auto place = [&](const double q[3]) {
        V3 p{q[0] / 1000 * c0 + q[2] / 1000 * s0, q[1] / 1000, -q[0] / 1000 * s0 + q[2] / 1000 * c0};
        if (fall > 0) {   // rotate p about `axis` through the origin (Rodrigues)
            double c = std::cos(fall), s = std::sin(fall);
            V3 k = gmap::norm(axis);
            p = p * c + gmap::cross(k, p) * s + k * (gmap::dot(k, p) * (1 - c));
        }
        return a.body.feet + p;
    };
    for (size_t i = 0; i < P.size(); i++) out.push_back({place(P[i].a), place(P[i].b), npcBoneReach(n.bones[i]) / 1000, (int)i});
    return out;
}

void think(std::vector<Actor>& actors, const gmap::Map& map, V3 eye, V3 feet, bool alive, double dt, std::vector<Event>& out, unsigned& rng, nav::Cache* navs) {
    // route planning is the costly part: only a few NPCs plan a frame (taking turns), the rest keep their old route
    // a moment longer, so a crowd that all want new routes at once doesn't stall the game
    int plansLeft = 3;
    static size_t turn = 0;
    turn++;
    for (size_t n = 0; n < actors.size(); n++) {
        size_t idx = (n + turn) % actors.size();   // (a different one goes first each frame)
        Actor& a = actors[idx];
        a.hitFlash = std::max(0.0, a.hitFlash - dt);
        if (a.dead) { a.deadT += dt; a.body.vel = {0, 0, 0}; continue; }
        a.cool = std::max(0.0, a.cool - dt);
        V3 me = eyeOf(a);
        double dist = gmap::len(eye - me), flatDist = gmap::len(flat(feet - a.body.feet));
        // does it see you? within its sight, in front of it (or close enough to hear), nothing in between
        bool seen = false;
        if (alive && dist <= a.def.sight) {
            V3 to = flat(eye - me);
            double ang = std::fabs(angleTo(a.body.yaw, std::atan2(to.z, to.x)));
            if ((a.aware || ang < 1.2 || dist < 3) && clearLine(map, me, eye)) seen = true;
        }
        if (seen || (a.heard && alive)) {
            if (!a.aware) a.react = 0.35 + rnd(rng) * 0.45;
            if (!a.aware && seen)   // it calls out: the others near it that can see it know too
                for (Actor& o : actors)
                    if (&o != &a && !o.dead && !o.aware && gmap::len(o.body.feet - a.body.feet) < 12 && clearLine(map, eyeOf(o), me)) o.heard = true;
            a.aware = true; a.lastSeen = feet;
            a.sinceSeen = 0;
        } else {
            a.sinceSeen += dt;
            if (a.sinceSeen > 15 || !alive) a.aware = false;   // (it hunts for you a while before it gives up)
        }
        a.heard = false;
        a.react = std::max(0.0, a.react - dt);
        // where it wants to be
        const std::string& beh = a.def.behaviour;
        bool ranged = a.def.attack == "ranged" && a.hasGun, explode = a.def.attack == "explode";
        // (a shooter whose gun couldn't be found fights with its hands, at arm's length, not at its gun's range)
        double armReach = a.def.attack == "ranged" ? 1.2 : a.def.reach;
        double keep = ranged ? std::max(2.0, a.def.reach * 0.6) : explode ? 0.3 : std::max(0.5, armReach * 0.7);
        V3 target = a.body.feet; bool move = false;
        if (a.aware && beh != "stand") {
            target = a.lastSeen;
            if (beh == "guard" && gmap::len(flat(target - a.home)) > 8) target = a.home;   // it won't stray far from its post
            move = gmap::len(flat(target - a.body.feet)) > (seen ? keep : 0.6);
        } else if (!a.aware && beh == "wander") {
            a.wanderT -= dt;
            if (a.wanderT <= 0 || gmap::len(flat(a.wanderTo - a.body.feet)) < 0.5) {
                double r = 6 * std::sqrt(rnd(rng)), t = rnd(rng) * 2 * PI_A;
                V3 to = a.home + V3{std::cos(t) * r, 0, std::sin(t) * r};
                a.wanderT = 3 + rnd(rng) * 4;
                if (navs) {   // somewhere it can stand
                    const nav::Grid& G = navs->get(map, agentOf(a));
                    int n = nav::nearestNode(G, V3{to.x, a.body.feet.y, to.z}, 1.5);
                    if (n >= 0) to = G.at(n); else { to = a.wanderTo; a.wanderT = 0.5; }
                }
                a.wanderTo = to;
            }
            target = a.wanderTo; move = true;
        } else if (!a.aware && beh == "guard" && gmap::len(flat(a.home - a.body.feet)) > 0.8) { target = a.home; move = true; }
        // which way it walks (round what's in its way), and turns toward: that way, or you when it stands and sees you
        V3 way{0, 0, 0};
        a.planAge += dt;
        if (move) {
            V3 goTo = target;
            bool stop = false;
            bool onRoute = false;
            if (navs) {   // along its route
                const nav::Grid& G = navs->get(map, agentOf(a));
                a.replanT -= dt;
                bool moved = gmap::len(flat(target - a.pathGoal)) > 1.0 && a.planAge > 0.3;
                if ((!a.pathOk || a.replanT <= 0 || moved || a.stuckT > 1.0) && plansLeft > 0) {
                    plansLeft--;
                    a.pathOk = nav::findPath(G, a.body.feet, target, a.path, &a.pathReaches);
                    a.pathAt = 1; a.pathGoal = target; a.planAge = 0; a.stuckT = 0;
                    a.replanT = (a.aware ? 0.8 : 3.0) + rnd(rng) * 0.5;
                }
                if (a.pathOk) {
                    double close = std::max(0.35, G.cell);
                    int last = (int)a.path.size() - 1;
                    // on to the next point once it's at this one (or nearly, if it can walk straight on from here)
                    while (a.pathAt < last) {
                        double dd = gmap::len(flat(a.path[a.pathAt] - a.body.feet));
                        if (dd < 0.15 || (dd < close && nav::straightWalk(G, a.body.feet, a.path[a.pathAt + 1]))) a.pathAt++;
                        else break;
                    }
                    // (and on past a corner once it can walk straight to the point after)
                    if (a.pathAt < last && a.path[a.pathAt + 1].y > a.body.feet.y - G.agent.step && nav::straightWalk(G, a.body.feet, a.path[a.pathAt + 1])) a.pathAt++;
                    onRoute = true;
                    if (last <= 0 || a.pathAt > last) stop = !a.pathReaches;
                    else {
                        goTo = a.path[a.pathAt];
                        // the end of a route that can't get all the way: it waits there
                        if (a.pathAt == last && !a.pathReaches && gmap::len(flat(goTo - a.body.feet)) < close) stop = true;
                    }
                }
            }
            V3 to = flat(goTo - a.body.feet);
            double d = gmap::len(to);
            // on a route it follows it (the route already goes round things); otherwise it steps round what's in front
            if (!stop && d > 1e-6) way = onRoute ? to * (1 / d) : steer(a, map, to * (1 / d), d, dt);
            if (stop) move = false;
        }
        V3 face = move ? way : seen ? flat(feet - a.body.feet) : flat(target - a.body.feet);
        if (gmap::len(face) > 0.05) {
            double want = std::atan2(face.z, face.x), d = angleTo(a.body.yaw, want), turn = 4.5 * dt;
            a.body.yaw += std::clamp(d, -turn, turn);
        }
        gmap::PlayerInput in;
        if (move && gmap::len(way) > 0.05) {
            // it walks that way whichever way it faces (slower while it's still turning)
            double d = angleTo(a.body.yaw, std::atan2(way.z, way.x)), k = std::fabs(d) < 1.0 ? 1 : 0.5;
            if (beh == "wander" && !a.aware) k *= 0.5;
            in.forward = std::cos(d) * k; in.right = std::sin(d) * k;
        }
        gmap::playerStep(a.body, map, in, dt, a.tune);
        {   // doors: it can't walk through one, so it opens a shut one it walks into
            int door = -1;
            if (doors::pushOut(map, a.body.feet, a.tune.halfWidth + 0.05, a.tune.standH, &door) && move && !doors::isOpen(map, door))
                doors::toggle(map, door, a.body.feet);
        }
        a.stuckT = move && gmap::len(flat(a.body.vel)) < 0.15 ? a.stuckT + dt : 0;   // (pressing on and getting nowhere: plan again)
        a.walkPhase += gmap::len(flat(a.body.vel)) * dt * 2 * PI_A / std::max(0.4, a.tune.standH * 0.9);
        // attacking
        if (!alive || !a.aware || a.react > 0) { a.burstLeft = 0; continue; }
        if (explode) {
            if (a.fuse < 0 && flatDist <= std::max(0.8, a.def.reach * 0.8)) a.fuse = 0.6;
            if (a.fuse >= 0) {
                a.fuse -= dt;
                if (a.fuse <= 0) {
                    out.push_back({Event::Explode, (int)idx, me - V3{0, a.tune.standEye * 0.4, 0}, {0, 0, 0}, a.def.damage, std::max(2.0, a.def.reach * 2)});
                    a.health = 0; a.dead = true; a.deadT = 10;   // (nothing left to fall over)
                    out.push_back({Event::Died, (int)idx, a.body.feet, {0, 0, 0}, 0, 0});
                }
            }
        } else if (ranged) {
            if (!seen || dist > a.def.reach) { a.burstLeft = 0; continue; }
            if (a.burstLeft == 0 && a.cool <= 0) { a.burstLeft = std::max(1, a.gun.burst); a.shotT = 0; }
            if (a.burstLeft > 0) {
                a.shotT -= dt;
                if (a.shotT <= 0) {
                    // from about where it holds the gun, at your chest, as wide as its gun shoots (worse on the move)
                    V3 fwd{std::cos(a.body.yaw), 0, std::sin(a.body.yaw)};
                    V3 from = me - V3{0, a.tune.standEye * 0.22, 0} + fwd * 0.45;
                    V3 aim = gmap::norm((eye - V3{0, 0.35, 0}) - from);
                    V3 side = gmap::norm(gmap::cross(aim, {0, 1, 0})), up = gmap::cross(side, aim);
                    double spread = (a.gun.spread + (gmap::len(flat(a.body.vel)) > 0.3 ? 2 : 0)) * PI_A / 180;
                    double r = spread * std::sqrt(rnd(rng)) * 0.5, t = rnd(rng) * 2 * PI_A;
                    V3 dir = gmap::norm(aim + side * (std::tan(r) * std::cos(t)) + up * (std::tan(r) * std::sin(t)));
                    out.push_back({Event::Shot, (int)idx, from, dir, 0, 0});
                    a.shotT = 60.0 / std::max(1.0, a.gun.rpm);
                    if (--a.burstLeft == 0) a.cool = std::max(60.0 / std::max(1.0, a.gun.rpm), 0.45 + rnd(rng) * 0.7);
                }
            }
        } else {   // up close: when you're in reach
            if (a.cool <= 0 && flatDist <= std::max(0.6, armReach) && std::fabs(eye.y - me.y) < a.tune.standH + 1) {
                out.push_back({Event::Melee, (int)idx, me, gmap::norm(flat(feet - a.body.feet)), a.def.damage, 0});
                a.cool = 1.1;
            }
        }
    }
}

}  // namespace npcai
