// Gunsmith3D - the firing range: walk a map in first person with a gun you built, and shoot it.
//
// The gun in your hands is the same simulation as the test bench: its parts move, feed, extract and jam for real.
// Each shot fires the cartridge's bullet (or pellets) from the muzzle along the bore, flying with gravity and air drag,
// and leaves a mark where it lands. Recoil kicks the view by the cartridge's push against the gun's weight, a heavy gun
// swings more slowly, and one held in one hand kicks more than one in the shoulder.
#include "app/range.hpp"
#include "app/doors.hpp"
#include "core/penetrate.hpp"
#include "app/fitbench.hpp"
#include "app/view.hpp"
#include "app/perf.hpp"
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
#include "armor.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <filesystem>
#include <functional>

using gmap::V3;
double segSeg(V3 p1, V3 q1, V3 p2, V3 q2, double* tOut);   // the closest two segments come (below)

namespace {
struct Bullet { V3 p, v; double k = 0, travelled = 0, life = 0; double mass = 0.008; int npc = -1; double dmg = 0; double dia = 0.009; };   // dia: the bullet's, m (how far it gets through wood)   // dmg: an NPC gun's set damage to you (0: by energy)   // npc: fired by that NPC (-1: by you)
struct Impact { V3 p, n; bool steel = false; double r = 0.005; };
struct Target { V3 c, n; double size = 0.6; };
struct Carried { Design d; std::unique_ptr<sim::Sim> S; std::vector<fit::Item> fit; int reserve = -1; };   // (fit: its attachments)   // a gun you carry (the one in your hands lives in A.design / A.S)
struct WorldGun { Design d; std::unique_ptr<sim::Sim> S; V3 pos; double yaw = 0; std::vector<fit::Item> fit; int reserve = -1; };   // one lying on the ground

struct Range {
    gmap::Map map;
    std::vector<Carried> inv;            // the guns you carry, up to 4; inv[cur] is the one in your hands (empty here)
    int cur = -1;                        // -1: empty hands
    std::vector<WorldGun> ground;
    std::vector<npcai::Actor> actors;    // the NPCs on the map, alive or fallen
    nav::Cache nav;                      // their path-finding grids, one per NPC size
    unsigned rng = 12345;
    double health = 100;                 // yours
    double armor = 0;                    // your armour bar (core/armor.hpp), filled by plates
    std::vector<int> plates;             // per map entity: armour plates still lying there
    std::vector<double> plateBack;       // ...and how long until taken ones come back (respawning spots)
    float armorT = 0;                    // a blue flash when you pick plates up
    bool dead = false;
    float hurtT = 0, deadT = 0;          // a red flash when you're hurt; how long since you died
    std::string feed; float feedT = 0;   // "Killed the ..." messages
    bool spawnerOpen = false;
    std::string prompt;                  // "F: ..." near something you can use
    gmap::Player pl;
    gmap::Stance want = gmap::Stance::Stand;
    float aim = 0;                       // 0 from the hip .. 1 aiming down the gun
    float point = 0;                     // point shooting (middle mouse held): the gun brought up under the eye without the
                                         // sights, quicker than aiming and steadier than the hip; 0 .. 1
    struct Case { V3 p, v, axis, spinAx; double spin = 0, life = 0, len = 0.02, dia = 0.01; Color col; bool rest = false; int bounces = 0; };
    std::vector<Case> cases;             // spent cases out in the world: flying, bouncing, then lying on the floor
    float wallU = 0;                     // up against a wall: the gun pulled back and its muzzle tipped up (0 .. 1)
    double wallBack = 0;                 // how far it's pulled back (mm)
    double gunReach = 0.6;               // how far the gun reaches in front of the eye (m), from its frontmost point
    bool holstered = false;              // put away (H): empty hands, nothing to fire
    float holsterU = 0;                  // 0 in your hands .. 1 put away
    int ready = 0;                       // carried at the ready (X): 0 no, 1 high ready (muzzle up), 2 low ready (down)
    int readyShown = 1;                  // which ready the gun is moving to or from
    float readyU = 0;                    // 0 level .. 1 fully at that ready
    bool readyForced = false;            // sprinting or crawling: the gun goes to low ready by itself
    double stanceDip = 0;                // the gun dipping as you get down or up (the eye's speed, eased)
    double lastEyeH = -1;
    double lagYaw = 0, lagPitch = 0;     // the gun trailing the view: a heavy gun swings late
    double kickPitch = 0, kickYaw = 0, kickBack = 0, kickVel = 0;
    std::vector<Bullet> bullets;
    std::vector<Impact> impacts;
    std::vector<Target> targets;
    int shots = 0;
    bool loadWhenOpen = false;           // R on a gun loaded by hand: the action is being opened, rounds go in as soon as it is
    std::string lastHit; float lastHitT = 0;
    double mass = 1;                     // the gun, kg
    bool twoHands = false;
    float t = 0;
    Matrix G = MatrixIdentity();         // the gun's drawing frame -> the map, this frame
    bool placed = false;                 // G has been worked out at least once (a shot before then has nowhere to come from)
    Vector3 eye{};
    double lastYaw = 0, lastPitch = 0;
    double sightY = 0, boreZ = 0;        // aiming: the eye's height over the gun (design mm, y down) and the bore's depth
    // an optic fitted to the gun in your hands: aiming puts your eye on its sight line (whatever the pose says)
    bool optic = false;
    OpticDef op; int opticItem = -1;     // (which fitted item it is: switched off, its reticle isn't lit)
    fit::P3 optRear, optFront;           // its lenses on the gun (design mm)
    double optEyeX = 0, zoom = 1;
    // what its other attachments do (worked out in armGun): kick, climb, loudness, flash, how far NPCs hear it, sway
    double recoilK = 1, riseK = 1, loudK = 1, flashK = 1, hearM = 50, swayK = 1;
    // lights and lasers on it (gun design mm: the beam's back and front ends), its bipod's feet, its bayonet's point
    struct Beam { int item; bool laser; fit::P3 a, b; double deg, lumens; unsigned color; };
    std::vector<Beam> beams;
    int bipodItem = -1; fit::P3 bipodFoot; bool rested = false;
    bool bayonet = false; fit::P3 tip; double stabDmg = 12, reach = 0.25;   // (no bayonet: a hit with the muzzle)
    float stabT = -1; bool stabHit = false;   // a stab under way: how long since it started
    bool freeMouse = false;              // Alt held: the mouse is free for the attachments' buttons
    // ammunition: magazines never run out, rounds do. The gun in your hands carries `reserve` rounds besides what's in it
    // (up to 10 magazines' worth); an ammo box fills it up again
    int reserve = 0, reserveMax = 0, roundsIn = -1;
    RenderTexture2D pip{}; bool pipOk = false;   // a scope's magnified picture
    double zero = 0;                     // the bore tipped up so bullets meet the line of sight at the zero distance (rad)
};
Range R;
// career mode: a range demonstration for a customer: rounds fired and paper hit, reported when it ends
struct Demo { bool on = false; std::string client; int shots = 0, hits = 0, need = 12, limit = 20; float overT = -1; std::function<void(int, int)> done; } gDemo;

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
    if (R.optic) eyeMM.x = eyeMM.x + (R.optEyeX - eyeMM.x) * a;   // an optic: the eye where its sight line and eye relief put it
    // point shooting: up toward the line of sight like aiming, but held a little low and to the right, under the eye,
    // looking over the gun rather than through its sights
    float pw = (1 - a) * R.point;
    float a2 = a + pw;
    eyeMM.y = eyeMM.y + (R.sightY - eyeMM.y) * a2 - pw * 55;
    eyeZ = eyeZ + (R.boreZ - eyeZ) * a2 - pw * 30;
    a = a2;   // (the hip's offsets below fade as it comes up)
    eyeZ -= (1 - a) * (R.twoHands ? 75 : 110);
    eyeMM.y -= (1 - a) * (R.twoHands ? 70 : 95);
    eyeMM.x -= (1 - a) * (R.twoHands ? 0 : -60);   // a pistol comes in a little closer
    // the bore's way, in the gun's drawing frame (x right, y up)
    const auto& br = D.breech;
    V2 face = br.face, muz = br.muzzle;   // (the bore as drawn, in battery: as the shot leaves it, fireBullets)
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
    double lunge = R.stabT >= 0 ? std::sin(std::clamp(R.stabT / 0.4f, 0.f, 1.f) * PI) * 0.2 : 0;   // a stab: thrust out and back
    V3 at = eye - fw * R.kickBack + fw * lunge;
    R.G = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-pe.x, -pe.y, -pe.z), MatrixScale(s, s, s)), Rm), MatrixTranslate((float)at.x, (float)at.y, (float)at.z));
    R.placed = true;
    {   // at the ready: the gun turned about the firing hand to the posed high or low ready (its muzzle angle), and
        // dipping a little as you crouch or go prone (the eye's speed)
        float u = R.readyU;
        double tilt = holdOf(D, R.readyShown == 1 ? HOLD_HIGH : HOLD_LOW).tilt;
        if (R.readyShown == 1 && tilt > -10) tilt = -40;   // (a high ready posed level: still up)
        if (R.readyShown == 2 && tilt < 10) tilt = 35;
        tilt = std::clamp(tilt, -80.0, 80.0);
        const auto& P = D.pose;
        bool high = R.readyShown == 1, stock = P.butt.on;
        // high ready: up about the firing hand, out to the right; low ready: muzzle down, a stocked gun about its butt
        // (it stays in the shoulder), a pistol about the hand
        // (a stocked gun's low ready seen from the eye right over it: shown at 60% of the posed angle, or it leaves the view)
        double shown = !high && stock ? tilt * 0.6 : tilt;
        double g[6] = {u * (high ? -30.0 : stock ? 0.0 : 30.0), u * (high ? -10.0 : stock ? 45.0 : -50.0), u * (high ? (stock ? 90.0 : 40.0) : stock ? 40.0 : -20.0),
                       -shown * u + R.stanceDip * 25, u * (high ? 0.0 : -12.0), u * (high ? 10.0 : -12.0) + R.stanceDip * 10 - (1 - R.aim) * R.point * 12};   // (point shooting: canted in a little)
        if (gunMoved(g)) {
            int fi = P.firing.on ? D.bodyIndex(P.firing.part) : -1, bi = stock ? D.bodyIndex(P.butt.part) : -1;
            Vector3 pv = P.firing.on ? W3(P.firing.at, fi >= 0 ? (float)(D.bodies[fi].z * U) : 0.f) : pe;
            if (!high && stock) pv = W3(P.butt.at, bi >= 0 ? (float)(D.bodies[bi].z * U) : 0.f);
            R.G = MatrixMultiply(gunMoveMatrix(g, pv), R.G);
        }
    }
    {   // its frontmost point (a muzzle device, a bayonet: whatever is furthest forward), from the eye: how far it reaches
        double x1 = -1e9;
        for (auto& b : D.bodies) for (auto& q : b.pts) x1 = std::max(x1, q.x);
        R.gunReach = std::max(0.15, (x1 - eyeMM.x) / 1000.0);
    }
    if (R.wallU > 0.001f) {   // a wall in front: pulled back toward you, the muzzle tipped up past it
        const auto& P = D.pose;
        int fi = P.firing.on ? D.bodyIndex(P.firing.part) : -1;
        Vector3 pv = P.firing.on ? W3(P.firing.at, fi >= 0 ? (float)(D.bodies[fi].z * U) : 0.f) : pe;
        double g[6] = {-R.wallBack, -20.0 * R.wallU, 0, 60.0 * R.wallU, 0, 0};
        R.G = MatrixMultiply(gunMoveMatrix(g, pv), R.G);
    }
    if (R.holsterU > 0) {   // put away: down and out of sight to the right, muzzle dropping, the way it goes to the holster or sling
        float h = R.holsterU * R.holsterU * (3 - 2 * R.holsterU);
        const auto& P = D.pose;
        int fi = P.firing.on ? D.bodyIndex(P.firing.part) : -1;
        Vector3 pv = P.firing.on ? W3(P.firing.at, fi >= 0 ? (float)(D.bodies[fi].z * U) : 0.f) : pe;
        double g[6] = {-120.0 * h, -420.0 * h, 160.0 * h, -70.0 * h, 0, 25.0 * h};
        R.G = MatrixMultiply(gunMoveMatrix(g, pv), R.G);
    }
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
    // along the barrel as it was when it fired: in battery, as drawn. (By the end of the frame a tilting or hinged barrel
    // has already moved to unlock, and its bore no longer points where the shot went.)
    if (!R.placed) return;
    V3 m = gunToMap(mz, zc), back = gunToMap(fc, zc);
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
    {   // the shooter: from the hip the gun isn't held steady on anything, so each shot goes off a few degrees either way;
        // point shooting holds it much closer; through the sights, only the sway (already in where the gun points)
        double pw = (1 - R.aim) * R.point;
        double hip = R.twoHands ? 0.035 : 0.045, pt = R.twoHands ? 0.010 : 0.013;   // (about 2 and 0.6 degrees)
        double err = (1 - R.aim) * ((1 - pw) * hip + pw * pt)
                   * (R.pl.stance == gmap::Stance::Prone ? 0.7 : R.pl.stance == gmap::Stance::Crouch ? 0.85 : 1.0);
        dir = gmap::norm(dir + side * rnd((float)err) + upv * rnd((float)err));
        side = gmap::norm(gmap::cross(dir, {0, 1, 0})); upv = gmap::cross(side, dir);
    }
    for (int i = 0; i < n; i++) {
        V3 d = gmap::norm(dir + side * rnd(spread) + upv * rnd(spread));
        R.bullets.push_back({m, d * cart.velocity, k, 0, 0, mass, -1, 0, dia});
        if (std::getenv("GUNSMITH_RANGEDBG")) std::printf("bullet from %.2f %.2f %.2f dir %.3f %.3f %.3f v %.0f\n", m.x, m.y, m.z, d.x, d.y, d.z, cart.velocity);
    }
    // recoil: the cartridge's push against the gun's weight, felt less in the shoulder than in one hand
    double v = cart.impulse() / std::max(0.3, R.mass);
    double feel = (R.twoHands ? 0.9 : 1.7) * (R.rested ? 0.6 : 1.0)   // (rested on a bipod: the ground takes some)
                * (R.pl.stance == gmap::Stance::Prone ? 0.7 : R.pl.stance == gmap::Stance::Crouch ? 0.85 : 1.0);   // (down low: steadier)
    R.kickVel += v * feel * 0.045 * R.recoilK * R.riseK;   // (a brake: less kick; a compensator: less climb)
    R.kickYaw += rnd((float)(v * feel * 0.006 * R.recoilK));
    R.kickBack = std::min(0.08, R.kickBack + v * 0.006 * R.recoilK);
    R.shots++;
    if (gDemo.on) gDemo.shots++;
}

