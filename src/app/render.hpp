// Gunsmith3D - 3D scene drawing.
#pragma once
#include "app/app.hpp"

struct RB { int tmpl; Matrix M; float roll; sim::Body* sb; };  // one part to draw: template index, pose, live body (Test)
// A rotating bolt's handle as drawn: it turns with the bolt about the bore axis.
struct BoltHandle {
    Vector3 base, tip, axis;   // world: root on the bore axis, knob, bore direction
    float radius = 0;          // knob distance from the axis (world units)
    double localMM = 0;        // root position along the bolt from its centre (mm)
    float restDeg = 20;        // at roll 0 the handle points this far below level
};
bool boltHandle(const RB& r, BoltHandle& h);

std::vector<RB> renderList();
bool mainPart(const sim::Body& b);
float plateY();
std::vector<V2> caseProfile(const Cartridge& c);
std::vector<V2> bulletProfile(const Cartridge& c);
std::vector<V2> hullProfile(const Cartridge& c, bool fired);  // shotshells only; empty otherwise
void drawScene();

// The designer's workbench (for scale): where its top is and where the ruler lies, from the design's size (mm).
struct BenchLayout { double x0, x1, top, zFront, zBack, rulerX0, rulerLen, rulerZ; bool ok = false; };
BenchLayout benchLayout();
