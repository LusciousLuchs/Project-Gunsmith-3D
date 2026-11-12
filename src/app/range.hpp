#include <string>
// Gunsmith3D - the firing range (see range.cpp): first person on a map, with a gun you built.
#pragma once
#include "design.hpp"
#include "map.hpp"

void enterRange(const Design& gun, const gmap::Map& map);   // from the main menu's picker
void rangeShotSetup(double yawDeg, double pitchDeg, double armorBar);   // (screenshots) where you look, and your armour
void leaveRange();                                          // (backToMenu calls it)
bool inRange();
void rangeUpdate(float dt, bool modal);
void rangeRender();                                         // the 3D view
void rangeHud();                                            // 2D, after it
void rangeMapChanged();                                     // (map editor) its meshes must be built again
bool rangeSpawnerOpen();                                    // choosing a gun at a gun spawner
void rangeSpawnerWindow();                                  // its list (ImGui)

void openRangeFitting();   // an attachment crate: the held gun on the fitting bench
void backToRange();
void rangeGadgetWindow();   // Alt held on the range: switch the attachments on and off
void rangeShotExtras(const std::string& loadout, bool aim);   // (screenshots)
