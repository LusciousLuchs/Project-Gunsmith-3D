// Gunsmith3D - reliability test (see reliability.hpp).
#include "reliability.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace sim {

namespace {

struct Runner {
    Sim& S;
    Body* B;               // the breech assembly the shooter works
    V2 dir;                // bore direction (toward the muzzle)
    double stroke;         // how far the breech can come back (m)
    bool rolling;          // a turn bolt: lift the handle first
    const std::atomic<bool>* cancel;
    // a hinged part (break-open barrels, a tip-up frame): swung open about its pin instead of drawn straight back
    bool hinged = false;
    V2 hinge, grab0, grabL;  // the pin (world), and where the hand holds the part (closed position, world; and local)
    double sign = 1;         // which way it swings open

    void findHinge() {
        for (auto& J : S.joints) {
            if (J.type != JT::Pin || !J.A || !J.B) continue;
            Body* a = J.A->root ? J.A->root : J.A;
            Body* b = J.B->root ? J.B->root : J.B;
            Body* other = a == B ? b : b == B ? a : nullptr;
            if (!other || !(other->fixed || other == &S.world)) continue;
            hinge = a == B ? wpt(*J.B, J.lb) : wpt(*J.A, J.la);
            hinged = true;
            break;
        }
        if (!hinged) return;
        const Body* hp = nullptr;  // the part marked "worked by hand", where the hand holds it
        for (auto& b : S.bodies) if (b.d && b.d->ctrl == "handle" && (&b == B || b.root == B)) { hp = &b; break; }
        grab0 = hp ? V2{hp->d->ctrlPt.x / 1000, hp->d->ctrlPt.y / 1000} : V2{B->x, B->y};
        grabL = lpt(*B, grab0);
        V2 r{grab0.x - hinge.x, grab0.y - hinge.y}, t{-r.y, r.x};
        double dir = hp ? hp->d->ctrlDir * 3.14159265358979 / 180 : 0;
        sign = t.x * std::cos(dir) + t.y * std::sin(dir) >= 0 ? 1 : -1;
    }
    void swing(double to) {  // turn the part about its pin to `to` radians open, at a hand's pace
        S.hand = {true, B, grabL, wpt(*B, grabL)};
        double from = B->a * sign, r = std::hypot(grab0.x - hinge.x, grab0.y - hinge.y);
        int n = std::max(1, (int)(std::fabs(to - from) * r / 1.5 / 0.005));
        for (int k = 1; k <= n; k++) {
            double ph = sign * (from + (to - from) * k / n), c = std::cos(ph), s = std::sin(ph);
            V2 d{grab0.x - hinge.x, grab0.y - hinge.y};
            S.hand.t = {hinge.x + c * d.x - s * d.y, hinge.y + s * d.x + c * d.y};
            run(S, 0.005);
        }
    }

    bool stop() const { return cancel && cancel->load(); }

