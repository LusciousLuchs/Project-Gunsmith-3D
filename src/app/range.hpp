// Gunsmith3D - the firing range (see range.cpp): first person on a map, with a gun you built.
#pragma once
#include "design.hpp"
#include "map.hpp"

void enterRange(const Design& gun, const gmap::Map& map);   // from the main menu's picker
void leaveRange();                                          // (backToMenu calls it)
bool inRange();
void rangeUpdate(float dt, bool modal);
void rangeRender();                                         // the 3D view
void rangeHud();                                            // 2D, after it
void rangeMapChanged();                                     // (map editor) its meshes must be built again
