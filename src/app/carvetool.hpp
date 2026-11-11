// Gunsmith3D - the Carve tool (see carvetool.cpp).
#pragma once
#include <string>
#include "design.hpp"
#include "raylib.h"

bool carvePick(int& body, Vector3& p, Vector3& n);  // the part surface under the cursor (world point, normal facing us)
void carveInput(bool overUI);                       // Build mode, Carve tool: press on a surface, drag to size, release
void drawCarveOverlay();
bool cutPickInput(bool overUI);                    // "Pick cuts in the view": a press on a cut picks it (a drag moves it)
std::string cutLabel(const CarveDef& c, int k);    // its name, or "Cut 3"                            // the cutter under the cursor and the selected part's carves
