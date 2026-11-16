// Headless checks for maps: brush geometry, collision, files, and walking around.
#include "map.hpp"
#include "mapops.hpp"
#include "player.hpp"
#include "armor.hpp"
#include "npc.hpp"
#include "npcai.hpp"
#include <cmath>
#include <cstdio>

using namespace gmap;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) std::printf("  ok    %s\n", msg);                   \
        else { std::printf("  FAIL  %s\n", msg); failures++; }        \
    } while (0)

static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }

static void mapOpsTests(int& failures);
void mapTests(int& failures) {
    mapOpsTests(failures);
    std::printf("Map lighting: daylight and lamps\n");
    {
        Map m;
        Map back;
        CHECK(mapFromJson(mapToJson(m), back) && near(back.look.daylight, 1) && mapToJson(m).find("daylight") == std::string::npos, "older maps stay in daylight (and save as before)");
        m.look.daylight = 0;
        double dark[3]; lightAt(m, {0, 0, 0}, {0, 1, 0}, dark);
        CHECK(dark[0] < 0.05 && dark[1] < 0.05, "a night map with no lights is nearly black");
        Entity l; l.type = "light"; l.pos = {0, 2.5, 0}; l.power = 6; l.range = 8; l.color[0] = 1; l.color[1] = 0.5; l.color[2] = 0.25; m.ents.push_back(l);
        double under[3], far[3], beyond[3];
        lightAt(m, {0, 0, 0}, {0, 1, 0}, under); lightAt(m, {4, 0, 0}, {0, 1, 0}, far); lightAt(m, {9, 0, 0}, {0, 1, 0}, beyond);
        CHECK(under[0] > 0.5 && far[0] < under[0] && far[0] > dark[0] && near(beyond[0], dark[0], 1e-9), "a lamp lights the floor under it, less further off, nothing past its reach");
        CHECK(under[0] > under[1] && under[1] > under[2], "in its own colour");
        double wall[3]; lightAt(m, {0, 0, 0}, {0, -1, 0}, wall);
        CHECK(near(wall[0], dark[0], 1e-9), "a face turned away from it gets none");
        m.ents[0].cone = 40; m.ents[0].pitch = -90;
        double inCone[3], outCone[3];
        lightAt(m, {0, 0, 0}, {0, 1, 0}, inCone); lightAt(m, {3, 0, 0}, {0, 1, 0}, outCone);
        CHECK(inCone[0] > 0.5 && near(outCone[0], dark[0], 1e-9), "a spotlight lights only inside its cone");
        CHECK(mapFromJson(mapToJson(m), back) && back.ents.size() == 1 && back.ents[0].type == "light" && near(back.ents[0].power, 6) && near(back.ents[0].range, 8) &&
              near(back.ents[0].cone, 40) && near(back.ents[0].color[1], 0.5) && near(back.look.daylight, 0), "a map keeps its daylight and its lights");
        Map r = builtinRange();
        int lamps = 0; for (auto& e : r.ents) if (e.type == "light") lamps++;
        double paper[3]; lightAt(r, {7, 1.45, 0}, {-1, 0, 0}, paper);
        CHECK(r.look.daylight < 0.2 && lamps >= 8 && paper[0] > 0.3, "the practice range is dark, lit by its own lamps (its paper targets well lit)");
    }
    std::printf("Armour: the bar and plates\n");
    {
        double bar = 0;
        CHECK(armor::takePlates(bar, 1) == 1 && near(bar, 50), "a plate fills 50");
        CHECK(armor::takePlates(bar, 5) == 2 && near(bar, 150), "the bar stops at 150: of five plates, two are taken and three stay lying");
        CHECK(armor::takePlates(bar, 1) == 0, "a full bar takes none");
        double toHealth = armor::absorb(bar, 40);
        CHECK(near(toHealth, 12) && near(bar, 122), "a hit of 40: the armour soaks 28 (70%), 12 reaches health");
        double low = 10; toHealth = armor::absorb(low, 40);
        CHECK(near(toHealth, 30) && near(low, 0), "a nearly empty bar soaks what it has left, the rest goes through");
        double none = 0;
        CHECK(near(armor::absorb(none, 40), 40), "no armour: all of it");
        Map m; Entity e; e.type = "armor_plate"; e.count = 3; e.respawn = 45; m.ents.push_back(e);
        Map back;
        CHECK(mapFromJson(mapToJson(m), back) && back.ents.size() == 1 && back.ents[0].type == "armor_plate" && back.ents[0].count == 3 && near(back.ents[0].respawn, 45),
              "plate stacks keep their count and comeback time in the map file");
        NpcDef n; n.bones.push_back(NpcBone()); n.armorBar = 100;
        NpcDef nb;
        CHECK(npcFromJson(npcToJson(n), nb) && near(nb.armorBar, 100), "an NPC's armour is saved with it");
        NpcDef old; old.bones.push_back(NpcBone()); NpcDef ob;
        CHECK(npcFromJson(npcToJson(old), ob) && near(ob.armorBar, 0) && npcToJson(old).substr(npcToJson(old).find("\"fight\"")).find("\"armor\"") == std::string::npos, "NPCs without armour save as before");
        auto a = npcai::spawn(nb, {0, 0, 0}, 0, nullptr);
        double h0 = a.health;
        npcai::hurt(a, 50, {1, 0, 0});
        CHECK(near(a.health, h0 - 15) && near(a.armor, 65), "an armoured NPC: a hit of 50 takes 35 off its armour and 15 off its health");
    }
    std::printf("Maps: your own textures\n");
    {
        int n0 = (int)materialNames().size();
        int m = materialIndex("user:rust.png");
        CHECK(m >= builtInMaterials() && isUserMaterial(m) && materialIndex("user:rust.png") == m && (int)materialNames().size() == n0 + 1,
              "a picture from user/textures becomes a map material, once");
        Map mp; mp.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, m));
        Map back;
        std::string err;
        bool ok = mapFromJson(mapToJson(mp), back);
        CHECK(ok && !back.brushes.empty() && materialNames()[back.brushes[0].faces[0].mat] == "user:rust.png", "maps keep the picture's name when saved");
        CHECK(materialIndex("no_such_material") == 0, "unknown built-in names still fall back to the dev grid");
    }
    std::printf("Maps: brushes\n");
    Brush box = makeBox({0, 0, 0}, {2, 1, 3}, 1);
    CHECK(brushValid(box), "a box is a valid brush");
    CHECK(facePolygon(box, 0).size() == 4, "each box face is a quad");
    V3 mn, mx; brushBounds(box, mn, mx);
    CHECK(near(mx.x, 2) && near(mx.y, 1) && near(mx.z, 3) && near(mn.x, 0), "bounds match the box");
    Brush w = makeWedge({0, 0, 0}, {4, 2, 1}, 0, 1);
    CHECK(brushValid(w) && w.faces.size() == 5, "a wedge has five faces");
    Brush cyl = makeCylinder({-1, 0, -1}, {1, 2, 1}, 8, 1);
    CHECK(brushValid(cyl) && cyl.faces.size() == 10, "an 8-sided cylinder has ten faces");
    Brush c = box;
    CHECK(clipBrush(c, norm({1, 1, 0}), dot(norm({1, 1, 0}), V3{2, 0, 0}), 1) && brushValid(c), "clipping a corner off works");
    CHECK(!clipBrush(c, {1, 0, 0}, -5, 1), "clipping everything away is refused");
    Brush p = box;
    CHECK(pushFace(p, 0, 1.0) && near((brushBounds(p, mn, mx), mx.x), 3), "pushing a face grows the brush");
    CHECK(!pushFace(p, 0, -10) , "pushing a face through the brush is refused");
    auto walls = hollow(makeBox({0, 0, 0}, {4, 3, 4}, 1), 0.2);
    CHECK(walls.size() == 6, "hollowing a box makes six walls");
    Brush r = box; rotateY(r, 3.14159265358979 / 2, {1, 0.5, 1.5});
    brushBounds(r, mn, mx);
    CHECK(near(mx.x - mn.x, 3, 1e-6) && near(mx.z - mn.z, 2, 1e-6), "a quarter turn swaps width and depth");
    auto stairs = makeStairs({0, 0, 0}, {2, 1, 1}, 4, 0, 1);
    CHECK(stairs.size() == 4, "stairs make one brush per step");

    std::printf("Maps: picking and collision\n");
    Map m;
    m.brushes.push_back(makeBox({-10, -1, -10}, {10, 0, 10}, 9));   // floor
    m.brushes.push_back(makeBox({3, 0, -10}, {4, 3, 10}, 1));        // wall at x = 3..4
    Hit h = rayMap(m, {0, 5, 0}, {0, -1, 0}, 100);
    CHECK(h.brush == 0 && near(h.t, 5) && near(h.n.y, 1), "a ray down hits the floor top");
    Hit s = sweepBox(m, {0, 1, 0}, {6, 1, 0}, {0.25, 0.9, 0.25});
    CHECK(s.brush == 1 && s.t < 0.5 && near(s.n.x, -1), "a moving body stops at the wall");
    CHECK(boxFree(m, {0, 1, 0}, {0.25, 0.9, 0.25}) && !boxFree(m, {3.5, 1, 0}, {0.25, 0.9, 0.25}), "free space and solid space are told apart");

    std::printf("Maps: files\n");
    Map rng = builtinRange();
    Map back;
    CHECK(mapFromJson(mapToJson(rng), back) && back.brushes.size() == rng.brushes.size() && back.ents.size() == rng.ents.size() && back.ents.back().type == "player_start", "a map saves and loads (its player start and paper targets too)");
    CHECK(back.brushes[0].faces[0].mat == rng.brushes[0].faces[0].mat, "materials survive by name");
    CHECK(!mapFromJson("{\"format\":\"something else\"}", back), "files that aren't maps are refused");

    std::printf("Maps: walking\n");
    Player pl;
    pl.feet = {0, 2, 0};
    PlayerInput in;
    for (int i = 0; i < 120; i++) playerStep(pl, m, in, 1.0 / 60);
    CHECK(pl.onGround && near(pl.feet.y, 0, 0.01), "falls and lands on the floor");
    in.forward = 1;
    for (int i = 0; i < 240; i++) playerStep(pl, m, in, 1.0 / 60);
    CHECK(pl.feet.x < 3 - 0.2 && pl.feet.x > 2.5, "walks up to the wall and stops");
    // a 30 cm step can be walked onto, a 1 m ledge can't
    Map st;
    st.brushes.push_back(makeBox({-50, -1, -10}, {50, 0, 10}, 9));
    st.brushes.push_back(makeBox({2, 0, -10}, {50, 0.3, 10}, 1));
    Player q; q.feet = {0, 0.01, 0};
    for (int i = 0; i < 60; i++) playerStep(q, st, in, 1.0 / 60);
    CHECK(q.feet.x > 3 && near(q.feet.y, 0.3, 0.02), "steps up onto a kerb");
    st.brushes.push_back(makeBox({6, 0.3, -10}, {50, 1.3, 10}, 1));
    for (int i = 0; i < 240; i++) playerStep(q, st, in, 1.0 / 60);
    CHECK(q.feet.x < 6 && near(q.feet.y, 0.3, 0.02), "can't walk up a waist-high ledge");
    // crouch under a low ceiling and stay crouched until there's room
    Map low;
    low.brushes.push_back(makeBox({-10, -1, -10}, {10, 0, 10}, 9));
    low.brushes.push_back(makeBox({-10, 1.4, -10}, {10, 2, 10}, 1));
    Player c2; c2.feet = {0, 0.01, 0};
    PlayerInput ci; ci.want = Stance::Crouch;
    for (int i = 0; i < 30; i++) playerStep(c2, low, ci, 1.0 / 60);
    ci.want = Stance::Stand;
    for (int i = 0; i < 30; i++) playerStep(c2, low, ci, 1.0 / 60);
    CHECK(c2.stance == Stance::Crouch, "can't stand up under a low ceiling");
    // jumping goes up and comes back down
    Player j; j.feet = {0, 0.01, 0};
    PlayerInput ji;
    for (int i = 0; i < 10; i++) playerStep(j, m, ji, 1.0 / 60);
    ji.jump = true; playerStep(j, m, ji, 1.0 / 60); ji.jump = false;
    double top = 0;
    for (int i = 0; i < 120; i++) { playerStep(j, m, ji, 1.0 / 60); top = std::max(top, j.feet.y); }
    CHECK(top > 0.4 && top < 0.9 && j.onGround, "jumps about half a metre and lands");
    // the starter range: spawn and walk downrange without getting stuck
    Player r2; playerSpawn(r2, rng);
    r2.feet.z = 1.1;  // along the walkway between two benches
    PlayerInput fi; fi.forward = 1;
    for (int i = 0; i < 60 * 4; i++) playerStep(r2, rng, fi, 1.0 / 60);
    CHECK(r2.onGround && r2.feet.x > 5, "walks out of the shed onto the range");
}

