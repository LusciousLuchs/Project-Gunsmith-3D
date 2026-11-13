// Gunsmith3D - action kits: a working mechanism (an example's action: its breech, barrel, feeding, extraction and fire
// control, with the frame they run in) dropped into your own gun. The kit's parts are added under new names, moved so
// its breech face lands where your gun's is, and your gun's breech settings become the kit's. Nothing here knows what
// kind of gun a kit is: it's the same parts, joints and settings the example is made of.
#pragma once
#include "design.hpp"
#include <string>
#include <vector>

struct KitInfo { const char* key; const char* preset; const char* label; const char* blurb; };
const std::vector<KitInfo>& actionKits();
Design kitDesign(const std::string& key);    // the kit's mechanism (the example it comes from, as it is)

// Everything in the design moved by (dx, dy) mm in the side view: outlines, cuts, mounts, engravings, meshes, joints,
// the breech's points, the hands.
void translateDesign(Design& d, double dx, double dy);
// Adds `kit` to `D`, its breech face moved to `at`. Returns the index of the first part added (-1 if the kit is empty).
// D's breech is replaced by the kit's (renamed); D's own pose stays unless it has no firing hand yet.
int insertKit(Design& D, const Design& kit, V2 at);
// A new gun still as it started (just the blank frame's bar, nothing else): a kit takes the bar's place, since the bar
// sits right where an action's trigger and magazine go (measured: 9 of the 11 kits stopped working on it).
bool untouchedBlank(const Design& D);

// A kit made for another cartridge: every part scaled by the ratio of the cartridges' overall lengths (about the breech
// face), the distances in its settings with it (springs' lengths and coils, latches, cams, feed, cocking, hold-open,
// gas, cylinder stroke), fitted magazines refitted to the new round, and the springs made stiffer or softer so they
// soak up the new round's push on the scaled parts (`springK` multiplies that on top: 1 = as worked out).
Design fitKitTo(const Design& kit, const Cartridge& c, double springK = 1);
double kitScale(const Design& kit, const Cartridge& c);     // that ratio
double kitSpringScale(const Design& kit, const Cartridge& c);   // the springs' factor (before springK)