// A bullet at `at` going along `dir` through `thick` m of wood: if it gets through, it's put just past the far side,
// slower and turned a little (more the more it lost), and true; false if it stops inside.
static bool throughWood(Bullet& b, V3 at, V3 dir, double thick) {
    double v0 = gmap::len(b.v), v1 = pen::exitSpeed(b.mass, b.dia, v0, thick);
    if (v1 <= 0) return false;
    float wob = (float)(0.04 * (1 - v1 / std::max(1.0, v0)));   // (about 2 degrees when it barely gets through)
    V3 side = gmap::norm(gmap::cross(dir, {0, 1, 0})), upv = gmap::cross(side, dir);
    V3 d = gmap::norm(dir + side * rnd(wob) + upv * rnd(wob));
    b.p = at + dir * (thick + 0.002);
    b.v = d * v1;
    return true;
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
                    double dt0 = doors::ray(R.map, b.p, step * (1 / L), L);
                    if ((mh.brush < 0 || mh.t / L > bestS) && (dt0 < 0 || dt0 / L > bestS)) {   // (and not a wall or a shut door in front of it)
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
                    double dt0 = doors::ray(R.map, b.p, step * (1 / L), L);
                    if ((mh.brush < 0 || mh.t / L > sAlong) && (dt0 < 0 || dt0 / L > sAlong)) {
                        double v2 = gmap::len(b.v);
                        double dmg = b.dmg > 0 ? b.dmg : 0.5 * b.mass * v2 * v2 / 60;   // (an NPC's gun hurts you less than its bullet's energy would say: a game, not a simulation)
                        R.health -= armor::absorb(R.armor, dmg); R.hurtT = 0.35;
                        b.life = -1;
                        break;
                    }
                }
            }
            if (L > 1e-9) {
                gmap::Hit hit = gmap::rayMap(R.map, b.p, step * (1 / L), L);
                int doorHit = -1;
                double doorT = doors::ray(R.map, b.p, step * (1 / L), L, &doorHit);
                if (doorT >= 0 && (hit.brush < 0 || doorT < hit.t)) {   // a shut door: steel stops it, wood if it's too slow to get through
                    // (no bullet hole: it would hang in the air once the door swings)
                    V3 dir = step * (1 / L);
                    if (R.map.ents[doorHit].ref != "metal") {
                        V3 dh, de; doors::slab(R.map, doorHit, dh, de);
                        V3 n = gmap::norm(V3{-(de.z - dh.z), 0, de.x - dh.x});
                        double thick = pen::DOOR_THICK / std::max(0.05, std::fabs(gmap::dot(n, dir)));
                        if (throughWood(b, b.p + dir * doorT, dir, thick)) continue;
                    } else { R.lastHit = "Ding! (a steel door)"; R.lastHitT = 1.2; }
                    b.life = -1;
                    break;
                }
                if (hit.brush >= 0) {
                    V3 at = b.p + step * (hit.t / L);
                    int mat = R.map.brushes[hit.brush].faces[hit.face].mat;
                    const std::string& mn = gmap::materialNames()[std::clamp(mat, 0, (int)gmap::materialNames().size() - 1)];
                    bool steel = mn == "steel" || mn == "paint_orange" || mn == "corrugated";
                    R.impacts.push_back({at, hit.n, steel, std::max(0.004, b.k > 0 ? 0.0 : 0.0) + 0.005});
                    if (R.impacts.size() > 600) R.impacts.erase(R.impacts.begin());
                    double dist = b.travelled + hit.t;
                    if (std::getenv("GUNSMITH_RANGEDBG")) std::printf("impact %s at %.2f %.2f %.2f\n", mn.c_str(), at.x, at.y, at.z);
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
                        if (gDemo.on && gDemo.shots <= gDemo.limit && b.npc < 0) gDemo.hits++;
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
                    if (pen::woodLike(mn)) {   // wood: through it if it's thin enough, out the other side slower
                        V3 dir = step * (1 / L);
                        double thick = pen::brushExit(R.map.brushes[hit.brush], at, dir);
                        if (throughWood(b, at, dir, thick)) {
                            R.impacts.push_back({at + dir * thick, dir, false, 0.005});   // (the hole it leaves coming out)
                            b.travelled += hit.t;
                            continue;
                        }
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

static int magCapacity(const Design& d) {
    int cap = 0;
    for (auto& b : d.bodies) if (b.magazine) cap = std::max(cap, b.magCap);
    if (d.breech.cylinder.on) cap = std::max(cap, d.breech.cylinder.chambers);   // (a revolver: a cylinder's worth)
    return std::max(1, cap);
}
// What's been loaded, so new rounds can be counted: which magazine is in the gun (its index; -1 none) and its rounds
// when last seen, and the rounds in built-in magazines (a tube, a fixed box)
static int gMagIdx = -1, gMagRounds = 0, gBuiltIn = 0, gCyl = 0;
static void seeAmmo(sim::Sim& S) {
    sim::Body* M = sim::seatedMag(S);
    gMagIdx = M ? (int)(M - &S.bodies[0]) : -1;
    gMagRounds = M ? M->rounds : 0;
    gBuiltIn = 0;
    for (auto& b : S.bodies) if (b.builtInMag && !b.gone) gBuiltIn += b.rounds;
    gCyl = sim::cylinderLive(S);
}
static void armAmmo(int reserve) {   // the gun in your hands: its reserve (-1: a fresh gun, full)
    R.reserveMax = std::max(10, 10 * magCapacity(A.design));
    R.reserve = reserve < 0 ? R.reserveMax : std::min(reserve, R.reserveMax);
    R.roundsIn = -1;
}
// after the gun has run: a magazine put in takes its rounds out of the reserve, and the one taken out gives back what
// was left in it; topping up a magazine or a tube takes what went in. Not enough spare: only what there was goes in.
static void countAmmo(sim::Sim& S) {
    if (R.roundsIn < 0) { seeAmmo(S); R.roundsIn = 0; return; }   // (a gun just taken up: what's in it is free)
    auto take = [&](int want, int& from) {   // the reserve pays for `want` rounds; the rest come back out of `from`
        int k = std::min(want, R.reserve);
        R.reserve -= k;
        from -= want - k;
    };
    sim::Body* M = sim::seatedMag(S);
    int idx = M ? (int)(M - &S.bodies[0]) : -1;
    if (idx != gMagIdx) {
        if (gMagIdx >= 0) R.reserve = std::min(R.reserveMax, R.reserve + gMagRounds);   // the old one out: its rounds back
        if (M) take(M->rounds, M->rounds);
    } else if (M && M->rounds > gMagRounds) take(M->rounds - gMagRounds, M->rounds);
    int built = 0;
    for (auto& b : S.bodies) if (b.builtInMag && !b.gone) built += b.rounds;
    if (built > gBuiltIn) {
        int over = built - gBuiltIn;
        for (auto& b : S.bodies) if (over > 0 && b.builtInMag && !b.gone) { int k = std::min(over, b.rounds); int before = b.rounds; take(k, b.rounds); over -= k; (void)before; }
    }
    if (sim::hasCylinder(S) && sim::cylinderLive(S) > gCyl) {   // a revolving cylinder filled: paid for, and short of rounds some chambers stay empty
        int want = sim::cylinderLive(S) - gCyl, k = std::min(want, R.reserve), cut = want - k;
        R.reserve -= k;
        for (int j = 0; j < (int)S.cyl.size() && cut > 0; j++) if (j != S.cylAt && S.cyl[j] == 1) { S.cyl[j] = 0; cut--; }
        if (cut > 0 && S.chambered) S.chambered = false;
    }
    seeAmmo(S);
}
// a round into every chamber that's open and empty, while there are rounds to spare (loaded by hand); how many went in
static int loadOpenChambers(sim::Sim& S) {
    int n = 0;
    for (int k = 0; k < sim::chamberCount(S) && R.reserve > 0; k++) {
        sim::useChamber(S, k);
        if (sim::handLoad(S)) { R.reserve--; n++; }
    }
    sim::useChamber(S, 0);
    return n;
}
void reload() {
    if (!A.S) return;
    if (R.reserve <= 0) { toast("Out of ammunition: find an ammo box."); return; }
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
    if (loadOpenChambers(S) > 0) return;   // (by hand, into the open action)
    if (!sim::chamberCount(S) || sim::chamberedCount(S) >= sim::chamberCount(S)) { toast("It's loaded."); return; }
    // loaded by hand and shut: open it, put the rounds in as soon as it's open, and close it again
    R.loadWhenOpen = true;
    workAction();
}
}  // namespace
static void handOffCases(sim::Sim& S);   // (below)
static void stepCases(float dt);

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
        R.recoilK = R.riseK = R.loudK = R.flashK = R.swayK = 1; R.hearM = 50;
        bool gripped = false, stocked = false;
        for (auto& it : A.fitItems) {
            const auto& at = it.att.attach;
            if (at.job == "muzzle") {
                if (at.muzzleKind == "brake") R.recoilK *= 0.7;
                else if (at.muzzleKind == "compensator") R.riseK *= 0.6;
                else if (at.muzzleKind == "suppressor") { R.loudK *= 0.35; R.flashK = 0; R.hearM = 15; }
                else if (at.muzzleKind == "flashhider") R.flashK *= 0.3;
            } else if (at.job == "grip" && !gripped) { R.swayK *= 0.75; gripped = true; }
            else if (at.job == "stock" && !stocked) { stocked = true; if (!R.twoHands) { R.twoHands = true; R.swayK *= 0.7; } }
        }
        R.beams.clear(); R.bipodItem = -1; R.rested = false; R.bayonet = false; R.stabDmg = 12; R.reach = 0.25;
        R.tip = {br.muzzle.x, br.muzzle.y, R.boreZ};
        for (size_t i = 0; i < A.fitItems.size(); i++) {
            const auto& it = A.fitItems[i];
            const auto& at = it.att.attach;
            if (at.job == "light" || at.job == "laser") {
                fit::P3 a = it.xf.apply({at.optic.rear.x, at.optic.rear.y, at.atZ}, it.spot()), b = it.xf.apply({at.optic.front.x, at.optic.front.y, at.atZ}, it.spot());
                R.beams.push_back({(int)i, at.job == "laser", a, b, at.beamDeg, at.lumens, beamColorOf(at)});
            } else if ((at.job == "bipod" && R.bipodItem < 0) || at.job == "bayonet") {
                bool bip = at.job == "bipod";
                double best = -1e18;
                fit::P3 pick{};
                for (auto& b : it.att.bodies)
                    for (auto& p : b.pts) {
                        fit::P3 q = it.xf.apply({p.x, p.y, b.z}, it.spot());
                        double v = bip ? q.y : q.x;   // the lowest point (y is down); the frontmost
                        if (v > best) { best = v; pick = q; }
                    }
                if (best < -1e17) continue;
                if (bip) { R.bipodItem = (int)i; R.bipodFoot = pick; }
                else if (!R.bayonet || pick.x > R.tip.x) {
                    R.bayonet = true; R.tip = pick;
                    double past = std::max(0.0, pick.x - br.muzzle.x);   // how far the blade sticks out
                    R.stabDmg = at.stab > 0 ? at.stab : std::clamp(35 + past * 0.25, 35.0, 90.0);
                    R.reach = 0.3 + past / 1000;
                }
            }
        }
        R.optic = false;
        for (auto& it : A.fitItems) {   // an optic on it: the eye goes on its sight line, behind it by its eye relief
            if (it.att.attach.job != "optic") continue;
            const auto& o = it.att.attach.optic;
            double z = it.att.attach.atZ;
            R.optRear = it.xf.apply({o.rear.x, o.rear.y, z}, it.spot());
            R.optFront = it.xf.apply({o.front.x, o.front.y, z}, it.spot());
            double dx = R.optFront.x - R.optRear.x, dy = R.optFront.y - R.optRear.y, dz = R.optFront.z - R.optRear.z, L = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (L < 1e-6) continue;
            double back = o.kind == "scope" ? o.eyeRelief : 70;
            R.optic = true; R.op = o;
            R.opticItem = (int)(&it - &A.fitItems[0]);
            R.sightY = R.optRear.y - dy / L * back; R.boreZ = R.optRear.z - dz / L * back; R.optEyeX = R.optRear.x - dx / L * back;
            R.zoom = o.mag;
            break;
        }
        double h = std::max(0.0, (br.face.y - R.sightY) / 1000.0), zd = R.twoHands ? 100.0 : 25.0, v = std::max(50.0, br.cart.velocity);
        R.zero = std::atan(h / zd) + 0.5 * 9.81 * (zd / v) * (zd / v) / zd;
        if (std::getenv("GUNSMITH_RANGEDBG")) std::printf("arm: face.y %.1f sightY %.1f posedAim %d twoHands %d zero %.4f\n", br.face.y, R.sightY, (int)A.design.pose.hold[HOLD_AIM].set, (int)R.twoHands, R.zero);
    }
}
// hands: put the gun in your hands back in its slot, and take slot k (-1: nothing)
static void holdSlot(int k) {
    if (R.cur >= 0 && R.cur < (int)R.inv.size()) R.inv[R.cur].reserve = R.reserve;
    if (R.cur >= 0 && R.cur < (int)R.inv.size()) { std::swap(A.design, R.inv[R.cur].d); std::swap(A.S, R.inv[R.cur].S); std::swap(A.fitItems, R.inv[R.cur].fit); }
    else { A.design = Design(); A.S.reset(); A.fitItems.clear(); }
    if (k != R.cur) { R.holstered = false; R.holsterU = 1; }   // (the new one comes up out of the holster)
    R.cur = k >= 0 && k < (int)R.inv.size() ? k : -1;
    if (R.cur >= 0) { std::swap(A.design, R.inv[R.cur].d); std::swap(A.S, R.inv[R.cur].S); std::swap(A.fitItems, R.inv[R.cur].fit); }
    else { A.design = Design(); A.S.reset(); A.fitItems.clear(); }
    armGun();
    armAmmo(R.cur >= 0 ? R.inv[R.cur].reserve : 0);
}
// a new gun into your hands (a free slot, or in place of the one you hold, which is dropped)
static void takeGun(const Design& d, std::unique_ptr<sim::Sim> S = nullptr, std::vector<fit::Item> fitted = {}, int reserve = -1) {
    if (R.inv.size() >= 4 && R.cur >= 0) {   // full: what you hold goes on the ground
        holdSlot(R.cur);   // (back in its slot)
        WorldGun w; w.d = std::move(R.inv[R.cur].d); w.S = std::move(R.inv[R.cur].S); w.fit = std::move(R.inv[R.cur].fit); w.reserve = R.inv[R.cur].reserve;
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
    A.fitItems = std::move(fitted);   // (a gun from the spawner comes bare: attachments are fresh each map)
    if (S) A.S = std::move(S); else restartTest();
    A.mode = Mode::Test;
    armGun();
    armAmmo(reserve);
}
// the gun in your hands, put down on the ground in front of you
static void dropGun() {
    if (R.cur < 0) return;
    WorldGun w; w.d = A.design; w.S = std::move(A.S); w.fit = std::move(A.fitItems); A.fitItems.clear(); w.reserve = R.reserve;
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
// the armour plates lying on the map, as it starts
static void placePlates() {
    R.armor = 0;
    R.plates.assign(R.map.ents.size(), 0);
    R.plateBack.assign(R.map.ents.size(), 0);
    for (size_t i = 0; i < R.map.ents.size(); i++) if (R.map.ents[i].type == "armor_plate") R.plates[i] = std::max(1, R.map.ents[i].count);
}
static void placeNpcs() {
    placePlates();
    R.actors.clear();
    for (auto& e : R.map.ents) {
        if (e.type != "npc_spawn" || e.ref.empty()) continue;
        NpcDef n;
        if (!npcByName(R.map, e.ref, n)) continue;
        Design gun; bool hasGun = n.attack == "ranged" && !n.gun.empty() && npcGunByName(R.map, n.gun, gun) && gun.npcGun.on;
        double y = e.yaw * DEG2RAD;
        V3 f{std::cos(y), 0, std::sin(y)}, side{-f.z, 0, f.x};
        for (int k = 0; k < e.count && (int)R.actors.size() < npcai::NPC_LIMIT; k++) {   // more than one: side by side in rows (up to the limit)
            int row = k / 5, col = k % 5;
            V3 p = e.pos + side * ((col - std::min(e.count - row * 5, 5) / 2.0 + 0.5) * 1.1) - f * (row * 1.4);
            R.actors.push_back(npcai::spawn(n, p, e.yaw, hasGun ? &gun : nullptr));
        }
    }
    for (auto& a : R.actors) R.nav.get(R.map, npcai::agentOf(a));   // (their paths worked out now, not mid-fight)
    int asked = npcai::npcCount(R.map);
    if (asked > npcai::NPC_LIMIT) { R.feed = TextFormat("This map asks for %d NPCs: only the first %d came (the most at once)", asked, npcai::NPC_LIMIT); R.feedT = 6; }
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

static std::vector<fit::Item> gBenchFit;   // the bench gun's attachments, set aside while on the range
void enterRange(const Design& gun, const gmap::Map& map) {
    if (!A.stash) { A.stash = std::make_unique<Design>(A.design); gBenchFit = A.fitItems; }
    A.fitItems.clear();
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
// an attachment crate: the gun in your hands on the fitting bench (the designer's view, the gun itself locked)
static bool gForceAim = false, gForceFree = false, gForcePoint = false;
void rangeShotExtras(const std::string& loadout, bool aim) {   // (screenshots) a loadout on the gun in hand; aiming held
    if (!loadout.empty() && R.cur >= 0) { loadLoadout(loadout); restartTest(); armGun(); R.roundsIn = -1; }
    gForceAim = aim;
    gForceFree = std::getenv("GUNSMITH_RANGEALT") != nullptr;   // (the attachments' buttons shown)
    if (std::getenv("GUNSMITH_RANGEPRONE")) R.want = gmap::Stance::Prone;   // (lying down)
    gForcePoint = std::getenv("GUNSMITH_RANGEPOINT") != nullptr;   // (point shooting held)
    if (const char* e = std::getenv("GUNSMITH_RANGEREADY")) { R.ready = std::atoi(e); R.readyShown = std::max(1, R.ready); }   // (1 high ready, 2 low ready)
    if (std::getenv("GUNSMITH_RANGECROUCH")) R.want = gmap::Stance::Crouch;
    if (const char* e = std::getenv("GUNSMITH_RANGETURN")) { R.pl.yaw += std::atof(e) * DEG2RAD; R.lastYaw = R.pl.yaw; }   // (screenshots) turned from the start, degrees
    if (const char* e = std::getenv("GUNSMITH_RANGEPITCH")) { R.pl.pitch = std::atof(e) * DEG2RAD; R.lastPitch = R.pl.pitch; }   // (looking up or down, degrees)
    if (const char* e = std::getenv("GUNSMITH_RANGEWALK")) { double fw = std::atof(e); R.pl.feet.x += std::cos(R.pl.yaw) * fw; R.pl.feet.z += std::sin(R.pl.yaw) * fw; }   // (and walked forward, m)
    if (const char* e = std::getenv("GUNSMITH_RANGEHOLSTER")) { R.holstered = true; R.holsterU = (float)std::atof(e); }   // (put away; how far along)
    if (std::getenv("GUNSMITH_RANGESTAB")) { R.stabT = 0; R.stabHit = false; }
}
void openRangeFitting() {
    A.rangeFit = true;
    A.screen = Screen::Workshop;
    A.mode = Mode::Test;
    EnableCursor();
    fitView();
    enterFitting();
}
void backToRange() {
    leaveFitting();
    A.rangeFit = false;
    A.screen = Screen::Range;
    A.mode = Mode::Test;
    restartTest();   // (built again: the attachments' weight is in it now)
    R.roundsIn = -1;   // (the rebuilt gun's rounds aren't new ammunition)
    armGun();
    R.lastYaw = R.pl.yaw; R.lastPitch = R.pl.pitch;
    DisableCursor();
}
// the gun spawner's list: your guns and the examples (guns made for NPCs aren't for you)
void rangeShotSetup(double yawDeg, double pitchDeg, double armorBar) {
    R.pl.yaw = yawDeg * DEG2RAD; R.pl.pitch = pitchDeg * DEG2RAD;
    R.lastYaw = R.pl.yaw; R.lastPitch = R.pl.pitch;
    R.armor = armorBar;
}
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
void rangeDemoStart(const std::string& client, int need, int limit, std::function<void(int, int)> done) {
    gDemo = Demo(); gDemo.on = true; gDemo.client = client; gDemo.need = need; gDemo.limit = limit; gDemo.done = std::move(done);
    R.shots = 0;
}
bool rangeDemoOver() {
    if (!gDemo.on || gDemo.shots < gDemo.limit) { gDemo.overT = -1; return false; }
    if (gDemo.overT < 0) gDemo.overT = (float)R.t;
    return R.t - gDemo.overT > 3.0;   // (the last rounds land, and a moment to read the result)
}
void leaveRange() {
    if (gDemo.on) { gDemo.on = false; if (gDemo.done) gDemo.done(std::min(gDemo.hits, 999), gDemo.shots); gDemo.done = nullptr; }
    EnableCursor();
    R.bullets.clear(); R.impacts.clear(); R.cases.clear();
    R.inv.clear(); R.ground.clear(); R.actors.clear(); R.cur = -1; R.spawnerOpen = false;
    A.fitItems = gBenchFit; gBenchFit.clear();   // (the bench gun's attachments back with it)
    mapRestoreLook();
    rangeMapChanged();
}

// lights and lasers: T switches them all (off if any is on, else all on)
static bool gadgetOn(const Range::Beam& b) { return b.item < (int)A.fitItems.size() && A.fitItems[b.item].on; }
static void toggleGadgets() {
    if (R.beams.empty()) { toast("No light or laser on this gun."); return; }
    bool any = false;
    for (auto& b : R.beams) any |= gadgetOn(b);
    for (auto& b : R.beams) if (b.item < (int)A.fitItems.size()) A.fitItems[b.item].on = !any;
    if (SFX.ok && A.sound) PlaySound(SFX.click);
}
// a stab with the bayonet (or a hit with the muzzle): the point thrust forward; the first NPC it meets is hurt
static void stepStab(float dt) {
    if (R.stabT < 0) return;
    R.stabT += dt;
    if (R.stabT > 0.4f) { R.stabT = -1; return; }
    if (R.stabHit || R.stabT < 0.08f || R.stabT > 0.26f || R.cur < 0) return;
    V3 tip = gunToMap({R.tip.x, R.tip.y}, (float)(R.tip.z * U));
    const auto& br = A.design.breech;
    V3 back = gunToMap({br.face.x, br.face.y}, (float)(R.boreZ * U));
    V3 dir = gmap::norm(tip - back);
    V3 from = tip - dir * 0.15, to = tip + dir * (R.reach - 0.2);
    if (std::getenv("GUNSMITH_RANGEDBG")) std::printf("stab tip %.2f %.2f %.2f reach %.2f dmg %.0f eye-to-tip %.2f\n", tip.x, tip.y, tip.z, R.reach, R.stabDmg, gmap::len(tip - gmap::playerEye(R.pl, R.map, nullptr)));
    int ha = -1, hb = -1; double bestS = 1e9;
    for (size_t ai = 0; ai < R.actors.size(); ai++) {
        if (R.actors[ai].dead) continue;
        for (auto& nb : npcai::bones(R.actors[ai])) {
            double sAlong;
            if (segSeg(from, to, nb.a, nb.b, &sAlong) < nb.r + 0.02 && sAlong < bestS) { bestS = sAlong; ha = (int)ai; hb = nb.bone; }
        }
    }
    if (ha < 0) return;
    R.stabHit = true;
    auto& act = R.actors[ha];
    const NpcBone& bone = act.def.bones[hb];
    double dmg = R.stabDmg * (bone.vital ? 2 : 1) * std::max(0.2, 1 - bone.armor / 100);
    bool killed = npcai::hurt(act, dmg, dir);
    npcai::hear(R.actors, R.pl.feet, 6);
    R.lastHit = std::string(R.bayonet ? "Stabbed the " : "Hit the ") + act.def.name + ": " + bone.name + (bone.vital ? " (weak spot!)" : "") + TextFormat("  -%.0f", dmg);
    R.lastHitT = 2;
    if (killed) { R.feed = "Killed the " + act.def.name; R.feedT = 3; }
    R.kickBack += 0.03;   // (it meets something)
    playAction(0.4, 1);
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
    R.freeMouse = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT) || gForceFree;   // Alt held: the mouse is yours (the attachments' buttons)
    if (R.freeMouse) { if (IsCursorHidden()) EnableCursor(); holdFire = true; skipLook = true; if (A.S) { A.S->trigger = false; sim::setKey(*A.S, "fire", false); } }
    else if (!IsCursorHidden()) { DisableCursor(); skipLook = true; }
    if (IsKeyPressed(KEY_ESCAPE)) { A.showPause = true; return; }   // (the mouse is freed next frame, once)
    {   // your guns: the wheel changes them, G drops the one in your hands, F uses what's in front of you
        float wh = GetMouseWheelMove();
        if (wh != 0 && R.optic && R.aim > 0.5f && R.op.magMax > R.op.mag) {   // aiming through a zoom scope: the wheel zooms
            R.zoom = std::clamp(R.zoom * std::pow(1.15, (double)wh), R.op.mag, R.op.magMax);
            wh = 0;
        }
        if (wh != 0 && R.inv.size() > 1) {
            int n = (int)R.inv.size(), k = R.cur < 0 ? 0 : ((R.cur + (wh < 0 ? 1 : -1)) % n + n) % n;
            holdSlot(k);
        }
        if (actPressed(Act::DropGun) && R.cur >= 0) dropGun();
        R.prompt.clear();
        V3 me = R.pl.feet, fw = gmap::playerForward(R.pl);
        if (R.plates.size() != R.map.ents.size()) placePlates();
        for (size_t i = 0; i < R.map.ents.size(); i++) {   // armour plates: walk over them to pick them up
            const auto& e = R.map.ents[i];
            if (e.type != "armor_plate") continue;
            if (R.plateBack[i] > 0 && (R.plateBack[i] -= dt) <= 0) { R.plateBack[i] = 0; R.plates[i] = std::max(1, e.count); }   // back again
            if (R.plates[i] <= 0 || R.dead) continue;
            V3 d = e.pos - me;
            if (std::hypot(d.x, d.z) > 0.9 || std::fabs(d.y) > 1.2) continue;
            int n = armor::takePlates(R.armor, R.plates[i]);
            if (n == 0) { R.prompt = "Your armour is full"; continue; }
            R.plates[i] -= n;
            if (R.plates[i] == 0 && e.respawn > 0) R.plateBack[i] = e.respawn;
            R.armorT = 0.5f;
            R.feed = TextFormat("Armour plate%s: armour %.0f", n > 1 ? "s" : "", R.armor); R.feedT = 2.5f;
            playAction(0.6, 2);   // (a heavy plate slotting in)
        }
        int spawner = -1, ground = -1, crate = -1;
        double best = 1e9;
        for (size_t i = 0; i < R.map.ents.size(); i++) {
            if (R.map.ents[i].type != "gun_spawner" && R.map.ents[i].type != "weapon_crate") continue;   // (a crate of guns: the same)
            V3 d = R.map.ents[i].pos - me; d.y = 0;
            double l = gmap::len(d);
            if (l < 1.8 && l < best) { best = l; spawner = (int)i; }
        }
        for (size_t i = 0; i < R.map.ents.size(); i++) {   // attachment crates
            if (R.map.ents[i].type != "attachment_crate") continue;
            V3 d = R.map.ents[i].pos - me; d.y = 0;
            double l = gmap::len(d);
            if (l < 1.6 && l < best) { best = l; crate = (int)i; spawner = -1; }
        }
        int ammo = -1;
        for (size_t i = 0; i < R.map.ents.size(); i++) {   // ammo boxes: fill up every gun you carry
            if (R.map.ents[i].type != "ammo_box") continue;
            V3 d = R.map.ents[i].pos - me; d.y = 0;
            double l = gmap::len(d);
            if (l < 1.6 && l < best) { best = l; ammo = (int)i; spawner = -1; crate = -1; }
        }
        for (size_t i = 0; i < R.ground.size(); i++) {
            V3 d = R.ground[i].pos - me; d.y = 0;
            double l = gmap::len(d);
            if (l < 1.6 && gmap::dot(gmap::norm(d), V3{fw.x, 0, fw.z}) > 0.2 && l < best) { best = l; ground = (int)i; spawner = -1; }
        }
        if (ground >= 0) ammo = -1;
        if (ground >= 0) R.prompt = std::string(keyName(keyOf(Act::Use))) + ": pick up the " + R.ground[ground].d.name;
        else if (ammo >= 0) R.prompt = std::string(keyName(keyOf(Act::Use))) + (R.inv.empty() ? ": ammunition (no gun to fill)" : R.reserve >= R.reserveMax ? ": ammunition (you're full)" : ": take ammunition");
        else if (spawner >= 0) R.prompt = std::string(keyName(keyOf(Act::Use))) + ": take a gun";
        else if (crate >= 0) R.prompt = std::string(keyName(keyOf(Act::Use))) + (R.cur >= 0 ? ": fit attachments to the " + A.design.name : ": attachments (hold a gun first)");
        int door = (ground < 0 && ammo < 0 && spawner < 0 && crate < 0) ? doors::facing(R.map, gmap::playerEye(R.pl, R.map), gmap::playerForward(R.pl)) : -1;
        if (door >= 0) R.prompt = std::string(keyName(keyOf(Act::Use))) + (doors::isOpen(R.map, door) ? ": shut the door" : ": open the door");
        if (door >= 0 && actPressed(Act::Use)) doors::toggle(R.map, door, R.pl.feet);
        else if (actPressed(Act::Use)) {
            if (ground >= 0) {
                WorldGun w = std::move(R.ground[ground]);
                R.ground.erase(R.ground.begin() + ground);
                takeGun(w.d, std::move(w.S), std::move(w.fit), w.reserve);
            } else if (ammo >= 0) {
                if (R.cur >= 0) R.inv[R.cur].reserve = R.reserve;
                int got = 0;
                for (auto& c : R.inv) {   // every gun you carry: up to ten magazines' worth (each its own cartridge)
                    int mx = std::max(10, 10 * magCapacity(&c == &R.inv[std::max(0, R.cur)] && R.cur >= 0 ? A.design : c.d));
                    if (c.reserve < mx) { got += mx - std::max(0, c.reserve); c.reserve = mx; }
                }
                if (R.cur >= 0) { got += 0; R.reserve = R.inv[R.cur].reserve; R.reserveMax = std::max(10, 10 * magCapacity(A.design)); }
                if (got > 0) { R.feed = TextFormat("Ammunition: +%d rounds", got); R.feedT = 2.5f; playAction(0.3, 1.5); }
            } else if (spawner >= 0) { R.spawnerOpen = true; EnableCursor(); return; }
            else if (crate >= 0 && R.cur >= 0) { openRangeFitting(); return; }
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
    doors::step(R.map, R.pl, dt);
    // aiming: right mouse; a heavy gun comes up more slowly
    bool aimWant = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !R.freeMouse;
    // carrying it at the ready (X: high ready, low ready, back); sprinting or crawling takes it to low ready by itself.
    // Aiming or pulling the trigger brings it back up first.
    if (actPressed(Act::Ready) && !R.dead) R.ready = (R.ready + 1) % 3;
    if (actPressed(Act::Holster) && !R.dead && R.cur >= 0) R.holstered = !R.holstered;
    if (R.holstered && (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) && !R.freeMouse) R.holstered = false;   // (a click draws it)
    {   // putting it away or drawing it: about half a second, slower for a heavy gun
        float hrate = 1.f / (0.35f + (float)R.mass * 0.08f);
        R.holsterU = std::clamp(R.holsterU + (R.holstered ? hrate : -hrate) * dt, 0.f, 1.f);
    }
    bool crawling = R.pl.stance == gmap::Stance::Prone && std::hypot(R.pl.vel.x, R.pl.vel.z) > 0.25;
    bool sprinting = in.sprint && in.forward > 0 && std::hypot(R.pl.vel.x, R.pl.vel.z) > 4.0;
    R.readyForced = crawling || sprinting;
    bool firePress = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !R.freeMouse;
    bool pointWant = (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || actDown(Act::PointShoot)) && !R.freeMouse && !aimWant;
    if (gForcePoint) pointWant = true;
    if (R.holsterU > 0.05f) aimWant = pointWant = false;   // (put away, or still coming out)
    if ((aimWant || firePress || pointWant) && !R.readyForced) R.ready = 0;
    int readyWant = R.readyForced ? 2 : R.ready;
    if (readyWant) { if (R.readyU < 0.05f) R.readyShown = readyWant; else if (readyWant != R.readyShown) readyWant = 0; }   // (through level to the other one)
    float rrate = 1.f / (0.22f + (float)R.mass * 0.04f);
    R.readyU = std::clamp(R.readyU + (readyWant ? rrate : -rrate) * dt, 0.f, 1.f);
    if (readyWant) aimWant = pointWant = false;
    {   // point shooting comes up fast: about twice as quick as the sights
        float prate = 1.f / (0.06f + (float)R.mass * 0.025f);
        R.point = std::clamp(R.point + (pointWant ? prate : -prate) * dt, 0.f, 1.f);
    }
    {   // getting down or up: the gun dips and swings back
        double eh = R.pl.eye;
        double v = R.lastEyeH < 0 || dt <= 0 ? 0 : (eh - R.lastEyeH) / dt;
        R.lastEyeH = eh;
        R.stanceDip += (std::clamp(v, -3.0, 3.0) * 0.12 - R.stanceDip) * std::min(1.0, (double)dt * 10);
    }
    float rate = 1.f / (0.12f + (float)R.mass * 0.05f);
    R.aim = std::clamp(R.aim + (aimWant ? rate : -rate) * dt, 0.f, 1.f);
    if (gForceAim) R.aim = 1;
    // the gun: the trigger is the left mouse button; R reloads, B works the action
    if (A.S && R.dead) { A.S->trigger = false; sim::setKey(*A.S, "fire", false); { perf::Scope t(perf::Physics); perf::substeps(sim::run(*A.S, dt, 1200)); } A.S->events.clear(); }
    if (A.S && !R.dead) {
        auto& S = *A.S;
        bool atReady = R.readyU > 0.15f || R.holsterU > 0.05f || R.wallU > 0.3f;   // (the muzzle off the target: the finger stays off the trigger until it's back)
        static bool waitRelease = false;
        if (atReady && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) waitRelease = true;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) waitRelease = false;
        bool trig = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !holdFire && !atReady && !waitRelease;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !holdFire && !atReady) S.pressed = true;   // (as on the bench: a fresh pull of the trigger)
        if (std::getenv("GUNSMITH_RANGEAUTOFIRE")) {   // (tests) pull the trigger every 0.4 s by itself
            static float at = 0, rl = 0; at += dt; rl += dt;
            trig = std::fmod(at, 0.4f) < 0.1f && !sim::animPlaying(S);   // (hands off the trigger while working the action)
            if (!std::getenv("GUNSMITH_RANGEHIP") && !gForcePoint) R.aim = 1;   // (or from the hip / point shooting, to measure them)
            sim::Body* M = sim::seatedMag(S);
            if (sim::chamberedCount(S) == 0 && (!M || M->rounds == 0) && rl > 1.5f && !sim::animPlaying(S)) { reload(); rl = 0; }
            else if (!S.chambered && M && M->rounds > 0 && !sim::hasCylinder(S) && sim::chamberedCount(S) == 0 && S.t - S.fireT > 0.8 && rl > 0.8f && !sim::animPlaying(S)) { workAction(); rl = 0; }   // (still shut on the case: work it)   // (worked by hand: the bolt, the pump)
            static bool was = false;
            if (trig && !was) S.pressed = true;   // (each new pull)
            was = trig;
        }
        S.trigger = trig;
        sim::setKey(S, "fire", trig);
        if (R.loadWhenOpen) {   // opening it to load by hand: in they go once it's open
            loadOpenChambers(S);
            if (!sim::animPlaying(S)) R.loadWhenOpen = false;
        }
        bool away = R.holsterU > 0.05f;   // (put away: hands off it)
        if (actPressed(Act::Reload) && !away) reload();
        if (actPressed(Act::WorkAction) && !away) workAction();
        if (actPressed(Act::Gadgets)) toggleGadgets();
        if (actPressed(Act::Melee) && R.stabT < 0 && !away) { R.stabT = 0; R.stabHit = false; }
        if (actPressed(Act::Inspect) && !sim::animPlaying(S) && !away) {   // the player's own inspect animation, or a plain look over it
            S.design.anims = A.design.anims;
            if (!sim::playAnim(S, "inspect")) { S.design.anims["auto:inspect"] = defaultInspect(A.design); sim::playAnim(S, "auto:inspect"); }
        }
        // the magazine catch and your controls, as on the bench
        bool held[5] = {actDown(Act::MagRelease), actDown(Act::Control1), actDown(Act::Control2), actDown(Act::Control3), actDown(Act::Control4)};
        if (sim::hasControl(S, "magrelease")) { sim::setKey(S, "magrelease", held[0]); S.magRelease = false; }
        else S.magRelease = held[0];
        {   // the slide release, as on the bench
            bool sr = actDown(Act::SlideRelease);
            if (sim::hasControl(S, "sliderelease")) { sim::setKey(S, "sliderelease", sr); S.slideRelease = false; }
            else S.slideRelease = sr;
        }
        sim::setKey(S, "k1", held[1]); sim::setKey(S, "k2", held[2]); sim::setKey(S, "k3", held[3]); sim::setKey(S, "k4", held[4]);
        { perf::Scope t(perf::Physics); perf::substeps(sim::run(S, dt, 1200)); }
        if (std::getenv("GUNSMITH_RANGECHDBG")) for (auto& p : S.pushers) if (p.key == "fire") { V2 w = sim::wpt(*p.b, p.l); std::printf("  trigger part moved %.2f mm (on %d)\n", ((w.x - (p.b->x0 + p.l.x)) * p.dx + (w.y - (p.b->y0 + p.l.y)) * p.dy) * 1000, p.on); }
        if (std::getenv("GUNSMITH_RANGECHDBG")) { static int fr = 0; if (fr++ < 300 && fr % 6 == 0) std::printf("frame %d lwo %d gap %.1f t %.3f fireT %.3f cc %d chambered %d spent %d gap %.2f shots %d acc %.2f %.2f hand %d anim %d trig %d pressed %d cocked %d dead %d inB %d igAt %.2f\n", fr, R.loadWhenOpen, sim::breechGap(S), S.t, S.fireT, sim::chamberedCount(S), S.chambered, S.spent, sim::breechGap(S), S.shots, S.frameAccel.x, S.frameAccel.y, S.hand.on, sim::animPlaying(S), S.trigger, S.pressed, S.cocked, S.deadPull, sim::inBattery(S), S.igniteAt); }
        countAmmo(S);   // what went into the gun came out of your ammunition
        handOffCases(S);   // a fired case clear of the gun goes out into the world
        // a dropped magazine (and anything else loose) flies with the gun's frame here, so it's cleared once it's fallen away
        sim::tidy(S, dt, 1.2, 0.8, 2);
        const auto& br = A.design.breech;
        for (auto& e : S.events) {
            if (e.kind == sim::Ev::Bang) {
                A.flash = 0.06f * (float)R.flashK;
                {
                    int heard = 0;
                    for (size_t k = 0; k <= br.more.size(); k++)
                        if (k < S.chambers.size() ? std::fabs(S.chambers[k].shotT - e.t) < 1e-9 : k == 0) { playShot(A.design, (int)k, (heard ? 0.7f : 1.f) * (float)R.loudK); heard++; }
                    if (!heard) playShot(A.design, 0, (float)R.loudK);
                }
                npcai::hear(R.actors, R.pl.feet, R.hearM);   // a gunshot carries: NPCs within 50 m come to look (15 m suppressed)
                bool any = false;
                for (size_t k = 0; k <= br.more.size(); k++) {
                    bool fired = k < S.chambers.size() ? std::fabs(S.chambers[k].shotT - e.t) < 1e-9 : k == 0;
                    if (fired && (k == 0 ? br.hasMuzzle : br.more[k - 1].hasMuzzle)) { fireBullets((int)k); any = true; }
                }
                if (!any && br.hasMuzzle) fireBullets(0);
            }
            if (e.kind == sim::Ev::Eject || e.kind == sim::Ev::EjectLive || e.kind == sim::Ev::Chambered) actionEventSound(S, e.kind);
            if (e.kind == sim::Ev::Click && SFX.ok && A.sound) PlaySound(SFX.click);
            if (e.kind == sim::Ev::Index && SFX.ok && A.sound) { SetSoundPitch(SFX.click, 1.7f); PlaySound(SFX.click); SetSoundPitch(SFX.click, 1.f); }   // (a cylinder turning)
            if (e.kind == sim::Ev::Malfunction && S.malf != sim::Malf::None && SFX.ok && A.sound) PlaySound(SFX.click);
        }
        S.events.clear();
        actionRollSound(S);
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
        { perf::Scope t(perf::Npcs); npcai::think(R.actors, R.map, eye, R.pl.feet, !R.dead, dt, ev, R.rng, &R.nav); }
        for (auto& e : ev) {
            if (e.kind == npcai::Event::Shot) {
                const auto& a = R.actors[e.actor];
                const Cartridge& c = a.cart;
                double mass = c.bulletMass / 1000, dia = c.bulletDia / 1000;
                double k = 0.5 * 1.2 * 0.3 * 3.14159 * dia * dia / 4 / std::max(1e-4, mass);
                R.bullets.push_back({e.from, e.dir * std::max(50.0, c.velocity), k, 0, 0, mass, e.actor, a.gun.damage, dia});
                { float d = (float)gmap::len(e.from - eye); playShotMix(shotMix(c, -1), std::clamp(6.f / (d + 4), 0.08f, 0.8f)); }   // farther: quieter
            } else if (e.kind == npcai::Event::Melee && !R.dead) {
                R.health -= armor::absorb(R.armor, e.damage); R.hurtT = 0.4;
                R.kickYaw += 0.04; R.kickPitch += 0.02;   // (knocked about)
            } else if (e.kind == npcai::Event::Explode) {
                double d = gmap::len(eye - e.from);
                if (!R.dead && d < e.radius) { R.health -= armor::absorb(R.armor, e.damage * (1 - d / e.radius)); R.hurtT = 0.5; }
                for (auto& o : R.actors) {   // and whatever else is near
                    V3 c = o.body.feet + V3{0, npcai::height(o) / 2, 0};
                    double dd = gmap::len(c - e.from);
                    if (&o != &R.actors[e.actor] && dd < e.radius) npcai::hurt(o, e.damage * (1 - dd / e.radius), c - e.from);
                }
                A.flash = 0.15f;
                playShotMix(shotMix(6000, 300, 60));   // a blast: the rifle recording, deep and loud
            }
        }
        if (!R.dead && R.health <= 0) { R.health = 0; R.dead = true; R.deadT = 0; if (A.S) { A.S->trigger = false; sim::setKey(*A.S, "fire", false); } }
        if (R.dead) {
            R.deadT += dt;
            if (R.deadT > 1.2 && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))) restartLevel();
        }
        R.hurtT = std::max(0.f, R.hurtT - dt);
        R.armorT = std::max(0.f, R.armorT - dt);
        R.feedT = std::max(0.f, R.feedT - dt);
    }
    stepStab(dt);
    if (R.optic && R.opticItem >= 0 && R.opticItem < (int)A.fitItems.size()) {   // its reticle lit as it's switched
        const auto& it = A.fitItems[R.opticItem];
        R.op.lit = it.att.attach.optic.lit && it.on;
    }
    {   // a bipod's feet on something (a bench, a wall top, the floor when you're down): rested
        R.rested = false;
        if (R.bipodItem >= 0 && R.bipodItem < (int)A.fitItems.size() && A.fitItems[R.bipodItem].on && R.aim > 0.6f && R.cur >= 0) {
            V3 foot = gunToMap({R.bipodFoot.x, R.bipodFoot.y}, (float)(R.bipodFoot.z * U));
            gmap::Hit h = gmap::rayMap(R.map, foot + V3{0, 0.03, 0}, {0, -1, 0}, 0.12);
            R.rested = h.brush >= 0 && h.n.y > 0.5;
            if (std::getenv("GUNSMITH_RANGEDBG")) std::printf("bipod foot %.3f above %s rested %d\n", foot.y, h.brush >= 0 ? "something" : "nothing", (int)R.rested);
        }
    }
    stepBullets(dt);
    stepCases((float)dt);
    R.lastHitT = std::max(0.f, R.lastHitT - dt);
    A.flash = std::max(0.f, A.flash - dt);
    stepEffects(dt);
}

// Spent cases in the world. Once a fired case is out of the gun (clear of every part), it leaves the gun's physics and
// carries on in the map: its speed and spin as it left, plus yours; gravity, bounces off floors and walls (losing most of
// its speed), then lies where it stops. At most 150 at once; each stays two minutes.
static void handOffCases(sim::Sim& S) {
    if (!R.placed) return;
    const Design& D = A.design;
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& b : D.bodies) if (!b.magazine) for (auto& q : b.pts) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
    const Cartridge& c = D.breech.cart;
    for (auto& b : S.bodies) {
        if (b.gone || !b.isCase || b.rs != sim::RS::Loose || b.idle > 50) continue;
        double mx = b.x * 1000, my = b.y * 1000, mz = b.z * 1000;
        bool clear = mx < x0 - 8 || mx > x1 + 8 || my < y0 - 8 || my > y1 + 8 || std::fabs(mz) > 40;
        if (!clear) continue;
        float zd = (float)(mz * U);
        V3 p = gunToMap({mx, my}, zd);
        double h = 0.01;
        V3 q = gunToMap({mx + b.vx * 1000 * h, my + b.vy * 1000 * h}, (float)((mz + b.vz * 1000 * h) * U));
        V3 ax = gunToMap({mx + std::cos(b.a) * 10, my + std::sin(b.a) * 10}, zd) - p;
        Range::Case k;
        k.p = p; k.v = (q - p) * (1 / h) + R.pl.vel;
        k.v = k.v * (1 + rnd(0.18f)) + V3{rnd(0.35f), rnd(0.25f), rnd(0.35f)};   // (no two leave quite alike)
        k.axis = gmap::norm(ax);
        k.spinAx = gmap::norm(gmap::cross(k.axis, {0, 1, 0}));
        k.spin = b.w * 0.6 + 25 + rnd(15.f);
        k.len = c.caseLen / 1000; k.dia = c.headDia() / 1000;
        k.col = c.kind == "shotshell" ? GetColor(c.hullColor) : Color{214, 170, 76, 255};
        R.cases.push_back(k);
        b.idle = 100;   // (the gun's copy goes: tidy retires it)
    }
    while (R.cases.size() > 150) R.cases.erase(R.cases.begin());
}
static void stepCases(float dt) {
    if (std::getenv("GUNSMITH_CASEDBG")) { static int fr = 0; if (++fr % 60 == 0) { std::printf("cases %zu:", R.cases.size()); for (auto& k : R.cases) std::printf(" (%.2f %.2f %.2f %s b%d)", k.p.x, k.p.y, k.p.z, k.rest ? "rest" : "fly", k.bounces); std::printf("  me %.2f %.2f %.2f\n", R.pl.feet.x, R.pl.feet.y, R.pl.feet.z); } }
    for (auto& k : R.cases) {
        k.life += dt;
        if (k.rest) continue;
        k.v.y -= 9.81 * dt;
        V3 step = k.v * dt;
        double L = gmap::len(step);
        if (L > 1e-6) {
            gmap::Hit h = gmap::rayMap(R.map, k.p, step * (1 / L), L + k.dia / 2);
            if (h.brush >= 0) {   // a bounce: most of the speed into the surface is lost, some along it
                k.p = k.p + step * (std::max(0.0, h.t - k.dia / 2) / L);
                double vn = gmap::dot(k.v, h.n);
                V3 vt = k.v - h.n * vn;
                k.v = vt * 0.55 - h.n * (vn * 0.3);
                k.spin *= 0.6;
                k.bounces++;
                if (h.n.y > 0.7 && gmap::len(k.v) < 0.35) {   // settled on a floor: lies on its side
                    k.rest = true;
                    k.axis = gmap::norm(V3{k.axis.x, 0, k.axis.z});
                    if (gmap::len(k.axis) < 0.5) k.axis = {1, 0, 0};
                    k.p.y += k.dia / 2 - 0.001;
                }
                continue;
            }
            k.p = k.p + step;
        }
        // tumbling through the air
        double a = k.spin * dt;
        Matrix M = MatrixRotate(rv(k.spinAx), (float)a);
        Vector3 nx = Vector3Transform(rv(k.axis), M);
        k.axis = gmap::norm(V3{nx.x, nx.y, nx.z});
        if (k.p.y < -200) k.life = 1e9;
    }
    R.cases.erase(std::remove_if(R.cases.begin(), R.cases.end(), [](const Range::Case& k) { return k.life > 120; }), R.cases.end());
}
static void drawCases() {
    for (auto& k : R.cases) {
        V3 a = k.p - k.axis * (k.len / 2), b = k.p + k.axis * (k.len / 2);
        drawCyl(rv(a), rv(b), (float)(k.dia / 2), k.col);
        drawCyl(rv(b), rv(b + k.axis * 0.0006), (float)(k.dia / 2 * 0.8), Color{40, 34, 26, 255});   // (the open mouth)
    }
}


