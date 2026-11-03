// Headless checks that each example action actually cycles. Run: ./gunsmith3d_tests
#include "solver.hpp"
#include "reliability.hpp"
#include "version.hpp"
#include "carve.hpp"
#include "hand.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) std::printf("  ok    %s\n", msg);                   \
        else { std::printf("  FAIL  %s\n", msg); failures++; }        \
    } while (0)

static double travel(sim::Sim& S, const char* id) { auto* b = S.find(id); return (b->x - b->x0) * 1000; }

static void tilting() {
    std::printf("Tilting barrel\n");
    auto S = sim::build(loadPreset("legacy-tilting"));
    sim::run(*S, 0.003);
    S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true);
    double minSlide = 0, minAngle = 0, lockedTogether = 0;
    for (int i = 0; i < 400; i++) {
        sim::run(*S, 0.0001);
        double s = travel(*S, "slide"), b = travel(*S, "barrel");
        minSlide = std::min(minSlide, s);
        minAngle = std::min(minAngle, S->find("barrel")->a / sim::D2R);
        if (std::fabs(s - b) < 0.2 && s < -2) lockedTogether = std::min(lockedTogether, s);
        if (i == 150) { sim::setKey(*S, "fire", false); S->trigger = false; }  // a finger takes ~8 ms to pull 2 mm
    }
    CHECK(S->shots == 1, "fires one round");
    CHECK(lockedTogether < -2.0, "slide and barrel recoil locked together first");
    CHECK(minAngle < -1.5, "barrel tilts down to unlock");
    CHECK(minSlide < -30, "slide completes its stroke");
    CHECK(sim::inBattery(*S), "returns to battery");
    CHECK(S->chambered && sim::seatedMag(*S) && sim::seatedMag(*S)->rounds == 12, "chambers the next round from the seated magazine");
    CHECK(minSlide > -33, "slide stops on the frame's drawn rear stop");
}

static void longRecoil() {
    std::printf("Long recoil\n");
    auto S = sim::build(loadPreset("longrecoil"));
    sim::run(*S, 0.003);
    S->trigger = true; S->pressed = true;
    bool heldWhileBarrelReturned = false;
    for (int i = 0; i < 300; i++) {
        sim::run(*S, 0.001);
        if (travel(*S, "bolt") < -60 && travel(*S, "barrel") > -10) heldWhileBarrelReturned = true;
    }
    CHECK(heldWhileBarrelReturned, "catch holds the bolt while the barrel returns");
    CHECK(S->shots >= 3, "keeps firing in full-auto");
}

static void turnBolt() {
    std::printf("Turn bolt\n");
    auto S = sim::build(loadPreset("legacy-turnbolt"));
    auto* b = S->find("bolt");
    S->pressed = true; sim::run(*S, 0.02);
    CHECK(S->shots == 1, "fires when locked");
    S->hand = {true, b, {0, 0}, {b->x0 - 0.05, b->y0}}; sim::run(*S, 0.4); S->hand.on = false;
    CHECK(std::fabs(travel(*S, "bolt")) < 0.5, "cannot be pulled back while locked");
    S->rollHand = {true, b, 90 * sim::D2R}; sim::run(*S, 0.4); S->rollHand.on = false;
    S->hand = {true, b, {0, 0}, {b->x0 - 0.11, b->y0}}; sim::run(*S, 0.6);
    CHECK(travel(*S, "bolt") < -90, "opens after lifting the handle");
    S->rollHand = {true, b, 0}; sim::run(*S, 0.3); S->rollHand.on = false;
    CHECK(b->roll > 70 * sim::D2R, "handle cannot drop while the bolt is open");
    S->hand.t = {b->x0 + 0.01, b->y0}; sim::run(*S, 0.6); S->hand.on = false;
    S->rollHand = {true, b, 0}; sim::run(*S, 0.4); S->rollHand.on = false;
    CHECK(sim::inBattery(*S) && S->chambered, "closes and chambers a new round");
    S->pressed = true; sim::run(*S, 0.02);
    CHECK(S->shots == 2, "fires again");
}

// A bolt with one lug welded to it and a shoulder in the receiver: no lug joint, no unlock angle, just geometry.
static const char* LUG_TEST = R"({"name":"lug test","bodies":[
 {"id":"rcv","name":"Receiver","fixed":true,"layer":"none","thick":30,"pts":[[0,30],[140,30],[140,62],[0,62]]},
 {"id":"shoulder","name":"Locking shoulder","fixed":true,"layer":"ABC","thick":8,"z":0,"pts":[[100,33],[119.5,33],[119.5,38.6],[100,38.6]]},
 {"id":"barrel","name":"Barrel","fixed":true,"layer":"ABC","thick":16,"pts":[[139,38],[400,40],[400,52],[139,54]]},
 {"id":"bolt","name":"Bolt","layer":"ABC","thick":14,"mass":300,"damp":4,"roll":{"on":true,"min":0,"max":90,"handle":30},"pts":[[20,39],[138,39],[138,53],[20,53]]},
 {"id":"lug","name":"Lug","layer":"ABC","thick":6,"mass":10,"pts":[[120,35],[131,35],[131,39],[120,39]]}],
 "joints":[
 {"type":"track","a":"bolt","b":"rcv","p":[80,46],"path":[[-20,46],[80,46]],"lockRot":true},
 {"type":"weld","a":"lug","b":"bolt","p":[125,38]}],
 "breech":{"breech":"bolt","chamber":"barrel","face":[138,46],"muzzle":[400,46],"cartridge":"7.62x54mmR","loaded":true,"feedMode":"single"}})";

static void physicalLugs() {
    std::printf("Rotating bolt locked by geometry\n");
    Design d; designFromJson(LUG_TEST, d);
    auto S = sim::build(d);
    auto* b = S->find("bolt");
    auto* lug = S->find("lug");
    CHECK(lug && lug->rollParent == b, "a part welded to a rotating bolt turns with it");
    S->hand = {true, b, {0, 0}, {b->x0 - 0.08, b->y0}}; sim::run(*S, 0.4); S->hand.on = false;
    CHECK(travel(*S, "bolt") > -0.8, "with the lug up, it can't be pulled back: the lug sits on the shoulder");
    S->pressed = true; sim::run(*S, 0.05);
    CHECK(S->shots == 1 && travel(*S, "bolt") > -1.0, "a shot is held by the lug, not by a rule");
    S->rollHand = {true, b, 90 * sim::D2R}; sim::run(*S, 0.3);
    CHECK(std::fabs(lug->z) - lug->zh > 0.004, "turning the bolt swings the lug round to the side, clear of the shoulder");
    S->hand = {true, b, {0, 0}, {b->x0 - 0.08, b->y0}}; sim::run(*S, 0.5);
    CHECK(travel(*S, "bolt") < -60, "turned, the lug clears the shoulder and the bolt draws back");
    S->rollHand.t = 0; sim::run(*S, 0.3);
    S->hand.t = {b->x0 + 0.01, b->y0}; sim::run(*S, 0.5);
    CHECK(travel(*S, "bolt") < -10, "with the handle down while open, the lug hits the back of the shoulder and it won't close");
    S->rollHand.t = 90 * sim::D2R; sim::run(*S, 0.3); sim::run(*S, 0.5);
    S->rollHand.t = 0; sim::run(*S, 0.4); S->rollHand.on = false; S->hand.on = false;
    CHECK(std::fabs(travel(*S, "bolt")) < 0.8 && b->roll < 5 * sim::D2R, "pushed home and turned down, it locks again");
}

static void magazines() {
    std::printf("Magazines\n");
    auto S = sim::build(loadPreset("legacy-tilting"));
    auto* J = sim::magwell(*S);
    CHECK(J && sim::seatedMag(*S), "starts with a magazine latched in the well");
    // press the release: the magazine falls out under gravity and lands on the baseplate
    S->magRelease = true; sim::run(*S, 0.05); S->magRelease = false; sim::run(*S, 0.6);
    auto* old = S->find("mag");
    double bottom = -1e9;
    for (auto& l : old->lp) bottom = std::max(bottom, sim::wpt(*old, l).y);
    CHECK(!sim::seatedMag(*S) && old->loose, "release drops the magazine out of the well");
    CHECK(std::fabs(bottom - S->groundY) < 0.002, "dropped magazine comes to rest on the baseplate");
    // firing with no magazine: the chambered round fires, nothing feeds
    S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true); sim::run(*S, 0.06);
    sim::setKey(*S, "fire", false); S->trigger = false; sim::run(*S, 0.1);
    CHECK(S->shots == 1 && !S->chambered, "fires the chambered round, then nothing to feed");
    // pick up a fresh magazine and push it home by hand
    auto* m = sim::spawnMagazine(*S, {120, groundLevelMM(S->design) - 52}, false);
    sim::run(*S, 0.4);
    V2 exitW = sim::wpt(*J->B, J->path.back()), seatW = sim::wpt(*J->B, J->path.front());
    S->hand.on = true; S->hand.b = m; S->hand.l = J->la;
    S->hand.t = {exitW.x, exitW.y + 0.01};
    sim::run(*S, 0.6);
    S->hand.t = {seatW.x, seatW.y - 0.003};
    sim::run(*S, 0.6);
    S->hand.on = false; sim::run(*S, 0.1);
    CHECK(sim::seatedMag(*S) == m && m->rounds == 13, "a fresh magazine pushed into the well latches");
    // rack the slide by hand to chamber a round
    auto* slide = S->find("slide");
    S->hand = {true, slide, {0, 0}, {slide->x0 - 0.035, slide->y0}};
    sim::run(*S, 0.5);
    S->hand.on = false; sim::run(*S, 0.3);
    CHECK(S->chambered && m->rounds == 12 && sim::inBattery(*S), "racking the slide by hand chambers a round");
    // quick reload swaps in a full magazine
    sim::quickReload(*S); sim::run(*S, 0.3);
    CHECK(sim::seatedMag(*S) && sim::seatedMag(*S) != m && sim::seatedMag(*S)->rounds == 13 && m->loose, "quick reload drops the old magazine and seats a full one");
}

static void controlsAndSprings() {
    std::printf("Controls and springs\n");
    {   // the magazine catch is a real button: pressing it frees the magazine
        auto S = sim::build(loadPreset("legacy-tilting"));
        sim::run(*S, 0.05);
        sim::setKey(*S, "magrelease", true); sim::run(*S, 0.4); sim::setKey(*S, "magrelease", false); sim::run(*S, 0.3);
        CHECK(!sim::seatedMag(*S) && S->find("mag")->loose, "pressing the mag release button drops the magazine");
        CHECK(std::fabs(S->find("magbtn")->x - S->find("magbtn")->x0) < 0.0005, "its spring pushes the button back out");
    }
    {   // without pulling the trigger part far enough, nothing fires
        auto S = sim::build(loadPreset("legacy-tilting"));
        sim::run(*S, 0.05);
        S->pressed = true; S->trigger = true; sim::run(*S, 0.05);
        CHECK(S->shots == 0, "FIRE alone does nothing until the trigger part has been pulled");
        auto* tr = S->find("trigger");
        sim::setKey(*S, "fire", true); sim::run(*S, 0.05);
        double pulled = tr->a;
        sim::setKey(*S, "fire", false); S->trigger = false; sim::run(*S, 0.2);
        CHECK(S->shots == 1 && pulled > 0.1, "pulling the trigger part fires");
        CHECK(std::fabs(tr->a - pulled) > 0.1, "the leaf spring returns the trigger");
    }
    {   // coil springs: solid height is a hard stop, and a long unguided coil buckles
        Design d = loadPreset("blank");
        BodyDef b; b.id = "blk"; b.name = "Block"; b.pts = {{100, 20}, {120, 20}, {120, 40}, {100, 40}}; fixShape(b); d.bodies.push_back(b);
        JointDef j; j.type = "track"; j.a = "blk"; j.b = "frame"; j.p = {110, 30}; j.path = {{20, 30}, {110, 30}}; j.lockRot = true; d.joints.push_back(j);
        JointDef sp; sp.type = "spring"; sp.a = "blk"; sp.b = "frame"; sp.pa = {100, 30}; sp.pb = {0, 30}; sp.k = 2; sp.rest = 120; sp.kind = "coil"; sp.coilDia = 8; sp.wire = 1.5; sp.turns = 20; d.joints.push_back(sp);
        auto S = sim::build(d);
        auto* blk = S->find("blk");
        S->handStrength = 2000;
        S->hand = {true, blk, {0, 0}, {blk->x0 - 0.09, blk->y0}};
        sim::run(*S, 0.3);
        double len = (blk->x - 0.010) * 1000;  // spring end on the block is 10 mm behind its centre
        CHECK(len > 32.5, "a coil spring stops at its solid height");
        S->hand.t = {blk->x0 - 0.04, blk->y0}; sim::run(*S, 0.3);
        bool buckled = false;
        for (auto& J : S->joints) if (J.type == sim::JT::Spring) buckled = J.buckled;
        CHECK(buckled, "a long coil spring with no guide rod buckles");
        Design d2 = d;
        BodyDef rod; rod.id = "rod"; rod.name = "Rod"; rod.fixed = true; rod.pts = {{5, 29}, {95, 29}, {95, 31}, {5, 31}}; fixShape(rod); d2.bodies.push_back(rod);
        d2.joints.back().guide = "rod";
        auto S2 = sim::build(d2);
        auto* b2 = S2->find("blk");
        S2->hand = {true, b2, {0, 0}, {b2->x0 - 0.04, b2->y0}}; sim::run(*S2, 0.3);
        for (auto& J : S2->joints) if (J.type == sim::JT::Spring) buckled = J.buckled;
        CHECK(!buckled, "the same spring on a guide rod does not");
    }
}

