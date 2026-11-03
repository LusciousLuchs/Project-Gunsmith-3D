// Gunsmith3D - Shared app state and helpers used by every module.
#pragma once
#include "raylib.h"
#include "raymath.h"
#include "design.hpp"
#include "solver.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- constants
inline constexpr float U = 0.1f;          // world units per millimetre
inline constexpr float PANEL_W_BASE = 360.0f, TOOLBAR_H_BASE = 44.0f;
inline float PANEL_W = PANEL_W_BASE;      // right-hand panel (scaled with the UI)
inline float TOOLBAR_H = TOOLBAR_H_BASE;  // top bar (0 on the main menu)
inline constexpr const char* AUTOSAVE = "gunsmith3d_autosave.json";
inline constexpr const char* OLD_AUTOSAVE = "actionlab_autosave.json";  // from before the rename

// ---------------------------------------------------------------- app state
enum class Mode { Build, Test };
enum class Screen { Menu, Workshop, MapEditor, Range };  // the main menu, the designer / test bench, the map editor, or the firing range
enum class Tool { Select, Body, Pin, Weld, Track, Spring, Face, Muzzle, Rect, Circle, CtrlPt, StrikerTip, Knife, Mirror, RoundPath, Carve, Grip, Barrel, Measure, Faces, Engrave, Count };
struct Sel { int kind = 0; int idx = -1; int v = -1; };  // kind 1 = body, 2 = joint
struct Handle { Vector2 s; int kind; int i; int v; };  // kind: 0 vertex, 1 joint p, 2 path, 3 pa, 4 pb, 5 face, 6 muzzle, 9 grip, 10 grip's direction
struct BodyMesh { std::string key; Mesh mesh{}; bool ok = false; };
struct Shell { Vector3 p, v, dir; float spin, life, len, r; Vector3 axisSpin; };
struct Puff { Vector3 p, v; float r, life, max; };

