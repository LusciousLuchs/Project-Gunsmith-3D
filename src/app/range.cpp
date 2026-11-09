// Gunsmith3D - the firing range: walk a map in first person with a gun you built, and shoot it.
//
// The gun in your hands is the same simulation as the test bench: its parts move, feed, extract and jam for real.
// Each shot fires the cartridge's bullet (or pellets) from the muzzle along the bore, flying with gravity and air drag,
// and leaves a mark where it lands. Recoil kicks the view by the cartridge's push against the gun's weight, a heavy gun
// swings more slowly, and one held in one hand kicks more than one in the shoulder.
#include "app/range.hpp"
#include "app/app.hpp"
#include "app/pose.hpp"
#include "app/audio.hpp"
#include "app/bench.hpp"
#include "app/fonts.hpp"
#include "app/gfx.hpp"
#include "app/keys.hpp"
#include "app/mapgfx.hpp"
#include "app/mapedit.hpp"
#include "app/npcedit.hpp"
#include "imgui.h"
#include "app/paths.hpp"
#include "app/render.hpp"
#include "app/settings.hpp"
#include "app/units.hpp"
#include "player.hpp"
#include "npcai.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <filesystem>

using gmap::V3;
double segSeg(V3 p1, V3 q1, V3 p2, V3 q2, double* tOut);   // the closest two segments come (below)

namespace {
struct Bullet { V3 p, v; double k = 0, travelled = 0, life = 0; double mass = 0.008; int npc = -1; double dmg = 0; };   // dmg: an NPC gun's set damage to you (0: by energy)   // npc: fired by that NPC (-1: by you)
struct Impact { V3 p, n; bool steel = false; double r = 0.005; };
struct Target { V3 c, n; double size = 0.6; };
struct Carried { Design d; std::unique_ptr<sim::Sim> S; };   // a gun you carry (the one in your hands lives in A.design / A.S)
struct WorldGun { Design d; std::unique_ptr<sim::Sim> S; V3 pos; double yaw = 0; };   // one lying on the ground

struct Range {
    gmap::Map map;
    std::vector<Carried> inv;            // the guns you carry, up to 4; inv[cur] is the one in your hands (empty here)
    int cur = -1;                        // -1: empty hands
    std::vector<WorldGun> ground;
    std::vector<npcai::Actor> actors;    // the NPCs on the map, alive or fallen
    nav::Cache nav;                      // their path-finding grids, one per NPC size
    unsigned rng = 12345;
    double health = 100;                 // yours
    bool dead = false;
    float hurtT = 0, deadT = 0;          // a red flash when you're hurt; how long since you died
    std::string feed; float feedT = 0;   // "Killed the ..." messages
    bool spawnerOpen = false;
    std::string prompt;                  // "F: ..." near something you can use
    gmap::Player pl;
    gmap::Stance want = gmap::Stance::Stand;
    float aim = 0;                       // 0 from the hip .. 1 aiming down the gun
    double lagYaw = 0, lagPitch = 0;     // the gun trailing the view: a heavy gun swings late
    double kickPitch = 0, kickYaw = 0, kickBack = 0, kickVel = 0;
    std::vector<Bullet> bullets;
    std::vector<Impact> impacts;
    std::vector<Target> targets;
    int shots = 0;
    std::string lastHit; float lastHitT = 0;
    double mass = 1;                     // the gun, kg
    bool twoHands = false;
    float t = 0;
    Matrix G = MatrixIdentity();         // the gun's drawing frame -> the map, this frame
    Vector3 eye{};
    double lastYaw = 0, lastPitch = 0;
    double sightY = 0, boreZ = 0;        // aiming: the eye's height over the gun (design mm, y down) and the bore's depth
    double zero = 0;                     // the bore tipped up so bullets meet the line of sight at the zero distance (rad)
};
Range R;

Vector3 rv(V3 v) { return {(float)v.x, (float)v.y, (float)v.z}; }
V3 vr(Vector3 v) { return {v.x, v.y, v.z}; }
float rnd(float a) { return (GetRandomValue(-10000, 10000) / 10000.f) * a; }

// The paper targets: on the practice range, on the plywood boards facing the firing line.
void findTargets() {
    R.targets.clear();
    for (auto& e : R.map.ents)
        if (e.type == "target_paper") {
            double y = e.yaw * DEG2RAD;
            R.targets.push_back({e.pos, V3{std::cos(y), 0, std::sin(y)}, 0.6});
        }
}

// The gun in the player's hands: where its drawing frame sits in the map this frame.
void placeGun(V3 eye, V3 f, V3 r, V3 u) {
    const Design& D = A.design;
    // aiming: the eye where the pose puts it. From the hip (in view, ready): the same hold, the gun lower and to the right
    BodyHold aimH = holdOf(D, HOLD_AIM);
    float a = R.aim;
    V2 eyeMM = aimH.eye;
    double eyeZ = aimH.eyeZ;
    // aiming: the eye looks over the top of the gun, the way you look over its sights, in line with the bore
    eyeMM.y = eyeMM.y + (R.sightY - eyeMM.y) * a;
    eyeZ = eyeZ + (R.boreZ - eyeZ) * a;
    eyeZ -= (1 - a) * (R.twoHands ? 75 : 110);
    eyeMM.y -= (1 - a) * (R.twoHands ? 70 : 95);
    eyeMM.x -= (1 - a) * (R.twoHands ? 0 : -60);   // a pistol comes in a little closer
    // the bore's way, in the gun's drawing frame (x right, y up)
    const auto& br = D.breech;
    V2 face = curMM(br.chamber.empty() ? br.breech : br.chamber, br.face), muz = curMM(br.chamber, br.muzzle);
    double bx = muz.x - face.x, by = -(muz.y - face.y), bl = std::hypot(bx, by);
    if (!br.on || !br.hasMuzzle || bl < 1) { bx = 1; by = 0; bl = 1; }
    bx /= bl; by /= bl;
    // turned with the lag and the kick
    double yawOff = R.lagYaw + R.kickYaw, pitchOff = R.lagPitch + R.kickPitch + R.zero;
    V3 fw = gmap::norm(f + r * std::tan(yawOff) + u * std::tan(pitchOff));
    V3 rt = gmap::norm(gmap::cross(fw, u));
    V3 up = gmap::cross(rt, fw);
    // gun frame basis: bore (bx, by, 0), its up (-by, bx, 0), and z across (+ = the gun's right)
    float B[3][3] = {{(float)bx, (float)-by, 0}, {(float)by, (float)bx, 0}, {0, 0, 1}};   // rows: gun x, y, z axes in (bore, up, right)
    // M3 maps gun-frame vectors to the map: v -> (v.bore) fw + (v.up) up + (v.right) rt
    float F[3][3] = {{(float)fw.x, (float)up.x, (float)rt.x}, {(float)fw.y, (float)up.y, (float)rt.y}, {(float)fw.z, (float)up.z, (float)rt.z}};
    float M3[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) { M3[i][j] = 0; for (int k = 0; k < 3; k++) M3[i][j] += F[i][k] * B[j][k]; }
    Matrix Rm = {M3[0][0], M3[0][1], M3[0][2], 0, M3[1][0], M3[1][1], M3[1][2], 0, M3[2][0], M3[2][1], M3[2][2], 0, 0, 0, 0, 1};
    // the eye point in the gun's drawing frame (U per mm), moved back by the kick
    Vector3 pe{(float)(eyeMM.x * U), (float)(-eyeMM.y * U), (float)(eyeZ * U)};
    float s = 1.0f / (1000 * U);   // drawing units -> metres
    V3 at = eye - fw * R.kickBack;
    R.G = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-pe.x, -pe.y, -pe.z), MatrixScale(s, s, s)), Rm), MatrixTranslate((float)at.x, (float)at.y, (float)at.z));
    // an animation moving the gun (inspecting it, a reload that tips it): about the firing hand
    if (A.S && sim::animPlaying(*A.S)) {
        AnimHands h = sim::animHandsNow(*A.S);
        if (gunMoved(h.gun)) {
            const auto& P = D.pose;
            int fi = P.firing.on ? D.bodyIndex(P.firing.part) : -1;
            Vector3 pv = P.firing.on ? W3(P.firing.at, fi >= 0 ? (float)(D.bodies[fi].z * U) : 0.f) : pe;
            R.G = MatrixMultiply(gunMoveMatrix(h.gun, pv), R.G);
        }
    }
}
V3 gunToMap(V2 mm, float zDraw) { return vr(Vector3Transform(W3(mm, zDraw), R.G)); }