// everything on the map besides its brushes: targets, bullet holes, NPCs, spawners, plates, crates, guns on the ground
static std::vector<Lamp> gunLamps();
static void drawRangeThings() {
    doors::draw(R.map);
    drawCases();
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
        double lit[3]; gmap::lightAt(R.map, t.c, t.n, lit);   // (drawn without the shader: lit by hand, with the gun's light)
        for (auto& gl : gunLamps()) {
            V3 lp{gl.pos[0], gl.pos[1], gl.pos[2]}, ld = lp - t.c;
            double dl = gmap::len(ld); if (dl > gl.range || dl < 1e-6) continue;
            V3 l = ld * (1 / dl);
            double q4 = std::pow(dl / gl.range, 4), w = std::max(0.0, 1 - q4), att = gl.power * w * w / (1 + dl * dl);
            if (gl.cosCone > -1.5) { double cs = -(l.x * gl.dir[0] + l.y * gl.dir[1] + l.z * gl.dir[2]); double in2 = std::min(1.0, gl.cosCone + 0.08), q = std::clamp((cs - gl.cosCone) / std::max(1e-4, in2 - gl.cosCone), 0.0, 1.0); att *= q * q * (3 - 2 * q); }
            att *= std::max(0.0, gmap::dot(t.n, l)) * 0.7;
            for (int k = 0; k < 3; k++) lit[k] += att * gl.col[k];
        }
        auto shade = [&](Color c) { return Color{(unsigned char)std::clamp(c.r * lit[0] * 1.25, 0.0, 255.0), (unsigned char)std::clamp(c.g * lit[1] * 1.25, 0.0, 255.0), (unsigned char)std::clamp(c.b * lit[2] * 1.25, 0.0, 255.0), c.a}; };
        V3 a = o - side * h - tu * h, b = o + side * h - tu * h, c = o + side * h + tu * h, d = o - side * h + tu * h;
        Color paper = shade(Color{238, 234, 222, 255});
        DrawTriangle3D(rv(a), rv(b), rv(c), paper); DrawTriangle3D(rv(a), rv(c), rv(b), paper);
        DrawTriangle3D(rv(a), rv(c), rv(d), paper); DrawTriangle3D(rv(a), rv(d), rv(c), paper);
        for (int ring = 5; ring >= 1; ring--)
            disc(h * ring / 5.0 * 0.95, ring % 2 ? shade(Color{30, 30, 32, 255}) : paper, 0.003 + (5 - ring) * 0.0006);
        disc(h * 0.05, shade(Color{200, 60, 40, 255}), 0.0065);
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
    for (size_t i = 0; i < R.map.ents.size() && i < R.plates.size(); i++) {   // armour plates: a stack lying flat
        const auto& e = R.map.ents[i];
        if (e.type != "armor_plate") continue;
        for (int k = 0; k < R.plates[i]; k++) drawArmorPlate(rv(e.pos + V3{0, 0.03 * k, 0}), (float)(e.yaw + 9 * k));
    }
    for (auto& e : R.map.ents) if (e.type == "attachment_crate") drawAttachmentCrate(rv(e.pos), (float)e.yaw);
    for (auto& e : R.map.ents) if (e.type == "ammo_box") drawAmmoBox(rv(e.pos), (float)e.yaw);
    for (auto& e : R.map.ents) if (e.type == "weapon_crate") drawWeaponCrate(rv(e.pos), (float)e.yaw);
    for (auto& g : R.ground)   // a gun case on the floor (the gun itself is drawn once it's in your hands)
        drawBox(rv(g.pos + V3{0, 0.06, 0}), {0.75f, 0.12f, 0.28f}, Color{58, 52, 44, 255}, MatrixRotateY((float)(-g.yaw * DEG2RAD)));
    // bullets in flight: a faint streak
    for (auto& b : R.bullets) DrawLine3D(rv(b.p), rv(b.p - b.v * 0.004), Color{255, 236, 190, 120});
}

