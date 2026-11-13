// Gunsmith3D - bullets through wood (see penetrate.hpp).
#include "penetrate.hpp"

#include <algorithm>
#include <cmath>

namespace pen {
namespace {
double decayLength(double mass, double dia) {
    double area = 3.14159265358979 * dia * dia / 4;
    return 2 * std::max(1e-5, mass) / (WOOD_DENSITY * WOOD_CD * std::max(1e-8, area));
}
}  // namespace

bool woodLike(const std::string& m) { return m == "plywood" || m == "wood_planks" || m == "parquet" || m == "door_wood"; }
double exitSpeed(double mass, double dia, double speed, double thick) {
    if (thick <= 0) return speed;
    double v = speed * std::exp(-thick / decayLength(mass, dia));
    return v < STOP ? 0 : v;
}
double reach(double mass, double dia, double speed) {
    return speed <= STOP ? 0 : decayLength(mass, dia) * std::log(speed / STOP);
}
double brushExit(const gmap::Brush& b, gmap::V3 o, gmap::V3 dir) {
    double t = 1e9;
    for (auto& f : b.faces) {
        double dn = gmap::dot(f.n, dir);
        if (dn <= 1e-9) continue;   // (facing back along the ray: it can't be where it leaves)
        t = std::min(t, (f.d - gmap::dot(f.n, o)) / dn);
    }
    return std::max(0.0, t);
}
}  // namespace pen
