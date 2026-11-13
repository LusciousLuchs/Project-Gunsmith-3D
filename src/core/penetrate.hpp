// Gunsmith3D - bullets through wood: a shot that hits wood (plywood, planks, parquet, a wooden door) goes through if
// the wood is thin enough for it, and comes out slower. No graphics.
// The model: wood pushes back like a very thick fluid, so the bullet's speed falls off as exp(-x / L) with
// L = 2 m / (rho Cd A) (its mass over the wood's density, its frontal area and a drag factor); below STOP m/s it's
// stuck. Heavier, narrower and faster bullets go further. Tuned so a 9 mm pistol round gets through about 15 cm of
// wood (9 cm for a buckshot pellet) and a 5.56 rifle round about 31 cm, a little short of what's measured in pine boards.
#pragma once
#include "map.hpp"
#include <string>

namespace pen {
constexpr double WOOD_DENSITY = 550, WOOD_CD = 5, STOP = 60;   // kg/m3, drag factor, m/s
constexpr double DOOR_THICK = 0.045;                             // a wooden door's slab, m
bool woodLike(const std::string& material);                      // a map material that's wood
// a bullet (kg, diameter m) at `speed` through `thick` m of wood: its speed coming out, or 0 if it stops inside
double exitSpeed(double mass, double dia, double speed, double thick);
// how far a bullet gets into wood before it stops (m)
double reach(double mass, double dia, double speed);
// a convex brush: how far along `dir` (unit) from `o` (on or in it) until the ray leaves it
double brushExit(const gmap::Brush& b, gmap::V3 o, gmap::V3 dir);
}  // namespace pen