// a reticle at screen point c, k pixels a milliradian
static void drawReticle(Vector2 c, double k, const OpticDef& op, float alpha) {
    unsigned rc = op.color;
    Color col{(unsigned char)((rc >> 24) & 255), (unsigned char)((rc >> 16) & 255), (unsigned char)((rc >> 8) & 255), (unsigned char)(255 * alpha)};
    if (op.kind == "scope" && !op.lit) col = Color{12, 12, 14, (unsigned char)(255 * alpha)};   // etched black lines
    else if (!op.lit) return;   // a reflex sight switched off: no dot
    for (auto& m : op.reticle) {
        float w = std::max(1.f, (float)(m.size * k));
        Vector2 a{c.x + (float)(m.x0 * k), c.y - (float)(m.y0 * k)};
        if (m.kind == 0) DrawLineEx(a, {c.x + (float)(m.x1 * k), c.y - (float)(m.y1 * k)}, w, col);
        else if (m.kind == 1) DrawCircleV(a, std::max(1.f, w / 2), col);
        else DrawRing(a, std::max(0.f, (float)(m.x1 * k) - w / 2), (float)(m.x1 * k) + w / 2, 0, 360, 64, col);
    }
}
// where a point on the gun (design mm) is on screen now
static Vector2 gunToScreen(fit::P3 p) { return GetWorldToScreen(rv(gunToMap({p.x, p.y}, (float)(p.z * U))), A.cam); }
// the gun's own light for the lit shader: each light switched on (a spot as bright as its lumens through its beam), and
// the muzzle flash for a moment
static std::vector<Lamp> gunLamps() {
    std::vector<Lamp> out;
    if (R.cur < 0) return out;
    for (auto& bm : R.beams) {
        if (bm.laser || !gadgetOn(bm)) continue;
        V3 a = gunToMap({bm.a.x, bm.a.y}, (float)(bm.a.z * U)), b = gunToMap({bm.b.x, bm.b.y}, (float)(bm.b.z * U));
        V3 d = b - a;
        if (gmap::len(d) < 1e-6) continue;
        d = gmap::norm(d);
        double half = std::clamp(bm.deg, 1.0, 120.0) * DEG2RAD / 2;
        double candela = bm.lumens / (2 * PI * (1 - std::cos(half)));   // its lumens squeezed into its beam
        Lamp L;
        V3 o = b + d * 0.02;
        L.pos[0] = (float)o.x; L.pos[1] = (float)o.y; L.pos[2] = (float)o.z;
        L.dir[0] = (float)d.x; L.dir[1] = (float)d.y; L.dir[2] = (float)d.z;
        L.col[0] = ((bm.color >> 24) & 255) / 255.f; L.col[1] = ((bm.color >> 16) & 255) / 255.f; L.col[2] = ((bm.color >> 8) & 255) / 255.f;
        L.power = (float)std::clamp(candela / 300, 0.5, 80.0);   // (1 = about 300 lux at 1 m)
        L.range = 70;
        L.cosCone = (float)std::cos(half * 1.15);
        out.push_back(L);
    }
    if (A.flash > 0 && R.flashK > 0) {   // the muzzle flash lights the room for a moment
        const auto& br = A.design.breech;
        int ci = A.design.bodyIndex(br.chamber);
        V3 m = gunToMap(curMM(br.chamber, br.muzzle), ci >= 0 ? bodyZ(ci) : 0);
        Lamp L;
        L.pos[0] = (float)m.x; L.pos[1] = (float)m.y; L.pos[2] = (float)m.z;
        L.col[0] = 1; L.col[1] = 0.72f; L.col[2] = 0.38f;
        L.power = (float)(8 * std::min(1.0, A.flash / 0.06) * std::min(1.0, R.flashK));
        L.range = 9;
        out.push_back(L);
    }
    return out;
}
// lights and lasers switched on: a beam out of each, a lit spot (a light) or a dot (a laser) where it meets something
static void drawBeams() {
    if (R.cur < 0 || R.beams.empty()) return;
    for (auto& bm : R.beams) {
        if (!gadgetOn(bm)) continue;
        V3 a = gunToMap({bm.a.x, bm.a.y}, (float)(bm.a.z * U)), b = gunToMap({bm.b.x, bm.b.y}, (float)(bm.b.z * U));
        V3 d = b - a;
        if (gmap::len(d) < 1e-6) continue;
        d = gmap::norm(d);
        double far = bm.laser ? 300 : 120;
        gmap::Hit h = gmap::rayMap(R.map, b, d, far);
        double dist = h.brush >= 0 ? h.t : far;
        V3 n = h.brush >= 0 ? h.n : d * -1;
        for (auto& ac : R.actors) {   // an NPC in the way
            if (ac.dead) continue;
            V3 c = ac.body.feet;
            if (gmap::len(c - b) > dist + 3) continue;
            for (auto& nb : npcai::bones(ac)) {
                double sAlong;
                if (segSeg(b, b + d * dist, nb.a, nb.b, &sAlong) < nb.r && sAlong * dist < dist) { dist = sAlong * dist; n = d * -1; }
            }
        }
        Color c{(unsigned char)(bm.color >> 24), (unsigned char)(bm.color >> 16), (unsigned char)(bm.color >> 8), 255};
        V3 hit = b + d * dist;
        V3 s = gmap::norm(gmap::cross(n, std::fabs(n.y) > 0.9 ? V3{1, 0, 0} : V3{0, 1, 0})), t = gmap::cross(n, s);
        auto disc = [&](V3 o, double r, Color col) {
            const int N = 24;
            for (int i = 0; i < N; i++) {
                double a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N;
                V3 p0 = o + s * (r * std::cos(a0)) + t * (r * std::sin(a0)), p1 = o + s * (r * std::cos(a1)) + t * (r * std::sin(a1));
                DrawTriangle3D(rv(o), rv(p0), rv(p1), col); DrawTriangle3D(rv(o), rv(p1), rv(p0), col);
            }
        };
        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ADDITIVE);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        if (bm.laser) {
            if (dist < far) {
                double r = 0.0025 + dist * 0.00025;
                disc(hit + n * 0.004, r * 2.2, Color{c.r, c.g, c.b, 70});
                disc(hit + n * 0.005, r, c);
                disc(hit + n * 0.006, r * 0.45, Color{255, 255, 255, 200});
            }
            DrawLine3D(rv(b), rv(hit), Color{c.r, c.g, c.b, 60});   // (faint: a beam shows only a little in daylight)
        } else {
            double half = std::tan(bm.deg * DEG2RAD / 2), r = std::max(0.03, dist * half);
            double k = std::clamp(bm.lumens / 60 / (1 + dist * dist * 0.15), 0.0, 1.0);   // brighter close up and for a stronger light
            (void)r; (void)k;   // (the lit spot itself: the light's lamp in the shader, gunLamps)
            double len = std::min(dist, 4.0);   // a faint cone of light in the air
            V3 cs = gmap::norm(gmap::cross(d, std::fabs(d.y) > 0.9 ? V3{1, 0, 0} : V3{0, 1, 0})), ct = gmap::cross(d, cs);
            const int N = 16;
            unsigned char ca = (unsigned char)(4 * std::clamp(bm.lumens / 600, 0.5, 2.5));
            for (int i = 0; i < N; i++) {
                double a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N, rr = len * half;
                V3 e = b + d * len;
                V3 p0 = e + cs * (rr * std::cos(a0)) + ct * (rr * std::sin(a0)), p1 = e + cs * (rr * std::cos(a1)) + ct * (rr * std::sin(a1));
                DrawTriangle3D(rv(b), rv(p0), rv(p1), Color{c.r, c.g, c.b, ca});
            }
            disc(b + d * 0.002, 0.012, Color{c.r, c.g, c.b, 200});   // the lens, lit
        }
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
    }
}
// a scope's magnified picture: the map seen down its sight line, narrower by its magnification (drawn before the view)
static void renderScopePicture(V3 up) {
    if (!R.optic || R.op.kind != "scope" || R.aim < 0.3f || R.cur < 0) return;
    const int SZ = 512;
    if (!R.pipOk) { R.pip = LoadRenderTexture(SZ, SZ); R.pipOk = true; }
    V3 wr = gunToMap({R.optRear.x, R.optRear.y}, (float)(R.optRear.z * U)), wf = gunToMap({R.optFront.x, R.optFront.y}, (float)(R.optFront.z * U));
    V3 d = gmap::norm(wf - wr);
    Camera3D c{};
    c.position = rv(wf); c.target = rv(wf + d); c.up = rv(up); c.fovy = (float)(A.cam.fovy / std::max(1.0, R.zoom)); c.projection = CAMERA_PERSPECTIVE;
    BeginTextureMode(R.pip);
    ClearBackground(mapSkyHorizon(R.map.look));
    mapDrawSky(R.map.look, 0, 0, SZ, SZ, (float)std::asin(std::clamp(d.y, -1.0, 1.0)), c.fovy);
    BeginMode3D(c);
    mapApplyLook(R.map.look);
    mapSetLamps(R.map, wf, gunLamps());
    mapDrawWorld();
    drawRangeThings();
    drawBeams();
    EndMode3D();
    EndTextureMode();
}

