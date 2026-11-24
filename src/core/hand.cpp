// Gunsmith3D - the hand skeleton (see hand.hpp). Proportions of an adult hand, roughly.
#include "hand.hpp"
#include "solver.hpp"
#include <cmath>
#include <algorithm>

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
        const double l[5] = {-30, -30, -25, -35, -10}, h[5] = {80, 65, 80, 90, 110};   // (a little past a real hand's: more room to pose)
        for (int i = 0; i < 5; i++) lo[i] = l[i], hi[i] = h[i];
    } else {
        const double l[5] = {-30, -35, -5, -20, 0}, h[5] = {30, 100, 115, 95, 0};
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
HV digitJoint(const DigitPose digits[DIGIT_COUNT], int digit, int seg) {
    for (auto& b : handBones(digits)) if (b.digit == digit && b.seg == seg) return b.b;
    return {};
}
double reachDigit(DigitPose digits[DIGIT_COUNT], int digit, HV target, int iterations) { return reachJoint(digits, digit, 2, target, iterations); }
double reachJoint(DigitPose digits[DIGIT_COUNT], int digit, int seg, HV target, int iterations) {
    if (digit < 0 || digit >= DIGIT_COUNT) return 0;
    seg = std::max(0, std::min(2, seg));
    double lo[5], hi[5];
    digitLimits(digit, lo, hi);
    int np = digit == DIGIT_THUMB ? 5 : 4;
    bool use[5] = {true, true, seg >= 1, seg >= 2, digit == DIGIT_THUMB};   // (only the joints before this one move it)
    DigitPose& P = digits[digit];
    // how readily each joint moves: the base joints carry most of a reach, the tip joint follows
    const double weight[5] = {0.7, 1.0, 0.9, 0.6, 1.6};
    auto err = [&]() { HV t = digitJoint(digits, digit, seg); return HV{target.x - t.x, target.y - t.y, target.z - t.z}; };
    HV e = err();
    for (int it = 0; it < iterations; it++) {
        double left = std::sqrt(dot(e, e));
        if (left < 0.2) break;
        double J[3][5] = {};
        for (int i = 0; i < np; i++) {   // how the tip moves for a small turn of each joint (mm per degree)
            if (!use[i]) continue;
            double* v = param(P, i), keep = *v, h = *v + 0.5 > hi[i] ? -0.5 : 0.5;
            HV t0 = digitJoint(digits, digit, seg);
            *v = keep + h;
            HV t1 = digitJoint(digits, digit, seg);
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

// ---------------------------------------------------------------- the hand on the gun
V2 wristOff(double angleDeg) {
    double a = angleDeg * 3.14159265358979 / 180;
    V2 n1{-std::sin(a), std::cos(a)}, n2{std::sin(a), -std::cos(a)};
    auto score = [](V2 n) { return -0.5 * n.x + n.y; };
    return score(n1) >= score(n2) ? n1 : n2;
}
void gunSpan(const Design& d, V2 at, const std::string& part, double& zc, double& half) {
    double z0 = 1e9, z1 = -1e9;
    for (auto& b : d.bodies)
        if (!b.magazine && sim::inPoly(at.x, at.y, b.pts)) { z0 = std::min(z0, b.z - b.thick / 2); z1 = std::max(z1, b.z + b.thick / 2); }
    if (z0 > z1) {
        int i = d.bodyIndex(part);
        double t = i >= 0 ? d.bodies[i].thick : 20, z = i >= 0 ? d.bodies[i].z : 0;
        z0 = z - t / 2; z1 = z + t / 2;
    }
    zc = (z0 + z1) / 2; half = (z1 - z0) / 2;
}
HandFrame handFrame(const Design& d, const HandHold& h, int side) {
    HandFrame f;
    if (!h.on) return f;
    double zc, half;
    gunSpan(d, h.at, h.part, zc, half);
    double an = h.angle * 3.14159265358979 / 180;
    V2 g{std::cos(an), std::sin(an)}, n = wristOff(h.angle);
    // the index finger's end of the grip line: the higher one (or the one toward the muzzle)
    V2 toPinky = (-(-g.y) + 0.3 * (-g.x)) >= (-g.y + 0.3 * g.x) ? g : V2{-g.x, -g.y};
    f.on = true;
    f.o = {h.at.x + n.x * 12, h.at.y + n.y * 12, zc + side * (half + 12 + h.dz)};   // (dz: lifted off the gun's side)
    f.X = {-n.x, -n.y, 0}; f.Y = {0, 0, (double)-side}; f.Z = {toPinky.x, toPinky.y, 0};
    if (h.under) {   // snug: the hand slid down the part's side until its knuckles are at the top edge, so the fingers
                     // wrap over the top instead of standing up past it (into the line of sight, over a slim fore-end)
        double yt = h.at.y;   // the part's top above the hand (design y runs down)
        for (int k = 0; k < 200; k++) {
            bool in = false;
            for (auto& b : d.bodies) if (!b.magazine && sim::inPoly(h.at.x, yt - 1, b.pts)) { in = true; break; }
            if (!in) break;
            yt -= 1;
        }
        double along = (yt - f.o.y) / (-n.y != 0 ? -n.y : 1) - 40;   // (how far the middle moves along X to get there)
        if (-n.y < -0.5 && along < 0) { f.o.x += -n.x * along; f.o.y += -n.y * along; }
    }
    if (h.twist != 0) {   // rolled about the grip line (Z)
        double c = std::cos(h.twist * 3.14159265358979 / 180), s = std::sin(h.twist * 3.14159265358979 / 180);
        HV X = f.X, Y = f.Y;
        f.X = {X.x * c + Y.x * s, X.y * c + Y.y * s, X.z * c + Y.z * s};
        f.Y = {-X.x * s + Y.x * c, -X.y * s + Y.y * c, -X.z * s + Y.z * c};
    }
    return f;
}

// ---------------------------------------------------------------- the skin
namespace {
struct Cone { HV a, b; double r1, r2; double x0, y0, z0, x1, y1, z1; };   // a round cone and its box
double roundCone(const Cone& c, HV p) {   // signed distance to a cone with round ends (radius r1 at a, r2 at b)
    HV ba{c.b.x - c.a.x, c.b.y - c.a.y, c.b.z - c.a.z};
    double l2 = dot(ba, ba);
    if (l2 < 1e-9) { HV d{p.x - c.a.x, p.y - c.a.y, p.z - c.a.z}; return std::sqrt(dot(d, d)) - c.r1; }
    double rr = c.r1 - c.r2, a2 = l2 - rr * rr, il2 = 1.0 / l2;
    HV pa{p.x - c.a.x, p.y - c.a.y, p.z - c.a.z};
    double y = dot(pa, ba), z = y - l2;
    HV xv{pa.x * l2 - ba.x * y, pa.y * l2 - ba.y * y, pa.z * l2 - ba.z * y};
    double x2 = dot(xv, xv), y2 = y * y * l2, z2 = z * z * l2;
    double k = (rr > 0 ? 1 : rr < 0 ? -1 : 0) * rr * rr * x2;
    if ((z > 0 ? 1 : z < 0 ? -1 : 0) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - c.r2;
    if ((y > 0 ? 1 : y < 0 ? -1 : 0) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - c.r1;
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - c.r1;
}
double smin(double a, double b, double k) { double h = std::max(k - std::fabs(a - b), 0.0) / k; return std::min(a, b) - h * h * k * 0.25; }
}  // namespace

HandSurface handSurface(const DigitPose digits[DIGIT_COUNT], double cell) {
    HandSurface out;
    std::vector<HandBone> bones = handBones(digits);
    // the shapes: the palm from its long bones (flattened by keeping them thin and blending them wide), each digit's
    // bones as round cones that taper toward the tip
    std::vector<Cone> palm, dig[DIGIT_COUNT];
    auto box = [](Cone& c) {
        double r = std::max(c.r1, c.r2);
        c.x0 = std::min(c.a.x, c.b.x) - r; c.x1 = std::max(c.a.x, c.b.x) + r;
        c.y0 = std::min(c.a.y, c.b.y) - r; c.y1 = std::max(c.a.y, c.b.y) + r;
        c.z0 = std::min(c.a.z, c.b.z) - r; c.z1 = std::max(c.a.z, c.b.z) + r;
    };
    for (auto& b : bones) {
        Cone c{b.a, b.b, 0, 0};
        if (b.digit < 0) {   // a palm bone: thick at the knuckle, thinner toward the wrist; pushed a little palm-side
            c.r1 = 9; c.r2 = 10.5;
            c.a.y += 1.5; c.b.y += 1.5;
            box(c); palm.push_back(c);
            continue;
        }
        double r = b.radius;
        if (b.digit == DIGIT_THUMB) { c.r1 = b.seg == 0 ? 15 : b.seg == 1 ? 10.5 : 9.3; c.r2 = b.seg == 0 ? 10.5 : b.seg == 1 ? 9.3 : 8.2; }
        else { double base = r * 0.92; c.r1 = base; c.r2 = b.seg == 2 ? base * 0.86 : r * (1 - 0.1 * (b.seg + 1)) * 0.92; }
        box(c);
        dig[b.digit].push_back(c);
    }
    // the wrist: a short round cone back toward the forearm, so the hand doesn't end in a ball
    { HV w = handWrist(); Cone c{{w.x + 18, 1.5, 0}, {w.x - 14, 1.5, 0}, 19, 17}; box(c); palm.push_back(c); }
    const double kPalm = 14, kDigit[DIGIT_COUNT] = {12, 7, 7, 7, 7}, M = 1e9;
    auto field = [&](HV p, int* which = nullptr) {
        double pd = M;
        for (auto& c : palm) if (p.x > c.x0 - kPalm && p.x < c.x1 + kPalm && p.y > c.y0 - kPalm && p.y < c.y1 + kPalm && p.z > c.z0 - kPalm && p.z < c.z1 + kPalm) {
            double d = roundCone(c, p);
            pd = pd >= M ? d : smin(pd, d, kPalm);
        }
        if (pd >= M) pd = 40;   // (far from the palm: only its sign matters there)
        double best = pd, nearest = M; int who = -1;
        for (int d = 0; d < DIGIT_COUNT; d++) {
            double dd = M;
            for (auto& c : dig[d]) if (p.x > c.x0 - 2 && p.x < c.x1 + 2 && p.y > c.y0 - 2 && p.y < c.y1 + 2 && p.z > c.z0 - 2 && p.z < c.z1 + 2)
                dd = std::min(dd, roundCone(c, p));
            if (dd >= M) continue;
            best = std::min(best, smin(pd, dd, kDigit[d]));   // each digit blends into the palm; the digits stay apart
            if (dd < nearest) { nearest = dd; who = d; }
        }
        if (which) *which = nearest < pd + 1 ? who : -1;
        return best;
    };
    // the grid round it all
    double x0 = 1e9, y0 = 1e9, z0 = 1e9, x1 = -1e9, y1 = -1e9, z1 = -1e9;
    auto grow = [&](const Cone& c) { x0 = std::min(x0, c.x0); y0 = std::min(y0, c.y0); z0 = std::min(z0, c.z0); x1 = std::max(x1, c.x1); y1 = std::max(y1, c.y1); z1 = std::max(z1, c.z1); };
    for (auto& c : palm) grow(c);
    for (auto& D : dig) for (auto& c : D) grow(c);
    const double pad = 3 * cell;
    x0 -= pad; y0 -= pad; z0 -= pad; x1 += pad; y1 += pad; z1 += pad;
    const int nx = (int)std::ceil((x1 - x0) / cell) + 1, ny = (int)std::ceil((y1 - y0) / cell) + 1, nz = (int)std::ceil((z1 - z0) / cell) + 1;
    auto at = [&](int i, int j, int k) { return (size_t)i + (size_t)nx * ((size_t)j + (size_t)ny * (size_t)k); };
    std::vector<float> f((size_t)nx * ny * nz);
    // in blocks: a block whose middle is far from the skin has no crossing in it, so it's filled with that value
    const int B = 4;
    const double reach = std::sqrt(3.0) * B * cell * 0.5 + cell;
    for (int bk = 0; bk < nz; bk += B)
        for (int bj = 0; bj < ny; bj += B)
            for (int bi = 0; bi < nx; bi += B) {
                int ei = std::min(nx, bi + B), ej = std::min(ny, bj + B), ek = std::min(nz, bk + B);
                HV mid{x0 + (bi + (ei - 1 - bi) * 0.5) * cell, y0 + (bj + (ej - 1 - bj) * 0.5) * cell, z0 + (bk + (ek - 1 - bk) * 0.5) * cell};
                double dm = field(mid);
                bool far = std::fabs(dm) > reach;
                for (int k = bk; k < ek; k++)
                    for (int j = bj; j < ej; j++)
                        for (int i = bi; i < ei; i++)
                            f[at(i, j, k)] = far ? (float)dm : (float)field({x0 + i * cell, y0 + j * cell, z0 + k * cell});
            }
    // surface nets: a corner in each cell the skin passes through (the average of where it crosses the cell's edges),
    // and a quad across every grid edge the skin crosses, joining the four cells round that edge
    std::vector<int> vid((size_t)nx * ny * nz, -1);
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int k = 0; k + 1 < nz; k++)
        for (int j = 0; j + 1 < ny; j++)
            for (int i = 0; i + 1 < nx; i++) {
                float c[8];
                int in = 0;
                for (int q = 0; q < 8; q++) { c[q] = f[at(i + (q & 1), j + ((q >> 1) & 1), k + ((q >> 2) & 1))]; if (c[q] < 0) in++; }
                if (in == 0 || in == 8) continue;
                double sx = 0, sy = 0, sz = 0; int n = 0;
                for (auto& e : E) {
                    float a = c[e[0]], b = c[e[1]];
                    if ((a < 0) == (b < 0)) continue;
                    double t = a / (a - b);
                    double ax = e[0] & 1, ay = (e[0] >> 1) & 1, az = (e[0] >> 2) & 1, bx = e[1] & 1, by = (e[1] >> 1) & 1, bz = (e[1] >> 2) & 1;
                    sx += ax + (bx - ax) * t; sy += ay + (by - ay) * t; sz += az + (bz - az) * t; n++;
                }
                HV p{x0 + (i + sx / n) * cell, y0 + (j + sy / n) * cell, z0 + (k + sz / n) * cell};
                vid[at(i, j, k)] = (int)(out.v.size() / 3);
                out.v.push_back((float)p.x); out.v.push_back((float)p.y); out.v.push_back((float)p.z);
                int who = -1; field(p, &who);
                out.digit.push_back((signed char)who);
            }
    auto quad = [&](int a, int b, int c, int d, bool flip) {
        if (a < 0 || b < 0 || c < 0 || d < 0) return;
        if (flip) std::swap(b, d);
        out.idx.insert(out.idx.end(), {(unsigned)a, (unsigned)b, (unsigned)c, (unsigned)a, (unsigned)c, (unsigned)d});
    };
    for (int k = 1; k + 1 < nz; k++)
        for (int j = 1; j + 1 < ny; j++)
            for (int i = 1; i + 1 < nx; i++) {
                bool s0 = f[at(i, j, k)] < 0;
                if (s0 != (f[at(i + 1, j, k)] < 0))   // an x edge: the cells round it in y and z
                    quad(vid[at(i, j - 1, k - 1)], vid[at(i, j, k - 1)], vid[at(i, j, k)], vid[at(i, j - 1, k)], !s0);
                if (s0 != (f[at(i, j + 1, k)] < 0))   // a y edge: round it in z and x
                    quad(vid[at(i - 1, j, k - 1)], vid[at(i - 1, j, k)], vid[at(i, j, k)], vid[at(i, j, k - 1)], !s0);
                if (s0 != (f[at(i, j, k + 1)] < 0))   // a z edge: round it in x and y
                    quad(vid[at(i - 1, j - 1, k)], vid[at(i, j - 1, k)], vid[at(i, j, k)], vid[at(i - 1, j, k)], !s0);
            }
    // normals: each corner the area-weighted average of the faces round it
    out.n.assign(out.v.size(), 0.f);
    for (size_t t = 0; t + 2 < out.idx.size(); t += 3) {
        unsigned a = out.idx[t], b = out.idx[t + 1], c = out.idx[t + 2];
        HV A{out.v[a * 3], out.v[a * 3 + 1], out.v[a * 3 + 2]}, Bv{out.v[b * 3], out.v[b * 3 + 1], out.v[b * 3 + 2]}, C{out.v[c * 3], out.v[c * 3 + 1], out.v[c * 3 + 2]};
        HV nn = cross({Bv.x - A.x, Bv.y - A.y, Bv.z - A.z}, {C.x - A.x, C.y - A.y, C.z - A.z});
        for (unsigned q : {a, b, c}) { out.n[q * 3] += (float)nn.x; out.n[q * 3 + 1] += (float)nn.y; out.n[q * 3 + 2] += (float)nn.z; }
    }
    for (size_t q = 0; q + 2 < out.n.size(); q += 3) {
        double l = std::sqrt((double)out.n[q] * out.n[q] + (double)out.n[q + 1] * out.n[q + 1] + (double)out.n[q + 2] * out.n[q + 2]);
        if (l > 1e-12) { out.n[q] /= (float)l; out.n[q + 1] /= (float)l; out.n[q + 2] /= (float)l; }
    }
    return out;
}
