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
