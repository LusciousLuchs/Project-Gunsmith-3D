// Gunsmith3D - Editor panels (Dear ImGui).
#pragma once
#include "app/app.hpp"
#include "imgui.h"
#include "app/units.hpp"

extern bool editedAny;  // set when a panel widget changed the design this frame

// common.cpp
void markEdit();
bool inputText(const char* label, std::string& s);
bool bodyCombo(const char* label, std::string& id, bool withWorld);
void numField(const char* label, double& v, double step, const char* fmt = "%.1f");
bool numFieldU(const char* label, double& v, double step, Q q, int decimals = -1, double lo = 0);  // shown in the player's units
void typeable();
bool texturePicker(std::string& file);  // "Plain" or one of the pictures in user/textures, with add / open folder
                 // after a slider: double-click it to type a value
bool tipHover();                 // hovered, and tooltips are on
ImVec4 colGood(); ImVec4 colBad(); ImVec4 colWarn(); ImVec4 colAccent();
extern ImFont* gFontBold;
void setupImGui();               // fonts, theme, scale
void applyUiScale();
float autoUiScale();                       // a scale that suits this screen
void setUiScale(float s, bool chosen);     // change it now (and remember it as chosen)
void fileButtons();                        // Save, Save as and Open, for the top bar
void check(const char* label, bool& v);
bool accentButton(const char* label, bool on, ImVec2 size = {0, 0});
const char* JT_LABEL(const std::string& t);
std::string jointDesc(const JointDef& j);
void panelHeader();
// property rows: a muted label on the left, the control filling the right
void prop(const char* label, const char* tip = nullptr);
bool pNumU(const char* label, double& v, double step, Q q, int dec = -1, const char* tip = nullptr, double lo = 0);  // lo: the smallest it goes (sizes: 0)
constexpr double SIGNED = -1e9;  // lo for offsets and positions, which can be negative
bool pNum(const char* label, double& v, double step, const char* fmt, const char* tip = nullptr);
bool pCheck(const char* label, bool& v, const char* tip = nullptr);
bool pText(const char* label, std::string& s);
bool pBody(const char* label, std::string& id, bool withWorld);
bool section(const char* name, bool open);   // a quiet fold-away heading
void heading(const char* t);                 // a quiet fixed heading
bool dangerButton(const char* label, ImVec2 size = {0, 0});
void hintOverlay();
void toastOverlay();
void npcToolbar();                // the NPC editor (panel_npc.cpp)
void npcPanel();
void styleUI();
// intro.cpp: the studio's splash at launch, then the End User License Agreement until it's agreed to once
void introStart(bool eulaOnly);   // the splash then the agreement (if not yet agreed); eulaOnly: just the agreement
void introForceEula();            // (screenshots) the agreement, agreed or not
bool introActive();
void introFrame(float dt);        // draws it (inside an ImGui frame)
// toolbar.cpp
void toolbar();
void toolRail();
void tutorialsWindow();   // the list of lessons
void rangePicker();       // the firing range: which map and which gun
void tutorialPanel();     // the lesson in progress, over the view
void tutAnchor(const char* id);
bool tutAnchorAfter(bool r, const char* id);   // tutAnchor, passing a button's result through   // marks the last widget drawn, so a tutorial step can point at it
int tutorialsDoneCount();
void startTutorial(int lesson);   // (also used by automated screenshots)
void openExample(const char* key, const char* msg);   // open an example design (undo brings back the old one)
extern int gPanelTab;      // designer panel tab to open next frame (0 Inspect, 1 Outline, 2 Firing, 3 File), -1 none   // the designer tools down the left of the view
bool backdropList();             // workspace background: the choices with swatches
bool backdropCombo();            // ... the same as a dropdown
bool backdropColour();           // ... the colour for "Solid colour" (nothing otherwise)
bool backdropGrid(const char* label);
// panel_test.cpp
void traceWindow();
void testPanel();
// panel_build.cpp
void buildPanel();
// cartridge_designer.cpp
void cartridgeDesigner();
// panel_map.cpp
void mapToolbar();
void mapPanel();
// menu.cpp
void mainMenu();
void pauseMenu();
void settingsWindow();
void settingsPageIndex(int i);   // (screenshots) open the settings on a page
void benchPicker();
