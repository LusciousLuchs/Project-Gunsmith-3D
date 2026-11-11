// Headless checks for maps: brush geometry, collision, files, and walking around.
#include "map.hpp"
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

void mapTests(int& failures) {
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
