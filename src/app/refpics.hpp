// Gunsmith3D - reference pictures (0.8.62): a photo or drawing of a real gun behind the designer's side view, and
// another under its top view, scaled to the real gun's size by marking a length you know, levelled by marking a line
// that should run straight, and traced over. They're saved with the blueprint (the picture files in References).
#pragma once
#include "design.hpp"
#include <string>

RefPicture& refOf(int view);                    // 0 the side view, 1 the top view
bool refLoad(int view, const std::string& path); // copy the picture into References, lay it over the gun at about its size
void refDrawBehind();                            // in the 3D pass, before the parts (the designer only)
void refDrawFront();                             // after the parts: pictures set to show in front
void refOverlay();                               // the clicks of a scale or level being marked, in 2D
bool refInput(bool overUI);                      // true while the mouse is marking or moving a picture

// what the mouse is doing with a picture: 0 nothing, 1 moving it, 2 marking a known length, 3 marking a level line
int refMode();
void refSetMode(int view, int mode);
int refView();
bool refMarked();                                // a length has been marked: the panel asks how long it really is
double refMarkedMm();                            // ...and how long it is now (mm)
void refApplyLength(double realMm);              // scale the picture so the marked length is realMm
bool refPictureMissing(int view);                // its file isn't in References (a blueprint from someone else)
