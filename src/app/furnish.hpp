// Gunsmith3D - Furniture, props and guns placed on maps: the choices the Level editor offers, your own saved pieces
// (the Furniture folder), and drawing them. The blocks and the rules live in core (map.hpp, furniture.cpp).
#pragma once
#include "map.hpp"
#include "design.hpp"
#include "raylib.h"

#include <string>
#include <vector>

struct FurnChoice { std::string pick, label; };
// furniture (props = false), or things to pick up (props = true: the built-in props, your own, then guns as
// "gun:preset:<key>" / "gun:<blueprint>")
std::vector<FurnChoice> furnitureChoices(bool props);
bool furnitureFromPick(const std::string& pick, gmap::Entity& e);   // the piece round the origin, facing +X
bool saveFurniture(const gmap::Entity& e, std::string& file);        // to the Furniture folder, by its name
// guns placed on maps (entity "gun_item", ref "preset:<key>" or a blueprint's name)
const Design* gunItemDesign(const std::string& ref);   // loaded once; nullptr if it's gone
std::string gunItemLabel(const std::string& ref);
Matrix gunItemMatrix(const Design& d, gmap::V3 pos, double yawDeg);   // lying on its side at pos, muzzle along yaw
double gunItemLength(const Design& d);                                // metres
Matrix entityMatrix(gmap::V3 pos, double yawDeg);
// the props and guns standing on a map and on its furniture (selEnt: picked, outlined)
void drawMapThings(const gmap::Map& m, int selEnt);
