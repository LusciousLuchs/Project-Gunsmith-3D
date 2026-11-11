// Gunsmith3D - The armour bar: a second bar beside health, filled by armour plates picked up on maps. It soaks most of
// each hit while it lasts; what it doesn't soak reaches health. The same rules for the player and for NPCs. A game
// rule, not a simulation (per-bone armour on NPCs, which stops a share of hits on that bone, is separate).
#pragma once
#include <algorithm>

namespace armor {
constexpr double PLATE = 50;    // one plate's worth
constexpr double MAX = 150;     // three plates
constexpr double SOAK = 0.7;    // the share of a hit the armour takes while it lasts

// A hit of `dmg` on someone with `bar` armour: the bar loses what it soaks; returns what reaches health
inline double absorb(double& bar, double dmg) {
    if (dmg <= 0) return 0;
    double soak = std::min(std::max(0.0, bar), dmg * SOAK);
    bar -= soak;
    return dmg - soak;
}
// Take up to `plates` plates into `bar` (each PLATE, the bar stops at MAX); returns how many were taken (a full bar
// takes none, so they stay where they lie)
inline int takePlates(double& bar, int plates) {
    int n = 0;
    while (n < plates && bar < MAX - 1e-6) { bar = std::min(MAX, bar + PLATE); n++; }
    return n;
}
}  // namespace armor
