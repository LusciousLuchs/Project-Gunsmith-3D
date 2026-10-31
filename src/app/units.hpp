// Gunsmith3D - Display units. The design and the simulation always work in millimetres, grams, metres per second,
// newtons and so on; these only convert what the player sees and types.
#pragma once
#include <string>

enum class Q { Length, BulletMass, PartMass, Velocity, Energy, Force, SpringRate, Pressure, Distance, Impulse, Damping };

double toDisp(Q q, double v);              // stored value -> shown value
double fromDisp(Q q, double v);            // shown value -> stored value
const char* unitOf(Q q);                   // "mm" or "in", "g" or "gr" ...
int decimalsOf(Q q);                       // sensible precision for the shown value
std::string fmtQ(Q q, double v, int decimals = -1);   // "12.5 mm"
double niceStep(Q q, double storedStep);   // an input step that is round in the shown unit
