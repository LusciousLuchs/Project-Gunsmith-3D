// Headless checks that each example action actually cycles. Run: ./gunsmith3d_tests
#include "solver.hpp"
#include "autoanim.hpp"
#include "shotsound.hpp"
#include "npc.hpp"
#include "career.hpp"
#include "places.hpp"
#include "npcai.hpp"
#include "rig.hpp"
#include <array>
#include <map>
#include "doors.hpp"
#include "penetrate.hpp"
#include "map.hpp"
#include "defaults.hpp"
#include "nav.hpp"
#include <fstream>
#include <filesystem>
#include "reliability.hpp"
#include "version.hpp"
#include "carve.hpp"
#include "mounts.hpp"
#include "fitting.hpp"
#include "hand.hpp"
#include "kits.hpp"
#include "netradio.hpp"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <chrono>
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
        S->handStrength = 150;   // (a careful hand: a hard yank can flick the case off the claw)
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
    for (auto& b : d.bodies) b.fcg = b.id == "frame";   // (the tutorial's "The trigger" step)
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

// A magazine's round angle, feed direction and stack, set by hand on any magazine (0.6.1)
static void magazineAngles() {
    std::printf("magazineAngles\n");
    Design d = loadPreset("blowback");
    int mi = -1;
    for (int i = 0; i < (int)d.bodies.size(); i++) if (d.bodies[i].magazine) { mi = i; break; }
    double rd = 0, fd = 0;
    CHECK(mi >= 0 && magFeedAngles(d, d.bodies[mi], rd, fd) && std::fabs(rd) < 0.5 && std::fabs(fd - 90) < 0.5, "a fitted pistol magazine: rounds level, fed straight up");
    sim::ReliabilityOptions o; o.rounds = 15;
    auto base = sim::runReliability(d, o);
    Design e = d; e.bodies[mi].roundSet = e.bodies[mi].feedSet = true; e.bodies[mi].roundAngle = rd; e.bodies[mi].feedAngle = fd;
    auto same = sim::runReliability(e, o);
    CHECK(base.fired == same.fired && base.jams == same.jams, "set by hand to the automatic angles: it feeds exactly as before");
    Design down = d; down.bodies[mi].roundSet = true; down.bodies[mi].roundAngle = -10;
    auto bad = sim::runReliability(down, o);
    std::printf("        (round level: %d jams in %d; nose down 10 degrees: %d jams)\n", base.jams, base.fired, bad.jams);
    CHECK(bad.jams > base.jams, "rounds lying nose down stub on the way in: the angle matters");
    Design h = d; freezeMagazine(h.bodies[mi], h.breech.cart); h.bodies[mi].magStack = 2;
    auto dbl = sim::runReliability(h, o);
    CHECK(magStackOf(h.bodies[mi]) == 2 && dbl.jams == 0 && dbl.fired == 15, "a magazine reshaped by hand can be double stack, and feeds");
    Design back;
    h.bodies[mi].roundSet = true; h.bodies[mi].roundAngle = 4; h.bodies[mi].feedSet = true; h.bodies[mi].feedAngle = 80;
    CHECK(designFromJson(designToJson(h), back) && back.bodies[mi].magStack == 2 && back.bodies[mi].roundSet && std::fabs(back.bodies[mi].roundAngle - 4) < 1e-9
          && back.bodies[mi].feedSet && std::fabs(back.bodies[mi].feedAngle - 80) < 1e-9 && !d.bodies[mi].roundSet, "stack and angles save and load (and older blueprints stay automatic)");
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

// a reload animation with a stripper clip: taken in hand, stripped in one by one over the key's span, thrown away
static void stripperClipAnim() {
    Design st = loadPreset("boltaction");
    for (auto& b : st.bodies) if (b.magazine) { b.clip = "stripper"; b.clipSize = 5; }
    Animation an;
    AnimKey k; k.firing = st.pose.firing; k.support = st.pose.support;
    k.t = 0.2; k.event = "clipin"; an.keys.push_back(k);
    k.t = 0.4; k.event = "strip"; an.keys.push_back(k);
    k.t = 1.4; k.event = ""; an.keys.push_back(k);
    k.t = 1.6; k.event = "clipout"; an.keys.push_back(k);
    st.anims["reload"] = an;
    CHECK(clipAt(st, an, 0.1) == -1 && clipAt(st, an, 0.3) == 5 && clipAt(st, an, 0.9) == 3 && clipAt(st, an, 1.5) == 0 && clipAt(st, an, 1.7) == -1,
          "the editor's clip: none, five, three halfway through stripping, empty, gone");
    auto S = sim::build(st); sim::run(*S, 0.1);
    sim::Body* M = sim::seatedMag(*S);
    bool ok = M != nullptr && M->d && M->d->magCap >= 5;
    if (!ok) { CHECK(false, "the bolt rifle has a magazine to strip into"); return; }
    auto inMag = [&]() { return M->rounds; };   // (the top round under the lips isn't counted, as fillMagazines counts it)
    M->rounds = 0;
    int base = 0;
    CHECK(sim::playAnim(*S, "reload"), "the reload animation plays");
    sim::run(*S, 0.3);
    int inHand = sim::clipInHand(*S), at03 = inMag() - base;
    sim::run(*S, 0.6);   // t 0.9: halfway through the stripping
    int mid = inMag() - base, midClip = sim::clipInHand(*S);
    sim::run(*S, 0.6);   // t 1.5
    int after = inMag() - base, afterClip = sim::clipInHand(*S);
    bool out = false;
    sim::run(*S, 0.2);
    for (auto& e : S->events) out |= e.kind == sim::Ev::ClipOut;
    std::printf("  stripper clip: in hand %d, magazine %d -> %d (clip %d) -> %d (clip %d), clip out %d\n", inHand, at03, mid, midClip, after, afterClip, (int)out);
    CHECK(inHand == 5 && at03 == 0, "a clip of five in the hand, none in yet");
    CHECK(mid >= 2 && mid <= 3 && midClip == 5 - mid, "halfway: rounds going in one by one, the rest still on the clip");
    CHECK(after == 5 && afterClip == 0, "all five in by the next key, the clip empty");
    CHECK(out && sim::clipInHand(*S) == -1, "the empty clip is thrown away");
    M->rounds = M->d->magCap - 2;   // nearly full: only two fit, the rest stay on the clip
    sim::playAnim(*S, "reload"); sim::run(*S, 1.5);
    CHECK(inMag() == M->d->magCap && sim::clipInHand(*S) == 3, "a nearly full magazine takes what fits");
    Design back; designFromJson(designToJson(st), back);
    CHECK(back.anims["reload"].keys.size() == 4 && back.anims["reload"].keys[1].event == "strip", "the clip keys save and load");
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
    stripperClipAnim();
}

static void grips() {
    std::printf("How it's held (the pose)\n");
    Design p = loadPreset("blowback"), r = loadPreset("boltaction");
    CHECK(heldWith(p) == "one hand" && heldWith(r) == "two hands" && r.pose.butt.on, "the pistol is held with one hand, the rifle with two and against the shoulder");
    Design back; designFromJson(designToJson(r), back);
    CHECK(back.pose.firing.on && back.pose.support.on && back.pose.butt.on && std::fabs(back.pose.firing.angle - 105) < 1e-9 && back.pose.firing.part == "stock",
          "grip markers from earlier versions become the pose's hands and butt, and save as a pose");
    CHECK(r.pose.support.under && back.pose.support.under && !r.pose.firing.under, "an older rifle's support hand sits with its knuckles at the fore-end's top (clear of the sights), and keeps it when saved");
    r.pose.hold[HOLD_HIGH].set = true; r.pose.hold[HOLD_HIGH].eye = {12, -340}; r.pose.hold[HOLD_HIGH].yaw = 20;
    designFromJson(designToJson(r), back);
    CHECK(back.pose.hold[HOLD_HIGH].set && std::fabs(back.pose.hold[HOLD_HIGH].yaw - 20) < 1e-9 && !back.pose.hold[HOLD_AIM].set, "a posed hold saves; the others stay worked out from the gun");
    BodyHold aim = holdOf(r, HOLD_AIM), low = holdOf(r, HOLD_LOW);
    CHECK(std::fabs(aim.eye.x - (r.pose.butt.at.x + 90)) < 1e-6 && low.tilt > 20, "unposed: the eye sits over the stock when aiming, and the muzzle drops when lowered");
    {
        BodyHold high = holdOf(p, HOLD_HIGH);
        double t = -high.tilt * 3.14159265358979 / 180, fx = p.pose.firing.at.x - high.eye.x, fy = p.pose.firing.at.y - high.eye.y;
        double fwd = fx * std::cos(t) + fy * std::sin(t), down = fy * std::cos(t) - fx * std::sin(t);   // (the hand from the eye, once tipped)
        CHECK(high.tilt < -20 && std::fabs(fwd - 230) < 1 && std::fabs(down - 330) < 1, "unposed high ready: the muzzle up, the firing hand in front of the chest");
    }
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
    {   // thickness corner by corner: one corner only 4 mm thick (the rest 20), so the part tapers toward it
        BodyDef t; t.pts = {{0, 0}, {100, 0}, {100, 40}, {0, 40}}; t.thick = 20; fixShape(t);
        t.depthOff.assign(4, V2{0, 0}); t.depthZn = {0, -8, 0, 0}; t.depthZf = {0, 8, 0, 0}; t.depthRef = t.pts;
        auto area = [](const std::vector<std::vector<V2>>& sec) { double a = 0; for (auto& o : sec) for (size_t i = 0; i < o.size(); i++) { auto& u = o[i]; auto& v = o[(i + 1) % o.size()]; a += u.x * v.y - v.x * u.y; } return std::fabs(a / 2); };
        double mid = area(depthSection(t, 0)), outer = area(depthSection(t, 8));
        CHECK(std::fabs(mid - 4000) < 1 && outer < mid * 0.85, "a corner given its own thickness: the part tapers to it, and the physics sees the taper");
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

static void engravings() {
    std::printf("engravings\n");
    BodyDef b; b.id = "s"; b.name = "Slide"; b.pts = {{0, 0}, {100, 0}, {100, 30}, {0, 30}}; b.thick = 20; b.z = 0; fixShape(b);
    MarkDef line; line.pts = {{10, 10, 10}, {40, 12, 10}}; line.n = {{0, 0, 1}, {0, 0, 1}};
    MarkDef text; text.pts = {{70, 15, 10}}; text.n = {{0, 0, 1}}; text.text = "CAL. 9MM"; text.size = 4; text.right = textRightFor({0, 0, 1}, 0);
    b.marks = {line, text}; followMarks(b);
    double w = 0;
    auto st = strokeText("CAL. 9MM", 4, &w);
    CHECK(st.size() == 8 && w > 20 && w < 50, "text is spelt out in strokes, about as wide as it should be");
    CHECK(std::fabs(text.right.x - 1) < 1e-9 && std::fabs(textRightFor({0, 0, -1}, 0).x + 1) < 1e-9, "text reads left to right on the near side, and on the far side seen from there");
    Design d; d.bodies = {b};
    Design back;
    CHECK(designFromJson(designToJson(d), back) && back.bodies[0].marks.size() == 2 && back.bodies[0].marks[1].text == "CAL. 9MM" &&
          std::fabs(back.bodies[0].marks[0].pts[1].x - 40) < 1e-9, "engravings are saved");
    BodyDef m = b;
    for (auto& p : m.pts) { p.x += 25; p.y -= 5; }
    followMarks(m);
    CHECK(std::fabs(m.marks[0].pts[0].x - 35) < 1e-9 && std::fabs(m.marks[0].pts[0].y - 5) < 1e-9, "moving the part takes its engravings along");
    BodyDef f = b; flipBody(f, true); followMarks(f);
    double cx = 50;   // flipped about its middle: a mark 40 mm in from the left is now 40 mm in from the right
    CHECK(std::fabs(f.marks[0].pts[0].x - (2 * cx - 10)) < 1e-6, "flipping the part flips its engravings");
    BodyDef r = b; r.pts[2].x = 140; followMarks(r);
    CHECK(std::fabs(r.marks[0].pts[0].x - 10) < 1e-9, "reshaping the outline leaves engravings where they are");
    BodyDef t2 = b; t2.thick = 30; followMarks(t2);
    CHECK(std::fabs(t2.marks[0].pts[0].z - 15) < 1e-9, "made thicker, its near face moves out and the engravings stay on it");
    BodyDef c = b; c.mirrorKind = "depth";
    CHECK(mirrorImage(c).marks.empty(), "a copy on the other side of the gun starts without engravings");
}

static void poseAndGunMoves() {
    std::printf("poseAndGunMoves\n");
    Design d = loadPreset("boltaction");
    // the shoulder pocket: kept with the gun, and older blueprints keep the old place
    d.pose.hold[HOLD_AIM] = holdOf(d, HOLD_AIM); d.pose.hold[HOLD_AIM].set = true;
    d.pose.hold[HOLD_AIM].pocket[0] = -70; d.pose.hold[HOLD_AIM].pocket[1] = -150;
    Design back; designFromJson(designToJson(d), back);
    CHECK(std::fabs(back.pose.hold[HOLD_AIM].pocket[0] + 70) < 1e-9 && std::fabs(back.pose.hold[HOLD_AIM].pocket[1] + 150) < 1e-9, "a moved shoulder pocket is saved");
    BodyHold def;
    CHECK(def.pocket[0] == -90 && def.pocket[1] == -120 && def.pocket[2] == 25, "an unmoved pocket is where it always was");
    // the gun moved in an animation: saved, eased in from the hold and back to it
    Animation a;
    AnimKey k; k.t = 1.0; k.firing = d.pose.firing; k.support = d.pose.support; k.gun[1] = 80; k.gun[4] = -40;
    a.keys = {k};
    AnimKey k2 = k; k2.t = 5.0; k2.gun[1] = 60;
    a.keys.push_back(k2);
    d.anims["inspect"] = a;
    designFromJson(designToJson(d), back);
    CHECK(back.anims.count("inspect") && back.anims["inspect"].keys.size() == 2 && std::fabs(back.anims["inspect"].keys[0].gun[4] + 40) < 1e-9, "the gun's place at a key is saved");
    CHECK(animLength(a) > 5.0, "an animation can run past 2 seconds");
    AnimHands h0 = animAt(d, a, 0), h1 = animAt(d, a, 1.0), hh = animAt(d, a, 0.5), he = animAt(d, a, animLength(a));
    CHECK(!gunMoved(h0.gun) && !gunMoved(he.gun), "it starts and ends in the hold");
    CHECK(std::fabs(h1.gun[1] - 80) < 1e-6 && hh.gun[1] > 5 && hh.gun[1] < 80, "it passes through the key, on its way between");
    Animation ins = defaultInspect(d);
    CHECK(!ins.keys.empty() && gunMoved(ins.keys[0].gun), "a gun with no inspect animation is still looked over");
}
// a free piston beside a rifle bore, pushed by a gas port: what it gets from one shot
static double gasShot(const char* cart, double barrel, double at, double port, double pdia, double stroke, double mass) {
    Design d = loadPreset("blank");
    d.bodies.clear(); d.joints.clear();
    d.fcgRequired = false;   // (gas measurements, not fire control)
    auto rect = [](std::string id, double x0, double y0, double x1, double y1, bool fixed, double m) {
        BodyDef b; b.id = id; b.name = id; b.pts = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; b.fixed = fixed; b.mass = m; b.thick = 10; b.layer = "ABC"; fixShape(b); return b; };
    d.bodies.push_back(rect("barrel", 0, -6, barrel, 6, true, 0));
    d.bodies.push_back(rect("brk", -30, -6, 0, 6, true, 0));
    d.bodies.push_back(rect("car", 100, 20, 160, 32, false, mass));
    JointDef t; t.type = "track"; t.a = "car"; t.b = "world"; t.p = {130, 26}; t.path = {{130, 26}, {-300, 26}}; t.lockRot = true; d.joints.push_back(t);
    BreechDef& br = d.breech; br.on = true; br.breech = "brk"; br.chamber = "barrel"; br.hasFace = true; br.face = {0, 0}; br.hasMuzzle = true; br.muzzle = {barrel, 0};
    for (auto& c : cartridgePresets()) if (c.name == cart) br.cart = c;
    br.cartImpulse = true; applyCartridge(br); br.loaded = true; br.feedMode = "single"; br.ignition = "button"; br.cockAt = 0;
    GasPortDef g; g.at = at; g.portDia = port; g.piston = "car"; g.pistonDia = pdia; g.stroke = stroke; br.gas.push_back(g);
    auto S = sim::build(d);
    sim::run(*S, 0.05);
    S->pressed = true; S->trigger = true;
    sim::run(*S, 0.02);
    return S->gas.empty() ? 0 : S->gas[0].impulse;
}
static void gasAndOpenBolt() {
    std::printf("gasAndOpenBolt\n");
    // gas: an AR-15-like setup gives its 300 g carrier what a real one gets (about 1.6 N s, 5 to 6 m/s)
    double ar = gasShot("5.56x45mm", 508, 330, 2.4, 12.7, 3.5, 300);
    std::printf("  (AR-15-like carrier: %.2f N s)\n", ar);
    CHECK(ar > 1.3 && ar < 2.1, "a rifle's gas port pushes its carrier about as hard as a real one");
    CHECK(gasShot("5.56x45mm", 508, 330, 1.5, 12.7, 3.5, 300) < ar && gasShot("5.56x45mm", 508, 330, 3.5, 12.7, 3.5, 300) > ar, "a bigger port pushes harder");
    CHECK(gasShot("5.56x45mm", 508, 180, 2.4, 12.7, 3.5, 300) > ar && gasShot("5.56x45mm", 508, 480, 2.4, 12.7, 3.5, 300) < ar, "nearer the chamber pushes harder, nearer the muzzle softer");
    CHECK(gasShot("5.56x45mm", 508, 330, 2.4, 12.7, 1.5, 300) < ar, "a shorter stroke vents sooner");
    CHECK(gasShot("5.56x45mm", 508, 600, 2.4, 12.7, 3.5, 300) == 0, "a port past the muzzle gets nothing");
    // saved with the gun; older blueprints have none
    {
        Design d = loadPreset("pump");
        CHECK(d.breech.gas.empty() && !d.breech.openBolt, "older guns have no gas ports and a closed bolt");
        GasPortDef g; g.at = 150; g.portDia = 3; g.piston = "bar"; g.pistonDia = 14; g.stroke = 20; g.dir = 5;
        d.breech.gas.push_back(g); d.breech.openBolt = true;
        Design back; designFromJson(designToJson(d), back);
        CHECK(back.breech.openBolt && back.breech.gas.size() == 1 && back.breech.gas[0].piston == "bar" && back.breech.gas[0].pistonDia == 14 && back.breech.gas[0].dir == 5, "gas ports and the open bolt are saved");
    }
    // open bolt: the long-recoil gun made to fire from an open bolt
    for (const char* mode : {"semi", "auto"}) {
        Design d = loadPreset("longrecoil");
        d.breech.openBolt = true; d.breech.fireMode = mode;
        auto S = sim::build(d);
        sim::run(*S, 0.2);
        CHECK(S->searHeld && !S->chambered && sim::breechGap(*S) > 20, "an open bolt starts held back, the chamber empty");
        S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true);
        // every shot with the bolt home against the breech face (not still running in, short of it)
        double worst = 0; int seen = S->shots;
        for (int k = 0; k < (std::string(mode) == "auto" ? 4500 : 800); k++) {
            sim::substep(*S);
            if (S->shots != seen) { worst = std::max(worst, sim::breechGap(*S)); seen = S->shots; }
        }
        std::printf("  (%s: %d shots, the bolt at most %.2f mm off the breech face when each fired)\n", mode, S->shots, worst);
        CHECK(S->shots > 0 && worst <= 0.15, "an open bolt fires only once it's home against the breech face");
        S->trigger = false; sim::setKey(*S, "fire", false);
        sim::run(*S, 0.2);
        if (std::string(mode) == "semi") CHECK(S->shots == 1 && S->searHeld, "semi: one shot per pull, then the sear holds the bolt open again");
        else CHECK(S->shots >= 3 && S->searHeld, "full-auto: it keeps firing while the trigger is held, and is caught when it's let go");
        sim::ReliabilityOptions o; o.rounds = 20;
        auto R = sim::runReliability(d, o);
        CHECK(R.fired == 20 && R.jams == 0, "it shoots 20 rounds without a stoppage");
    }
}
static void magazineShaping() {
    std::printf("magazineShaping\n");
    Design d = loadPreset("tilting");
    int mi = -1;
    for (int i = 0; i < (int)d.bodies.size(); i++) if (d.bodies[i].magazine) mi = i;
    CHECK(mi >= 0 && d.bodies[mi].gen.on, "the example's magazine is fitted to its cartridge");
    // a size of your own, kept with the gun
    double H0 = d.bodies[mi].gen.H;
    d.bodies[mi].gen.extraH = 12; d.bodies[mi].gen.extraW = 3;
    generateMagazine(d, mi);
    CHECK(std::fabs(d.bodies[mi].gen.H - H0 - 12) < 1e-6, "a fitted magazine can be made longer than it needs to be");
    Design back; designFromJson(designToJson(d), back);
    CHECK(back.bodies[mi].gen.extraH == 12 && back.bodies[mi].gen.extraW == 3, "its extra length and width are saved");
    // reshaped by hand: it keeps the shape, and its top round is presented where it was
    Design f = loadPreset("tilting");
    auto presented = [](const Design& dd) {
        auto S = sim::build(dd); sim::run(*S, 0.3);
        for (auto& r : S->bodies) if (r.isRound && r.rs == sim::RS::Presented) return V2{r.x * 1000, r.y * 1000};
        return V2{-1e9, -1e9};
    };
    V2 a = presented(f);
    freezeMagazine(f.bodies[mi], f.breech.cart);
    f.bodies[mi].pts[0].x -= 2;   // a corner moved by hand
    std::vector<V2> shaped = f.bodies[mi].pts;
    generateMagazine(f, mi);
    bool kept = f.bodies[mi].pts.size() == shaped.size();
    for (size_t k = 0; kept && k < shaped.size(); k++) kept = f.bodies[mi].pts[k].x == shaped[k].x && f.bodies[mi].pts[k].y == shaped[k].y;
    CHECK(kept, "a magazine reshaped by hand isn't refitted over what you did");
    V2 b = presented(f);
    CHECK(std::hypot(a.x - b.x, a.y - b.y) < 0.05, "its rounds keep their place (the top round is presented where it was)");
    designFromJson(designToJson(f), back);
    CHECK(back.bodies[mi].gen.byHand && back.bodies[mi].roundPath.size() >= 2, "the hand-made shape and the rounds' path are saved");
    CHECK(autoRoundPath(loadPreset("tilting").bodies[mi], f.breech.cart).size() >= 2, "an automatic layout has a feed spot and a direction to drag");
}
// Career mode: rating a gun, building copies, the online store, reviews, contracts, saving
// (0.8.47) money before your first gun: ammunition, accessories, cleaning jobs; the counter's progress kept; paperwork help
// the built-in home garage street: loads, has the shop's things, and isn't too heavy for walking and path-finding
static void garageStreet() {
    using namespace gmap;
    Map m = propertyShell("garage");
    int stations = 0; for (auto& e : m.ents) stations += e.type == "station_bench" || e.type == "shop_counter" || e.type == "shop_computer" || e.type == "shop_door";
    CHECK(m.brushes.size() > 500 && stations == 4 && m.build.on() && m.roofY > 2.5, "the garage street: hundreds of blocks, the shop's four things, a build area and ceiling");
    auto t0 = std::chrono::steady_clock::now();
    nav::Grid g = nav::build(m, nav::Agent{});
    double navMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    t0 = std::chrono::steady_clock::now();
    int hits = 0;
    for (int i = 0; i < 2000; i++) { Hit h = sweepBox(m, {4.0 + 0.001 * i, 0.9, -8.5}, {12.0, 0.9, -8.5}, {0.3, 0.9, 0.3}); hits += h.brush >= 0; }
    double sweepUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 2000;
    std::printf("  garage street: %zu blocks, path-finding grid %.0f ms, a walking step %.1f us\n", m.brushes.size(), navMs, sweepUs);
    CHECK(navMs < 4000 && sweepUs < 500, "the street is quick enough to walk and plan routes on");
}
static void careerEarlyIncome() {
    career::Career c = career::newCareer(5);
    career::Walkin* clean = nullptr; career::Walkin* ammo = nullptr; career::Walkin* gun = nullptr;
    for (auto& w : c.walkins) { if (w.want == "cleaning") clean = &w; if (w.want == "ammo") ammo = &w; if (w.want == "gun" && w.trouble.empty() && !gun) gun = &w; }
    CHECK(clean && ammo && gun, "day one: a gun to clean and an ammunition buyer besides the gun buyers");
    if (!clean || !ammo || !gun) return;
    double m0 = c.money; int a0 = c.ammo;
    std::string why;
    CHECK(career::sellSupplies(c, ammo->id, ammo->qty, &why) && c.ammo == a0 - std::min(a0, ammo->qty) && c.money > m0, "ammunition sold from stock, no paperwork");
    double pay = clean->jobPay; std::string who = clean->name;
    CHECK(career::takeJob(c, clean->id, &why) && c.jobs.size() == 1 && pay > 0, "a gun taken in to clean");
    std::string gunName = gun->name; int gid = gun->id;
    gun->typed[0] = "Half"; gun->step = 2;
    career::Career back; career::careerFromJson(career::careerToJson(c), back);
    bool kept = false;
    for (auto& w : back.walkins) if (w.id == gid) kept = w.typed[0] == "Half" && w.step == 2;
    CHECK(kept && back.jobs.size() == 1 && back.ammo == c.ammo, "what you'd typed at the counter, the step, jobs and ammunition are saved");
    std::string r = career::doPaperwork(c, gid);
    career::Walkin* g = nullptr; for (auto& w : c.walkins) if (w.id == gid) g = &w;
    CHECK(g && g->checked && (r == "PROCEED" || r == "DELAYED") && g->scanned && g->flaws.empty(), "the paperwork done for you: form fixed, ID scanned, check run");
    career::Walkin bad = *g; bad.idName = "Someone Else"; bad.scan = "EXPIRED";
    auto probs = career::paperworkProblems(bad);
    CHECK(probs.size() >= 2, "paperwork help spells out a name that doesn't match and an ID that's run out");
    double m1 = c.money;
    career::endDay(c);
    bool paid = false; for (auto& n : c.news) paid |= n.find("Cleaned " + who) != std::string::npos;
    std::printf("  cleaning paid %.0f (news %d), money %.0f -> %.0f, errands tomorrow %d\n", pay, (int)paid, m1, c.money,
                (int)std::count_if(c.walkins.begin(), c.walkins.end(), [](const career::Walkin& w) { return w.want != "gun"; }));
    CHECK(paid && c.jobs.empty(), "the cleaning is done at the end of the day and paid");
    CHECK(std::count_if(c.walkins.begin(), c.walkins.end(), [](const career::Walkin& w) { return w.want != "gun"; }) >= 1, "more errands the next day");
}
static void careerRules() {
    std::printf("Career mode\n");
    using namespace career;
    Design pistol = loadPreset("blowback"), rifle = loadPreset("boltaction");
    GunSheet p = sheetOf(pistol), r = sheetOf(rifle);
    CHECK(!p.twoHands && r.twoHands && p.energy > 100 && r.energy > p.energy, "a pistol and a rifle are told apart by how they're held and their rounds");
    CHECK(p.massKg > 0.2 && p.massKg < 3 && p.buildHours > 1 && p.materialCost > 5 && p.capacity > 0, "a sheet has weight, hours to build, material cost, capacity");
    CHECK(r.moa > 0 && r.moa < p.moa, "a long barrel groups tighter than a short one");
    GunSheet good = p, bad = p; good.rated = bad.rated = true; good.jamRate = 0; bad.jamRate = 12;
    CHECK(good.value() > bad.value() * 1.3 && good.value() > p.value(), "a reliable gun is worth more than a jammy one (and than an untested one)");
    Career c = newCareer(7);
    int offers = 0; for (auto& k : c.contracts) if (k.state == 0) offers++;
    CHECK(c.day == 1 && c.money > 0 && offers >= 2, "a new career: day 1, some money, customers waiting");
    p.rated = true; p.jamRate = 0; p.name = "P";
    c.sheets["P"] = p;
    double before = c.money;
    CHECK(startBuild(c, "P", 3) && std::fabs(c.money - (before - 3 * p.materialCost)) < 1e-6, "building copies pays for their material up front");
    std::string why;
    CHECK(!startBuild(c, "Nothing", 1, &why) && !why.empty(), "a gun not yet rated can't be built");
    int days = 0;
    while (c.stock["P"] < 3 && days < 20) { endDay(c); days++; }
    int expect = (int)std::ceil(3 * p.buildHours / c.hoursPerDay);
    CHECK(c.stock["P"] == 3 && days <= expect + 1, "the copies are made from your working hours, day by day");
    // the store: cheap sells, dear doesn't
    Career cheap = c, dear = c;
    cheap.listings.push_back({"P", p.value() * 0.5, true, 0});
    dear.listings.push_back({"P", p.value() * 4, true, 0});
    for (int d = 0; d < 15; d++) { endDay(cheap); endDay(dear); }
    CHECK(cheap.listings[0].sold == 3 && cheap.money > c.money, "priced below its worth, every copy sells");
    CHECK(dear.listings[0].sold == 0, "priced at four times its worth, none do");
    // reviews follow how it really works
    auto avgStars = [&](double jam) {
        Career t = newCareer(3); GunSheet s = p; s.jamRate = jam; t.sheets["P"] = s; t.stock["P"] = 400; t.listings.push_back({"P", s.value() * 0.6, true, 0});
        for (int d = 0; d < 60; d++) endDay(t);
        double sum = 0; int n = 0; for (auto& rv : t.reviews) { sum += rv.stars; n++; }
        return n ? sum / n : 0.0;
    };
    double fine = avgStars(0), jammy = avgStars(15);
    std::printf("        (average stars: reliable %.2f, jammy %.2f)\n", fine, jammy);
    CHECK(fine > jammy + 1.0, "buyers of a jammy gun leave worse reviews");
    // contracts
    Contract k; k.id = 99; k.state = 1; k.hands = 2; k.qty = 2; k.pay = 3000; k.dueDay = c.day + 10;
    CHECK(!shortfalls(k, p).empty() && shortfalls(k, r).empty(), "a one-handed gun doesn't meet a brief for a long gun");
    Design unposed = rifle; unposed.pose = Pose();
    GunSheet u = sheetOf(unposed);
    auto miss = shortfalls(k, u);
    CHECK(!u.posed && miss.size() == 1 && miss[0].find("Pose") != std::string::npos, "a gun with no hold set isn't guessed at: the brief says to set it in Pose");
    Contract k2 = k; k2.id = 100; k2.hands = 1;
    c.contracts.push_back(k2);
    c.stock["P"] = 1;
    CHECK(!deliver(c, 100, "P", p), "not enough copies built: not delivered");
    c.stock["P"] = 2;
    double m0 = c.money;
    CHECK(deliver(c, 100, "P", p) && std::fabs(c.money - (m0 + 3000)) < 1e-6 && c.stock["P"] == 0, "delivered: paid, and the copies leave stock");
    Contract k3 = k2; k3.id = 101; k3.maxJam = 1;
    c.contracts.push_back(k3); c.stock["P"] = 2;
    CHECK(!deliver(c, 101, "P", bad) && c.stock["P"] == 2, "a gun that fails the test is turned down and the copies stay");
    Contract k4 = k2; k4.id = 102; k4.dueDay = c.day + 1; c.contracts.push_back(k4);
    double rep0 = c.reputation; endDay(c); endDay(c);   // (due by tomorrow: you have today and tomorrow)
    bool lapsed = false; for (auto& x : c.contracts) if (x.id == 102) lapsed = x.state == 3;
    CHECK(lapsed && c.reputation <= rep0, "a contract not delivered in time lapses, and costs reputation");
    // saving
    Career back;
    CHECK(careerFromJson(careerToJson(c), back) && back.day == c.day && std::fabs(back.money - c.money) < 1e-6 && back.contracts.size() == c.contracts.size() &&
          back.sheets.size() == c.sheets.size() && back.reviews.size() == c.reviews.size(), "a career saves and loads whole");
    double m1 = c.money, r1 = c.reputation + 10; c.reputation = r1;
    soldToProhibited(c);
    CHECK(std::fabs(c.money - (m1 - 2500)) < 1e-6 && c.reputation < r1, "selling to someone who failed the check: a fine and some reputation, nothing worse");
}

// Career mode, stages 2-4: staff and workstations, flaws and checks, the counter, big orders with a range demonstration
static void careerStages() {
    std::printf("Career mode: staff, the counter, big orders\n");
    using namespace career;
    Career c = newCareer(11);
    c.props[0].kind = "shack";   // (a garage has no room for staff: these are about workstations)
    auto setSt = [](Career& k, std::map<std::string, int> st) { k.props[0].stations = st; sumStations(k); };
    CHECK(c.stations["bench"] == 1 && c.stations["counter"] == 1 && c.candidates.size() == 3 && !c.walkins.empty(), "you start with a bench, a counter, people looking for work and customers");
    std::string why;
    CHECK(!hire(c, 0, &why) && why.find("workstation") != std::string::npos, "nobody can be hired with no free workstation (you're at the only bench)");
    setSt(c, {{"bench", 2}, {"lathe", 1}, {"counter", 1}});
    double alone = workHours(c);
    Employee mach; mach.name = "M"; mach.specialty = "machinist"; mach.skill = 0.6; mach.wage = 200;
    c.candidates = {mach, mach, mach};
    CHECK(hire(c, 0) && hire(c, 0) && !hire(c, 0, &why), "three workstations: you and two staff, and no more");
    double withM = workHours(c);
    Career c2 = c; setSt(c2, {{"bench", 3}, {"counter", 1}});
    std::printf("        (hours a day: you alone %.1f, with a machinist at a lathe %.1f, at a bench %.1f)\n", alone, withM, workHours(c2));
    CHECK(withM > alone + 6 && workHours(c2) < withM, "a machinist adds a day's work, more at a lathe than at a bench");
    c.accessories = 0;   // (no stock selling meanwhile: just the wages)
    double m0 = c.money; endDay(c);
    CHECK(c.money <= m0 - 400 + 1e-6, "wages are paid at the end of each day");
    // flaws: an unskilled crew makes flawed copies; a finisher at a test bench catches most
    auto flawRate = [&](double skill, bool finisher) {
        Career t = newCareer(5); t.props[0].kind = "warehouse"; setSt(t, {{"bench", 6}, {"qc", 1}});
        for (int i = 0; i < 4; i++) { Employee e; e.name = "A"; e.specialty = "assembler"; e.skill = skill; e.wage = 0; t.staff.push_back(e); }
        if (finisher) { Employee f; f.name = "F"; f.specialty = "finisher"; f.skill = 0.9; f.wage = 0; t.staff.push_back(f); }
        GunSheet g = sheetOf(loadPreset("blowback")); g.rated = true; g.name = "G"; t.sheets["G"] = g; t.money = 1e9;
        startBuild(t, "G", 400);
        for (int d = 0; d < 400 && t.stock["G"] < 400; d++) endDay(t);
        return (double)t.flawed["G"] / std::max(1, t.stock["G"]);
    };
    double raw = flawRate(0.1, false), checked = flawRate(0.1, true), good = flawRate(0.9, false);
    std::printf("        (flawed copies: green crew %.1f%%, with a finisher checking %.1f%%, skilled crew %.1f%%)\n", raw * 100, checked * 100, good * 100);
    CHECK(raw > good * 2 && checked < raw * 0.5, "green hands make more flawed copies; a finisher at a test bench catches most of them");
    // the counter: one in a hundred can't buy
    Career q = newCareer(17); int trouble = 0, n = 0;
    q.props[0].stations["counter"] = 1; sumStations(q);
    for (int d = 0; d < 1500; d++) { endDay(q); for (auto& w : q.walkins) { n++; if (!w.trouble.empty()) trouble++; } }
    std::printf("        (%d customers, %d couldn't buy: %.2f%%)\n", n, trouble, 100.0 * trouble / n);
    CHECK(n > 2000 && trouble > 0 && 100.0 * trouble / n < 2.5 && 100.0 * trouble / n > 0.3, "about one customer in a hundred can't buy");
    Career s = newCareer(23);
    GunSheet pist = sheetOf(loadPreset("blowback")); pist.rated = true; pist.name = "P"; s.sheets["P"] = pist; s.stock["P"] = 5;
    Walkin w; w.id = 900; w.name = "Clean"; w.idName = "Clean"; w.hands = 1; w.budget = 5000; s.walkins = {w};
    CHECK(!sellToWalkin(s, 900, "P", 400, &why) && why.find("check") != std::string::npos, "no sale before the background check");
    runCheck(s, 900);
    double sm = s.money;
    CHECK(s.walkins[0].result == "PROCEED" && sellToWalkin(s, 900, "P", 400) && std::fabs(s.money - (sm + 400)) < 1e-6 && s.stock["P"] == 4, "checked and clean: sold over the counter");
    Walkin bad = w; bad.id = 901; bad.trouble = "denied"; s.walkins.push_back(bad);
    runCheck(s, 901);
    double r0 = s.reputation;
    CHECK(s.walkins[1].result == "DENIED", "the check denies someone who can't buy");
    refuseWalkin(s, 901, true);
    CHECK(s.reputation > r0 && s.walkins[1].state == 3, "refusing them and calling the police: the sheriff thanks you");
    Walkin bad2 = w; bad2.id = 902; bad2.trouble = "mismatch"; bad2.idName = "Someone Else"; s.walkins.push_back(bad2);
    runCheck(s, 902);
    double m2 = s.money, r2 = s.reputation;
    CHECK(s.walkins[2].result == "PROCEED", "an ID that doesn't match the form gets past the check: you have to spot it");
    sellToWalkin(s, 902, "P", 400);
    CHECK(s.money < m2 && s.reputation < r2, "selling to them anyway: a fine and some reputation");
    // big orders: the bench test, then the range demonstration
    Career b = newCareer(31);
    Contract k; k.id = 950; k.state = 1; k.hands = 1; k.qty = 10; k.pay = 9000; k.dueDay = b.day + 30; k.demo = true; b.contracts.push_back(k);
    b.sheets["P"] = pist; b.stock["P"] = 10;
    double bm = b.money;
    CHECK(!deliver(b, 950, "P", pist) && b.contracts.back().state == 4 && std::fabs(b.money - bm) < 1e-6 && b.stock["P"] == 10, "a big order that passes the bench test waits for its demonstration, unpaid");
    CHECK(!demoResult(b, 950, "P", 7, 20) && b.contracts.back().state == 4, "too few hits: not yet (you can try again)");
    CHECK(demoResult(b, 950, "P", 15, 20) && b.contracts.back().state == 2 && std::fabs(b.money - (bm + 9000)) < 1e-6 && b.stock["P"] == 0, "a good demonstration: delivered and paid");
    CHECK(std::string(tierName(0)) != tierName(70), "reputation brings a name");
    Career back;
    CHECK(careerFromJson(careerToJson(c), back) && back.staff.size() == c.staff.size() && back.stations["lathe"] == 1 && back.walkins.size() == c.walkins.size(), "staff, stations and customers save and load");
    {   // a career saved by 0.5 (no workshop yet) loads with a bench, a counter, people to hire and customers
        Career old = newCareer(5); std::string t = careerToJson(old);
        auto o = nlohmann::json::parse(t); o.erase("stations"); o.erase("candidates"); o.erase("walkins"); o.erase("staff");
        Career back; CHECK(careerFromJson(o.dump(), back) && back.stations["bench"] == 1 && back.stations["counter"] == 1 && !back.candidates.empty() && !back.walkins.empty(),
              "a career saved before the workshop gets a bench, a counter, people looking for work and customers");
    }
}

static void holdOpenTest() {
    std::printf("holdOpen\n");
    for (const char* key : {"blowback", "tilting"}) {
        Design d = loadPreset(key);
        d.breech.holdOpen = true;
        auto S = sim::build(d);
        sim::run(*S, 0.1);
        int guard = 0;
        while (guard++ < 40) {   // fire until the magazine runs dry and the last round has gone
            S->trigger = true; S->pressed = true; sim::setKey(*S, "fire", true);
            sim::run(*S, 0.06);
            S->trigger = false; S->pressed = false; sim::setKey(*S, "fire", false);
            sim::run(*S, 0.25);
            sim::Body* M = sim::seatedMag(*S);
            if (M && M->rounds == 0 && !S->chambered) break;
        }
        sim::run(*S, 0.3);
        double at = sim::holdPoint(d.breech);
        CHECK(S->held && sim::breechGap(*S) >= at - 0.5, (std::string(key) + ": the empty gun is held open").c_str());
        S->slideRelease = true; sim::run(*S, 0.05); S->slideRelease = false; sim::run(*S, 0.3);
        CHECK(!S->held && sim::inBattery(*S), (std::string(key) + ": the slide release sends it home").c_str());
        // again, with a loaded magazine letting it go, and a round chambered on the way
        Design d2 = d; d2.breech.holdReleaseMag = true;
        auto S2 = sim::build(d2);
        sim::run(*S2, 0.1);
        guard = 0;
        while (guard++ < 40) {
            S2->trigger = true; S2->pressed = true; sim::setKey(*S2, "fire", true);
            sim::run(*S2, 0.06);
            S2->trigger = false; S2->pressed = false; sim::setKey(*S2, "fire", false);
            sim::run(*S2, 0.25);
            sim::Body* M = sim::seatedMag(*S2);
            if (M && M->rounds == 0 && !S2->chambered) break;
        }
        sim::run(*S2, 0.3);
        bool heldBefore = S2->held;
        sim::quickReload(*S2); sim::run(*S2, 0.5);
        CHECK(heldBefore && !S2->held && sim::inBattery(*S2) && S2->chambered, (std::string(key) + ": a loaded magazine sends it home on a round").c_str());
    }
    {   // without the setting, nothing changes: it closes on the empty chamber
        auto S = sim::build(loadPreset("blowback"));
        CHECK(!S->held, "off by default");
    }
}
static void reliabilityTimeout() {
    std::printf("reliabilityTimeout\n");
    sim::ReliabilityOptions o;
    o.rounds = 30;
    o.stallSeconds = 1e-6;   // (far too short for any machine: it must give up at once, with the reason)
    auto t0 = std::chrono::steady_clock::now();
    auto R = sim::runReliability(loadPreset("blowback"), o);
    double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(R.gaveUp && R.note.rfind("Gave up", 0) == 0, "a test that can't keep up gives up, saying why");
    CHECK(R.fired < 30, "it stopped early");
    CHECK(took < 3, "and promptly");
    o.stallSeconds = 10;
    o.rounds = 10;
    auto R2 = sim::runReliability(loadPreset("blowback"), o);
    CHECK(!R2.gaveUp && R2.fired == 10, "a working gun is untouched by the limit");
}
static void propertiesTest() {
    std::printf("properties\n");
    career::Career c = career::newCareer(7);
    CHECK(c.props.size() == 1 && c.props[0].kind == "garage", "a new career starts in the home garage");
    CHECK(career::staffRoom(c) == 0, "no room for staff in a garage");
    c.candidates.clear(); career::refreshCandidates(c);
    c.props[0].stations = {{"bench", 3}, {"counter", 1}}; career::sumStations(c);
    std::string why;
    CHECK(!career::hire(c, 0, &why) && why.find("garage") != std::string::npos, "hiring in a garage is refused, saying why");
    CHECK(!career::buyProperty(c, "warehouse", "both", &why), "a warehouse is too dear on day 1");
    c.money = 100000;
    CHECK(career::buyProperty(c, "shack", "workshop", &why) && c.props.size() == 2, "a shack bought, as a workshop");
    c.props[1].stations = {{"bench", 4}, {"lathe", 2}}; career::sumStations(c);
    CHECK(career::staffRoom(c) == 3, "the shack has room for 3 staff");
    CHECK(career::stationRoom(c) == 3 + 5, "workstations count up to each building's size (3 + 5)");
    CHECK(career::hire(c, 0, &why), "now someone can be hired");
    c.props[1].role = "store"; career::sumStations(c);
    CHECK(career::staffRoom(c) == 0 && career::stationRoom(c) == 3, "set as a store only: its benches and staff room don't count");
    c.props[1].role = "both"; career::sumStations(c);
    // a store that looks good gets more customers
    auto walkinsWith = [&](double rating) {
        career::Career k = c; k.props[0].rating = rating; k.props[0].stations["counter"] = 1; k.props[1].stations.erase("counter");
        int n = 0;
        for (int d = 0; d < 20; d++) { career::endDay(k); for (auto& w : k.walkins) if (w.prop == k.props[0].id && w.want == "gun") n++; }   // (gun buyers: errands come anyway)
        return n;
    };
    int plain = walkinsWith(0), smart = walkinsWith(5);
    std::printf("    walk-ins over 20 days: rating 0 -> %d, rating 5 -> %d\n", plain, smart);
    CHECK(smart > plain * 1.7, "a five-star store gets close to twice the walk-ins of a bare one");
    double before = c.money;
    CHECK(career::sellProperty(c, c.props[1].id, &why) && c.props.size() == 1 && c.money > before, "selling the shack gives money back");
    CHECK(!career::sellProperty(c, c.props[0].id, &why), "the last place can't be sold");
    // a shell's walls are locked, and its storefront rating grows with displays
    gmap::Map m = gmap::propertyShell("retail");
    bool locked = !m.brushes.empty(); for (auto& b : m.brushes) locked = locked && b.locked;
    CHECK(locked && m.build.on() && m.store.on(), "a retail unit's walls come locked, with its build area and storefront");
    double r0 = gmap::storeRating(m);
    for (int i = 0; i < 3; i++) { gmap::Entity e; e.type = "display_case"; e.ref = "Gun"; e.pos = {-3, 0, -2.0 + i * 1.5}; m.ents.push_back(e); }
    for (const char* d : {"decor_plant", "decor_rug", "decor_poster", "decor_sign"}) { gmap::Entity e; e.type = d; e.pos = {-2, 0, 0}; m.ents.push_back(e); }
    double r1 = gmap::storeRating(m);
    std::printf("    store rating: bare %.2f, decorated %.2f\n", r0, r1);
    CHECK(r1 > r0 + 1.5 && r1 <= 5, "displays and decoration raise the store's rating");
    gmap::Map back; gmap::mapFromJson(gmap::mapToJson(m), back);
    CHECK(back.build.on() && back.brushes[0].locked && std::fabs(back.roofY - m.roofY) < 1e-9, "zones, locked walls and the roof survive saving");
    // an older career's shop becomes its first property
    career::Career old = career::newCareer(3); old.props.clear(); old.shopTier = 1;
    career::Career loaded; career::careerFromJson(career::careerToJson(old), loaded);
    CHECK(loaded.props.size() == 1 && loaded.props[0].kind == "shack", "an older career's shop (tier 1) becomes a shack");
    // the home garage is only the starter: a retail unit or warehouse moves you out of it (a shack doesn't)
    {
        career::Career g = career::newCareer(9); g.money = 500000;
        int home = g.props[0].id; g.here = home;
        career::buyProperty(g, "shack", "workshop", &why);
        bool keptWithShack = career::propertyById(g, home) != nullptr;
        career::buyProperty(g, "retail", "both", &why);
        bool gone = career::propertyById(g, home) == nullptr, moved = g.here != home && career::propertyById(g, g.here) && career::propertyById(g, g.here)->kind == "retail";
        CHECK(keptWithShack && gone && moved && g.props.size() == 2, "buying a retail unit moves you out of the garage (and into the new place); a shack doesn't");
        career::Career both = career::newCareer(9); career::Property w; w.id = 7; w.kind = "warehouse"; both.props.push_back(w);
        career::Career lb; career::careerFromJson(career::careerToJson(both), lb);
        bool noGarage = true; for (auto& q : lb.props) noGarage &= q.kind != "garage";
        CHECK(noGarage && lb.props.size() == 1, "an older save with a garage and a warehouse loses the garage");
    }
}
static void taxesAndAccessories() {
    std::printf("taxes and accessories\n");
    career::Career c = career::newCareer(41);
    career::setLocation(c, c.props[0], places::cityByName("Austin", "TX"));
    CHECK(c.props[0].address.find("Austin, TX 78705") != std::string::npos, "the garage gets a made-up street address in Austin, TX 78705");
    CHECK(std::fabs(career::taxRate(c) - 21) < 1e-9, "Texas: no state tax on profit, so 21% federal");
    career::setLocation(c, c.props[0], places::cityByName("Los Angeles", "CA"));
    CHECK(career::taxRate(c) > 29, "California adds its own");
    c.money = 100000; c.taxable = 0;
    c.taxable = 10000;
    double m0 = c.money;
    for (int d = 0; d < 8; d++) career::endDay(c);
    CHECK(c.taxesPaid > 0 && c.money < m0, "a week's profit is taxed at the week's end");
    CHECK(!career::buyAccessories(c, 1000000), "can't buy more accessories than you can pay for");
    CHECK(career::buyAccessories(c, 100) && c.accessories == 100, "a hundred boxes of accessories in stock");
    double before = c.money;
    for (int d = 0; d < 10; d++) career::endDay(c);
    std::printf("    accessories sold in 10 days: %d\n", 100 - c.accessories);
    CHECK(c.accessories < 100, "the store sells accessories each day");
    (void)before;
    career::Career back; career::careerFromJson(career::careerToJson(c), back);
    CHECK(back.props[0].city == c.props[0].city && back.accessories == c.accessories && std::fabs(back.taxesPaid - c.taxesPaid) < 1e-6, "location, accessories and taxes save and load");
}
static void paperworkTest() {
    std::printf("paperwork\n");
    career::Career c = career::newCareer(77);
    career::setLocation(c, c.props[0], places::cityByName("Austin", "TX"));
    career::Walkin w;
    int found = 0, flawed = 0;
    for (int i = 0; i < 400; i++) { career::endDay(c); for (auto& x : c.walkins) { if (x.trouble.empty() && x.flaws.empty() && !found) { w = x; found = 1; } if (!x.flaws.empty()) flawed++; } }
    CHECK(found, "a clean customer turns up");
    CHECK(w.idNumber.size() == 12 && w.idNumber[2] == '-', "their licence number is in the game's own format");
    c.walkins = {w};
    std::string first = w.idName.substr(0, w.idName.find(' ')), last = w.idName.substr(w.idName.rfind(' ') + 1);
    CHECK(career::submitCheck(c, w.id, last + "x", first, w.idDob, w.idNumber) == "NO MATCH", "a typo in the check system: no match, try again");
    std::string r = career::submitCheck(c, w.id, last, first, w.idDob, w.idNumber);
    CHECK(r == "PROCEED" || r == "DELAYED", "their ID's details typed in right: the check answers");
    CHECK(career::scanId(c, w.id) == "VALID", "a good ID scans as valid");
    // a form with mistakes: handed back, they fix it; sold anyway, it's on record
    career::Walkin f = w; f.id = 9001; f.flaws = {"unsigned"}; f.signedForm = false; f.checked = true; f.result = "PROCEED";
    c.walkins.push_back(f);
    CHECK(career::fixForm(c, 9001) && c.walkins.back().signedForm && c.walkins.back().flaws.empty(), "handed back, they sign it");
    career::Walkin g = w; g.id = 9002; g.flaws = {"blank"}; g.answers[3] = ' '; g.checked = true; g.result = "PROCEED"; g.hands = 0; g.budget = 1e6; g.minEnergy = g.maxEnergy = 0; g.trouble.clear();
    c.walkins.push_back(g);
    career::GunSheet sh = career::sheetOf(loadPreset("blowback")); sh.rated = true; sh.name = "P"; c.sheets["P"] = sh; c.stock["P"] = 2;
    CHECK(career::sellToWalkin(c, 9002, "P", 500) && c.badForms == 1, "sold on a form with a blank answer: it's on record");
    career::Walkin d = w; d.id = 9003; d.trouble = "fakeid"; c.walkins.push_back(d);
    CHECK(career::scanId(c, 9003) == "NOT VALID", "a fake ID doesn't scan");
    std::printf("    customers with mistakes on their form: %d\n", flawed);
    CHECK(flawed > 0, "some forms come with mistakes to catch");
    career::Career back; career::careerFromJson(career::careerToJson(c), back);
    CHECK(back.walkins.size() == c.walkins.size() && back.walkins[0].idNumber == c.walkins[0].idNumber && back.badForms == 1, "the paperwork saves and loads");
}
static void shopChecks() {
    std::printf("shopChecks\n");
    gmap::Map m = gmap::defaultWorkshop();
    CHECK(gmap::shopMissing(m).empty(), "the starting workshop has everything");
    // take the bench, the counter, the door and the start away
    std::vector<gmap::Entity> keep;
    for (auto& e : m.ents) if (e.type != "station_bench" && e.type != "shop_counter" && e.type != "shop_door" && e.type != "player_start" && e.type != "light") keep.push_back(e);
    m.ents = keep;
    CHECK(gmap::shopMissing(m).size() == 5, "missing: start, bench, counter, door, a light");
    int n = gmap::shopRepair(m);
    CHECK(n == 5, "five put back");
    CHECK(gmap::shopMissing(m).empty(), "nothing missing after the repair");
    for (auto& e : m.ents) if (e.type == "station_bench") CHECK(std::fabs(e.pos.y) < 0.05, "the bench stands on the floor");
    CHECK(gmap::shopRepair(m) == 0, "a second repair adds nothing");
}
static void npcRotate() {
    std::printf("npcRotate\n");
    NpcDef n = npcPreset("human");
    int arm = -1;
    for (int i = 0; i < (int)n.bones.size(); i++) if (n.bones[i].name == "Upper arm (left)") arm = i;
    CHECK(arm > 0, "the human has a left upper arm");
    auto before = npcPose(n);
    double ax[3] = {before[arm].z[0], before[arm].z[1], before[arm].z[2]};   // about its own front axis
    NpcDef m = n;
    npcRotateBone(m, arm, ax, 30);
    auto after = npcPose(m);
    double c = 0; for (int k = 0; k < 3; k++) c += before[arm].y[k] * after[arm].y[k];
    double dz = 0; for (int k = 0; k < 3; k++) dz += before[arm].z[k] * after[arm].z[k];
    std::printf("        (the arm turned %.1f degrees; its axis kept %.4f)\n", std::acos(std::min(1.0, c)) * 180 / 3.14159265, dz);
    CHECK(std::fabs(std::acos(std::min(1.0, c)) * 180 / 3.14159265 - 30) < 0.5 && dz > 0.9999, "turning a bone 30 degrees about an axis turns it exactly that much, about that axis");
    double tw[3] = {before[arm].y[0], before[arm].y[1], before[arm].y[2]};
    NpcDef t = n; npcRotateBone(t, arm, tw, 40);
    auto tp = npcPose(t);
    double same = 0; for (int k = 0; k < 3; k++) same += before[arm].y[k] * tp[arm].y[k];
    CHECK(same > 0.9999 && std::fabs(t.bones[arm].twist - n.bones[arm].twist) > 30, "about its own length: it only twists");
    NpcDef r = m; npcRotateBone(r, arm, ax, -30);
    auto rp = npcPose(r);
    double err = 0; for (size_t i = 0; i < rp.size(); i++) for (int k = 0; k < 3; k++) err = std::max(err, std::fabs(rp[i].b[k] - before[i].b[k]));
    CHECK(err < 0.01, "turned back, every bone is where it was");
}
static void npcDesigns() {
    std::printf("npcDesigns\n");
    for (auto& [k, label] : npcPresets()) {
        NpcDef n = npcPreset(k);
        auto P = npcPose(n);
        double low = 1e9;
        for (auto& b : P) low = std::min({low, b.a[1], b.b[1]});
        CHECK(!n.bones.empty() && low > -60, (label + ": stands on the ground, not through it").c_str());
    }
    NpcDef h = npcPreset("human");
    CHECK(std::fabs(npcHeight(h) - 1780) < 120, "the human is about a person's height");
    NpcDef back;
    CHECK(npcFromJson(npcToJson(h), back) && back.bones.size() == h.bones.size() && back.bones[3].vital == h.bones[3].vital && back.name == h.name, "an NPC is saved and read back whole");
    int n0 = (int)h.bones.size();
    npcMirrorBone(h, 4);   // the left arm (3 bones) copied
    CHECK((int)h.bones.size() == n0 + 3, "mirroring a limb copies it and everything on it");
    npcRemoveBone(h, n0);
    CHECK((int)h.bones.size() == n0, "deleting a bone takes what hangs off it too");
    double dir[3] = {0, 0, 1};   // point the left forearm straight forward
    npcAimBone(h, 5, dir);
    auto P = npcPose(h);
    CHECK(P[5].y[2] > 0.999, "dragging a bone's end points it where you drag");
}
static void mapsCarryNpcs() {
    std::printf("mapsCarryNpcs\n");
    gmap::Map m = gmap::builtinRange();
    gmap::Entity e; e.type = "npc_spawn"; e.ref = "Crawler"; e.count = 4; e.pos = {10, 0, 2}; e.yaw = 180;
    gmap::Entity g; g.type = "gun_spawner"; g.pos = {1, 0, 1};
    m.ents.push_back(e); m.ents.push_back(g);
    m.npcs = {{"Crawler", npcToJson(npcPreset("crawler"))}};
    gmap::Map back;
    CHECK(gmap::mapFromJson(gmap::mapToJson(m), back), "a map with NPCs and a gun spawner saves and loads");
    int spawns = 0, spawners = 0;
    for (auto& x : back.ents) { if (x.type == "npc_spawn" && x.ref == "Crawler" && x.count == 4) spawns++; if (x.type == "gun_spawner") spawners++; }
    CHECK(spawns == 1 && spawners == 1, "its NPC spawn (which and how many) and gun spawner come back");
    NpcDef c;
    CHECK(back.npcs.size() == 1 && npcFromJson(back.npcs[0].second, c) && c.bones.size() == npcPreset("crawler").bones.size(), "the map file carries a full copy of its NPC");
    gmap::Map old;
    CHECK(gmap::mapFromJson(gmap::mapToJson(gmap::builtinRange()), old) && old.npcs.empty(), "maps without NPCs are as before");
    // guns made for NPCs
    Design d = loadPreset("blowback");
    CHECK(!d.npcGun.on, "guns are for players unless marked");
    d.npcGun.on = true; d.npcGun.rpm = 600; d.npcGun.burst = 3; d.npcGun.spread = 4;
    Design db; designFromJson(designToJson(d), db);
    CHECK(db.npcGun.on && db.npcGun.rpm == 600 && db.npcGun.burst == 3 && db.npcGun.spread == 4, "an NPC gun keeps its rate, burst and spread");
    // the character maker
    CharLook L; L.on = true;
    NpcDef ch = characterFromLook(L);
    auto has = [&](const NpcDef& n, const std::string& nm) { for (auto& b : n.bones) if (b.name == nm) return true; return false; };
    int fingers = 0; for (auto& b : ch.bones) if (b.name.rfind("Index", 0) == 0 || b.name.rfind("Middle", 0) == 0 || b.name.rfind("Ring", 0) == 0 || b.name.rfind("Little", 0) == 0 || b.name.rfind("Thumb", 0) == 0) fingers++;
    CHECK(fingers == 30, "the maker's hands have five fingers of three pieces each");
    bool core = true; for (auto& b : npcPreset("human").bones) core &= has(ch, b.name);
    CHECK(core, "it keeps every bone a human has (the poser finds the arms and the head by name)");
    CHECK(has(ch, "Eye (left)") && has(ch, "Ear (left)") && has(ch, "Belt") && has(ch, "Ear cup (right)") && !has(ch, "Cap"), "a face, ears and the chosen extras");
    {   // the sculpted head (its nose, sockets, chin and lips are its own shape; the hair a shell drawn over it)
        const NpcBone* hd = nullptr; for (auto& b : ch.bones) if (b.name == "Head") hd = &b;
        bool sculpted = hd && hd->body && !has(ch, "Nose") && !has(ch, "Hair");
        double nose = hd ? npcHeadFront(*hd, 0.445, 0) : 0, cheek = hd ? npcHeadFront(*hd, 0.445, 30) : 0;
        double socket = hd ? npcHeadFront(*hd, 0.565, 0.33 * hd->r0 * hd->width) : 0, brow = hd ? npcHeadFront(*hd, 0.645, 0.33 * hd->r0 * hd->width) : 0;
        double back = 0, sideX = 0, frontZ = 0; if (hd) { npcHeadPoint(*hd, 0.5, 3.14159265, sideX, back); npcHeadPoint(*hd, 0.5, 0, sideX, frontZ); }
        std::printf("  (head: nose tip %.0f mm forward, cheek beside it %.0f, eye socket %.0f under the brow %.0f, back of the head %.0f)\n", nose, cheek, socket, brow, back);
        CHECK(sculpted && nose > cheek + 15 && socket < brow - 5 && back < 0 && frontZ > 0, "the head is sculpted: the nose stands out, the eyes sit in sockets under the brow");
    }
    L.body = "female"; L.hair = "ponytail"; L.hat = true; L.glasses = true; L.top = "tank"; L.pants = "shorts";
    NpcDef fem = characterFromLook(L);
    CHECK(!has(fem, "Bust (left)") && fem.look.body == "female" && has(fem, "Ponytail") && has(fem, "Cap") && has(fem, "Lens (right)"), "a female body (the chest shaped in the one-piece body, not pieces), a ponytail, a cap and glasses");
    double hip = 0, hipF = 0; for (auto& b : ch.bones) if (b.name == "Pelvis") hip = b.r1 * b.width; for (auto& b : fem.bones) if (b.name == "Pelvis") hipF = b.r1 * b.width;
    CHECK(hipF > hip * 0.95, "a female body's hips aren't narrower");
    // your own gear stays when the choices change; the look saves and loads
    NpcBone vest; vest.name = "Vest"; vest.parent = 1; vest.color = "#333333";
    for (int i = 0; i < (int)fem.bones.size(); i++) if (fem.bones[i].name == "Chest") vest.parent = i;
    fem.bones.push_back(vest);
    L.hair = "bun";
    NpcDef again = characterFromLook(L, &fem);
    CHECK(has(again, "Vest") && has(again, "Bun") && !has(again, "Ponytail"), "gear you added stays when the choices change");
    NpcDef rt;
    CHECK(npcFromJson(npcToJson(again), rt) && rt.look.on && rt.look.body == "female" && rt.look.hair == "bun" && rt.bones.size() == again.bones.size(), "the character's choices save and load");
    NpcDef oldh; npcFromJson(npcToJson(npcPreset("human")), oldh);
    CHECK(!oldh.look.on && npcToJson(npcPreset("human")).find("\"look\"") == std::string::npos, "NPCs and older characters save as before");
    double eyeOut = 0; auto P = npcPose(ch);
    for (size_t i = 0; i < ch.bones.size(); i++) if (ch.bones[i].name == "Iris (left)") eyeOut = P[i].b[2];
    double headFront = 0; for (size_t i = 0; i < ch.bones.size(); i++) if (ch.bones[i].name == "Head") headFront = P[i].a[2] + ch.bones[i].r0 * ch.bones[i].depth * 0.9;
    CHECK(eyeOut > headFront, "the eyes sit out on the face, not inside the head");
}
static void npcsAlive() {
    std::printf("npcsAlive\n");
    gmap::Map m; m.brushes.push_back(gmap::makeBox({-50, -1, -50}, {50, 0, 50}, 0));   // a flat floor
    unsigned rng = 1;
    std::vector<npcai::Event> ev;
    gmap::V3 youFeet{10, 0, 0}, youEye{10, 1.62, 0};
    // a chaser: it sees you, walks to you, hits you
    NpcDef h = npcPreset("human"); h.behaviour = "chase"; h.attack = "melee"; h.reach = 1.2; h.damage = 25;
    std::vector<npcai::Actor> A{npcai::spawn(h, {0, 0, 0}, 0, nullptr)};
    double meleeAt = -1, d0 = 10;
    for (int k = 0; k < 600 && meleeAt < 0; k++) {
        ev.clear(); npcai::think(A, m, youEye, youFeet, true, 1.0 / 60, ev, rng);
        for (auto& e : ev) if (e.kind == npcai::Event::Melee) meleeAt = k / 60.0;
    }
    double d1 = gmap::len(gmap::V3{youFeet.x - A[0].body.feet.x, 0, youFeet.z - A[0].body.feet.z});
    std::printf("  (walked from %.0f m to %.1f m, first blow after %.1f s)\n", d0, d1, meleeAt);
    CHECK(A[0].aware && d1 < 1.3, "a chasing NPC sees you and walks up to you");
    CHECK(meleeAt > 0 && meleeAt < 9, "and strikes when you're in reach");
    // a wall between: it doesn't see you
    gmap::Map walled = m; walled.brushes.push_back(gmap::makeBox({4, 0, -10}, {4.5, 4, 10}, 0));
    std::vector<npcai::Actor> B{npcai::spawn(h, {0, 0, 0}, 0, nullptr)};
    for (int k = 0; k < 120; k++) { ev.clear(); npcai::think(B, walled, youEye, youFeet, true, 1.0 / 60, ev, rng); }
    CHECK(!B[0].aware, "with a wall in between it doesn't notice you");
    // but a gunshot carries: it hears you and comes round the wall's end
    gmap::Map shortWall = m; shortWall.brushes.push_back(gmap::makeBox({4, 0, -2}, {4.5, 4, 2}, 0));
    std::vector<npcai::Actor> B2{npcai::spawn(h, {0, 0, 0}, 0, nullptr)};
    npcai::hear(B2, youFeet, 50);
    bool struck = false;
    for (int k = 0; k < 900 && !struck; k++) { ev.clear(); npcai::think(B2, shortWall, youEye, youFeet, true, 1.0 / 60, ev, rng); for (auto& e : ev) if (e.kind == npcai::Event::Melee) struck = true; }
    std::printf("  (heard a shot, went round a 4 m wall: %s, ended %.1f m away)\n", struck ? "struck" : "didn't reach", gmap::len(youFeet - B2[0].body.feet));
    CHECK(struck, "it hears a gunshot and walks round what's in its way to reach you");
    // a shooter with a gun made for NPCs
    Design g = loadPreset("tilting"); g.npcGun.on = true; g.npcGun.rpm = 300; g.npcGun.burst = 3;
    NpcDef s = npcPreset("human"); s.behaviour = "stand"; s.attack = "ranged"; s.reach = 40;
    std::vector<npcai::Actor> C{npcai::spawn(s, {0, 0, 0}, 0, &g)};
    int shots = 0;
    for (int k = 0; k < 300; k++) { ev.clear(); npcai::think(C, m, youEye, youFeet, true, 1.0 / 60, ev, rng); for (auto& e : ev) if (e.kind == npcai::Event::Shot) shots++; }
    std::printf("  (%d shots in 5 s)\n", shots);
    CHECK(shots >= 6 && shots <= 25, "an NPC with a gun shoots at you in bursts");
    // damage: a 9 mm in the chest, and in the head
    double body = npcai::bulletDamage(0.00745, 360, false, 0), head = npcai::bulletDamage(0.00745, 360, true, 0);
    std::printf("  (9 mm: %.0f to the body, %.0f to the head)\n", body, head);
    CHECK(body > 15 && body < 40 && std::fabs(head - body * 3) < 1e-6, "damage follows the bullet's energy, weak spots x3");
    CHECK(npcai::bulletDamage(0.004, 940, false, 0.5) < npcai::bulletDamage(0.004, 940, false, 0), "armour stops its share");
    npcai::Actor v = npcai::spawn(h, {0, 0, 0}, 0, nullptr);
    bool killed = false;
    for (int k = 0; k < 10 && !killed; k++) killed = npcai::hurt(v, body, {1, 0, 0});
    CHECK(killed && v.dead, "enough hits kill it");
    std::vector<npcai::Actor> D{v};
    for (int k = 0; k < 60; k++) { ev.clear(); npcai::think(D, m, youEye, youFeet, true, 1.0 / 60, ev, rng); }
    double top = 0; for (auto& b : npcai::bones(D[0])) top = std::max({top, b.a.y, b.b.y});
    CHECK(top < 0.6, "and it falls down");
}
static void npcPaths() {
    std::printf("npcPaths\n");
    NpcDef h = npcPreset("human"); h.behaviour = "chase"; h.attack = "melee"; h.reach = 1.2; h.speed = 2.5;
    gmap::Map floor; floor.brushes.push_back(gmap::makeBox({-30, -1, -30}, {30, 0, 30}, 0));
    // run until it strikes (or time runs out); where it ended
    auto chase = [&](const gmap::Map& m, gmap::V3 start, gmap::V3 you, double secs, bool useNav, bool& struck) {
        std::vector<npcai::Actor> A{npcai::spawn(h, start, 0, nullptr)};
        nav::Cache C; unsigned rng = 3; std::vector<npcai::Event> ev; struck = false;
        for (int k = 0; k < secs * 60 && !struck; k++) {
            if (k % 120 == 0) npcai::hear(A, you, 100);   // (you keep shooting)
            ev.clear();
            npcai::think(A, m, you + gmap::V3{0, 1.62, 0}, you, true, 1.0 / 60, ev, rng, useNav ? &C : nullptr);
            for (auto& e : ev) if (e.kind == npcai::Event::Melee) struck = true;
        }
        return A[0].body.feet;
    };
    // a long wall with the way round at one end: it has to walk 14 m away from you first
    gmap::Map wall = floor;
    wall.brushes.push_back(gmap::makeBox({4, 0, -20}, {4.4, 3, 12}, 0));
    bool struck = false, struckNav = false;
    chase(wall, {0, 0, 0}, {9, 0, 0}, 30, false, struck);
    chase(wall, {0, 0, 0}, {9, 0, 0}, 30, true, struckNav);
    std::printf("  (a 32 m wall between: without paths %s, with paths %s)\n", struck ? "reached you" : "stuck", struckNav ? "reached you" : "stuck");
    CHECK(struckNav, "it finds the way round a long wall");
    // a U: the way out is behind it
    gmap::Map cup = floor;
    cup.brushes.push_back(gmap::makeBox({3, 0, -4}, {3.4, 3, 4}, 0));
    cup.brushes.push_back(gmap::makeBox({-6, 0, -4.4}, {3.4, 3, -4}, 0));
    cup.brushes.push_back(gmap::makeBox({-6, 0, 4}, {3.4, 3, 4.4}, 0));
    chase(cup, {0, 0, 0}, {8, 0, 0}, 30, false, struck);
    chase(cup, {0, 0, 0}, {8, 0, 0}, 30, true, struckNav);
    std::printf("  (in a dead end, you beyond its far wall: without paths %s, with paths %s)\n", struck ? "reached you" : "stuck", struckNav ? "reached you" : "stuck");
    CHECK(struckNav, "and out of a dead end, backwards");
    // upstairs: a platform 2.4 m up, stairs at one end
    gmap::Map up = floor;
    up.brushes.push_back(gmap::makeBox({6, 0, -3}, {10, 2.4, 5}, 0));
    for (auto& b : gmap::makeStairs({1, 0, 3.2}, {6, 2.4, 5}, 12, 0, 0)) up.brushes.push_back(b);
    gmap::V3 end = chase(up, {0, 0, -2}, {8, 2.4, 0}, 30, true, struckNav);
    std::printf("  (you upstairs: it ended at height %.2f m)\n", end.y);
    CHECK(struckNav && end.y > 2.0, "it climbs the stairs to reach you upstairs");
    // you on a box it can't climb: it comes as close as it can and waits
    gmap::Map box = floor;
    box.brushes.push_back(gmap::makeBox({6, 0, -1}, {8, 2, 1}, 0));
    end = chase(box, {0, 0, 0}, {7, 2, 0}, 12, true, struckNav);
    double gap = std::hypot(end.x - 7, end.z);
    std::printf("  (you on a 2 m box: it waits %.1f m from you)\n", gap);
    CHECK(gap < 2.0, "out of reach, it waits as close as it can get");
    // down: it steps off a ledge to come to you
    gmap::Map ledge = floor;
    ledge.brushes.push_back(gmap::makeBox({-4, 0, -4}, {4, 1.5, 4}, 0));
    end = chase(ledge, {0, 1.5, 0}, {9, 0, 0}, 20, true, struckNav);
    CHECK(struckNav, "it drops off a ledge to reach you below");
    // the grids are quick to make: the practice range
    auto t0 = std::chrono::steady_clock::now();
    nav::Grid G = nav::build(gmap::builtinRange(), {0.2, 1.8, 0.45});
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  (practice range grid: %zu spots, %.0f ms)\n", G.nodes(), ms);
    CHECK(G.nodes() > 20000 && ms < 2000, "a map's path grid takes moments to make");
    // a crowd: the limit's worth of NPCs chasing you round the practice range stays quick
    {
        gmap::Map R = gmap::builtinRange();
        std::vector<npcai::Actor> A;
        for (int i = 0; i < npcai::NPC_LIMIT; i++) A.push_back(npcai::spawn(h, {15.0 + (i % 10) * 3, 0, -9.0 + (i / 10) * 2}, 180, nullptr));
        nav::Cache C; unsigned rng = 1; std::vector<npcai::Event> ev;
        for (auto& a : A) C.get(R, npcai::agentOf(a));
        double total = 0;
        for (int k = 0; k < 300; k++) {
            auto t1 = std::chrono::steady_clock::now();
            ev.clear(); npcai::think(A, R, {-3, 1.62, 0}, {-3, 0, 0}, true, 1.0 / 60, ev, rng, &C);
            total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        }
        std::printf("  (%d NPCs chasing you: %.2f ms a frame)\n", npcai::NPC_LIMIT, total / 300);
        CHECK(total / 300 < 8, "the most NPCs a map can have think and move in well under a frame");
    }
}
static void armFreedom() {
    std::printf("armFreedom\n");
    Design d = loadPreset("boltaction");
    AnimKey k1; k1.t = 0.6; k1.firing = d.pose.firing; k1.support = d.pose.support;
    k1.support.on = false; k1.support.free = true; k1.support.pos[0] = -120; k1.support.pos[1] = -470; k1.support.pos[2] = -170; k1.support.twist = 30;
    k1.firing.elbow = 40; k1.firing.dz = 6;
    AnimKey k2 = k1; k2.t = 1.3; k2.support = d.pose.support;
    d.anims["reload"].keys = {k1, k2};
    Design r;
    CHECK(designFromJson(designToJson(d, true), r), "a gun with placed hands loads");
    const AnimKey& rk = r.anims["reload"].keys[0];
    CHECK(rk.support.free && !rk.support.on && rk.support.pos[1] == -470 && rk.support.twist == 30, "a hand placed off the gun keeps its place and roll");
    CHECK(rk.firing.elbow == 40 && rk.firing.dz == 6 && rk.firing.on, "the elbow's swing and the hand's lift are kept");
    AnimHands h = animAt(r, r.anims["reload"], 0.3);
    CHECK(h.uS > 0 && h.uS < 1 && h.toS.free, "on its way off the gun, the support hand is between the gun and its place");
    CHECK(h.firing.elbow > 0 && h.firing.elbow < 40, "the elbow swings smoothly toward its key");
    // older blueprints: no new fields, nothing changes
    Design old = loadPreset("boltaction"), o2;
    designFromJson(designToJson(old, true), o2);
    CHECK(o2.pose.firing.elbow == 0 && o2.pose.firing.twist == 0 && o2.pose.firing.dz == 0 && !o2.pose.support.free, "older poses load unchanged");
}
static void cartridgeSharing() {
    std::printf("cartridgeSharing\n");
    Cartridge c = cartridgePresets()[0];
    c.name = "Test round"; c.velocity = 433; c.nose = "spitzer"; c.custom = {{1, 2}, {3, 4}};
    std::string code = cartridgeShareCode(c);
    std::printf("  (share code: %zu characters)\n", code.size());
    Cartridge a, b, d;
    CHECK(cartridgeFromText(code, a) && a.name == "Test round" && a.velocity == 433 && a.custom.size() == 2, "a share code brings the cartridge back exactly");
    CHECK(cartridgeFromText("  " + code + "\n", d) && d.name == c.name, "with spaces round it too (pasted from a message)");
    CHECK(cartridgeFromText(cartridgeFileText(c), b) && b.nose == c.nose && b.bulletMass == c.bulletMass, "an exported file brings it back");
    Cartridge x;
    CHECK(!cartridgeFromText("GS3D-CART1:notacode!!", x) && !cartridgeFromText("hello", x), "anything else isn't taken for a cartridge");
}
static void controlButtons() {
    std::printf("controlButtons\n");
    // a frame, and a button on a short track: it can only slide 4 mm forward from where it sits
    auto make = [](double dir) {
        Design d;
        BodyDef f; f.id = "frame"; f.name = "Frame"; f.fixed = true; f.layer = "B"; f.pts = {{-50, -50}, {50, -50}, {50, 50}, {-50, 50}}; fixShape(f);
        BodyDef b; b.id = "btn"; b.name = "Bolt release"; b.layer = "A"; b.pts = {{0, 0}, {10, 0}, {10, 6}, {0, 6}}; b.thick = 4; fixShape(b);
        b.ctrl = "button"; b.key = "k1"; b.ctrlPt = {5, 3}; b.ctrlDir = dir; b.ctrlForce = 25;
        JointDef j; j.type = "track"; j.a = "btn"; j.b = "frame"; j.p = {5, 3}; j.path = {{5, 3}, {9, 3}}; j.lockRot = true;
        d.bodies = {f, b}; d.joints = {j};
        return d;
    };
    double td = 0;
    Design d0 = make(180);
    CHECK(trackDirection(d0, "btn", td) && std::fabs(td) < 1e-9, "a button's track tells which way it's pushed (forward here)");
    auto press = [](const Design& d) {
        auto S = sim::build(d);
        sim::run(*S, 0.05);
        sim::Body* B = S->find("btn");
        double x0 = B->x;
        sim::setKey(*S, "k1", true);
        sim::run(*S, 0.4);
        double moved = (B->x - x0) * 1000;
        double held = 0, along = 0;
        for (auto& p : S->pushers) if (p.key == "k1") { held = p.heldT; along = p.moved * 1000; }
        return std::array<double, 3>{moved, held, along};
    };
    auto wrong = press(d0), right = press(make(td));
    std::printf("    pushed back: moved %.2f mm; pushed along its track: moved %.2f mm\n", wrong[0], right[0]);
    CHECK(std::fabs(wrong[0]) < 0.3 && wrong[1] > 0.35 && wrong[2] < 0.3, "pushed a way it can't go, the key does nothing (and the game can tell)");
    CHECK(right[0] > 3.5, "pushed along its track, the key slides it all the way");
}

// Mirrored cuts: the same shape, the other way round, and mirroring twice gives the cut back
static void mirroredCuts() {
    std::printf("Mirrored cuts\n");
    CarveDef r; r.shape = "rect"; r.at = {131, -3}; r.atZ = 10; r.dx = 0; r.dy = 0; r.dz = -1; r.spin = -13; r.w = 3; r.h = 15.5; r.depth = 0.7;
    CarveDef l = carve::mirrored(r, 2, 0);
    CHECK(std::fabs(l.atZ + 10) < 1e-9 && std::fabs(l.dz - 1) < 1e-9 && std::fabs(std::remainder(l.spin - 13, 180)) < 1e-6,   // (a rectangle turned half round is the same rectangle)
          "a slit on the right, mirrored across: the same slit on the left (as drawn by hand on the SOG P300)");
    for (const char* shape : {"rect", "tri", "half", "circle"})
        for (int axis = 0; axis < 3; axis++) {
            CarveDef c; c.shape = shape; c.at = {40, 12}; c.atZ = 3; c.dx = 0.3; c.dy = -0.4; c.dz = -std::sqrt(0.75); c.spin = 27; c.w = 8; c.h = 5;
            CarveDef m = carve::mirrored(c, axis, axis == 2 ? 0 : 25);
            // every outline point of the mirror is the reflection of an outline point of the original
            auto pts = [](const CarveDef& k) {
                carve::P3 u, v, d; carve::frame(k, u, v, d); std::vector<carve::P3> o;
                for (auto& q : carve::shape2D(k, 24)) o.push_back(carve::P3{k.at.x, k.at.y, k.atZ} + u * q.x + v * q.y);
                return o;
            };
            auto a = pts(c), b = pts(m);
            double worst = 0;
            for (auto& q : a) {
                carve::P3 rq = q;
                if (axis == 0) rq.x = 50 - q.x; else if (axis == 1) rq.y = 50 - q.y; else rq.z = -q.z;
                double best = 1e9;
                for (auto& w : b) best = std::min(best, std::sqrt((w.x - rq.x) * (w.x - rq.x) + (w.y - rq.y) * (w.y - rq.y) + (w.z - rq.z) * (w.z - rq.z)));
                worst = std::max(worst, best);
            }
            CarveDef back = carve::mirrored(m, axis, axis == 2 ? 0 : 25);
            bool same = std::fabs(back.at.x - c.at.x) < 1e-9 && std::fabs(back.at.y - c.at.y) < 1e-9 && std::fabs(back.atZ - c.atZ) < 1e-9 &&
                        std::fabs(back.dx - c.dx) < 1e-9 && std::fabs(back.dy - c.dy) < 1e-9 && std::fabs(back.dz - c.dz) < 1e-9 &&
                        std::fabs(std::remainder(back.spin - c.spin, 360)) < 1e-6;
            if (worst > 1e-6 || !same) CHECK(false, (std::string(shape) + " mirrored on axis " + std::to_string(axis) + ": the exact reflection, and back again").c_str());
        }
    CHECK(true, "every shape, mirrored on X, Y and Z, is the exact reflection, and mirroring twice gives it back");
}

// Picatinny rails and attachment points: real slots at the standard spacing, kept on save, and old blueprints unchanged
static void railsAndMounts() {
    std::printf("Rails and attachment points\n");
    BodyDef r = mounts::makeRail({0, 0}, {100, 0}, 0, 0);
    auto sl = mounts::railSlots(r);
    int slots = 0; for (auto& c : r.carves) if (c.name == "Slot") slots++;
    CHECK(sl.size() == 10 && slots == 10 && std::fabs(sl[1] - sl[0] - 10.01) < 1e-9, "a 100 mm rail: 10 slots, 10.01 mm apart (MIL-STD-1913)");
    bool along = true, onTop = true;
    for (auto& c : r.carves) if (c.name == "Slot") {
        carve::P3 u, v, d; carve::frame(c, u, v, d);
        if (std::fabs(std::fabs(u.x) - 1) > 1e-6) along = false;          // its width runs along the rail
        if (std::fabs(c.at.y + 9.4) > 1e-9 || d.y < 0.99) onTop = false;   // cut from the top face, straight down
    }
    CHECK(along && onTop, "the slots are cut across the top, straight down");
    double ymin = 1e9, ymax = -1e9; for (auto& p : r.pts) { ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y); }
    CHECK(std::fabs(ymin + 9.4) < 1e-9 && std::fabs(ymax) < 1e-9 && std::fabs(r.thick - 21.2) < 1e-9, "the bar: 21.2 mm wide, 9.4 mm tall, standing on the line drawn");
    BodyDef angled = mounts::makeRail({0, 0}, {60, -60}, 0, 0);
    bool alongA = true;
    for (auto& c : angled.carves) if (c.name == "Slot") { carve::P3 u, v, d; carve::frame(c, u, v, d); if (std::fabs(std::fabs((u.x - u.y) * std::sqrt(0.5)) - 1) > 1e-6) alongA = false; }
    CHECK(alongA, "a rail drawn at an angle: its slots still run across it");
    BodyDef base; base.z = 0; base.thick = 30;
    BodyDef side = mounts::makeRail({0, 20}, {80, 20}, 2, mounts::sideRailZ(base, 2));
    CHECK(std::fabs(side.z - (15 + 4.7)) < 1e-9 && std::fabs(side.thick - 9.4) < 1e-9, "a side rail lies flat against the part's right side");
    Design d = loadPreset("bolt");
    BodyDef rr = mounts::makeRail({20, -30}, {120, -30}, 0, 0); rr.id = "rail1"; rr.rail.base = "x";
    MountDef m; m.type = "Bayonet lug"; m.at = {200, 10}; m.atZ = 0; m.nx = 0; m.ny = 1; m.nz = 0; m.spin = 15;
    rr.mounts.push_back(m);
    d.bodies.push_back(rr);
    Design back;
    bool ok = designFromJson(designToJson(d), back);
    const BodyDef* b2 = nullptr; for (auto& b : back.bodies) if (b.id == "rail1") b2 = &b;
    CHECK(ok && b2 && b2->rail.on && b2->rail.face == 0 && b2->rail.base == "x" && b2->mounts.size() == 1 && b2->mounts[0].type == "Bayonet lug" && std::fabs(b2->mounts[0].spin - 15) < 1e-9,
          "rails and attachment points are saved with the gun");
    Design plain = loadPreset("bolt");
    CHECK(designToJson(plain).find("\"rail\"") == std::string::npos && designToJson(plain).find("\"mounts\"") == std::string::npos, "guns without them save as before");
    {   // a rail welded on top of the bolt action's receiver: the gun works just the same
        Design g = loadPreset("boltaction");
        BodyDef top = mounts::makeRail({-60, 9}, {100, 9}, 0, 0); top.id = "railtop"; top.rail.base = "rcv"; top.fixed = true;
        g.bodies.push_back(top); weldParts(g, "railtop", "rcv");
        sim::ReliabilityOptions o; o.rounds = 5;
        auto R0 = sim::runReliability(loadPreset("boltaction"), o), R1 = sim::runReliability(g, o);
        CHECK(R1.fired == R0.fired && R1.fired > 0, "a rail on the receiver: the bolt action still feeds and fires as before");
    }
    mounts::regenRail(rr);
    CHECK(rr.id == "rail1" && rr.mounts.size() == 1 && rr.rail.base == "x", "rebuilding a rail keeps what's attached to it");
}

// Attachments: saved apart from guns' data, read back whole; worked-out mount spot and lenses
static void attachmentsData() {
    std::printf("Attachments\n");
    Design a; a.name = "Red dot";
    BodyDef body; body.id = "b"; body.pts = {{0, -30}, {40, -30}, {40, 0}, {0, 0}}; body.thick = 25; a.bodies.push_back(body);
    a.attach.on = true; a.attach.job = "optic"; a.attach.mount = "Picatinny"; a.attach.clamp = 25;
    a.attach.optic.kind = "reflex";
    autoAttach(a);
    CHECK(std::fabs(a.attach.at.x - 20) < 1e-9 && std::fabs(a.attach.at.y) < 1e-9, "where it mounts: worked out as the middle of its underside");
    CHECK(std::fabs(a.attach.optic.rear.x) < 1e-9 && std::fabs(a.attach.optic.front.x - 40) < 1e-9 && std::fabs(a.attach.optic.rear.y + 15) < 1e-9, "its lenses: worked out at its ends, through its longest part");
    CHECK(!a.attach.optic.reticle.empty(), "a reflex sight starts with a dot and ring");
    a.attach.optic.reticle.push_back({0, -1, 0, 1, 0, 0.2});
    a.attach.optic.mag = 4; a.attach.optic.magMax = 12; a.attach.optic.ffp = true;
    Design b;
    bool ok = designFromJson(designToJson(a), b);
    CHECK(ok && b.attach.on && b.attach.job == "optic" && std::fabs(b.attach.clamp - 25) < 1e-9 && b.attach.optic.reticle.size() == a.attach.optic.reticle.size() &&
          std::fabs(b.attach.optic.magMax - 12) < 1e-9 && b.attach.optic.ffp, "an attachment saves and loads whole (job, mount, optic, reticle)");
    Design g = loadPreset("boltaction");
    CHECK(designToJson(g).find("\"attachment\"") == std::string::npos, "guns save as before");
    // lights, lasers, bayonets: their settings save and load; a loadout keeps what's switched off
    Design l = a; l.attach.job = "light"; l.attach.beamDeg = 30; l.attach.lumens = 1200; l.attach.optic.lensSet = true; l.attach.optic.front = {55, -12};
    Design l2;
    ok = designFromJson(designToJson(l), l2);
    CHECK(ok && std::fabs(l2.attach.beamDeg - 30) < 1e-9 && std::fabs(l2.attach.lumens - 1200) < 1e-9 && l2.attach.optic.lensSet && std::fabs(l2.attach.optic.front.x - 55) < 1e-9,
          "a light keeps its beam width, brightness and beam line");
    CHECK(beamColorOf(l2.attach) == 0xFFF2DCFFu, "a light shines warm white unless coloured");
    Design z = a; z.attach.job = "laser"; z.attach.beamColor = 0x30FF50FFu;
    Design z2; designFromJson(designToJson(z), z2);
    CHECK(beamColorOf(z2.attach) == 0x30FF50FFu, "a laser keeps its colour");
    Design k = a; k.attach.job = "bayonet"; k.attach.stab = 70;
    Design k2; designFromJson(designToJson(k), k2);
    CHECK(std::fabs(k2.attach.stab - 70) < 1e-9, "a bayonet keeps its stab damage");
    std::vector<fit::Item> its(2); its[0].file = "Light"; its[0].on = false; its[1].file = "Dot";
    std::string lj = fit::loadoutToJson("Gun", its), gn;
    std::vector<fit::LoadoutEntry> es;
    CHECK(fit::loadoutFromJson(lj, gn, es) && es.size() == 2 && !es[0].on && es[1].on, "a loadout keeps which attachments are switched off");
}

// Fitting attachments: where they sit, and every reason one may not go there
static void fittingAttachments() {
    std::printf("Fitting attachments\n");
    Design gun = loadPreset("boltaction");
    BodyDef rail = mounts::makeRail({-60, 9}, {100, 9}, 0, 0); rail.id = "rail1"; rail.rail.base = "rcv"; rail.fixed = true;
    gun.bodies.push_back(rail); weldParts(gun, "rail1", "rcv");
    MountDef lug; lug.type = "Bayonet lug"; lug.at = {395, 27}; lug.nx = 0; lug.ny = 1; lug.nz = 0;
    gun.bodies[gun.bodyIndex("barrel")].mounts.push_back(lug);
    Design dot; dot.name = "Red dot";
    { BodyDef h; h.id = "h"; h.pts = {{0, -38}, {45, -38}, {45, -10}, {0, -10}}; h.thick = 30; dot.bodies.push_back(h);
      BodyDef b; b.id = "b"; b.pts = {{8, -10}, {40, -10}, {40, 0}, {8, 0}}; b.thick = 24; dot.bodies.push_back(b); }
    dot.attach.on = true; dot.attach.job = "optic"; dot.attach.clamp = 22; autoAttach(dot);
    std::vector<fit::Item> items;
    auto ss = fit::sites(gun, items);
    const fit::Site* rs = nullptr; const fit::Site* ls = nullptr;
    for (auto& x : ss) { if (x.rail) rs = &x; else ls = &x; }
    CHECK(rs && ls && ss.size() == 2, "the gun offers its rail and its bayonet lug");
    double mid = fit::snapAlong(*rs, 80);
    fit::Xf xf = fit::place(dot, *rs, mid);
    fit::P3 g = xf.apply({dot.attach.at.x, dot.attach.at.y, dot.attach.atZ}, {dot.attach.at.x, dot.attach.at.y, dot.attach.atZ});
    CHECK(std::fabs(g.y - (9 - 9.4)) < 1e-9 && std::fabs(g.x - (-60 + mid)) < 1e-9 && std::fabs(xf.R[0][0] - 1) < 1e-9 && std::fabs(xf.R[1][1] - 1) < 1e-9,
          "on the rail: its clamp sits on the rail's top, upright, at a slot");
    { bool onSlot = false; for (double v : rs->slots) if (std::fabs(v - mid) < 1e-9) onSlot = true;
      CHECK(onSlot && std::fabs(mid - 80) <= 10.01 / 2 + 1e-6, "clamps snap to the rail's nearest slot"); }
    CHECK(fit::check(gun, items, dot, *rs, mid).empty(), "a red dot on the rail: it fits");
    CHECK(!fit::check(gun, items, dot, *rs, rs->slots[0]).empty(), "at the very end: its clamp runs off the rail");
    fit::Item it; it.file = "dot"; it.att = dot; it.site = rs->key(); it.along = mid; it.xf = xf; items.push_back(it);
    std::string why = fit::check(gun, items, dot, *rs, fit::snapAlong(*rs, mid + 10));
    CHECK(why.find("runs into") != std::string::npos, "a second one right next to it: they would overlap");
    CHECK(fit::check(gun, items, dot, *rs, fit::snapAlong(*rs, mid - 60)).empty(), "far enough along: both fit");
    CHECK(!fit::check(gun, items, dot, *ls, 0).empty(), "a clamp-on sight on a bayonet lug: the wrong kind of mount");
    Design bay; bay.name = "Bayonet";
    { BodyDef bl; bl.id = "bl"; bl.pts = {{0, 0}, {200, 0}, {200, 12}, {0, 12}}; bl.thick = 4; bay.bodies.push_back(bl); }
    bay.attach.on = true; bay.attach.job = "bayonet"; bay.attach.mount = "bayonet LUG "; bay.attach.face = "top"; autoAttach(bay);
    CHECK(fit::check(gun, items, bay, *ls, 0).empty(), "a bayonet on the lug (kinds match whatever the capitals): it fits, hanging under the barrel");
    // a slide on a track that runs forward over the rail: an attachment there would be in its way
    Design g2; { BodyDef fr; fr.id = "frame"; fr.fixed = true; fr.pts = {{-100, 0}, {150, 0}, {150, 30}, {-100, 30}}; fr.thick = 20; g2.bodies.push_back(fr);
                 BodyDef sl; sl.id = "slide"; sl.name = "Slide"; sl.pts = {{-90, -40}, {-50, -40}, {-50, -12}, {-90, -12}}; sl.thick = 20; g2.bodies.push_back(sl);
                 BodyDef r2 = mounts::makeRail({0, 0}, {100, 0}, 0, 0); r2.id = "r2"; r2.fixed = true; g2.bodies.push_back(r2);
                 JointDef j; j.type = "track"; j.a = "slide"; j.b = "frame"; j.p = {-70, -26}; j.path = {{-70, -26}, {40, -26}}; g2.joints.push_back(j); }
    auto s2 = fit::sites(g2, {});
    std::string w2 = fit::check(g2, {}, dot, s2[0], fit::snapAlong(s2[0], 30));
    CHECK(w2.find("in the way of the Slide") != std::string::npos, "under a slide's travel: in the way of the moving part");
    // a rail adapter on the lug: it brings its own rail
    Design ad; ad.name = "Adapter";
    { BodyDef ab; ab.id = "ab"; ab.pts = {{0, 0}, {60, 0}, {60, 8}, {0, 8}}; ab.thick = 10; ad.bodies.push_back(ab);
      BodyDef ar = mounts::makeRail({0, 20}, {60, 20}, 1, 0); ar.id = "ar"; ad.bodies.push_back(ar); }
    ad.attach.on = true; ad.attach.job = "adapter"; ad.attach.mount = "Bayonet lug"; ad.attach.face = "top"; autoAttach(ad);
    std::vector<fit::Item> two;
    fit::Item ai; ai.file = "adapter"; ai.att = ad; ai.site = ls->key(); ai.xf = fit::place(ad, *ls, 0); two.push_back(ai);
    auto s3 = fit::sites(gun, two);
    bool found = false; for (auto& x : s3) if (x.owner == 0 && x.rail) found = true;
    CHECK(found, "a rail adapter, once fitted, offers its own rail");
    {   // their weight is real: on a blowback pistol's slide it slows the slide; on the frame the action runs as before
        auto peak = [](Design d) {
            auto S = sim::build(d);
            sim::run(*S, 0.003);
            S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true);
            double v = 0;
            for (int i = 0; i < 300; i++) {
                sim::run(*S, 0.0001);
                v = std::max(v, -S->find("slide")->vx);
                if (i == 150) { sim::setKey(*S, "fire", false); S->trigger = false; }
            }
            return v;
        };
        Design p0 = loadPreset("blowback"), pS = p0, pF = p0;
        pS.addMass.push_back({"slide", 150});
        pF.addMass.push_back({"grip_l", 150});
        double v0 = peak(p0), vS = peak(pS), vF = peak(pF);
        std::printf("    slide speed: plain %.2f m/s, 150 g on the slide %.2f, 150 g on the frame %.2f\n", v0, vS, vF);
        CHECK(vS < v0 * 0.93 && std::fabs(vF - v0) < v0 * 0.02, "150 g on the slide slows it; on the frame it changes nothing");
    }
    {   // materials: a slide's weight follows its material, and so does how fast the action runs; old designs are steel
        auto peak = [](Design d) {
            auto S = sim::build(d);
            sim::run(*S, 0.003);
            S->pressed = true; S->trigger = true; sim::setKey(*S, "fire", true);
            double v = 0;
            for (int i = 0; i < 300; i++) {
                sim::run(*S, 0.0001);
                v = std::max(v, -S->find("slide")->vx);
                if (i == 150) { sim::setKey(*S, "fire", false); S->trigger = false; }
            }
            return v;
        };
        // the whole slide: every part welded to it, each weighed by its material (a typed-in weight would override it)
        Design st = loadPreset("blowback");
        std::set<std::string> ids;
        {
            auto S = sim::build(st);
            sim::Body* sl = S->find("slide"); sim::Body* root = sl->root ? sl->root : sl;
            for (auto& b : S->bodies) if (&b == root || b.root == root) ids.insert(b.d ? b.d->id : b.id);
        }
        for (auto& b : st.bodies) if (ids.count(b.id)) b.mass = 0;
        Design al = st, br = st;
        for (auto& b : al.bodies) if (ids.count(b.id)) b.material = "aluminum";
        for (auto& b : br.bodies) if (ids.count(b.id)) b.material = "brass";
        double vSt = peak(st), vAl = peak(al), vBr = peak(br);
        std::printf("    slide speed: steel %.2f m/s, brass %.2f, aluminum %.2f\n", vSt, vBr, vAl);
        CHECK(vAl > vSt * 1.3 && vBr < vSt, "an aluminum slide (a third the weight) flies back much faster; a brass one a little slower");
        Design plain = loadPreset("blowback"); Design back;
        CHECK(designToJson(plain).find("\"material\"") == std::string::npos && designFromJson(designToJson(al), back) &&
              [&] { for (auto& b : back.bodies) if (b.id == "slide") return b.material == "aluminum"; return false; }(), "materials save and load; older designs save as before (steel)");
        CHECK(std::fabs(densityOf(BodyDef()) - 7.85e-6) < 1e-12 && materialOf("wood").density < 1 && materialOf("rubber").friction > 2, "no material is steel; wood floats; rubber grips");
    }
    {   // a suppressor screwed on a muzzle thread (its back end against a point facing forward): it carries on forward
        Design sup; sup.name = "Suppressor";
        { BodyDef b; b.id = "can"; b.pts = {{0, -15}, {150, -15}, {150, 15}, {0, 15}}; b.round = true; b.thick = 30; sup.bodies.push_back(b); }
        sup.attach.on = true; sup.attach.job = "muzzle"; sup.attach.muzzleKind = "suppressor"; sup.attach.mount = "Muzzle thread"; sup.attach.face = "rear"; autoAttach(sup);
        Design g3 = loadPreset("boltaction");
        MountDef th; th.type = "Muzzle thread"; th.at = {420, 20}; th.nx = 1; th.ny = 0; th.nz = 0;
        g3.bodies[g3.bodyIndex("barrel")].mounts.push_back(th);
        auto s3 = fit::sites(g3, {});
        fit::Xf x3 = fit::place(sup, s3[0], 0);
        fit::P3 tip = x3.apply({150, 0, sup.attach.atZ}, sup.bodies.empty() ? fit::P3{} : fit::P3{sup.attach.at.x, sup.attach.at.y, sup.attach.atZ});
        CHECK(std::fabs(tip.x - 570) < 1e-6 && std::fabs(tip.y - 20) < 1e-6 && fit::check(g3, {}, sup, s3[0], 0).empty(),
              "a suppressor on a muzzle thread: it goes on straight forward from the muzzle, 150 mm long");
        Design back;
        CHECK(designFromJson(designToJson(sup), back) && back.attach.muzzleKind == "suppressor" && back.attach.face == "rear", "a muzzle device keeps its kind and the side that meets the mount");
    }
    std::string gname; std::vector<fit::LoadoutEntry> le;
    CHECK(fit::loadoutFromJson(fit::loadoutToJson("Bolt-action rifle", items), gname, le) && gname == "Bolt-action rifle" && le.size() == 1 && le[0].site == rs->key() && std::fabs(le[0].along - mid) < 1e-9,
          "a loadout saves which attachment goes where");
}