static void feedAndCartridges() {
    std::printf("Feeding, cartridges, magazine maker\n");
    {   // single shot: open, drop a round in by hand, close, fire
        auto S = sim::build(loadPreset("legacy-single"));
        auto* b = S->find("bolt");
        CHECK(!S->chambered && !sim::handLoad(*S), "single shot starts empty and cannot be loaded while closed");
        S->rollHand = {true, b, 90 * sim::D2R}; sim::run(*S, 0.4); S->rollHand.on = false;
        S->hand = {true, b, {0, 0}, {b->x0 - 0.09, b->y0}}; sim::run(*S, 0.6);
        CHECK(sim::handLoad(*S) && S->chambered, "a round can be dropped into the open chamber");
        S->hand.t = {b->x0 + 0.01, b->y0}; sim::run(*S, 0.6); S->hand.on = false;
        S->rollHand = {true, b, 0}; sim::run(*S, 0.4); S->rollHand.on = false;
        S->pressed = true; sim::run(*S, 0.02);
        CHECK(S->shots == 1, "closes and fires the hand-loaded round");
        S->rollHand = {true, b, 90 * sim::D2R}; sim::run(*S, 0.4); S->rollHand.on = false;
        S->hand = {true, b, {0, 0}, {b->x0 - 0.09, b->y0}}; sim::run(*S, 0.6);
        CHECK(!S->chambered && !S->inTransit, "nothing feeds by itself: it stays a single shot");
    }
    {   // single shot loaded with a physical round: pick it up, put it in the open action, close, fire
        auto S = sim::build(loadPreset("legacy-single"));
        auto* b = S->find("bolt");
        auto open = [&]() {
            S->rollHand = {true, b, 90 * sim::D2R}; sim::run(*S, 0.4); S->rollHand.on = false;
            S->hand = {true, b, {0, 0}, {b->x0 - 0.09, b->y0}}; sim::run(*S, 0.6); S->hand.on = false;
        };
        auto close = [&]() {
            S->hand = {true, b, {0, 0}, {b->x0 + 0.01, b->y0}}; sim::run(*S, 0.6); S->hand.on = false;
            S->rollHand = {true, b, 0}; sim::run(*S, 0.4); S->rollHand.on = false;
        };
        open();
        double ground = groundLevelMM(S->design);
        sim::Body* r = sim::spawnRound(*S, {60, ground - 20});
        sim::run(*S, 0.5);
        double low = -1e9;
        for (auto& l : r->lp) low = std::max(low, sim::wpt(*r, l).y);
        CHECK(r && r->loose && std::fabs(low - S->groundY) < 0.002, "a round put on the bench lies on the baseplate");
        // pick it up and hold it in the open action, just in front of the bolt face and in line with the bore
        sim::BoreLine L = sim::boreLine(*S);
        V2 grab = sim::wpt(*r, {0, 0});
        S->hand = {true, r, sim::lpt(*r, grab), grab};
        for (int k = 1; k <= 40; k++) {  // move the hand up to the port over 0.4 s
            V2 tgt{L.face.x - L.dir.x * 0.02 + (r->x - (r->x + std::cos(r->a) * r->headLx)), L.face.y - L.dir.y * 0.02};
            S->hand.t = {grab.x + (tgt.x - grab.x) * k / 40, grab.y + (tgt.y - grab.y) * k / 40};
            sim::run(*S, 0.01);
        }
        CHECK(r->staged && !S->chambered, "held in line with the bore in the open action, it drops into place");
        S->hand.on = false; sim::run(*S, 0.3);
        CHECK(r->staged, "it stays in the action when let go");
        close();
        CHECK(r->gone && S->chambered && sim::inBattery(*S), "closing the bolt pushes it into the chamber");
        S->pressed = true; sim::run(*S, 0.02);
        CHECK(S->shots == 1, "and it fires");
        open();
        CHECK(!S->chambered && sim::looseRounds(*S) == 0, "opening after the shot throws out only the empty case");
        // load again, close, then open without firing: the live round comes out and can be picked up
        sim::Body* r2 = sim::spawnRound(*S, {L.face.x * 1000 - 25, L.face.y * 1000});
        V2 g2 = sim::wpt(*r2, {0, 0});
        S->hand = {true, r2, sim::lpt(*r2, g2), g2}; sim::run(*S, 0.05); S->hand.on = false;
        CHECK(r2->staged, "a round placed straight into the open action drops in");
        close();
        CHECK(S->chambered, "and chambers");
        open(); sim::run(*S, 0.8);
        sim::Body* out = nullptr;
        for (auto& bb : S->bodies) if (bb.isRound && !bb.gone) out = &bb;
        CHECK(!S->chambered && out && out->loose && out->grounded, "opening a loaded action ejects the live round onto the bench");
    }
    {   // racking a loaded pistol throws out the chambered round and feeds the next
        auto S = sim::build(loadPreset("legacy-tilting"));
        auto* sl = S->find("slide");
        int before = sim::seatedMag(*S)->rounds;
        S->hand = {true, sl, {-0.06, -0.006}, {sl->x0 - 0.06 - 0.035, sl->y0 - 0.006}}; sim::run(*S, 0.25);
        S->hand.on = false; sim::run(*S, 0.4);
        CHECK(sim::looseRounds(*S) == 1 && S->chambered && sim::seatedMag(*S)->rounds == before - 1, "racking a loaded pistol ejects the live round and chambers the next");
    }
    {
        BreechDef br; br.cart = cartridgePresets()[5]; br.cartImpulse = true; applyCartridge(br);
        CHECK(br.cart.name == "7.62x54mmR" && br.roundLen > 77 && br.impulse > 9 && br.impulse < 13, "cartridge presets set round size and recoil");
    }
    {   // extended magazine: taller, still feeds
        Design d = loadPreset("legacy-tilting");
        int mi = d.bodyIndex("mag");
        double h13 = d.bodies[mi].gen.H;
        d.bodies[mi].magCap = 33; generateMagazine(d, mi);
        CHECK(d.bodies[mi].gen.H > h13 * 2, "a 33-round magazine is generated much longer");
        auto S = sim::build(d);
        CHECK(sim::seatedMag(*S) && sim::seatedMag(*S)->rounds == 33, "and it seats with 33 rounds");
        sim::run(*S, 0.05);
        S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true); sim::run(*S, 0.06);
        sim::setKey(*S, "fire", false); S->trigger = false; sim::run(*S, 0.2);
        CHECK(S->shots == 1 && S->chambered && sim::seatedMag(*S)->rounds == 32, "and feeds the gun");
        d.bodies[mi].gen.curve = 25; generateMagazine(d, mi);
        auto S2 = sim::build(d);
        CHECK(sim::seatedMag(*S2) != nullptr, "a curved magazine also seats");
    }
    {   // rounded corners and curved edges change the real shape
        BodyDef b; b.pts = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; fixShape(b);
        double a0 = std::fabs(sim::polyArea(outlineOf(b)));
        b.rad = {5, 5, 5, 5};
        double a1 = std::fabs(sim::polyArea(outlineOf(b)));
        b.rad = {0, 0, 0, 0}; b.bulge = {0, 3, 0, 0};
        double a2 = std::fabs(sim::polyArea(outlineOf(b)));
        CHECK(a1 < a0 - 15 && a2 > a0 + 30, "fillets remove area and outward curves add it");
    }
}

static void blowbackAndEditing() {
    std::printf("Blowback pistol and editing tools\n");
    {
        auto S = sim::build(loadPreset("legacy-blowback"));
        sim::run(*S, 0.05);
        CHECK(sim::inBattery(*S) && S->chambered, "blowback pistol rests in battery with a round chambered");
        auto* sl = S->find("slide");
        double minSlide = 0;
        for (int shot = 0; shot < 3; shot++) {
            sim::setKey(*S, "fire", true); S->trigger = true; S->pressed = true;
            for (int i = 0; i < 60; i++) { sim::run(*S, 0.001); minSlide = std::min(minSlide, (sl->x - sl->x0) * 1000); }
            sim::setKey(*S, "fire", false); S->trigger = false;
            sim::run(*S, 0.2);
        }
        CHECK(S->shots == 3, "fires three shots, one per trigger pull");
        CHECK(minSlide < -25 && minSlide > -33, "the slide runs back on its drawn rails to the stop");
        CHECK(sim::inBattery(*S) && S->chambered && sim::seatedMag(*S)->rounds == 10, "feeds every shot from the magazine");
    }
    {   // knife: a frame drawn as one block, cut into a slide and a frame
        BodyDef b; b.pts = {{0, 0}, {100, 0}, {100, 40}, {0, 40}}; fixShape(b);
        auto pcs = splitBody(b, {-5, 20}, {105, 20});
        CHECK(pcs.size() == 2 && std::fabs(std::fabs(sim::polyArea(pcs[0].pts)) - 2000) < 1, "the knife cuts a block into two parts");
        BodyDef u; u.pts = {{0, 0}, {10, 0}, {10, 30}, {30, 30}, {30, 0}, {40, 0}, {40, 40}, {0, 40}}; fixShape(u);
        CHECK(splitBody(u, {-5, 15}, {45, 15}).size() == 3, "cutting across a U makes three pieces");
        BodyDef m = u; mirrorBody(m, {50, 0}, {50, 1});
        CHECK(std::fabs(std::fabs(sim::polyArea(m.pts)) - std::fabs(sim::polyArea(u.pts))) < 0.01 && m.pts[0].x >= 60, "mirroring keeps the shape and moves it across the axis");
        BodyDef f = u; flipBody(f, true);
        CHECK(std::fabs(std::fabs(sim::polyArea(f.pts)) - std::fabs(sim::polyArea(u.pts))) < 0.01 && sim::polyArea(f.pts) * sim::polyArea(u.pts) > 0, "flipping keeps the area and the winding");
        BodyDef sd = b; subdivideBody(sd);
        CHECK(sd.pts.size() == 8 && sd.rad.size() == 8 && std::fabs(sim::polyArea(sd.pts) - sim::polyArea(b.pts)) < 0.01, "subdividing doubles the vertices without changing the shape");
        BodyDef ci; ci.pts = circlePts({0, 0}, 10, &ci.bulge); fixShape(ci);
        auto halves = splitBody(ci, {-20, 0}, {20, 0});
        CHECK(halves.size() == 2 && std::fabs(std::fabs(sim::polyArea(halves[0].pts)) - 157) < 4, "cutting a circle keeps its curved edges");
    }
}

// The physical blowback pistol: every round, case, lip, ramp and the ejector is a real part. These check that the
// design as drawn runs, and that breaking the design breaks it the way it would break a real gun.
static BodyDef* findDef(Design& d, const std::string& id) { for (auto& b : d.bodies) if (b.id == id) return &b; return nullptr; }
static sim::Body* rootOf(sim::Body* b) { return b->root ? b->root : b; }
static std::unique_ptr<sim::Sim> shootPistol(const Design& d, int pulls = 7) {
    auto S = sim::build(d);
    sim::run(*S, 0.05);
    for (int s = 0; s < pulls && S->malf == sim::Malf::None; s++) {
        sim::setKey(*S, "fire", true); sim::run(*S, 0.08);
        sim::setKey(*S, "fire", false); sim::run(*S, 0.5);
    }
    return S;
}
static void rackSlide(sim::Sim& S) {
    sim::Body* sl = rootOf(S.find("slide"));
    S.hand = {true, sl, {0, 0}, {sl->x0 - 0.046, sl->y0}}; sim::run(S, 0.25);  // all the way back to the stop
    S.hand.on = false; sim::run(S, 0.3);
}
static void physicalPistol(const char* key) {
    const Design base = loadPreset(key);
    std::printf("%s (physical parts)\n", base.name.c_str());
    const bool tilting = std::string(key) == "tilting";
    int cap = 0;
    for (auto& b : base.bodies) if (b.magazine) cap = b.magCap;
    {
        auto S = shootPistol(base, cap + 1);
        CHECK(S->shots == cap + 1 && S->fed == cap && S->malf == sim::Malf::None, "as drawn it fires the chambered round and feeds every round from the magazine");
        int cases = 0;
        for (auto& b : S->bodies) if (b.isCase && !b.gone) cases++;
        CHECK(cases >= 7, "every empty case is thrown clear as a real part");
        CHECK(S->recoilImpulse > 1 && S->recoilImpulse < 10, "the frame feels a plausible recoil impulse");
    }
    if (tilting) {  // locked breech: barrel and slide recoil together, then the cam pulls the barrel down and stops it
        auto S = sim::build(base);
        sim::run(*S, 0.05);
        sim::Body *br = S->find("barrel"), *sl = rootOf(S->find("slide"));
        sim::setKey(*S, "fire", true);
        double minB = 0, minS = 0, gapAt2 = -1, dropMax = 0;
        for (int i = 0; i < 480; i++) {
            sim::substep(*S);
            minB = std::min(minB, (br->x - br->x0) * 1000); minS = std::min(minS, (sl->x - sl->x0) * 1000);
            if (gapAt2 < 0 && (sl->x - sl->x0) < -0.002) gapAt2 = sim::breechGap(*S);
            dropMax = std::max(dropMax, (br->y - br->y0) * 1000);
        }
        CHECK(gapAt2 >= 0 && gapAt2 < 0.6, "barrel and slide stay locked for the first 2 mm");
        CHECK(minB < -5.5 && minB > -8 && dropMax > 1.2, "then the cam drops the barrel and stops it about 6 mm back");
        CHECK(minS < -40, "while the slide runs on to its stop");
    }
    {
        auto S = sim::build(base);
        sim::run(*S, 0.05);
        sim::setKey(*S, "fire", true); sim::run(*S, 0.5);
        CHECK(S->shots == 1, "holding the trigger fires once: the connector drops the link off the sear");
        sim::setKey(*S, "fire", false); sim::run(*S, 0.1);
        sim::setKey(*S, "fire", true); sim::run(*S, 0.1);
        CHECK(S->shots == 2, "letting the trigger out resets it for the next shot");
    }
    {
        auto S = sim::build(base);
        sim::run(*S, 0.05);
        int before = sim::seatedMag(*S)->rounds, loose = sim::looseRounds(*S);
        rackSlide(*S);
        CHECK(S->chambered && sim::inBattery(*S) && sim::seatedMag(*S)->rounds == before - 1 && sim::looseRounds(*S) == loose + 1,
              "racking by hand throws out the live round and chambers the next");
    }
    {
        Design d = base;
        for (auto& b : d.bodies) if (b.magazine) { b.gen.top.y += 3.5; generateMagazine(d, (int)(&b - &d.bodies[0])); }
        auto S = shootPistol(d);
        CHECK(S->shots == 1 && S->malf == sim::Malf::FailureToFeed, "a magazine that sits too low: the slide passes over the round, failure to feed");
    }
    {
        Design d = base;
        for (size_t i = 0; i < d.bodies.size(); i++) if (d.bodies[i].id == "ejector") { d.bodies.erase(d.bodies.begin() + i); break; }
        auto S = shootPistol(d);
        if (!tilting) CHECK(S->shots == 1 && S->malf == sim::Malf::FailureToEject, "without an ejector the case rides back in: failure to eject");
        // the tilting gun's case swings off the extractor into the next round's way instead, so the next round doesn't feed
        else CHECK(S->shots == 1 && (S->malf == sim::Malf::FailureToEject || S->malf == sim::Malf::FailureToFeed), "without an ejector it jams on the next round");
        // clear it by hand: hold the slide back, pick the case off the breech face, let the slide go
        sim::Body* sl = rootOf(S->find("slide"));
        S->hand = {true, sl, {0, 0}, {sl->x0 - 0.046, sl->y0}}; sim::run(*S, 0.25);
        sim::Body* cs = nullptr;
        for (auto& b : S->bodies) if (b.isCase && !b.gone && b.rs != sim::RS::Loose) cs = &b;
        if (!tilting) CHECK(cs && cs->rs == sim::RS::Clawed, "with the slide held back the case is still on the extractor");
        // (on the tilting gun it usually falls off by itself once the slide is all the way back)
        if (cs) { V2 g = sim::wpt(*cs, {0, 0}); S->hand = {true, cs, {0, 0}, {g.x, g.y - 0.04}}; sim::run(*S, 0.3); }
        S->hand.on = false; sim::run(*S, 0.5);
        CHECK(S->chambered && S->malf == sim::Malf::None && sim::inBattery(*S), "picking the case out by hand clears it, and the slide chambers a round");
    }
    if (!tilting) {
        Design d = base;
        auto* w = findDef(d, "wall_r");
        w->pts = {{5, 8}, {70, 8}, {70, 26}, {76, 26}, {76, 8}, {152, 8}, {152, 27}, {5, 27}}; w->rad.clear(); w->bulge.clear(); fixShape(*w);
        auto* t = findDef(d, "slide");
        t->pts = {{5, 8}, {86, 8}, {86, 13.3}, {5, 13.3}}; t->rad.clear(); t->bulge.clear(); fixShape(*t);
        auto S = shootPistol(d);
        CHECK(S->shots == 1 && S->malf != sim::Malf::None, "an ejection port too small to pass the case jams the gun");
    }
}

