// Gunsmith3D - Display units (see units.hpp).
#include "app/units.hpp"
#include "app/settings.hpp"

#include <cmath>
#include <cstdio>

namespace {
struct UnitDef { const char* metric; const char* imperial; double toImperial; int decM, decI; };
const UnitDef UNITS[] = {
    {"mm", "in", 1 / 25.4, 1, 3},          // Length
    {"g", "gr", 15.4323584, 2, 1},          // BulletMass (powder too)
    {"g", "oz", 1 / 28.3495231, 0, 2},      // PartMass
    {"m/s", "ft/s", 3.2808399, 0, 0},       // Velocity
    {"J", "ft\xC2\xB7lbf", 0.737562149, 0, 0},  // Energy
    {"N", "lbf", 0.224808943, 0, 1},        // Force
    {"N/mm", "lb/in", 5.71014715, 2, 1},    // SpringRate
    {"MPa", "psi", 145.037738, 0, 0},       // Pressure
    {"m", "yd", 1.0936133, 0, 0},           // Distance
    {"N\xC2\xB7s", "lbf\xC2\xB7s", 0.224808943, 2, 3},  // Impulse
    {"N\xC2\xB7s/m", "lbf\xC2\xB7s/ft", 0.0685217659, 1, 2},  // Damping
};
bool imperial() { return CFG.units == Units::Imperial; }
}

double toDisp(Q q, double v) { return imperial() ? v * UNITS[(int)q].toImperial : v; }
double fromDisp(Q q, double v) { return imperial() ? v / UNITS[(int)q].toImperial : v; }
const char* unitOf(Q q) { return imperial() ? UNITS[(int)q].imperial : UNITS[(int)q].metric; }
int decimalsOf(Q q) { return imperial() ? UNITS[(int)q].decI : UNITS[(int)q].decM; }
std::string fmtQ(Q q, double v, int dec) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f %s", dec >= 0 ? dec : decimalsOf(q), toDisp(q, v), unitOf(q));
    return buf;
}
double niceStep(Q q, double st) {
    double s = toDisp(q, st);
    if (s <= 0) return 0;
    double p = std::pow(10.0, std::floor(std::log10(s))), m = s / p;
    return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}