// Shot sounds come from the cartridge and barrel: the recordings play as recorded for their own rounds, bigger is lower
static void hollowParts() {
    std::printf("hollowParts\n");
    // a fixed tube (100 x 20 mm side, 20 thick), open at both ends, and a slider pushed along a track through it
    auto rig = [](bool hollow, double wall, double topBottom) {
        Design d;
        BodyDef t; t.id = "tube"; t.fixed = true; t.layer = "A"; t.thick = 20; t.pts = {{0, 0}, {100, 0}, {100, 20}, {0, 20}};
        t.rad = {0, 0, 0, 0}; t.bulge = {0, 0, 0, 0};
        if (hollow) { t.hollow.on = true; t.hollow.wall = wall; t.hollow.sides = wall; t.hollow.edges = {topBottom, 0, topBottom, 0}; }
        BodyDef s; s.id = "slider"; s.layer = "A"; s.thick = 8; s.pts = {{-60, 7}, {-30, 7}, {-30, 13}, {-60, 13}};
        s.rad = {0, 0, 0, 0}; s.bulge = {0, 0, 0, 0};
        d.bodies = {t, s};
        JointDef j; j.type = "track"; j.a = "slider"; j.b = "world"; j.p = {-45, 10}; j.path = {{-45, 10}, {135, 10}};
        d.joints = {j};
        return d;
    };
    auto travel = [](const Design& d) {
        auto S = sim::build(d);
        sim::Body* b = S->find("slider");
        S->hand = {true, b, {0, 0}, {0.14, 0.010}};
        sim::run(*S, 0.8);
        return (b->x - b->x0) * 1000;
    };
    double solid = travel(rig(false, 0, 0)), hollow = travel(rig(true, 3, -1)), thick = travel(rig(true, 3, 8));
    std::printf("  (slider travel: into a solid block %.0f mm, through the hollow tube %.0f mm, through one with 8 mm top and bottom walls %.0f mm)\n", solid, hollow, thick);
    CHECK(solid < 60, "a slider is stopped by a solid block");
    CHECK(hollow > 160, "made hollow (3 mm walls, open ends), the block is a tube and the slider runs right through it");
    CHECK(thick < 40, "with 8 mm top and bottom walls the opening is only 4 mm high: the 6 mm slider can't get in");
    {   // the walls carry the weight: 100 x 20 x 20 solid, less the inside 100 x 14 x 14 (open ends: it runs the full length)
        auto weigh = [](const Design& d) {
            auto S = sim::build(d);
            const sim::Body* t = S->find("tube");
            double m = 0;
            for (auto& b : S->bodies) if (&b == t || b.root == t) m += b.m;
            return m * 1000;
        };
        double m = weigh(rig(true, 3, -1)), solidM = weigh(rig(false, 0, 0));
        double want = (100 * 20 * 20 - 100 * 14 * 14) * 7.85e-3;
        std::printf("  (tube %.1f g, solid block %.1f g; the walls alone should weigh %.1f g)\n", m, solidM, want);
        CHECK(std::fabs(m - want) < want * 0.03 && std::fabs(solidM - 314) < 3, "a hollow part weighs only its walls");
    }
    {   // an L-shaped outline (concave): its inside comes in convex pieces that cover it
        BodyDef L; L.id = "l"; L.thick = 10; L.pts = {{0, 0}, {60, 0}, {60, 20}, {20, 20}, {20, 60}, {0, 60}};
        L.hollow.on = true; L.hollow.wall = 2;
        auto in = carve::hollowInside(L);
        auto pc = carve::hollowPieces(L);
        double ai = 0, ap = 0;
        auto area = [](const std::vector<V2>& q) { double a = 0; for (size_t i = 0; i < q.size(); i++) a += q[i].x * q[(i + 1) % q.size()].y - q[(i + 1) % q.size()].x * q[i].y; return std::fabs(a) / 2; };
        ai = area(in); for (auto& q : pc) ap += area(q);
        std::printf("  (L shape: inside %.1f mm2 in %zu convex pieces covering %.1f)\n", ai, pc.size(), ap);
        CHECK(in.size() == 6 && std::fabs(ai - (56 * 16 + 16 * 40)) < 0.5 && pc.size() >= 2 && std::fabs(ap - ai) < 0.5, "a concave part's inside: its outline brought in by the wall, split into convex pieces that cover it exactly");
    }
    {   // saved and read back
        Design d = rig(true, 2.5, 6), back; designFromJson(designToJson(d), back);
        const auto& h = back.bodies[0].hollow;
        CHECK(h.on && std::fabs(h.wall - 2.5) < 1e-9 && h.edges.size() == 4 && h.edges[1] == 0 && h.edges[0] == 6, "a hollow part's walls are saved in the blueprint");
        Design plain = rig(false, 0, 0); designFromJson(designToJson(plain), back);
        CHECK(!back.bodies[0].hollow.on, "a plain part stays solid");
    }
}