// The bolt-action rifle: turn bolt locked by its lugs, cock-on-close striker, magazine, ejector, all real parts.
static void workBolt(sim::Sim& S, double stroke = 0.09) {  // fire, lift the handle, pull back, push home, turn down
    auto* b = S.find("bolt");
    sim::setKey(S, "fire", true); sim::run(S, 0.12); sim::setKey(S, "fire", false); sim::run(S, 0.1);
    S.rollHand = {true, b, 90 * sim::D2R, 0}; sim::run(S, 0.25);
    S.hand = {true, b, {0, 0}, {b->x0, b->y0}};
    for (int k = 1; k <= 50; k++) { S.hand.t = {b->x0 - stroke * k / 50, b->y0}; sim::run(S, 0.006); }  // about 1.5 m/s
    sim::run(S, 0.2);
    for (int k = 1; k <= 50; k++) { S.hand.t = {b->x0 - stroke + (stroke + 0.005) * k / 50, b->y0}; sim::run(S, 0.006); }
    sim::run(S, 0.3);
    S.rollHand.t = 0; sim::run(S, 0.3); S.rollHand.on = false; S.hand.on = false; sim::run(S, 0.1);
}
static void boltRifle() {
    std::printf("Bolt-action and single-shot rifles (physical parts)\n");
    const Design base = loadPreset("boltaction");
    {
        auto S = sim::build(base);
        sim::run(*S, 0.1);
        auto* b = S->find("bolt");
        S->hand = {true, b, {0, 0}, {b->x0 - 0.05, b->y0}}; sim::run(*S, 0.3); S->hand.on = false;
        CHECK(travel(*S, "bolt") > -1.0, "with the handle down the lugs sit behind their shoulders: the bolt won't come back");
        sim::run(*S, 0.2);
        for (int i = 0; i < 6; i++) workBolt(*S);
        int cases = 0;
        for (auto& x : S->bodies) if (x.isCase && !x.gone && x.rs == sim::RS::Loose) cases++;
        CHECK(S->shots == 6 && S->fed == 5 && S->malf == sim::Malf::None, "fires the chambered round and all five from the magazine, one per stroke of the bolt");
        CHECK(cases == 6, "every empty case is pulled out and thrown clear by the ejector");
    }
    {
        Design d = base;
        for (size_t i = 0; i < d.bodies.size(); i++) if (d.bodies[i].id == "ejector") { d.bodies.erase(d.bodies.begin() + i); break; }
        auto S = sim::build(d); sim::run(*S, 0.1);
        workBolt(*S);
        CHECK(S->shots == 1 && S->malf == sim::Malf::FailureToEject, "without an ejector the case stays on the bolt face: failure to eject");
    }
    {
        Design d = base;
        for (auto& b : d.bodies) if (b.magazine) { b.gen.top.y += 4; generateMagazine(d, (int)(&b - &d.bodies[0])); }
        auto S = sim::build(d); sim::run(*S, 0.1);
        workBolt(*S);
        CHECK(S->shots == 1 && S->malf == sim::Malf::FailureToFeed, "a magazine sitting too low: the bolt rides over the round, failure to feed");
    }
    {   // the single shot: open, put a round from the bench into the action by hand, close, fire
        auto S = sim::build(loadPreset("single"));
        sim::run(*S, 0.1);
        auto* b = S->find("bolt");
        CHECK(!sim::seatedMag(*S) && S->chambered, "the single shot has no magazine and starts with a round chambered");
        int ok = 0;
        for (int n = 0; n < 2; n++) {
            sim::setKey(*S, "fire", true); sim::run(*S, 0.12); sim::setKey(*S, "fire", false); sim::run(*S, 0.1);
            S->rollHand = {true, b, 90 * sim::D2R, 0}; sim::run(*S, 0.25);
            S->hand = {true, b, {0, 0}, {b->x0, b->y0}};
            for (int k = 1; k <= 50; k++) { S->hand.t = {b->x0 - 0.09 * k / 50, b->y0}; sim::run(*S, 0.006); }
            sim::run(*S, 0.3); S->hand.on = false;
            sim::Body* r = sim::spawnRound(*S, {60, groundLevelMM(S->design) - 20});
            sim::run(*S, 0.3);
            sim::BoreLine L = sim::boreLine(*S);
            V2 grab = sim::wpt(*r, {0, 0});
            S->hand = {true, r, sim::lpt(*r, grab), grab};
            for (int k = 1; k <= 40; k++) {
                V2 tgt{L.face.x - L.dir.x * 0.02 - std::cos(r->a) * r->headLx, L.face.y - L.dir.y * 0.02};
                S->hand.t = {grab.x + (tgt.x - grab.x) * k / 40, grab.y + (tgt.y - grab.y) * k / 40};
                sim::run(*S, 0.01);
            }
            S->hand.on = false; sim::run(*S, 0.2);
            S->hand = {true, b, {0, 0}, {b->x0 - 0.09, b->y0}};
            for (int k = 1; k <= 50; k++) { S->hand.t = {b->x0 - 0.09 + 0.095 * k / 50, b->y0}; sim::run(*S, 0.006); }
            sim::run(*S, 0.3);
            S->rollHand.t = 0; sim::run(*S, 0.3); S->rollHand.on = false; S->hand.on = false; sim::run(*S, 0.1);
            if (S->chambered && sim::inBattery(*S)) ok++;
        }
        sim::setKey(*S, "fire", true); sim::run(*S, 0.12);
        CHECK(ok == 2 && S->shots == 3, "each round loaded by hand is pushed into the chamber by the bolt and fires");
    }
}

static void roundTrip() {
    std::printf("Blueprint JSON\n");
    Design a = loadPreset("longrecoil"), b;
    bool ok = designFromJson(designToJson(a), b);
    CHECK(ok && b.bodies.size() == a.bodies.size() && b.joints.size() == a.joints.size(), "survives save and load");
    CHECK(b.format == GUNSMITH_BLUEPRINT_FORMAT && b.savedWith == std::string("Gunsmith3D ") + version::text() && blueprintVersionNote(b).empty(),
          "records the version that saved it");
    std::string old = designToJson(a);
    auto cut = [&](const std::string& key) {  // remove "key":value, from the compact JSON
        size_t a = old.find("\"" + key + "\""), b = old.find(',', a);
        if (a != std::string::npos) old.erase(a, b - a + 1);
    };
    cut("format"); cut("savedWith");  // now it looks like a blueprint from before versioning
    Design c;
    CHECK(designFromJson(old, c) && c.savedWith.empty() && blueprintVersionNote(c).empty(), "still opens blueprints from before versioning");
    std::string newer = designToJson(a);
    newer.replace(newer.find("\"format\":1"), 10, "\"format\":99");
    Design e;
    CHECK(designFromJson(newer, e) && !blueprintVersionNote(e).empty(), "warns about blueprints from a newer version");
}

void mapTests(int& failures);

static double outlineArea(const BodyDef& b) {
    auto p = outlineOf(b);
    double a = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += p[j].x * p[i].y - p[i].x * p[j].y;
    return std::fabs(a) / 2;
}
static void edgeSplitting() {
    std::printf("Splitting edges\n");
    BodyDef b; b.pts = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; fixShape(b);
    int v = splitEdge(b, 0);
    CHECK(v == 1 && b.pts.size() == 5 && std::fabs(b.pts[1].x - 20) < 1e-9 && std::fabs(b.pts[1].y) < 1e-9, "a straight edge splits exactly halfway");
    CHECK(std::fabs(outlineArea(b) - 800) < 1e-6, "the shape doesn't change");
    BodyDef c; c.pts = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; fixShape(c);
    c.bulge[2] = 6;  // the bottom edge bows outward (y down)
    double before = outlineArea(c);
    splitEdge(c, 2);
    CHECK(c.pts.size() == 5 && std::fabs(c.pts[3].x - 20) < 0.01 && std::fabs(c.pts[3].y - 26) < 0.01, "a curved edge splits on the curve");
    CHECK(std::fabs(outlineArea(c) - before) < before * 0.005, "and the curve keeps its shape");
    BodyDef d; d.pts = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; fixShape(d);
    subdivideBody(d);
    CHECK(d.pts.size() == 8 && std::fabs(d.pts[1].x - 20) < 1e-9 && std::fabs(d.pts[3].y - 10) < 1e-9, "splitting all edges halves each one");
    BodyDef e; e.pts = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; fixShape(e);
    for (int k = 0; k < 10; k++) subdivideBody(e);
    CHECK((int)e.pts.size() == MAX_CORNERS && std::fabs(outlineArea(e) - 800) < 1e-6, "splitting over and over stops at the corner limit, with the shape unchanged");
    CHECK(splitEdge(e, 0) == -1 && subdivideBody(e) == 0 && (int)e.pts.size() == MAX_CORNERS, "a part at the limit takes no more corners");
    // one long edge on top, the bottom already in many small pieces: two corners' room left
    BodyDef f; f.pts = {{0, 0}, {60, 0}, {60, 2}};
    int bottom = MAX_CORNERS - 5;
    for (int k = 1; k < bottom; k++) f.pts.push_back({60 - 60.0 * k / bottom, 2});
    f.pts.push_back({0, 2}); fixShape(f);
    subdivideBody(f);
    double longest = 0;
    for (size_t i = 0; i < f.pts.size(); i++) { V2 p = f.pts[i], q = f.pts[(i + 1) % f.pts.size()]; longest = std::max(longest, std::hypot(q.x - p.x, q.y - p.y)); }
    CHECK((int)f.pts.size() == MAX_CORNERS && longest < 31, "when they can't all be split, the longest edges are split first");
    BodyDef g; g.pts = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; fixShape(g);
    g.pts.clear();  // an old blueprint's part with far too many corners: a rectangle with a corner every 1 mm
    for (int x = 0; x < 40; x++) g.pts.push_back({(double)x, 0});
    for (int y = 0; y < 20; y++) g.pts.push_back({40, (double)y});
    for (int x = 40; x > 0; x--) g.pts.push_back({(double)x, 20});
    for (int y = 20; y > 0; y--) g.pts.push_back({0, (double)y});
    g.rad.clear(); g.bulge.clear(); fixShape(g);
    simplifyBody(g);
    CHECK((int)g.pts.size() <= MAX_CORNERS && std::fabs(outlineArea(g) - 800) < 1e-6, "simplifying an over-split part keeps its shape");
}

// The built-in hammer or striker: no parts, but the action has to cock it, and the fire mode decides follow-up shots.
static int pullAndHold(sim::Sim& S, double holdS, double releaseS = 0.1) {
    int before = S.shots;
    S.pressed = true; S.trigger = true;
    sim::run(S, holdS);
    S.trigger = false;
    sim::run(S, releaseS);
    return S.shots - before;
}
static void builtInFireControl() {
    std::printf("Built-in fire control\n");
    Design base = loadPreset("longrecoil");
    base.breech.ignition = "button";
    {
        Design d = base; d.breech.fireMode = "semi"; d.breech.autoFire = false; d.breech.cockAt = 20;
        auto S = sim::build(d); sim::run(*S, 0.003);
        CHECK(pullAndHold(*S, 0.3) == 1, "semi: holding the trigger fires once");
        CHECK(S->cocked, "semi: the action cocked it again");
        CHECK(pullAndHold(*S, 0.3) == 1, "semi: a fresh pull fires again");
    }
    {
        Design d = base; d.breech.fireMode = "semi"; d.breech.autoFire = false; d.breech.cockAt = 500;  // the bolt never opens that far
        auto S = sim::build(d); sim::run(*S, 0.003);
        CHECK(pullAndHold(*S, 0.3) == 1, "fires the first shot from a cocked start");
        CHECK(!S->cocked, "stays down when the action doesn't open far enough");
        CHECK(pullAndHold(*S, 0.3) == 0 && S->deadPull, "single action: a pull with it down does nothing");
        S->design.breech.doubleAction = true;
        CHECK(pullAndHold(*S, 0.3) == 1, "double action: the pull cocks and fires it");
    }
    {
        Design d = base; d.breech.fireMode = "burst"; d.breech.autoFire = false; d.breech.burst = 2; d.breech.cockAt = 20;
        auto S = sim::build(d); sim::run(*S, 0.003);
        CHECK(pullAndHold(*S, 0.6) == 2, "burst of two: holding fires exactly two");
        CHECK(pullAndHold(*S, 0.6) == 2, "burst of two: the next pull fires two more");
    }
    {
        Design d = base; d.breech.fireMode = "auto"; d.breech.cockAt = 20;
        auto S = sim::build(d); sim::run(*S, 0.003);
        CHECK(pullAndHold(*S, 0.3) >= 3, "full-auto: keeps firing while held");
    }
    {
        Design d = base; d.breech.fireMode = "semi"; d.breech.autoFire = false; d.breech.lockTime = 5;
        auto S = sim::build(d); sim::run(*S, 0.003);
        double t0 = S->t;
        S->pressed = true; S->trigger = true;
        sim::run(*S, 0.004);
        bool notYet = S->shots == 0;
        sim::run(*S, 0.003);
        CHECK(notYet && S->shots == 1 && S->fireT - t0 >= 0.0049, "lock time delays the shot");
    }
    {
        Design d = base; d.breech.fireMode = "burst"; d.breech.burst = 4; d.breech.cockAt = 12.5; d.breech.doubleAction = true;
        d.breech.lockTime = 2.5; d.breech.hammer = "hammer";
        Design r;
        bool ok = designFromJson(designToJson(d), r);
        auto& b = r.breech;
        CHECK(ok && b.fireMode == "burst" && b.burst == 4 && b.cockAt == 12.5 && b.doubleAction && b.lockTime == 2.5 && b.hammer == "hammer",
              "fire control settings survive saving");
        std::string old = designToJson(base);
        for (const char* k : {"\"cockAt\"", "\"fireMode\""}) {  // as written by older versions
            size_t at = old.find(k);
            if (at != std::string::npos) old.erase(at, old.find(',', at) - at + 1);
        }
        Design o;
        CHECK(designFromJson(old, o) && o.breech.cockAt == 0 && o.breech.fireMode == "auto", "older blueprints stay always-cocked and keep full-auto");
    }
}

