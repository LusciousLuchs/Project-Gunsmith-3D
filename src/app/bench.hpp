// Gunsmith3D - Test mode: simulation playback, grabbing, effects.
#pragma once
#include "solver.hpp"
#include "app/app.hpp"

void testInput(bool overUI);
// The selector key (bench and range): a drawn lever bound to "selector" is pushed while it's held; otherwise the fire
// control group's selector goes to its next position (with a click and a note)
void turnSelector(sim::Sim& S, bool held, bool pressed);
void stepSim(float dt);
void stepEffects(float dt);