struct App {
    Screen screen = Screen::Menu;
    bool benchOnly = false;              // came in through Test Bench on the menu: no designer
    bool showSettings = false, showPause = false, showBenchPicker = false, showTutorials = false, showRangePicker = false, showOpenWindow = false;
    int tutLesson = -1, tutStep = 0;     // the tutorial in progress (-1: none) and its step
    std::unique_ptr<Design> stash;       // the sandbox design, kept aside while another gun is on the bench
    bool saveDue = false; double lastSave = 0;
    bool quit = false;
    bool showcase = false;               // the menu backdrop: just the gun, no editor helpers
    float viewX = 0;                     // left edge of the 3D view (the menu shifts it right)
    bool modalAtFrameStart = false;      // a menu was open when this frame began (its Escape shouldn't reach the bench)
    Design design;
    std::vector<std::string> hist;
    std::vector<std::string> future;     // undone designs, for redo
    std::string lastSnap;
    Mode mode = Mode::Build;
    std::unique_ptr<sim::Sim> S;
    bool running = false;
    Tool tool = Tool::Select;
    std::string gripKind = "firing", gripStyle;  // the Grip tool places this kind (and style) of grip
    int chamberEdit = -1;
    V2 measureA, measureB; bool measured = false;  // the Measure tool's last line (mm)
    int pressNext = -1;                  // a click (no drag) on the selected part picks this one, behind it
    int axisLock = 0;                    // while dragging: 1 side to side (X), 2 up and down (Y), 3 depth (Z), 0 free
    double zDrag = 0, zDragLast = 0;     // depth dragged so far (Z lock), mm
    bool posing = false;                 // the bench's pose editor (app/pose.cpp)
    int poseHold = 0;                    // which hold is being posed (HOLD_AIM ...)
    // recorded animations (Pose > Animations): editing one, which slot, the moment on its timeline shown, and a
    // preview running (hands only, no physics)
    bool animEdit = false, animPreview = false;
    std::string animSlot = "reload";
    double animT = 0;
    int poseDrag = -1;                   // the pose handle being dragged
    int posePicked = -1;                 // the Pose dot clicked once: a second press drags it
    Vector2 posePress{};
    bool poseLive = false;
    int poseHand = 0, poseDigit = DIGIT_INDEX;  // the hand whose fingers are shown (0 firing, 1 support, -1 none) and the digit picked                // the Face and Muzzle tools place this further chamber's points (-1: the first chamber)
    Sel sel;
    bool snapOn = true, xray = true, sound = true, cutaway = false;  // cutaway: near-side parts drawn see-through
    float handN = 150;                   // hand strength on the bench (a test condition, so it lives here)
    sim::Body* hover = nullptr;
    int grabTmpl = -1;
    float grabZ = 0;                     // depth of the grabbed thing's front face (world units)
    bool hoverHandle = false;            // the cursor is on a bolt handle
    sim::Body* handleBody = nullptr;     // bolt being worked by its handle
    bool releaseBtnHeld = false, fireBtnHeld = false;
    // carving
    std::string carveShape = "circle";   // the Carve tool's cutter, size (for a plain click) and depth
    double carveW = 6, carveH = 6, carveDepth = 4;
    bool carveThrough = false;
    int selCarve = -1;                   // with a part selected: one of its carves
    bool carveDrag = false; int carveBody = -1; Vector3 carveP{}, carveN{};  // pressed on this surface point (world)
    // the Engrave tool: draw lines or write text on a part's surface (looks only)
    int engraveMode = 0;                 // 0 draw, 1 text, 2 erase
    double engraveWidth = 0.4, engraveSize = 4, engraveTurn = 0;
    std::string engraveColor = "#dcd8d0", engraveText = "CAL. 9MM";
    bool engraving = false; int engraveBody = -1; MarkDef engraveCur;   // a line being drawn
    // Faces: a face of a part under the mouse (kind 0 a side wall, `edge` its edge; 1 the near face (+Z); 2 the far face)
    struct FacePick { int body = -1, kind = 0, edge = -1; Vector3 at{}; };
    FacePick faceHover, faceDrag, facePicked;   // facePicked: clicked once, a second press drags it
    bool faceDragging = false, faceFollow = false, faceExtruded = false;   // follow: E pressed, finish with a click
    bool faceLive = false;
    bool cornerPicked = false;           // cornerDrag was clicked once: a second press drags it               // a face or corner drag has gone past the few-pixel dead zone
    std::vector<V2> faceOrig; double faceOrigZ = 0, faceOrigThick = 0; std::vector<CarveDef> faceOrigCarves;
    Vector2 faceStart{};
    // the selected part's corners in 3D (Faces tool): one on the near face and one on the far face for each, dragged on
    // their own to shape the part in depth
    struct CornerPick { int body = -1, idx = -1; bool far = false; };
    CornerPick cornerHover, cornerDrag;
    bool cornerDragging = false;
    double cornerOrigX = 0, cornerOrigY = 0, cornerOrigZ = 0, cornerZDrag = 0;
    int cornerPlane = 2;                 // the plane a corner is dragged in: 0 across the gun from the front (y-z), 1 from the top (x-z), 2 the side (x-y)
    // drafting
    bool drafting = false;
    std::vector<V2> draft;
    V2 draftF; std::string draftA; bool draftHasF = false;
    // dragging
    int dragKind = 0;  // 1 handle, 2 body, 3 hand, 4 orbit, 5 pan
    Handle dragH{};
    V2 dragStart{}, dragLast{};
    bool dragMoved = false;
    Vector2 dragPress{};                 // where the mouse went down: nothing moves until it has gone a few pixels (dragLive)
    bool dragLive = false;
    bool dragExtrude = false;            // a Shift-drag on an edge: extrude it once the drag starts
    int armedKind = -1, armedI = -2, armedV = -2;   // the handle (a joint's point, the breech face or muzzle, a round path point) picked once: a second press drags it
    Vector2 pressPos{};
    float lastClickT = -1; Vector2 lastClickPos{};
    std::vector<Handle> handles;
    // camera
    bool ortho = false;
    Vector3 target{10, -3, 0};
    float yaw = 0.42f, pitch = 0.30f, dist = 40, orthoH = 26;
    Camera3D cam{};
    int vw = 800, vh = 600;
    // effects
    float flash = 0, clickT = 0;
    std::vector<Shell> shells;
    std::vector<Puff> puffs;
    std::vector<std::pair<double, std::vector<float>>> trace;
    std::vector<BodyMesh> meshes;
    std::string toast; float toastT = 0;
    bool uiDirty = false;
    char saveName[96] = "";              // the blueprint file it saves to; empty: named after the design
    V2 shapeStart{}; bool shaping = false;
    std::vector<int> selVs;              // selected vertices of the selected part
    std::vector<V2> dragOrig;            // vertex positions when a vertex/edge drag started
    V2 dragMM{};                         // mouse position (mm) when it started
    bool boxing = false; Vector2 boxStart{};
    bool showCartDesigner = false;
    Cartridge editCart;
    std::vector<Cartridge> userCarts;
};
extern App A;

extern double gSnap;            // snap step in mm (0 = off)
extern const char* TOOL_HINT[]; // one hint per Tool, in enum order

Color hexColor(const std::string& h, Color def = {142, 154, 180, 255});
std::string colorHex(const float c[3]);
Color shade(Color c, float f);
Vector3 W3(V2 mm, float z);
V2 toMM(Vector3 w);
double snapv(double v, bool on);
std::string uid();
void save();
void autosaveTick();             // saves when the autosave setting says so
void enterWorkshop(bool benchOnly);
void enterBenchWith(const Design& d);  // put another gun on the bench, keeping the sandbox design aside
void backToMenu();
void commit();
void undo();
int trackHolderAt(V2 w);     // part a track's slot would be cut in at w (-1 = the ground)
std::string trackHint();     // step-by-step prompt while a track is drawn
void finishRoundPath();   // the magazine's round path being drawn
void redo();
void toast(const std::string& t);
float bodyDepth(const BodyDef& b);
float bodyBias(int i);
float bodyZ(int i);
float bodyFront(int i);
std::string bname(const std::string& id);
V2 curMM(const std::string& id, V2 p);
float frontZ(const std::string& id);
int magwellIndex(const std::string& magId);
void setMode(Mode m);
void restartTest();
void fireDown();
void spawnSpareMag();
void workAction();                // the bench: work the action (your animation, or the plain way from the parts)
void topUpMagazines();   // fill every magazine (T on the bench)
void spawnBenchRound(bool quiet = false);  // put a loose round on the baseplate under the action
void setTool(Tool t);
