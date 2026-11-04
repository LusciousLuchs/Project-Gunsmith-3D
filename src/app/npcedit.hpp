// Gunsmith3D - the NPC editor (Map editing tools > NPC editor): build an enemy or character from bones, shape and
// pose them, and set how it fights. The look and the pose are done here; bringing them to life on maps comes next.
#pragma once
#include "npc.hpp"
#include <string>
#include <vector>

struct NpcEdit {
    NpcDef npc;
    int sel = 0;                       // the bone selected
    bool endPicked = false;            // its end's dot was clicked once: a second press drags it (aims the bone)
    bool aiming = false;               // dragging it
    float yaw = 0.7f, pitch = 0.25f, dist = 32;   // the camera, round the NPC (units: 1 = 10 cm)
    float targetY = 9;
    std::vector<std::string> undo, redo;
    std::string last;                  // the design as last saved to the undo list
    std::string file;                  // its file name in user/npcs (without .json), once saved or opened
    bool dirty = false;
};
extern NpcEdit NE;

void enterNpcEditor();
void leaveNpcEditor();
void npcEditorUpdate(float dt, bool modal);
void npcEditorRender();
void npcFrame();                       // the camera on the whole NPC
void npcCommit();                      // an edit is finished: it goes on the undo list and is autosaved
void npcUndo();
void npcRedo();
bool npcSave(const std::string& name); // to user/npcs/<name>.json
bool npcOpen(const std::string& file); // a file in user/npcs
std::vector<std::string> npcFiles();   // the names in user/npcs
std::vector<std::string> npcGunFiles(); // the blueprints in user/guns made for NPCs
// An NPC drawn standing at `origin` (drawing units), facing `yawDeg` (0 along +x, as map entities face), at
// `unitsPerMm` (the NPC editor: 0.01; maps, in metres: 0.001). `highlight`: a bone drawn brighter (-1: none).
#include "raylib.h"
void drawNpcAt(const NpcDef& n, Vector3 origin, float yawDeg, float unitsPerMm, int highlight = -1);
// NPCs and maps: a map carries copies of the NPCs its spawn points use, so a shared map brings its enemies along
namespace gmap { struct Map; }
bool npcByName(const gmap::Map& m, const std::string& name, NpcDef& out);   // the map's own copy, else user/npcs
void embedMapNpcs(gmap::Map& m);       // before saving a map: copies of the NPCs its spawn points use
int installMapNpcs(const gmap::Map& m);   // after opening one: its NPCs into user/npcs (those not there yet); how many
