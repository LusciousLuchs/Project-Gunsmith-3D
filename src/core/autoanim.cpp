// Gunsmith3D - animations made for you (see autoanim.hpp).
#include "autoanim.hpp"
#include "hand.hpp"
#include "solver.hpp"
#include "reliability.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>

static std::string fmtText(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    return buf;
}

namespace {
// ---------------------------------------------------------------- the gun as solid slabs (outline x thickness)
struct Slab { const BodyDef* b; double x0, y0, x1, y1, z0, z1; };
struct Shape { std::vector<Slab> s; };
Shape shapeOf(const Design& d) {
    Shape S;
    for (auto& b : d.bodies) {
        if (b.pts.size() < 3) continue;
        Slab k{&b, 1e9, 1e9, -1e9, -1e9, b.z - b.thick / 2, b.z + b.thick / 2};
        for (auto& p : b.pts) { k.x0 = std::min(k.x0, p.x); k.x1 = std::max(k.x1, p.x); k.y0 = std::min(k.y0, p.y); k.y1 = std::max(k.y1, p.y); }
        S.s.push_back(k);
    }
    return S;
}
double segDist(double px, double py, V2 a, V2 b) {
    double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? std::clamp(((px - a.x) * dx + (py - a.y) * dy) / l2, 0.0, 1.0) : 0;
    double ex = a.x + dx * t - px, ey = a.y + dy * t - py;
    return std::sqrt(ex * ex + ey * ey);
}
// is a ball of radius r at p (gun mm) into a part?
bool inGun(const Shape& S, HV p, double r) {
    for (auto& k : S.s) {
        if (p.x < k.x0 - r || p.x > k.x1 + r || p.y < k.y0 - r || p.y > k.y1 + r || p.z < k.z0 - r || p.z > k.z1 + r) continue;
        if (sim::inPoly(p.x, p.y, k.b->pts)) return true;
        const auto& P = k.b->pts;
        for (size_t i = 0; i < P.size(); i++) if (segDist(p.x, p.y, P[i], P[(i + 1) % P.size()]) < r) return true;
    }
    return false;
}
const char* whichOf(int side) { return side > 0 ? "firing" : "support"; }
// bits of the hand (or one digit of it, digit >= 0) into the gun
int clipsOf(const Shape& S, const Design& d, const HandHold& h, int side, int digit = -2) {
    if (!h.on) return 0;
    HandFrame f = handFrame(d, h, side);
    if (!f.on) return 0;
    DigitPose dg[DIGIT_COUNT];
    digitsOf(h, whichOf(side), dg);
    int n = 0;
    for (auto& b : handBones(dg)) {
        if (digit >= -1 && b.digit != digit) continue;
        double r = std::min(b.radius, 10.0) * 0.7;
        for (double t : {0.3, 0.65, 1.0}) {
            HV p{b.a.x + (b.b.x - b.a.x) * t, b.a.y + (b.b.y - b.a.y) * t, b.a.z + (b.b.z - b.a.z) * t};
            if (inGun(S, handToGun(f, p), r)) n++;
        }
    }
    return n;
}
// curl each finger (and the thumb) from straight until it touches the gun, no further
void wrapFingers(const Shape& S, const Design& d, HandHold& h, int side) {
    if (!h.on) return;
    if (!h.digitsSet) { digitsOf(h, whichOf(side), h.digits); h.digitsSet = true; }
    static const double FULL[3] = {80, 95, 65}, THUMB[3] = {30, 40, 35};
    for (int dg = 0; dg < DIGIT_COUNT; dg++) {
        DigitPose base = h.digits[dg];
        const double* F = dg == DIGIT_THUMB ? THUMB : FULL;
        double best = 0;
        for (double t = 0; t <= 1.0001; t += 0.05) {
            DigitPose p = base;
            for (int s = 0; s < 3; s++) p.bend[s] = F[s] * t;
            clampDigit(dg, p);
            h.digits[dg] = p;
            if (clipsOf(S, d, h, side, dg) > 0) break;
            best = t;
        }
        DigitPose p = base;
        for (int s = 0; s < 3; s++) p.bend[s] = F[s] * best;
        clampDigit(dg, p);
        h.digits[dg] = p;
    }
}
void openFingers(const Design& d, HandHold& h, int side, double k = 0.25) {
    if (!h.digitsSet) { digitsOf(h, whichOf(side), h.digits); h.digitsSet = true; }
    for (int dg = 0; dg < DIGIT_COUNT; dg++) { for (int s = 0; s < 3; s++) h.digits[dg].bend[s] *= k; clampDigit(dg, h.digits[dg]); }
    (void)d;
}
HandHold& handOf(AnimKey& k, int hand) { return hand == 0 ? k.firing : k.support; }
bool& gripOf(AnimKey& k, int hand) { return hand == 0 ? k.gripF : k.gripS; }
void sortKeys(Animation& a) { std::stable_sort(a.keys.begin(), a.keys.end(), [](const AnimKey& x, const AnimKey& y) { return x.t < y.t; }); }
AnimKey keyAt(const Design& d, const Animation& a, double t) {   // a key that changes nothing: where everything is at t
    AnimHands H = animAt(d, a, t);
    AnimKey k; k.t = t; k.firing = H.firing; k.support = H.support; k.gripF = H.gripF; k.gripS = H.gripS; k.turn = H.turn;
    for (int q = 0; q < 6; q++) k.gun[q] = H.gun[q];
    return k;
}
// bits of hand into the gun over the animation, beyond what each hand's own hold already has; a gripping hand rides
// with its part, so it's counted once where it takes hold rather than against the part as drawn at rest
int scanClips(const Shape& S, const Design& d, const Animation& a, double dt, double* worstT = nullptr, int* worstHand = nullptr, bool worstFree = false) {
    if (a.keys.empty()) return 0;
    int base[2] = {clipsOf(S, d, d.pose.firing, 1), clipsOf(S, d, d.pose.support, -1)};
    int total = 0, worst = 0;
    double end = animLength(a);
    bool counted[2] = {false, false};
    for (double t = 0; t <= end + 1e-9; t += dt) {
        AnimHands H = animAt(d, a, t);
        for (int k = 0; k < 2; k++) {
            const HandHold& h = k == 0 ? H.firing : H.support;
            bool grip = k == 0 ? H.gripF : H.gripS;
            if (grip && counted[k]) continue;
            int c = std::max(0, clipsOf(S, d, h, k == 0 ? 1 : -1) - base[k]);
            if (grip) counted[k] = true;
            total += c;
            if (c > worst && !(worstFree && grip)) { worst = c; if (worstT) *worstT = t; if (worstHand) *worstHand = k; }
        }
    }
    return total;
}
// a hand passing through the gun between keys is lifted off its side there (a key in the middle of that stretch)
void route(const Shape& S, const Design& d, Animation& a) {
    for (int pass = 0; pass < 8; pass++) {
        double wt = -1; int wh = -1;
        int c = scanClips(S, d, a, 0.02, &wt, &wh, true);   // (where a hand holds its part, it is where the part is: not moved)
        if (c == 0 || wt < 0 || wh < 0) return;
        // already a key right there? lift that one further
        int at = -1;
        for (size_t i = 0; i < a.keys.size(); i++) if (std::fabs(a.keys[i].t - wt) < 0.03) at = (int)i;
        if (at < 0) { a.keys.push_back(keyAt(d, a, wt)); sortKeys(a); for (size_t i = 0; i < a.keys.size(); i++) if (std::fabs(a.keys[i].t - wt) < 1e-9) at = (int)i; }
        if (at < 0) return;
        HandHold& h = handOf(a.keys[at], wh);
        if (!h.on) return;
        h.dz += 30;
        openFingers(d, h, wh == 0 ? 1 : -1, 0.5);
    }
}
// play an animation on the gun and see how far the action opens, and whether it ends home
struct Played { double opened = 0, end = 0; bool home = false, magIn = false, chambered = false; };
Played play(const Design& d0, const Animation& a, const std::string& slot, bool emptyMag) {
    Design d = d0;
    d.anims[slot] = a;
    Played r;
    auto S = sim::build(d);
    if (!S) return r;
    sim::run(*S, 0.3);
    if (emptyMag) {
        if (sim::Body* m = sim::seatedMag(*S)) m->rounds = 0;
        S->chambered = false;
    }
    sim::playAnim(*S, slot, true);   // (exactly this one, not another version)
    for (int i = 0; i < 800 && sim::animPlaying(*S); i++) { sim::run(*S, 0.01); r.opened = std::max(r.opened, sim::breechGap(*S)); }
    sim::run(*S, 0.6);
    r.end = sim::breechGap(*S);
    r.home = r.end < 1.5;
    sim::Body* m = sim::seatedMag(*S);
    r.magIn = m && m->rounds > 0;
    r.chambered = S->chambered;
    return r;
}
// which hand takes hold in an animation (the first that grips), or -1
int grippingHand(const Animation& a) {
    for (auto& k : a.keys) { if (k.gripS) return 1; if (k.gripF) return 0; }
    return -1;
}
// the plain way's keys made gentler on the gun: a hand comes in lifted off the side with its fingers open, takes hold
// with its fingers curled round the part to touch, and leaves the same way
Animation dressed(const Shape& S, const Design& d, const Animation& plain, double slow, double longer, bool overhand) {
    Animation a = plain;
    int hand = grippingHand(a);
    if (hand < 0 || a.keys.empty()) return a;
    int side = hand == 0 ? 1 : -1;
    // slower, and a longer pull (the farthest key pulled on past where the plain way stops)
    double t0 = a.keys.front().t;
    for (auto& k : a.keys) k.t = t0 + (k.t - t0) * slow;
    if (longer > 0) {
        V2 g0 = handOf(a.keys.front(), hand).at;
        for (auto& k : a.keys) {
            HandHold& h = handOf(k, hand);
            double dx = h.at.x - g0.x, dy = h.at.y - g0.y, L = std::hypot(dx, dy);
            if (L > 3) { h.at.x += dx / L * longer; h.at.y += dy / L * longer; }
        }
    }
    // the grip: a slide (not a handle, not a turning bolt) is taken overhand, palm on its side, fingers over its top
    if (overhand) {
        for (auto& k : a.keys) {
            HandHold& h = handOf(k, hand);
            h.angle = 0; h.under = true;
        }
    }
    // the way the hand lies on the part where it takes hold: of eight turns of the grip line (and lifted off the side a
    // little if it must be), the one whose fingers, curled to touch, end up clearest of the gun; the nearest to the plain
    // way's on a tie
    for (auto& k0 : a.keys) {
        if (!gripOf(k0, hand)) continue;
        HandHold h0 = handOf(k0, hand);
        double bestA = h0.angle, bestDz = h0.dz; bool bestUnder = h0.under; int bestC = 1 << 30; double bestTurn = 1e9;
        for (int lift = 0; lift < 3 && bestC > 0; lift++)
            for (int q = 0; q < 8; q++) {
                for (int u = 0; u < 2; u++) {
                    HandHold h = h0;
                    h.angle = std::fmod(h0.angle + q * 45, 360.0); h.dz = h0.dz + lift * 6; h.under = u ? !h0.under : h0.under;
                    h.digitsSet = false;
                    wrapFingers(S, d, h, side);
                    int c = clipsOf(S, d, h, side);
                    double turn = std::fabs(std::remainder(h.angle - h0.angle, 360.0)) + lift * 20 + u * 10;
                    if (c < bestC || (c == bestC && turn < bestTurn)) { bestC = c; bestA = h.angle; bestDz = h.dz; bestUnder = h.under; bestTurn = turn; }
                }
            }
        for (auto& k : a.keys) { HandHold& h = handOf(k, hand); h.angle = bestA; h.under = bestUnder; h.dz += bestDz - h0.dz; }
        break;
    }
    // fingers round the part where it takes hold (the same all the way through the pull: they ride with it)
    HandHold held;
    bool have = false;
    for (auto& k : a.keys) {
        HandHold& h = handOf(k, hand);
        if (gripOf(k, hand)) {
            if (!have) { wrapFingers(S, d, h, side); held = h; have = true; }
            else { h.digitsSet = true; for (int q = 0; q < DIGIT_COUNT; q++) h.digits[q] = held.digits[q]; }
        } else if (have) { h.digitsSet = true; for (int q = 0; q < DIGIT_COUNT; q++) h.digits[q] = held.digits[q]; openFingers(d, h, side, 0.4); }
    }
    // in and out: lifted off the side, fingers open
    AnimKey in = a.keys.front(), out = a.keys.back();
    for (auto& k : a.keys) k.t += 0.2;
    in.t = std::max(0.05, a.keys.front().t - 0.2);
    gripOf(in, hand) = false; in.turn = -1;
    { HandHold& h = handOf(in, hand); h.dz += 30; if (have) { h.digitsSet = true; for (int q = 0; q < DIGIT_COUNT; q++) h.digits[q] = held.digits[q]; } openFingers(d, h, side, 0.2); }
    out.t = a.keys.back().t + 0.18;
    gripOf(out, hand) = false; out.turn = -1;
    { HandHold& h = handOf(out, hand); h.dz += 30; openFingers(d, h, side, 0.3); }
    a.keys.insert(a.keys.begin(), in);
    a.keys.push_back(out);
    // off its own hold and back onto it straight out from the gun's side, the fingers as the hold has them (a hold that
    // already has its fingers in a part only closes the gap, not sweeping them through on the way)
    const HandHold& hold = hand == 0 ? d.pose.firing : d.pose.support;
    if (hold.on) {
        for (auto& k : a.keys) k.t += 0.12;
        AnimKey up = keyAt(d, a, 0), down = keyAt(d, a, 0);
        up.t = 0.1; down.t = a.keys.back().t + 0.2;
        for (AnimKey* k : {&up, &down}) {
            HandHold& h = handOf(*k, hand);
            h = hold; h.dz += 22;
            if (!h.digitsSet) { digitsOf(hold, whichOf(side), h.digits); h.digitsSet = true; }
            gripOf(*k, hand) = false; k->turn = -1;
        }
        a.keys.insert(a.keys.begin(), up);
        a.keys.push_back(down);
    }
    sortKeys(a);
    route(S, d, a);
    return a;
}
bool slideLike(sim::Sim& S) {   // the action is pulled straight back by its own body (not a handle, not a bolt that turns, not hinged)
    if (!S.BR) return false;
    for (auto& b : S.bodies) if (b.d && b.d->ctrl == "handle" && !b.fixed) return false;
    const sim::Body* B = S.BR->root ? S.BR->root : S.BR;
    return B->invIr <= 0 && B->d && !B->d->rollOn;
}
Animation workAction(const Design& d, AutoAnimReport& R, const std::string& slot) {
    auto Sp = sim::build(d);
    if (!Sp) { R.note = "The gun couldn't be built."; return {}; }
    sim::run(*Sp, 0.2);
    Animation plain = sim::autoWorkAction(*Sp);
    if (plain.keys.empty()) { R.note = "Nothing to work: this gun has no moving breech."; return {}; }
    Shape S = shapeOf(d);
    Played ref = play(d, plain, slot, false);
    R.needed = ref.opened;
    R.clipsBefore = scanClips(S, d, plain, 0.02);
    bool slide = slideLike(*Sp);
    struct Try { double slow, longer; bool over; };
    std::vector<Try> tries;
    if (slide) tries.push_back({1.0, 0, true});
    tries.push_back({1.0, 0, false});
    tries.push_back({1.4, 8, slide});
    tries.push_back({1.4, 8, false});
    Animation best; double bestScore = -1e18; Played bestP;
    for (auto& t : tries) {
        Animation a = dressed(S, d, plain, t.slow, t.longer, t.over);
        Played p = play(d, a, slot, false);
        int c = scanClips(S, d, a, 0.02);
        bool works = p.opened >= ref.opened * 0.9 - 0.5 && p.home;
        double score = (works ? 1e6 : 0) - c * 10 - t.slow * 5 - t.longer;
        if (score > bestScore) { bestScore = score; best = a; bestP = p; R.clipsAfter = c; }
        if (works && c == 0) break;
    }
    R.opened = bestP.opened;
    R.ok = bestP.opened >= ref.opened * 0.9 - 0.5 && bestP.home;
    if (R.ok && ref.opened < 1) R.note = "Made. (This action hardly opens by hand: the plain way doesn't open it either.)";
    else if (R.ok) R.note = fmtText("Made: the action opens %.0f mm and comes home.", bestP.opened);
    else if (!bestP.home) R.note = fmtText("Made, but the action didn't come home afterwards (%.1f mm open): check its spring or what it rubs on.", bestP.end);
    else R.note = fmtText("Made, but the hand only opens it %.0f mm of the %.0f mm the plain way does.", bestP.opened, ref.opened);
    return best;
}
// reloading: the hand goes to the magazine's base, it's dropped, the hand fetches one from the belt and pushes it home
Animation reload(const Design& d, AutoAnimReport& R, bool empty) {
    const JointDef* well = nullptr;
    for (auto& j : d.joints) if (j.type == "magwell") { well = &j; break; }
    int mi = well ? d.bodyIndex(well->a) : -1;
    if (mi < 0) { R.note = "This gun has no magazine to change: R loads it by hand."; return {}; }
    const BodyDef& mag = d.bodies[mi];
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& p : mag.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    int hand = d.pose.support.on ? 1 : 0, side = hand == 0 ? 1 : -1;
    Shape S = shapeOf(d);
    auto key = [&](double t) { AnimKey k; k.t = t; k.firing = d.pose.firing; k.support = d.pose.support; return k; };
    HandHold base = hand == 0 ? d.pose.firing : d.pose.support;
    HandHold at = base;
    at.on = true; at.free = false; at.part = mag.id; at.angle = 0; at.under = false; at.dz = 0; at.twist = 0; at.elbow = 0;
    at.at = {(x0 + x1) / 2, y1 - 14};
    // the hand round the magazine's base
    HandHold onMag = at; wrapFingers(S, d, onMag, side);
    Animation a;
    AnimKey k1 = key(0.2); handOf(k1, hand) = onMag; handOf(k1, hand).dz = 30; openFingers(d, handOf(k1, hand), side, 0.2);
    AnimKey k2 = key(0.38); handOf(k2, hand) = onMag; k2.event = "dropmag";
    AnimKey k3 = key(0.8); { HandHold& h = handOf(k3, hand); h = base; h.on = false; h.free = true; h.pos[0] = 80; h.pos[1] = -560; h.pos[2] = side * 170; }
    AnimKey k4 = key(1.2); handOf(k4, hand) = onMag; handOf(k4, hand).at.y = y1 + 40;   // a fresh one, below the well
    AnimKey k5 = key(1.42); handOf(k5, hand) = onMag; k5.event = "freshmag";            // pushed home
    AnimKey k6 = key(1.6); handOf(k6, hand) = onMag; handOf(k6, hand).dz = 25; openFingers(d, handOf(k6, hand), side, 0.3);
    a.keys = {k1, k2, k3, k4, k5, k6};
    route(S, d, a);
    if (empty) {   // and then the action worked to chamber a round
        AutoAnimReport w;
        Animation wa = workAction(d, w, "reload_empty");
        if (!wa.keys.empty()) {
            double off = a.keys.back().t + 0.1 - wa.keys.front().t;
            for (auto k : wa.keys) { k.t += off; a.keys.push_back(k); }
            sortKeys(a);
        }
    }
    Played p = play(d, a, empty ? "reload_empty" : "reload", true);
    R.clipsAfter = scanClips(S, d, a, 0.02);
    R.ok = p.magIn && (!empty || p.chambered);
    if (R.ok) R.note = empty ? "Made: a fresh magazine goes in and a round is chambered." : "Made: a fresh magazine goes in.";
    else if (!p.magIn) R.note = "Made, but the fresh magazine didn't stay in: check the magazine catch.";
    else R.note = "Made, but no round was chambered after: work the action by hand to check it feeds.";
    return a;
}
}  // namespace