    // move the hand holding the breech from where it is to `to` at about 1.5 m/s, like a shooter's hand
    void handMove(V2 to) {
        V2 from{B->x, B->y};
        if (!S.hand.on || S.hand.b != B) S.hand = {true, B, {0, 0}, from};
        double dist = std::hypot(to.x - from.x, to.y - from.y);
        int n = std::max(1, (int)(dist / 1.5 / 0.005));
        for (int k = 1; k <= n; k++) {
            S.hand.t = {from.x + (to.x - from.x) * k / n, from.y + (to.y - from.y) * k / n};
            run(S, 0.005);
        }
    }
    V2 home(double ahead) const { return {B->x0 + dir.x * ahead, B->y0 + dir.y * ahead}; }
    void open() {  // lift the handle and draw the breech all the way back; the hand keeps hold
        if (hinged) { swing(80 * 3.14159265358979 / 180); run(S, 0.15); return; }  // (as far as it goes: a stop you drew holds it sooner)
        if (rolling) { S.rollHand = {true, B, B->rmax, 0}; run(S, 0.15); }
        handMove(home(-(stroke + 0.003)));
        run(S, 0.08);
    }
    void close() {
        if (hinged) {  // swing it shut and press it home
            swing(-0.01);
            run(S, 0.1);
            S.hand.on = false;
            run(S, 0.2);
            return;
        }
        if (rolling) {
            handMove(home(0.005));
            run(S, 0.15);
            S.rollHand = {true, B, B->rmin, 0}; run(S, 0.18);
            S.rollHand.on = false;
        }
        else if (!S.breechSprung) {  // nothing springs it shut (a straight-pull bolt, a sliding breech): push it home
            handMove(home(0.005));
            run(S, 0.1);
        }
        S.hand.on = false;  // a self-loader's spring closes it; a bolt is already home
        run(S, rolling ? 0.05 : 0.35);
    }
    void cycle() { open(); close(); }
    // pick out anything stuck in the action: rounds or cases loose in it, or hanging on the extractor
    // (hard: also strip the top round off the magazine, for one that has tipped up and wedged)
    void pickOut(bool hard = false) {
        for (int pass = 0; pass < 3; pass++) {
            Body* stuck = nullptr;
            for (auto& b : S.bodies)
                if (b.isRound && !b.gone && (b.rs == RS::Free || b.rs == RS::Clawed || (hard && (b.rs == RS::Presented || b.rs == RS::Tube)))) { stuck = &b; break; }
            if (!stuck) break;
            V2 g = wpt(*stuck, {0, 0});
            S.hand = {true, stuck, {0, 0}, {g.x, g.y - 0.03}};
            run(S, 0.02);           // grabbing it takes it out of the action
            stuck->vz = 2.0;        // and it is tossed aside
            S.hand.on = false;
            run(S, 0.05);
        }
        S.hand = {true, B, {0, 0}, {B->x, B->y}};
    }
    // a round from the box, put into the open action in line with the bore: it drops into place
    void loadByHand(int chamber = 0) {
        Body* r = spawnRound(S, {S.gunBox[0] * 1000 + 30, S.groundY * 1000 - 20});
        if (!r) return;
        run(S, 0.1);
        Body* keep = S.hand.b;
        Sim::Hand held = S.hand;
        V2 grab = wpt(*r, {0, 0});
        S.hand = {true, r, lpt(*r, grab), grab};
        for (int k = 1; k <= 40; k++) {
            useChamber(S, chamber);
            BoreLine L = boreLine(S);
            useChamber(S, 0);
            V2 tgt{L.face.x - L.dir.x * 0.02 - std::cos(r->a) * r->headLx, L.face.y - L.dir.y * 0.02};
            S.hand.t = {grab.x + (tgt.x - grab.x) * k / 40, grab.y + (tgt.y - grab.y) * k / 40};
            run(S, 0.01);
        }
        S.hand.on = false;
        run(S, 0.2);
        if (keep == B) {
            if (hinged) { S.hand = held; return; }  // still holding it open
            S.hand = {true, B, {0, 0}, {B->x, B->y}};
        }
    }
    void loadAll() {  // a round into every empty chamber
        for (int k = 0; k < chamberCount(S); k++) {
            useChamber(S, k);
            bool empty = !S.chambered && !S.spent && !S.inTransit && S.hasFace;
            useChamber(S, 0);
            if (!empty) continue;
            if (hinged) { dropIntoChamber(S, k); run(S, 0.15); }  // one hand holds it open, the other pushes a round in
            else loadByHand(k);
        }
    }
};

double breechStroke(Sim& S, Body* B) {
    for (auto& J : S.joints) {
        if (J.type != JT::Track || !J.A || J.path.size() < 2) continue;
        Body* a = J.A->root ? J.A->root : J.A;
        if (a != B) continue;
        V2 p0 = wpt(*J.B, J.path.front()), p1 = wpt(*J.B, J.path.back());
        return std::hypot(p1.x - p0.x, p1.y - p0.y);
    }
    return S.design.breech.cart.oal / 1000 * 1.4;
}

}  // namespace

