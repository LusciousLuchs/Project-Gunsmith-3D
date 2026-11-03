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
            // opposition: the whole thumb swings round the palm's long axis, from beside the index finger to in front
            // of the palm, turning as it goes so its pad comes to face the fingers' pads
            dir = rotate(dir, {1, 0, 0}, P.oppose * D2R_);
            curl = rotate(curl, {1, 0, 0}, P.oppose * D2R_);
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

void digitLimits(int digit, double lo[5], double hi[5]) {
    if (digit == DIGIT_THUMB) {   // spread, root, knuckle, tip, across the palm
        const double l[5] = {-20, -20, -15, -25, 0}, h[5] = {70, 55, 70, 85, 100};
        for (int i = 0; i < 5; i++) lo[i] = l[i], hi[i] = h[i];
    } else {
        const double l[5] = {-25, -25, 0, -10, 0}, h[5] = {25, 95, 110, 90, 0};
        for (int i = 0; i < 5; i++) lo[i] = l[i], hi[i] = h[i];
    }
}
namespace {
double* param(DigitPose& p, int i) { return i == 0 ? &p.spread : i == 4 ? &p.oppose : &p.bend[i - 1]; }
}
void clampDigit(int digit, DigitPose& p) {
    double lo[5], hi[5];
    digitLimits(digit, lo, hi);
    for (int i = 0; i < 5; i++) { double* v = param(p, i); *v = std::fmin(hi[i], std::fmax(lo[i], *v)); }
}
HV digitTip(const DigitPose digits[DIGIT_COUNT], int digit) {
    for (auto& b : handBones(digits)) if (b.digit == digit && b.seg == 2) return b.b;
    return {};
}
double reachDigit(DigitPose digits[DIGIT_COUNT], int digit, HV target, int iterations) {
    if (digit < 0 || digit >= DIGIT_COUNT) return 0;
    double lo[5], hi[5];
    digitLimits(digit, lo, hi);
    int np = digit == DIGIT_THUMB ? 5 : 4;
    DigitPose& P = digits[digit];
    // how readily each joint moves: the base joints carry most of a reach, the tip joint follows
    const double weight[5] = {0.7, 1.0, 0.9, 0.6, 1.6};
    auto err = [&]() { HV t = digitTip(digits, digit); return HV{target.x - t.x, target.y - t.y, target.z - t.z}; };
    HV e = err();
    for (int it = 0; it < iterations; it++) {
        double left = std::sqrt(dot(e, e));
        if (left < 0.2) break;
        double J[3][5] = {};
        for (int i = 0; i < np; i++) {   // how the tip moves for a small turn of each joint (mm per degree)
            double* v = param(P, i), keep = *v, h = *v + 0.5 > hi[i] ? -0.5 : 0.5;
            HV t0 = digitTip(digits, digit);
            *v = keep + h;
            HV t1 = digitTip(digits, digit);
            *v = keep;
            J[0][i] = (t1.x - t0.x) / h * weight[i]; J[1][i] = (t1.y - t0.y) / h * weight[i]; J[2][i] = (t1.z - t0.z) / h * weight[i];
        }
        // damped least squares: step = W J^T (J J^T + l^2 I)^-1 e
        double A[3][3], lam2 = 0.25;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) { A[r][c] = r == c ? lam2 : 0; for (int i = 0; i < np; i++) A[r][c] += J[r][i] * J[c][i]; }
        double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                     A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
        if (std::fabs(det) < 1e-12) break;
        double I[3][3] = {
            {(A[1][1] * A[2][2] - A[1][2] * A[2][1]) / det, (A[0][2] * A[2][1] - A[0][1] * A[2][2]) / det, (A[0][1] * A[1][2] - A[0][2] * A[1][1]) / det},
            {(A[1][2] * A[2][0] - A[1][0] * A[2][2]) / det, (A[0][0] * A[2][2] - A[0][2] * A[2][0]) / det, (A[0][2] * A[1][0] - A[0][0] * A[1][2]) / det},
            {(A[1][0] * A[2][1] - A[1][1] * A[2][0]) / det, (A[0][1] * A[2][0] - A[0][0] * A[2][1]) / det, (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / det}};
        double y[3] = {I[0][0] * e.x + I[0][1] * e.y + I[0][2] * e.z, I[1][0] * e.x + I[1][1] * e.y + I[1][2] * e.z, I[2][0] * e.x + I[2][1] * e.y + I[2][2] * e.z};
        DigitPose before = P;
        double steps[5] = {};
        for (int i = 0; i < np; i++)
            steps[i] = std::fmax(-10.0, std::fmin(10.0, (J[0][i] * y[0] + J[1][i] * y[1] + J[2][i] * y[2]) * weight[i]));
        bool better = false;
        for (double scale = 1; scale > 0.06 && !better; scale *= 0.5) {   // a shorter step if the full one overshoots
            P = before;
            for (int i = 0; i < np; i++) { double* v = param(P, i); *v = std::fmin(hi[i], std::fmax(lo[i], *v + steps[i] * scale)); }
            HV e2 = err();
            if (dot(e2, e2) < dot(e, e)) { e = e2; better = true; }
        }
        if (!better) { P = before; break; }   // no better: stop where it was nearest
    }
    return std::sqrt(dot(e, e));
}
