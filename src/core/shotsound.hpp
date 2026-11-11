// Gunsmith3D - How a shot and a moving part sound, worked out from the gun (no audio here; app/audio.cpp plays it).
//
// The recordings are the owner's: a .380 pistol (the "pistol" set) and an AR-15 (the "rifle" set). A shot is a blend of
// the two, chosen by the cartridge's energy, each recording pitched for how big the round is compared with the one it
// was recorded from (bigger is lower, and plays longer, so its echo rings on). A bullet faster than sound adds the
// crack; a short barrel with a lot of energy behind it adds blast. Nothing here knows what kind of gun it is.
#pragma once
#include "design.hpp"

struct ShotMix {
    float pistolGain = 0, pistolPitch = 1;   // the .380 recordings
    float rifleGain = 0, riflePitch = 1;     // the AR-15 recordings
    float crackGain = 0;                     // the supersonic crack, layered on
    double energy = 0, blast = 1;            // (for the record: muzzle energy J, blast factor)
};
// energy J, bullet speed m/s, barrel length mm (face to muzzle; <= 0 if unknown)
ShotMix shotMix(double energy, double velocity, double barrelMm);
ShotMix shotMix(const Cartridge& c, double barrelMm);

struct ActionMix { float gain = 0.8f, pitch = 1; };
// A part of the action slamming home or a case flying out: mass kg of what moved, its speed m/s (<0 if unknown)
ActionMix actionMix(double massKg, double speed);

// The recordings' own cartridges (muzzle energy, J) and moving part (kg): what pitch 1 means
constexpr double SHOT_PISTOL_REF_J = 270;    // .380 ACP from the Thunder
constexpr double SHOT_RIFLE_REF_J = 1700;    // 5.56 from the AR-15
constexpr double ACTION_REF_KG = 0.25;       // the Thunder's slide
