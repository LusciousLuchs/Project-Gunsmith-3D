// Gunsmith3D - scaling parts (the Scale tool, and fitting an action kit to another cartridge).
#pragma once
#include "design.hpp"
#include <vector>

// Scales parts about c (design mm) by sx, sy in the side view and sz through the depth, with everything attached at
// points on them (pins, welds, tracks, magazine wells, spring ends, the breech face and muzzle, hands), their cuts,
// engravings, depth shaping and mass you set. With `corners` (one part), only those corners move.
void scaleParts(Design& D, const std::vector<int>& parts, V2 c, double sx, double sy, double sz, const std::vector<int>& corners = {});