void fireBullets(int chamber) {
    const auto& br = A.design.breech;
    const std::string& chb = chamber == 0 ? br.chamber : br.more[chamber - 1].chamber;
    V2 mz = chamber == 0 ? br.muzzle : br.more[chamber - 1].muzzle, fc = chamber == 0 ? br.face : br.more[chamber - 1].face;
    const Cartridge& cart = chamber == 0 || br.more[chamber - 1].ownCart == false ? br.cart : br.more[chamber - 1].cart;
    int ci = A.design.bodyIndex(chb);
    float zc = ci >= 0 ? bodyZ(ci) : 0;
    V3 m = gunToMap(curMM(chb, mz), zc), back = gunToMap(curMM(chb, fc), zc);
    V3 dir = gmap::norm(m - back);
    int n = std::max(1, cart.projectiles);
    double mass = (cart.shot() ? cart.pelletMass() : cart.bulletMass) / 1000.0;
    double dia = (cart.shot() ? cart.shotDia : cart.bulletDia) / 1000.0;
    double cd = cart.shot() ? 0.47 : 0.30;
    double k = 0.5 * 1.2 * cd * 3.14159 * dia * dia / 4 / std::max(1e-4, mass);
    float spread = n > 1 ? 0.025f : 0.0008f;   // pellets open into a pattern; a bullet goes nearly where it's pointed
    // the barrel's rifling: a bullet spun too slowly (or not at all) tumbles, goes wide and slows; shot spreads more if spun
    if (ci >= 0 && A.design.bodies[ci].barrel.on) {
        const BarrelDef& bd = A.design.bodies[ci].barrel;
        if (cart.shot()) { if (bd.rifled) spread *= 1.6f; }
        else if (!(cart.kind == "shotshell" && !bd.rifled)) {
            double sg = spinStability(cart, bd.twist, bd.rifled);
            if (sg < 1.0) { spread *= 25; k *= 3; }                       // tumbling: sideways through the air
            else if (sg < 1.4) spread *= 1 + (float)((1.4 - sg) / 0.4 * 3);   // marginal: groups open up
        }
    }
    V3 side = gmap::norm(gmap::cross(dir, {0, 1, 0})), upv = gmap::cross(side, dir);
    for (int i = 0; i < n; i++) {
        V3 d = gmap::norm(dir + side * rnd(spread) + upv * rnd(spread));
        R.bullets.push_back({m, d * cart.velocity, k, 0, 0, mass, -1});
    }
    // recoil: the cartridge's push against the gun's weight, felt less in the shoulder than in one hand
    double v = cart.impulse() / std::max(0.3, R.mass);
    double feel = R.twoHands ? 0.9 : 1.7;
    R.kickVel += v * feel * 0.045;
    R.kickYaw += rnd((float)(v * feel * 0.006));
    R.kickBack = std::min(0.08, R.kickBack + v * 0.006);
    R.shots++;
}

