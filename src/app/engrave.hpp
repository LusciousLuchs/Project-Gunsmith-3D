// Gunsmith3D - the Engrave tool (see engrave.cpp).
#pragma once
#include "raylib.h"

void engraveInput(bool overUI);          // Build mode, Engrave tool: draw, write or erase on the selected part
void drawMarks(int tmpl, Matrix M);      // (3D) part tmpl's engravings, where the part is drawn (M: its drawing transform)
void drawEngravePreview();               // (3D) Text: the words under the cursor before the click
