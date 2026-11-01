// Gunsmith3D - the Carve tool (see carvetool.cpp).
#pragma once
#include "raylib.h"

bool carvePick(int& body, Vector3& p, Vector3& n);  // the part surface under the cursor (world point, normal facing us)
void carveInput(bool overUI);                       // Build mode, Carve tool: press on a surface, drag to size, release
void drawCarveOverlay();                            // the cutter under the cursor and the selected part's carves
