// Gunsmith3D - Test mode: running the simulation, grabbing parts, shot effects.
#include "app/app.hpp"
#include "app/perf.hpp"
#include "reliability.hpp"
#include "app/settings.hpp"
#include "app/audio.hpp"
#include "app/view.hpp"
#include "app/render.hpp"
#include "app/bench.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// Where the mouse puts a bolt handle's knob: the ray meets the cylinder the knob sweeps around the bore,
// giving how far to turn the bolt and how far along the bore to pull it, from one drag.
static void handleTarget(const BoltHandle& h, double& rollOut, V2& axialMM) {
    Ray r = mouseRay();
    Vector3 X = h.axis, o = Vector3Subtract(r.position, h.base), d = r.direction;
    Vector3 op = Vector3Subtract(o, Vector3Scale(X, Vector3DotProduct(o, X)));
    Vector3 dp = Vector3Subtract(d, Vector3Scale(X, Vector3DotProduct(d, X)));
    float A2 = Vector3DotProduct(dp, dp), B2 = 2 * Vector3DotProduct(op, dp), C2 = Vector3DotProduct(op, op) - h.radius * h.radius;
    float disc = B2 * B2 - 4 * A2 * C2, t;
    if (A2 < 1e-9f) t = 0;
    else if (disc >= 0) t = (-B2 - std::sqrt(disc)) / (2 * A2);  // the near side of the cylinder
    else t = -B2 / (2 * A2);                                     // outside it: the closest approach
    Vector3 P = Vector3Add(o, Vector3Scale(d, t));
    float along = Vector3DotProduct(P, X);
    Vector3 rad = Vector3Subtract(P, Vector3Scale(X, along));
    Vector3 Z{0, 0, 1}, Y = Vector3CrossProduct(Z, X);
    rollOut = std::atan2(Vector3DotProduct(rad, Y), Vector3DotProduct(rad, Z)) + h.restDeg * DEG2RAD;
    Vector3 foot = Vector3Add(h.base, Vector3Scale(X, along));
    axialMM = toMM(foot);
}

void testInput(bool overUI) {
    if (!A.S) return;
    if (A.dragKind == 6) {  // working a bolt by its handle
        sim::Body* b = A.handleBody;
        bool ok = false;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && b)
            for (auto& rb : renderList()) {
                BoltHandle h;
                if (rb.sb != b || !boltHandle(rb, h)) continue;
                double roll; V2 ax;
                handleTarget(h, roll, ax);
                while (roll - b->roll > PI) roll -= 2 * PI;  // the nearest way round
                while (roll - b->roll < -PI) roll += 2 * PI;
                A.S->rollHand.on = true; A.S->rollHand.b = b; A.S->rollHand.t = roll;
                A.S->rollHand.arm = h.radius / U / 1000;
                A.S->hand.on = true; A.S->hand.b = b; A.S->hand.l = {h.localMM / 1000, 0}; A.S->hand.t = {ax.x / 1000, ax.y / 1000};
                ok = true;
            }
        if (!ok) { A.S->hand.on = false; A.S->rollHand.on = false; A.dragKind = 0; A.handleBody = nullptr; A.grabTmpl = -1; }
        return;
    }
    if (A.dragKind == 3) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && A.S->hand.on) {
            V2 p = mouseMM(A.grabZ);
            A.S->hand.t = {p.x / 1000, p.y / 1000};
        } else { A.S->hand.on = false; A.dragKind = 0; A.grabTmpl = -1; }
        return;
    }
    TestHit h = overUI ? TestHit{} : testPick();
    A.hover = h.b;
    A.hoverHandle = h.handle;
    if (!overUI && h.b && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        A.running = true;
        A.grabTmpl = h.tmpl;
        if (h.handle) { A.dragKind = 6; A.handleBody = h.b; return; }
        V2 w{h.mm.x / 1000, h.mm.y / 1000};
        A.S->hand.on = true; A.S->hand.b = h.b; A.S->hand.l = sim::lpt(*h.b, w); A.S->hand.t = w;
        A.dragKind = 3; A.grabZ = h.z;
    }
}


