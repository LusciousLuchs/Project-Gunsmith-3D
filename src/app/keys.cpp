// Gunsmith3D - Rebindable controls (see keys.hpp).
#include "app/keys.hpp"
#include "app/settings.hpp"
#include "raylib.h"

#include <cstdio>
#include <cstring>
#include <string>

static const ActInfo ACTS[(int)Act::Count] = {
    {"fire", "Fire / pull the trigger", "Test bench", KEY_SPACE, false},
    {"magrelease", "Magazine release (hold)", "Test bench", KEY_M, false},
    {"reload", "Quick reload", "Test bench", KEY_R, false},
    {"newmag", "Fresh magazine on the bench", "Test bench", KEY_N, false},
    {"round", "Loose round on the bench", "Test bench", KEY_L, false},
    {"pause", "Pause", "Test bench", KEY_P, false},
    {"restart", "Restart the test", "Test bench", KEY_BACKSPACE, false},
    {"k1", "Your control 1", "Test bench", KEY_ONE, false},
    {"k2", "Your control 2", "Test bench", KEY_TWO, false},
    {"k3", "Your control 3", "Test bench", KEY_THREE, false},
    {"k4", "Your control 4", "Test bench", KEY_FOUR, false},
    {"undo", "Undo", "Designer", KEY_Z, true},
    {"delete", "Delete selection", "Designer", KEY_DELETE, false},
    {"duplicate", "Duplicate part", "Designer", KEY_D, true},
    {"knife", "Knife tool", "Designer", KEY_K, false},
    {"finish", "Finish part or track", "Designer", KEY_ENTER, false},
    {"mode", "Switch designer / bench", "View", KEY_TAB, false},
    {"fit", "Fit view", "View", KEY_F, false},
    {"side", "Side view (flat) / back to 3D", "View", KEY_V, false},
    {"xray", "X-ray", "View", KEY_X, false},
    {"cutaway", "Cutaway", "View", KEY_C, false},
    {"screenshot", "Screenshot", "View", KEY_F12, false},
    {"fwd", "Move forward", "First person", KEY_W, false},
    {"back", "Move back", "First person", KEY_S, false},
    {"left", "Move left", "First person", KEY_A, false},
    {"right", "Move right", "First person", KEY_D, false},
    {"jump", "Jump", "First person", KEY_SPACE, false},
    {"sprint", "Sprint (hold)", "First person", KEY_LEFT_SHIFT, false},
    {"crouch", "Crouch", "First person", KEY_C, false},
    {"prone", "Go prone", "First person", KEY_Z, false},
    {"leanl", "Lean left (hold)", "First person", KEY_Q, false},
    {"leanr", "Lean right (hold)", "First person", KEY_E, false},
    {"redo", "Redo", "Map editor", KEY_Y, true},
    {"flydown", "Fly down (camera)", "Map editor", KEY_Q, false},
    {"flyup", "Fly up (camera)", "Map editor", KEY_E, false},
    {"maprotate", "Turn selection 90 degrees", "Map editor", KEY_R, false},
    {"maphollow", "Make hollow (walls)", "Map editor", KEY_H, false},
    {"mapwalk", "Walk the map", "Map editor", KEY_F5, false},
    {"maptop", "Top view / 3D view", "Map editor", KEY_V, false},
    {"mapfocus", "Focus on the selection", "Map editor", KEY_F, false},
    {"fillmags", "Top up magazines", "Test bench", KEY_T, false},
    {"work", "Work the action (bolt, slide, lever)", "Test bench", KEY_B, false},
    {"inspect", "Inspect the gun", "Test bench", KEY_I, false},
    {"use", "Use (take guns from a spawner, pick one up)", "First person", KEY_F, false},
    {"dropgun", "Drop the gun in your hands", "First person", KEY_G, false},
    {"perf", "Performance overlay (frame rate, frame time)", "View", KEY_F3, false},
    {"fullscreen", "Fullscreen / window", "View", KEY_F11, false},
    {"gadgets", "Lights and lasers on / off", "First person", KEY_T, false},
    {"melee", "Stab (a bayonet) or hit with the gun", "First person", KEY_V, false},
    {"sliderelease", "Slide / bolt release (held open on empty)", "Test bench", KEY_K, false},
    {"ready", "Carry the gun: high ready / low ready / back", "First person", KEY_X, false},
    {"pointshoot", "Point shooting (hold; the middle mouse button does it too)", "First person", 0, false},
    {"holster", "Holster / draw the gun", "First person", KEY_H, false},
};