// Magazines on the bench: loading one round at a time, topping up, and tidying away what's been left lying about.
static void benchHousekeeping() {
    std::printf("Bench housekeeping\n");
    {   // a long session: rounds and cases are reused, so the gun keeps feeding however many shots are fired
        sim::ReliabilityOptions o; o.rounds = 160;
        auto R = sim::runReliability(loadPreset("blowback"), o);
        CHECK(R.fired == 160, "fires 160 physical rounds without running out of room");
    }
    auto S = sim::build(loadPreset("blowback"));
    sim::run(*S, 0.01);
    sim::dropMagazine(*S);
    sim::run(*S, 1.0);
    sim::Body* M = nullptr;
    for (auto& b : S->bodies) if (b.isMag && b.loose && !b.gone) M = &b;
    CHECK(M != nullptr, "the dropped magazine lies on the bench");
    if (!M) return;
    int cap = M->d->magCap;
    M->rounds = cap - 2;
    CHECK(sim::fillMagazines(*S) == 2 && M->rounds == cap, "topping up fills it");
    M->rounds = 0;
    V2 lips;
    CHECK(sim::magLoadPoint(*S, *M, lips), "knows where its feed lips are");
    for (int k = 0; k < 3; k++) {  // pick a round up and push it in at the lips
        sim::Body* r = sim::spawnRound(*S, {lips.x * 1000 - 60, lips.y * 1000 - 40});
        if (!r) break;
        S->hand.on = true; S->hand.b = r; S->hand.l = {0, 0}; S->hand.t = {r->x, r->y};
        for (int i = 1; i <= 40; i++) { S->hand.t = {r->x + (lips.x - r->x) * i / 40.0, r->y + (lips.y - r->y) * i / 40.0}; sim::run(*S, 0.002); if (r->gone) break; }
        S->hand.on = false;
    }
    CHECK(M->rounds == 3, "three rounds pushed in one at a time");
    sim::tidy(*S, 15, 10, 30);
    CHECK(!M->gone, "a dropped magazine stays until its time is up");
    S->hand.on = true; S->hand.b = M; S->hand.l = {0, 0}; S->hand.t = {M->x, M->y};
    sim::tidy(*S, 20, 10, 30);
    CHECK(!M->gone, "picking it up starts its time again");
    S->hand.on = false;
    sim::tidy(*S, 31, 10, 30);
    CHECK(M->gone, "a magazine left alone (with rounds in it too) is cleared away");
    {   // at most so many lie out: a new one clears away the oldest, never the one in the hand
        int first = S->spawned;
        sim::Body* held = nullptr;
        for (int k = 0; k < 10; k++) {
            sim::Body* m = sim::spawnMagazine(*S, {80.0 + 45 * k, 60}, false);
            if (k == 0) held = m;
        }
        S->hand.on = true; S->hand.b = held; S->hand.l = {0, 0}; S->hand.t = {held->x, held->y};
        sim::tidy(*S, 0, 10, 30, 6);
        int oldest = 1 << 30; for (auto& b : S->bodies) if (b.isMag && b.loose && !b.gone && &b != held) oldest = std::min(oldest, b.born);
        CHECK(sim::looseMagazines(*S) == 6 && !held->gone && oldest == first + 5, "at most 6 lie out: the oldest went first, the held one stays");
        S->hand.on = false;
        sim::run(*S, 0.05);
        CHECK(sim::seatedMag(*S) || sim::magwell(*S), "(the gun is untouched)");
    }
    // spent cases
    auto P = sim::build(loadPreset("blowback"));
    sim::run(*P, 0.003);
    for (int shot = 0; shot < 3; shot++) {
        P->pressed = true; P->trigger = true; sim::setKey(*P, "fire", true);
        sim::run(*P, 0.06);
        P->pressed = false; P->trigger = false; sim::setKey(*P, "fire", false);
        sim::run(*P, 0.25);
    }
    sim::run(*P, 1.5);
    auto cases = [&] { int n = 0; for (auto& b : P->bodies) if (b.isCase && !b.gone) n++; return n; };
    int before = cases();
    sim::tidy(*P, 5);
    int mid = cases();
    sim::tidy(*P, 6);
    CHECK(before >= 2 && mid == before && cases() == 0, "spent cases are cleared after 10 seconds on the bench");
}

// Magazine springs and lips: a weak spring can't get the next round up in time; the defaults feed cleanly.
static void magazineSprings() {
    std::printf("Magazine springs and lips\n");
    auto burst = [](double spring, double lips) {
        Design d = loadPreset("tilting");
        d.breech.ignition = "button"; d.breech.fireMode = "auto"; d.breech.autoFire = true; d.breech.cockAt = 8;
        for (auto& b : d.bodies) if (b.magazine) { b.magSpring = spring; b.lipHeight = lips; }
        auto S = sim::build(d); sim::run(*S, 0.003);
        S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true);
        sim::run(*S, 1.0);
        return S;
    };
    auto good = burst(1, 0), weak = burst(0.3, 0);
    CHECK(good->shots == 9 && good->malf != sim::Malf::FailureToFeed, "full-auto with a standard spring feeds every round in the magazine");
    CHECK(weak->shots < 4 && weak->malf == sim::Malf::FailureToFeed, "a weak spring can't keep up: failure to feed");
    Design d = loadPreset("tilting");
    for (auto& b : d.bodies) if (b.magazine) { b.magSpring = 0.6; b.lipHeight = -0.5; }
    Design r;
    bool ok = designFromJson(designToJson(d), r);
    bool same = false;
    for (auto& b : r.bodies) if (b.magazine) same = std::fabs(b.magSpring - 0.6) < 1e-9 && std::fabs(b.lipHeight + 0.5) < 1e-9;
    CHECK(ok && same, "spring strength and lip height survive saving");
    // double stack: single position feeds from the centre, double position alternately from each side
    auto doubleStack = [](bool dbl) {
        Design d = loadPreset("tilting");
        for (size_t i = 0; i < d.bodies.size(); i++) if (d.bodies[i].magazine) { d.bodies[i].gen.stack = 2; d.bodies[i].magCap = 15; d.bodies[i].doubleFeed = dbl; generateMagazine(d, (int)i); }
        return d;
    };
    for (int dbl = 0; dbl < 2; dbl++) {
        auto S = sim::build(doubleStack(dbl)); sim::run(*S, 0.003);
        bool left = false, right = false, centre = false;
        int shots = 0;
        for (int k = 0; k < 4; k++) {
            for (auto& b : S->bodies) if (b.isRound && !b.gone && b.rs == sim::RS::Presented) { if (b.zFeed > 0) right = true; else if (b.zFeed < 0) left = true; else centre = true; }
            S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true);
            sim::run(*S, 0.06);
            S->pressed = false; S->trigger = false; sim::setKey(*S, "fire", false);
            sim::run(*S, 0.25);
            shots = S->shots;
        }
        if (dbl) CHECK(left && right && !centre && shots == 4, "double position: rounds come from both sides, and it feeds");
        else CHECK(centre && !left && !right && shots == 4, "single position: every round comes from the centre, and it feeds");
    }
    Design dd = doubleStack(true), rr;
    bool fb = false;
    if (designFromJson(designToJson(dd), rr)) for (auto& b : rr.bodies) if (b.magazine) fb = b.doubleFeed;
    CHECK(fb, "double-position feed survives saving");
}

// Bullet shapes: the drawn shape is the round's real outline, its weight can come from it, and it's saved.
static void bulletShapes() {
    std::printf("Bullet shapes\n");
    Cartridge c = cartridgePresets()[0];   // 9 mm
    auto tipOf = [](const Cartridge& k) { auto s = bulletShape(k); double r = 0; for (auto& p : s) if (p.x >= s.back().x - 1e-6) r = std::max(r, p.y); return r; };
    Cartridge sp = c; sp.nose = "spitzer";
    Cartridge wc = c; wc.nose = "wadcutter";
    CHECK(tipOf(sp) < 0.2 && tipOf(wc) > c.bulletDia * 0.4, "a spitzer ends in a point, a wadcutter in a flat as wide as the bullet");
    Cartridge rn = c; rn.nose = "round";
    double mr = bulletShapeMass(rn), mw = bulletShapeMass(wc);
    CHECK(mr > 5 && mr < 11 && mw > mr, "weight from shape: a 9 mm round nose weighs a believable amount, and a wadcutter more");
    Cartridge hp = c; hp.nose = "hollow";
    CHECK(bulletShapeMass(hp) < mr, "a hollow point's cavity makes it lighter");
    Cartridge bt = sp; bt.tailLen = 3;
    CHECK(ballisticCoefficient(bt) > ballisticCoefficient(sp) && ballisticCoefficient(sp) > ballisticCoefficient(rn) && ballisticCoefficient(rn) > ballisticCoefficient(wc),
          "sleeker shapes keep their speed better: boat-tail spitzer > spitzer > round nose > wadcutter");
    Cartridge cu = c; cu.nose = "custom"; cu.custom = {{0, 4.5}, {9, 4.5}, {12, 2.0}, {12.5, 0}};
    auto sh = bulletShape(cu);
    CHECK(sh.size() == 4 && std::fabs(sh[2].y - 2.0) < 1e-9, "a drawn bullet uses the drawn corners");
    Design d = loadPreset("blowback");
    d.breech.cart = cu; d.breech.cart.nose = "custom"; d.breech.cart.massFromShape = true;
    Design r;
    bool ok = designFromJson(designToJson(d), r);
    CHECK(ok && r.breech.cart.nose == "custom" && r.breech.cart.custom.size() == 4 && r.breech.cart.massFromShape, "a drawn bullet survives saving");
    Design old = loadPreset("blowback");
    old.breech.cart.nose = "auto";
    std::string js = designToJson(old);
    size_t at = js.find("\"nose\":");
    if (at != std::string::npos) js.erase(at, js.find(',', at) - at + 1);
    Design o;
    CHECK(designFromJson(js, o) && o.breech.cart.nose == "auto" && bulletShape(o.breech.cart).empty(), "older blueprints keep the original bullet shape");
}

// The gun the "Build your first gun" tutorial makes: a barrel, a heavy bolt on a track with no spring, an ejector.
// Firing blows the bolt open and throws the case; a round loaded by hand and the bolt pushed shut fires again.
static Design firstGun() {
    Design d = loadPreset("blank");
    auto box = [](double x0, double y0, double x1, double y1) { return std::vector<V2>{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; };
    BodyDef barrel; barrel.id = "barrel"; barrel.fixed = true; barrel.layer = "ABC"; barrel.thick = 16; barrel.pts = box(120, 22, 300, 38);
    BodyDef bolt; bolt.id = "bolt"; bolt.layer = "ABC"; bolt.thick = 14; bolt.mass = 400; bolt.pts = box(60, 20, 120, 33);
    BodyDef ej; ej.id = "ejector"; ej.fixed = true; ej.layer = "ABC"; ej.thick = 4; ej.pts = box(78, 33.6, 82, 38);
    for (auto* b : {&barrel, &bolt, &ej}) { fixShape(*b); d.bodies.push_back(*b); }
    JointDef tr; tr.type = "track"; tr.a = "bolt"; tr.b = "frame"; tr.p = {90, 27}; tr.path = {{40, 27}, {90, 27}}; tr.lockRot = true;
    d.joints.push_back(tr);
    auto& br = d.breech; br = BreechDef(); br.on = true; br.breech = "bolt"; br.chamber = "barrel";
    br.hasFace = true; br.face = {120, 30}; br.hasMuzzle = true; br.muzzle = {300, 30};
    br.feedMode = "single"; br.physicalFeed = true; br.loaded = true;
    return d;
}
static void tutorialGun() {
    std::printf("The tutorial's first gun\n");
    auto S = sim::build(firstGun()); sim::run(*S, 0.05);
    auto* b = S->find("bolt");
    S->pressed = true; S->trigger = true; sim::run(*S, 0.05); S->pressed = false; S->trigger = false; sim::run(*S, 0.3);
    bool caseOut = false;
    for (auto& r : S->bodies) if (r.isCase && !r.gone && r.rs == sim::RS::Loose) caseOut = true;
    CHECK(S->shots == 1 && (b->x - b->x0) < -0.04 && caseOut, "it fires, the bolt blows open and stays open, and the case is thrown clear");
    int fired = 0;
    for (int k = 0; k < 2; k++) {   // load by hand: a round in front of the bolt, then push the bolt shut and fire
        sim::Body* r = sim::spawnRound(*S, {40, 70}); sim::run(*S, 0.1);
        V2 grab = sim::wpt(*r, {0, 0});
        S->hand = {true, r, sim::lpt(*r, grab), grab};
        sim::BoreLine L = sim::boreLine(*S);
        for (int i = 1; i <= 40; i++) { V2 t{L.face.x - L.dir.x * 0.015 - std::cos(r->a) * r->headLx, L.face.y}; S->hand.t = {grab.x + (t.x - grab.x) * i / 40, grab.y + (t.y - grab.y) * i / 40}; sim::run(*S, 0.01); }
        S->hand.on = false; sim::run(*S, 0.2);
        S->hand = {true, b, {0, 0}, {b->x0 + 0.002, b->y0}}; sim::run(*S, 0.4); S->hand.on = false; sim::run(*S, 0.1);
        int before = S->shots;
        S->pressed = true; S->trigger = true; sim::run(*S, 0.06); S->pressed = false; S->trigger = false; sim::run(*S, 0.3);
        fired += S->shots - before;
    }
    CHECK(fired == 2, "loaded by hand and pushed shut, it fires again, twice");
    sim::ReliabilityOptions o; o.rounds = 15;
    auto R = sim::runReliability(firstGun(), o);
    CHECK(R.fired == 15 && R.jams == 0, "the reliability test loads it by hand and pushes the un-sprung bolt shut: 15 of 15");
}

// Magazines of any shape: built in (loaded in place) or removable (swapped along an insertion path you shape),
// with the rounds laid out along a path you draw: side by side (a box) or nose to tail (a tube).
static Design builtInBox(const char* preset) {
    Design d = loadPreset(preset);
    for (auto& b : d.bodies) if (b.magazine) { b.magFixed = true; b.fixed = true; }
    d.joints.erase(std::remove_if(d.joints.begin(), d.joints.end(), [](const JointDef& j) { return j.type == "magwell"; }), d.joints.end());
    return d;
}
static void anyMagazine() {
    std::printf("Built-in and removable magazines\n");
    {   // the blowback pistol with its magazine made part of the gun: an internal box
        Design d = builtInBox("blowback");
        auto S = sim::build(d); sim::run(*S, 0.05);
        sim::Body* M = sim::seatedMag(*S);
        CHECK(M && M->builtInMag && M->rounds == 7 && !S->magTmpl, "a built-in magazine feeds the gun, and there's no spare to swap in");
        int shots = 0;
        for (int k = 0; k < 9; k++) { S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true); sim::run(*S, 0.06); S->pressed = false; S->trigger = false; sim::setKey(*S, "fire", false); sim::run(*S, 0.25); }
        shots = S->shots;
        CHECK(shots == 8 && M->rounds == 0, "it fires the chambered round and all seven from the internal box");
        CHECK(sim::fillMagazines(*S) == 7 && M->rounds == 7, "topping up fills the built-in magazine in place");
        M->rounds = 3;
        V2 lips; bool lp = sim::magLoadPoint(*S, *M, lips);
        S->hand = {true, M, {0, 0}, {M->x, M->y}}; S->hand.on = false;
        sim::Body* r = lp ? sim::spawnRound(*S, {lips.x * 1000 - 40, lips.y * 1000 - 30}) : nullptr;
        if (r) {
            S->hand = {true, r, {0, 0}, {r->x, r->y}};
            for (int i = 1; i <= 40 && !r->gone; i++) { S->hand.t = {r->x + (lips.x - r->x) * i / 40.0, r->y + (lips.y - r->y) * i / 40.0}; sim::run(*S, 0.002); }
            S->hand.on = false;
        }
        CHECK(lp && M->rounds == 4, "a round pushed in at the feed end loads the built-in magazine by hand");
        sim::ReliabilityOptions o; o.rounds = 20;
        auto R = sim::runReliability(d, o);
        CHECK(R.fired == 20 && R.jams == 0 && R.reloads >= 2, "the reliability test refills a built-in magazine in place: 20 rounds, no jams");
    }
    {   // a drawn round path: the top round is presented at its first point
        Design d = builtInBox("blowback");
        V2 lipsAuto;
        { auto S = sim::build(d); sim::run(*S, 0.01); sim::magLoadPoint(*S, *sim::seatedMag(*S), lipsAuto); }
        for (auto& b : d.bodies) if (b.magazine) b.roundPath = {{lipsAuto.x * 1000, lipsAuto.y * 1000}, {lipsAuto.x * 1000, lipsAuto.y * 1000 + 60}};
        auto S = sim::build(d); sim::run(*S, 0.01);
        V2 lp; sim::magLoadPoint(*S, *sim::seatedMag(*S), lp);
        CHECK(std::hypot(lp.x - lipsAuto.x, lp.y - lipsAuto.y) < 0.0005, "a round path drawn from the lips presents the top round there");
        sim::ReliabilityOptions o; o.rounds = 15;
        auto R = sim::runReliability(d, o);
        CHECK(R.fired == 15 && R.jams == 0, "and the gun feeds from it: 15 rounds, no jams");
        Design r2;
        bool ok = designFromJson(designToJson(d), r2);
        bool same = false;
        for (auto& b : r2.bodies) if (b.magazine) same = b.magFixed && b.roundPath.size() == 2 && b.roundStyle == "side";
        CHECK(ok && same, "built-in magazines and their round paths survive saving");
    }
    {   // a tube: rounds nose to tail along the path
        Design d = builtInBox("blowback");
        for (auto& b : d.bodies) if (b.magazine) { b.roundStyle = "line"; b.roundPath = {{20, 60}, {20, 200}}; b.tubeFeed = "simple"; }
        auto S = sim::build(d); sim::run(*S, 0.05);
        for (int k = 0; k < 9; k++) { S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true); sim::run(*S, 0.06); S->pressed = false; S->trigger = false; sim::setKey(*S, "fire", false); sim::run(*S, 0.25); }
        CHECK(S->shots == 8 && sim::seatedMag(*S)->rounds == 0, "a tube magazine set to feed the simple way feeds every round");
    }
    {   // a removable magazine that rocks in: tilted 20 degrees at the mouth of its well
        Design d = loadPreset("blowback");
        for (auto& j : d.joints) if (j.type == "magwell") {
            V2 a = j.path.front(), b = j.path.back();
            j.path = {a, {(a.x + b.x) / 2, (a.y + b.y) / 2}, b};
            j.pathAng = {0, 8, 20};
        }
        auto S = sim::build(d); sim::run(*S, 0.05);
        sim::Body* M = sim::seatedMag(*S);
        double seatedA = M->a;
        S->magRelease = true; sim::run(*S, 0.12);
        double outA = (M->a - seatedA) / sim::D2R;
        S->magRelease = false;
        Design r2; designFromJson(designToJson(d), r2);
        bool kept = false; for (auto& j : r2.joints) if (j.type == "magwell") kept = j.pathAng.size() == 3 && j.pathAng[2] == 20;
        CHECK(outA > 5 && kept, "a rock-in magazine tilts as it leaves along its path, and the tilt is saved");
    }
}

