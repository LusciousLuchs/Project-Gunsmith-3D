// Gunsmith3D - solver internals shared between the core's source files (not part of the public API).
#pragma once
#include "solver.hpp"

namespace sim {
// XPBD position constraint: move pA (on A) toward pB (on B) along n by c. Welded pieces push their assembly.
void solve(Body& A, Body& B, V2 pA, V2 pB, double nx, double ny, double c);
void placePieces(Body& r);
void bbox(Body& b);
void contacts(Body& A, Body& B, double mu);
// Is local point l (m) inside A's solid: its outline less its cuts (cavities)? If so, the nearest surface point there
// (an outline edge outside the cuts, or a cut's wall) and how far it is. For code that tests samples itself.
bool solidPoint(const Body& A, V2 l, V2& bq, double& d);  // push B's boundary samples out of A, with friction
bool makeBody(const BodyDef& d, Body& b);
// Room for a spawned body (magazine, round or case): a removed one is reused before the reserve is used up.
// `def` is where its BodyDef goes; returns false if there's no room. Put the body in with placeSpawned.
bool spawnSlot(Sim& S, Body** slot, BodyDef** def);
Body* placeSpawned(Sim& S, Body* slot, const Body& b);
void retireBody(Sim& S, Body& b);            // take a body out of the test (it can be reused later)
void resample(Body& b);  // contact points along the outline

// cartridges.cpp
void cartridgeSetup(Sim& S);                 // after build
void cartridgeForces(Sim& S);                // before integration: magazine followers
void cartridgeStep(Sim& S);                  // after the main constraints: feeding, chambering, extraction, jams
void legacyRoundsStep(Sim& S);               // loose and hand-loaded rounds only (simplified feeding)
}
