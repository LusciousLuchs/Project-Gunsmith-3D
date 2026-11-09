// Gunsmith3D - NPCs alive on a map: they walk (the player's own movement, sized to their body and speed), notice you
// (how far they see, with a clear line of sight), act on their behaviour, and attack: up close, with a gun made for
// NPCs, or by exploding. Hits on their bones do damage by the bullet's energy (weak spots triple, armour stops its
// share); at no health they fall and lie still. No graphics here: app/range.cpp draws them and flies their bullets.
#pragma once
#include "map.hpp"
#include "npc.hpp"
#include "player.hpp"
#include "design.hpp"
#include "nav.hpp"
#include <string>
#include <vector>

namespace npcai {

struct Actor {
    NpcDef def;
    gmap::Player body;               // where it is and how it moves (feet, velocity, yaw)
    gmap::PlayerTuning tune;         // its size and speed
    gmap::V3 home;                   // where it started (wander and guard stay near it)
    double health = 100;
    bool dead = false;
    double deadT = 0;                // seconds since it died (it falls, then lies there)
    gmap::V3 fallDir;                // which way it fell
    bool aware = false;              // it has noticed you
    double sinceSeen = 1e9;          // seconds since it last saw you
    gmap::V3 lastSeen;               // where it last saw you
    gmap::V3 wanderTo; double wanderT = 0;
    double react = 0;                // time to react before its first attack after noticing you
    double cool = 0;                 // until it can attack again
    int burstLeft = 0; double shotT = 0;
    double fuse = -1;                // exploding: seconds left (-1: not lit)
    double walkPhase = 0;            // for its legs' swing
    double hitFlash = 0;             // a moment of colour when hit
    bool heard = false;              // it heard you (a shot, or being hit): it turns toward you and comes to look
    int side = 0; double sideT = 0;  // going round something in its way: which side it chose, and for how long
    // its route (nav.hpp): the points to walk through, the next one, where it was going when it planned it
    std::vector<gmap::V3> path; int pathAt = 0; gmap::V3 pathGoal;
    bool pathOk = false, pathReaches = false;   // planned from where it stands; ends where it wanted to go
    double replanT = 0, planAge = 0, stuckT = 0;
    // a gun made for NPCs (Design::npcGun), loaded from its blueprint
    bool hasGun = false;
    NpcGunDef gun;
    Cartridge cart;
};

// What happened this step, for the game to act on
struct Event {
    enum Kind { Shot, Melee, Explode, Died } kind;
    int actor = -1;
    gmap::V3 from, dir;              // Shot: where the bullet leaves and which way (spread applied)
    double damage = 0;               // Melee: to the player; Explode: at the centre
    double radius = 0;               // Explode
};

// Make an actor standing at a spawn point. `gun`: its NPC gun's blueprint if it carries one (else nullptr).
Actor spawn(const NpcDef& def, gmap::V3 pos, double yawDeg, const Design* gun);
// One step: `eye` is where the player's eye is (they look for it), `feet` where they stand, `alive` whether they're alive.
// `navs`: the map's path-finding grids (they find their way round the map); without, they head straight for where
// they're going and only step round what's right in front of them.
void think(std::vector<Actor>& actors, const gmap::Map& map, gmap::V3 eye, gmap::V3 feet, bool alive, double dt,
           std::vector<Event>& out, unsigned& rng, nav::Cache* navs = nullptr);
nav::Agent agentOf(const Actor& a);   // its size, for path-finding
// Damage a bullet does: by its energy (J), 1 point per 20 J, weak spots x3, armour stops its share
double bulletDamage(double massKg, double speed, bool vital, double armor);
// A loud noise at `at` (a gunshot): everything alive within `radius` metres hears it and comes to look
void hear(std::vector<Actor>& actors, gmap::V3 at, double radius);
// A bone was hit: true if that killed it
bool hurt(Actor& a, double damage, gmap::V3 dir);
// Its bones in the world right now (metres; the falling and the legs' swing applied): start, end, radius
struct BoneAt { gmap::V3 a, b; double r; int bone; };
std::vector<BoneAt> bones(const Actor& a);
NpcDef posed(const Actor& a);        // its NPC with this moment's leg swing applied (for drawing)
double height(const Actor& a);       // metres
double fallAngle(const Actor& a);    // radians it has fallen over so far (about its feet, toward fallDir)

}  // namespace npcai