void stepBullets(double dt) {
    const int sub = 4;
    double h = dt / sub;
    for (auto& b : R.bullets) {
        for (int s = 0; s < sub && b.life >= 0; s++) {
            double sp = gmap::len(b.v);
            V3 acc = V3{0, -9.81, 0} - b.v * (b.k * sp);
            V3 nv = b.v + acc * h;
            V3 step = (b.v + nv) * (0.5 * h);
            double L = gmap::len(step);
            if (L > 1e-9 && b.npc < 0 && !R.actors.empty()) {   // your bullet: an NPC's bone in the way?
                int ha = -1, hb = -1; double bestS = 2;
                for (size_t ai = 0; ai < R.actors.size(); ai++) {
                    const auto& act = R.actors[ai];
                    if (act.dead && act.deadT > 30) continue;
                    V3 c = act.body.feet + V3{0, npcai::height(act) / 2, 0};   // (only near ones are looked at bone by bone)
                    if (gmap::len(c - b.p) > L + npcai::height(act) + 3) continue;
                    for (auto& nb : npcai::bones(act)) {
                        double sAlong;
                        if (segSeg(b.p, b.p + step, nb.a, nb.b, &sAlong) < nb.r && sAlong < bestS) { bestS = sAlong; ha = (int)ai; hb = nb.bone; }
                    }
                }
                if (ha >= 0) {
                    gmap::Hit mh = gmap::rayMap(R.map, b.p, step * (1 / L), L);
                    if (mh.brush < 0 || mh.t / L > bestS) {   // (and not a wall in front of it)
                        auto& act = R.actors[ha];
                        const NpcBone& bone = act.def.bones[hb];
                        double dmg = npcai::bulletDamage(b.mass, gmap::len(b.v), bone.vital, bone.armor);
                        bool wasDead = act.dead;
                        bool killed = npcai::hurt(act, dmg, step);
                        R.lastHit = (wasDead ? "Hit the fallen " : "Hit the ") + act.def.name + ": " + bone.name + (bone.vital ? " (weak spot!)" : "") + TextFormat("  -%.0f", dmg);
                        R.lastHitT = 2;
                        if (killed) { R.feed = "Killed the " + act.def.name; R.feedT = 3; }
                        R.impacts.push_back({b.p + step * bestS, gmap::norm(-step), false, 0.006});
                        b.life = -1;
                        break;
                    }
                }
            }
            if (L > 1e-9 && b.npc >= 0 && !R.dead) {   // their bullet: you in the way? (a body from your feet to your eye)
                V3 feet = R.pl.feet, eye = feet + V3{0, R.pl.eye + 0.12, 0};
                double sAlong;
                if (segSeg(b.p, b.p + step, feet + V3{0, 0.25, 0}, eye - V3{0, 0.1, 0}, &sAlong) < 0.28) {
                    gmap::Hit mh = gmap::rayMap(R.map, b.p, step * (1 / L), L);
                    if (mh.brush < 0 || mh.t / L > sAlong) {
                        double v2 = gmap::len(b.v);
                        double dmg = b.dmg > 0 ? b.dmg : 0.5 * b.mass * v2 * v2 / 60;   // (an NPC's gun hurts you less than its bullet's energy would say: a game, not a simulation)
                        R.health -= dmg; R.hurtT = 0.35;
                        b.life = -1;
                        break;
                    }
                }
            }
            if (L > 1e-9) {
                gmap::Hit hit = gmap::rayMap(R.map, b.p, step * (1 / L), L);
                if (hit.brush >= 0) {
                    V3 at = b.p + step * (hit.t / L);
                    int mat = R.map.brushes[hit.brush].faces[hit.face].mat;
                    const std::string& mn = gmap::materialNames()[std::clamp(mat, 0, (int)gmap::materialNames().size() - 1)];
                    bool steel = mn == "steel" || mn == "paint_orange" || mn == "corrugated";
                    R.impacts.push_back({at, hit.n, steel, std::max(0.004, b.k > 0 ? 0.0 : 0.0) + 0.005});
                    if (R.impacts.size() > 600) R.impacts.erase(R.impacts.begin());
                    double dist = b.travelled + hit.t;
                    // what was hit: a paper target's ring, a steel plate, or just the range
                    std::string what;
                    for (auto& t : R.targets) {
                        V3 o = at - t.c;
                        if (std::fabs(gmap::dot(o, t.n)) > 0.06) continue;
                        V3 inPlane = o - t.n * gmap::dot(o, t.n);
                        double rr = gmap::len(inPlane);
                        if (rr > t.size / 2) continue;
                        int ring = std::max(0, 10 - (int)(rr / (t.size / 2) * 10));
                        what = ring > 0 ? "Hit: " + std::to_string(ring) : "Hit the paper";
                    }
                    if (what.empty() && steel) {
                        what = "Ding!";
                        if (SFX.ok && A.sound) { SetSoundPitch(SFX.click, 2.4f); PlaySound(SFX.click); SetSoundPitch(SFX.click, 1.0f); }
                    }
                    if (!what.empty()) {
                        char d[32];
                        if (CFG.units == Units::Imperial) std::snprintf(d, sizeof d, "%.0f yd", dist * 1.09361); else std::snprintf(d, sizeof d, "%.0f m", dist);
                        R.lastHit = what + "  at " + d;
                        R.lastHitT = 3;
                    }
                    b.life = -1;
                    break;
                }
            }
            b.p = b.p + step; b.v = nv; b.travelled += L; b.life += h;
            if (b.life > 4 || b.p.y < -50) b.life = -1;
        }
    }
    R.bullets.erase(std::remove_if(R.bullets.begin(), R.bullets.end(), [](const Bullet& b) { return b.life < 0; }), R.bullets.end());
}

void reload() {
    if (!A.S) return;
    auto& S = *A.S;
    S.design.anims = A.design.anims;
    bool empty = sim::seatedMag(S) && sim::seatedMag(S)->rounds == 0 && !S.chambered;
    if (sim::playAnim(S, empty ? "reload_empty" : "reload") || (empty && sim::playAnim(S, "reload"))) return;
    if (sim::magwell(S)) {   // a fresh magazine in; if nothing was in the chamber, rack one in too
        sim::quickReload(S);
        if (!S.chambered) workAction();
        return;
    }
    if (sim::fillMagazines(S) > 0) { toast("Topped up."); return; }
    if (sim::handLoad(S)) return;
    toast("Open the action first (B), then reload.");
}
}  // namespace

bool inRange() { return A.screen == Screen::Range; }