static double vol(const Brush& b) { V3 a, c; brushBounds(b, a, c); return (c.x - a.x) * (c.y - a.y) * (c.z - a.z); }
static void mapOpsTests(int& failures) {
    std::printf("Level editor operations\n");
    const double PI = 3.14159265358979;
    {   // turning, scaling and mirroring about any axis
        Brush a = makeBox({0, 0, 0}, {2, 1, 1}, 1), b = a;
        rotateY(a, PI / 2, {0, 0, 0}); rotateAbout(b, 1, PI / 2, {0, 0, 0});
        V3 a0, a1, b0, b1; brushBounds(a, a0, a1); brushBounds(b, b0, b1);
        CHECK(len(a0 - b0) < 1e-9 && len(a1 - b1) < 1e-9, "turning about Y matches the old Y turn");
        Brush c = makeBox({0, 0, 0}, {2, 1, 1}, 1);
        rotateAbout(c, 2, PI / 2, {0, 0, 0});   // about Z: a 2 wide 1 tall box stands up
        V3 c0, c1; brushBounds(c, c0, c1);
        CHECK(near(c1.y - c0.y, 2, 1e-6) && near(c1.x - c0.x, 1, 1e-6), "turning about Z stands a box up");
        Brush d = makeBox({1, 1, 1}, {2, 2, 2}, 1);
        CHECK(scaleBrush(d, {2, 1, 3}, {1, 1, 1}), "stretching works");
        V3 d0, d1; brushBounds(d, d0, d1);
        CHECK(near(d0.x, 1) && near(d1.x, 3) && near(d1.y, 2) && near(d1.z, 4), "stretched about a corner: that corner stays");
        Brush e = d;
        CHECK(!scaleBrush(e, {0, 1, 1}, {0, 0, 0}) && near(vol(e), vol(d)), "a flat stretch is refused and changes nothing");
        Brush f = makeBox({1, 0, 0}, {3, 1, 1}, 1);
        mirrorAbout(f, 0, {0, 0, 0});
        V3 f0, f1; brushBounds(f, f0, f1);
        CHECK(near(f0.x, -3) && near(f1.x, -1) && brushValid(f), "mirroring across X");
        Brush g = makeBox({0, 1, 0}, {1, 2, 1}, 1);
        mirrorAbout(g, 1, {0, 0, 0});
        V3 g0, g1; brushBounds(g, g0, g1);
        CHECK(near(g0.y, -2) && near(g1.y, -1), "mirroring across Y (new)");
        Brush h = makeBox({0, 0, 0}, {1, 1, 1}, 1);
        rotateAbout(h, 1, 0.7, {0.5, 0.5, 0.5}); rotateAbout(h, 0, 0.4, {0.5, 0.5, 0.5});
        CHECK(near(vol(makeBox({0, 0, 0}, {1, 1, 1}, 1)), 1) && brushValid(h) && facePolygon(h, 0).size() == 4, "a box turned about two axes is still a box");
    }
    {   // a selection: brushes and things move together
        Map m;
        m.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, 1));
        Entity e; e.type = "npc_spawn"; e.pos = {1, 0, 0}; e.yaw = 0; m.ents.push_back(e);
        Selection s{{0}, {0}};
        rotateSelection(m, s, 1, PI / 2, {0, 0, 0});
        CHECK(near(m.ents[0].pos.x, 0, 1e-9) && near(m.ents[0].pos.z, -1, 1e-9), "a thing turns about the pivot with the brushes");
        CHECK(near(std::fmod(m.ents[0].yaw + 360, 360), 270, 1e-6), "and turns to face the way it went");
        Entity f; f.type = "npc_spawn"; f.pos = {2, 0, 3}; f.yaw = 30; m.ents.push_back(f);
        mirrorSelection(m, Selection{{}, {1}}, 0, {0, 0, 0});
        CHECK(near(m.ents[1].pos.x, -2) && near(m.ents[1].yaw, 150), "mirrored across X, it faces the mirrored way");
        mirrorSelection(m, Selection{{}, {1}}, 2, {0, 0, 0});
        CHECK(near(m.ents[1].pos.z, -3) && near(m.ents[1].yaw, 210), "and across Z");
        moveSelection(m, s, {1, 2, 3});
        V3 mn, mx; selectionBounds(m, Selection{{0}, {}}, mn, mx);
        CHECK(near(mn.y, 2) && near(mx.y, 3), "moving the selection");
        CHECK(!scaleSelection(m, s, {1, -1, 1}, {0, 0, 0}), "a backwards stretch is refused");
    }
    {   // corners
        Brush b = makeBox({0, 0, 0}, {1, 1, 1}, 1);
        auto v = brushVertices(b);
        CHECK(v.size() == 8 && brushEdges(b, v).size() == 12, "a box has 8 corners and 12 edges");
        int top = -1;
        for (size_t i = 0; i < v.size(); i++) if (near(v[i].x, 1) && near(v[i].y, 1) && near(v[i].z, 1)) top = (int)i;
        Brush out;
        CHECK(moveVertices(b, {top}, {0, 0.5, 0}, out) && brushValid(out), "pulling one corner up gives a valid solid");
        V3 o0, o1; brushBounds(out, o0, o1);
        CHECK(near(o1.y, 1.5) && out.faces.size() > 6, "...taller at that corner, with the top split into two faces");
        CHECK(!moveVertices(b, {}, {0, 1, 0}, out) && !moveVertices(b, {99}, {0, 1, 0}, out), "no corner, no change");
        std::vector<int> topFour;
        for (size_t i = 0; i < v.size(); i++) if (near(v[i].y, 1)) topFour.push_back((int)i);
        Brush shear;
        CHECK(moveVertices(b, topFour, {0.5, 0, 0}, shear) && brushValid(shear), "moving a whole face's corners slides it (a leaning wall)");
        Brush ex;
        CHECK(extrudeFace(b, 2, 0.5, ex) && brushValid(ex), "pulling a new piece out of the top face");
        V3 x0, x1; brushBounds(ex, x0, x1);
        CHECK(near(x0.y, 1) && near(x1.y, 1.5) && near(x1.x - x0.x, 1), "...standing on it, half a metre tall");
        CHECK(!extrudeFace(b, 2, 0, ex) && !extrudeFace(b, 9, 1, ex), "a nothing-pull is refused");
        Brush cyl = makeCylinder({0, 0, 0}, {2, 1, 2}, 12, 1), cyl2;
        auto cv = brushVertices(cyl);
        CHECK(cv.size() == 24 && moveVertices(cyl, {0}, {0, 0.2, 0}, cyl2) && brushValid(cyl2), "a 12-sided prism keeps its shape when a corner moves");
    }
    {   // snapping
        Map m;
        m.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, 1));      // the thing to land on
        m.brushes.push_back(makeBox({5, 0, 5}, {6, 1, 6}, 1));      // the thing being moved
        Selection s{{1}, {}};
        // moving it so its corner (5,0,5)+delta is 0.1 from (1,0,1): delta (-3.9, 0, -3.9)
        SnapHit h = snapMove(m, s, {-3.9, 0, -3.9}, 0.3);
        CHECK(h.hit && near(h.delta.x, -4, 1e-6) && near(h.delta.z, -4, 1e-6), "a corner lands exactly on another brush's corner");
        SnapHit far = snapMove(m, s, {-2, 0, -2}, 0.3);
        CHECK(!far.hit && near(far.delta.x, -2), "too far from anything: no snap");
        SnapHit mid = snapPoint(m, s, {1.02, 0.52, 0.5}, 0.2);
        CHECK(mid.hit && near(mid.to.x, 1) && near(mid.to.y, 0.5) && near(mid.to.z, 0.5), "face middles snap too");
        SnapHit edge = snapPoint(m, s, {0.5, 1.03, 0.02}, 0.2);
        CHECK(edge.hit && near(edge.to.z, 0) && near(edge.to.x, 0.5) && near(edge.to.y, 1), "and edge middles");
        SnapHit ground = snapMove(m, s, {-3.9, 0, -3.9}, 0.3, {1, 0, 1});
        CHECK(ground.hit && near(ground.delta.y, 0) && near(ground.delta.x, -4, 1e-6), "a snap that may not go up or down leaves the height alone");
        SnapHit alongX = snapMove(m, s, {-3.9, 0, -3.5}, 0.3, {1, 0, 0});
        CHECK(!alongX.hit || (near(alongX.delta.z, -3.5) && near(alongX.delta.y, 0)), "a snap along one axis moves only along it");
        m.brushes[0].hidden = true;
        CHECK(!snapPoint(m, s, {1, 0, 1}, 0.2).hit, "a hidden brush isn't snapped to");
    }
    {   // aligning and spacing
        Map m;
        m.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, 1));
        m.brushes.push_back(makeBox({4, 0, 2}, {6, 3, 3}, 1));
        m.brushes.push_back(makeBox({10, 5, 0}, {11, 6, 1}, 1));
        Selection s{{0, 1, 2}, {}};
        alignSelection(m, s, 1, AlignTo::Low);
        V3 a, b; brushBounds(m.brushes[2], a, b);
        CHECK(near(a.y, 0), "aligned to the lowest");
        alignSelection(m, s, 1, AlignTo::High);
        brushBounds(m.brushes[0], a, b); V3 a2, b2; brushBounds(m.brushes[1], a2, b2);
        CHECK(near(b.y, 3) && near(b2.y, 3), "aligned to the highest");
        distributeSelection(m, s, 0);
        V3 c[3];
        for (int i = 0; i < 3; i++) { V3 p, q; brushBounds(m.brushes[i], p, q); c[i] = (p + q) * 0.5; }
        CHECK(near(c[0].x, 0.5) && near(c[2].x, 10.5) && near(c[1].x - c[0].x, c[2].x - c[1].x), "spaced evenly between the ends");
        alignSelection(m, s, 2, AlignTo::Middle);
        brushBounds(m.brushes[0], a, b); brushBounds(m.brushes[1], a2, b2);
        CHECK(near((a.z + b.z) / 2, (a2.z + b2.z) / 2), "lined up on their middles");
        Map e; e.ents.resize(2); e.ents[0].pos = {0, 0, 0}; e.ents[1].pos = {4, 0, 9};
        alignSelection(e, Selection{{}, {0, 1}}, 2, AlignTo::Low);
        CHECK(near(e.ents[1].pos.z, 0), "things line up too");
    }
    {   // copies and arrays
        Map m;
        m.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, 1));
        Entity e; e.type = "light"; e.pos = {0.5, 2, 0.5}; m.ents.push_back(e);
        Selection s{{0}, {0}};
        Selection arr = linearArray(m, s, 3, {2, 0, 0});
        CHECK(m.brushes.size() == 4 && m.ents.size() == 4 && arr.brushes.size() == 3, "an array of 3 copies");
        V3 a, b; brushBounds(m.brushes[3], a, b);
        CHECK(near(a.x, 6) && near(m.ents[3].pos.x, 6.5), "each one a step further, things included");
        Map r;
        r.brushes.push_back(makeBox({4, 0, -0.5}, {5, 1, 0.5}, 1));
        radialArray(r, Selection{{0}, {}}, 3, {0, 0, 0}, 1, 360, true);
        CHECK(r.brushes.size() == 4, "a ring of 3 copies round the original");
        V3 c = selectionCentre(r, Selection{{0}, {}}), c2 = selectionCentre(r, Selection{{2}, {}});
        CHECK(near(c.x, 4.5) && near(c2.x, -4.5, 1e-6) && near(c2.z, 0, 1e-6), "quarter turns: the third copy is opposite the original");
        Map k;
        k.brushes.push_back(makeBox({4, 0, -0.5}, {5, 1, 0.5}, 1));
        radialArray(k, Selection{{0}, {}}, 1, {0, 0, 0}, 1, 180, false);
        V3 a3, b3; brushBounds(k.brushes[1], a3, b3);
        CHECK(near(a3.x, -5, 1e-6) && near(b3.x, -4, 1e-6) && near(b3.z - a3.z, 1, 1e-6), "not turned: it goes round but keeps its direction");
        // groups stay together as copies, apart from the originals
        Map g; g.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, 1)); g.brushes.push_back(makeBox({2, 0, 0}, {3, 1, 1}, 1));
        g.brushes[0].group = g.brushes[1].group = 4;
        Selection gs = linearArray(g, Selection{{0, 1}, {}}, 1, {0, 0, 5});
        CHECK(g.brushes[2].group != 0 && g.brushes[2].group == g.brushes[3].group && g.brushes[2].group != 4, "a grouped pair stays a pair, in its own group");
        // the start point is never copied
        Map p; Entity st; st.type = "player_start"; p.ents.push_back(st);
        Selection ps = linearArray(p, Selection{{}, {0}}, 2, {1, 0, 0});
        CHECK(p.ents.size() == 1 && ps.ents.empty(), "there is only one place you start");
    }
    {   // copying between maps and sharing
        Map m;
        m.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, materialIndex("brick")));
        m.brushes[0].name = "wall";
        Entity e; e.type = "door"; e.pos = {2, 0, 0}; m.ents.push_back(e);
        Piece p = copyPiece(m, Selection{{0}, {0}});
        Piece back;
        CHECK(pieceFromJson(pieceToJson(p), back) && back.brushes.size() == 1 && back.ents.size() == 1 && back.brushes[0].name == "wall", "a piece survives its text form");
        CHECK(!pieceFromJson("{\"format\":\"gunsmith3d-map\"}", back) && !pieceFromJson("nonsense", back), "ordinary maps and junk aren't pieces");
        Map other;
        Selection ns = pastePiece(other, back, {10, 0, 0});
        V3 a, b; brushBounds(other.brushes[0], a, b);
        CHECK(ns.brushes.size() == 1 && near(a.x, 10) && near(other.ents[0].pos.x, 12), "pasted into another map where you say");
        std::string code = mapShareCode(m);
        Map got;
        CHECK(code.rfind("GS3D-MAP1:", 0) == 0 && mapFromShareText(code, got) && got.brushes.size() == 1 && got.ents.size() == 1, "a whole map as a share code, and back");
        CHECK(mapFromShareText("hello " + code + " thanks", got) && got.brushes.size() == 1, "a code pasted with other words round it");
        CHECK(mapFromShareText(mapToJson(m), got) && !mapFromShareText("GS3D-MAP1:@@@@", got) && !mapFromShareText("", got), "plain map JSON is fine; junk isn't");
        // bookkeeping is only written when used
        Map fresh; fresh.brushes.push_back(makeBox({0, 0, 0}, {1, 1, 1}, 1));
        std::string js = mapToJson(fresh);
        CHECK(js.find("hidden") == std::string::npos && js.find("frozen") == std::string::npos && js.find("\"group\"") == std::string::npos && js.find("\"name\"") == js.find("\"name\""), "older maps save exactly as before");
        fresh.brushes[0].hidden = true; fresh.brushes[0].frozen = true; fresh.brushes[0].group = 7; fresh.brushes[0].name = "floor";
        fresh.ents.push_back(Entity()); fresh.ents[0].hidden = true; fresh.ents[0].group = 7; fresh.ents[0].name = "start";
        Map again;
        CHECK(mapFromJson(mapToJson(fresh), again) && again.brushes[0].hidden && again.brushes[0].frozen && again.brushes[0].group == 7 && again.brushes[0].name == "floor"
              && again.ents[0].hidden && again.ents[0].group == 7 && again.ents[0].name == "start", "hidden, frozen, group and name are kept");
    }
    {   // more shapes and props
        V3 lo{0, 0, 0}, hi{2, 2, 2};
        Brush cone = makeCone(lo, hi, 4, 1), cone2 = makeCone(lo, hi, 10, 1), sph = makeSphere(lo, hi, 5, 10, 1), q = makeQuarterRound(lo, hi, 6, 0, 1);
        CHECK(brushValid(cone) && cone.faces.size() == 5 && vol(cone) > 7, "a pyramid: a square base and 4 sloping sides");
        CHECK(brushValid(cone2) && brushValid(sph) && brushValid(q), "a cone, a ball and a rounded corner are solid");
        V3 s0, s1; brushBounds(sph, s0, s1);
        CHECK(near(s0.y, 0) && near(s1.y, 2) && s1.x - s0.x > 1.8, "the ball fills its box");
        auto arch = makeArch({0, 0, 0}, {3, 3, 0.4}, 8, 0.3, 0, 1);
        bool allValid = !arch.empty();
        for (auto& b : arch) allValid = allValid && brushValid(b);
        CHECK(arch.size() == 10 && allValid, "an arch: two legs and a curve of 8 blocks");
        // a person can walk through the opening and not through the legs
        Map am; am.brushes = arch;
        CHECK(boxFree(am, {1.5, 1.0, 0.2}, {0.2, 0.9, 0.1}) && !boxFree(am, {0.1, 1.0, 0.2}, {0.05, 0.5, 0.1}), "you can walk through the arch's opening, not its legs");
        auto pipe = makePipe({0, 0, 0}, {2, 2, 2}, 12, 0.2, 1);
        Map pm; pm.brushes = pipe;
        CHECK(pipe.size() == 12 && boxFree(pm, {1, 1, 1}, {0.2, 0.5, 0.2}) && !boxFree(pm, {1.9, 1, 1}, {0.05, 0.5, 0.2}), "a pipe has a hole down the middle");
        for (auto& kind : propKinds()) {
            auto pr = makeProp(kind, {3, 0, 4}, 0);
            bool ok = !pr.empty();
            double lowest = 1e30;
            for (auto& b : pr) { ok = ok && brushValid(b); V3 a, c; brushBounds(b, a, c); lowest = std::min(lowest, a.y); }
            Map pm2; pm2.brushes = pr;
            V3 a, c; Selection all; for (size_t i = 0; i < pr.size(); i++) all.brushes.push_back((int)i);
            selectionBounds(pm2, all, a, c);
            CHECK(ok && near(lowest, 0, 1e-9) && a.x < 3 && c.x > 3 && a.z < 4 && c.z > 4, (kind + ": solid, on the floor, centred on its spot").c_str());
        }
        auto turned = makeProp("table", {0, 0, 0}, 90);
        Map tm; tm.brushes = turned;
        Selection all; for (size_t i = 0; i < turned.size(); i++) all.brushes.push_back((int)i);
        V3 a, c; selectionBounds(tm, all, a, c);
        CHECK(c.z - a.z > c.x - a.x, "a table turned a quarter has its long side along Z");
    }
    {   // measuring
        Measure m = measure({0, 0, 0}, {3, 4, 0});
        CHECK(near(m.distance, 5) && near(m.flat, 3) && near(m.dy, 4) && near(m.slopeDegrees, std::atan2(4.0, 3.0) * 180 / PI), "distance, run and slope between two points");
        CHECK(near(measure({1, 1, 1}, {1, 3, 1}).slopeDegrees, 90) && near(measure({0, 0, 0}, {0, 0, 0}).distance, 0), "straight up, and no distance");
    }
}
