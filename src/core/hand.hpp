// Gunsmith3D - the hand skeleton: palm, thumb and four fingers, each with three joints that bend, and a spread.
// This is the framework a modelled hand attaches to: every bone has a name (thumb_01 .. pinky_03, palm), a start, an
// end and an up direction, so a skinned mesh can follow it. No graphics here.
//
// Hand space, millimetres: origin in the middle of the palm; x along the fingers when the hand is flat; y out of the
// palm (the way the fingers curl); z along the knuckles, from the index finger toward the little finger.
#pragma once
#include "design.hpp"
#include <string>
#include <vector>

struct HV { double x = 0, y = 0, z = 0; };
struct HandBone {
    std::string name;       // thumb_01, index_02, ..., palm_index (the palm's long bones), wrist
    int digit = -1, seg = 0;  // DIGIT_THUMB .. DIGIT_PINKY (-1: the palm), and which joint from the base (0..2)
    HV a, b, up;             // start (the joint), end, and the bone's up (the back of the finger)
    double radius = 9;       // mm, for a simple stand-in drawing
};
const char* digitName(int d);   // "Thumb", "Index", ...
const char* handPresets();      // "\0"-separated: the preset names, in preset-id order
const char* handPresetId(int i);
// The pose of the digits as posed, or a preset that suits the hand ("firing" holds the trigger, "support" wraps).
void digitsOf(const HandHold& h, const std::string& which, DigitPose out[DIGIT_COUNT]);
void applyHandPreset(HandHold& h, const std::string& which, const std::string& preset);
std::vector<HandBone> handBones(const DigitPose digits[DIGIT_COUNT]);
HV handWrist();                 // where the wrist joint is, in hand space
// How far each joint of a digit goes, like a real hand (degrees): [0] spread, [1..3] the bends from the base, [4] the
// thumb's swing across the palm.
void digitLimits(int digit, double lo[5], double hi[5]);
void clampDigit(int digit, DigitPose& p);
// Moves one digit, within its limits, so its tip comes as near `target` (hand space) as it can; the others stay. Small
// moves are preferred, so the rest of its pose is kept where it doesn't need to change. Returns the distance left (mm).
double reachDigit(DigitPose digits[DIGIT_COUNT], int digit, HV target, int iterations = 30);
HV digitTip(const DigitPose digits[DIGIT_COUNT], int digit);
