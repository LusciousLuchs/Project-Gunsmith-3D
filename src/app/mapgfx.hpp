// Gunsmith3D - Drawing maps: generated textures for each material, brush faces turned into meshes, sun and sky.
#pragma once
#include "raylib.h"
#include "map.hpp"
#include "app/gfx.hpp"
#include <vector>

void mapGfxInit();                              // makes the material textures (once)
Texture2D mapTexture(int mat);
void mapBuildMeshes(const gmap::Map& m);        // after the map changes
void mapApplyLook(const gmap::Look& L);         // sun, sky light and fog for the lit shader
void mapRestoreLook();                          // back to the workshop lighting
// The map's lights for the lit shader: `extra` first (a gun's light, a muzzle flash), then the nearest to the eye
void mapSetLamps(const gmap::Map& m, gmap::V3 eye, const std::vector<Lamp>& extra = {});
void mapWorkLight(int mode);                    // the level editor: 0 as players see it, 1 a work light (dark maps still show), 2 fullbright
void mapDrawWorld();                            // the meshes built last
void mapDrawSky(const gmap::Look& L, float x, float y, float w, float h, float pitch, float fovY);  // screen space, before 3D
Color mapSkyHorizon(const gmap::Look& L);
// Begin a 3D pass in a part of the window. Pair with EndMode3D() and then mapEndViewport().
void mapBeginViewport(const Camera3D& cam, float x, float y, float w, float h);
void mapEndViewport();