// the gun now in your hands (A.design / A.S): how it sits, how heavy it is, where its sights are
static void armGun() {
    R.kickPitch = R.kickYaw = R.kickBack = R.kickVel = 0;
    A.shells.clear(); A.puffs.clear();
    if (!A.S || A.design.bodies.empty()) { R.twoHands = false; R.mass = 0.3; return; }
    R.twoHands = heldWith(A.design) == "two hands";
    for (auto& b : A.S->bodies) if (!b.gone && b.isRound && !b.isCase && b.rs == sim::RS::Loose) sim::retireBody(*A.S, b);   // (no bench here)
    R.mass = 0;
    for (auto& b : A.S->bodies) if (!b.gone && !b.isRound && !b.isCase && b.tmpl.empty() && b.m < 50) R.mass += b.m;
    R.mass = std::clamp(R.mass, 0.3, 15.0);
    {   // the line of sight: just over the top of the gun. Sighted in at 25 m (one hand) or 100 m (in the shoulder).
        const auto& br = A.design.breech;
        double top = 1e9;
        for (auto& b : A.design.bodies) if (!b.magazine) for (auto& p : b.pts) top = std::min(top, p.y);
        if (top > 1e8) top = br.face.y - 20;
        R.sightY = top - (R.twoHands ? 32 : 7);   // about where sights would put the eye over the gun
        int ci = A.design.bodyIndex(br.chamber);
        R.boreZ = ci >= 0 ? A.design.bodies[ci].z : 0;
        const BodyHold& aimH = A.design.pose.hold[HOLD_AIM];
        if (aimH.set) { R.sightY = aimH.eye.y; R.boreZ = aimH.eyeZ; }   // posed: the eye exactly where the player put it
        double h = std::max(0.0, (br.face.y - R.sightY) / 1000.0), zd = R.twoHands ? 100.0 : 25.0, v = std::max(50.0, br.cart.velocity);
        R.zero = std::atan(h / zd) + 0.5 * 9.81 * (zd / v) * (zd / v) / zd;
    }
}
// hands: put the gun in your hands back in its slot, and take slot k (-1: nothing)
static void holdSlot(int k) {
    if (R.cur >= 0 && R.cur < (int)R.inv.size()) { std::swap(A.design, R.inv[R.cur].d); std::swap(A.S, R.inv[R.cur].S); }
    else { A.design = Design(); A.S.reset(); }
    R.cur = k >= 0 && k < (int)R.inv.size() ? k : -1;
    if (R.cur >= 0) { std::swap(A.design, R.inv[R.cur].d); std::swap(A.S, R.inv[R.cur].S); }
    else { A.design = Design(); A.S.reset(); }
    armGun();
}
// a new gun into your hands (a free slot, or in place of the one you hold, which is dropped)
static void takeGun(const Design& d, std::unique_ptr<sim::Sim> S = nullptr) {
    if (R.inv.size() >= 4 && R.cur >= 0) {   // full: what you hold goes on the ground
        holdSlot(R.cur);   // (back in its slot)
        WorldGun w; w.d = std::move(R.inv[R.cur].d); w.S = std::move(R.inv[R.cur].S);
        w.pos = R.pl.feet + gmap::playerForward(R.pl) * 0.6; w.pos.y = R.pl.feet.y; w.yaw = R.pl.yaw * RAD2DEG;
        R.ground.push_back(std::move(w));
        R.inv.erase(R.inv.begin() + R.cur);
        R.cur = -1;
        A.design = Design(); A.S.reset();
    }
    holdSlot(-1);   // empty hands
    Carried c; c.d = d; syncBarrels(c.d);
    R.inv.push_back(std::move(c));
    int k = (int)R.inv.size() - 1;
    R.cur = k;
    std::swap(A.design, R.inv[k].d);
    if (S) A.S = std::move(S); else restartTest();
    A.mode = Mode::Test;
    armGun();
}
// the gun in your hands, put down on the ground in front of you
static void dropGun() {
    if (R.cur < 0) return;
    WorldGun w; w.d = A.design; w.S = std::move(A.S);
    V3 f = gmap::playerForward(R.pl); f.y = 0; f = gmap::norm(f);
    w.pos = R.pl.feet + f * 0.7; w.pos.y = R.pl.feet.y; w.yaw = R.pl.yaw * RAD2DEG + 90;
    R.ground.push_back(std::move(w));
    R.inv.erase(R.inv.begin() + R.cur);
    A.design = Design(); A.S.reset();
    R.cur = -1;
    holdSlot(R.inv.empty() ? -1 : 0);
    if (R.cur >= 0) {}   // (holdSlot put the first one in your hands)
}

// NPCs on the map: standing at their spawn points (they don't move or fight yet), their bones ready to be hit
static void placeNpcs() {
    R.actors.clear();
    for (auto& e : R.map.ents) {
        if (e.type != "npc_spawn" || e.ref.empty()) continue;
        NpcDef n;
        if (!npcByName(R.map, e.ref, n)) continue;
        Design gun; bool hasGun = n.attack == "ranged" && !n.gun.empty() && npcGunByName(R.map, n.gun, gun) && gun.npcGun.on;
        double y = e.yaw * DEG2RAD;
        V3 f{std::cos(y), 0, std::sin(y)}, side{-f.z, 0, f.x};
        for (int k = 0; k < std::min(e.count, 50); k++) {   // more than one: side by side in rows
            int row = k / 5, col = k % 5;
            V3 p = e.pos + side * ((col - std::min(e.count - row * 5, 5) / 2.0 + 0.5) * 1.1) - f * (row * 1.4);
            R.actors.push_back(npcai::spawn(n, p, e.yaw, hasGun ? &gun : nullptr));
        }
    }
    for (auto& a : R.actors) R.nav.get(R.map, npcai::agentOf(a));   // (their paths worked out now, not mid-fight)
}
// you died: everything back to how the map starts
static void restartLevel() {
    gmap::playerSpawn(R.pl, R.map);
    R.health = 100; R.dead = false; R.deadT = 0; R.hurtT = 0;
    R.bullets.clear();
    placeNpcs();
    R.lastYaw = R.pl.yaw; R.lastPitch = R.pl.pitch;
}
// the closest a bullet's step passes to a bone (segment to segment)
double segSeg(V3 p1, V3 q1, V3 p2, V3 q2, double* tOut) {
    V3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    double a = gmap::dot(d1, d1), e = gmap::dot(d2, d2), f = gmap::dot(d2, r), s, t;
    if (a < 1e-12) { s = 0; t = e > 1e-12 ? std::clamp(f / e, 0.0, 1.0) : 0; }
    else {
        double c = gmap::dot(d1, r);
        if (e < 1e-12) { t = 0; s = std::clamp(-c / a, 0.0, 1.0); }
        else {
            double b = gmap::dot(d1, d2), den = a * e - b * b;
            s = den > 1e-12 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = std::clamp(-c / a, 0.0, 1.0); } else if (t > 1) { t = 1; s = std::clamp((b - c) / a, 0.0, 1.0); }
        }
    }
    if (tOut) *tOut = s;
    return gmap::len((p1 + d1 * s) - (p2 + d2 * t));
}