void rangeRender() {
    double roll;
    V3 eye = gmap::playerEye(R.pl, R.map, &roll);
    V3 f = gmap::playerForward(R.pl), r = gmap::norm(gmap::cross(f, {0, 1, 0})), u = gmap::cross(r, f);
    V3 up = u * std::cos(roll) + r * std::sin(roll);
    // a little breathing sway when aiming, more for a heavy gun held out in one hand
    double sway = std::max(R.aim, (1 - R.aim) * R.point * 0.8f) * (R.twoHands ? 0.0012 : 0.0025) * (0.6 + R.mass * 0.25) * R.swayK * (R.rested ? 0.15 : 1.0)
                 * (R.pl.stance == gmap::Stance::Prone ? 0.4 : R.pl.stance == gmap::Stance::Crouch ? 0.7 : 1.0);   // (kneeling, lying down: steadier)   // (a grip, a stock: steadier; rested on a bipod, hardly at all)
    R.lagYaw += std::sin(R.t * 1.1) * sway * 0.02;
    R.lagPitch += std::sin(R.t * 1.7 + 1) * sway * 0.015;
    Camera3D cam{};
    cam.position = rv(eye); cam.target = rv(eye + f); cam.up = rv(up); cam.fovy = CFG.fov - 12 * R.aim; cam.projection = CAMERA_PERSPECTIVE;
    renderScopePicture(up);   // (with last frame's gun: before this frame's view)
    A.cam = cam;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    mapDrawSky(R.map.look, 0, 0, W, H, (float)R.pl.pitch, cam.fovy);
    {   // a wall (or a shut door) closer than the gun reaches: it backs off, then tips up out of the way
        V3 rt = gmap::norm(gmap::cross(f, up));
        double d = 1e9;
        for (V3 o : {eye, eye - up * 0.08 + rt * 0.06, eye - up * 0.16 + rt * 0.1}) {
            gmap::Hit h = gmap::rayMap(R.map, o, f, R.gunReach + 0.3);
            if (h.brush >= 0) d = std::min(d, h.t);
            double dt2 = doors::ray(R.map, o, f, R.gunReach + 0.3);
            if (dt2 >= 0) d = std::min(d, dt2);
        }
        double over = R.gunReach + 0.04 - d;   // (how far it would go into the wall)
        if (std::getenv("GUNSMITH_WALLDBG")) { gmap::Hit hh = gmap::rayMap(R.map, eye, f, 50); std::printf("wall %.2f reach %.2f far %.2f at %.2f %.2f\n", d, R.gunReach, hh.t, eye.x, eye.z); }
        double back = std::clamp(over, 0.0, 0.18) * 1000;   // first it slides back toward you, up to 18 cm,
        float want = (float)std::clamp((over - 0.12) / 0.2, 0.0, 1.0);   // then the muzzle comes up
        float k = std::min(1.f, GetFrameTime() * 12);
        R.wallBack += (back - R.wallBack) * k;
        R.wallU += (want - R.wallU) * k;
    }
    // the gun, in the hands (placed first: its light shines on everything)
    placeGun(eye, f, gmap::norm(gmap::cross(f, up)), up);
    mapBeginViewport(cam, 0, 0, W, H);
    mapApplyLook(R.map.look);
    mapSetLamps(R.map, eye, gunLamps());
    mapDrawWorld();
    drawRangeThings();
    drawBeams();
    if (std::getenv("GUNSMITH_RANGEDBG")) {
        const auto& br = A.design.breech;
        V3 m = gunToMap(curMM(br.chamber, br.muzzle), 0), fc = gunToMap(curMM(br.chamber, br.face), 0);
        std::printf("eye %.3f %.3f %.3f  muzzle %.3f %.3f %.3f  face %.3f %.3f %.3f  f %.2f %.2f %.2f\n", eye.x, eye.y, eye.z, m.x, m.y, m.z, fc.x, fc.y, fc.z, f.x, f.y, f.z);
    }
    rlDrawRenderBatchActive();
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(R.G));
    setOpticSeeThrough(R.optic ? 1.f - 0.85f * std::clamp((R.aim - 0.3f) / 0.7f, 0.f, 1.f) : 1.f);
    bool shown = R.cur >= 0 && R.holsterU < 0.98f;   // (put away: out of sight)
    if (shown) drawGunOnly();
    setOpticSeeThrough(1);
    if (shown && CFG.fpArms && A.design.pose.firing.on) {   // your arms and hands on it
        Matrix inv = MatrixInvert(R.G);
        Vector3 e = Vector3Transform(rv(eye), inv), ef = Vector3Transform(rv(eye + f), inv), eu = Vector3Transform(rv(eye + up), inv);
        drawFirstPersonArms(e, Vector3Subtract(ef, e), Vector3Subtract(eu, e));
    }
    rlDrawRenderBatchActive();
    rlPopMatrix();
    EndMode3D();
    mapEndViewport();
    mapRestoreLook();
}

