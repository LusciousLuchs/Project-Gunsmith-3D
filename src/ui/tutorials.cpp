// Gunsmith3D - Tutorials: short guided lessons, one mechanic at a time.
// Each lesson opens an example (or a blank sheet) and walks through it in steps. A step can watch for the thing it
// asks you to do and tick itself off; you can always move on without doing it. Finished lessons are remembered.
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/keys.hpp"
#include "app/mapedit.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "imgui.h"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ---------------------------------------------------------------- what a step can check
// A snapshot of the things steps watch, taken when the step starts, so "do X" means "do X from here".
struct TutState {
    size_t bodies = 0, joints = 0, verts = 0, curved = 0;
    int pins = 0, welds = 0, tracks = 0, springs = 0, fixedParts = 0, controls = 0, solidOff = 0;
    float yaw = 0, pitch = 0, dist = 0;
    Vector3 target{};
    bool ortho = false, xray = false, cutaway = false;
    Mode mode = Mode::Build;
    int shots = 0, malfs = 0, looseMags = 0, magRounds = 0, looseRounds = 0, selKind = 0;
    bool seatedMag = false, chambered = false;
    std::string ignition, snap, fireMode;
    double cockAt = 0;
};
static TutState snapshot() {
    TutState t;
    auto& D = A.design;
    t.bodies = D.bodies.size(); t.joints = D.joints.size();
    for (auto& b : D.bodies) {
        t.verts += b.pts.size();
        for (double r : b.rad) if (r > 0.01) t.curved++;
        for (double q : b.bulge) if (std::fabs(q) > 0.01) t.curved++;
        if (b.smooth > 0) t.curved++;
        if (b.fixed) t.fixedParts++;
        if (b.ctrl != "none") t.controls++;
        if (b.layer == "none") t.solidOff++;
    }
    for (auto& j : D.joints) {
        if (j.type == "pin") t.pins++;
        if (j.type == "weld") t.welds++;
        if (j.type == "track") t.tracks++;
        if (j.type == "spring") t.springs++;
    }
    t.yaw = A.yaw; t.pitch = A.pitch; t.dist = A.dist; t.target = A.target;
    t.ortho = A.ortho; t.xray = A.xray; t.cutaway = A.cutaway; t.mode = A.mode; t.selKind = A.sel.kind;
    t.ignition = D.breech.ignition; t.fireMode = D.breech.fireMode; t.cockAt = D.breech.cockAt;
    t.snap = A.lastSnap;
    if (A.S) {
        auto& S = *A.S;
        t.shots = S.shots; t.chambered = S.chambered;
        for (int m = 1; m < (int)sim::Malf::Count; m++) t.malfs += S.malfCount[m];
        sim::Body* mg = sim::seatedMag(S);
        t.seatedMag = mg != nullptr;
        for (auto& b : S.bodies) {
            if (b.gone) continue;
            if (b.isMag) { t.magRounds += b.rounds; if (b.loose) t.looseMags++; }
        }
        t.looseRounds = sim::looseRounds(S);
    }
    return t;
}

using Check = bool (*)(const TutState& was, const TutState& now);
struct Step {
    const char* title;
    const char* text;
    Check done = nullptr;        // ticks the step off when it returns true
    const char* tryIt = nullptr; // the short "do this" line under the text
    int tab = -1;                // designer panel tab to open: 0 Inspect, 1 Outline, 2 Firing, 3 File
    int mode = -1;               // switch to 0 the designer or 1 the bench when the step starts
};
struct Lesson {
    const char* id;
    const char* title;
    const char* blurb;
    const char* example;         // preset to open, "" for none, "map" for the map editor
    std::vector<Step> steps;
};