void enterRange(const Design& gun, const gmap::Map& map) {
    if (!A.stash) A.stash = std::make_unique<Design>(A.design);
    A.screen = Screen::Range;
    A.showPause = A.showSettings = A.showBenchPicker = A.showRangePicker = false;
    A.mode = Mode::Test;
    mapGfxInit();
    R = Range();
    R.map = map;
    R.nav.clear();
    installMapNpcs(R.map);   // the NPCs this map brought with it
    mapBuildMeshes(R.map);
    gmap::playerSpawn(R.pl, R.map);
    findTargets();
    placeNpcs();
    A.design = Design(); A.S.reset();
    takeGun(gun);
    R.lastYaw = R.pl.yaw; R.lastPitch = R.pl.pitch;
    DisableCursor();
}
bool rangeSpawnerOpen() { return inRange() && R.spawnerOpen; }
// the gun spawner's list: your guns and the examples (guns made for NPCs aren't for you)
void rangeSpawnerWindow() {
    if (!R.spawnerOpen) return;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    ImGui::SetNextWindowPos({W / 2, H / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({460 * CFG.uiScale, 520 * CFG.uiScale});
    ImGui::Begin("Gun spawner", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    ImGui::TextDisabled("Take a gun. You can carry 4: the mouse wheel changes guns, G drops one.");
    ImGui::BeginChild("##sp", {0, -40 * CFG.uiScale});
    auto take = [&](const Design& d) { takeGun(d); R.spawnerOpen = false; toast("Took the " + d.name + "."); };
    ImGui::SeparatorText("Examples");
    for (auto& p : presetList()) {
        if (std::string(p.key) == "blank") continue;
        if (ImGui::Selectable(p.label)) { take(loadPreset(p.key)); break; }
    }
    ImGui::SeparatorText("Your blueprints");
    std::error_code ec;
    std::vector<std::string> files;
    for (auto& e : std::filesystem::directory_iterator(GUNS_DIR, ec)) if (e.path().extension() == ".json") files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        std::ifstream in(f); std::stringstream ss; ss << in.rdbuf();
        Design d;
        if (!designFromJson(ss.str(), d)) continue;
        std::string label = std::filesystem::path(f).stem().string();
        if (d.npcGun.on) { ImGui::TextDisabled("%s  (made for NPCs)", label.c_str()); continue; }
        if (ImGui::Selectable(label.c_str())) { take(d); break; }
    }
    ImGui::EndChild();
    if (ImGui::Button("Close (Esc)", {-1, 0}) || IsKeyPressed(KEY_ESCAPE)) R.spawnerOpen = false;
    ImGui::End();
}
void leaveRange() {
    EnableCursor();
    R.bullets.clear(); R.impacts.clear();
    R.inv.clear(); R.ground.clear(); R.actors.clear(); R.cur = -1; R.spawnerOpen = false;
    mapRestoreLook();
    rangeMapChanged();
}

void rangeUpdate(float dt, bool modal) {
    static bool holdFire = false, skipLook = false;
    if (modal) {   // paused: the mouse is free for the menu (once: freeing it also puts it in the middle), the trigger let go
        if (IsCursorHidden()) EnableCursor();
        if (A.S) { A.S->trigger = false; sim::setKey(*A.S, "fire", false); }
        holdFire = true; skipLook = true;
        return;
    }
    if (R.spawnerOpen) {   // choosing a gun at a spawner: the mouse is for the list
        if (IsCursorHidden()) EnableCursor();
        if (A.S) { A.S->trigger = false; sim::setKey(*A.S, "fire", false); }
        holdFire = true; skipLook = true;
        return;
    }
    if (!IsCursorHidden()) { DisableCursor(); skipLook = true; }
    if (IsKeyPressed(KEY_ESCAPE)) { A.showPause = true; return; }   // (the mouse is freed next frame, once)
    {   // your guns: the wheel changes them, G drops the one in your hands, F uses what's in front of you
        float wh = GetMouseWheelMove();
        if (wh != 0 && R.inv.size() > 1) {
            int n = (int)R.inv.size(), k = R.cur < 0 ? 0 : ((R.cur + (wh < 0 ? 1 : -1)) % n + n) % n;
            holdSlot(k);
        }
        if (actPressed(Act::DropGun) && R.cur >= 0) dropGun();
        R.prompt.clear();
        V3 me = R.pl.feet, fw = gmap::playerForward(R.pl);
        int spawner = -1, ground = -1;
        double best = 1e9;
        for (size_t i = 0; i < R.map.ents.size(); i++) {
            if (R.map.ents[i].type != "gun_spawner") continue;
            V3 d = R.map.ents[i].pos - me; d.y = 0;
            double l = gmap::len(d);
            if (l < 1.8 && l < best) { best = l; spawner = (int)i; }
        }
        for (size_t i = 0; i < R.ground.size(); i++) {
            V3 d = R.ground[i].pos - me; d.y = 0;
            double l = gmap::len(d);
            if (l < 1.6 && gmap::dot(gmap::norm(d), V3{fw.x, 0, fw.z}) > 0.2 && l < best) { best = l; ground = (int)i; spawner = -1; }
        }
        if (ground >= 0) R.prompt = std::string(keyName(keyOf(Act::Use))) + ": pick up the " + R.ground[ground].d.name;
        else if (spawner >= 0) R.prompt = std::string(keyName(keyOf(Act::Use))) + ": take a gun";
        if (actPressed(Act::Use)) {
            if (ground >= 0) {
                WorldGun w = std::move(R.ground[ground]);
                R.ground.erase(R.ground.begin() + ground);
                takeGun(w.d, std::move(w.S));
            } else if (spawner >= 0) { R.spawnerOpen = true; EnableCursor(); return; }
        }
    }
    // back from the menu: the click on Resume isn't a shot, and the mouse coming back to the middle isn't a turn
    if (holdFire && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) holdFire = false;
    R.t += dt;
    // looking
    Vector2 md = skipLook ? Vector2{0, 0} : GetMouseDelta();
    skipLook = false;
    double sens = 0.0021 * CFG.mouseSens * (1 - 0.45 * R.aim);
    R.pl.yaw += md.x * sens;
    R.pl.pitch = std::clamp(R.pl.pitch - md.y * sens * (CFG.invertMouse ? -1 : 1), -1.5, 1.5);
    // the gun trails the view and settles back: the heavier it is, the later it follows
    double dyaw = R.pl.yaw - R.lastYaw, dpitch = R.pl.pitch - R.lastPitch;
    R.lastYaw = R.pl.yaw; R.lastPitch = R.pl.pitch;
    double tau = 0.035 + R.mass * (R.twoHands ? 0.012 : 0.03);
    R.lagYaw = std::clamp(R.lagYaw - dyaw * 0.6, -0.25, 0.25) * std::exp(-dt / tau);
    R.lagPitch = std::clamp(R.lagPitch + dpitch * 0.6, -0.25, 0.25) * std::exp(-dt / tau);
    // moving
    gmap::PlayerInput in;
    in.forward = (actDown(Act::MoveFwd) ? 1 : 0) - (actDown(Act::MoveBack) ? 1 : 0);
    in.right = (actDown(Act::MoveRight) ? 1 : 0) - (actDown(Act::MoveLeft) ? 1 : 0);
    in.sprint = actDown(Act::Sprint) && R.aim < 0.2f;
    in.jump = actPressed(Act::Jump);
    if (actPressed(Act::Crouch)) R.want = R.want == gmap::Stance::Crouch ? gmap::Stance::Stand : gmap::Stance::Crouch;
    if (actPressed(Act::Prone)) R.want = R.want == gmap::Stance::Prone ? gmap::Stance::Stand : gmap::Stance::Prone;
    if (in.jump && R.want != gmap::Stance::Stand) { R.want = gmap::Stance::Stand; in.jump = false; }
    in.want = R.want;
    in.lean = (actDown(Act::LeanRight) ? 1 : 0) - (actDown(Act::LeanLeft) ? 1 : 0);
    if (R.dead) { in = gmap::PlayerInput(); in.want = gmap::Stance::Prone; }   // (down)
    gmap::playerStep(R.pl, R.map, in, dt);
    // aiming: right mouse; a heavy gun comes up more slowly
    bool aimWant = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    float rate = 1.f / (0.12f + (float)R.mass * 0.05f);
    R.aim = std::clamp(R.aim + (aimWant ? rate : -rate) * dt, 0.f, 1.f);
    // the gun: the trigger is the left mouse button; R reloads, B works the action
    if (A.S && R.dead) { A.S->trigger = false; sim::setKey(*A.S, "fire", false); sim::run(*A.S, dt, 1200); A.S->events.clear(); }
    if (A.S && !R.dead) {
        auto& S = *A.S;
        bool trig = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !holdFire;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !holdFire) S.pressed = true;   // (as on the bench: a fresh pull of the trigger)
        S.trigger = trig;
        sim::setKey(S, "fire", trig);
        if (actPressed(Act::Reload)) reload();
        if (actPressed(Act::WorkAction)) workAction();
        if (actPressed(Act::Inspect) && !sim::animPlaying(S)) {   // the player's own inspect animation, or a plain look over it
            S.design.anims = A.design.anims;
            if (!sim::playAnim(S, "inspect")) { S.design.anims["auto:inspect"] = defaultInspect(A.design); sim::playAnim(S, "auto:inspect"); }
        }
        // the magazine catch and your controls, as on the bench
        bool held[5] = {actDown(Act::MagRelease), actDown(Act::Control1), actDown(Act::Control2), actDown(Act::Control3), actDown(Act::Control4)};
        if (sim::hasControl(S, "magrelease")) { sim::setKey(S, "magrelease", held[0]); S.magRelease = false; }
        else S.magRelease = held[0];
        sim::setKey(S, "k1", held[1]); sim::setKey(S, "k2", held[2]); sim::setKey(S, "k3", held[3]); sim::setKey(S, "k4", held[4]);
        sim::run(S, dt, 1200);
        // a dropped magazine and the spent cases fly with the gun's frame here, so they're cleared once they've fallen away
        sim::tidy(S, dt, 1.2, 0.8, 2);
        const auto& br = A.design.breech;
        for (auto& e : S.events) {
            if (e.kind == sim::Ev::Bang) {
                A.flash = 0.06f;
                if (SFX.ok && A.sound) PlaySound(SFX.bang);
                npcai::hear(R.actors, R.pl.feet, 50);   // a gunshot carries: NPCs within 50 m come to look
                bool any = false;
                for (size_t k = 0; k <= br.more.size(); k++) {
                    bool fired = k < S.chambers.size() ? std::fabs(S.chambers[k].shotT - e.t) < 1e-9 : k == 0;
                    if (fired && (k == 0 ? br.hasMuzzle : br.more[k - 1].hasMuzzle)) { fireBullets((int)k); any = true; }
                }
                if (!any && br.hasMuzzle) fireBullets(0);
            }
            if ((e.kind == sim::Ev::Eject || e.kind == sim::Ev::EjectLive || e.kind == sim::Ev::Chambered) && SFX.ok && A.sound) PlaySound(SFX.clack);
            if (e.kind == sim::Ev::Click && SFX.ok && A.sound) PlaySound(SFX.click);
            if (e.kind == sim::Ev::Malfunction && S.malf != sim::Malf::None && SFX.ok && A.sound) PlaySound(SFX.click);
        }
        S.events.clear();
    }
    // the kick rises and settles like a spring
    R.kickPitch += R.kickVel * dt;
    R.kickVel += (-R.kickPitch * 160 - R.kickVel * 18) * dt;
    R.kickYaw *= std::exp(-dt * 9);
    R.kickBack *= std::exp(-dt * 14);
    {   // the NPCs: they look for you, move, and attack
        std::vector<npcai::Event> ev;
        double roll;
        V3 eye = gmap::playerEye(R.pl, R.map, &roll);
        npcai::think(R.actors, R.map, eye, R.pl.feet, !R.dead, dt, ev, R.rng, &R.nav);
        for (auto& e : ev) {
            if (e.kind == npcai::Event::Shot) {
                const auto& a = R.actors[e.actor];
                const Cartridge& c = a.cart;
                double mass = c.bulletMass / 1000, dia = c.bulletDia / 1000;
                double k = 0.5 * 1.2 * 0.3 * 3.14159 * dia * dia / 4 / std::max(1e-4, mass);
                R.bullets.push_back({e.from, e.dir * std::max(50.0, c.velocity), k, 0, 0, mass, e.actor, a.gun.damage});
                if (SFX.ok && A.sound) { float d = (float)gmap::len(e.from - eye); SetSoundVolume(SFX.bang, std::clamp(6.f / (d + 4), 0.08f, 0.8f)); PlaySound(SFX.bang); SetSoundVolume(SFX.bang, 1); }
            } else if (e.kind == npcai::Event::Melee && !R.dead) {
                R.health -= e.damage; R.hurtT = 0.4;
                R.kickYaw += 0.04; R.kickPitch += 0.02;   // (knocked about)
            } else if (e.kind == npcai::Event::Explode) {
                double d = gmap::len(eye - e.from);
                if (!R.dead && d < e.radius) { R.health -= e.damage * (1 - d / e.radius); R.hurtT = 0.5; }
                for (auto& o : R.actors) {   // and whatever else is near
                    V3 c = o.body.feet + V3{0, npcai::height(o) / 2, 0};
                    double dd = gmap::len(c - e.from);
                    if (&o != &R.actors[e.actor] && dd < e.radius) npcai::hurt(o, e.damage * (1 - dd / e.radius), c - e.from);
                }
                A.flash = 0.15f;
                if (SFX.ok && A.sound) PlaySound(SFX.bang);
            }
        }
        if (!R.dead && R.health <= 0) { R.health = 0; R.dead = true; R.deadT = 0; if (A.S) { A.S->trigger = false; sim::setKey(*A.S, "fire", false); } }
        if (R.dead) {
            R.deadT += dt;
            if (R.deadT > 1.2 && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))) restartLevel();
        }
        R.hurtT = std::max(0.f, R.hurtT - dt);
        R.feedT = std::max(0.f, R.feedT - dt);
    }
    stepBullets(dt);
    R.lastHitT = std::max(0.f, R.lastHitT - dt);
    A.flash = std::max(0.f, A.flash - dt);
    stepEffects(dt);
}

