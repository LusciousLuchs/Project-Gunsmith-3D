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
double niceStep(Q q, double storedStep);
std::string fracInch(double mm);           // nearest 32nd of an inch: 1 7/32"   // an input step that is round in the shown unit

// Read a typed value: numbers, fractions ("1/16", "1 1/2"), each with an optional unit, added or taken away
// ("1/4 in + 2 mm", "5' 3\"", "0.5mm"). A number without a unit is in the shown unit. Either system's unit is
// understood, whatever is shown (hybrid guns). False if it can't be read. `out` is the stored value.
bool parseQ(Q q, const char* text, double& out);
// The value as it would be typed back: "12.5 mm", or "1/16 in" for an exact small fraction of an inch.
std::string typedQ(Q q, double v, int decimals = -1);
