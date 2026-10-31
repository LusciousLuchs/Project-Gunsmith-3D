// Headless checks that each example action actually cycles. Run: ./gunsmith3d_tests
#include "solver.hpp"
#include "version.hpp"
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
    mapTests(failures);
    std::printf(failures ? "\n%d check(s) failed\n" : "\nAll checks passed\n", failures);
    return failures ? 1 : 0;
}
