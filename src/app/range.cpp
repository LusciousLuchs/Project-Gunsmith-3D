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
#include "app/paths.hpp"
#include "app/render.hpp"
#include "app/settings.hpp"
#include "app/units.hpp"
#include "player.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <sstream>

using gmap::V3;

namespace {
struct Bullet { V3 p, v; double k = 0, travelled = 0, life = 0; };
struct Impact { V3 p, n; bool steel = false; double r = 0.005; };
struct Target { V3 c, n; double size = 0.6; };
struct Range {
    gmap::Map map;
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
        R.bullets.push_back({m, d * cart.velocity, k, 0, 0});
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

void enterRange(const Design& gun, const gmap::Map& map) {
    if (!A.stash) A.stash = std::make_unique<Design>(A.design);
    A.design = gun;
    syncBarrels(A.design);
    A.screen = Screen::Range;
    A.showPause = A.showSettings = A.showBenchPicker = A.showRangePicker = false;
    A.mode = Mode::Test;
    restartTest();
    mapGfxInit();
    R = Range();
    R.map = map;
    mapBuildMeshes(R.map);
    gmap::playerSpawn(R.pl, R.map);
    findTargets();
    R.twoHands = heldWith(A.design) == "two hands";
    if (A.S)   // loose rounds laid out on the bench have no bench to lie on here (R loads a single shot directly)
        for (auto& b : A.S->bodies) if (!b.gone && b.isRound && !b.isCase && b.rs == sim::RS::Loose) sim::retireBody(*A.S, b);
    R.mass = 0;
    if (A.S) for (auto& b : A.S->bodies) if (!b.gone && !b.isRound && !b.isCase && b.tmpl.empty() && b.m < 50) R.mass += b.m;
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
    R.lastYaw = R.pl.yaw; R.lastPitch = R.pl.pitch;
    DisableCursor();
}
void leaveRange() {
    EnableCursor();
    R.bullets.clear(); R.impacts.clear();
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
    if (!IsCursorHidden()) { DisableCursor(); skipLook = true; }
    if (IsKeyPressed(KEY_ESCAPE)) { A.showPause = true; return; }   // (the mouse is freed next frame, once)
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
    gmap::playerStep(R.pl, R.map, in, dt);
    // aiming: right mouse; a heavy gun comes up more slowly
    bool aimWant = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    float rate = 1.f / (0.12f + (float)R.mass * 0.05f);
    R.aim = std::clamp(R.aim + (aimWant ? rate : -rate) * dt, 0.f, 1.f);
    // the gun: the trigger is the left mouse button; R reloads, B works the action
    if (A.S) {
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
    drawGunOnly();
    rlDrawRenderBatchActive();
    rlPopMatrix();
    EndMode3D();
    mapEndViewport();
    mapRestoreLook();
}

void rangeHud() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight(), cx = W / 2, cy = H / 2;
    if (R.aim < 0.5f) { DrawCircleV({cx, cy}, 2.5f, Color{255, 255, 255, 200}); DrawCircleLinesV({cx, cy}, 2.5f, Color{0, 0, 0, 120}); }
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
        const char* help = "Left mouse fires, right mouse aims.  R reloads, B works the action, I inspects.  WASD to walk.  Esc for the menu.";
        float hw = viewTextWidth(help, 13);
        DrawRectangleRounded({cx - hw / 2 - 12, 14, hw + 24, 24}, 0.4f, 6, Color{20, 23, 28, 150});
        viewText(help, cx - hw / 2, 19, 13, Color{235, 238, 242, 230});
    }
}

void rangeMapChanged() { ME.version++; }   // the map meshes now hold the range's map: the editor builds its own again
