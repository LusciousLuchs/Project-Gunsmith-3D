// Gunsmith3D - doors on maps (entity "door"), without graphics: a slab on a hinge at one side of a doorway, how far it
// has swung, opening and shutting, keeping bodies out of it, and rays (bullets, an NPC's sight) against it. Wood and
// steel doors stop bullets and sight; glass ones let both through. NPCs open a shut door they walk into.
// The app draws them (app/doors.hpp).
#pragma once
#include "map.hpp"
#include "player.hpp"

namespace doors {
constexpr double HEIGHT = 2.1, THICK = 0.05;
// the slab's hinge and free end on the floor, as it stands now
void slab(const gmap::Map& m, int ent, gmap::V3& hinge, gmap::V3& end);
// the door in front of `eye` looking along `look` within reach (its entity index), or -1
int facing(const gmap::Map& m, gmap::V3 eye, gmap::V3 look);
bool isOpen(const gmap::Map& m, int ent);
double openness(const gmap::Map& m, int ent);              // 0 shut .. 1 a quarter turn
void toggle(const gmap::Map& m, int ent, gmap::V3 from);   // open (away from `from`) or shut
void swing(const gmap::Map& m, double dt);                  // each frame: the doors move toward open or shut
// keep a body (a circle of radius r on the floor, `h` tall from its feet) out of every door; true if one pushed it
bool pushOut(const gmap::Map& m, gmap::V3& feet, double r, double h, int* which = nullptr);
// the player each frame on foot: swing, then keep them out
void step(const gmap::Map& m, gmap::Player& p, float dt);
// the nearest solid door slab (wood or steel) along a ray within maxT: its distance, or -1. `ent` gets which.
double ray(const gmap::Map& m, gmap::V3 o, gmap::V3 dir, double maxT, int* ent = nullptr);
bool solid(const gmap::Entity& e);                          // stops bullets and sight (not glass)
}  // namespace doors
