// Gunsmith3D - the Scale tool: make a part (and whatever is welded to it) bigger or smaller, or stretch it one way.
// Drag a handle of the box round it: a corner keeps its proportions (Shift: free), an edge stretches one way; Ctrl
// scales about the middle instead of the opposite side. Or type sizes in the panel. What's attached at points on the
// part (pins, track slots, spring ends, the breech face and muzzle, hands) goes with it.
#pragma once
#include "design.hpp"
#include <vector>

// Scale parts about c (design mm): sx across, sy up and down, sz in thickness (1 = unchanged). With corners picked
// (one part), only those corners move.
#include "scale.hpp"   // scaleParts
bool scaleBox(const Design& D, const std::vector<int>& parts, const std::vector<int>& corners, V2& lo, V2& hi);  // side-view box (mm)
void scaleInput(bool overUI);
void drawScaleOverlay();