const ActInfo& actInfo(Act a) { return ACTS[(int)a]; }
int keyOf(Act a) {
    auto it = CFG.keys.find(ACTS[(int)a].id);
    return it != CFG.keys.end() ? it->second : ACTS[(int)a].def;
}
void bindKey(Act a, int key) {
    for (int i = 0; i < (int)Act::Count; i++)
        if (i != (int)a && keyOf((Act)i) == key && std::strcmp(ACTS[i].group, ACTS[(int)a].group) == 0 && ACTS[i].ctrl == ACTS[(int)a].ctrl)
            CFG.keys[ACTS[i].id] = 0;  // that key now belongs to this action
    CFG.keys[ACTS[(int)a].id] = key;
}
void resetKeys() { CFG.keys.clear(); }
static bool ctrlHeld() { return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL); }
bool actPressed(Act a) {
    int k = keyOf(a);
    if (k <= 0 || ACTS[(int)a].ctrl != ctrlHeld()) return false;
    if (a == Act::Finish && IsKeyPressed(KEY_KP_ENTER)) return true;
    return IsKeyPressed(k);
}
bool actDown(Act a) {
    int k = keyOf(a);
    return k > 0 && ACTS[(int)a].ctrl == ctrlHeld() && IsKeyDown(k);
}
const char* keyName(int k) {
    static char buf[24];
    if (k <= 0) return "(none)";
    if (k >= KEY_A && k <= KEY_Z) { std::snprintf(buf, sizeof buf, "%c", 'A' + (k - KEY_A)); return buf; }
    if (k >= KEY_ZERO && k <= KEY_NINE) { std::snprintf(buf, sizeof buf, "%c", '0' + (k - KEY_ZERO)); return buf; }
    if (k >= KEY_F1 && k <= KEY_F12) { std::snprintf(buf, sizeof buf, "F%d", 1 + (k - KEY_F1)); return buf; }
    if (k >= KEY_KP_0 && k <= KEY_KP_9) { std::snprintf(buf, sizeof buf, "Num %d", k - KEY_KP_0); return buf; }
    switch (k) {
    case KEY_SPACE: return "Space"; case KEY_ENTER: return "Enter"; case KEY_TAB: return "Tab"; case KEY_BACKSPACE: return "Backspace";
    case KEY_DELETE: return "Delete"; case KEY_INSERT: return "Insert"; case KEY_HOME: return "Home"; case KEY_END: return "End";
    case KEY_PAGE_UP: return "Page Up"; case KEY_PAGE_DOWN: return "Page Down"; case KEY_LEFT: return "Left"; case KEY_RIGHT: return "Right";
    case KEY_UP: return "Up"; case KEY_DOWN: return "Down"; case KEY_LEFT_SHIFT: return "Left Shift"; case KEY_RIGHT_SHIFT: return "Right Shift";
    case KEY_LEFT_ALT: return "Left Alt"; case KEY_RIGHT_ALT: return "Right Alt"; case KEY_LEFT_CONTROL: return "Left Ctrl"; case KEY_RIGHT_CONTROL: return "Right Ctrl";
    case KEY_GRAVE: return "`"; case KEY_MINUS: return "-"; case KEY_EQUAL: return "="; case KEY_LEFT_BRACKET: return "[";
    case KEY_RIGHT_BRACKET: return "]"; case KEY_BACKSLASH: return "\\"; case KEY_SEMICOLON: return ";"; case KEY_APOSTROPHE: return "'";
    case KEY_COMMA: return ","; case KEY_PERIOD: return "."; case KEY_SLASH: return "/"; case KEY_CAPS_LOCK: return "Caps Lock";
    case KEY_KP_ENTER: return "Num Enter"; case KEY_KP_ADD: return "Num +"; case KEY_KP_SUBTRACT: return "Num -";
    case KEY_KP_MULTIPLY: return "Num *"; case KEY_KP_DIVIDE: return "Num /"; case KEY_KP_DECIMAL: return "Num .";
    }
    std::snprintf(buf, sizeof buf, "Key %d", k);
    return buf;
}