// Work the action: your recorded animation, or the plain way worked out from the parts (what the reliability test does)
void workAction() {
    if (!A.S || sim::animPlaying(*A.S)) return;
    A.S->design.anims = A.design.anims;
    if (sim::playAnim(*A.S, "work_action")) { toast("Working the action (your animation)."); return; }
    Animation a = sim::autoWorkAction(*A.S);
    if (a.keys.empty()) { toast("Nothing to work: this gun has no moving breech."); return; }
    A.S->design.anims["auto:work_action"] = a;
    sim::playAnim(*A.S, "auto:work_action");
    A.running = true;
}
void stepSim(float dt) {
    if (!A.S || !A.running || (A.posing && !sim::animPlaying(*A.S))) return;   // (in Pose mode only an animation runs it)
    auto& S = *A.S;
    bool slow = CFG.slowOnShot && S.shots > 0 && (S.t - S.fireT) * 1000 < CFG.slowWindowMs;
    double speed = slow ? CFG.slowSpeed : CFG.benchSpeed;
    { perf::Scope t(perf::Physics); perf::substeps(sim::run(S, dt * speed, 1200)); }
    for (auto& p : S.pushers)   // a control held down that doesn't move: say why it might be
        if (p.on && !p.stuckTold && p.heldT > 0.35 && p.moved < 0.0003 && p.key != "fire" && p.b && p.b->d) {
            p.stuckTold = true;
            const BodyDef& d = *p.b->d;
            double dir = std::fmod(std::fmod(d.ctrlDir, 360) + 360, 360);
            toast((d.name.empty() ? d.id : d.name) + TextFormat(" is pressed but doesn't move. Its push direction is %.0f degrees; check that it can move that way (the arrow on it in the designer), and that a stop, catch or strong spring isn't holding it.", dir));
        }
    sim::tidy(S, dt, 10, CFG.magLife, CFG.maxLooseMags);   // real seconds: spent cases go after 10 s, magazines left alone after magLife; at most maxLooseMags lie out
    for (auto& e : S.events) {
        auto& br = A.design.breech;
        if (e.kind == sim::Ev::Bang) {
            A.flash = 0.12f;
            if (SFX.ok && A.sound) PlaySound(SFX.bang);
            for (size_t k = 0; k <= br.more.size(); k++) {  // smoke from each muzzle that fired just now
                bool fired = k < S.chambers.size() ? std::fabs(S.chambers[k].shotT - e.t) < 1e-9 : k == 0;
                const std::string& chb = k == 0 ? br.chamber : br.more[k - 1].chamber;
                V2 mz = k == 0 ? br.muzzle : br.more[k - 1].muzzle;
                if (!fired || !(k == 0 ? br.hasMuzzle : br.more[k - 1].hasMuzzle)) continue;
                Vector3 m = W3(curMM(chb, mz), 0);
                for (int i = 0; i < 6; i++) A.puffs.push_back({Vector3Add(m, {1.5f + i * 0.9f, 0, 0}), {2.5f + GetRandomValue(0, 30) / 10.f, 0.8f + GetRandomValue(0, 10) / 10.f, GetRandomValue(-10, 10) / 10.f}, 0.35f + i * 0.08f, 1.3f, 1.3f});
            }
        }
        if (e.kind == sim::Ev::Eject && S.physicalFeed && SFX.ok && A.sound) PlaySound(SFX.clack);  // the case is a real part
        if (e.kind == sim::Ev::ClipOut) { if (SFX.ok && A.sound) PlaySound(SFX.clack); toast("Ping: the empty clip flew out."); }
        if (e.kind == sim::Ev::Eject && br.hasFace && !S.physicalFeed) {
            V2 f = curMM(br.breech, br.face);
            Shell s;
            s.p = W3({f.x + br.roundLen * 0.4, f.y - br.roundDia}, 0.5f);
            s.v = {(float)GetRandomValue(-30, 10) / 10.f, 14 + GetRandomValue(0, 40) / 10.f, 9 + GetRandomValue(0, 40) / 10.f};
            s.dir = {1, 0, 0}; s.spin = 0; s.life = 6; s.len = (float)br.roundLen * 0.62f * U; s.r = (float)br.roundDia / 2 * U;
            s.axisSpin = Vector3Normalize({(float)GetRandomValue(-10, 10), (float)GetRandomValue(-10, 10), 10});
            A.shells.push_back(s);
            if (SFX.ok && A.sound) PlaySound(SFX.clack);
        }
        if (e.kind == sim::Ev::Malfunction && S.malf != sim::Malf::None) {
            toast(std::string("Jam: ") + sim::malfName(S.malf) + ". Rack the action or pull the stuck round out by hand.");
            if (SFX.ok && A.sound) PlaySound(SFX.click);
        }
        if (e.kind == sim::Ev::Click) { A.clickT = 1.0f; if (SFX.ok && A.sound) PlaySound(SFX.click); }
        if ((e.kind == sim::Ev::EjectLive || e.kind == sim::Ev::Chambered) && SFX.ok && A.sound) PlaySound(SFX.clack);
        if (e.kind == sim::Ev::MagLoaded && SFX.ok && A.sound) PlaySound(SFX.click);
    }
    S.events.clear();
    std::vector<float> row;
    for (auto& b : S.bodies) if (mainPart(b)) row.push_back((float)((b.x - b.x0) * 1000));
    A.trace.push_back({S.t, row});
    double win = std::max(0.12, CFG.slowWindowMs / 1000.0 * 1.2);
    while (A.trace.size() > 2 && A.trace.front().first < S.t - win) A.trace.erase(A.trace.begin());
}
void stepEffects(float dt) {
    A.flash = std::max(0.f, A.flash - dt);
    A.clickT = std::max(0.f, A.clickT - dt);
    A.toastT = std::max(0.f, A.toastT - dt);
    float py = plateY();
    for (auto& s : A.shells) {
        s.v.y -= 60 * dt;
        s.p = Vector3Add(s.p, Vector3Scale(s.v, dt));
        s.dir = Vector3Normalize(Vector3RotateByAxisAngle(s.dir, s.axisSpin, 14 * dt * std::min(1.f, Vector3Length(s.v) / 5)));
        if (s.p.y < py + s.r) { s.p.y = py + s.r; s.v.y = -s.v.y * 0.35f; s.v.x *= 0.6f; s.v.z *= 0.6f; s.dir.y = 0; s.dir = Vector3Normalize(s.dir); }
        s.life -= dt;
    }
    A.shells.erase(std::remove_if(A.shells.begin(), A.shells.end(), [](const Shell& s) { return s.life <= 0; }), A.shells.end());
    for (auto& p : A.puffs) { p.p = Vector3Add(p.p, Vector3Scale(p.v, dt)); p.r += dt * 0.8f; p.life -= dt; }
    A.puffs.erase(std::remove_if(A.puffs.begin(), A.puffs.end(), [](const Puff& p) { return p.life <= 0; }), A.puffs.end());
}
