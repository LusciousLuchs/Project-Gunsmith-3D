// Gunsmith3D - Rebindable controls. Every keyboard action goes through here so players can rebind it.
// Ctrl+ combinations keep Ctrl and rebind the letter; Escape always opens the menu.
#pragma once

enum class Act {
    // test bench
    Fire, MagRelease, Reload, NewMag, LooseRound, Pause, Restart, Control1, Control2, Control3, Control4,
    // designer
    Undo, Delete, Duplicate, Knife, Finish,
    // view
    ToggleMode, FitView, SideView, XRay, Cutaway, Screenshot,
    // first person (walking a map)
    MoveFwd, MoveBack, MoveLeft, MoveRight, Jump, Sprint, Crouch, Prone, LeanLeft, LeanRight,
    // map editor
    Redo, FlyDown, FlyUp, MapRotate, MapHollow, MapWalk, MapTopView, MapFocus,
    // test bench (added later, so kept at the end: settings files store keys by name)
    FillMags, WorkAction,
    Count
};

struct ActInfo {
    const char* id;       // saved in the settings file
    const char* name;     // shown to the player
    const char* group;    // Test bench, Designer, View, First person, Map editor
    int def;              // default key
    bool ctrl;            // needs Ctrl held
};
const ActInfo& actInfo(Act a);
int keyOf(Act a);
void bindKey(Act a, int key);   // also clears that key from any other action in the same group
void resetKeys();
bool actPressed(Act a);
bool actDown(Act a);
const char* keyName(int key);
