// Gunsmith3D - solver internals shared between the core's source files (not part of the public API).
#pragma once
#include "solver.hpp"

namespace sim {
// XPBD position constraint: move pA (on A) toward pB (on B) along n by c. Welded pieces push their assembly.
void solve(Body& A, Body& B, V2 pA, V2 pB, double nx, double ny, double c);
void placePieces(Body& r);
void bbox(Body& b);
void contacts(Body& A, Body& B, double mu);  // push B's boundary samples out of A, with friction
bool makeBody(const BodyDef& d, Body& b);
void resample(Body& b);  // contact points along the outline

// cartridges.cpp
void cartridgeSetup(Sim& S);                 // after build
void cartridgeForces(Sim& S);                // before integration: magazine followers
void cartridgeStep(Sim& S);                  // after the main constraints: feeding, chambering, extraction, jams
void legacyRoundsStep(Sim& S);               // loose and hand-loaded rounds only (simplified feeding)
}