// Cheats: auto feed makes a gun with a bad feed path feed; brute force keeps a self-loader that can't run itself going,
// and works a bolt action by itself. Reports say they were on. (Switched off again after.)
// Fire control groups (0.8.39): new guns need one (a part marked, or drawn fire control); the selector picks the mode
static void fireControlTest() {
    std::printf("\n== fire control groups\n");
    sim::ReliabilityOptions o; o.rounds = 6; o.stallSeconds = 3;
    Design rv = loadPreset("revolver");
    std::string why;
    CHECK(rv.fcgRequired && hasFireControl(rv), "the revolver example needs a fire control group, and its trigger is one");
    auto r0 = sim::runReliability(rv, o);
    Design none = rv; for (auto& b : none.bodies) b.fcg = false;
    auto r1 = sim::runReliability(none, o);
    Design old = none; old.fcgRequired = false;   // (a blueprint from before: it never needed one)
    auto r2 = sim::runReliability(old, o);
    CHECK(r0.fired == 6 && r1.fired == 0 && r2.fired == 6 && !hasFireControl(none, &why) && !why.empty(),
          ("with its fire control group it fires (" + std::to_string(r0.fired) + "/6), without it doesn't (" + std::to_string(r1.fired) + "), an older blueprint still does (" + std::to_string(r2.fired) + ")").c_str());
    Design safe = rv; safe.breech.selector = {"safe", "semi"}; safe.breech.fireMode = "safe";
    CHECK(sim::runReliability(safe, o).fired == 0, "on safe the trigger does nothing");
    CHECK(nextSelector(safe.breech) == "semi" && (safe.breech.fireMode = "semi", nextSelector(safe.breech)) == "safe" && nextSelector(BreechDef()).empty(),
          "the selector goes round its positions (and a gun without one has none)");
    Design back;
    CHECK(designFromJson(designToJson(safe), back) && back.fcgRequired && back.breech.selector.size() == 2 && back.breech.fireMode == "semi" &&
          std::any_of(back.bodies.begin(), back.bodies.end(), [](const BodyDef& b) { return b.fcg; }), "saved and loaded: the fire control group, the selector and its position");
    Design drawn = loadPreset("blowback"); for (auto& b : drawn.bodies) b.fcg = false;   // (its hammer and sear are drawn parts)
    CHECK(drawn.breech.ignition == "striker" && hasFireControl(drawn), "drawn fire control needs no marked group");
    Design blank = loadPreset("blank");
    CHECK(blank.fcgRequired, "a new gun needs a fire control group");
}
// Editor layers (0.8.40): parts grouped to hide or lock while editing; saved; the physics ignores them

