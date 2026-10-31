// Gunsmith3D - The UI typeface (Inter, embedded) for both the panels and text drawn over the 3D view.
#pragma once
#include "raylib.h"

struct FontData { const unsigned char* data; int size; };
FontData interRegular();
FontData interSemiBold();
void initViewFont();                                            // after the window opens
void viewText(const char* t, float x, float y, float size, Color c);  // text over the 3D view (size before UI scale)
float viewTextWidth(const char* t, float size);