static double polysVolume(const std::vector<csg::Poly>& ps) {
    double v = 0;
    for (auto& p : ps)
        for (size_t i = 1; i + 1 < p.v.size(); i++) v += csg::dot(p.v[0].p, csg::cross(p.v[i].p, p.v[i + 1].p)) / 6;
    return v;
}
static void carving() {
    std::printf("Carving\n");
    {   // the solid geometry: a box with a 4 mm hole drilled through it
        std::vector<csg::Poly> box;
        double X = 10, Y = 5, Z = 4;
        auto q = [&](csg::P3 a, csg::P3 b, csg::P3 c, csg::P3 d) { box.push_back(csg::makePoly({{a, {}}, {b, {}}, {c, {}}, {d, {}}})); };
        q({-X, -Y, -Z}, {-X, Y, -Z}, {X, Y, -Z}, {X, -Y, -Z}); q({-X, -Y, Z}, {X, -Y, Z}, {X, Y, Z}, {-X, Y, Z});
        q({-X, -Y, -Z}, {X, -Y, -Z}, {X, -Y, Z}, {-X, -Y, Z}); q({-X, Y, -Z}, {-X, Y, Z}, {X, Y, Z}, {X, Y, -Z});
        q({-X, -Y, -Z}, {-X, -Y, Z}, {-X, Y, Z}, {-X, Y, -Z}); q({X, -Y, -Z}, {X, Y, -Z}, {X, Y, Z}, {X, -Y, Z});
        BodyDef b; b.thick = 8; b.pts = {{-10, -5}, {10, -5}, {10, 5}, {-10, 5}};
        CarveDef c; c.shape = "circle"; c.at = {0, 0}; c.atZ = 4; c.w = c.h = 4; c.depth = 0;
        double v0 = polysVolume(box), v1 = polysVolume(csg::subtract(box, carve::prismPolys(carve::prism(c, b), c)));
        double hole = 32 * 0.5 * 2 * 2 * std::sin(2 * 3.14159265 / 32) * 8;  // a 32-sided 4 mm hole, 8 mm long
        CHECK(std::fabs(v0 - 1600) < 1e-6 && std::fabs((v0 - v1) - hole) < 0.05, "cutting a hole through a solid takes away exactly the hole's volume");
        c.depth = 3;
        double v2 = polysVolume(csg::subtract(box, carve::prismPolys(carve::prism(c, b), c)));
        CHECK(std::fabs((v0 - v2) - hole * 3 / 8) < 0.05, "a 3 mm deep hole takes away 3 mm of it");
    }
    // a slider on a track runs 40 mm forward past a fixed block sitting at its near side
    auto rig = [](bool groove) {
        Design d;
        BodyDef s; s.id = "slider"; s.layer = "A"; s.thick = 10; s.pts = {{0, 0}, {30, 0}, {30, 10}, {0, 10}};
        s.rad = {0, 0, 0, 0}; s.bulge = {0, 0, 0, 0};
        if (groove) { CarveDef c; c.shape = "rect"; c.at = {15, 5}; c.atZ = 5; c.w = 34; c.h = 6; c.depth = 4; s.carves = {c}; }
        BodyDef e; e.id = "ejector"; e.fixed = true; e.layer = "A"; e.thick = 3; e.z = 3; e.pts = {{48, 3}, {52, 3}, {52, 7}, {48, 7}};
        e.rad = {0, 0, 0, 0}; e.bulge = {0, 0, 0, 0};
        d.bodies = {s, e};
        JointDef t; t.type = "track"; t.a = "slider"; t.b = "world"; t.p = {15, 5}; t.path = {{15, 5}, {75, 5}};
        d.joints = {t};
        return d;
    };
    auto travel = [](const Design& d) {
        auto S = sim::build(d);
        sim::Body* b = S->find("slider");
        S->hand = {true, b, {0, 0}, {0.09, 0.005}};
        sim::run(*S, 0.4);
        return (b->x - b->x0) * 1000;
    };
    double plain = travel(rig(false)), grooved = travel(rig(true));
    CHECK(plain < 20, "a solid slider is stopped by a block in its path");
    CHECK(grooved > 39, "with a groove carved in its side, the block runs in the groove and the slider goes by");
    {
        Design g = rig(true);
        auto S = sim::build(g);
        double m = S->find("slider")->m;  // the part's body carries its welded slices' weight
        CHECK(std::fabs(m * 1e6 / 7.85 - (3000 - 30 * 6 * 4)) < 15, "the groove takes its weight out of the part");
        Design r;
        bool ok = designFromJson(designToJson(g), r);
        CHECK(ok && r.bodies[0].carves.size() == 1 && r.bodies[0].carves[0].shape == "rect" && r.bodies[0].carves[0].depth == 4 && r.bodies[0].carves[0].atZ == 5,
              "carves are saved in the blueprint");
    }
}

static void roundParts() {
    std::printf("Round parts\n");
    BodyDef bar; bar.id = "bar"; bar.fixed = true; bar.layer = "A"; bar.pts = {{0, -8}, {100, -8}, {100, 8}, {0, 8}}; fixShape(bar);
    bar.round = true; carve::syncRound(bar);
    double v = carve::volume(bar), cyl = 3.14159265 * 64 * 100;
    CHECK(std::fabs(bar.thick - 16) < 1e-6, "a round part is as thick as it is wide");
    CHECK(std::fabs(v - cyl) < cyl * 0.03, "a 16 mm round bar has the volume of a cylinder, not a square bar");
    BodyDef step = bar; step.pts = {{0, -8}, {60, -8}, {60, -5}, {100, -5}, {100, 5}, {60, 5}, {60, 8}, {0, 8}}; fixShape(step); carve::syncRound(step);
    double vs = carve::volume(step), want = 3.14159265 * (64 * 60 + 25 * 40);
    CHECK(std::fabs(vs - want) < want * 0.04, "a stepped outline turns into a barrel with a shoulder");
    // a block dropped straight down onto the bar, out near its side (7.5 mm from the middle, the bar is 8 in radius):
    // a square bar stops it at its top face, a round one lets it down much further
    auto rig = [&](bool round) {
        Design d;
        BodyDef b = bar; b.round = round; if (!round) b.thick = 16;
        BodyDef k; k.id = "block"; k.layer = "A"; k.thick = 1; k.z = 7.5; k.pts = {{40, -20}, {60, -20}, {60, -15}, {40, -15}}; fixShape(k);
        d.bodies = {b, k};
        JointDef t; t.type = "track"; t.a = "block"; t.b = "world"; t.p = {50, -17.5}; t.path = {{50, -17.5}, {50, 10}};
        d.joints = {t};
        auto S = sim::build(d);
        sim::Body* m = S->find("block");
        m->vy = 0.5;  // thrown down at it
        for (int i = 0; i < 10; i++) sim::run(*S, 0.05);
        return -15 + (m->y - m->y0) * 1000;  // where the block's bottom ended up (mm, y down)
    };
    double sq = rig(false), rd = rig(true);
    CHECK(std::fabs(sq - -8) < 0.3, "dropped onto a square bar, a block stops on its flat top");
    CHECK(rd > -4.2 && rd < -2.8, "on a round bar, out near its side, it goes down to where the curve really is");
    {   // turned down to flats: square, octagon; turned; and a wedge
        BodyDef q = bar; q.roundSides = 4; carve::syncRound(q);
        double vq = carve::volume(q);
        CHECK(std::fabs(q.thick - 16) < 0.01 && std::fabs(vq - 16 * 16 * 100) < 16 * 16 * 100 * 0.01, "4 sides is the square bar as drawn");
        BodyDef o8 = bar; o8.roundSides = 8; carve::syncRound(o8);
        double v8 = carve::volume(o8), want8 = 8 * 64 * std::tan(3.14159265 / 8) * 100;  // flats 8 mm from the axis
        CHECK(std::fabs(v8 - want8) < want8 * 0.02, "8 sides is an octagon with its flats on the drawn outline");
        BodyDef d45 = q; d45.roundTurn = 45; carve::syncRound(d45);
        CHECK(std::fabs(d45.thick - 16 * std::sqrt(2.0)) < 0.05 && std::fabs(carve::volume(d45) - vq) < vq * 0.02,
              "a square turned 45 degrees is a diamond: as much metal, wider across");
        BodyDef w = bar; w.roundSweep = 60; carve::syncRound(w);
        double vw = carve::volume(w);
        CHECK(std::fabs(vw - cyl / 6) < cyl / 6 * 0.06, "a 60 degree sweep is a wedge, a sixth of the bar");
        auto up = carve::roundSection(w, 0.0);  // pointing up: at the middle depth it fills only above the axis
        double ymax = -1e9;
        for (auto& pl : up) for (auto& p : pl) ymax = std::max(ymax, p.y);
        CHECK(!up.empty() && ymax < 0.05, "a wedge turned to 0 points up");
        Design rt; BodyDef w2 = w; w2.roundSides = 6; w2.roundTurn = 30; rt.bodies = {w2};
        Design rb;
        CHECK(designFromJson(designToJson(rt), rb) && rb.bodies[0].roundSides == 6 && rb.bodies[0].roundTurn == 30 && rb.bodies[0].roundSweep == 60,
              "sides, turn and sweep are saved");
    }
    {
        Design pd; pd.bodies = {bar};
        JointDef pj; pj.type = "pin"; pj.a = "bar"; pj.p = {10, 0}; pj.pinDia = 5.5; pd.joints = {pj};
        Design pb;
        CHECK(designFromJson(designToJson(pd), pb) && pb.joints[0].pinDia == 5.5, "a pin's size is saved");
        Design old; std::string js = designToJson(pd); js.replace(js.find("\"dia\""), 5, "\"xxx\"");
        CHECK(designFromJson(js, old) && old.joints[0].pinDia == 3.2, "older pins keep the size they always had");
    }
    {
        Design td; BodyDef tb = bar; tb.texture = "walnut.jpg"; tb.texScale = 60; td.bodies = {tb};
        Design tr;
        CHECK(designFromJson(designToJson(td), tr) && tr.bodies[0].texture == "walnut.jpg" && tr.bodies[0].texScale == 60, "a part's texture is saved");
    }
    Design dd; dd.bodies = {bar};
    Design back;
    CHECK(designFromJson(designToJson(dd), back) && back.bodies[0].round, "round parts are saved in the blueprint");
}