// A modelled character (as a 3D artist would make one: a T-pose, Mixamo's joint names) fitted onto a person's bones.
static void rigTest() {
    std::printf("\n== modelled characters on the bones\n");
    std::vector<rig::Joint> J;
    auto add = [&](const char* nm, const char* parent, double x, double y, double z) {
        rig::Joint j; j.name = nm; j.bind.p[0] = x; j.bind.p[1] = y; j.bind.p[2] = z;
        for (int i = 0; i < (int)J.size(); i++) if (J[i].name == parent) j.parent = i;
        J.push_back(j);
    };
    add("Armature", "", 0, 0, 0);
    add("mixamorig:Hips", "Armature", 0, 1.0, 0);
    add("mixamorig:Spine", "mixamorig:Hips", 0, 1.1, 0); add("mixamorig:Spine1", "mixamorig:Spine", 0, 1.2, 0); add("mixamorig:Spine2", "mixamorig:Spine1", 0, 1.32, 0);
    add("mixamorig:Neck", "mixamorig:Spine2", 0, 1.5, 0); add("mixamorig:Head", "mixamorig:Neck", 0, 1.6, 0); add("mixamorig:HeadTop_End", "mixamorig:Head", 0, 1.8, 0);
    for (int sgn : {1, -1}) {   // (glTF: the model's left is +x)
        std::string S = sgn > 0 ? "Left" : "Right";
        auto N = [&](const char* w) { return "mixamorig:" + S + w; };
        add(N("Shoulder").c_str(), "mixamorig:Spine2", sgn * 0.06, 1.45, 0);
        add(N("Arm").c_str(), N("Shoulder").c_str(), sgn * 0.18, 1.45, 0);
        add(N("ForeArm").c_str(), N("Arm").c_str(), sgn * 0.46, 1.45, 0);
        add(N("Hand").c_str(), N("ForeArm").c_str(), sgn * 0.72, 1.45, 0);
        add(N("HandMiddle1").c_str(), N("Hand").c_str(), sgn * 0.82, 1.45, 0);
        add(N("UpLeg").c_str(), "mixamorig:Hips", sgn * 0.1, 0.95, 0);
        add(N("Leg").c_str(), N("UpLeg").c_str(), sgn * 0.1, 0.52, 0);
        add(N("Foot").c_str(), N("Leg").c_str(), sgn * 0.1, 0.08, 0);
        add(N("ToeBase").c_str(), N("Foot").c_str(), sgn * 0.1, 0.0, 0.13);
    }
    CharLook L; L.on = true;
    NpcDef n = characterFromLook(L);
    auto map = rig::mapJoints(J, n);
    std::string rep = rig::mapReport(J, map, n);
    std::printf("  (%s)\n", rep.c_str());
    auto idx = [&](const char* nm) { for (int i = 0; i < (int)J.size(); i++) if (J[i].name == nm) return i; return -1; };
    auto boneOf = [&](const char* nm) { int j = idx(nm); return j >= 0 && map[j] >= 0 ? n.bones[map[j]].name : std::string("-"); };
    CHECK(rep.rfind("16 of the 16", 0) == 0 && boneOf("mixamorig:LeftHandMiddle1") == "-" && boneOf("mixamorig:LeftShoulder") == "-",
          "Mixamo's joints are found: hips, spine, neck, head, arms and legs; fingers and collarbones ride along");
    auto P = npcPose(n);
    int lArm = idx("mixamorig:LeftArm");
    double npcSideOfThatArm = P[map[lArm]].a[0];
    CHECK(npcSideOfThatArm > 0, "an arm follows the bone on the same side of the body, whatever the names say");
    auto X = rig::poseJoints(J, map, n, P);
    auto at = [&](const char* nm) { const double* p = X[idx(nm)].p; return std::array<double, 3>{p[0], p[1], p[2]}; };
    auto sh = at("mixamorig:LeftArm"), el = at("mixamorig:LeftForeArm"), hd = at("mixamorig:LeftHand");
    std::printf("  (standing: shoulder %.2f m up, elbow %.2f, hand %.2f; head %.2f; feet %.2f)\n", sh[1], el[1], hd[1], at("mixamorig:Head")[1], at("mixamorig:LeftFoot")[1]);
    CHECK(el[1] < sh[1] - 0.2 && hd[1] < el[1] - 0.15, "the T-posed arms hang down at the sides, as the person stands");
    CHECK(std::fabs(std::hypot(el[0] - sh[0], el[1] - sh[1], el[2] - sh[2]) - 0.28) < 0.002, "an arm keeps its own length (the model keeps its size)");
    CHECK(at("mixamorig:Head")[1] > at("mixamorig:Neck")[1] && at("mixamorig:LeftFoot")[1] < 0.2 && at("mixamorig:LeftFoot")[1] > -0.05, "the head stays on top and the feet near the floor");
    NpcDef w = npcai::walking(n, 0.25, 1.4);
    auto Xw = rig::poseJoints(J, map, w, npcPose(w));
    double lz = Xw[idx("mixamorig:LeftFoot")].p[2], rz = Xw[idx("mixamorig:RightFoot")].p[2];
    std::printf("  (walking: one foot %.2f m forward, the other %.2f)\n", lz, rz);
    CHECK(std::fabs(lz - rz) > 0.15, "walking: its legs stride as the person's do");
    // the skinning matrix: a point on the bound joint moves with it
    float m[16]; rig::skinMatrix(J[idx("mixamorig:LeftHand")].bind, X[idx("mixamorig:LeftHand")], m);
    double px = 0.72, py = 1.45, pz = 0;
    double qx = m[0] * px + m[4] * py + m[8] * pz + m[12], qy = m[1] * px + m[5] * py + m[9] * pz + m[13], qz = m[2] * px + m[6] * py + m[10] * pz + m[14];
    CHECK(std::hypot(qx - hd[0], qy - hd[1], qz - hd[2]) < 1e-6, "the skin moves with its joints");
}

