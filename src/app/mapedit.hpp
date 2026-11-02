// Gunsmith3D - The map editor: build ranges out of brushes (Radiant style), texture them, place the player start,
// and walk them in first person. Maps are plain JSON in the maps folder, free to share and edit.
#pragma once
#include "map.hpp"
#include "player.hpp"

#include <string>
#include <vector>

enum class MTool { Select, Box, Wedge, Cylinder, Stairs, Clip, Start };

struct MapEd {
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
    gmap::Player pl;
    std::string status;                    // one-line message in the panel
};
extern MapEd ME;

void enterMapEditor();
void leaveMapEditor();                     // autosaves, gives the mouse back
void mapEditorUpdate(float dt, bool modal); // input, walking, rebuilding the meshes
void mapEditorRender();                    // the 3D view and its overlay (between BeginDrawing and the panels)

// operations (the panel and the keys both use these)
void meSnapshot();                         // call before changing ME.map
void meChanged();                          // call after
void meUndo();
void meRedo();
void meDelete();
void meDuplicate();
void meRotate(double degrees);
void meMirror(int axis);
void meHollow();
void meApplyMaterial(int mat);
void meNew();
void meLoadBuiltin();
bool meSave();                             // to user/maps/<name>.gsmap
bool meOpen(const std::string& path);
std::vector<std::string> meMapFiles();
void meWalk(bool fromCamera);
void meFocus();
std::string meLen(double metres);          // in the player's units
