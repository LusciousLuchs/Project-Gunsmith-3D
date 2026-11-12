// Gunsmith3D - doors on maps (entity "door"): a slab on a hinge at one side of a doorway. F opens or shuts the one you
// face (it swings away from you); a shut or half-open door keeps you out. Styles: wood, metal, glass. NPCs and bullets
// aren't stopped by them yet.
#pragma once
#include "map.hpp"
#include "player.hpp"

namespace doors {
// the door in front of `eye` looking along `look` within reach (its entity index), or -1
int facing(const gmap::Map& m, gmap::V3 eye, gmap::V3 look);
bool isOpen(const gmap::Map& m, int ent);
void toggle(const gmap::Map& m, int ent, gmap::V3 from);   // open (away from `from`) or shut
// each frame on foot: the doors swing, and the player is kept out of them
void step(const gmap::Map& m, gmap::Player& p, float dt);
void draw(const gmap::Map& m, int selected = -1);           // (in a 3D pass)
}  // namespace doors