static void layersTest() {
    std::printf("\n== editor layers\n");
    Design d = loadPreset("blowback");
    d.layers = {EdLayer(), EdLayer{1, "Internals", false, true}};
    for (auto& b : d.bodies) b.edLayer = b.fixed ? 0 : 1;
    const BodyDef* inner = nullptr; const BodyDef* outer = nullptr;
    for (auto& b : d.bodies) { if (b.edLayer == 1 && !inner) inner = &b; if (b.edLayer == 0 && !outer) outer = &b; }
    CHECK(inner && outer && !partShown(d, *inner) && partLocked(d, *inner) && partShown(d, *outer) && !partLocked(d, *outer),
          "a hidden, locked layer's parts are hidden and locked; Main's aren't");
    Design back;
    CHECK(designFromJson(designToJson(d), back) && back.layers.size() == 2 && back.layers[1].name == "Internals" && !back.layers[1].visible && back.layers[1].locked &&
          std::count_if(back.bodies.begin(), back.bodies.end(), [](const BodyDef& b) { return b.edLayer == 1; }) == std::count_if(d.bodies.begin(), d.bodies.end(), [](const BodyDef& b) { return b.edLayer == 1; }),
          "saved and loaded: the layers and which part is in which");
    BodyDef stray; stray.edLayer = 7;   // (a layer that's gone: Main)
    CHECK(layerOf(d, 7).id == 0 && partShown(d, stray), "a part in a layer that's gone counts as Main");
    Design older = loadPreset("blowback");
    CHECK(older.layers.empty() && std::all_of(older.bodies.begin(), older.bodies.end(), [&](const BodyDef& b) { return partShown(older, b) && !partLocked(older, b); }),
          "an older blueprint: no layers, everything shown");
    sim::ReliabilityOptions o; o.rounds = 6; o.stallSeconds = 5;
    auto r0 = sim::runReliability(older, o), r1 = sim::runReliability(d, o);
    CHECK(r0.fired == r1.fired && r0.jams == r1.jams, ("the test bench uses every part, hidden or not (" + std::to_string(r1.fired) + " fired either way)").c_str());
}
static void cheatsTest() {
    std::printf("\n== cheats\n");
    sim::ReliabilityOptions o; o.rounds = 10; o.stallSeconds = 20;
    Design bad = loadPreset("tilting");
    for (auto& b : bad.bodies) if (b.magazine) { b.roundSet = true; b.roundAngle = -25; }   // the top round nose down: it can't feed
    Design noGas = loadPreset("gaspistol"); noGas.breech.gas.clear();                       // nothing to unlock and cycle it
    auto r0 = sim::runReliability(bad, o), g0 = sim::runReliability(noGas, o);
    CHECK(r0.fired < 5 && r0.cheats.empty(), ("a tilting pistol with its top round nose down hardly fires (" + std::to_string(r0.fired) + "/10)").c_str());
    CHECK(g0.jams >= 8, ("the gas pistol with no gas port jams every shot (" + std::to_string(g0.jams) + " jams)").c_str());
    sim::cheats().autoFeed = true;
    auto r1 = sim::runReliability(bad, o);
    CHECK(r1.fired == 10 && r1.jams == 0 && r1.cheats == "auto feed", ("auto feed: it feeds every round (" + std::to_string(r1.fired) + "/10, " + std::to_string(r1.jams) + " jams), flagged").c_str());
    sim::cheats().autoFeed = false; sim::cheats().bruteForce = true;
    auto g1 = sim::runReliability(noGas, o);
    CHECK(g1.fired == 10 && g1.jams <= 1 && g1.cheats == "brute force", ("brute force: the gas pistol keeps going (" + std::to_string(g1.jams) + " jams), flagged").c_str());
    {   // a bolt action fired again and again, nobody touching the bolt
        auto S = sim::build(loadPreset("boltaction"));
        sim::run(*S, 0.2);
        if (S->magTmpl) { sim::quickReload(*S); sim::run(*S, 0.2); }
        S->design.anims["w"] = sim::autoWorkAction(*S); sim::playAnim(*S, "w"); sim::run(*S, 2.5);
        int shots0 = S->shots;
        for (int i = 0; i < 6; i++) {
            sim::setKey(*S, "fire", true); S->pressed = S->trigger = true; sim::run(*S, 0.08);
            sim::setKey(*S, "fire", false); S->pressed = S->trigger = false; sim::run(*S, 1.3);
        }
        CHECK(S->shots - shots0 >= 4, ("brute force: a bolt action works itself (" + std::to_string(S->shots - shots0) + " of 6 pulls fired)").c_str());
    }
    sim::cheats() = sim::Cheats();
}
// Furniture on maps: its blocks are solid parts of the map, kept inside it; files keep it whole
// (0.8.47) the level editor's rotation rings: blocks turned about any axis; glass lets bullets and sight through
static void mapRotateGlass() {
    using namespace gmap;
    {   // the developer build's own defaults: a built-in map from the Defaults folder comes first
        std::string dir = "/tmp/gs_defaults_test";
        std::filesystem::create_directories(dir + "/maps");
        Map mine; mine.name = "My garage street"; mine.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, 1));
        std::ofstream(dir + "/maps/garage.gsmap") << mapToJson(mine);
        std::string before = propertyShell("garage").name;
        defaults::setOverrideDir(dir);
        std::string after = propertyShell("garage").name, shack = propertyShell("shack").name;
        defaults::setOverrideDir("");
        std::filesystem::remove_all(dir);
        CHECK(after == "My garage street" && before != after && shack == "Workshop shack", "a built-in map set in the developer build replaces only that one");
    }
    Brush a = makeBox({0, 0, 0}, {2, 1, 0.5}, 1), b = a;
    rotateY(a, 0.7, {1, 0.5, 0.25});
    rotateAxis(b, {0, 1, 0}, 0.7, {1, 0.5, 0.25});
    V3 a0, a1, b0, b1; brushBounds(a, a0, a1); brushBounds(b, b0, b1);
    CHECK(len(a0 - b0) < 1e-9 && len(a1 - b1) < 1e-9, "turning about up matches the old 90-degree turn's maths");
    Brush c = makeBox({0, 0, 0}, {2, 1, 0.5}, 1);
    rotateAxis(c, {1, 0, 0}, 3.14159265358979 / 2, {1, 0.5, 0.25});
    V3 c0, c1; brushBounds(c, c0, c1);
    std::printf("  box 2 x 1 x 0.5 turned about X: %.3f x %.3f x %.3f\n", c1.x - c0.x, c1.y - c0.y, c1.z - c0.z);
    CHECK(std::fabs((c1.x - c0.x) - 2) < 1e-6 && std::fabs((c1.y - c0.y) - 0.5) < 1e-6 && std::fabs((c1.z - c0.z) - 1) < 1e-6, "a block turned on its side about X");
    Map m;
    m.brushes.push_back(makeBox({4, 0, -1}, {4.05, 2, 1}, glassMaterial()));
    m.brushes.push_back(makeBox({8, 0, -1}, {8.2, 2, 1}, 1));
    Hit h1 = rayMap(m, {0, 1, 0}, {1, 0, 0}, 50), h2 = rayMap(m, {0, 1, 0}, {1, 0, 0}, 50, true);
    CHECK(h1.brush == 0 && h2.brush == 1 && std::fabs(h2.t - 8) < 1e-6, "a window stops a plain ray (walking into it) but not a bullet or a look");
    Map back; mapFromJson(mapToJson(m), back);
    CHECK(back.brushes.size() == 2 && isGlass(back.brushes[0]) && !isGlass(back.brushes[1]), "glass saves and loads by name");
    {   // corners: a block rebuilt round its own corners is the same; one corner dragged makes a new shape; new shapes
        Brush bx = makeBox({0, 0, 0}, {2, 1, 1}, 3);
        bx.faces[0].color = 0x3E7CA8FFu;
        auto v = brushVertices(bx);
        Brush same; bool ok = brushFromPoints(v, 1, same, &bx);
        V3 a0, a1, b0, b1; brushBounds(bx, a0, a1); brushBounds(same, b0, b1);
        CHECK(v.size() == 8 && ok && same.faces.size() == 6 && len(a1 - b1) < 1e-9 && len(a0 - b0) < 1e-9, "a block rebuilt from its 8 corners is the same block");
        bool kept = false; for (auto& f : same.faces) kept |= f.color == 0x3E7CA8FFu && f.mat == 3;
        CHECK(kept, "the rebuilt faces keep their texture and colour");
        for (auto& p : v) if (p.x > 1.9 && p.y > 0.9 && p.z > 0.9) p.y += 1;   // one top corner pulled up a metre
        Brush pulled; bool ok2 = brushFromPoints(v, 1, pulled, &bx);
        V3 c0, c1; brushBounds(pulled, c0, c1);
        CHECK(ok2 && std::fabs(c1.y - 2) < 1e-9 && pulled.faces.size() > 6 && brushVertices(pulled).size() == 8, "dragging one corner reshapes the block");
        Brush py = makePyramid({0, 0, 0}, {2, 2, 2}, 1), co = makeCone({0, 0, 0}, {2, 2, 2}, 12, 1), sp = makeSphere({0, 0, 0}, {2, 2, 2}, 10, 1);
        std::printf("  shapes: pyramid %zu faces, cone %zu, sphere %zu\n", py.faces.size(), co.faces.size(), sp.faces.size());
        CHECK(brushValid(py) && py.faces.size() == 5 && brushValid(co) && co.faces.size() == 13 && brushValid(sp) && sp.faces.size() > 30, "pyramids, cones and balls");
    }
    {   // an invisible wall: walked into like any wall, but bullets and sight go through
        Map w; w.brushes.push_back(makeBox({2, 0, -1}, {2.2, 2, 1}, invisibleMaterial()));
        Hit a = rayMap(w, {0, 1, 0}, {1, 0, 0}, 10), b = rayMap(w, {0, 1, 0}, {1, 0, 0}, 10, true);
        bool blocks = !boxFree(w, {2.1, 1, 0}, {0.2, 0.5, 0.2});
        CHECK(a.brush == 0 && b.brush < 0 && blocks && isInvisible(w.brushes[0]), "an invisible wall stops you but not a bullet or a look");
    }
    m.brushes[1].faces[2].color = 0x3E7CA8FFu;
    Map back2; mapFromJson(mapToJson(m), back2);
    CHECK(back2.brushes[1].faces[2].color == 0x3E7CA8FFu && back2.brushes[1].faces[0].color == 0xFFFFFFFFu, "a painted face's colour saves and loads (the rest stay plain)");
}
static void furnitureTest() {
    std::printf("\n== furniture\n");
    gmap::Map m = gmap::builtinRange();
    int furn = 0, items = 0;
    for (auto& e : m.ents) if (e.type == "furniture") { furn++; items += (int)e.items.size(); }
    int baked = 0;
    for (auto& b : m.brushes) if (b.furn) baked++;
    CHECK(furn == 2 && items >= 10 && baked > 5, "the practice range has a shelf and a table, with things on them, solid in the map");
    gmap::Map back;
    CHECK(gmap::mapFromJson(gmap::mapToJson(m), back) && back.brushes.size() == m.brushes.size() && back.ents.size() == m.ents.size(),
          "saved and read back: the same blocks (furniture's from inside it) and things");
    // a shelf turned to face +Z: its blocks are turned with it
    gmap::Entity sh = gmap::builtinFurniture()[0].e;
    sh.pos = {10, 0, 5}; sh.yaw = 90;
    gmap::Map t; t.ents.push_back(sh); gmap::bakeFurniture(t);
    gmap::V3 mn{1e9, 1e9, 1e9}, mx{-1e9, -1e9, -1e9};
    for (auto& b : t.brushes) { gmap::V3 a, c; gmap::brushBounds(b, a, c); mn = {std::min(mn.x, a.x), std::min(mn.y, a.y), std::min(mn.z, a.z)}; mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)}; }
    CHECK(std::fabs((mx.x - mn.x) - 1.0) < 0.01 && std::fabs((mx.z - mn.z) - 0.4) < 0.01, "turned 90 degrees: 1 m wide across X now, 0.4 deep along Z");
    gmap::Hit h = gmap::rayMap(t, {10, 5, 5}, {0, -1, 0}, 10);
    CHECK(h.brush >= 0 && std::fabs(5 - h.t - 1.78) < 0.01, "you land on its top board, 1.78 m up");
    // blocks made into a piece round their floor, and a file of it read back
    std::vector<gmap::Brush> bs = {gmap::makeBox({1, 0, 1}, {1.5, 0.5, 1.5}, 0)};
    gmap::Entity p = gmap::makeFurniture(bs, {1.25, 0, 1.25}, "prop", "Box");
    gmap::Entity q;
    CHECK(gmap::entityFromJson(gmap::entityToJson(p), q) && q.parts.size() == 1 && q.type == "prop" && q.ref == "Box", "a prop saved on its own and read back");
    CHECK(std::fabs(gmap::propMass(q) - 37.5) < 0.1, "its weight from its size: a 0.5 m box, 37.5 kg (mostly air)");
}
// Action kits fitted to other rounds (core fitKitTo): resized along the bore by the round's length, up and across by
// the bigger of that and its thickness; recoil springs only on guns a spring closes; small springs push sqrt(s) harder
static void kitFittingTest() {
    std::printf("\n== kit fitting\n");
    auto cart = [](const char* n) { for (auto& c : cartridgePresets()) if (c.name == n) return c; return Cartridge(); };
    struct Case { const char* kit; const char* round; } cases[] = {{"blowback", ".45 ACP"}, {"blowback", "9x19mm"}, {"boltaction", "7.62x39mm"},
                                                                  {"boltaction", "7.62x54mmR"}, {"revolver", "9x19mm"}, {"pump", "9x19mm"}};
    for (auto& c : cases) {
        Design k = kitDesign(c.kit);
        Design f = fitKitTo(k, cart(c.round));
        sim::ReliabilityOptions o; o.rounds = 8; o.stallSeconds = 20;
        auto r = sim::runReliability(f, o);
        char b[200]; std::snprintf(b, sizeof b, "the %s kit fitted to %s (%.0f%% as long): %d/8 fired, %d jams", c.kit, c.round, 100 * kitScale(k, cart(c.round)), r.fired, r.jams);
        CHECK(r.fired == 8 && r.jams == 0 && f.breech.cart.name == c.round, b);
    }
    Design k = kitDesign("blowback");
    Design same = fitKitTo(k, k.breech.cart);
    sim::ReliabilityOptions o; o.rounds = 8; o.stallSeconds = 20;
    auto a = sim::runReliability(k, o), b = sim::runReliability(same, o);
    CHECK(a.fired == b.fired && a.jams == b.jams, "fitted to its own round: it fires exactly as it came");
}
// Time of day on maps: the sun follows the hour, nights are dark, older maps unchanged, the time saved with the map
static void dayNightTest() {
    std::printf("\n== day and night\n");
    gmap::Look b; b.time = 12; b.daylight = 1;
    auto at = [&](double h) { return gmap::lookAtHour(b, h); };
    CHECK(at(12).daylight > 0.95 && at(0).daylight < 0.1 && at(6.5).daylight > at(5).daylight, "bright at noon, dark at midnight, lighter after sunrise");
    CHECK(at(12).sunPitch > 55 && at(7).sunPitch < 25 && std::fabs(at(9).sunYaw - at(15).sunYaw) > 80, "high at noon, low in the morning, and it crosses the sky");
    CHECK(at(6.8).skyHorizon[0] > at(12).skyHorizon[0] - 0.01 && at(6.8).skyHorizon[2] < at(12).skyHorizon[2], "a warm horizon at dawn");
    gmap::Look old; old.daylight = 0.3;
    gmap::Look o2 = gmap::lookAtHour(old, 3);
    CHECK(o2.daylight == 0.3 && o2.sunYaw == old.sunYaw, "a map without a time of day looks as it always did");
    gmap::Map m = gmap::builtinRange(), r;
    CHECK(gmap::mapFromJson(gmap::mapToJson(m), r) && r.look.time == 14 && r.look.dayMinutes == 0, "the practice range is a fixed time of day (its Day / Night choice), saved with it");
    CHECK(gmap::clockText(17.5) == "17:30" && gmap::clockText(24) == "00:00", "the clock reads right");
}
// A round part rotated in the side view keeps its shape: its turning axis goes round with it (it used to stay level and
// spin the turned outline into an hourglass)
static void radioTest() {
    std::printf("\n== radios\n");
    CHECK(netradio::icyTitle("StreamTitle='Test Band - Song';StreamUrl='';") == "Test Band - Song", "the song title out of the station's note");
    CHECK(netradio::icyTitle("nothing here").empty(), "no title when the note has none");
    CHECK(netradio::playlistUrl("[playlist]\nNumberOfEntries=1\nFile1=https://ice1.example/stream\nTitle1=X\n") == "https://ice1.example/stream", "a .pls playlist's stream link");
    CHECK(netradio::playlistUrl("#EXTM3U\n#EXTINF:-1,X\nhttp://a.example:8000/live\n") == "http://a.example:8000/live", "an .m3u playlist's stream link");
    auto st = netradio::parseStations(R"([{"name":"A","url_resolved":"http://a/1","codec":"MP3","bitrate":128},
        {"name":"B","url_resolved":"http://b/1","codec":"AAC"},{"name":"A again","url_resolved":"http://a/1","codec":"MP3"}])");
    CHECK(st.size() == 1 && st[0].name == "A", "station search keeps MP3 stations, once each");
    // where it's heard from
    auto sp = [](double side, double front, double d, bool blocked = false) { return netradio::spatial(side, front, d, 1, 20, blocked); };
    auto r = sp(5, 0, 5), l = sp(-5, 0, 5), c = sp(0, 1, 1);
    CHECK(r.right > 2 * r.left && l.left > 2 * l.right, "a radio on your right is louder in the right ear (and left on the left)");
    CHECK(std::fabs(c.left - c.right) < 1e-9 && c.left > 0.95, "straight ahead and close: both ears, full");
    CHECK(sp(0, 8, 8).left < 0.6 * sp(0, 4, 4).left, "twice as far is about half as loud");
    CHECK(sp(0, 25, 25).left == 0, "out of its reach: silent");
    auto b = sp(0, 5, 5, true), o = sp(0, 5, 5);
    CHECK(b.left < 0.5 * o.left && b.cutoff < 1000, "through a wall: quieter and muffled");
    CHECK(sp(0, -5, 5).cutoff < 0.5 * o.cutoff, "from behind: duller");
    // saved with the map
    gmap::Map m; gmap::Entity e; e.type = "radio"; e.ref = "https://x.example/s"; e.label = "X"; e.power = 0.3; e.range = 12; e.playing = false;
    m.ents.push_back(e);
    gmap::Map m2;
    CHECK(gmap::mapFromJson(gmap::mapToJson(m), m2) && m2.ents.size() == 1 && m2.ents[0].ref == e.ref && m2.ents[0].label == "X" &&
          std::fabs(m2.ents[0].power - 0.3) < 1e-9 && std::fabs(m2.ents[0].range - 12) < 1e-9 && !m2.ents[0].playing, "a radio saves its station, volume, reach and on/off");
    int radios = 0; for (auto& x : gmap::builtinRange().ents) if (x.type == "radio") radios++;
    CHECK(radios == 1, "the practice range has a radio");
}
static void roundTurnTest() {
    std::printf("\n== round parts turned\n");
    BodyDef b; b.id = "bar"; b.round = true; b.thick = 10;
    b.pts = {{0, 0}, {100, 0}, {100, 10}, {0, 10}}; b.rad.assign(4, 0); b.bulge.assign(4, 0);
    auto turn = [](BodyDef& d, double deg) {
        turnRoundAxis(d, deg);
        V2 c = centroidOf(d); double a = deg * 3.14159265358979 / 180;
        for (auto& p : d.pts) { double x = p.x - c.x, y = p.y - c.y; p = {c.x + x * std::cos(a) - y * std::sin(a), c.y + x * std::sin(a) + y * std::cos(a)}; }
    };
    BodyDef t = b; turn(t, 30);
    carve::RoundAxis ax = carve::roundAxis(t);
    CHECK(std::fabs(std::atan2(ax.dir.y, ax.dir.x) * 180 / 3.14159265358979 - 30) < 0.01 && std::fabs(ax.R - 5) < 0.01, "turned 30 degrees: its axis turns too, still 10 mm thick");
    BodyDef u = b; turn(u, 90);
    CHECK(u.roundAxis == 1 && std::fabs(carve::roundAxis(u).R - 5) < 0.01, "turned upright: the plain Upright setting");
    BodyDef v = b; turn(v, 30); turn(v, -30);
    CHECK(v.roundAxis == 0, "turned and back: Level again");
    BodyDef w = t; flipBody(w, true);
    CHECK(std::fabs(roundAxisAngle(w) + 30) < 0.01, "flipped: the axis is mirrored with it");
    Design d; d.bodies = {t}; Design r;
    CHECK(designFromJson(designToJson(d), r) && r.bodies[0].roundAxis == 3 && std::fabs(r.bodies[0].roundAxisDeg - 30) < 1e-6, "the turned axis is saved");
}
static void actionKitsTest() {
    std::printf("actionKits\n");
    for (auto& k : ::actionKits()) {
        Design kit = kitDesign(k.key);
        sim::ReliabilityOptions o; o.rounds = 15; o.stallSeconds = 20;
        auto r0 = sim::runReliability(kit, o);
        Design g; g.name = "My gun";   // an empty gun, the kit dropped in somewhere else entirely
        int first = insertKit(g, kit, {300, -120});
        bool moved = first == 0 && std::fabs(g.breech.face.x - 300) < 1e-6 && std::fabs(g.breech.face.y + 120) < 1e-6;
        auto r1 = sim::runReliability(g, o);
        // twice into one gun: the second set of parts gets its own names, and the breech is the second kit's
        Design two = g; insertKit(two, kit, {300, -120});
        std::set<std::string> ids; bool unique = true;
        for (auto& b : two.bodies) unique &= ids.insert(b.id).second;
        bool second = two.bodies.size() == 2 * kit.bodies.size() && two.breech.breech != g.breech.breech;
        Design fresh = loadPreset("blank");   // a new gun from the menu: the kit takes the starting bar's place
        bool wasBlank = untouchedBlank(fresh);
        if (wasBlank) fresh.bodies.clear();
        insertKit(fresh, kit, kit.breech.face);
        auto r2 = sim::runReliability(fresh, o);
        std::printf("  (%s: example fired %d of 15, %d jams; dropped into an empty gun %d, %d jams; into a new gun %d, %d jams)\n", k.key, r0.fired, r0.jams, r1.fired, r1.jams, r2.fired, r2.jams);
        CHECK(wasBlank && !untouchedBlank(fresh) && r2.fired == r0.fired && r2.jams == r0.jams, (std::string(k.label) + ": into a new gun, it takes the starting bar's place and works").c_str());
        CHECK(moved && unique && second && r1.fired == r0.fired && r1.jams == r0.jams,
              (std::string(k.label) + ": dropped into another gun, moved and renamed, it works exactly as the example does").c_str());
    }
}

