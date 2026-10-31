// Gunsmith3D - Synthesised sound effects.
#pragma once
#include "app/app.hpp"

struct Sfx { Sound bang{}, clack{}, click{}, tick{}; bool ok = false; };  // tick: interface
void uiTick();  // the soft interface click
extern Sfx SFX;

void initSfx();
