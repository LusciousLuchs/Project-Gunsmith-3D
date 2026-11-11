// Gunsmith3D - How a shot and a moving part sound (see shotsound.hpp).
#include "shotsound.hpp"

#include <algorithm>
#include <cmath>

static double clampd(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
static double smooth(double u) { u = clampd(u, 0, 1); return u * u * (3 - 2 * u); }

ShotMix shotMix(double energy, double velocity, double barrelMm) {
    ShotMix m;
    double E = std::max(1.0, energy);
    m.energy = E;
    // pistol-sized to rifle-sized: all .380 at 300 J and below, all AR-15 at 1500 J and up, blended between
    double w = smooth((std::log(E) - std::log(300.0)) / (std::log(1500.0) - std::log(300.0)));
    // blast: energy per mm of barrel, against what the recordings had (.380 in the Thunder's 3.5" barrel: 2.9 J/mm;
    // 5.56 in the AR-15's 16": 4.3 J/mm). A stubby barrel on a big round is harsher and louder; a long one gentler.
    double refPerMm = 2.9 + (4.3 - 2.9) * w;
    m.blast = barrelMm > 1 ? clampd(E / barrelMm / refPerMm, 0.5, 3.0) : 1.0;
    // bigger rounds sound lower (and ring longer, played slower); each set against its own cartridge
    auto pitch = [&](double ref) { return (float)clampd(std::pow(E / ref, -0.2), 0.6, 1.4); };
    m.pistolPitch = pitch(SHOT_PISTOL_REF_J);
    m.riflePitch = pitch(SHOT_RIFLE_REF_J);
    // louder with energy and blast, kept under full scale; equal-power blend
    auto loud = [&](double ref) { return clampd(0.8 * std::pow(E / ref, 0.18) * std::pow(m.blast, 0.15), 0.3, 1.0); };
    m.pistolGain = (float)(loud(SHOT_PISTOL_REF_J) * std::cos(w * 1.5707963));
    m.rifleGain = (float)(loud(SHOT_RIFLE_REF_J) * std::sin(w * 1.5707963));
    if (m.pistolGain < 0.03f) m.pistolGain = 0;
    if (m.rifleGain < 0.03f) m.rifleGain = 0;
    // the crack: the rifle recordings have their own; the pistol side gets one layered on when the bullet is
    // faster than sound (from about 340 m/s, full by 450)
    m.crackGain = (float)(0.7 * smooth((velocity - 340) / 110) * (1 - w) * clampd(std::pow(E / SHOT_PISTOL_REF_J, 0.15), 0.5, 1.0));
    if (m.crackGain < 0.03f) m.crackGain = 0;
    // the made-up layers: a thump that grows with energy (and blast) and drops in pitch, and the range's echo, mostly
    // for the pistol side (the rifle recordings ring on by themselves); bigger rounds ring longer (played slower)
    m.boomGain = (float)clampd(0.55 * std::pow(E / 500, 0.2) * std::pow(m.blast, 0.1), 0.25, 0.9);
    m.boomPitch = (float)clampd(std::pow(E / 500, -0.12), 0.7, 1.3);
    m.tailGain = (float)clampd(0.55 * m.pistolGain + 0.5 * m.rifleGain, 0, 0.8);   // (both recordings are cut short: this is their echo)
    m.tailPitch = (float)clampd(std::pow(E / 500, -0.1), 0.75, 1.25);
    return m;
}

ShotMix shotMix(const Cartridge& c, double barrelMm) { return shotMix(c.energy(), c.velocity, barrelMm); }

ActionMix actionMix(double massKg, double speed) {
    ActionMix a;
    double m = massKg > 1e-4 ? massKg : ACTION_REF_KG;
    a.pitch = (float)clampd(std::pow(m / ACTION_REF_KG, -0.15), 0.6, 1.6);   // heavier: deeper
    a.gain = speed < 0 ? 0.8f : (float)clampd(0.35 + speed / 6.0, 0.25, 1.0); // harder: louder
    return a;
}

// ---------------------------------------------------------------- the made-up layers
namespace {
struct Rng { unsigned s; float next() { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xFFFF) / 32768.f - 1; } };
}
std::vector<float> synthBoom(int variant, int rate) {
    // a falling low sine (110 Hz down to about 42) with a quick noise thump at the start, softly saturated
    int n = (int)(0.4 * rate);
    std::vector<float> y(n);
    Rng r{1234u + 77u * (unsigned)variant};
    double ph = 0, lp = 0, f0 = 110 + 8 * variant, f1 = 42 + 3 * variant;
    for (int i = 0; i < n; i++) {
        double t = (double)i / rate;
        double f = f1 + (f0 - f1) * std::exp(-t * 30);
        ph += 2 * 3.14159265358979 * f / rate;
        double att = std::min(1.0, t / 0.0015);
        double tone = std::sin(ph) * std::exp(-t * 11) * att;
        lp += (r.next() - lp) * 0.08;                      // a dull noise burst: the thump
        double thump = lp * 3.0 * std::exp(-t * 60) * att;
        y[i] = (float)std::tanh(1.6 * (tone + thump)) * 0.9f;
    }
    return y;
}
std::vector<float> synthTail(int variant, int rate) {
    // dull noise that swells for a moment and dies away over about a second, with a few early echoes off the roof
    int n = (int)(1.5 * rate);
    std::vector<float> dry(n), y(n, 0.f);
    Rng r{9876u + 131u * (unsigned)variant};
    double a = 0, b = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / rate;
        a += (r.next() - a) * 0.12; b += (a - b) * 0.12;   // two gentle low-passes: a dull rumble
        double env = std::min(1.0, t / 0.02) * std::exp(-t * 4.2);
        dry[i] = (float)(b * 4.0 * env);
    }
    const double taps[][2] = {{0, 1.0}, {0.023, 0.55}, {0.061, 0.4}, {0.112, 0.28}, {0.17, 0.18}};
    for (auto& tp : taps) {
        int d = (int)((tp[0] + 0.004 * variant) * rate);
        for (int i = 0; i + d < n; i++) y[i + d] += (float)(dry[i] * tp[1]);
    }
    float pk = 1e-6f;
    for (float v : y) pk = std::max(pk, std::fabs(v));
    for (float& v : y) v *= 0.85f / pk;
    return y;
}