static void handLoadedReliability() {
    std::printf("handLoadedReliability\n");
    for (const char* fm : {"single", "magazine", "internal"}) {
        Design d = loadPreset("single"); d.breech.feedMode = fm;   // (a new gun starts on "Magazine", even with none drawn)
        sim::ReliabilityOptions o; o.rounds = 15; o.stallSeconds = 20;
        auto r = sim::runReliability(d, o);
        std::printf("  (feeds from %s: fired %d of 15)\n", fm, r.fired);
        CHECK(r.fired == 15 && r.jams == 0, "a single shot with no magazine is loaded by hand by the test, whatever Feeds from says");
    }
    // (0.8.56) self-loaders with their magazines taken off: loaded by hand, a round at a time (they used to fire one)
    for (const char* k : {"tilting", "gaspistol", "longrecoil", "blowback"}) {
        Design d = loadPreset(k);
        std::vector<BodyDef> keep; for (auto& b : d.bodies) if (!b.magazine) keep.push_back(b);
        d.bodies = keep;
        sim::ReliabilityOptions o; o.rounds = 8; o.stallSeconds = 20;
        auto r = sim::runReliability(d, o);
        std::printf("  (%s with no magazine: fired %d of 8, %d jams)\n", k, r.fired, r.jams);
        CHECK(r.fired == 8 && r.singleLoad, "a self-loader with no magazine is loaded by hand, every shot");
    }
}

