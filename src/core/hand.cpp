// Gunsmith3D - the hand skeleton (see hand.hpp). Proportions of an adult hand, roughly.
#include "hand.hpp"
#include <cmath>

namespace {
constexpr double PI_ = 3.14159265358979323846, D2R_ = PI_ / 180;
HV add(HV a, HV b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
HV mul(HV a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(HV a, HV b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
HV cross(HV a, HV b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
HV norm(HV a) { double l = std::sqrt(dot(a, a)); return l > 1e-12 ? mul(a, 1 / l) : HV{1, 0, 0}; }
HV rotate(HV v, HV k, double ang) {  // about unit axis k (Rodrigues)
    double c = std::cos(ang), s = std::sin(ang);
    return add(add(mul(v, c), mul(cross(k, v), s)), mul(k, dot(k, v) * (1 - c)));
}
// knuckle (or thumb base) position, natural splay (deg), segment lengths and thickness, per digit
struct DigitShape { HV base; double splay; double len[3]; double r; };
const DigitShape SHAPE[DIGIT_COUNT] = {
    // thumb: its first bone (the metacarpal) starts down by the wrist, beside the index finger's, and lies in the
    // ball of the thumb; only its two phalanges stand free. Lengths from a real hand: 43, 30, 22 (+ the pad)
    {{-35, 8, -21}, 0, {43, 30, 22}, 10.5},
    {{40, 0, -27}, -4, {44, 25, 21}, 9},
    {{43, 0, -8}, 0, {49, 29, 23}, 9.5},
    {{40, 0, 11}, 4, {46, 28, 22}, 9},
    {{34, 0, 28}, 9, {36, 21, 19}, 8},
};
const char* NAMES[DIGIT_COUNT] = {"thumb", "index", "middle", "ring", "pinky"};
const char* PRESET_IDS[] = {"grip", "trigger", "offtrigger", "open", "fist"};
}  // namespace

const char* digitName(int d) {
    static const char* n[DIGIT_COUNT] = {"Thumb", "Index", "Middle", "Ring", "Little"};
    return d >= 0 && d < DIGIT_COUNT ? n[d] : "?";
}
const char* handPresets() { return "Wrapped round\0Finger on the trigger\0Finger off the trigger\0Open\0Fist\0"; }
const char* handPresetId(int i) { return PRESET_IDS[i < 0 ? 0 : i > 4 ? 4 : i]; }
HV handWrist() { return {-48, 0, 0}; }

void applyHandPreset(HandHold& h, const std::string& which, const std::string& preset) {
    auto set = [&](int d, double a, double b, double c, double s) { h.digits[d].bend[0] = a; h.digits[d].bend[1] = b; h.digits[d].bend[2] = c; h.digits[d].spread = s; };
    double wrap[3] = {72, 80, 42};
    if (preset == "open") wrap[0] = 4, wrap[1] = 6, wrap[2] = 4;
    if (preset == "fist") wrap[0] = 92, wrap[1] = 100, wrap[2] = 65;
    for (int d = DIGIT_INDEX; d <= DIGIT_PINKY; d++) set(d, wrap[0], wrap[1], wrap[2], 0);
    if (preset == "trigger") set(DIGIT_INDEX, 48, 52, 24, 0);          // its pad on a trigger in front of the grip
    if (preset == "offtrigger") set(DIGIT_INDEX, 8, 6, 2, -2);         // straight along the frame
    if (preset == "open") set(DIGIT_THUMB, 0, 5, 5, 35);
    else if (preset == "fist") set(DIGIT_THUMB, 30, 45, 40, 0);
    else set(DIGIT_THUMB, 25, 20, 15, 0);
    (void)which;
    h.digitsSet = true;
}
void digitsOf(const HandHold& h, const std::string& which, DigitPose out[DIGIT_COUNT]) {
    if (h.digitsSet) { for (int d = 0; d < DIGIT_COUNT; d++) out[d] = h.digits[d]; return; }
    HandHold t = h;
    applyHandPreset(t, which, which == "firing" ? "trigger" : "grip");
    for (int d = 0; d < DIGIT_COUNT; d++) out[d] = t.digits[d];
}

std::vector<HandBone> handBones(const DigitPose digits[DIGIT_COUNT]) {
    std::vector<HandBone> out;
    const HV Y{0, 1, 0}, W = handWrist();
    for (int d = DIGIT_INDEX; d <= DIGIT_PINKY; d++) {  // the palm: wrist to each knuckle
        HandBone p; p.name = std::string("palm_") + NAMES[d]; p.a = {W.x, W.y, SHAPE[d].base.z * 0.55}; p.b = SHAPE[d].base; p.up = {0, -1, 0}; p.radius = 11;
        out.push_back(p);
    }
    for (int d = 0; d < DIGIT_COUNT; d++) {
        const DigitShape& S = SHAPE[d];
        const DigitPose& P = digits[d];
        HV dir, k;
        HV up = {0, -1, 0}, at = S.base;
        if (d == DIGIT_THUMB) {
            // at rest it leaves the palm about 30 degrees out from the index finger and tipped toward the palm side;
            // it is turned on its own axis, so its nail faces sideways and it curls across the palm toward the
            // little finger, not straight down like a finger
            dir = norm({0.84, 0.26, -0.48});
            HV curl = {0, 0.45, 0.89};
            curl = norm(add(curl, mul(dir, -dot(curl, dir))));
            HV away = {0, -0.25, -0.97};   // spread: out from the index finger, the way a hand opens into an L
            away = norm(add(away, mul(dir, -dot(away, dir))));
            HV ka = norm(cross(dir, away));
            dir = rotate(dir, ka, P.spread * D2R_);
            curl = rotate(curl, ka, P.spread * D2R_);
            k = norm(cross(dir, curl));
            up = mul(curl, -1);
        } else {
            dir = rotate({1, 0, 0}, Y, -(S.splay + P.spread) * D2R_);   // + spread swings toward the little finger
            k = norm(cross(dir, Y));
        }
        for (int s = 0; s < 3; s++) {
            dir = rotate(dir, k, P.bend[s] * D2R_);
            up = rotate(up, k, P.bend[s] * D2R_);
            HandBone b;
            b.name = std::string(NAMES[d]) + "_0" + std::to_string(s + 1);
            b.digit = d; b.seg = s;
            b.a = at; b.b = add(at, mul(dir, S.len[s])); b.up = up;
            b.radius = d == DIGIT_THUMB ? (s == 0 ? 14 : s == 1 ? 10.5 : 9.5) : S.r * (1 - 0.1 * s);   // the thumb's first bone: the ball of the thumb
            out.push_back(b);
            at = b.b;
        }
    }
    return out;
}