static void welding() {
    std::printf("Welding\n");
    {
        bool noJoints = true, tagged = false;
        for (auto& p : presetList()) {
            Design d = loadPreset(p.key);
            for (auto& j : d.joints) if (j.type == "weld") noJoints = false;
            for (auto& b : d.bodies) if (!b.weld.empty()) tagged = true;
        }
        CHECK(noJoints && tagged, "the examples' welds load as welded pieces, with no weld joints left");
    }
    BodyDef a; a.id = "a"; a.name = "A"; a.layer = "A"; a.pts = {{0, 0}, {20, 0}, {20, 10}, {0, 10}}; fixShape(a);
    BodyDef b = a; b.id = "b"; b.name = "B"; b.pts = {{20, 0}, {30, 0}, {30, 30}, {20, 30}}; fixShape(b);
    BodyDef c = a; c.id = "c"; c.name = "C"; c.pts = {{40, 0}, {50, 0}, {50, 10}, {40, 10}}; fixShape(c);
    Design d; d.bodies = {a, b, c};
    {   // an old blueprint with weld joints
        Design old = d;
        JointDef w; w.type = "weld"; w.a = "a"; w.b = "b"; w.p = {20, 5}; old.joints = {w};
        JointDef g; g.type = "weld"; g.a = "c"; g.b = "world"; g.p = {45, 5}; old.joints.push_back(g);
        Design back;
        CHECK(designFromJson(designToJson(old), back) && back.joints.empty() && !back.bodies[0].weld.empty() &&
              back.bodies[0].weld == back.bodies[1].weld && back.bodies[2].fixed,
              "older blueprints: weld joints become welded pieces, and a part welded to the ground is fixed");
    }
    weldParts(d, "a", "b");
    CHECK(weldedWith(d, 0).size() == 2 && weldedWith(d, 2).size() == 1, "welding two parts makes one piece");
    {   // pushed on one, the other comes along exactly
        auto S = sim::build(d);
        sim::Body *A = S->find("a"), *B = S->find("b");
        S->hand = {true, A, {0, 0}, {A->x + 0.02, A->y + 0.01}};
        sim::run(*S, 0.2);
        double da = std::hypot(A->x - A->x0, A->y - A->y0);
        double gap0 = std::hypot(A->x0 - B->x0, A->y0 - B->y0), gap = std::hypot(A->x - B->x, A->y - B->y);
        CHECK(da > 0.005 && std::fabs(gap - gap0) < 1e-6 && std::fabs(A->a - B->a) < 1e-9, "welded parts move as one piece: they stay locked together");
    }
    Design back;
    CHECK(designFromJson(designToJson(d), back) && back.bodies[0].weld == back.bodies[1].weld && !back.bodies[0].weld.empty(), "welds are saved");
    weldParts(d, "c", "b");
    CHECK(weldedWith(d, 2).size() == 3, "welding onto a welded piece adds to it");
    unweld(d, "c");
    CHECK(weldedWith(d, 2).size() == 1 && weldedWith(d, 0).size() == 2, "unwelding takes just that part out");
    unweld(d, "a");
    CHECK(d.bodies[1].weld.empty(), "the last part left on its own isn't welded to anything");
}

static void tubeFeeding() {
    std::printf("Tube feeding (the pump-action example)\n");
    Design d = loadPreset("pump");
    {
        auto S = sim::build(d); sim::run(*S, 0.3);
        sim::Body* M = sim::seatedMag(*S);
        bool held = false;
        for (auto& b : S->bodies) if (b.isRound && !b.gone && b.rs == sim::RS::Tube) held = sim::wpt(b, {b.headLx, 0}).x * 1000 > 100;
        CHECK(M && M->rounds == 6 && held, "closed, the lifter is up and holds the next round in the tube's mouth");
        S->pressed = true; sim::run(*S, 0.05); S->pressed = false; sim::run(*S, 0.1);
        sim::Body* B = S->find("bolt");
        CHECK(S->shots == 1 && std::fabs(B->x - B->x0) < 0.0005, "the action lock holds the bolt shut through the shot");
        sim::Body* F = S->find("forend");
        S->hand = {true, F, {0, 0}, {F->x, F->y}};
        for (int k = 1; k <= 40; k++) { S->hand.t = {F->x0 - 0.095 * k / 40, F->y0}; sim::run(*S, 0.0025); }
        sim::run(*S, 0.08);
        bool out = false;
        for (auto& b : S->bodies) if (b.isRound && !b.isCase && !b.gone && b.rs == sim::RS::Free) {
            V2 h = sim::wpt(b, {b.headLx, 0});
            out = std::fabs(h.x * 1000 - 70) < 1.5;
        }
        CHECK(out && M->rounds == 5, "pulled all the way back, the lifter drops and the spring pushes a real round out onto it");
        for (int k = 1; k <= 40; k++) { S->hand.t = {F->x0 - 0.095 + 0.1 * k / 40, F->y0}; sim::run(*S, 0.0025); }
        sim::run(*S, 0.1);
        CHECK(S->chambered, "pushing the fore-end home lifts the round and the bolt chambers it");
    }
    {
        sim::ReliabilityOptions o; o.rounds = 30;
        auto R = sim::runReliability(d, o);
        CHECK(R.fired == 30 && R.jams == 0, "the pump fires 30 rounds from its tube without a jam (worked by its fore-end)");
        Design weak = d;
        for (auto& b : weak.bodies) if (b.magazine) b.magSpring = 0.01;
        auto W = sim::runReliability(weak, o);
        CHECK(W.byType[(int)sim::Malf::FailureToFeed] > 0, "a tube spring far too weak can't get the round out in time: failure to feed");
    }
    {   // an older blueprint's tube (no tubeFeed saved) keeps feeding the simple way
        Design t = builtInBox("blowback");
        for (auto& b : t.bodies) if (b.magazine) { b.roundStyle = "line"; b.roundPath = {{20, 60}, {20, 200}}; }
        std::string js = designToJson(t);
        size_t at = js.find("\"tubeFeed\"");
        if (at != std::string::npos) js.replace(at, 10, "\"tubeXeed\"");
        Design back;
        bool ok = designFromJson(js, back);
        bool simple = false;
        for (auto& b : back.bodies) if (b.magazine) simple = b.tubeFeed == "simple";
        CHECK(ok && simple, "older blueprints' tubes keep the simple feeding");
    }
}

static void ammoKinds() {
    std::printf("Kinds of ammunition: shotshells, rimfire, caseless, several projectiles\n");
    {   // an older cartridge (no kind saved) is centrefire with one bullet, and recoils exactly as before
        Design d = loadPreset("blowback");
        std::string js = designToJson(d);
        for (const char* k : {"\"kind\"", "\"projectiles\""}) { size_t at = js.find(k); if (at != std::string::npos) js[at + 1] = 'x'; }
        Design back;
        bool ok = designFromJson(js, back);
        const Cartridge& c = back.breech.cart;
        CHECK(ok && c.kind == "centerfire" && c.projectiles == 1 && std::fabs(c.impulse() - c.bulletMass / 1000 * c.velocity * 1.35) < 1e-9,
              "older cartridges load as centrefire with one bullet, recoil unchanged");
    }
    const Cartridge* buck = nullptr; const Cartridge* nine = nullptr;
    for (auto& c : cartridgePresets()) { if (c.name == "12 gauge 00 buck") buck = &c; if (c.name == "9x19mm") nine = &c; }
    CHECK(buck && std::fabs(buck->pelletMass() - 3.49) < 0.1 && std::fabs(buck->payloadMass() - 31.4) < 1, "nine 8.4 mm lead pellets: 3.5 g each, 31 g of shot");
    CHECK(buck && nine && buck->impulse() > 3 * nine->impulse() && cartridgeMass(*buck) > 38 && cartridgeMass(*buck) < 50,
          "a buckshot shell kicks over three times as hard as a 9 mm, and weighs about 45 g");
    {
        Cartridge two = *nine; two.projectiles = 2;
        CHECK(std::fabs(two.energy() - 2 * nine->energy()) < 1e-9, "two bullets carry twice the energy");
        Cartridge rt;
        Design d = loadPreset("blowback"); d.breech.cart = *buck;
        Design back; designFromJson(designToJson(d), back);
        rt = back.breech.cart;
        CHECK(rt.kind == "shotshell" && rt.projectiles == 9 && std::fabs(rt.shotDia - 8.38) < 1e-9 && rt.hullColor == buck->hullColor, "a shell saves and loads");
        auto o = cartridgeOutline(*buck);
        double front = 0; for (auto& p : o) front = std::max(front, p.x);
        CHECK(std::fabs(front - buck->caseLen) < 1e-6, "a shell is crimped shut: nothing stands out of the hull");
    }
    {   // the pump shotgun: the carbine's action redrawn for 12 gauge
        Design d = loadPreset("pumpshotgun");
        auto S = sim::build(d); sim::run(*S, 0.3);
        S->pressed = true; sim::run(*S, 0.05); S->pressed = false; sim::run(*S, 0.1);
        sim::Body* B = S->find("bolt");
        CHECK(S->shots == 1 && std::fabs(B->x - B->x0) < 0.0005, "the action lock holds the bolt shut through a 12 gauge shot");
        sim::ReliabilityOptions o; o.rounds = 30;
        auto R = sim::runReliability(d, o);
        CHECK(R.fired == 30 && R.jams == 0, "the pump shotgun fires 30 shells from its tube without a jam");
    }
    {   // caseless: nothing to extract, and the next round goes straight in
        Design d = loadPreset("blowback");
        d.breech.cart.kind = "caseless";
        auto S = sim::build(d); sim::run(*S, 0.3);
        sim::setKey(*S, "fire", true); S->pressed = true; S->trigger = true; sim::run(*S, 0.08);
        sim::setKey(*S, "fire", false); S->pressed = false; S->trigger = false; sim::run(*S, 0.4);
        int cases = 0;
        for (auto& b : S->bodies) if (b.isCase && !b.gone) cases++;
        CHECK(S->shots == 1 && cases == 0 && S->chambered && S->malf == sim::Malf::None, "a caseless round leaves nothing to eject; the next one chambers");
    }
    {   // rimfire: a firing pin on the bore line hits the middle of the head, where there's no priming
        Design d = loadPreset("blowback");
        auto S0 = sim::build(d);
        if (S0->strikerIgnition) {
            d.breech.cart.kind = "rimfire";
            auto S = sim::build(d); sim::run(*S, 0.3);
            auto pull = [](sim::Sim& T) { sim::setKey(T, "fire", true); T.pressed = T.trigger = true; sim::run(T, 0.08); sim::setKey(T, "fire", false); T.pressed = T.trigger = false; sim::run(T, 0.2); };
            auto C = sim::build(loadPreset("blowback")); sim::run(*C, 0.3); pull(*C);
            pull(*S);
            CHECK(C->shots == 1 && S->shots == 0, "a rimfire round in a centre-striking gun doesn't fire");
        } else CHECK(true, "(the blowback example has no drawn striker: rimfire strike not checked)");
    }
}

static void severalChambers() {
    std::printf("Several chambers (the over-under derringer)\n");
    Design d = loadPreset("derringer");
    auto pull = [](sim::Sim& T) { sim::setKey(T, "fire", true); T.pressed = T.trigger = true; sim::run(T, 0.06); sim::setKey(T, "fire", false); T.pressed = T.trigger = false; sim::run(T, 0.1); };
    {
        auto S = sim::build(d); sim::run(*S, 0.3);
        CHECK(sim::chamberCount(*S) == 2 && sim::chamberedCount(*S) == 2, "two chambers, both loaded");
        sim::Body* B = S->find("block");
        CHECK(std::fabs(B->a) < 0.01, "gravity holds the barrels shut against the standing breech");
        pull(*S);
        int afterOne = S->shots, left = sim::chamberedCount(*S);
        pull(*S);
        int afterTwo = S->shots;
        int clicks = 0;
        for (auto& e : S->events) if (e.kind == sim::Ev::Click) clicks++;
        pull(*S);
        for (auto& e : S->events) if (e.kind == sim::Ev::Click) clicks--;
        CHECK(afterOne == 1 && left == 1 && afterTwo == 2 && S->shots == 2 && clicks < 0, "fires the top barrel, then the bottom one, then clicks");
    }
    {
        Design v = d; v.breech.fireOrder = "together";
        auto S = sim::build(v); sim::run(*S, 0.3);
        pull(*S);
        CHECK(S->shots == 2 && sim::chamberedCount(*S) == 0, "set to fire all at once, one pull fires both (a volley)");
    }
    {
        Design back;
        bool ok = designFromJson(designToJson(d), back);
        CHECK(ok && back.breech.more.size() == 1 && std::fabs(back.breech.more[0].face.y - d.breech.more[0].face.y) < 1e-9 && back.breech.fireOrder == "turn",
              "the further chambers save and load");
        Design old = loadPreset("boltaction");
        auto S = sim::build(old);
        CHECK(old.breech.more.empty() && sim::chamberCount(*S) == 1, "older blueprints have one chamber, as before");
    }
    {
        sim::ReliabilityOptions o; o.rounds = 30;
        auto R = sim::runReliability(d, o);
        CHECK(R.fired == 30 && R.jams == 0, "30 rounds: tipped open, the empties drop out, both reloaded, no jams");
    }
}

static void beltsAndClips() {
    std::printf("Belts and clips\n");
    Design d = loadPreset("belted");
    {
        sim::ReliabilityOptions o; o.rounds = 30;
        auto R = sim::runReliability(d, o);
        CHECK(R.fired == 30 && R.jams == 0, "the belt-fed blowback: its slide moves the belt on, 30 rounds without a jam");
        Design far = d;
        for (auto& b : far.bodies) if (b.magazine) b.pawlStroke = 36;   // further than the slide can travel
        auto F = sim::runReliability(far, o);
        CHECK(F.byType[(int)sim::Malf::FailureToFeed] > 0 && F.fired < 5, "a pawl that can't reach the next link never moves the belt: failure to feed");
    }
    {
        auto S = sim::build(d); sim::run(*S, 0.3);
        sim::Body* M = sim::seatedMag(*S);
        bool trayFull = M && M->top;
        S->hand = {true, S->find("slide"), {0, 0}, {S->find("slide")->x - 0.012, S->find("slide")->y}};   // a short pull: not far enough
        sim::run(*S, 0.15); S->hand.on = false; sim::run(*S, 0.2);
        CHECK(trayFull && M->rounds == 50, "a round waits in the tray; a short pull doesn't move the belt");
        Design back; designFromJson(designToJson(d), back);
        bool kept = false;
        for (auto& b : back.bodies) if (b.magazine) kept = b.feedPawl == "slide" && std::fabs(b.pawlStroke - 26) < 1e-9;
        CHECK(kept, "the belt's pawl saves and loads");
    }
    {   // clips, on the bolt rifle's magazine
        Design r = loadPreset("boltaction");
        for (auto& b : r.bodies) if (b.magazine) { b.magFixed = true; b.clip = "enbloc"; }
        auto S = sim::build(r); sim::run(*S, 0.1);
        sim::Body* M = nullptr;
        for (auto& b : S->bodies) if ((b.isMag || b.builtInMag) && !b.gone) M = &b;
        bool ok = M != nullptr;
        if (ok) { M->rounds = 2; int added = sim::fillMagazines(*S); ok = added == 0 && M->rounds == 2; }
        if (ok) { M->rounds = 0; int added = sim::fillMagazines(*S); ok = added == M->d->magCap; }
        CHECK(ok, "an en-bloc clip can't be topped up: it only reloads once empty, and then with a full clip");
        Design st = loadPreset("boltaction");
        for (auto& b : st.bodies) if (b.magazine) { b.magFixed = true; b.clip = "stripper"; b.clipSize = 2; }
        auto T = sim::build(st); sim::run(*T, 0.1);
        sim::Body* N = nullptr;
        for (auto& b : T->bodies) if ((b.isMag || b.builtInMag) && !b.gone) N = &b;
        bool ok2 = N != nullptr;
        if (ok2) { N->rounds = 0; sim::fillMagazines(*T); ok2 = N->rounds == 2; sim::fillMagazines(*T); ok2 = ok2 && N->rounds == 4; }
        CHECK(ok2, "stripper clips go in one clip at a time");
    }
}