static void gasPistol() {
    std::printf("gasPistol\n");
    Design d = loadPreset("gaspistol");
    CHECK(d.breech.gas.size() == 1 && d.breech.gas[0].piston == "oprod", "the gas pistol has a port driving its operating rod");
    sim::ReliabilityOptions o; o.rounds = 30;
    auto r = sim::runReliability(d, o);
    std::printf("  (gas-operated: fired %d of 30, %d jams)\n", r.fired, r.jams);
    CHECK(r.fired == 30 && r.jams == 0, "the gas-operated pistol fires 30 of 30");
    Design nogas = d; nogas.breech.gas.clear();
    auto n = sim::runReliability(nogas, o);
    std::printf("  (no gas: %d jams, %d failures to eject)\n", n.jams, n.byType[(int)sim::Malf::FailureToEject]);
    CHECK(n.byType[(int)sim::Malf::FailureToEject] >= 20, "without gas its locked slide never opens: the case stays in");
    Design small = d; small.breech.gas[0].portDia = 3.0;
    auto s3 = sim::runReliability(small, o);
    std::printf("  (3 mm port: %d jams)\n", s3.jams);
    CHECK(s3.jams > 0, "too small a port and it doesn't cycle properly");
}

static void revolverCylinder() {
    std::printf("revolverCylinder\n");
    Design d = loadPreset("revolver");
    CHECK(d.breech.cylinder.on && d.breech.cylinder.chambers == 6, "the revolver has a six-chamber cylinder");
    Design back; CHECK(designFromJson(designToJson(d), back) && back.breech.cylinder.on && back.breech.cylinder.chambers == 6, "the cylinder saves and loads");
    auto Sp = sim::build(d);
    sim::Sim& S = *Sp;
    CHECK(sim::hasCylinder(S) && sim::cylinderLive(S) == 6, "loaded: six live rounds");
    sim::run(S, 0.05);
    int clicks = 0, indexes = 0;
    auto pull = [&]() {
        S.events.clear();
        S.pressed = true; S.trigger = true; sim::run(S, 0.05);
        S.pressed = false; S.trigger = false; sim::run(S, 0.15);
        for (auto& e : S.events) { if (e.kind == sim::Ev::Click) clicks++; if (e.kind == sim::Ev::Index) indexes++; }
    };
    for (int k = 0; k < 6; k++) pull();
    std::printf("  (six pulls: %d shots, %d turns, %d left)\n", S.shots, indexes, sim::cylinderLive(S));
    CHECK(S.shots == 6 && indexes == 6 && sim::cylinderLive(S) == 0, "six pulls: the cylinder turns each time and all six fire");
    CHECK(S.spent && sim::chamberedCount(S) == 0, "the fired cases stay in their chambers");
    pull();
    CHECK(S.shots == 6 && clicks >= 1, "a seventh pull just clicks");
    int added = sim::cylinderReload(S);
    CHECK(added == 6 && sim::cylinderLive(S) == 6 && !S.spent, "reloading dumps the cases and fills all six");
    pull();
    CHECK(S.shots == 7, "and it fires again");
    // hand loading: one round at a time into the empty chambers
    auto Hp = sim::build(d); sim::Sim& H = *Hp;
    for (auto& c : H.cyl) c = 0;
    H.chambered = false;
    CHECK(sim::handLoad(H) && sim::handLoad(H) && sim::cylinderLive(H) == 2, "rounds can be put in one at a time");
    sim::ReliabilityOptions o; o.rounds = 30;
    auto r = sim::runReliability(d, o);
    std::printf("  (reliability: fired %d of 30, %d reloads, %d jams)\n", r.fired, r.reloads, r.jams);
    CHECK(r.fired == 30 && r.jams == 0, "the bench test fires 30 of 30, reloading the cylinder");
}

// Several faces pushed out together, corners joined into one, an edge drawn across a face (the 3D vertices tool)
static void meshEditing() {
    std::printf("\n== 3D mesh editing\n");
    auto box = []() { BodyDef b; b.id = "m"; b.pts = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; b.thick = 10; b.z = 0; fixShape(b); meshFromPart(b); return b.mesh; };
    auto volume = [](const MeshDef& m) {
        std::vector<int> t; meshTriangles(m, t);
        double s = 0;
        for (size_t k = 0; k + 2 < t.size(); k += 3) {
            const P3d &a = m.v[t[k]], &c = m.v[t[k + 1]], &d = m.v[t[k + 2]];
            s += a.x * (c.y * d.z - c.z * d.y) - a.y * (c.x * d.z - c.z * d.x) + a.z * (c.x * d.y - c.y * d.x);
        }
        return std::fabs(s / 6);
    };
    auto closed = [](const MeshDef& m) {   // every edge used once each way: no holes, no doubled walls
        std::map<std::pair<int, int>, int> e;
        for (auto& f : m.f) for (size_t k = 0; k < f.size(); k++) e[{f[k], f[(k + 1) % f.size()]}]++;
        for (auto& [k, n] : e) if (n != 1 || !e.count({k.second, k.first})) return false;
        return true;
    };
    // the face toward +z (the near face), split corner to corner into two faces, both pushed out 5 mm together
    MeshDef m = box();
    int near = -1; for (int f = 0; f < (int)m.f.size(); f++) if (meshFaceNormal(m, f).z > 0.9) near = f;
    std::vector<int> L = near >= 0 ? m.f[near] : std::vector<int>{};
    bool split = L.size() == 4 && meshConnect(m, L[0], L[2]);
    int other = (int)m.f.size() - 1;
    CHECK(split && m.f.size() == 7 && closed(m), "an edge drawn across a face splits it in two, the part still closed");
    CHECK(!meshConnect(m, L[0], L[1]), "two corners already joined by an edge: nothing to connect");
    size_t v0 = m.v.size(), f0 = m.f.size();
    auto moved = meshExtrudeFaces(m, {near, other}, 5);
    std::printf("  (two faces pushed out: %zu new corners, %zu new faces, volume %.0f -> %.0f mm3)\n", m.v.size() - v0, m.f.size() - f0, 8000.0, volume(m));
    CHECK(moved.size() == 4 && m.v.size() - v0 == 4 && m.f.size() - f0 == 4 && closed(m) && std::fabs(volume(m) - (8000 + 40 * 20 * 5)) < 1,
          "two faces side by side pushed out together: one block, walls only round the outside, the volume grown by area x 5 mm");
    // two faces on opposite sides pushed out at once: each grows its own walls
    MeshDef m2 = box();
    int far = -1; for (int f = 0; f < (int)m2.f.size(); f++) { double z = meshFaceNormal(m2, f).z; if (z > 0.9) near = f; if (z < -0.9) far = f; }
    meshExtrudeFaces(m2, {near, far}, 3);
    CHECK(m2.v.size() == 16 && m2.f.size() == 14 && closed(m2) && std::fabs(volume(m2) - 8000 * 1.6) < 1, "the near and far faces pushed out at once: thicker by 3 mm on each side");
    // two corners at either end of an edge joined into one: the box becomes a wedge, still closed
    MeshDef m3 = box();
    int a = -1, b = -1;
    for (auto& f : m3.f) for (size_t k = 0; k < f.size() && a < 0; k++) { a = f[k]; b = f[(k + 1) % f.size()]; }
    P3d mid{(m3.v[a].x + m3.v[b].x) / 2, (m3.v[a].y + m3.v[b].y) / 2, (m3.v[a].z + m3.v[b].z) / 2};
    int j = meshMergeVerts(m3, {a, b}, mid);
    CHECK(j >= 0 && m3.v.size() == 7 && m3.f.size() == 6 && closed(m3) && std::fabs(m3.v[j].x - mid.x) < 1e-9 && volume(m3) < 8000,
          "two corners joined into one, half way between: 7 corners, the faces closed round it");
    MeshDef m4 = box();
    std::vector<int> all; for (int v = 0; v < (int)m4.v.size(); v++) all.push_back(v);
    CHECK(meshMergeVerts(m4, all, {0, 0, 0}) < 0 && m4.v.size() == 8, "joining every corner is refused: the part is left as it was");
}

