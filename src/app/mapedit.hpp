// Gunsmith3D - The map editor: build ranges out of brushes (Radiant style), texture them, place the player start,
// and walk them in first person. Maps are plain JSON in the maps folder, free to share and edit.
#pragma once
#include "map.hpp"
#include "player.hpp"

#include <string>
#include <vector>

enum class MTool { Select, Box, Wedge, Cylinder, Stairs, Clip, Start, Npc, Spawner, Armor, Crate, Light, Ammo, Weapons, Station, Door, Target, Furniture, Prop, Radio };

struct MapEd {
    std::string doorStyle = "wood";       // the Door tool: wood, metal, glass
    double doorWidth = 0.9;
    gmap::Map map;
    unsigned version = 1, built = 0;      // the meshes are rebuilt when these differ
    bool dirty = false;                    // changed since the last save
    std::vector<std::string> undo, redo;
    std::vector<int> sel;                  // selected brushes
    int face = -1;                         // selected face of sel[0] (Ctrl+click), or -1 for whole brushes
    int ent = -1;                          // selected entity
    MTool tool = MTool::Select;
    double grid = 0.25;                    // metres
    double height = 2.5;                   // height of new brushes
    double wall = 0.2;                     // wall thickness for Make hollow
    int mat = 1;                           // material for new brushes
    int cylSides = 12, stairSteps = 8;
    bool showGrid = true;
    bool rotateMode = false;               // Select: rotation rings on the selection (drag a ring to turn it about that axis)
    int lightMode = 1;                     // 0 as players will see it, 1 a work light (dark maps still show), 2 fullbright (no shading, no fog)
    // camera
    gmap::V3 cam{-10, 4, 0};
    double yaw = 0, pitch = -0.25;         // radians, yaw 0 looks along +X
    double flySpeed = 8;                   // m/s
    bool top = false;                      // looking straight down, orthographic
    gmap::V3 topC{0, 0, 0};
    double topH = 40;                      // metres visible top to bottom
    // clip tool
    std::vector<gmap::V3> clipPts;
    gmap::V3 clipFaceN;
    bool clipFlip = false;
    // walking the map
    bool walking = false;
    std::string careerFile;                // career mode: editing your workshop (saved here, not in Levels)
    std::string stationKind = "bench";     // the Station tool: bench, lathe, mill, qc, counter
    gmap::Player pl;
    std::string status;                    // one-line message in the panel
    std::string npcPick;
    std::string furnPick = "builtin:shelf";   // the Furniture tool's piece
    std::string propPick = "builtin:crate";   // the Prop tool's thing to pick up (a prop, or a gun: "gun:...")                   // the NPC the NPC spawn tool places (its name)
};
extern MapEd ME;

void enterMapEditor();
// career mode: your workshop in the level editor (your sandbox map is set aside meanwhile); walk: straight in, on foot
void enterCareerWorkshop(const std::string& file, bool walk);
void leaveCareerWorkshop();                // saves it and puts your sandbox map back (also when leaving the editor)
void leaveMapEditor();                     // autosaves, gives the mouse back
void mapEditorUpdate(float dt, bool modal); // input, walking, rebuilding the meshes
void mapEditorRender();                    // the 3D view and its overlay (between BeginDrawing and the panels)

// operations (the panel and the keys both use these)
void meSnapshot();                         // call before changing ME.map
void meChanged();                          // call after
void meUndo();
void meSaveQuiet();                        // (career) write the property's map, no message
void meReplaceMap(const gmap::Map& m);   // the whole map swapped for another (undoable)
void meRedo();
void meDelete();
void meDuplicate();
void meSelectAll();   // Ctrl+A: every block you can edit
void meCopy();        // Ctrl+C: the selected blocks or thing
void mePaste();       // Ctrl+V: a copy where the mouse points
void meRotate(double degrees);
void meMirror(int axis);
void meHollow();
void meApplyMaterial(int mat);
void meNew();
void meLoadBuiltin();
bool meSave();                             // to user/maps/<name>.gsmap
bool meOpen(const std::string& path);
// groups: the selected brushes (and the selected object) saved as one, placed again as many times as you like
bool meSaveGroup(const std::string& name); // to user/groups/<name>.json
std::vector<std::string> meGroupFiles();   // their names
bool mePlaceGroup(const std::string& name);   // in front of the camera, selected so it can be moved straight away
// furniture: the selected brushes (and the props and guns standing on them) made into one piece, or a prop
bool meMakeFurniture(bool prop, const std::string& name);
void meBreakApart();
void meRebake();                          // furniture moved or turned: its blocks follow                       // the selected furniture or prop back into blocks (and its things)
bool meSaveFurniture();                    // the selected furniture or prop into your Furniture folder
std::vector<std::string> meMapFiles();
void meWalk(bool fromCamera);
void meStopWalking();                      // back to the editor (the mouse freed)
void meFocus();
std::string meLen(double metres);          // in the Level editor's unit (CFG.mapUnit: m, cm, ft and in, in, yd)
const char* meUnitName();                  // "m", "cm", "ft", "in", "yd"
double meUnitPerMetre();                   // how many of that unit in a metre