bool canAutoAnimate(const std::string& slotIn) {
    std::string slot = animBase(slotIn);
    return slot == "work_action" || slot == "reload" || slot == "reload_empty" || slot == "inspect" || slot == "clear_jam";
}
Animation autoAnimate(const Design& d, const std::string& slotIn, AutoAnimReport* rep) {
    AutoAnimReport R;
    const std::string slot = animBase(slotIn);   // (a second version of a reload is made the same way)
    Animation a;
    if (slot == "work_action") a = workAction(d, R, slot);
    else if (slot == "clear_jam") {   // worked twice, briskly
        a = workAction(d, R, slot);
        if (!a.keys.empty()) {
            Animation b = a;
            double off = a.keys.back().t + 0.05 - b.keys.front().t;
            for (auto k : b.keys) { k.t += off; a.keys.push_back(k); }
            sortKeys(a);
            if (R.ok) R.note = "Made: the action is worked twice, to clear what's stuck.";
        }
    } else if (slot == "reload") a = reload(d, R, false);
    else if (slot == "reload_empty") a = reload(d, R, true);
    else if (slot == "inspect") {
        a = defaultInspect(d);
        Shape S = shapeOf(d);
        R.clipsAfter = scanClips(S, d, a, 0.02);
        R.ok = true;
        R.note = "Made: the gun is brought up and turned to look it over.";
    } else R.note = "That one can't be made for you yet.";
    if (rep) *rep = R;
    return a;
}
int handClips(const Design& d, const HandHold& h, int side) { return clipsOf(shapeOf(d), d, h, side); }
int animClips(const Design& d, const Animation& a, double dt) { return scanClips(shapeOf(d), d, a, dt); }

