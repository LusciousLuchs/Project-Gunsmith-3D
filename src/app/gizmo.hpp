// Gunsmith3D - rotation rings: a joint or part's own turning axes, drawn as coloured circles round it. Drag along a
// ring to turn it about that axis (Ctrl: 15 degree steps). Shared by the NPC editor, the character maker, Pose and
// animations, and the designer; each says which axes it has and what turning about them changes.
#pragma once
#include "raylib.h"
#include <functional>

using ToScreen = std::function<Vector2(Vector3)>;

struct Rings {
    Vector3 c{};                 // the pivot (world)
    Vector3 ax[3]{};             // the axes (world, unit); an axis with no length isn't shown
    const char* label[3] = {nullptr, nullptr, nullptr};
    float r = 0;                 // the radius (world), from ringsFit
    // the drag
    int hot = -1, axis = -1;     // the ring under the mouse; the one being dragged
    Vector2 press{}, tan{};      // where the drag began, and the ring's way round there on the screen (pixels per radian)
    double deg = 0;              // how far it's turned since the press (+: the right-hand way about the axis)
};

void ringsFit(Rings& g, const ToScreen& ts, float px = 70);   // a radius about `px` pixels on screen
int ringsPick(const Rings& g, const ToScreen& ts, Vector2 m);   // the ring under the mouse (within 7 px), or -1
void ringsBegin(Rings& g, int axis, const ToScreen& ts, Vector2 m);
double ringsDrag(Rings& g, Vector2 m, bool snap);              // degrees since the press
void ringsDraw(const Rings& g, const ToScreen& ts, Vector3 eye); // (2D, after the 3D pass) the near half bright, the far half faint
// How many degrees `probe` goes round `axis` (about `c`) for each degree the value goes up (signed): dividing the ring's
// turn by this makes the joint follow the ring exactly, wherever the joint really pivots
double ringsGain(const std::function<Vector3(double)>& probe, Vector3 c, Vector3 axis);
// The value that turns `probe` `deg` degrees round `axis`, starting from deg / gain and correcting a few times (for joints
// that don't turn evenly with their value, like a hand's roll or a finger's spread)
double ringsSolve(const std::function<Vector3(double)>& probe, Vector3 c, Vector3 axis, double deg, double gain);
// Which way a value turns things: +1 if raising it by one moves `probe` the right-hand way about `axis` round `c`
double ringsSign(const std::function<Vector3(double)>& probe, Vector3 c, Vector3 axis);