static void grips() {
    std::printf("How it's held (the pose)\n");
    Design p = loadPreset("blowback"), r = loadPreset("boltaction");
    CHECK(heldWith(p) == "one hand" && heldWith(r) == "two hands" && r.pose.butt.on, "the pistol is held with one hand, the rifle with two and against the shoulder");
    Design back; designFromJson(designToJson(r), back);
    CHECK(back.pose.firing.on && back.pose.support.on && back.pose.butt.on && std::fabs(back.pose.firing.angle - 105) < 1e-9 && back.pose.firing.part == "stock",
          "grip markers from earlier versions become the pose's hands and butt, and save as a pose");
    r.pose.hold[HOLD_HIP].set = true; r.pose.hold[HOLD_HIP].eye = {12, -340}; r.pose.hold[HOLD_HIP].yaw = 20;
    designFromJson(designToJson(r), back);
    CHECK(back.pose.hold[HOLD_HIP].set && std::fabs(back.pose.hold[HOLD_HIP].yaw - 20) < 1e-9 && !back.pose.hold[HOLD_AIM].set, "a posed hold saves; the others stay worked out from the gun");
    BodyHold aim = holdOf(r, HOLD_AIM), low = holdOf(r, HOLD_LOWERED);
    CHECK(std::fabs(aim.eye.x - (r.pose.butt.at.x + 90)) < 1e-6 && low.tilt > 20, "unposed: the eye sits over the stock when aiming, and the muzzle drops when lowered");
    {   // the hand skeleton: named bones, and finger poses that save
        DigitPose dg[DIGIT_COUNT];
        digitsOf(r.pose.firing, "firing", dg);
        auto bones = handBones(dg);
        int named = 0; for (auto& b : bones) if (b.name == "index_03" || b.name == "thumb_01" || b.name == "pinky_02") named++;
        Design d2 = r;
        applyHandPreset(d2.pose.support, "support", "fist");
        d2.pose.support.digits[DIGIT_RING].bend[1] = 33;
        Design b2; designFromJson(designToJson(d2), b2);
        auto open = handBones([]{ static DigitPose z[DIGIT_COUNT]; return z; }());
        double tipOpen = 0, tipFist = 0;
        for (auto& b : open) if (b.name == "middle_03") tipOpen = b.b.x;
        DigitPose fist[DIGIT_COUNT]; digitsOf(b2.pose.support, "support", fist);
        for (auto& b : handBones(fist)) if (b.name == "middle_03") tipFist = b.b.x;
        CHECK(bones.size() == 19 && named == 3 && b2.pose.support.digitsSet && std::fabs(b2.pose.support.digits[DIGIT_RING].bend[1] - 33) < 1e-9 && tipFist < tipOpen - 60,
              "a hand has 19 named bones; bent fingers curl toward the palm; finger poses save and load");
    }
    Design none = p; none.pose = Pose();
    CHECK(heldWith(none).empty(), "a design with no firing hand has nowhere to hold it");
}

static void barrelsAndTilt() {
    std::printf("Barrels and tilted parts\n");
    Design d = loadPreset("single");
    const Cartridge* r556 = nullptr; const Cartridge* r22 = nullptr;
    for (auto& c : cartridgePresets()) { if (c.name == "5.56x45mm") r556 = &c; if (c.name == ".22 LR") r22 = &c; }
    V2 face0 = d.breech.face, muz0 = d.breech.muzzle;
    for (auto& b : d.bodies) if (b.id == "barrel") { b.barrel.on = true; b.barrel.cart = *r556; b.barrel.muzzleEnd = 1; }
    syncBarrels(d);
    CHECK(std::fabs(d.breech.face.x - face0.x) < 0.5 && std::fabs(d.breech.face.y - face0.y) < 0.5 && std::fabs(d.breech.muzzle.x - muz0.x) < 0.5 && d.breech.chamber == "barrel",
          "a barrel part sets the chamber mouth and muzzle from its own ends");
    {
        sim::ReliabilityOptions o; o.rounds = 20;
        auto R = sim::runReliability(d, o);
        CHECK(R.fired == 20 && R.jams == 0, "the single-shot rifle, its chamber taken from a barrel part: 20 rounds, no jams");
    }
    {   // a second barrel, for another cartridge
        Design t = d;
        BodyDef b2;
        for (auto& b : t.bodies) if (b.id == "barrel") b2 = b;
        b2.id = "barrel2"; b2.name = "Barrel 2"; b2.barrel.cart = *r22;
        for (auto& p : b2.pts) p.y += 40;
        t.bodies.push_back(b2);
        t.breech.fireOrder = "together";
        t.breech.ignition = "button"; t.breech.cockAt = 0;   // the built-in hammer (one drawn firing pin only reaches one primer)
        syncBarrels(t);
        auto S = sim::build(t);
        bool own = t.breech.more.size() == 1 && t.breech.more[0].ownCart && S->chambers.size() == 2 && S->chambers[1].cart.name == ".22 LR" && S->design.breech.cart.name == "5.56x45mm";
        double i1 = S->chambers[1].impulse, i0 = S->design.breech.impulse;
        sim::run(*S, 0.2);
        sim::setKey(*S, "fire", true); S->pressed = S->trigger = true; sim::run(*S, 0.08); sim::setKey(*S, "fire", false); S->pressed = S->trigger = false; sim::run(*S, 0.1);
        CHECK(own && i1 < i0 * 0.5 && S->shots == 2, "each barrel is cut for its own cartridge, with its own recoil; fired together, both go off");
        Design back; designFromJson(designToJson(t), back);
        int bn = 0; for (auto& b : back.bodies) if (b.barrel.on) bn++;
        CHECK(bn == 2 && back.breech.more.size() == 1 && back.breech.more[0].cart.name == ".22 LR", "barrels and their cartridges save and load");
    }
    {   // tilted out of the side view: the physics slices the tilted shape
        BodyDef p; p.id = "plate"; p.pts = {{0, 0}, {60, 0}, {60, 20}, {0, 20}}; p.thick = 6; p.layer = "ABC";
        BodyDef q = p; q.tiltY = 40;
        double z0, z1; carve::tiltDepth(q, z0, z1);
        auto sl = carve::slices(q);
        double area0 = 0; for (auto& c : sl) for (auto& o : c.outlines) { double a, J; V2 cen; carve::material(o, {}, a, cen, J); area0 += a * (c.z1 - c.z0); }
        CHECK(carve::slices(p).empty() && sl.size() >= 3 && z1 - z0 > 40 && std::fabs(area0 - 60 * 20 * 6) < 0.12 * 60 * 20 * 6,
              "a plate swung 40 degrees spans much more depth, sliced into pieces that keep its volume");
    }
}

static void shapeTools() {
    std::printf("shapeTools\n");
    auto area = [](const std::vector<V2>& p) { double a = 0; for (size_t i = 0; i < p.size(); i++) { auto& u = p[i]; auto& v = p[(i + 1) % p.size()]; a += u.x * v.y - v.x * u.y; } return std::fabs(a / 2); };
    BodyDef a, b; a.pts = {{0, 0}, {100, 0}, {100, 40}, {0, 40}}; b.pts = {{60, 20}, {160, 20}, {160, 60}, {60, 60}};
    std::vector<V2> o;
    CHECK(combineOutlines(a, b, 0, o) && std::fabs(area(o) - 7200) < 60 && o.size() <= (size_t)MAX_CORNERS, "merge covers both (7200 mm2)");
    CHECK(combineOutlines(a, b, 1, o) && std::fabs(area(o) - 800) < 30, "keep overlap leaves 800 mm2");
    CHECK(combineOutlines(a, b, 2, o) && std::fabs(area(o) - 3200) < 40, "cut away leaves 3200 mm2");
    BodyDef hole; hole.pts = {{40, 10}, {60, 10}, {60, 30}, {40, 30}};
    std::string why;
    CHECK(!combineOutlines(a, hole, 2, o, &why) && why.find("Carve") != std::string::npos, "a cut that would only leave a hole points to the Carve tool");
    BodyDef abut; abut.pts = {{100, 0}, {150, 0}, {150, 40}, {100, 40}};
    CHECK(combineOutlines(a, abut, 0, o) && std::fabs(area(o) - 6000) < 60, "parts edge to edge merge");
    BodyDef c; c.pts = {{300, 0}, {320, 0}, {320, 20}};
    CHECK(!combineOutlines(a, c, 0, o), "parts that don't touch won't merge");
    BodyDef g; g.pts = circlePts({0, 0}, 30, &g.bulge); fixShape(g);   // a round part and a bar through it
    BodyDef bar; bar.pts = {{-80, -5}, {80, -5}, {80, 5}, {-80, 5}};
    CHECK(combineOutlines(g, bar, 0, o) && o.size() <= (size_t)MAX_CORNERS && area(o) > 2827 + 900, "a disc and a bar merge within the corner limit");
    BodyDef ch = a;
    CHECK(chamferCorners(ch, 5) == 4 && ch.pts.size() == 8 && std::fabs(area(ch.pts) - (4000 - 4 * 12.5)) < 0.01, "chamfering takes 12.5 mm2 off each corner");
    BodyDef one = a;
    CHECK(chamferCorners(one, 5, {2}) == 1 && one.pts.size() == 5, "only the selected corner is chamfered");
}
static void liveMirrors() {
    std::printf("liveMirrors\n");
    Design d;
    BodyDef a; a.id = "a"; a.name = "Grip panel"; a.pts = {{0, 0}, {30, 0}, {30, 60}, {0, 60}}; a.z = 8; a.thick = 3; fixShape(a);
    CarveDef c; c.at = {15, 30}; c.atZ = 1.5; c.dz = -1; a.carves.push_back(c);
    a.mirror = "m1"; a.mirrorKind = "depth";
    BodyDef b = mirrorImage(a); b.id = "b"; b.name = "Grip panel (other side)";
    d.bodies = {a, b};
    CHECK(std::fabs(d.bodies[1].z + 8) < 1e-9 && std::fabs(d.bodies[1].carves[0].atZ + 1.5) < 1e-9, "the other side sits across the gun, its cut from the far face");
    CHECK(mirrorPartner(d, 0) == 1 && mirrorPartner(d, 1) == 0, "each finds its partner");
    Design prev = d;
    d.bodies[0].pts[2] = {40, 70}; d.bodies[0].thick = 4;
    CHECK(syncMirrors(d, &prev) == 1 && std::fabs(d.bodies[1].pts[2].x - 40) < 1e-9 && std::fabs(d.bodies[1].thick - 4) < 1e-9, "changing one side changes the other");
    prev = d;
    d.bodies[1].pts[0] = {-5, -5};
    CHECK(syncMirrors(d, &prev) == 1 && std::fabs(d.bodies[0].pts[0].x + 5) < 1e-9 && std::fabs(d.bodies[0].z - 8) < 1e-9, "and the other way round");
    prev = d;
    d.bodies[1].fixed = true;
    CHECK(syncMirrors(d, &prev) == 0 && !d.bodies[0].fixed, "how a part moves isn't mirrored");
    Design back; designFromJson(designToJson(d), back);
    CHECK(mirrorPartner(back, 0) == 1 && back.bodies[0].mirrorKind == "depth", "the link is saved");
    // across a line in the side view
    Design e;
    BodyDef l; l.id = "l"; l.pts = {{10, 0}, {20, 0}, {20, 10}}; fixShape(l);
    l.mirror = "m2"; l.mirrorKind = "line"; l.mirrorA = {0, 0}; l.mirrorB = {0, 1};
    BodyDef r = mirrorImage(l); r.id = "r";
    e.bodies = {l, r};
    CHECK(std::fabs(e.bodies[1].pts[0].x + 20) < 1e-9, "a line mirror reflects across the line");
    e.bodies[0].pts[1] = {25, 0};
    CHECK(syncMirrors(e, nullptr, 0) == 1 && std::fabs(e.bodies[1].pts[1].x + 25) < 1e-9 && std::fabs(e.bodies[1].pts[0].x + 20) < 1e-9, "and follows the selected side");
    Design old; designFromJson(designToJson(loadPreset("blowback")), old);
    bool none = true; for (auto& x : old.bodies) none &= x.mirror.empty();
    CHECK(none, "older designs have no mirrors");
}
static void springBuckling() {
    std::printf("springBuckling\n");
    CHECK(bucklingSqueeze(16, 4, 0.6) > 1e6, "a spring short for its width never buckles");
    double mid = bucklingSqueeze(60, 10.6, 0.6), slim = bucklingSqueeze(100, 10.6, 0.6);   // 6 and 10 coil widths long
    CHECK(mid > 0.35 * 60 && mid < 0.5 * 60, "six coil widths long: it takes a good squeeze to buckle it (about 40%)");
    CHECK(slim > 0.08 * 100 && slim < 0.16 * 100, "ten coil widths long: it buckles early (about 12%)");
    // a long recoil spring with its guide rod taken out: it buckles, but keeps pushing and the gun still works
    Design d = loadPreset("blowback");
    for (auto& j : d.joints) if (j.type == "spring" && j.guide == "rod") j.guide.clear();
    auto S = sim::build(d); sim::run(*S, 0.3);
    bool buck = false; double F = 0, bow = 0;
    for (auto& J : S->joints) if (J.j && J.j->type == "spring" && J.j->a == "nose") { buck = J.buckled; F = J.force; bow = J.bow; }
    CHECK(buck && F > 5 && bow > 0 && bow <= 0.014 + 1e-9, "unguided, it bows out (no more than two coil widths) and still pushes");
}
static void magazinePositions() {
    std::printf("magazinePositions\n");
    // the blowback pistol's magazine turned to stick out of the left side: rounds come in sideways
    Design side = loadPreset("blowback");
    int mi = -1;
    for (int i = 0; i < (int)side.bodies.size(); i++) if (side.bodies[i].magazine && side.bodies[i].gen.on) mi = i;
    side.bodies[mi].gen.side = -1;
    generateMagazine(side, mi);
    CHECK(side.bodies[mi].z < -side.bodies[mi].thick / 2 && side.bodies[mi].thick > 40, "a side magazine sticks out across the gun");
    Design back; designFromJson(designToJson(side), back);
    CHECK(back.bodies[mi].gen.side == -1, "its side is saved");
    sim::ReliabilityOptions o; o.rounds = 20;
    auto R = sim::runReliability(side, o);
    CHECK(R.fired == 20 && R.jams == 0 && R.reloads >= 2, "fed from the side, it fires 20 of 20, reloading sideways");
    // the whole pistol upside down: its magazine on top, feeding down; the round still points at the muzzle
    Design up = loadPreset("blowback"), ref = up;
    double c = up.breech.face.y;
    auto fy = [&](V2& p) { p.y = 2 * c - p.y; };
    for (auto& b : up.bodies) { mirrorBody(b, {0, c}, {1, c}); for (auto& q : b.roundPath) fy(q); }
    for (auto& j : up.joints) { fy(j.p); fy(j.pa); fy(j.pb); for (auto& q : j.path) fy(q); for (auto& a : j.pathAng) a = -a; if (j.kind == "leaf") j.flip = !j.flip; }
    fy(up.breech.face); fy(up.breech.muzzle); fy(up.breech.strikerTip);
    up.bodies[mi].gen = ref.bodies[mi].gen; fy(up.bodies[mi].gen.top); up.bodies[mi].gen.angle = 180; generateMagazine(up, mi);
    auto S = sim::build(up);
    sim::run(*S, 0.3);
    for (int k = 0; k < 4; k++) { sim::setKey(*S, "fire", true); sim::run(*S, 0.05); sim::setKey(*S, "fire", false); sim::run(*S, 0.25); }
    CHECK(S->shots == 4, "a magazine on top feeds down: the upside-down pistol fires 4 of 4");
    R = sim::runReliability(up, o);
    CHECK(R.fired == 20 && R.jams <= 2, "and runs 20 rounds (the odd case falls back in through the port, now underneath)");
}
static void handAnimations() {
    std::printf("handAnimations\n");
    Design d = loadPreset("blowback");
    d.breech.loaded = false;   // the magazine is in, the chamber empty: racking the slide must chamber a round
    auto key = [](double t, V2 at, bool grip) {
        AnimKey k; k.t = t;
        k.support.on = true; k.support.part = "slide"; k.support.at = at; k.support.angle = 90; k.gripS = grip;
        return k;
    };
    Animation rack;
    rack.keys = {key(0.25, {30, 14}, true), key(0.45, {-8, 14}, true), key(0.5, {-8, 14}, false)};   // grip, pull back, let go
    d.anims["clear_jam"] = rack;
    Design back; designFromJson(designToJson(d), back);
    CHECK(back.anims.count("clear_jam") && back.anims["clear_jam"].keys.size() == 3 && back.anims["clear_jam"].keys[0].gripS, "an animation is saved with the gun");
    AnimHands mid = animAt(d, rack, 0.35);
    CHECK(mid.support.on && mid.support.at.x < 30 && mid.support.at.x > -8 && mid.gripS, "between keys the hand is on its way, still gripping");
    AnimHands a0 = animAt(d, rack, 0.3), a1 = animAt(d, rack, 0.4);
    CHECK(a1.support.at.x < a0.support.at.x, "and moving the way the keys go");
    auto S = sim::build(d);
    sim::run(*S, 0.3);
    CHECK(!S->chambered, "it starts with the chamber empty");
    CHECK(!sim::playAnim(*S, "inspect") && sim::playAnim(*S, "clear_jam"), "an empty slot doesn't play; a recorded one does");
    double furthest = 0;
    for (int i = 0; i < 160 && sim::animPlaying(*S); i++) {
        sim::run(*S, 0.005);
        for (auto& b : S->bodies) if (b.id == "slide") furthest = std::max(furthest, (b.x0 - b.x) * 1000);
    }
    sim::run(*S, 0.3);
    CHECK(furthest > 25, "the gripping hand pulled the slide back through the physics");
    CHECK(S->chambered, "and racking it that way chambered a round");
    // racking a loaded gun by hand: the live round comes out and falls through the action without knocking the next
    // one down into the magazine, so the slide still picks it up
    Design ld = loadPreset("blowback");
    ld.anims["clear_jam"] = rack;
    auto L = sim::build(ld);
    sim::run(*L, 0.3);
    sim::playAnim(*L, "clear_jam");
    while (sim::animPlaying(*L)) sim::run(*L, 0.01);
    sim::run(*L, 0.4);
    int out = 0;
    for (auto& b : L->bodies) if (b.isRound && !b.isCase && !b.gone && b.rs == sim::RS::Loose) out++;
    CHECK(out == 1 && L->chambered && L->malf == sim::Malf::None, "racking a loaded gun by hand throws the live round out and chambers the next");
    // working the action without an animation: worked out from the parts, the way the reliability test works it
    for (const char* P : {"boltaction", "pumpshotgun", "blowback"}) {
        auto B = sim::build(loadPreset(P));
        sim::run(*B, 0.3);
        sim::setKey(*B, "fire", true); sim::run(*B, 0.05); sim::setKey(*B, "fire", false); sim::run(*B, 0.4);
        Animation au = sim::autoWorkAction(*B);
        B->design.anims["auto"] = au;
        sim::Body* bolt = B->BR->root ? B->BR->root : B->BR;
        double turned = 0;
        sim::playAnim(*B, "auto");
        while (sim::animPlaying(*B)) { sim::run(*B, 0.005); turned = std::max(turned, std::fabs(bolt->roll)); }
        sim::run(*B, 0.4);
        bool turns = std::string(P) == "boltaction";
        CHECK(!au.keys.empty() && B->chambered && !B->spent && B->malf == sim::Malf::None && (!turns || turned > 80 * 3.14159 / 180),
              (std::string("worked by hand without an animation, the ") + P + (turns ? " (its bolt turned up and down)" : "") + " has a fresh round in").c_str());
    }
    {   // a key's turn: eased between keys, only while the hand grips
        Animation t;
        AnimKey k0; k0.t = 0.2; k0.firing.on = true; k0.gripF = true; k0.turn = 0;
        AnimKey k1 = k0; k1.t = 0.4; k1.turn = 90;
        t.keys = {k0, k1};
        double mid = animAt(d, t, 0.3).turn;
        CHECK(mid > 30 && mid < 60 && animAt(d, t, 0.45).turn < 0, "a bolt's turn eases between keys, and stops when the hand lets go");
    }
    Design old; designFromJson(designToJson(loadPreset("pump")), old);
    CHECK(old.anims.empty(), "older guns have no animations: the plain actions happen");
}
static void benchBugs() {
    std::printf("Playtest bugs (0.2.1)\n");
    {   // the bench used to run out of room after about 200 reloads: no new magazine, the gun empty
        auto S = sim::build(loadPreset("blowback")); sim::run(*S, 0.3);
        bool ok = true;
        for (int i = 0; i < 300 && ok; i++) { sim::quickReload(*S); sim::run(*S, 0.05); ok = sim::seatedMag(*S) != nullptr; }
        CHECK(ok, "300 reloads in a row: a magazine always goes in (the oldest clutter on the bench is cleared away)");
    }
    {   // changing capacity re-read the magazine's direction from its well: a well pointing up turned it over
        Design d = loadPreset("blowback");
        int mi = -1; for (size_t i = 0; i < d.bodies.size(); i++) if (d.bodies[i].magazine) mi = (int)i;
        for (auto& j : d.joints) if (j.type == "magwell") j.path.back().y = j.path.front().y - 60;
        d.bodies[mi].magCap = 9; generateMagazine(d, mi);
        double top = 1e9, bot = -1e9; for (auto& p : d.bodies[mi].pts) { top = std::min(top, p.y); bot = std::max(bot, p.y); }
        CHECK(std::fabs(d.bodies[mi].gen.angle) < 1 && std::fabs(top - d.bodies[mi].gen.top.y) < 1 && bot > top + 50, "changing a magazine's capacity never turns it over");
    }
}

