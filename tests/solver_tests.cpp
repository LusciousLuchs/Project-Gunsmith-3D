// Headless checks that each example action actually cycles. Run: ./gunsmith3d_tests
#include "solver.hpp"
#include "reliability.hpp"
#include "version.hpp"
#include "carve.hpp"
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
    BodyDef f; f.pts = {{0, 0}, {60, 0}, {60, 2}, {0, 2}}; fixShape(f);
    for (int k = 0; k < 4; k++) subdivideBody(f);
    double longest = 0;
    for (size_t i = 0; i < f.pts.size(); i++) { V2 p = f.pts[i], q = f.pts[(i + 1) % f.pts.size()]; longest = std::max(longest, std::hypot(q.x - p.x, q.y - p.y)); }
    CHECK((int)f.pts.size() == MAX_CORNERS && longest < 8, "when they can't all be split, the longest edges are split first");
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
    sim::tidy(*S, 31);
    CHECK(!M->gone, "a magazine with rounds in it is never tidied away");
    M->rounds = 0;
    sim::tidy(*S, 20);
    CHECK(!M->gone, "an empty magazine stays for 30 seconds");
    S->hand.on = true; S->hand.b = M; S->hand.l = {0, 0}; S->hand.t = {M->x, M->y};
    sim::tidy(*S, 20);
    CHECK(!M->gone, "picking it up starts the 30 seconds again");
    S->hand.on = false;
    sim::tidy(*S, 31);
    CHECK(M->gone, "an empty magazine left alone for 30 seconds is removed");
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
        for (auto& b : d.bodies) if (b.magazine) { b.roundStyle = "line"; b.roundPath = {{20, 60}, {20, 200}}; }
        auto S = sim::build(d); sim::run(*S, 0.05);
        for (int k = 0; k < 9; k++) { S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true); sim::run(*S, 0.06); S->pressed = false; S->trigger = false; sim::setKey(*S, "fire", false); sim::run(*S, 0.25); }
        CHECK(S->shots == 8 && sim::seatedMag(*S)->rounds == 0, "a tube magazine feeds every round (the simple way, for now)");
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
    mapTests(failures);
    std::printf(failures ? "\n%d check(s) failed\n" : "\nAll checks passed\n", failures);
    return failures ? 1 : 0;
}
