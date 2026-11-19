// Gunsmith3D - animations made for you: working the action, reloading, inspecting, with the hands kept out of the gun.
//
// The hands are taken as their finger and palm bones (round, about a finger thick) placed where the hold puts them on
// the gun (core hand.cpp handFrame); the gun as its parts' outlines and thicknesses. A hand that grips gets its fingers
// curled until they touch the part, no further; a hand moving between keys that would pass through the gun is lifted
// off the gun's side for that stretch. The result is played through the physics to check it does the job (the action
// opens as far as the plain way opens it and comes home; a fresh magazine seats), trying a longer, slower pull if not.
// No graphics here.
#pragma once
#include "design.hpp"
#include <string>

struct AutoAnimReport {
    bool ok = false;          // made, and it did its job in the physics
    std::string note;         // what it did, or why it couldn't (plain words for the player)
    int clipsBefore = 0;      // bits of hand inside the gun over the whole animation, the plain way (where there is one)
    int clipsAfter = 0;       // ...and in the one made here
    double opened = 0;        // mm the action opened when played (working the action)
    double needed = 0;        // ...and what the plain way opens it
};

bool canAutoAnimate(const std::string& slot);   // work_action, reload, reload_empty, inspect, clear_jam
Animation autoAnimate(const Design& d, const std::string& slot, AutoAnimReport* rep = nullptr);

// Bits of this hand inside the gun as it's held (finger and palm bones sampled along their length); 0 = clear.
int handClips(const Design& d, const HandHold& h, int side);
// The same over a whole animation, sampled every `dt` seconds (both hands, while they're on the gun).
int animClips(const Design& d, const Animation& a, double dt = 0.02);