static void depthShaping() {
    std::printf("depthShaping\n");
    // a block whose back face is a lower, shorter shape than its front
    BodyDef w; w.id = "w"; w.name = "Wedge"; w.layer = "A"; w.fixed = true;
    w.pts = {{50, 0}, {60, 0}, {60, 40}, {50, 40}}; w.thick = 10; fixShape(w);
    CHECK(!depthShaped(w), "a part is straight through until a back corner is moved");
    w.depthOff = {{0, 0}, {0, 0}, {0, -30}, {0, -30}}; w.depthZn = {0, 0, 0, 0}; w.depthZf = {0, 0, 0, 0}; w.depthRef = w.pts;
    CHECK(depthShaped(w), "moving a back corner shapes it in depth");
    auto top = [](const std::vector<std::vector<V2>>& s) { double t = -1e9; for (auto& o : s) for (auto& p : o) t = std::max(t, p.y); return t; };
    CHECK(std::fabs(top(depthSection(w, 4.99)) - 40) < 0.1 && std::fabs(top(depthSection(w, -4.99)) - 10) < 0.1 &&
          std::fabs(top(depthSection(w, 0)) - 25) < 1e-6, "its cross-section changes evenly from the front face to the back");
    auto sl = carve::slices(w);
    CHECK(sl.size() >= 3 && std::fabs(top(sl.front().outlines) - top(sl.back().outlines)) > 15, "the physics sees it as slabs, each its own shape");
    Design back; Design d; d.bodies = {w};
    CHECK(designFromJson(designToJson(d), back) && back.bodies[0].depthOff.size() == 4 && std::fabs(back.bodies[0].depthOff[2].y + 30) < 1e-9,
          "the shape in depth is saved");
    {   // adding a corner keeps the shape: the new corner's offsets come from its neighbours
        BodyDef x = w; x.pts.insert(x.pts.begin() + 3, V2{55, 40}); normalizeDepth(x);
        CHECK(x.depthOff.size() == 5 && std::fabs(x.depthOff[3].y + 30) < 1e-6 && std::fabs(x.depthOff[2].y + 30) < 1e-6, "adding a corner keeps the shape");
        BodyDef y = w; y.depthOff.assign(4, {0, 0}); normalizeDepth(y);
        CHECK(!depthShaped(y) && y.depthOff.empty(), "put back straight, it's a plain part again");
    }
    {   // the mirror across the gun swaps its front and back faces
        BodyDef m = w; m.mirrorKind = "depth"; BodyDef img = mirrorImage(m);
        CHECK(std::fabs(top(depthSection(img, 4.99)) - 10) < 0.1 && std::fabs(top(depthSection(img, -4.99)) - 40) < 0.1, "the mirror image is shaped the other way round");
    }
    // a slider pushed at the wedge: at the front it hits the tall face, at the back it passes over the low one
    auto push = [&](double z) {
        BodyDef s; s.id = "s"; s.name = "Slider"; s.layer = "A"; s.pts = {{0, 20}, {20, 20}, {20, 30}, {0, 30}}; s.thick = 2; s.z = z; fixShape(s);
        Design e; e.bodies = {w, s};
        auto S = sim::build(e);
        sim::Body* B = S->find("s");
        S->hand = {true, B, {0, 0}, {B->x + 0.08, B->y}};
        sim::run(*S, 0.4);
        return (B->x - B->x0) * 1000;
    };
    double front = push(4), rear = push(-4);
    std::printf("    slider travel: front %.1f mm, back %.1f mm\n", front, rear);
    CHECK(front < 35 && rear > 60, "a part at the front stops on the tall face; at the back it clears the low one");
}

static void handMobility() {
    std::printf("handMobility\n");
    DigitPose dg[DIGIT_COUNT];
    HandHold h; applyHandPreset(h, "support", "open");
    for (int d = 0; d < DIGIT_COUNT; d++) dg[d] = h.digits[d];
    auto dist = [](HV a, HV b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };
    {   // the thumb's root sits by the wrist, and only its two phalanges stand free
        auto bones = handBones(dg);
        HV root{}, knuckle{};
        for (auto& b : bones) if (b.name == "thumb_01") { root = b.a; knuckle = b.b; }
        CHECK(root.x < -25 && knuckle.x < 15, "the thumb's first bone runs from by the wrist to about the middle of the palm");
    }
    {   // opposition: the thumb's tip can meet each fingertip, the little finger's too
        int met = 0;
        for (int f = DIGIT_INDEX; f <= DIGIT_PINKY; f++) {
            DigitPose p[DIGIT_COUNT]; for (int d = 0; d < DIGIT_COUNT; d++) p[d] = dg[d];
            p[f].bend[0] = 60; p[f].bend[1] = 70; p[f].bend[2] = 35;   // the finger curls to meet it
            double left = reachDigit(p, DIGIT_THUMB, digitTip(p, f), 80);
            std::printf("    thumb to %s: %.1f mm left, across the palm %.0f\n", digitName(f), left, p[DIGIT_THUMB].oppose);
            if (left < 15) met++;   // the pads touch (they are about 17 mm across together)
        }
        CHECK(met == 4, "the thumb reaches every fingertip");
    }
    {   // reaching stays within what a hand can do, and a far-off target just gets as near as it can
        DigitPose p[DIGIT_COUNT]; for (int d = 0; d < DIGIT_COUNT; d++) p[d] = dg[d];
        reachDigit(p, DIGIT_INDEX, {300, 300, 300});
        double lo[5], hi[5]; digitLimits(DIGIT_INDEX, lo, hi);
        bool inside = p[DIGIT_INDEX].spread >= lo[0] - 1e-9 && p[DIGIT_INDEX].spread <= hi[0] + 1e-9;
        for (int k = 0; k < 3; k++) inside &= p[DIGIT_INDEX].bend[k] >= lo[k + 1] - 1e-9 && p[DIGIT_INDEX].bend[k] <= hi[k + 1] + 1e-9;
        CHECK(inside && p[DIGIT_INDEX].oppose == 0, "joints stay within their limits; only the thumb swings across the palm");
        DigitPose q[DIGIT_COUNT]; for (int d = 0; d < DIGIT_COUNT; d++) q[d] = dg[d];
        HV tip = digitTip(q, DIGIT_MIDDLE);
        double before = q[DIGIT_MIDDLE].bend[0];
        reachDigit(q, DIGIT_MIDDLE, tip);
        CHECK(std::fabs(q[DIGIT_MIDDLE].bend[0] - before) < 1e-9, "reaching where it already is moves nothing");
        q[DIGIT_THUMB].oppose = 45; reachDigit(q, DIGIT_MIDDLE, {tip.x - 10, tip.y + 15, tip.z});
        CHECK(dist(digitTip(q, DIGIT_MIDDLE), {tip.x - 10, tip.y + 15, tip.z}) < 1, "a fingertip goes where it's dragged");
    }
    {   // the swing across the palm saves; older hands load with none
        Design d = loadPreset("blowback");
        applyHandPreset(d.pose.firing, "firing", "grip");
        d.pose.firing.digits[DIGIT_THUMB].oppose = 37;
        Design back; designFromJson(designToJson(d), back);
        CHECK(std::fabs(back.pose.firing.digits[DIGIT_THUMB].oppose - 37) < 1e-9, "the thumb's swing across the palm is saved");
        std::string js = designToJson(d);
        size_t at = js.find("37");
        CHECK(at != std::string::npos, "(written)");
    }
}

int main() {
    tilting();
    longRecoil();
    turnBolt();
    physicalLugs();
    magazines();
    controlsAndSprings();
    feedAndCartridges();
    blowbackAndEditing();
    physicalPistol("blowback");
    physicalPistol("tilting");
    boltRifle();
    roundTrip();
    builtInFireControl();
    edgeSplitting();
    benchHousekeeping();
    magazineSprings();
    bulletShapes();
    tutorialGun();
    anyMagazine();
    carving();
    roundParts();
    welding();
    tubeFeeding();
    ammoKinds();
    severalChambers();
    beltsAndClips();
    grips();
    barrelsAndTilt();
    benchBugs();
    shapeTools();
    liveMirrors();
    springBuckling();
    magazinePositions();
    handAnimations();
    depthShaping();
    handMobility();
    mapTests(failures);
    std::printf(failures ? "\n%d check(s) failed\n" : "\nAll checks passed\n", failures);
    return failures ? 1 : 0;
}