// ---------------------------------------------------------------- the lessons
static const std::vector<Lesson>& lessons() {
    static const std::vector<Lesson> L = {
    {"camera", "Getting around", "Move the camera, switch views, and go between the designer and the bench.", "blowback", {
        {"Welcome", "Tutorials open an example gun to work on. Your own design isn't lost: Undo (Ctrl+Z) brings it back once you're done.\n\nThe big view is the gun. The panel on the right shows whatever you've selected, and the tools are down the left."},
        {"Orbit", "Hold the right mouse button and drag to turn the camera around the gun. (Settings > Designer can swap this to the middle button.)",
         [](const TutState& w, const TutState& n) { return std::fabs(n.yaw - w.yaw) + std::fabs(n.pitch - w.pitch) > 0.3f; }, "Right-drag in the view."},
        {"Zoom", "Scroll the mouse wheel to zoom in and out.", [](const TutState& w, const TutState& n) { return std::fabs(n.dist - w.dist) > w.dist * 0.15f; }, "Scroll in the view."},
        {"Pan", "Hold the middle mouse button (the wheel) and drag to slide the view sideways. Press F any time to fit the whole gun back in view.",
         [](const TutState& w, const TutState& n) { return Vector3Distance(n.target, w.target) > 1.0f; }, "Middle-drag in the view."},
        {"Side view", "Press V for a flat side view, the way you'll usually draw parts. Press it again to go back to 3D.",
         [](const TutState& w, const TutState& n) { return n.ortho != w.ortho; }, "Press V."},
        {"Seeing inside", "Guns hide their insides. X-ray (X) draws see-through parts as ghosts, and cutaway (C) makes the parts on the near side see-through so you can watch the mechanism.",
         [](const TutState& w, const TutState& n) { return n.xray != w.xray || n.cutaway != w.cutaway; }, "Press X or C."},
        {"Designer and bench", "The designer is where you build: nothing moves. The test bench runs your gun for real, and you work it by hand. Testing never changes your design.\n\nPress Tab, or click Test bench at the top.",
         [](const TutState&, const TutState& n) { return n.mode == Mode::Test; }, "Press Tab."},
    }},
    {"drawing", "Drawing and shaping parts", "Draw parts, then shape their corners, edges and curves.", "blank", {
        {"Parts are side-view shapes", "Every part is a flat outline drawn in side view, then given a thickness. A slide, a trigger, a magazine: all outlines.\n\nThis lesson starts on a blank frame. Press V for the side view if you like.", nullptr, nullptr, -1, 0},
        {"Rectangle", "Pick Rectangle from the tools on the left (under DRAW) and drag out a box in the view.",
         [](const TutState& w, const TutState& n) { return n.bodies > w.bodies; }, "Draw a rectangle."},
        {"Polygon", "Polygon draws any outline: click to place each corner, then click the first corner again (or press Enter) to close it. Esc cancels.",
         [](const TutState& w, const TutState& n) { return n.bodies > w.bodies; }, "Draw a polygon part."},
        {"Corners", "Click Select, then click a part. Its corners show as squares: drag one to move it. Shift-click or drag a box to pick several and move them together. The Inspect panel lets you type exact coordinates, and arrow keys nudge.",
         [](const TutState& w, const TutState& n) { return n.snap != w.snap && n.bodies == w.bodies; }, "Move a corner."},
        {"Edges", "The diamonds are edges. Drag one to move the whole edge. Shift-drag pulls out a new step, like a lug or a rail. Right-click (or double-click) a diamond to split that edge in half and get a new corner.",
         [](const TutState& w, const TutState& n) { return n.verts > w.verts; }, "Extrude or split an edge."},
        {"Curves and smoothness", "In Inspect > Shape: select a corner to give it a radius, or bow the edge after it. Round corners > All rounds every corner. Smoothness rounds the part's front and back edges, for grips and stocks (it's only for looks).",
         [](const TutState& w, const TutState& n) { return n.curved > w.curved; }, "Round a corner or add smoothness.", 0},
        {"Knife and mirror", "The knife (K) cuts parts apart along a line you drag: delete the offcut to carve a notch. Mirror makes a reflected copy of the selected part across a line you drag.",
         [](const TutState& w, const TutState& n) { return n.bodies > w.bodies; }, "Cut a part with the knife."},
        {"Snap and undo", "Corners snap to the step in the top bar (0.5 mm by default; turn it off for free placement). Ctrl+Z undoes and Ctrl+Y redoes. Ctrl+D duplicates the selected part, and Delete removes it."},
    }},
    {"depth", "Thickness, depth and collisions", "How flat parts stack up into a 3D gun, and which parts touch.", "blowback", {
        {"A gun in layers", "Each part has a Thickness and a Depth: where it sits across the gun, with 0 on the bore and positive towards you. The pistol's slide is made of walls at +10 and -10 mm with a top piece between them.\n\nClick a part to see its settings.",
         [](const TutState&, const TutState& n) { return n.selKind == 1; }, "Select a part.", 0},
        {"What touches what", "Solid parts push on each other wherever they overlap, both in the side view and in depth. Parts at different depths slide past each other, the way real parts sit side by side. Untick Solid for parts that are only for looks, such as grip panels: they pass through everything."},
        {"Fixed parts and mass", "Fixed parts are the frame: they never move, and everything else is held by them. A moving part's mass comes from its shape and thickness as steel, unless you type your own."},
        {"Assemblies", "Pieces at different depths that move together are welded into one rigid assembly. The slide here is a top, two walls, rail lips and a breech block, all welded. Copy to the other side makes the matching part on the far side of the gun."},
        {"Look inside", "Turn on cutaway (C) to see the parts in the middle. The X-ray button (X) shows the see-through frame parts as outlines.",
         [](const TutState& w, const TutState& n) { return n.cutaway != w.cutaway || n.xray != w.xray; }, "Press C or X."},
    }},
    {"joints", "Joints: pins, welds and tracks", "Connect parts so they pivot, slide or move as one.", "blank", {
        {"The frame", "Joints connect two parts. Everything is held, in the end, by a Fixed part: the frame. The blank sheet already has one.\n\nDraw two small parts on the frame to join (a rectangle each is fine).",
         [](const TutState& w, const TutState& n) { return n.bodies >= w.bodies + 2; }, "Draw two parts.", -1, 0},
        {"Pin", "A pin is a pivot: two parts share a point and can turn about it, like a trigger, hammer or lever. Pick Pin (under JOIN) and click where two parts overlap. Over a single part, it pins that part to the ground.",
         [](const TutState& w, const TutState& n) { return n.pins > w.pins; }, "Add a pin."},
        {"Weld", "A weld joins two parts rigidly so they move as one: build complicated parts out of simple pieces this way. Pick Weld and click where two parts overlap.",
         [](const TutState& w, const TutState& n) { return n.welds > w.welds; }, "Add a weld."},
        {"Track", "A track makes a point on one part ride along a path in another: cam slots, guide rails, a barrel's link. Pick Track, click the point on the riding part, then click the path's points and press Enter. Its ends are hard stops. Tick Lock rotation in Inspect for a straight rail.",
         [](const TutState& w, const TutState& n) { return n.tracks > w.tracks; }, "Add a track."},
        {"Real shapes do the rest", "You don't need a joint for everything. A groove and a tongue on solid parts really ride each other, with friction, and lugs you draw really lock behind shoulders. The examples lock and cam with real shapes.\n\nTry the bench (Tab) and drag your parts to see the joints work."},
    }},
    {"springs", "Springs", "Coil and leaf springs: what the numbers mean and why springs buckle.", "blowback", {
        {"Adding a spring", "Pick Spring (under JOIN), click one end on one part, then the other end on another part. It starts a little longer than you drew it, so it pushes.",
         [](const TutState& w, const TutState& n) { return n.springs > w.springs; }, "Add a spring."},
        {"Coil springs", "Select a spring to see it in Inspect. Stiffness is how hard it pushes per millimetre squeezed; Free length is its length with nothing on it. Coil diameter, wire and turns set how it looks and its solid height: squeezed that far, it's a hard stop.", nullptr, nullptr, 0},
        {"Buckling", "A coil longer than about four times its diameter bends sideways under load and loses most of its force; it shows red on the bench. Give it a guide rod: draw a rod part inside it and pick it as the spring's guide. The recoil spring here rides on one."},
        {"Leaf springs", "A leaf spring is a flat strip clamped to one part that pushes on another and never pulls. Use it where a coil won't fit, such as triggers and sears. The trigger here has one."},
        {"Watch them work", "Go to the bench (Tab) and rack the slide by dragging it back: the recoil spring squeezes and throws it home.",
         [](const TutState&, const TutState& n) { return n.mode == Mode::Test; }, "Press Tab."},
    }},
    {"breech", "Breech, barrel and cartridge", "Tell the game where the round sits and what it fires.", "blowback", {
        {"The firing system", "Open the Firing tab. The firing system says which part is the breech (the bolt or slide that holds the round in) and which is the chamber (the barrel).", nullptr, nullptr, 2},
        {"Breech face and muzzle", "The breech face (red marker) is where the cartridge head sits against the breech. The muzzle is the end of the bore. The bore runs between them, and rounds feed and fire along it. Use Place breech face / Place muzzle to move them.", nullptr, nullptr, 2},
        {"In battery", "The gun only fires in battery: with the breech and barrel back where you drew them, closed on a chambered round. Anything else, and pulling the trigger does nothing."},
        {"Cartridges", "Pick a cartridge, or click ... to open the cartridge designer and make your own: case, rim, shoulder, bullet weight and velocity. Recoil is worked out from the bullet's momentum plus a share for the powder gas, so a hotter round kicks the slide harder.", nullptr, nullptr, 2},
    }},
    {"feeding", "Feeding and magazines", "How rounds get from the magazine into the chamber.", "blowback", {
        {"Three ways to feed", "In Firing > Feeding: a detachable magazine, a built-in supply (a simple counter), or loading by hand (a single shot).", nullptr, nullptr, 2},
        {"Magazines", "A magazine is a part with Magazine > Detachable ticked. It slides along its well (the green line), latches when pushed home, and drops free when released. Shape it around the cartridge builds the body and the well for you: capacity, single or double stack, curve."},
        {"Physical rounds", "With Physical rounds ticked, every round is a real part. The magazine holds the top round under its lips. The breech face strips it forward over the feed ramp, and it only goes in if its nose meets the chamber mouth roughly straight. Nothing is scripted: your shapes decide whether it feeds."},
        {"Feed a round", "On the bench, drag the slide back and let it go: it strips a round from the magazine and chambers it.",
         [](const TutState& w, const TutState& n) { return n.mode == Mode::Test && n.magRounds < w.magRounds; }, "Rack the slide on the bench.", -1, 1},
    }},
    {"extraction", "Extraction and ejection", "Getting the empty case out of the gun.", "blowback", {
        {"Extractor", "When the breech closes on a round, the extractor takes hold of its rim. You don't draw it: it comes with the breech face. When the breech opens, the case comes back with it."},
        {"Ejector", "The case rides back on the breech face until it hits the ejector, a fixed part you draw in its path, and pivots out. Here it's the small block on the left of the frame. Move it and the case leaves differently, or not at all."},
        {"Ejection port", "The case has to get out through a hole: the ejection port you cut in the slide wall. Too small, or in the wrong place, and the case is caught (a stovepipe) or falls back in (a failure to eject)."},
        {"Try it", "Fire a few shots on the bench (Space). Each one drops into slow motion so you can watch the case come out. Spent cases are cleared after 10 seconds.",
         [](const TutState& w, const TutState& n) { return n.shots >= w.shots + 2; }, "Fire twice.", -1, 1},
    }},
    {"firecontrol", "Fire control: triggers, hammers and strikers", "Choose a simple built-in fire control, or design every part of it.", "blowback", {
        {"Two ways", "Feeding, the bolt and extraction are always parts you design. The hammer or striker can be either built in or drawn. Open Firing > Fire control.", nullptr, nullptr, 2},
        {"Built in", "Built in means no hammer, sear or disconnector to draw. It still has to be cocked: the action must open at least Cocks after mm between shots, or the next pull does nothing (a failure to cock). Choose semi, full-auto or burst, double action and a lock time.",
         [](const TutState&, const TutState& n) { return n.ignition == "button"; }, "Set Hammer / striker to Built in.", 2},
        {"Drawn parts", "Drawn parts (advanced) fire only when the part you pick hits the primer fast enough. Your own sear holds it, your trigger releases it, and your disconnector decides whether holding the trigger fires again. This pistol's striker, sear, link and connector ramp work that way.", nullptr, nullptr, 2},
        {"The trigger", "Any part can be a control (Inspect > Control): a button, lever or trigger bound to a key. A trigger bound to FIRE must really be pulled 2 mm before the gun fires, with either kind of fire control."},
        {"Try it", "Go to the bench and fire (Space). With the built-in striker, the panel shows whether it's cocked.",
         [](const TutState& w, const TutState& n) { return n.shots > w.shots; }, "Fire a shot.", -1, 1},
    }},
    {"operating", "Locking and operating systems", "Why some guns need to lock, and the ways they do it.", "tilting", {
        {"Blowback", "A blowback gun doesn't lock. The slide's weight and spring hold the breech closed long enough for the bullet to leave. That works for weak rounds like .380, but a 9 mm needs a very heavy slide."},
        {"Locked breech", "Stronger rounds need the breech locked to the barrel for the first few millimetres. This example is a tilting barrel: its chamber block locks into the ejection port, and barrel and slide recoil together for 2 mm."},
        {"Unlocking", "Then a fixed cross pin in the frame, riding in a slot cut in the barrel's lug, pulls the breech down off the slide and stops the barrel. No joint does this: it's just the shapes. The slide runs on alone. Watch it on the bench in slow motion.",
         [](const TutState& w, const TutState& n) { return n.shots > w.shots; }, "Fire a shot on the bench.", -1, 1},
        {"Rotating bolts", "Rifles often turn the bolt: lugs on the bolt head lock behind shoulders, and turning the bolt swings them clear. Tick Motion > Turns on the bore on a bolt part. Open the bolt-action example (File tab) to try it: drag the handle up and back."},
    }},
    {"bench", "The test bench", "Work the gun by hand, load it, fire it and clear jams.", "blowback", {
        {"Hands on", "On the bench everything moves for real. Left-drag any moving part to work it: rack the slide, pull the trigger, pick up rounds. Your hand strength is in the Bench section.", nullptr, nullptr, -1, 1},
        {"Fire", "Space (or the FIRE button) pulls the trigger. Hold it for full-auto. Each shot drops into slow motion briefly, then returns to real time.",
         [](const TutState& w, const TutState& n) { return n.shots > w.shots; }, "Fire a shot."},
        {"Magazines", "M presses the magazine release and the magazine drops out. N puts a fresh one on the bench: drag it up into the well until it latches. R does both at once. T tops up every magazine. Empty magazines left alone for 30 seconds are cleared away.",
         [](const TutState& w, const TutState& n) { return n.seatedMag != w.seatedMag || n.looseMags > w.looseMags; }, "Drop the magazine (M)."},
        {"Loading rounds", "L puts a loose round on the bench. Drag it onto the top of a magazine that's out of the gun to push it in, one at a time. Or hold it in the open action in line with the bore and close the action on it.",
         [](const TutState& w, const TutState& n) { return n.looseRounds > w.looseRounds || n.magRounds > w.magRounds; }, "Press L for a round, or load one."},
        {"Jams", "When something goes wrong, the panel says what and why. Clear it like a real gun: rack the slide, or drag the stuck case out. Backspace restarts the test."},
    }},
    {"reliability", "Reliability testing", "Shoot hundreds of rounds and find out how often it jams, and why.", "blowback", {
        {"Run it", "In the bench panel, pick a number of rounds and click Run reliability test. It fires the design over and over in the background, clearing jams the way a shooter would.", nullptr, nullptr, -1, 1},
        {"Real variation", "Each shot varies like real shooting: rounds a few percent hot or weak, a cleaner or dirtier action, a firmer or looser grip (a loose grip lets the frame recoil with the slide and robs it of travel)."},
        {"Read the result", "It reports the jam rate, one jam every how many rounds, and which kinds. Hover a kind for what usually causes it. The same seed gives the same run, so change one thing and run it again to compare."},
    }},
    {"maps", "Building a range", "The map editor: brushes, textures, and walking your range.", "map", {
        {"Brushes", "Maps are built out of brushes: solid blocks you draw, cut and texture, like the editors behind early-2000s shooters. This is the built-in practice range, or your autosave."},
        {"Look around", "Hold the right mouse button to look, and fly with WASD (Q and E go down and up, Shift is faster). V switches to a top view and F focuses on the selection."},
        {"Draw and shape", "Pick a shape tool in the top bar and drag on the ground for its footprint; the height is the box in the top bar. Drag a brush to move it, drag the squares on its faces to resize. Clip cuts along a line, and H makes a block hollow, the quickest way to a room."},
        {"Textures", "Select a brush (Ctrl+click for one face) and click a material in the Textures tab. Each face has its own scale and offset."},
        {"Walk it", "Press F5 to walk the map: WASD, Shift to sprint, Space to jump, C to crouch, Z to go prone, Q and E to lean. Esc comes back to the editor."},
    }},
    };
    return L;
}