void rangeRender() {
    double roll;
    V3 eye = gmap::playerEye(R.pl, R.map, &roll);
    V3 f = gmap::playerForward(R.pl), r = gmap::norm(gmap::cross(f, {0, 1, 0})), u = gmap::cross(r, f);
    V3 up = u * std::cos(roll) + r * std::sin(roll);
    // a little breathing sway when aiming, more for a heavy gun held out in one hand
    double sway = R.aim * (R.twoHands ? 0.0012 : 0.0025) * (0.6 + R.mass * 0.25);
    R.lagYaw += std::sin(R.t * 1.1) * sway * 0.02;
    R.lagPitch += std::sin(R.t * 1.7 + 1) * sway * 0.015;
    Camera3D cam{};
    cam.position = rv(eye); cam.target = rv(eye + f); cam.up = rv(up); cam.fovy = CFG.fov - 12 * R.aim; cam.projection = CAMERA_PERSPECTIVE;
    A.cam = cam;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    mapDrawSky(R.map.look, 0, 0, W, H, (float)R.pl.pitch, cam.fovy);
    mapBeginViewport(cam, 0, 0, W, H);
    mapApplyLook(R.map.look);
    mapDrawWorld();
    // paper targets: rings on a white sheet
    for (auto& t : R.targets) {
        V3 side = gmap::norm(gmap::cross(t.n, {0, 1, 0})), tu = gmap::cross(side, t.n);
        auto disc = [&](double rad, Color c, double lift) {
            V3 o = t.c + t.n * lift;
            const int N = 40;
            for (int i = 0; i < N; i++) {
                double a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N;
                V3 p0 = o + side * (rad * std::cos(a0)) + tu * (rad * std::sin(a0)), p1 = o + side * (rad * std::cos(a1)) + tu * (rad * std::sin(a1));
                DrawTriangle3D(rv(o), rv(p0), rv(p1), c); DrawTriangle3D(rv(o), rv(p1), rv(p0), c);
            }
        };
        double h = t.size / 2;
        V3 o = t.c + t.n * 0.002;
        V3 a = o - side * h - tu * h, b = o + side * h - tu * h, c = o + side * h + tu * h, d = o - side * h + tu * h;
        Color paper{238, 234, 222, 255};
        DrawTriangle3D(rv(a), rv(b), rv(c), paper); DrawTriangle3D(rv(a), rv(c), rv(b), paper);
        DrawTriangle3D(rv(a), rv(c), rv(d), paper); DrawTriangle3D(rv(a), rv(d), rv(c), paper);
        for (int ring = 5; ring >= 1; ring--)
            disc(h * ring / 5.0 * 0.95, ring % 2 ? Color{30, 30, 32, 255} : paper, 0.003 + (5 - ring) * 0.0006);
        disc(h * 0.05, Color{200, 60, 40, 255}, 0.0065);
    }
    // where bullets landed
    for (auto& im : R.impacts) {
        V3 n = im.n, s = gmap::norm(gmap::cross(n, std::fabs(n.y) > 0.9 ? V3{1, 0, 0} : V3{0, 1, 0})), t = gmap::cross(n, s);
        V3 o = im.p + n * 0.008;
        Color c = im.steel ? Color{205, 205, 200, 255} : Color{22, 18, 16, 255};
        const int N = 8;
        for (int i = 0; i < N; i++) {
            double a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N;
            DrawTriangle3D(rv(o), rv(o + s * (im.r * std::cos(a0)) + t * (im.r * std::sin(a0))), rv(o + s * (im.r * std::cos(a1)) + t * (im.r * std::sin(a1))), c);
            DrawTriangle3D(rv(o), rv(o + s * (im.r * std::cos(a1)) + t * (im.r * std::sin(a1))), rv(o + s * (im.r * std::cos(a0)) + t * (im.r * std::sin(a0))), c);
        }
    }
    // NPCs standing on the map, gun spawners, guns on the ground
    for (auto& a : R.actors) {
        if (!a.dead) {
            drawNpcAt(npcai::posed(a), rv(a.body.feet), (float)(a.body.yaw * RAD2DEG), 0.001f, -1);
            if (a.hasGun && a.def.attack == "ranged") {   // its gun: a dark block held out in front (NPC guns are simple)
                V3 fwd{std::cos(a.body.yaw), 0, std::sin(a.body.yaw)};
                V3 g = a.body.feet + V3{0, a.tune.standEye * 0.78, 0} + fwd * 0.3;
                drawBox(rv(g + fwd * 0.12), {0.42f, 0.09f, 0.06f}, Color{34, 36, 40, 255}, MatrixRotateY((float)-a.body.yaw));
            }
            if (a.fuse >= 0) drawSphereLit(rv(a.body.feet + V3{0, npcai::height(a) * 0.5, 0}), (float)(0.25 + 0.15 * std::sin(R.t * 40)), Color{255, 120, 40, 255}, 0.5f);
        } else if (a.deadT < 9) {   // fallen: turned over about its feet toward where the hit came from (a burst one, nothing)
            V3 ax = gmap::cross(V3{0, 1, 0}, a.fallDir);
            drawNpcAt(a.def, rv(a.body.feet), (float)(a.body.yaw * RAD2DEG), 0.001f, -1, rv(ax), (float)npcai::fallAngle(a));
        }
    }
    for (auto& e : R.map.ents) {
        if (e.type != "gun_spawner") continue;
        Matrix Ry = MatrixRotateY((float)(-e.yaw * DEG2RAD));
        drawBox(rv(e.pos + V3{0, 0.45, 0}), {0.9f, 0.9f, 0.6f}, Color{60, 66, 74, 255}, Ry);
        drawBox(rv(e.pos + V3{0, 0.92, 0}), {0.95f, 0.06f, 0.65f}, Color{70, 120, 170, 255}, Ry);
        double y = e.yaw * DEG2RAD;
        drawBox(rv(e.pos + V3{0, 1.25, 0} + V3{std::cos(y), 0, std::sin(y)} * 0.05), {0.7f, 0.45f, 0.04f}, Color{40, 90, 130, 255}, Ry);
    }
    for (auto& g : R.ground)   // a gun case on the floor (the gun itself is drawn once it's in your hands)
        drawBox(rv(g.pos + V3{0, 0.06, 0}), {0.75f, 0.12f, 0.28f}, Color{58, 52, 44, 255}, MatrixRotateY((float)(-g.yaw * DEG2RAD)));
    // bullets in flight: a faint streak
    for (auto& b : R.bullets) DrawLine3D(rv(b.p), rv(b.p - b.v * 0.004), Color{255, 236, 190, 120});
    // the gun, in the hands
    placeGun(eye, f, gmap::norm(gmap::cross(f, up)), up);
    if (std::getenv("GUNSMITH_RANGEDBG")) {
        const auto& br = A.design.breech;
        V3 m = gunToMap(curMM(br.chamber, br.muzzle), 0), fc = gunToMap(curMM(br.chamber, br.face), 0);
        std::printf("eye %.3f %.3f %.3f  muzzle %.3f %.3f %.3f  face %.3f %.3f %.3f  f %.2f %.2f %.2f\n", eye.x, eye.y, eye.z, m.x, m.y, m.z, fc.x, fc.y, fc.z, f.x, f.y, f.z);
    }
    rlDrawRenderBatchActive();
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(R.G));
    if (R.cur >= 0) drawGunOnly();
    rlDrawRenderBatchActive();
    rlPopMatrix();
    EndMode3D();
    mapEndViewport();
    mapRestoreLook();
}

