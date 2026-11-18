// Gunsmith3D - modelled characters: a skinned .glb/.gltf (one armature) from the Models folder, drawn over an NPC's
// bones instead of their shapes, its joints following the bones (core/rig: by name, Mixamo's, Blender's and others).
#pragma once
#include "npc.hpp"
#include "raylib.h"
#include <string>
#include <vector>

struct CharModelInfo { bool ok = false; std::string error, report; int joints = 0, triangles = 0; };
CharModelInfo charModelInfo(const std::string& rel, const NpcDef& n);   // (the panels: loads it if need be)
// NPC n drawn as its model: `M` each bone's frame in the world, as drawCharacterBody takes them. False if it has no
// model or the model can't be used (then draw the bones as usual).
bool drawCharModel(const NpcDef& n, const std::vector<Matrix>& M, const Color* tint = nullptr);
bool charModelUsable(const NpcDef& n);
std::vector<std::string> charModelFiles(const std::string& sub = "");   // .glb/.gltf in Models (paths under it)
std::string shopPersonModel(int id, bool female);                       // one from Models/Shop people for this person ("" if none)
void charModelsForget();                                                  // (the files changed: load again when next drawn)
