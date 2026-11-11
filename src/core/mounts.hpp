// Gunsmith3D - Mounts for attachments: Picatinny rails (real parts, slotted to MIL-STD-1913) and typed attachment
// points. No graphics here.
#pragma once
#include "design.hpp"
#include <string>
#include <vector>

namespace mounts {
// MIL-STD-1913 (Picatinny), mm
constexpr double RAIL_WIDTH = 21.2;     // across the top
constexpr double RAIL_HEIGHT = 9.4;     // base to top
constexpr double SLOT_PITCH = 10.01;    // slot centre to slot centre
constexpr double SLOT_WIDTH = 5.23;
constexpr double SLOT_DEPTH = 3.0;

// A rail from a to b (side view, design mm) whose mounting face looks `face` way (RailDef), its middle at depth z.
// On top or underneath, a -> b is the surface it stands on (it rises 9.4 mm from it); on a side, its middle line.
// Real geometry: the bar, the cross slots and the dovetail grooves are cuts, so it shows and collides as it is.
BodyDef makeRail(V2 a, V2 b, int face, double z);
// The same rail again after its ends or face changed (keeps its id, name, look, base, weld and mounts)
void regenRail(BodyDef& rail);
// Where its slots are: distance of each slot's middle from a, along a -> b (mm)
std::vector<double> railSlots(const BodyDef& rail);
int railSlotCount(double length);
// The depth (z) a side rail sits at on this base part, so it lies on its side face
double sideRailZ(const BodyDef& base, int face);
// Common attachment point types (the player can type any other): attachments fit points with the same type
const std::vector<std::string>& commonMountTypes();

// carried along when a part moves, turns or is scaled (with the outline)
void moveMounts(BodyDef& d, double dx, double dy);
void rotateMounts(BodyDef& d, V2 c, double deg);
}  // namespace mounts