void rangeHud() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight(), cx = W / 2, cy = H / 2;
    if (R.aim < 0.5f) { DrawCircleV({cx, cy}, 2.5f, Color{255, 255, 255, 200}); DrawCircleLinesV({cx, cy}, 2.5f, Color{0, 0, 0, 120}); }
    {   // your health; a red edge when you're hurt; and dying
        if (R.hurtT > 0) {
            unsigned char al = (unsigned char)std::min(160.f, R.hurtT * 420);
            DrawRectangleGradientH(0, 0, (int)(W * 0.18f), (int)H, Color{170, 20, 16, al}, Color{170, 20, 16, 0});
            DrawRectangleGradientH((int)(W * 0.82f), 0, (int)(W * 0.18f), (int)H, Color{170, 20, 16, 0}, Color{170, 20, 16, al});
        }
        if (!R.actors.empty() || R.health < 100) {
            float bw = 220, bx = 24, by = H - 128;
            DrawRectangleRounded({bx, by, bw, 12}, 0.5f, 6, Color{20, 23, 28, 190});
            float f = (float)std::clamp(R.health / 100, 0.0, 1.0);
            Color hc = f > 0.5f ? Color{110, 210, 120, 230} : f > 0.25f ? Color{230, 190, 70, 240} : Color{230, 70, 60, 250};
            if (f > 0) DrawRectangleRounded({bx, by, bw * f, 12}, 0.5f, 6, hc);
            viewText(TextFormat("Health %.0f", std::max(0.0, R.health)), bx, by - 18, 12, Color{230, 233, 238, 220});
            int alive = 0; for (auto& a : R.actors) if (!a.dead) alive++;
            if (!R.actors.empty()) viewText(TextFormat("%d of %d NPCs left", alive, (int)R.actors.size()), W - 200, 54, 13, Color{230, 233, 238, 220});
        }
        if (R.feedT > 0) {
            float tw = viewTextWidth(R.feed.c_str(), 16);
            viewText(R.feed.c_str(), W - tw - 24, 76, 16, Color{255, 150, 120, (unsigned char)(255 * std::min(1.f, R.feedT))});
        }
        if (R.dead) {
            DrawRectangle(0, 0, (int)W, (int)H, Color{60, 0, 0, (unsigned char)std::min(170.f, R.deadT * 200)});
            const char* t = "You died";
            float tw = viewTextWidth(t, 40);
            viewText(t, cx - tw / 2, cy - 50, 40, Color{255, 230, 225, 255});
            if (R.deadT > 1.2) {
                const char* t2 = "Click to try again";
                float tw2 = viewTextWidth(t2, 16);
                viewText(t2, cx - tw2 / 2, cy + 10, 16, Color{255, 230, 225, 220});
            }
        }
    }
    {   // what you carry, and what you can use
        float y = H - 70 - 22 * (float)R.inv.size();
        for (size_t i = 0; i < R.inv.size(); i++) {
            bool on = (int)i == R.cur;
            std::string t = std::to_string(i + 1) + "  " + (on ? A.design.name : R.inv[i].d.name);
            viewText(t.c_str(), 24, y + 22 * i, on ? 15 : 13, on ? Color{255, 205, 60, 255} : Color{210, 214, 220, 190});
        }
        if (R.inv.empty()) viewText("Empty hands", 24, H - 92, 13, Color{210, 214, 220, 190});
        if (!R.prompt.empty()) {
            float tw = viewTextWidth(R.prompt.c_str(), 16);
            DrawRectangleRounded({cx - tw / 2 - 12, cy + 70, tw + 24, 30}, 0.4f, 6, Color{20, 23, 28, 190});
            viewText(R.prompt.c_str(), cx - tw / 2, cy + 76, 16, Color{255, 230, 160, 255});
        }
        if (R.lastHitT > 0 && !A.S) {
            float a = std::min(1.f, R.lastHitT);
            float tw = viewTextWidth(R.lastHit.c_str(), 18);
            viewText(R.lastHit.c_str(), cx - tw / 2, cy + 40, 18, Color{255, 220, 120, (unsigned char)(255 * a)});
        }
    }
    if (!A.S) return;
    auto& S = *A.S;
    // rounds: in the magazine, and one in the chamber
    char b[128];
    sim::Body* M = sim::seatedMag(S);
    int inMag = M ? M->rounds : 0;
    for (auto& x : S.bodies) if (x.builtInMag && !x.gone) inMag = x.rounds;
    std::snprintf(b, sizeof b, "%d%s", inMag, S.chambered ? " + 1" : "");
    float fs = 30;
    float w = viewTextWidth(b, fs);
    viewText(b, W - w - 36, H - 70, fs, Color{240, 242, 245, 240});
    const char* mag = M || S.chambered ? (M ? "in the magazine, + in the chamber" : "in the chamber") : "empty: R to reload";
    viewText(mag, W - viewTextWidth(mag, 12) - 36, H - 32, 12, Color{200, 204, 212, 200});
    std::snprintf(b, sizeof b, "%s   %d shot%s", A.design.name.c_str(), R.shots, R.shots == 1 ? "" : "s");
    viewText(b, 24, H - 32, 13, Color{235, 238, 242, 220});
    if (S.malf != sim::Malf::None) {
        std::string t = std::string("Jammed: ") + sim::malfName(S.malf) + ".  B works the action to clear it.";
        float tw = viewTextWidth(t.c_str(), 16);
        DrawRectangleRounded({cx - tw / 2 - 14, H * 0.68f, tw + 28, 34}, 0.4f, 6, Color{120, 30, 24, 210});
        viewText(t.c_str(), cx - tw / 2, H * 0.68f + 8, 16, Color{255, 236, 230, 255});
    }
    if (R.lastHitT > 0) {
        float a = std::min(1.f, R.lastHitT);
        float tw = viewTextWidth(R.lastHit.c_str(), 18);
        viewText(R.lastHit.c_str(), cx - tw / 2, cy + 40, 18, Color{255, 220, 120, (unsigned char)(255 * a)});
    }
    if (CFG.hints) {
        const char* help = "Left mouse fires, right mouse aims.  R reloads, B works the action, I inspects.  Wheel: change guns, G drops, F uses.  Esc for the menu.";
        float hw = viewTextWidth(help, 13);
        DrawRectangleRounded({cx - hw / 2 - 12, 14, hw + 24, 24}, 0.4f, 6, Color{20, 23, 28, 150});
        viewText(help, cx - hw / 2, 19, 13, Color{235, 238, 242, 230});
    }
}

void rangeMapChanged() { ME.version++; }   // the map meshes now hold the range's map: the editor builds its own again