void rangeHud() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight(), cx = W / 2, cy = H / 2;
    if (R.optic && R.cur >= 0 && R.aim > 0.3f) {   // looking through an optic: where its sight line points, a long way out
        double dx = R.optFront.x - R.optRear.x, dy = R.optFront.y - R.optRear.y, dz = R.optFront.z - R.optRear.z, L = std::sqrt(dx * dx + dy * dy + dz * dz);
        fit::P3 far{R.optFront.x + dx / L * 100000, R.optFront.y + dy / L * 100000, R.optFront.z + dz / L * 100000};
        Vector2 c = gunToScreen(far);
        float u = std::clamp((R.aim - 0.3f) / 0.7f, 0.f, 1.f);
        double pxRad = (H / 2) / std::tan(A.cam.fovy * DEG2RAD / 2);   // the view's own pixels a radian
        if (R.op.kind == "scope") {   // the magnified picture in the eyepiece, the reticle in it
            float r = H * 0.23f * (0.6f + 0.4f * u);
            if (R.pipOk) {   // the picture, cut to the eyepiece's circle by a tiny shader
                static Shader round{}; static bool roundOk = false;
                if (!roundOk) {
                    round = LoadShaderFromMemory(nullptr,
                        "#version 330\nin vec2 fragTexCoord; in vec4 fragColor; uniform sampler2D texture0; uniform vec4 colDiffuse; out vec4 finalColor;\n"
                        "void main() { vec2 d = fragTexCoord - vec2(0.5); float r = length(d); if (r > 0.5) discard;\n"
                        "  vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor; c.rgb *= 1.0 - smoothstep(0.38, 0.5, r) * 0.45; finalColor = c; }\n");
                    roundOk = true;
                }
                unsigned char al = (unsigned char)(255 * u);
                BeginShaderMode(round);
                DrawTexturePro(R.pip.texture, {0, 0, (float)R.pip.texture.width, -(float)R.pip.texture.height}, {c.x - r, c.y - r, 2 * r, 2 * r}, {0, 0}, 0, Color{255, 255, 255, al});
                EndShaderMode();
            }
            DrawRing(c, r, r + H * 0.035f, 0, 360, 64, Color{10, 11, 13, (unsigned char)(255 * u)});   // the tube's rim
            double fov2 = A.cam.fovy / std::max(1.0, R.zoom) * DEG2RAD;
            double ref = R.op.ffp ? R.zoom : (R.op.magMax > R.op.mag ? R.op.magMax : R.op.mag);   // second focal plane: true at its highest
            double k = (R.op.ffp ? r / std::tan(fov2 / 2) : r / std::tan(A.cam.fovy / ref * DEG2RAD / 2)) / 1000;
            BeginScissorMode((int)(c.x - r), (int)(c.y - r), (int)(2 * r), (int)(2 * r));
            drawReticle(c, k, R.op, u);
            EndScissorMode();
            if (R.op.magMax > R.op.mag) viewText(TextFormat("%.1fx", R.zoom), c.x + r * 0.72f, c.y + r * 0.78f, 13, Color{230, 233, 238, (unsigned char)(200 * u)});
        } else drawReticle(c, pxRad / 1000, R.op, u);   // a reflex sight: the dot out where the sight looks, true size
    }
    if (gDemo.on) {   // a range demonstration under way
        std::string t = TextFormat("Range demonstration for %s:  %d hit%s on paper,  round %d of %d  (they want %d hits)", gDemo.client.c_str(), gDemo.hits, gDemo.hits == 1 ? "" : "s", std::min(gDemo.shots, gDemo.limit), gDemo.limit, gDemo.need);
        float tw = viewTextWidth(t.c_str(), 16);
        DrawRectangleRounded({cx - tw / 2 - 14, 48, tw + 28, 32}, 0.4f, 6, Color{20, 23, 28, 210});
        viewText(t.c_str(), cx - tw / 2, 55, 16, gDemo.hits >= gDemo.need ? Color{130, 230, 150, 255} : Color{255, 220, 140, 255});
        if (gDemo.shots >= gDemo.limit) {
            const char* e = gDemo.hits >= gDemo.need ? "That's the course. They're impressed." : "That's the course. They don't look convinced.";
            float ew = viewTextWidth(e, 20);
            viewText(e, cx - ew / 2, cy - 90, 20, Color{240, 242, 245, 255});
        }
    }
    {   // how you're standing and carrying it
        std::string st = R.pl.stance == gmap::Stance::Prone ? "Prone" : R.pl.stance == gmap::Stance::Crouch ? "Crouching" : "";
        const char* rd = R.readyU > 0.5f ? (R.readyShown == 1 ? "High ready" : "Low ready") : nullptr;
        if (R.holsterU > 0.5f) rd = R.holstered ? "Holstered (H or click to draw)" : nullptr;
        if (rd) st += (st.empty() ? "" : "   ") + std::string(rd);
        else if (R.point > 0.5f && R.aim < 0.5f) st += (st.empty() ? "" : "   ") + std::string("Point shooting");
        if (!st.empty() && R.cur >= 0) viewText(st.c_str(), 24, H - 70, 14, Color{220, 224, 230, 200});
    }
    if (R.rested) viewText("Rested on its bipod", cx - viewTextWidth("Rested on its bipod", 13) / 2, cy + H * 0.3f, 13, Color{170, 230, 180, 220});
    if (!A.fitItems.empty() && R.cur >= 0 && !R.freeMouse && CFG.hints) viewText("Hold Alt: your attachments", W - viewTextWidth("Hold Alt: your attachments", 12) - 24, 110, 12, Color{210, 214, 220, 170});
    if (R.aim < 0.5f && R.point < 0.5f) { DrawCircleV({cx, cy}, 2.5f, Color{255, 255, 255, 200}); DrawCircleLinesV({cx, cy}, 2.5f, Color{0, 0, 0, 120}); }
    {   // your health; a red edge when you're hurt; and dying
        if (R.hurtT > 0) {
            unsigned char al = (unsigned char)std::min(160.f, R.hurtT * 420);
            DrawRectangleGradientH(0, 0, (int)(W * 0.18f), (int)H, Color{170, 20, 16, al}, Color{170, 20, 16, 0});
            DrawRectangleGradientH((int)(W * 0.82f), 0, (int)(W * 0.18f), (int)H, Color{170, 20, 16, 0}, Color{170, 20, 16, al});
        }
        bool anyPlates = false;
        for (int n : R.plates) if (n > 0) anyPlates = true;
        if (R.armor > 0 || anyPlates) {   // the armour bar, over the health bar: three plates' worth, in thirds
            float bw = 220, bx = 24, by = H - 160;
            DrawRectangleRounded({bx, by, bw, 10}, 0.5f, 6, Color{20, 23, 28, 190});
            float f = (float)std::clamp(R.armor / armor::MAX, 0.0, 1.0);
            unsigned char fl = (unsigned char)(R.armorT > 0 ? 255 : 225);
            if (f > 0) DrawRectangleRounded({bx, by, bw * f, 10}, 0.5f, 6, Color{90, 160, 235, fl});
            for (int k = 1; k < 3; k++) DrawLineEx({bx + bw * k / 3, by}, {bx + bw * k / 3, by + 10}, 1.5f, Color{20, 23, 28, 220});
            viewText(TextFormat("Armour %.0f", R.armor), bx, by - 18, 12, Color{170, 205, 245, 230});
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
    bool cyl = sim::hasCylinder(S);   // a revolver: what's in the cylinder
    if (cyl) std::snprintf(b, sizeof b, "%d / %d", sim::cylinderLive(S), (int)S.cyl.size());
    else std::snprintf(b, sizeof b, "%d%s", inMag, S.chambered ? " + 1" : "");
    float fs = 30;
    float w = viewTextWidth(b, fs);
    viewText(b, W - w - 36, H - 70, fs, Color{240, 242, 245, 240});
    const char* mag = cyl ? (sim::cylinderLive(S) > 0 ? "in the cylinder" : "empty: R to reload")
                    : M || S.chambered ? (M ? "in the magazine, + in the chamber" : "in the chamber") : "empty: R to reload";
    {   // what you carry besides
        std::string rs = TextFormat("%d spare", R.reserve);
        Color rc = R.reserve == 0 ? Color{255, 120, 100, 240} : R.reserve < R.reserveMax / 5 ? Color{255, 200, 90, 230} : Color{200, 204, 212, 200};
        viewText(rs.c_str(), W - viewTextWidth(rs.c_str(), 15) - 36, H - 96, 15, rc);
        if (R.reserve == 0) mag = "no spare ammunition: find an ammo box";
    }
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
        const char* help = "Left mouse fires, right mouse aims.  R reloads, B works the action, I inspects, V stabs or hits.  Wheel: change guns, G drops, F uses.  Esc for the menu.";
        float hw = viewTextWidth(help, 13);
        DrawRectangleRounded({cx - hw / 2 - 12, 14, hw + 24, 24}, 0.4f, 6, Color{20, 23, 28, 150});
        viewText(help, cx - hw / 2, 19, 13, Color{235, 238, 242, 230});
    }
}

