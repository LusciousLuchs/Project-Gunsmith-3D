// Gunsmith3D - drawing doors on maps (entity "door"). Their swing, bodies and rays are in core/doors.hpp.
#pragma once
#include "core/doors.hpp"

namespace doors {
void draw(const gmap::Map& m, int selected = -1);           // (in a 3D pass)
}  // namespace doors