// ---------------------------------------------------------------- progress
static bool isDone(const char* id) {
    std::string s = "," + CFG.tutorialsDone + ",";
    return s.find(std::string(",") + id + ",") != std::string::npos;
}
static void markDone(const char* id) {
    if (isDone(id)) return;
    if (!CFG.tutorialsDone.empty()) CFG.tutorialsDone += ",";
    CFG.tutorialsDone += id;
    saveSettings();
}
int tutorialsDoneCount() { int n = 0; for (auto& l : lessons()) if (isDone(l.id)) n++; return n; }

static TutState gWas;
static bool gStepDone = false;
static void enterStep(int s) {
    const Lesson& L = lessons()[A.tutLesson];
    A.tutStep = std::clamp(s, 0, (int)L.steps.size() - 1);
    const Step& st = L.steps[A.tutStep];
    if (A.screen == Screen::Workshop) {
        if (st.mode == 0 && A.mode != Mode::Build) setMode(Mode::Build);
        if (st.mode == 1 && A.mode != Mode::Test) setMode(Mode::Test);
        if (st.tab >= 0) gPanelTab = st.tab;
    }
    gWas = snapshot();
    gStepDone = false;
}
static void startLesson(int i);
void startTutorial(int i) { if (i >= 0 && i < (int)lessons().size()) startLesson(i); }
static void startLesson(int i) {
    const Lesson& L = lessons()[i];
    A.showTutorials = false;
    std::string ex = L.example;
    if (ex == "map") enterMapEditor();
    else {
        if (A.screen != Screen::Workshop) enterWorkshop(false);
        if (A.mode != Mode::Build) setMode(Mode::Build);
        if (!ex.empty()) openExample(ex.c_str(), (std::string("Opened an example for the tutorial \"") + L.title + "\". Undo brings back your design.").c_str());
    }
    A.tutLesson = i;
    enterStep(0);
}