ReliabilityReport runReliability(const Design& design, const ReliabilityOptions& o, std::atomic<int>* done,
                                 const std::atomic<bool>* cancel) {
    ReliabilityReport R;
    std::mt19937 rng(o.seed);
    std::normal_distribution<double> power(1.0, o.powerSpread);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    Design d = design;
    d.friction *= 1 + o.frictionSpread * uni(rng);  // how dirty the action is, for this run
    auto Sp = build(d);
    Sim& S = *Sp;
    if (!S.BR || !S.hasFace) { R.note = "This design has no breech face, so it can't fire."; return R; }
    Body* B = S.BR->root ? S.BR->root : S.BR;
    for (auto& b : S.bodies)  // a part marked "worked by hand" (a pump's fore-end, a charging handle) is what the shooter pulls
        if (b.d && b.d->ctrl == "handle" && !b.fixed && !b.slice && b.tmpl.empty()) { B = b.root ? b.root : &b; break; }
    BoreLine L0 = boreLine(S);
    Runner run_{S, B, L0.ok ? L0.dir : V2{1, 0}, breechStroke(S, B), B->invIr > 0, cancel};
    run_.findHinge();
    R.manual = run_.rolling || !S.breechSprung;
    R.singleLoad = !S.magTmpl && S.feedMode == "single";
    run(S, 0.1);

    auto chamberNext = [&]() {  // get a round into the chamber the way this gun is loaded
        if (R.singleLoad) { run_.open(); run_.loadAll(); run_.close(); return; }
        if (S.magTmpl) {
            Body* M = seatedMag(S);
            if (!M || M->rounds == 0) { quickReload(S); R.reloads++; run(S, 0.1); }
        } else if (Body* M = seatedMag(S); M && M->rounds == 0) { fillMagazines(S); R.reloads++; }   // a built-in magazine: fill it in place
        run_.cycle();
    };
    auto clear = [&]() -> bool {  // open, pick out what's stuck, let it close on a fresh round
        for (int tries = 0; tries < 3; tries++) {
            run_.open();
            run_.pickOut(tries > 0);
            if (R.singleLoad) run_.loadAll();
            else if (S.magTmpl) { Body* M = seatedMag(S); if (!M || M->rounds == 0) { quickReload(S); R.reloads++; } }
            else if (Body* M = seatedMag(S); M && M->rounds == 0) { fillMagazines(S); R.reloads++; }
            run_.close();
            if (std::getenv("GUNSMITH_RELDEBUG")) {
                Body* M = seatedMag(S);
                std::fprintf(stderr, "clear try %d: malf=%s ch=%d spent=%d gap=%.1f mag=%d top=%d |", tries, malfName(S.malf), S.chambered, S.spent, breechGap(S), M ? M->rounds : -1, M && M->top);
                for (auto& b : S.bodies) if (b.isRound && !b.gone && b.rs != RS::Loose) { V2 h = wpt(b, {b.headLx, 0}); std::fprintf(stderr, " %s rs=%d (%.1f,%.1f,z%.1f) a=%.0f", b.isCase ? "C" : "L", (int)b.rs, h.x*1000, h.y*1000, b.z*1000, b.a*57.3); }
                std::fprintf(stderr, "\n");
            }
            if (S.malf == Malf::None && inBattery(S)) return true;
        }
        return false;
    };
    auto record = [&](Malf m) {
        R.jams++; R.byType[(int)m]++;
        R.events.push_back({S.shots, m, S.powerScale, S.limp});
    };

    int limit = o.rounds * 3 + 10;
    while (S.shots < o.rounds && R.attempts < limit && !run_.stop()) {
        if (!anyChambered(S)) chamberNext();
        if (S.malf != Malf::None) {  // jammed while loading
            record(S.malf);
            if (!clear()) { R.gaveUp = true; R.note = std::string("Stuck after a ") + malfName(S.malf) + " that racking didn't clear."; break; }
            continue;
        }
        S.powerScale = std::clamp(power(rng), 0.8, 1.2);
        S.limp = o.limpMax * uni(rng);
        int before = S.shots;
        bool wasChambered = (chamberCount(S) == 1 ? S.chambered : anyChambered(S)) && inBattery(S);
        // pull the drawn trigger if there is one, and press FIRE as the bench does (the built-in hammer listens for it;
        // with a drawn trigger it still waits until that part has actually moved)
        setKey(S, "fire", true);
        S.pressed = true; S.trigger = true;
        run(S, 0.06);
        setKey(S, "fire", false);
        S.pressed = false; S.trigger = false;
        run(S, R.manual ? 0.05 : 0.25);
        if (!R.manual && !inBattery(S) && S.malf == Malf::None) run(S, 0.2);  // give a slow action time to settle or jam
        R.attempts++;
        if (S.shots == before) {
            if (wasChambered) {  // the trigger fell on a chambered round and nothing happened
                record(S.deadPull ? Malf::FailureToCock : Malf::LightStrike);
                run_.cycle();     // work the action to get rid of the dud
            } else if (S.malf == Malf::None) {
                chamberNext();
            }
        } else if (R.manual && S.malf == Malf::None && !anyChambered(S)) {  // (another barrel still loaded: fire that first)
            run_.cycle();
        }
        if (S.malf != Malf::None) {
            record(S.malf);
            if (!clear()) { R.gaveUp = true; R.note = std::string("Stuck after a ") + malfName(S.malf) + " that racking didn't clear."; break; }
        }
        if (done) done->store(S.shots);
    }
    R.fired = S.shots;
    if (R.attempts >= limit && R.note.empty()) R.note = "Stopped: too many trigger pulls without a shot.";
    if (run_.stop() && R.note.empty()) R.note = "Stopped early.";
    return R;
}

}  // namespace sim
