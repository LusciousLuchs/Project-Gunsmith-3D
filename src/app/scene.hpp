// Gunsmith3D - The look of the workspace: backdrop, drafting grid and bench floor, per the chosen theme.
// Purely cosmetic; nothing here touches the simulation.
#pragma once
#include "app/app.hpp"

struct SceneLook {
    Color inner, outer;          // backdrop: centre and edges (radial), or top and bottom (vertical)
    float radial;                // 1 radial vignette, 0 vertical gradient
    float grain;                 // dither / paper grain, 0..1 colour units
    Color fine, minor, major;    // grid lines, alpha = strength
    Color floor;                 // bench floor fill
    Color fog;                   // far things fade towards this
    bool studs;                  // the classic studded baseplate instead of a drawn floor
    Color ink;                   // outlines of see-through parts: reads against the backdrop
};
SceneLook sceneLook();
const char* backdropNames();     // for a combo: "Blueprint\0Drafting paper\0..."

void initScene();
void drawBackdrop(float x, float y, float w, float h);   // screen space, before the 3D view
void drawBoard(float zBack);                              // drafting sheet behind the parts, sized to the design
void drawFloor(float y);                                  // the bench floor at height y (world units)
// A horizontal grid at height y (world units), for the map editor: steps are fine / minor / major spacing.
void drawWorldGrid(float y, float cx, float cz, float radius, const float steps[3], Color fine, Color minor, Color major);
