// Gunsmith3D - cartridges as physical objects.
// Loose rounds on the bench, rounds guided in by hand, and (with physical feeding) the whole cycle: the magazine
// holds its top round up under the lips, the breech strips it forward, it rides the feed ramp, the chamber takes it
// only if it arrives straight, the extractor pulls the case back on the breech face, an ejector you draw kicks it
// round, and it has to get out through the port before the breech comes back. Anything that goes wrong is a jam.
#include "solver_internal.hpp"
#include <algorithm>
#include <cmath>

namespace sim {

static V2 vsub(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
static double vdot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static double vcross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
static V2 rot(V2 v, double a) { double c = std::cos(a), s = std::sin(a); return {c * v.x - s * v.y, s * v.x + c * v.y}; }

const char* malfName(Malf m) {
    switch (m) {
    case Malf::FailureToFeed: return "Failure to feed";
    case Malf::DoubleFeed: return "Double feed";
    case Malf::Stovepipe: return "Stovepipe";
    case Malf::FailureToEject: return "Failure to eject";
    case Malf::FailureToReturn: return "Failure to return to battery";
    case Malf::LightStrike: return "Light strike";
    case Malf::FailureToCock: return "Failure to cock";
    default: return "";
    }
}
const char* malfAdvice(Malf m) {
    switch (m) {
    case Malf::FailureToFeed: return "The round didn't make it into the chamber. If the breech rode over it, the next round wasn't up in time: use a stronger magazine spring, or raise the lips. Also check the feed ramp angle, and that the breech moves fast enough to push the round home.";
    case Malf::DoubleFeed: return "Two rounds in the action at once. The magazine let the next round up before the first was out of the way.";
    case Malf::Stovepipe: return "The empty case didn't get clear before the breech came back. Move the ejector, enlarge the ejection port, or let the breech travel further or faster.";
    case Malf::FailureToEject: return "The empty case stayed on the breech face. Nothing kicked it off: add or move an ejector so the case hits it as the breech opens.";
    case Malf::FailureToReturn: return "The breech stopped short of closing. The spring is too weak for the friction, or something is in the way.";
    case Malf::LightStrike: return "The striker or hammer reached the primer too slowly to fire it. Use a stronger spring or a longer run.";
    case Malf::FailureToCock: return "The trigger was pulled but the hammer or striker was already down. The breech didn't open far enough to cock it: lower Firing > Cocks after, or let the breech travel further. (An open bolt: it didn't come back far enough for the sear to catch it. Lower Sear catches at, or let it travel further.)";
    default: return "";
    }
}
int jamCount(const Sim& S) {
    int n = 0;
    for (int i = 1; i < (int)Malf::Count; i++) n += S.malfCount[i];
    return n;
}

// ---------- the bore ----------
BoreLine boreLine(const Sim& S) {
    BoreLine L;
    if (!S.BR || !S.hasFace || !S.CH) return L;
    L.face = wpt(*S.CH, S.faceC);
    V2 m = S.hasMuzzle ? wpt(*S.CH, S.muzC) : V2{L.face.x + std::cos(S.CH->a), L.face.y + std::sin(S.CH->a)};
    double dx = m.x - L.face.x, dy = m.y - L.face.y, l = std::hypot(dx, dy);
    if (l < 1e-9) return L;
    L.dir = {dx / l, dy / l};
    V2 f = wpt(*S.BR, S.faceB);
    double rx = L.face.x - f.x, ry = L.face.y - f.y;
    L.faceS = rx * L.dir.x + ry * L.dir.y;               // how far the breech face is behind the chamber mouth
    L.faceLat = std::fabs(rx * L.dir.y - ry * L.dir.x);  // and how far off the bore line it is
    L.ok = true;
    return L;
}
static double chamberRadius(const Sim& S, double a) {  // a: metres forward of the chamber face
    if (S.chamberR.empty()) return 0.005;
    int i = (int)(a / 0.0005);
    if (i < 0) i = 0;
    if (i >= (int)S.chamberR.size()) return S.chamberR.back();
    return S.chamberR[i];
}

void cartridgeSetup(Sim& S) {
    const Cartridge& c = S.design.breech.cart;
    std::vector<V2> top;
    for (auto& p : cartridgeOutline(c)) if (p.y <= 0) top.push_back({p.x, -p.y});
    std::sort(top.begin(), top.end(), [](V2 a, V2 b) { return a.x < b.x; });
    S.chamberR.clear();
    for (double a = 0; a <= c.oal + 3; a += 0.5) {  // the chamber is cut to the cartridge, then the bore carries on
        double r = c.bulletDia / 2;
        if (a < c.caseLen) {
            r = 0;
            for (size_t i = 0; i + 1 < top.size(); i++)
                if (a >= top[i].x && a <= top[i + 1].x) {
                    double t = top[i + 1].x > top[i].x ? (a - top[i].x) / (top[i + 1].x - top[i].x) : 0;
                    r = std::max(r, top[i].y + (top[i + 1].y - top[i].y) * t);
                }
            r = std::max(r, c.bulletDia / 2);
            if (c.kind == "shotshell") r = std::max(r, c.baseDia / 2);  // cut for the shell as it opens when fired
        }
        S.chamberR.push_back((r + 0.15) / 1000);  // a little clearance
    }
    S.gunZmin = 1e9; S.gunZmax = -1e9;
    for (auto& b : S.bodies) {
        if (b.isRound || b.d->layer == "none") continue;
        S.gunZmin = std::min(S.gunZmin, b.z - b.zh); S.gunZmax = std::max(S.gunZmax, b.z + b.zh);
    }
    if (S.gunZmin > S.gunZmax) S.gunZmin = S.gunZmax = 0;
    S.gunBox[0] = S.gunBox[1] = 1e9; S.gunBox[2] = S.gunBox[3] = -1e9;  // the whole gun as drawn, solid or not
    for (auto& b : S.bodies) {
        if (b.isRound || b.isMag) continue;
        for (auto& l : b.lp) {
            V2 w = wpt(b, l);
            S.gunBox[0] = std::min(S.gunBox[0], w.x); S.gunBox[1] = std::min(S.gunBox[1], w.y);
            S.gunBox[2] = std::max(S.gunBox[2], w.x); S.gunBox[3] = std::max(S.gunBox[3], w.y);
        }
    }
    S.physicalFeed = S.design.breech.on && S.design.breech.physicalFeed && S.hasFace && S.feedMode != "internal";
}

static void setState(Body& b, RS s) {
    b.rs = s;
    if (s == RS::Loose || s == RS::Staged) { b.zFeed = 0; b.zVis = 0; }  // out of the feed path: no longer coming in from the side
    b.loose = s == RS::Loose;
    b.staged = s == RS::Staged;
}
static void poseStaged(Body& b, const BoreLine& L);
static Body* newCartridge(Sim& S, V2 head, bool isCase) {
    Body* slot; BodyDef* dp;
    if (!spawnSlot(S, &slot, &dp)) return nullptr;
    const Cartridge& c = S.design.breech.cart;
    BodyDef& d = *dp;
    d = BodyDef();
    d.id = (isCase ? "case#" : "round#") + std::to_string(++S.spawned);
    d.name = c.name + (isCase ? " case" : " round");
    d.mass = isCase ? spentCaseMass(c) : cartridgeMass(c);
    d.thick = c.headDia();
    d.color = "#d9a441";
    for (auto& p : cartridgeOutline(c, isCase)) d.pts.push_back({head.x + p.x, head.y + p.y});
    fixShape(d);
    Body b;
    if (!makeBody(d, b)) { if (!slot) S.extraDefs.pop_back(); return nullptr; }
    b.tmpl = "round";
    b.isRound = true;
    b.slot = S.cur;
    b.isCase = isCase;
    b.z = 0; b.zh = c.headDia() / 2000;
    b.headLx = head.x / 1000 - b.x;
    BoreLine L = boreLine(S);
    if (L.ok) {  // lie it level with the bore, keeping the head where asked
        b.a = std::atan2(L.dir.y, L.dir.x);
        b.x = head.x / 1000 - std::cos(b.a) * b.headLx;
        b.y = head.y / 1000 - std::sin(b.a) * b.headLx;
    }
    b.px = b.x; b.py = b.y; b.pa = b.a;
    return placeSpawned(S, slot, b);
}
static void placeHead(Body& b, V2 head, double a) {
    b.a = a; b.x = head.x - std::cos(a) * b.headLx; b.y = head.y - std::sin(a) * b.headLx;
    b.px = b.x; b.py = b.y; b.pa = b.a;
}
Body* spawnRound(Sim& S, V2 head, double vx, double vy) {
    Body* b = newCartridge(S, head, false);
    if (!b) return nullptr;
    setState(*b, RS::Loose);
    b->vx = vx; b->vy = vy;
    return b;
}
Body* dropIntoChamber(Sim& S, int k) {  // a round pushed all the way into chamber k of an open action
    useChamber(S, k);
    BoreLine L = boreLine(S);
    Body* r = nullptr;
    if (L.ok && !S.chambered && !S.spent && !S.inTransit && (r = newCartridge(S, {L.face.x * 1000, L.face.y * 1000}, false))) {
        setState(*r, RS::Staged);
        r->stageS = 0; r->slot = k;
        poseStaged(*r, L);
    }
    useChamber(S, 0);
    return r;
}
int looseRounds(const Sim& S) {
    int n = 0;
    for (auto& b : S.bodies) if (b.isRound && !b.isCase && !b.gone && (b.rs == RS::Loose || b.rs == RS::Staged)) n++;
    return n;
}
static void retire(Sim& S, Body& b) { retireBody(S, b); }

static bool magLips(const Sim& S, const Body& M, V2& headL, V2& fwdL, V2* liftL = nullptr, int* side = nullptr);
static bool doubleStack(const Body& M) { return M.d && M.d->gen.on && M.d->gen.stack == 2; }
static bool tubeMag(const Body& M) { return M.d && M.d->roundStyle == "line" && M.d->roundPath.size() >= 2; }   // rounds nose to tail
static bool physicalTube(const Body& M) { return tubeMag(M) && M.d->tubeFeed == "physical"; }
// A tube's mouth (its round path's first point) and the way out of it, in the magazine's frame.
static void tubeMouth(const Body& M, V2& mouthL, V2& outL) {
    V2 a = M.d->roundPath[0], b = M.d->roundPath[1];
    mouthL = {a.x / 1000 - M.x0, a.y / 1000 - M.y0};
    double dx = a.x - b.x, dy = a.y - b.y, l = std::hypot(dx, dy);
    outL = l > 1e-9 ? V2{dx / l, dy / l} : V2{-1, 0};
}
// The spring's effective strength: a double stack tapering to one central round (single position) loses some to friction.
static double springOf(const Body& M) { return M.d ? M.d->magSpring * (doubleStack(M) && !M.d->doubleFeed ? 0.8 : 1.0) : 1.0; }

bool magLoadPoint(const Sim& S, const Body& M, V2& w) {  // the middle of a round sitting at the feed lips
    V2 hl, fl;
    if (!(M.isMag || M.builtInMag) || !M.d || !magLips(S, M, hl, fl)) return false;
    const Cartridge& c = S.design.breech.cart;
    V2 f = {fl.x * std::cos(M.a) - fl.y * std::sin(M.a), fl.x * std::sin(M.a) + fl.y * std::cos(M.a)};
    V2 head = wpt(M, hl);
    w = {head.x + f.x * c.oal / 2000, head.y + f.y * c.oal / 2000};
    return true;
}

// ---------- rounds guided in by hand ----------
// A round held over the feed lips of a magazine that isn't in the gun is pushed in, if there's room.
static bool loadIntoMagazine(Sim& S, Body& r) {
    const Cartridge& c = S.design.breech.cart;
    Joint* J = magwell(S);
    for (auto& M : S.bodies) {
        if (!(M.isMag || M.builtInMag) || M.gone || !M.d || M.rounds >= M.d->magCap) continue;
        if (M.isMag && J && J->A == &M) continue;   // a removable magazine is loaded out of the gun; a built-in one in place
        V2 mid;
        if (!magLoadPoint(S, M, mid)) continue;
        if (std::hypot(r.x - mid.x, r.y - mid.y) > std::max(0.010, c.oal / 2000)) continue;
        M.rounds++;
        M.idle = 0;
        retire(S, r);
        S.events.push_back({Ev::MagLoaded, S.t});
        return true;
    }
    return false;
}
static void poseStaged(Body& b, const BoreLine& L) {
    double a = std::atan2(L.dir.y, L.dir.x);
    V2 head{L.face.x - L.dir.x * b.stageS, L.face.y - L.dir.y * b.stageS};
    b.px = b.x; b.py = b.y; b.pa = b.a;
    b.a = a; b.x = head.x - std::cos(a) * b.headLx; b.y = head.y - std::sin(a) * b.headLx;
}
// Rounds in the hand line up with the bore and drop into the open action when held in the right place;
// staged rounds ride the bore line and are pushed home by the breech face (or by hand).
static void handRounds(Sim& S, const BoreLine& L) {
    const Cartridge& c = S.design.breech.cart;
    double headR = c.headDia() / 2000 + 0.001;
    bool faceOnLine = L.ok && L.faceLat < headR;
    double room = faceOnLine ? L.faceS : c.oal / 1000 * 1.5;  // how far back a round can sit before it meets the face
    bool canTake = L.ok && !S.chambered && !S.spent && !S.inTransit;
    for (auto& b : S.bodies) {
        if (!b.isRound || b.gone || b.isCase) continue;
        bool held = S.hand.on && S.hand.b == &b;
        if (held && b.rs == RS::Loose && S.cur == 0 && loadIntoMagazine(S, b)) continue;
        if (b.rs == RS::Staged && b.slot != S.cur) continue;  // lined up with another chamber
        if (b.rs == RS::Staged) {
            if (!canTake) { setState(b, RS::Loose); continue; }
            double s = b.stageS;
            if (held) {  // slide it along the bore with the hand, or pull it back out sideways
                V2 t = S.hand.t;
                double rx = t.x - L.face.x, ry = t.y - L.face.y;
                double along = rx * L.dir.x + ry * L.dir.y, lat = std::fabs(rx * L.dir.y - ry * L.dir.x);
                if (lat > 0.012) { setState(b, RS::Loose); continue; }
                s = (S.hand.l.x - b.headLx) - along;
            }
            if (faceOnLine) s = std::min(s, L.faceS);  // the breech face pushes it forward
            s = std::max(0.0, std::min(s, c.oal / 1000 * 1.5));
            b.stageS = s;
            if (s < 0.0006) {  // seated
                retire(S, b);
                S.chambered = true; S.gripped = false;
                S.events.push_back({Ev::Chambered, S.t});
                continue;
            }
            poseStaged(b, L);
            continue;
        }
        if (held && L.ok && b.rs == RS::Loose) {
            b.a = std::atan2(L.dir.y, L.dir.x); b.w = 0;  // a hand holds a round level with the bore
            if (!canTake) continue;
            V2 head = wpt(b, {b.headLx, 0});
            double rx = L.face.x - head.x, ry = L.face.y - head.y;
            double s = rx * L.dir.x + ry * L.dir.y, lat = std::fabs(rx * L.dir.y - ry * L.dir.x);
            double latTol = std::max(0.006, c.headDia() / 1000);  // within about one head width of the bore line
            if (lat < latTol && s > -c.oal / 1000 * 0.6 && s < room + 0.004 && room > 0.004) {
                setState(b, RS::Staged); b.stageS = std::max(0.0, std::min(s, room)); b.slot = S.cur;
                b.vx = b.vy = b.w = 0;
                poseStaged(b, L);
            }
        }
    }
}

void legacyRoundsStep(Sim& S) {
    BoreLine L = boreLine(S);
    handRounds(S, L);
}

// ---------- physical feeding ----------
// Where the top round's head sits and which way it feeds (toward the muzzle), in the magazine's frame; `liftL` is
// the way the follower pushes it (out of the magazine, toward the lips), and `side` a magazine across the gun.
static bool magLips(const Sim& S, const Body& M, V2& headL, V2& fwdL, V2* liftL, int* side) {
    const BodyDef& d = *M.d;
    const Cartridge& c = S.design.breech.cart;
    V2 cen = centroidOf(d);
    V2 lift{0, -1};
    if (side) *side = 0;
    const BreechDef& brd = S.design.breech;
    V2 gunFwd{brd.hasMuzzle && brd.hasFace ? brd.muzzle.x - brd.face.x : 1, brd.hasMuzzle && brd.hasFace ? brd.muzzle.y - brd.face.y : 0};
    auto done = [&]() { if (liftL) *liftL = lift; return true; };
    if (d.roundPath.size() >= 2) {  // the round path you drew: the top round sits at its first point
        V2 p0 = d.roundPath[0], p1 = d.roundPath[1];
        double dx = p1.x - p0.x, dy = p1.y - p0.y, l = std::hypot(dx, dy);
        V2 u = l > 0 ? V2{dx / l, dy / l} : V2{0, 1};
        V2 up{-u.x * d.lipHeight, -u.y * d.lipHeight};   // raising the lips moves it back towards the feed end
        if (d.roundStyle == "line") {   // nose to tail: its head at the mouth, its nose pointing into the tube
            headL = {(p0.x + up.x - cen.x) / 1000, (p0.y + up.y - cen.y) / 1000};
            fwdL = u;
            lift = {u.y, -u.x};
        } else {                        // side by side: across the path, pointing at the muzzle; the follower pushes back up the path
            V2 f{-u.y, u.x};
            if (f.x * gunFwd.x + f.y * gunFwd.y < 0) f = {-f.x, -f.y};
            headL = {(p0.x + up.x - f.x * c.oal / 2 - cen.x) / 1000, (p0.y + up.y - f.y * c.oal / 2 - cen.y) / 1000};
            fwdL = f;
            lift = {-u.x, -u.y};
        }
        return done();
    }
    if (d.gen.on && d.gen.side != 0) {   // across the gun: the top round lies in the bolt's path at the mouth, along the bore
        double dia = c.headDia() * 1.02, gl = std::hypot(gunFwd.x, gunFwd.y);
        V2 f = gl > 0 ? V2{gunFwd.x / gl, gunFwd.y / gl} : V2{1, 0};
        V2 m{d.gen.top.x, d.gen.top.y + 3 + dia / 2 - d.lipHeight};
        headL = {(m.x - f.x * c.oal / 2 - cen.x) / 1000, (m.y - f.y * c.oal / 2 - cen.y) / 1000};
        fwdL = f;
        lift = {f.y, -f.x};
        if (side) *side = d.gen.side > 0 ? 1 : -1;
        return done();
    }
    if (d.gen.on) {
        double dia = c.headDia() * 1.02, phi;
        V2 m = magCentre(d.gen, 3 + dia / 2, &phi);
        V2 f{std::cos(phi * D2R), -std::sin(phi * D2R)};
        lift = {f.y, -f.x};   // back up the magazine toward its mouth, whichever way it's turned
        headL = {(m.x - f.x * c.oal / 2 - cen.x) / 1000, (m.y - f.y * c.oal / 2 - cen.y) / 1000};
        headL = {headL.x + lift.x * d.lipHeight / 1000, headL.y + lift.y * d.lipHeight / 1000};
        if (f.x * gunFwd.x + f.y * gunFwd.y < 0) {   // turned over (a magazine on top): the round still points at the muzzle
            headL = {headL.x + f.x * c.oal / 1000, headL.y + f.y * c.oal / 1000};
            f = {-f.x, -f.y};
        }
        fwdL = f;
        return done();
    }
    double x0 = 1e9, x1 = -1e9, y0 = 1e9;
    for (auto& p : d.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); }
    headL = {((x0 + x1) / 2 - c.oal / 2 - cen.x) / 1000, (y0 + 1.5 + c.headDia() / 2 - d.lipHeight - cen.y) / 1000};
    fwdL = {1, 0};
    return done();
}

// Which edge of the rim the extractor grips: set in the design, or away from the magazine (the top, unless the
// magazine is above the bore)
static double clawSide(const Sim& S, V2 up) {
    const BreechDef& br = S.design.breech;
    if (br.extractor == "top") return 1;
    if (br.extractor == "bottom") return -1;
    const BodyDef* m = S.magTmpl;
    if (!m) for (auto& b : S.design.bodies) if (b.magazine) { m = &b; break; }
    if (!m || (m->gen.on && m->gen.side) || !br.hasFace) return 1;
    V2 c = centroidOf(*m);
    return (c.x - br.face.x) * up.x + (c.y - br.face.y) * up.y > 0 ? -1 : 1;   // (up is in the gun's frame: design y is down)
}

void cartridgeForces(Sim& S) {
    if (!S.physicalFeed) return;
    for (auto& b : S.bodies) {  // the follower spring pushes the top round up against the lips
        if (b.rs != RS::Presented || !b.mag || b.gone || b.sideFeed) continue;   // (a side magazine pushes across the gun)
        V2 n = rot(b.liftL, b.mag->a);
        double F = (10 + 0.5 * b.mag->rounds) * springOf(*b.mag);
        b.fx += n.x * F; b.fy += n.y * F;
        b.mag->fx -= n.x * F; b.mag->fy -= n.y * F;
    }
}

static void chamberContacts(Sim& S, Body& r, const BoreLine& L) {
    Body& C = *S.CH;
    const Cartridge& c = S.design.breech.cart;
    V2 u = L.dir, p{-u.y, u.x};
    double len = (r.isCase ? c.caseLen : c.oal) / 1000;
    V2 tip = wpt(r, {r.headLx + len, 0}), head = wpt(r, {r.headLx, 0});
    double ta = vdot(vsub(tip, L.face), u), tl = vdot(vsub(tip, L.face), p);
    double ra = std::fabs(std::remainder(r.a - std::atan2(u.y, u.x), 2 * 3.14159265358979323846));
    if (!r.inChamber) {  // the nose has to arrive at the mouth, roughly straight, to go in
        // a round fed from one side of a double-position magazine also comes in at an angle sideways: steep for a short
        // pistol round, gentle for a long rifle round
        double side = r.zFeed != 0 ? std::atan2(std::fabs(r.zFeed), (r.isCase ? c.caseLen : c.oal) / 1000 * 0.75) : 0;
        if (ta > -0.001 && ta < 0.004 && std::fabs(tl) < chamberRadius(S, std::max(0.0, ta)) + 0.0008 && std::hypot(ra, side) < 12 * D2R) {
            r.inChamber = true;  // at the mouth
            r.zFeed = 0; r.zVis = 0;
        }
    } else if (vdot(vsub(tip, L.face), u) < -0.0005) r.inChamber = false;  // pulled back out
    (void)head;
    for (auto& s : r.samples) {
        V2 w = wpt(r, s), rel = vsub(w, L.face);
        double a = vdot(rel, u), l = vdot(rel, p);
        if (r.inChamber && a >= 0) {  // inside the chamber: its walls hold it on the bore line
            double R = chamberRadius(S, a);
            if (std::fabs(l) > R) {
                double d = std::fabs(l) - R, sg = l > 0 ? 1 : -1;
                V2 q{w.x - p.x * sg * d, w.y - p.y * sg * d};
                solve(r, C, w, q, -p.x * sg, -p.y * sg, d);
            }
            continue;
        }
        // anywhere else the barrel is solid: the round is stopped by its rear face, or slides up the feed ramp
        V2 lc = lpt(C, w);
        if (w.x < C.bb[0] || w.x > C.bb[2] || w.y < C.bb[1] || w.y > C.bb[3] || !inPoly(lc.x, lc.y, C.lp)) continue;
        double best = 1e18; V2 bq;
        for (size_t i = 0; i < C.lp.size(); i++) {
            double d2; V2 q = closestOnSeg(lc, C.lp[i], C.lp[(i + 1) % C.lp.size()], &d2);
            if (d2 < best) { best = d2; bq = q; }
        }
        double d = std::sqrt(best);
        if (d < 1e-7 || d > 0.005) continue;
        V2 qw = wpt(C, bq);
        solve(r, C, w, qw, (qw.x - w.x) / d, (qw.y - w.y) / d, d);
    }
    if (!r.inChamber) contacts(r, C, 0);  // the barrel's corners pushing into the round
}

static bool inAction(const Body& b) { return b.isRound && !b.gone && (b.rs == RS::Free || b.rs == RS::Clawed || b.rs == RS::Presented || b.rs == RS::Tube); }
static bool solidPart(const Sim& S, const Body& b) {
    if (b.isRound) return inAction(b);  // rounds and cases in the action get in each other's way
    const Body* ch = S.CH ? (S.CH->root ? S.CH->root : S.CH) : nullptr;  // the chamber and anything welded to it: the
    return !b.gone && !b.isMag && &b != S.CH && b.root != ch && &b != ch && b.d && b.d->layer != "none";  // walls handle those
}
static void partContacts(Sim& S, Body& r, const Body* ignore = nullptr) {  // a cartridge in the action against every solid part it meets in depth
    bbox(r);
    for (auto& P : S.bodies) {
        if (&P == &r || &P == ignore || !solidPart(S, P)) continue;
        // the top round under the lips is solid to a loose empty case (the spring holds it up); a round being fed
        // over it gives way to it instead
        if (P.rs == RS::Presented && !((r.isCase || r.thrown) && r.rs == RS::Free)) continue;
        if (P.isRound) bbox(P);
        if (std::fabs(P.z - r.z) >= P.zh + r.zh) continue;
        if (P.bb[0] > r.bb[2] || r.bb[0] > P.bb[2] || P.bb[1] > r.bb[3] || r.bb[1] > P.bb[3]) continue;
        contacts(P, r, S.design.friction);
        contacts(r, P, S.design.friction);
        bbox(r);
    }
}

// Under the lips a round can only go two ways: down into the magazine (the slide riding over it) or forward
// (the breech face catching its rim). Contacts are steered that way so a corner can't flick it off the lips.
// Returns true when a feed ramp lifts the nose of a round that is being stripped: the lips let it go and it tips up.
static bool presentedContacts(Sim& S, Body& r, V2 f, V2 n, double stripped) {
    bool lifted = false;
    bbox(r);
    // a side magazine's round: whatever rides over it pushes it back into the magazine, across the gun, and it pops
    // out again behind it. Only a round that is out (or nearly) can be caught by the breech face.
    const bool side = r.sideFeed != 0;
    const bool wasIn = side && std::fabs(r.zVis) > S.design.breech.cart.headDia() / 1000 * 0.35;
    if (side) r.pushedIn = false;
    const Cartridge& c = S.design.breech.cart;
    for (auto& P : S.bodies) {
        if (&P == &r || !solidPart(S, P) || std::fabs(P.z - r.z) >= P.zh + r.zh) continue;
        if ((P.isCase || P.thrown) && P.rs == RS::Free) continue;  // a loose case (or a racked-out round) lands on it rather than pushing it down
        if (P.isRound) bbox(P);
        if (P.bb[0] > r.bb[2] || r.bb[0] > P.bb[2] || P.bb[1] > r.bb[3] || r.bb[1] > P.bb[3]) continue;
        for (auto& s : r.samples) {
            V2 w = wpt(r, s), lc = lpt(P, w);
            if (!inPoly(lc.x, lc.y, P.lp)) continue;
            double best = 1e18; V2 bq;
            for (size_t i = 0; i < P.lp.size(); i++) {
                double d2; V2 q = closestOnSeg(lc, P.lp[i], P.lp[(i + 1) % P.lp.size()], &d2);
                if (d2 < best) { best = d2; bq = q; }
            }
            double d = std::sqrt(best);
            if (d < 1e-7 || d > 0.006) continue;
            V2 qw = wpt(P, bq), nc{(qw.x - w.x) / d, (qw.y - w.y) / d};
            double t = (s.x - r.headLx) / (c.oal / 1000);       // 0 at the head, 1 at the tip
            Body* br = S.BR->root ? S.BR->root : S.BR;
            bool breechPart = (P.root ? P.root : &P) == br;
            bool closing = (br->vx - r.vx) * f.x + (br->vy - r.vy) * f.y > 0.05;  // the breech coming forward onto the rim
            if (vdot(nc, f) > 0.5 && t < 0.25 && breechPart && closing && !wasIn) {  // the breech face catching the rim: stripping
                solve(r, P, w, qw, nc.x, nc.y, d);
            } else if (side) {
                r.pushedIn = true;
            } else if (t > 0.55 && vdot(nc, n) > 0.3 && !breechPart && stripped > 0.002) {  // nose up a feed ramp: it tips up
                solve(r, P, w, qw, nc.x, nc.y, d);                                                // while the lips hold its head
                lifted = true;
                double rel = std::remainder(r.a - std::atan2(f.y, f.x), 2 * 3.14159265358979323846);
                // it can only tip toward the bore, away from the magazine: nose up from below, nose down from a top magazine
                if (vcross(f, n) <= 0) r.tilt = std::clamp(std::min(r.tilt, rel), -25 * D2R, 0.0);
                else r.tilt = std::clamp(std::max(r.tilt, rel), 0.0, 25 * D2R);
            } else {  // anything else rides over it and presses it down; the follower is soft, so only the round moves
                double dn = std::max(0.0, -vdot(vsub(qw, w), n));
                if (dn > 1e-7) { r.x -= n.x * dn; r.y -= n.y * dn; }
            }
        }
    }
    return lifted;
}

static void groundContact(Sim& S, Body& b) {
    for (auto& l : b.lp) {
        V2 w = wpt(b, l);
        if (w.y > S.groundY) { solve(b, S.world, w, {w.x, S.groundY}, 0, -1, w.y - S.groundY); b.grounded = true; }
    }
}

static void chamberStep(Sim& S, bool main);
void cartridgeStep(Sim& S) {
    chamberStep(S, true);
    for (int k = 1; k < chamberCount(S); k++) { useChamber(S, k); chamberStep(S, false); }  // the further chambers
    useChamber(S, 0);
}
// One chamber's rounds: loading by hand, extraction, ejection and seating. The main pass (chamber 0) also runs the
// magazine, the loose rounds and the jam checks.
static void chamberStep(Sim& S, bool main) {
    BoreLine L = boreLine(S);
    const Cartridge& c = S.design.breech.cart;
    const int me = S.cur;
    // a hand that grabs a cartridge in the action takes it out of the cycle: that's how you clear a jam
    if (main && S.hand.on && S.hand.b && S.hand.b->isRound) {
        Body& h = *S.hand.b;
        if (h.rs == RS::Presented || h.rs == RS::Free || h.rs == RS::Clawed || h.rs == RS::Tube) {
            if ((h.rs == RS::Presented || h.rs == RS::Tube) && h.mag) { h.mag->rounds--; h.mag->top = nullptr; h.mag = nullptr; }
            setState(h, RS::Loose); h.vz = 0; h.z = 0;
        }
    }
    for (auto& b : S.bodies) {  // loose rounds and cases keep their sideways motion until the bench stops them
        if (!main) break;
        if (!b.isRound || b.gone || b.rs != RS::Loose) continue;
        if (S.hand.on && S.hand.b == &b) {  // a hand brings it back to the bore plane, where it can be loaded
            b.vz = 0;
            double step = 0.5 * H;
            b.z = std::fabs(b.z) < step ? 0 : b.z - std::copysign(step, b.z);
            continue;
        }
        if (b.vz == 0) continue;
        b.z = std::clamp(b.z + b.vz * H, -2.5, 2.5);
        if (b.grounded) b.vz *= std::max(0.0, 1 - 12 * H);
        if (std::fabs(b.vz) < 0.01 || std::fabs(b.z) >= 2.5) b.vz = 0;
    }
    handRounds(S, L);
    if (!S.physicalFeed || !L.ok) return;
    V2 u = L.dir, up{u.y, -u.x};
    double ang = std::atan2(u.y, u.x), rimR = c.headDia() / 2000;
    double gap = breechGap(S) / 1000;

    // extraction: a breech that closed on a round or case pulls it back out with it
    if (S.gripped && (S.chambered || S.spent) && L.faceS > 0.0008) {  // the breech face has drawn back along the bore
        bool cs = S.spent;
        S.chambered = S.spent = false; S.gripped = false;
        if (Body* r = newCartridge(S, {L.face.x * 1000, L.face.y * 1000}, cs)) {
            setState(*r, RS::Clawed);
            placeHead(*r, L.face, ang);
            r->inChamber = true;
            V2 f = wpt(*S.BR, S.faceB);
            double cs = clawSide(S, up);   // +1 the top edge of the rim, -1 the bottom
            V2 claw{f.x + up.x * rimR * cs + u.x * 0.0003, f.y + up.y * rimR * cs + u.y * 0.0003};
            r->clawB = lpt(*S.BR, claw);
            r->clawR = {r->headLx + 0.0003, -rimR * cs};
            r->vx = S.BR->vx; r->vy = S.BR->vy;
        }
    }
    // the magazine holds its top round up under the lips, once the way is clear
    Body* M = main ? seatedMag(S) : nullptr;  // (the magazine feeds the first chamber)
    for (auto& b : S.bodies)  // a magazine leaving the gun takes its top round with it
        if (main)
        if ((b.rs == RS::Presented || b.rs == RS::Tube) && b.mag && (b.mag != M)) { b.mag->top = nullptr; b.mag = nullptr; retire(S, b); }
    // a belt: the pawl moves it on one round each time it has been far enough from where it rests and comes back.
    // Until then the tray stays empty, whatever the breech does.
    bool belt = M && M->d && !M->d->feedPawl.empty();
    if (belt) {
        if (!M->pawl) { M->pawl = S.find(M->d->feedPawl); if (M->pawl == &S.world) M->pawl = nullptr; }
        if (M->pawl) {
            const Body& P = *M->pawl;
            double d = std::hypot(P.x - P.x0, P.y - P.y0) * 1000;
            // it rides back over the next link and catches it at the end of its stroke, then pushes the belt on one round
            // on its way home. A round still sitting in the tray stops the belt: that push is lost.
            if (!M->pawlArmed && d >= M->d->pawlStroke) M->pawlArmed = true;
            if (M->pawlArmed && d < M->d->pawlStroke * 0.3) { M->pawlArmed = false; if (!M->top) M->indexReady = true; }
        }
    }
    if (M && M->rounds > 0 && !M->top && !tubeMag(*M) && (!belt || M->indexReady)) {
        V2 hl, fl, ll; int side = 0;
        magLips(S, *M, hl, fl, &ll, &side);
        V2 LH = wpt(*M, hl), f = rot(fl, M->a);
        bool clear = true;
        for (auto& b : S.bodies) {
            if (!b.isRound || b.gone || b.rs == RS::Loose || b.rs == RS::None) continue;
            V2 h = wpt(b, {b.headLx, 0}), d = vsub(h, LH);
            if (std::fabs(vdot(d, f)) < c.oal / 1000 * 0.95 && std::fabs(vcross(f, d)) < c.headDia() / 1000 * 1.2) clear = false;
        }
        if (clear)
            if (Body* r = newCartridge(S, {LH.x * 1000, LH.y * 1000}, false)) {
                setState(*r, RS::Presented);
                // it starts one round down in the stack and the follower spring has to lift it to the lips
                // (a double stack rises about half a round at a time); a weak spring may not get it there in time
                double step = belt || side ? 0 : c.headDia() / 1000 * (doubleStack(*M) ? 0.55 : 1.0);  // (a belt lays it in the tray;
                if (belt) M->indexReady = false;                                                         //  a side magazine pushes it out sideways)
                V2 up = rot(ll, M->a);
                placeHead(*r, {LH.x - up.x * step, LH.y - up.y * step}, std::atan2(f.y, f.x));
                if (doubleStack(*M) && M->d->doubleFeed && !side) {  // double position: the top round sits to one side, alternating
                    r->zFeed = c.headDia() / 1000 * 0.43 * ((M->rounds % 2) ? 1 : -1);
                    r->zVis = r->zFeed;   // drawn off to the side; it still collides as if centred (see zVis)
                }
                r->mag = M; r->lipsL = hl; r->feedL = fl; r->liftL = ll; r->sideFeed = side; r->pushedIn = false;
                if (side) r->zVis = side * c.headDia() / 1000;   // it comes out of the mouth beside the gun
                M->top = r;
            }
    }
    // a tube: its spring pushes the next round out of the mouth, head first, as a real round. Whatever is in the way
    // (a lifter that's up, a cartridge stop) holds it back; once it's all the way out it's loose in the action.
    if (M && M->rounds > 0 && !M->top && physicalTube(*M)) {
        V2 mouthL, outL;
        tubeMouth(*M, mouthL, outL);
        V2 mouth = wpt(*M, mouthL), out = rot(outL, M->a);
        if (Body* r = newCartridge(S, {mouth.x * 1000, mouth.y * 1000}, false)) {
            setState(*r, RS::Tube);
            placeHead(*r, mouth, std::atan2(-out.y, -out.x));  // nose up the tube
            r->z = M->z;
            r->mag = M; r->lipsL = mouthL; r->feedL = outL;
            M->top = r;
        }
    }
    int spentOnBench = 0;
    for (int pass = 0; pass < 2; pass++)
        for (auto& b : S.bodies) {
            if (!b.isRound || b.gone || b.slot != me) continue;
            if (b.rs == RS::Tube && b.mag) {  // on its way out of a tube: it runs along the tube's line, pushed by the spring
                Body& Mg = *b.mag;
                V2 mouth = wpt(Mg, b.lipsL), out = rot(b.feedL, Mg.a), n{out.y, -out.x};
                b.a = std::atan2(-out.y, -out.x); b.pa = b.a; b.w = 0;
                V2 h = wpt(b, {b.headLx, 0}), d = vsub(h, mouth);
                double s = vdot(d, out);
                if (s < 0) solve(b, Mg, h, {h.x - out.x * s, h.y - out.y * s}, out.x, out.y, -s);  // the column behind it
                h = wpt(b, {b.headLx, 0}); d = vsub(h, mouth);
                double e = vdot(d, n);
                if (std::fabs(e) > 1e-7) solve(b, Mg, h, {h.x - n.x * e, h.y - n.y * e}, e > 0 ? -n.x : n.x, e > 0 ? -n.y : n.y, std::fabs(e));
                if (pass == 0) {  // the spring: about 6 N on a standard spring, up to a couple of metres a second
                    double sp = springOf(Mg), vmax = 2.5 * std::sqrt(sp), m = std::max(b.m, 1e-4);
                    double v = ((b.x - b.px) * out.x + (b.y - b.py) * out.y) / H - (Mg.vx * out.x + Mg.vy * out.y);
                    if (v < vmax) {
                        double dv = std::min(6.0 * sp / m * H, vmax - v);
                        b.px -= out.x * dv * H; b.py -= out.y * dv * H;
                    }
                }
                partContacts(S, b, &Mg);  // (not against its own tube)
                h = wpt(b, {b.headLx, 0});
                if (vdot(vsub(h, mouth), out) > c.oal / 1000 - 0.0005) {  // the nose has left the mouth: it's out
                    Mg.rounds--; Mg.top = nullptr; b.mag = nullptr;
                    setState(b, RS::Free);
                }
                continue;
            }
            if (b.rs == RS::Presented && b.mag) {  // under the lips: it can slide forward, or be pushed down into the magazine
                Body& Mg = *b.mag;
                V2 LH = wpt(Mg, b.lipsL), f = rot(b.feedL, Mg.a), n = rot(b.liftL, Mg.a);
                double fa = std::atan2(f.y, f.x);
                auto holdLevel = [&]() { b.a = fa + b.tilt; b.pa = b.a; b.w = 0; };  // level, or as far as a feed ramp tipped it
                holdLevel();
                V2 h = wpt(b, {b.headLx, 0}), d = vsub(h, LH);
                double s = vdot(d, f), e = vdot(d, n);
                if (s < 0) solve(b, Mg, h, {h.x - f.x * s, h.y - f.y * s}, f.x, f.y, -s);
                h = wpt(b, {b.headLx, 0}); d = vsub(h, LH); e = vdot(d, n);
                if (e > 0) solve(b, Mg, h, {h.x - n.x * e, h.y - n.y * e}, -n.x, -n.y, e);
                double low = b.sideFeed ? 0.0 : std::max(0.008, c.headDia() / 1000 + 0.002);  // as far down as the stack can be pushed
                                                                                                // (a side magazine's goes in sideways)
                if (e < -low) solve(b, Mg, h, {h.x - n.x * (e + low), h.y - n.y * (e + low)}, n.x, n.y, -(e + low));
                bool lifted = presentedContacts(S, b, f, n, s);
                b.wasOut = true;
                {   // held by the lips; the follower can lift it briskly but contacts can't fling it about
                    holdLevel();
                    (void)lifted;
                    V2 mv{Mg.vx, Mg.vy};
                    double dx = b.x - b.px - mv.x * H, dy = b.y - b.py - mv.y * H, dn = dx * n.x + dy * n.y;
                    double lim = 2.5 * H * std::sqrt(springOf(Mg));  // 2.5 m/s, about what a standard magazine spring manages
                    double cl = std::clamp(dn, -lim, lim);
                    b.px += n.x * (dn - cl); b.py += n.y * (dn - cl);
                    double df = dx * f.x + dy * f.y;  // the lips grip it: it only moves forward while something pushes it
                    b.px += f.x * df * 0.2; b.py += f.y * df * 0.2;
                }
                h = wpt(b, {b.headLx, 0});
                double fwd = vdot(vsub(h, LH), f);
                if (b.zFeed != 0) b.zVis = b.zFeed * std::clamp(1 - fwd / (c.oal / 1000 * 0.75), 0.0, 1.0);  // the lips steer it in
                if (b.sideFeed) {   // pushed back into a side magazine, or out at its mouth in the bolt's path
                    double want = b.pushedIn ? b.sideFeed * c.headDia() / 1000 : 0, step = 2.5 * H;   // the follower: ~2.5 m/s
                    b.zVis += std::clamp(want - b.zVis, -step, step);
                }
                if (pass == 0) chamberContacts(S, b, L);  // only to notice the nose entering the chamber mouth
                // stripped off the lips, or pushed forward into the chamber (a barrel that recoils back to sit just in
                // front of the round doesn't count: something has to push it)
                double release = Mg.d ? Mg.d->lipRelease : 0.45;   // the lips' grip: early release for short rounds, late for long
                if (fwd > c.oal / 1000 * release || (b.inChamber && fwd > 0.002)) {
                    Mg.rounds--; Mg.top = nullptr; b.mag = nullptr;
                    if (Mg.rounds == 0 && Mg.d && Mg.d->clip == "enbloc") S.events.push_back({Ev::ClipOut, S.t});  // ping
                    setState(b, RS::Free);
                }
                continue;
            }
            if (b.rs == RS::Clawed) {  // the extractor holds its rim to the breech face; it can swing about the claw
                // the rim sits in a slot across the breech face: it is held tight along the bore, but can slide a little
                // up and down the face (a tilting barrel drops the chamber away under it)
                V2 pr = wpt(b, b.clawR), pb = wpt(*S.BR, b.clawB);
                double ca = std::cos(S.BR->a), sa = std::sin(S.BR->a);
                double dx = pb.x - pr.x, dy = pb.y - pr.y;
                double ax = dx * ca + dy * sa, lat = -dx * sa + dy * ca;
                if (std::fabs(ax) > 0.004 || std::fabs(lat) > 0.006) { setState(b, RS::Free); continue; }  // torn off
                // a chamber that swings away from the breech face (barrels tipped open on a hinge) turns the rim out of
                // the hook: past about 25 degrees it can't hold it, and the chamber alone carries (or drops) the case
                if (std::fabs(std::remainder(S.CH->a - S.BR->a, 2 * 3.14159265358979323846)) > 25 * D2R) { setState(b, RS::Free); continue; }
                if (std::fabs(ax) > 1e-9) solve(b, *S.BR, pr, {pr.x + ca * ax, pr.y + sa * ax}, ax > 0 ? ca : -ca, ax > 0 ? sa : -sa, std::fabs(ax));
                const double play = 0.003;
                if (std::fabs(lat) > play) {
                    double e = lat - std::copysign(play, lat);
                    pr = wpt(b, b.clawR);
                    solve(b, *S.BR, pr, {pr.x - sa * e, pr.y + ca * e}, e > 0 ? -sa : sa, e > 0 ? ca : -ca, std::fabs(e));
                }
                chamberContacts(S, b, L);
                partContacts(S, b);
                double swing = std::remainder(b.a - ang, 2 * 3.14159265358979323846);
                if (std::fabs(swing) > 18 * D2R && !b.inChamber) {  // knocked round off the claw: out it goes, sideways
                    setState(b, RS::Free);
                    b.thrown = true;
                    // the ejector really spins it about the vertical and out to the side; drawn flat we only see the
                    // pitch, so hand most of the throw over to sideways motion
                    double dx = b.x - b.px, dy = b.y - b.py;  // velocities come from the positions after this
                    b.vz = std::max(1.0, 0.6 * std::hypot(dx, dy) / H);
                    b.px = b.x - 0.45 * dx; b.py = b.y - 0.45 * dy;
                    // likewise most of the spin: tumbling end over end at the ejector's full rate would be about the
                    // vertical in a real gun, not in the plane where it would smash into the slide
                    b.pa = b.a - std::clamp(0.6 * (b.a - b.pa), -400 * H, 400 * H);
                    S.events.push_back({b.isCase ? Ev::Eject : Ev::EjectLive, S.t});
                }
            } else if (b.rs == RS::Free) {
                if (pass == 0 && b.vz != 0) {  // moving sideways: a wall it runs into stops it, unless it lines up with a port
                    double nz = b.z + b.vz * H;
                    bbox(b);
                    for (auto& P : S.bodies) {
                        if (&P == &b || !solidPart(S, P)) continue;
                        bool now = std::fabs(P.z - b.z) < P.zh + b.zh, next = std::fabs(P.z - nz) < P.zh + b.zh;
                        if (now || !next) continue;
                        if (P.bb[0] > b.bb[2] || b.bb[0] > P.bb[2] || P.bb[1] > b.bb[3] || b.bb[1] > P.bb[3]) continue;
                        bool hit = false;
                        for (auto& s : b.samples) { V2 w = wpt(b, s), l = lpt(P, w); if (inPoly(l.x, l.y, P.lp)) { hit = true; break; } }
                        if (hit) { nz = b.z; b.vz = -0.25 * b.vz; break; }
                    }
                    b.z = nz;
                }
                bool inGun = b.z - b.zh < S.gunZmax && b.z + b.zh > S.gunZmin && b.x > S.gunBox[0] - 0.02 && b.x < S.gunBox[2] + 0.02 &&
                             b.y > S.gunBox[1] - 0.02 && b.y < S.gunBox[3] + 0.02;  // fell or flew clear of the gun
                if (b.zVis != 0) b.zVis *= std::max(0.0, 1 - 40 * H);  // still being steered in
                if (inGun) { chamberContacts(S, b, L); partContacts(S, b); }
                else setState(b, RS::Loose);  // out through the port: it keeps flying sideways onto the bench
                b.grounded = false;
                groundContact(S, b);
                if (b.grounded && b.rs == RS::Free) setState(b, RS::Loose);  // fell out onto the bench
            } else continue;
            // seated in the chamber
            V2 h = wpt(b, {b.headLx, 0});
            double aH = vdot(vsub(h, L.face), u);
            if (aH < -0.002) b.wasOut = true;
            // seated: pushed home after it had been out, or the breech closed on it again (a short stroke)
            if (b.inChamber && (b.wasOut || L.faceS < 0.0003) && aH > -0.0006 && !S.chambered && !S.spent) {
                bool wasCase = b.isCase;
                retire(S, b);
                if (wasCase) {  // the empty case went back in: nothing kicked it out (counted when the breech closes)
                    S.spent = true;  // (one that never got out, the action only stirred, isn't a failure)
                    if (!S.breechSprung && b.wasOut && S.malf == Malf::None) { S.malf = Malf::FailureToEject; S.malfCount[(int)Malf::FailureToEject]++; S.events.push_back({Ev::Malfunction, S.t}); }
                } else {
                    S.chambered = true;
                    if (b.wasOut) S.fed++;  // (not again for a round the breech merely closed on once more)
                    S.events.push_back({Ev::Chambered, S.t});
                }
            }
        }
    if (!main) return;
    // keep the bench tidy: only the most recent empty cases stay
    for (auto it = S.bodies.rbegin(); it != S.bodies.rend(); ++it)
        if (it->isCase && !it->gone && it->rs == RS::Loose && ++spentOnBench > 30) retire(S, *it);

    // jams: the action has sat open and still, nobody is holding it, and the springs can't close it
    bool open = gap > 0.0015, still = std::hypot(S.BR->vx, S.BR->vy) < 0.02;
    if (gap > c.oal / 1000 * 0.9) S.cycledOpen = true;
    if (gap < 0.0006 && S.cycledOpen) {  // closed again: did it pick up a round?
        S.cycledOpen = false;
        Body* Mg = seatedMag(S);
        bool caseBack = S.spent;
        for (auto& b : S.bodies)  // an empty case still in the action, on the extractor or lying loose in it: it never got out
            if (b.isCase && !b.gone && (b.rs == RS::Clawed || b.rs == RS::Free)) caseBack = true;
        Malf m = caseBack ? Malf::FailureToEject : (!S.chambered && !S.inTransit && Mg && Mg->rounds > 0) ? Malf::FailureToFeed : Malf::None;
        if (m != Malf::None && S.malf == Malf::None && (S.breechSprung || m == Malf::FailureToFeed)) {  // (worked by hand too: an
            // action closed on an empty chamber with rounds still in the magazine failed to feed, whoever closed it)
            S.malf = m; S.malfCount[(int)m]++;
            S.events.push_back({Ev::Malfunction, S.t});
        }
    }
    S.stuckT = ((open || !inBattery(S)) && still && !S.hand.on && S.breechSprung) ? S.stuckT + H : 0;  // stuck open, or short of battery
    if (!open && S.chambered) S.malf = Malf::None;                       // ready to fire again: cleared
    if (inBattery(S) && S.malf == Malf::FailureToReturn) S.malf = Malf::None;
    if (S.stuckT > 0.3 && S.malf == Malf::None) {
        int lives = 0, cases = 0, clawed = 0;
        for (auto& b : S.bodies) {
            if (!b.isRound || b.gone || !(b.rs == RS::Free || b.rs == RS::Clawed)) continue;
            if (b.rs == RS::Clawed) clawed++;
            (b.isCase ? cases : lives)++;
        }
        Malf m = cases && clawed ? Malf::FailureToEject : cases ? Malf::Stovepipe : lives >= 2 ? Malf::DoubleFeed
               : lives ? Malf::FailureToFeed : Malf::FailureToReturn;
        // an open action with nothing left to feed is just empty, not jammed
        Body* Mg = seatedMag(S);
        bool empty = !lives && !cases && (!Mg || Mg->rounds == 0) && !S.chambered;
        if (!empty) {
            S.malf = m; S.malfCount[(int)m]++;
            S.events.push_back({Ev::Malfunction, S.t});
        }
    }
}

}  // namespace sim