// Alt held: the mouse is free, and the gun's attachments have buttons to switch them
void rangeGadgetWindow() {
    if (!inRange() || !R.freeMouse || R.spawnerOpen || R.cur < 0 || A.fitItems.empty()) return;
    float W = (float)GetScreenWidth();
    ImGui::SetNextWindowPos({W - 330 * CFG.uiScale - 16, 110 * CFG.uiScale}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({330 * CFG.uiScale, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("Attachments##rangegadgets", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextDisabled("On the %s (let go of Alt to carry on)", A.design.name.c_str());
    for (size_t i = 0; i < A.fitItems.size(); i++) {
        auto& it = A.fitItems[i];
        const auto& at = it.att.attach;
        ImGui::PushID((int)i);
        const char* what = nullptr;
        if (at.job == "light" || at.job == "laser") what = it.on ? "On" : "Off";
        else if (at.job == "bipod") what = it.on ? "Legs down" : "Folded";
        else if (at.job == "optic" && at.optic.lit) what = it.on ? "Lit" : "Not lit";
        if (what) {
            bool on = it.on;
            if (ImGui::Checkbox("##on", &on)) { it.on = on; if (SFX.ok && A.sound) PlaySound(SFX.click); }
            ImGui::SameLine();
            ImGui::Text("%s  ", it.att.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", what);
        } else {
            ImGui::Dummy({ImGui::GetFrameHeight(), ImGui::GetFrameHeight()});
            ImGui::SameLine();
            ImGui::Text("%s  ", it.att.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s (always on)", attachJobName(at.job));
        }
        ImGui::PopID();
    }
    if (!R.beams.empty()) ImGui::TextDisabled("%s switches the lights and lasers.", keyName(keyOf(Act::Gadgets)));
    ImGui::End();
}

void rangeMapChanged() { ME.version++; }   // the map meshes now hold the range's map: the editor builds its own again

// ---------------------------------------------------------------- the performance overlay's details (perf.cpp)
std::vector<std::pair<std::string, std::string>> rangeDebugLines() {
    std::vector<std::pair<std::string, std::string>> out;
    int alive = 0, aware = 0;
    for (auto& a : R.actors) if (!a.dead) { alive++; if (a.aware) aware++; }
    out.push_back({"Map", R.map.name});
    out.push_back({"Blocks", std::to_string(R.map.brushes.size())});
    out.push_back({"NPCs", TextFormat("%d alive of %d (%d after you), the most %d", alive, (int)R.actors.size(), aware, npcai::NPC_LIMIT)});
    out.push_back({"Path grids", TextFormat("%d (one per NPC size)", (int)R.nav.grids.size())});
    out.push_back({"Bullets flying", std::to_string(R.bullets.size())});
    out.push_back({"Bullet marks", std::to_string(R.impacts.size())});
    out.push_back({"Guns on the ground", std::to_string(R.ground.size())});
    if (A.S) out.push_back({"Gun in hand", TextFormat("%s: %d bodies", A.design.name.c_str(), (int)A.S->bodies.size())});
    out.push_back({"You", TextFormat("at %.1f, %.1f, %.1f m, health %.0f", R.pl.feet.x, R.pl.feet.y, R.pl.feet.z, R.health)});
    return out;
}
