// Gunsmith3D - First-person movement (see player.hpp).
#include "player.hpp"

#include <algorithm>
#include <cmath>

namespace gmap {

static double stanceH(Stance s, const PlayerTuning& T) { return s == Stance::Stand ? T.standH : s == Stance::Crouch ? T.crouchH : T.proneH; }
static double stanceEye(Stance s, const PlayerTuning& T) { return s == Stance::Stand ? T.standEye : s == Stance::Crouch ? T.crouchEye : T.proneEye; }
double playerHeight(const Player& p, const PlayerTuning& T) { return stanceH(p.stance, T); }

static V3 halfOf(const Player& p, const PlayerTuning& T) { return {T.halfWidth, stanceH(p.stance, T) / 2, T.halfWidth}; }
static V3 centreOf(const Player& p, const PlayerTuning& T) { return p.feet + V3{0, stanceH(p.stance, T) / 2, 0}; }

V3 playerForward(const Player& p) {
    return {std::cos(p.pitch) * std::cos(p.yaw), std::sin(p.pitch), std::cos(p.pitch) * std::sin(p.yaw)};
}

void playerSpawn(Player& p, const Map& m) {
    p = Player();
    for (auto& e : m.ents)
        if (e.type == "player_start") { p.feet = e.pos; p.yaw = e.yaw * 3.14159265358979 / 180; break; }
    p.feet.y += 0.01;
}

// Slide along everything hit, up to four times per step (Quake's PM_SlideMove, simplified).
static void slideMove(Player& p, const Map& m, double dt, const PlayerTuning& T) {
    V3 half = halfOf(p, T);
    V3 planes[5];
    int np = 0;
    double left = dt;
    for (int it = 0; it < 4 && left > 0; it++) {
        V3 c = centreOf(p, T), end = c + p.vel * left;
        Hit h = sweepBox(m, c, end, half);
        if (h.startSolid && h.t == 0) { p.vel = {0, 0, 0}; return; }  // stuck inside something: don't make it worse
        if (h.t > 0) p.feet += (end - c) * h.t;
        if (h.brush < 0) break;
        left -= left * h.t;
        if (np < 5) planes[np++] = h.n;
        // take out the part of the velocity going into each plane touched so far
        V3 v = p.vel;
        for (int i = 0; i < np; i++) {
            double into = dot(v, planes[i]);
            if (into < 0) v -= planes[i] * (into * 1.001);
        }
        // two walls meeting in a crease: slide along the crease
        for (int i = 0; i < np; i++)
            for (int j = i + 1; j < np; j++)
                if (dot(v, planes[i]) < -1e-6 || dot(v, planes[j]) < -1e-6) {
                    V3 dir = norm(cross(planes[i], planes[j]));
                    v = dir * dot(dir, p.vel);
                }
        p.vel = v;
    }
}

// Try the move as it is and lifted by a step; keep whichever goes further (Quake's PM_StepSlideMove).
static void stepSlideMove(Player& p, const Map& m, double dt, const PlayerTuning& T) {
    Player flat = p;
    slideMove(flat, m, dt, T);
    if (!p.onGround) { p = flat; return; }
    Player up = p;
    V3 half = halfOf(p, T), c = centreOf(p, T);
    Hit hu = sweepBox(m, c, c + V3{0, T.stepHeight, 0}, half);
    up.feet.y += T.stepHeight * hu.t;
    up.vel.y = 0;
    slideMove(up, m, dt, T);
    V3 cu = centreOf(up, T);
    Hit hd = sweepBox(m, cu, cu - V3{0, T.stepHeight + 0.02, 0}, half);
    up.feet.y -= (T.stepHeight + 0.02) * hd.t;
    bool landed = hd.brush >= 0 && hd.n.y > 0.7;
    double dFlat = std::hypot(flat.feet.x - p.feet.x, flat.feet.z - p.feet.z), dUp = std::hypot(up.feet.x - p.feet.x, up.feet.z - p.feet.z);
    if (landed && dUp > dFlat + 1e-4) { up.vel.y = std::min(0.0, flat.vel.y); p = up; }
    else p = flat;
}

static void groundCheck(Player& p, const Map& m, const PlayerTuning& T) {
    V3 half = halfOf(p, T), c = centreOf(p, T);
    Hit h = sweepBox(m, c, c - V3{0, 0.06, 0}, half);
    p.onGround = h.brush >= 0 && h.n.y > 0.7 && p.vel.y <= 0.5;
    if (p.onGround) { p.feet.y -= 0.06 * h.t; if (p.vel.y < 0) p.vel.y = 0; }
}

static bool fits(const Player& p, Stance s, const Map& m, const PlayerTuning& T) {
    Player q = p;
    q.stance = s;
    return boxFree(m, centreOf(q, T), halfOf(q, T));
}

void playerStep(Player& p, const Map& m, const PlayerInput& in, double dt, const PlayerTuning& T) {
    dt = std::clamp(dt, 0.0, 0.05);
    // stance: going lower always works; standing back up needs headroom
    Stance want = in.want;
    if (want != p.stance) {
        bool lower = stanceH(want, T) < stanceH(p.stance, T);
        if (lower || fits(p, want, m, T)) p.stance = want;
        else if (want == Stance::Stand && p.stance == Stance::Prone && fits(p, Stance::Crouch, m, T)) p.stance = Stance::Crouch;
    }
    double eyeT = stanceEye(p.stance, T);
    p.eye += std::clamp(eyeT - p.eye, -3.0 * dt, 3.0 * dt);
    double leanT = p.stance == Stance::Prone ? 0 : std::clamp(in.lean, -1.0, 1.0);
    p.lean += std::clamp(leanT - p.lean, -5.0 * dt, 5.0 * dt);

    groundCheck(p, m, T);
    // wished direction on the ground plane
    V3 fwd{std::cos(p.yaw), 0, std::sin(p.yaw)}, right{-std::sin(p.yaw), 0, std::cos(p.yaw)};
    V3 wish = fwd * in.forward + right * in.right;
    double wl = len(wish);
    if (wl > 1) wish = wish * (1 / wl), wl = 1;
    double speed = p.stance == Stance::Prone ? T.proneSpeed : p.stance == Stance::Crouch ? T.crouchSpeed : (in.sprint && in.forward > 0 ? T.sprint : T.walk);
    if (std::fabs(p.lean) > 0.3) speed = std::min(speed, T.crouchSpeed);
    if (p.onGround) {
        // friction, then accelerate towards the wished velocity
        double sp = std::hypot(p.vel.x, p.vel.z);
        if (sp > 0) { double drop = std::max(sp, 1.0) * T.friction * dt, ns = std::max(0.0, sp - drop) / sp; p.vel.x *= ns; p.vel.z *= ns; }
        if (wl > 0) {
            V3 dir = wish * (1 / wl);
            double cur = dot(p.vel, dir), add = speed * wl - cur;
            if (add > 0) { double a = std::min(add, T.accel * speed * dt); p.vel += dir * a; }
        }
        if (in.jump && p.stance == Stance::Stand) { p.vel.y = T.jumpSpeed; p.onGround = false; }
    } else {
        if (wl > 0) {
            V3 dir = wish * (1 / wl);
            double cur = dot(p.vel, dir), add = speed * wl - cur;
            if (add > 0) p.vel += dir * std::min(add, T.airAccel * speed * dt);
        }
        p.vel.y -= T.gravity * dt;
    }
    stepSlideMove(p, m, dt, T);
    groundCheck(p, m, T);
    if (p.feet.y < -200) playerSpawn(p, m);  // fell out of the world
}

V3 playerEye(const Player& p, const Map& m, double* roll, const PlayerTuning& T) {
    V3 base = p.feet + V3{0, p.eye, 0};
    V3 right{-std::sin(p.yaw), 0, std::cos(p.yaw)};
    V3 off = right * (p.lean * T.leanDist) + V3{0, -std::fabs(p.lean) * 0.08, 0};
    V3 half{0.08, 0.08, 0.08};
    Hit h = sweepBox(m, base, base + off, half);
    if (roll) *roll = p.lean * T.leanRollDeg * 3.14159265358979 / 180 * (h.brush >= 0 ? h.t : 1);
    return base + off * (h.brush >= 0 ? h.t : 1.0);
}

}  // namespace gmap