static void meshParts() {
    std::printf("meshParts\n");
    BodyDef b; b.id = "m"; b.pts = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; b.thick = 10; b.z = 0;
    fixShape(b);
    // the same convention as parts shaped in depth: their triangles turn outward the same way
    auto vol = [](const std::vector<P3d>& v, const std::vector<int>& t) {
        double s = 0;
        for (size_t k = 0; k + 2 < t.size(); k += 3) {
            const P3d &a = v[t[k]], &c = v[t[k + 1]], &d = v[t[k + 2]];
            s += a.x * (c.y * d.z - c.z * d.y) - a.y * (c.x * d.z - c.z * d.x) + a.z * (c.x * d.y - c.y * d.x);
        }
        return s / 6;
    };
    BodyDef ds = b; ds.depthOff.assign(4, V2{0, 0}); ds.depthZn = {1, 0, 0, 0}; ds.depthZf.assign(4, 0.0); ds.depthRef = ds.pts;
    std::vector<P3d> dv; std::vector<int> dt; depthTriangles(ds, dv, dt);
    meshFromPart(b);
    CHECK(meshShaped(b) && b.mesh.v.size() == 8 && b.mesh.f.size() == 6, "a box becomes 8 corners and 6 faces");
    std::vector<int> mt; meshTriangles(b.mesh, mt);
    double vm = vol(b.mesh.v, mt), vd = vol(dv, dt);
    std::printf("  (box volume %.0f mm3; shaped-in-depth volume %.0f)\n", vm, vd);
    CHECK(std::fabs(std::fabs(vm) - 8000) < 1 && (vm > 0) == (vd > 0), "its faces turn outward, the same way as parts shaped in depth");
    auto sec = meshSection(b.mesh, 0.3);
    double a = 0; if (!sec.empty()) for (size_t i = 0; i < sec[0].size(); i++) { auto &p = sec[0][i], &q = sec[0][(i + 1) % sec[0].size()]; a += p.x * q.y - q.x * p.y; }
    CHECK(sec.size() == 1 && sec[0].size() == 4 && std::fabs(std::fabs(a / 2) - 800) < 1e-6, "cut through its depth: the 40 x 20 outline");
    // a ring through the middle of its thickness, made smaller: the sides cave in
    auto ring = meshCut(b.mesh, 2, 0.0);
    CHECK(ring.size() == 4, "a ring through the thickness adds a corner on each of the 4 side edges");
    for (int v : ring) { auto& p = b.mesh.v[v]; p.x = 20 + (p.x - 20) * 0.5; p.y = 10 + (p.y - 10) * 0.5; }
    meshOrient(b.mesh);
    auto mid = meshSection(b.mesh, 0.0), face = meshSection(b.mesh, 4.9);
    auto ar = [](const std::vector<V2>& s) { double a = 0; for (size_t i = 0; i < s.size(); i++) { auto &p = s[i], &q = s[(i + 1) % s.size()]; a += p.x * q.y - q.x * p.y; } return std::fabs(a / 2); };
    double am = mid.empty() ? 0 : ar(mid[0]), af = face.empty() ? 0 : ar(face[0]);
    std::printf("  (caved in: %.0f mm2 at the middle, %.0f near a face)\n", am, af);
    CHECK(std::fabs(am - 200) < 1 && af > 700, "caved in at the middle, full size at the faces");
    meshSync(b);
    auto sl = carve::slices(b);
    double mn = 1e9, mx = 0;
    for (auto& s : sl) for (auto& o : s.outlines) { mn = std::min(mn, ar(o)); mx = std::max(mx, ar(o)); }
    std::printf("  (%d slabs for the physics, %.0f to %.0f mm2)\n", (int)sl.size(), mn, mx);
    CHECK(sl.size() >= 6 && mn < 400 && mx > 650, "the physics' slabs follow the waist");
    CHECK(std::fabs(b.thick - 10) < 1e-9 && b.pts.size() == 4, "its outline is its biggest cross-section; its thickness the mesh's");
    // moved as a whole (the Select tool moves the outline): the mesh goes along
    for (auto& p : b.pts) p.x += 5;
    meshSync(b);
    double minx = 1e9; for (auto& p : b.mesh.v) minx = std::min(minx, p.x);
    CHECK(std::fabs(minx - 5) < 1e-9, "moving the part moves the mesh");
    // pushed out: a face extruded 6 mm
    b.mesh = {}; b.pts = {{5, 0}, {45, 0}, {45, 20}, {5, 20}}; fixShape(b); meshFromPart(b);   // (a plain box again)
    int top = -1;
    for (int f = 0; f < (int)b.mesh.f.size(); f++) { P3d n = meshFaceNormal(b.mesh, f); if (n.x > 0.9) top = f; }
    CHECK(top >= 0, "the front end face points forward");
    size_t nf = b.mesh.f.size();
    auto moved = meshExtrude(b.mesh, top, 6);
    double maxx = -1e9; for (auto& p : b.mesh.v) maxx = std::max(maxx, p.x);
    CHECK(!moved.empty() && b.mesh.f.size() > nf && std::fabs(maxx - 51) < 1e-9, "pushing the front face out 6 mm makes it longer, with new side faces");
    // saved and read back
    Design D; D.bodies.push_back(b);
    Design R;
    CHECK(designFromJson(designToJson(D), R) && R.bodies.size() == 1 && R.bodies[0].mesh.v.size() == b.mesh.v.size() && R.bodies[0].mesh.f == b.mesh.f, "a mesh part saves and loads");
    // corners removed: the faces close up
    MeshDef box = b.mesh;
    size_t nv = box.v.size();
    CHECK(meshRemoveVerts(box, {0}) && box.v.size() == nv - 1, "a corner can be taken away");
    std::vector<int> bt; meshTriangles(box, bt);
    CHECK(vol(box.v, bt) > 0, "and the part stays closed");
    // a real gun with its slide made a mesh still runs
    for (int meshed = 0; meshed < 2; meshed++) {
        Design g = loadPreset("blowback");
        if (meshed) for (auto& p : g.bodies) if (p.id == "slide" || p.id == "slide_f") { fixShape(p); meshFromPart(p); }
        sim::ReliabilityOptions o; o.rounds = 10;
        auto r = sim::runReliability(g, o);
        std::printf("  (blowback, slide %s: fired %d of 10)\n", meshed ? "a 3D mesh" : "as drawn", r.fired);
        if (meshed) CHECK(r.fired >= 9, "the blowback pistol still cycles with its slide made a 3D mesh");
    }
}

static void woodPenetration() {
    std::printf("woodPenetration\n");
    struct R { const char* name; double g, mm, v; } rs[] = {{".22 LR", 2.6, 5.7, 370}, {".380", 6.2, 9.0, 290}, {"9 mm", 7.45, 9.01, 360},
                                                          {"00 buck pellet", 3.5, 8.4, 400}, {"5.56", 4.0, 5.7, 940}, {".308", 9.5, 7.82, 850}};
    for (auto& r : rs) std::printf("  (%s: through %.0f cm of wood; out of a 19 mm board at %.0f m/s)\n", r.name,
                                   100 * pen::reach(r.g / 1000, r.mm / 1000, r.v), pen::exitSpeed(r.g / 1000, r.mm / 1000, r.v, 0.019));
    double p9 = pen::reach(0.00745, 0.00901, 360), r556 = pen::reach(0.004, 0.0057, 940), p22 = pen::reach(0.0026, 0.0057, 370);
    CHECK(p9 > 0.1 && p9 < 0.3, "a 9 mm goes through 10-30 cm of wood");
    CHECK(r556 > p9 && r556 < 0.6, "a rifle round goes further, but not through half a metre");
    CHECK(p22 < p9, "a .22 doesn't get as far as a 9 mm");
    CHECK(pen::exitSpeed(0.00745, 0.00901, 360, 0.045) > 200, "a 9 mm goes through a wooden door and keeps most of its speed");
    CHECK(pen::exitSpeed(0.00745, 0.00901, 360, 0.5) == 0, "but not through a thick beam");
    CHECK(pen::woodLike("plywood") && pen::woodLike("door_wood") && !pen::woodLike("concrete") && !pen::woodLike("steel"), "only wood");
    gmap::Brush b = gmap::makeBox({0, 0, 0}, {0.02, 2, 2}, 0);   // a 2 cm board
    CHECK(std::fabs(pen::brushExit(b, {0, 1, 1}, {1, 0, 0}) - 0.02) < 1e-9, "straight through a board: its thickness");
    double sl = pen::brushExit(b, {0, 1, 1}, gmap::norm(gmap::V3{1, 0, 1}));
    CHECK(std::fabs(sl - 0.02 * std::sqrt(2.0)) < 1e-9, "at an angle: a longer way through");
}

static void doorsBlock() {
    std::printf("doorsBlock\n");
    // a wall across the way with a 1 m doorway, a wooden door in it
    gmap::Map m; m.brushes.push_back(gmap::makeBox({-50, -1, -50}, {50, 0, 50}, 0));
    m.brushes.push_back(gmap::makeBox({4, 0, -10}, {4.5, 3, -0.5}, 0));
    m.brushes.push_back(gmap::makeBox({4, 0, 0.5}, {4.5, 3, 10}, 0));
    m.brushes.push_back(gmap::makeBox({4, 2.2, -0.5}, {4.5, 3, 0.5}, 0));
    gmap::Entity d; d.type = "door"; d.pos = {4.25, 0, 0}; d.yaw = 0; d.width = 1.0; d.ref = "wood";
    m.ents.push_back(d);
    double t = doors::ray(m, {0, 1.5, 0}, {1, 0, 0}, 20);
    CHECK(std::fabs(t - 4.25) < 0.01, "a shut door stops a bullet");
    CHECK(doors::ray(m, {0, 2.5, 0}, {1, 0, 0}, 20) < 0, "but not over its top");
    gmap::Map glass = m; glass.ents[0].ref = "glass";
    CHECK(doors::ray(glass, {0, 1.5, 0}, {1, 0, 0}, 20) < 0, "a glass door lets bullets and sight through");
    // an NPC behind it doesn't see you
    unsigned rng = 1; std::vector<npcai::Event> ev;
    gmap::V3 youFeet{10, 0, 0}, youEye{10, 1.62, 0};
    NpcDef h = npcPreset("human"); h.behaviour = "chase"; h.attack = "melee"; h.reach = 1.2; h.damage = 25;
    std::vector<npcai::Actor> A{npcai::spawn(h, {0, 0, 0}, 0, nullptr)};
    for (int k = 0; k < 120; k++) { doors::swing(m, 1.0 / 60); ev.clear(); npcai::think(A, m, youEye, youFeet, true, 1.0 / 60, ev, rng); }
    CHECK(!A[0].aware, "an NPC doesn't see you through a shut door");
    // it hears a shot, opens the door and comes through
    npcai::hear(A, youFeet, 50);
    bool struck = false, opened = false;
    for (int k = 0; k < 900 && !struck; k++) {
        doors::swing(m, 1.0 / 60); ev.clear(); npcai::think(A, m, youEye, youFeet, true, 1.0 / 60, ev, rng);
        opened = opened || doors::isOpen(m, 0);
        for (auto& e : ev) if (e.kind == npcai::Event::Melee) struck = true;
    }
    std::printf("  (door opened: %s, reached you: %s, at x %.1f)\n", opened ? "yes" : "no", struck ? "yes" : "no", A[0].body.feet.x);
    CHECK(opened && struck, "an NPC opens a shut door in its way and comes through");
    // the player is kept out of a shut one
    gmap::Map m2 = m; gmap::Player p; p.feet = {4.1, 0, 0};
    doors::step(m2, p, 1.0 / 60);
    CHECK(p.feet.x < 4.25 - 0.29, "a shut door keeps you out");
}

static void shotSounds() {
    std::printf("Shot sounds\n");
    auto cart = [](const char* n) { for (auto& c : cartridgePresets()) if (c.name == n) return c; return Cartridge(); };
    ShotMix p = shotMix(cart(".380 ACP"), 89), r = shotMix(cart("5.56x45mm"), 406);
    CHECK(p.rifleGain == 0 && std::fabs(p.pistolPitch - 1) < 0.03, ".380 in a short barrel: the pistol recording, as recorded");
    CHECK(r.pistolGain == 0 && std::fabs(r.riflePitch - 1) < 0.03 && std::fabs(r.blast - 1) < 0.1, "5.56 in 16 inches: the rifle recording, as recorded");
    CHECK(shotMix(cart("7.62x54mmR"), 700).riflePitch < r.riflePitch - 0.08, "a bigger rifle round sounds lower");
    CHECK(shotMix(cart(".22 LR"), 150).pistolPitch > p.pistolPitch + 0.05, "a .22 sounds higher than a .380");
    ShotMix nine = shotMix(cart("9x19mm"), 100);
    CHECK(nine.pistolGain > nine.rifleGain && nine.rifleGain > 0, "9 mm: mostly pistol, a little rifle");
    CHECK(shotMix(cart("5.56x45mm"), 180).blast > r.blast * 1.8, "a short barrel on a rifle round: more blast");
    CHECK(shotMix(400, 420, 100).crackGain > shotMix(400, 300, 100).crackGain, "a supersonic pistol bullet adds the crack");
    CHECK(shotMix(cart("7.62x54mmR"), 700).boomGain > shotMix(cart(".22 LR"), 150).boomGain, "the thump grows with the round");
    CHECK(p.tailGain > 0.2f && r.tailGain > 0.2f, "both cut-short recordings get the made-up echo");
    CHECK(synthBoom(0, 48000).size() > 10000 && synthTail(1, 48000).size() > 60000, "the made-up layers are built");
    CHECK(actionMix(1.0, -1).pitch < actionMix(0.1, -1).pitch && actionMix(0.25, 8).gain > actionMix(0.25, 1).gain, "heavier parts deeper, faster louder");
}

// Several versions of one animation: the game picks one at random each time; an exact play takes that one; saved
static void animVersionsTest() {
    std::printf("\n== animation versions\n");
    Design d = loadPreset("blowback");
    AnimKey k; k.t = 0.3; k.firing = d.pose.firing; k.support = d.pose.support;
    d.anims["reload"].keys = {k};
    std::string v2 = newAnimVersion(d, "reload");
    d.anims[v2].keys = {k, k}; d.anims[v2].keys[1].t = 0.6;
    std::string v3 = newAnimVersion(d, "reload");
    d.anims[v3] = Animation{};   // (empty: left out)
    CHECK(v2 == "reload~2" && v3 == "reload~3" && animVersions(d, "reload~3").size() == 3 && animBase(v3) == "reload", "versions are named and listed in order");
    CHECK(animSlotName(d, v2) == "Reload (2)", "a version is named after its animation");
    auto S = sim::build(d);
    int got1 = 0, got2 = 0, other = 0;
    for (int i = 0; i < 200; i++) {
        sim::playAnim(*S, "reload");
        if (S->animSlot == "reload") got1++; else if (S->animSlot == v2) got2++; else other++;
    }
    std::printf("  (200 reloads: version 1 x%d, version 2 x%d, the empty one x%d)\n", got1, got2, other);
    CHECK(got1 > 60 && got2 > 60 && other == 0, "each reload picks one of the recorded versions at random, never an empty one");
    sim::playAnim(*S, v2, true);
    CHECK(S->animSlot == v2, "playing one version exactly (the editor's Play) plays that one");
    Design back; designFromJson(designToJson(d), back);
    CHECK(back.anims.count(v2) && back.anims[v2].keys.size() == 2, "versions are saved with the gun");
}
// "Make it for me": the blowback's work-the-action and reload made with the hands kept clear of the gun, checked in the
// physics; a hand keyed on a moving part holds that part, one dropped on the frame takes the action under it
static void autoAnimTest() {
    std::printf("\n== auto animations\n");
    Design d = loadPreset("blowback");
    AutoAnimReport r; Animation a = autoAnimate(d, "work_action", &r);
    std::printf("  (work the action: %zu keys, clips %d -> %d, opens %.1f of %.1f mm)\n", a.keys.size(), r.clipsBefore, r.clipsAfter, r.opened, r.needed);
    CHECK(r.ok && r.opened >= r.needed * 0.9 && !a.keys.empty(), "the made work-the-action opens the slide as far as the plain way, and it comes home");
    CHECK(r.clipsBefore > 0 && r.clipsAfter == 0, "the plain way's hand goes through the gun; the made one stays clear");
    AutoAnimReport r2; Animation b = autoAnimate(d, "reload", &r2);
    CHECK(r2.ok && !b.keys.empty(), "the made reload puts a fresh magazine in");
    AutoAnimReport r3; autoAnimate(loadPreset("revolver"), "reload", &r3);
    CHECK(!r3.ok && !r3.note.empty(), "a gun with no magazine says so instead");
}


// (0.8.58) the hand's skin is one smooth closed surface; a knuckle can be placed on its own
static void handSkinTest() {
    std::printf("\n== hand skin and joints\n");
    for (const char* pr : {"grip", "trigger", "open", "fist"}) {
        HandHold h; applyHandPreset(h, "firing", pr);
        HandSurface s = handSurface(h.digits);
        double vol = 0;
        for (size_t t = 0; t + 2 < s.idx.size(); t += 3) {
            const float *a = &s.v[s.idx[t] * 3], *b = &s.v[s.idx[t + 1] * 3], *c = &s.v[s.idx[t + 2] * 3];
            vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6;
        }
        std::map<long long, int> edges;
        for (size_t t = 0; t + 2 < s.idx.size(); t += 3)
            for (int e = 0; e < 3; e++) { unsigned a = s.idx[t + e], b = s.idx[t + (e + 1) % 3]; edges[(long long)std::min(a, b) * 1000000 + std::max(a, b)]++; }
        int loose = 0;
        for (auto& kv : edges) if (kv.second != 2) loose++;
        int fingers[DIGIT_COUNT] = {0};
        for (auto d : s.digit) if (d >= 0) fingers[(int)d]++;
        std::printf("  (%s: %zu corners, %.0f cm3, %d odd edges of %zu)\n", pr, s.v.size() / 3, vol / 1000, loose, edges.size());
        CHECK(vol > 150000 && vol < 500000, "the hand's skin encloses about a hand's volume, wound outward");
        CHECK(loose * 200 < (int)edges.size(), "the skin is closed: almost every edge is shared by two faces");
        CHECK(s.v.size() / 3 < 65535, "few enough corners to draw in one piece");
        bool all = true; for (int d = 0; d < DIGIT_COUNT; d++) if (fingers[d] < 100) all = false;
        CHECK(all, "every finger has its own skin");
    }
    HandHold h; applyHandPreset(h, "firing", "grip");
    DigitPose before = h.digits[DIGIT_INDEX];
    HV want;
    {   // somewhere it can reach: where it is with the base joint straightened 30 degrees and spread 10
        HandHold t = h; t.digits[DIGIT_INDEX].bend[0] -= 30; t.digits[DIGIT_INDEX].spread += 10;
        want = digitJoint(t.digits, DIGIT_INDEX, 0);
    }
    double left = reachJoint(h.digits, DIGIT_INDEX, 0, want);
    std::printf("  (index's first knuckle moved: %.1f mm left; end joints %.0f/%.0f -> %.0f/%.0f)\n", left, before.bend[1], before.bend[2], h.digits[DIGIT_INDEX].bend[1], h.digits[DIGIT_INDEX].bend[2]);
    CHECK(left < 1.0, "a knuckle reaches where it's dragged");
    CHECK(h.digits[DIGIT_INDEX].bend[1] == before.bend[1] && h.digits[DIGIT_INDEX].bend[2] == before.bend[2], "the joints past it keep their bend");
}
int main() {
    if (const char* only = std::getenv("GUNSMITH_TESTONLY")) {   // (one quick group while working on it)
        std::string o = only;
        if (o == "mesh") { meshParts(); meshEditing(); }
        else if (o == "revolver") revolverCylinder();
        else if (o == "gas") gasPistol();
        else if (o == "handload") handLoadedReliability();
        else if (o == "wood") woodPenetration();
        else if (o == "doors") doorsBlock();
        else if (o == "pose") grips();
        else if (o == "kits") actionKitsTest();
        else if (o == "kitfit") kitFittingTest();
        else if (o == "daynight") dayNightTest();
        else if (o == "roundturn") roundTurnTest();
        else if (o == "radio") radioTest();
        else if (o == "fcg") fireControlTest();
        else if (o == "layers") layersTest();
        else if (o == "rig") rigTest();
        else if (o == "hollow") hollowParts();
        else if (o == "cheats") cheatsTest();
        else if (o == "furniture") furnitureTest();
        else if (o == "clips") stripperClipAnim();
        else if (o == "career") careerEarlyIncome();
        else if (o == "maprot") mapRotateGlass();
        else if (o == "garage") garageStreet();
        else if (o == "autoanim") autoAnimTest();
        else if (o == "animversions") animVersionsTest();
        else if (o == "hand") handSkinTest();
        std::printf(failures ? "\n%d check(s) failed\n" : "\nAll checks passed\n", failures);
        return failures ? 1 : 0;
    }
    shotSounds();
    handSkinTest();
    fittingAttachments();
    attachmentsData();
    railsAndMounts();
    mirroredCuts();
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
    magazineAngles();
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
    engravings();
    controlButtons();
    poseAndGunMoves();
    gasAndOpenBolt();
    magazineShaping();
    npcRotate();
    shopChecks();
    reliabilityTimeout();
    holdOpenTest();
    propertiesTest();
    taxesAndAccessories();
    paperworkTest();
    npcDesigns();
    careerRules();
    careerEarlyIncome();
    mapRotateGlass();
    garageStreet();
    autoAnimTest();
    animVersionsTest();
    careerStages();
    mapsCarryNpcs();
    npcsAlive();
    doorsBlock();
    woodPenetration();
    meshParts();
    meshEditing();
    revolverCylinder();
    gasPistol();
    handLoadedReliability();
    actionKitsTest();
    kitFittingTest();
    dayNightTest();
    roundTurnTest();
    radioTest();
    fireControlTest();
    layersTest();
    rigTest();
    hollowParts();
    cheatsTest();
    furnitureTest();
    npcPaths();
    armFreedom();
    cartridgeSharing();
    mapTests(failures);
    std::printf(failures ? "\n%d check(s) failed\n" : "\nAll checks passed\n", failures);
    return failures ? 1 : 0;
}