// ---------------------------------------------------------------- the lesson list
void tutorialsWindow() {
    float s = CFG.uiScale;
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, {(float)GetScreenWidth(), (float)GetScreenHeight()}, IM_COL32(8, 9, 11, 150));
    ImGui::SetNextWindowPos({GetScreenWidth() / 2.f, GetScreenHeight() / 2.f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({560 * s, std::min(640 * s, GetScreenHeight() - 60.f)});
    ImGui::Begin("##tutorials", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(gFontBold, 20.0f);
    ImGui::TextUnformatted("Tutorials");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("  %d of %d done", tutorialsDoneCount(), (int)lessons().size());
    ImGui::TextDisabled("Short guided lessons, one mechanic at a time. They're in order, but start anywhere.");
    ImGui::Dummy({0, 4 * s});
    ImGui::BeginChild("##list", {0, -40 * s});
    int i = 0;
    for (auto& L : lessons()) {
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = 52 * s;
        if (ImGui::Selectable("##l", false, 0, {w, h})) { uiTick(); startLesson(i); ImGui::PopID(); break; }
        auto* dl = ImGui::GetWindowDrawList();
        bool done = isDone(L.id);
        dl->AddText({p.x + 10 * s, p.y + 8 * s}, done ? ImGui::GetColorU32(colGood()) : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    done ? ICON_FA_CHECK : std::to_string(i + 1).c_str());
        dl->AddText(gFontBold, ImGui::GetFontSize(), {p.x + 38 * s, p.y + 7 * s}, ImGui::GetColorU32(ImGuiCol_Text), L.title);
        dl->AddText({p.x + 38 * s, p.y + 28 * s}, ImGui::GetColorU32(ImGuiCol_TextDisabled), L.blurb);
        ImGui::PopID();
        i++;
    }
    ImGui::EndChild();
    if (ImGui::Button("Close", {120 * s, 30 * s}) || IsKeyPressed(KEY_ESCAPE)) { uiTick(); A.showTutorials = false; }
    ImGui::End();
}

// ---------------------------------------------------------------- the lesson in progress
void tutorialPanel() {
    if (A.tutLesson < 0 || A.tutLesson >= (int)lessons().size()) return;
    const Lesson& L = lessons()[A.tutLesson];
    bool mapLesson = std::string(L.example) == "map";
    if (mapLesson != (A.screen == Screen::MapEditor) || A.screen == Screen::Menu) { A.tutLesson = -1; return; }  // left the lesson's screen
    if (mapLesson && ME.walking) return;
    const Step& st = L.steps[A.tutStep];
    if (st.done && !gStepDone && st.done(gWas, snapshot())) { gStepDone = true; uiTick(); }
    float s = CFG.uiScale, w = 440 * s;
    float x = mapLesson ? 10 * s : 92 * s;
    ImGui::SetNextWindowPos({x, GetScreenHeight() - 84 * s}, ImGuiCond_Always, {0, 1});   // above the hint strip and messages
    ImGui::SetNextWindowSize({w, 0});
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14 * s, 12 * s});
    ImGui::Begin("##tutstep", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextDisabled("%s  \xC2\xB7  step %d of %d", L.title, A.tutStep + 1, (int)L.steps.size());
    ImGui::PushFont(gFontBold, 17.0f);
    ImGui::TextUnformatted(st.title);
    ImGui::PopFont();
    ImGui::PushTextWrapPos(w - 14 * s);
    ImGui::TextUnformatted(st.text);
    ImGui::PopTextWrapPos();
    if (st.tryIt) {
        ImGui::Dummy({0, 2 * s});
        if (gStepDone) ImGui::TextColored(colGood(), ICON_FA_CHECK "  Done");
        else ImGui::TextColored(colAccent(), ICON_FA_HAND_POINTER "  %s", st.tryIt);
    }
    ImGui::Dummy({0, 4 * s});
    bool last = A.tutStep + 1 >= (int)L.steps.size();
    if (A.tutStep > 0) { if (ImGui::Button(ICON_FA_ARROW_LEFT "  Back")) { uiTick(); enterStep(A.tutStep - 1); } ImGui::SameLine(); }
    bool next = false;
    if (gStepDone) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.91f, 0.63f, 0.23f, 0.55f));
    if (ImGui::Button(last ? "Finish  " ICON_FA_CHECK : (st.tryIt && !gStepDone ? "Skip  " ICON_FA_ARROW_RIGHT : "Next  " ICON_FA_ARROW_RIGHT))) next = true;
    if (gStepDone) ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Lessons")) { uiTick(); A.showTutorials = true; }
    ImGui::SameLine();
    if (ImGui::Button("Close")) { uiTick(); A.tutLesson = -1; }
    if (next) {
        uiTick();
        if (last) { markDone(L.id); A.tutLesson = -1; toast(std::string("Finished \"") + L.title + "\"."); A.showTutorials = true; }
        else enterStep(A.tutStep + 1);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
