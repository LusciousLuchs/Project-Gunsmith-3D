// Gunsmith3D - Tutorials: short guided lessons, one mechanic at a time.
// Each lesson opens an example (or a blank sheet) and walks through it one small step at a time. A step can point at
// the button or setting it's about (a glowing outline: widgets mark themselves with tutAnchor), select the part it
// talks about, and watch for what it asks you to do: when you've done it, it ticks off and moves on by itself.
// You can always go back or skip. Finished lessons are remembered.
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/keys.hpp"
#include "app/mapedit.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "imgui.h"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <map>
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
    Check done = nullptr;          // ticks the step off (and moves on) when it returns true
    const char* tryIt = nullptr;   // the short "do this" line
    const char* target = nullptr;  // widget(s) to point at, "a|b" for either
    const char* part = nullptr;    // part id to select and show
    const char* spring = nullptr;  // select the spring attached to this part instead
    int tab = -1;                  // designer panel tab to open: 0 Inspect, 1 Outline, 2 Firing, 3 File
    int mode = -1;                 // switch to 0 the designer or 1 the bench when the step starts
    Step(const char* t, const char* x) : title(t), text(x) {}
    Step& check(Check c, const char* t) { done = c; tryIt = t; return *this; }
    Step& at(const char* w) { target = w; return *this; }
    Step& show(const char* id) { part = id; return *this; }
    Step& showSpring(const char* id) { spring = id; return *this; }
    Step& onTab(int t) { tab = t; return *this; }
    Step& bench() { mode = 1; return *this; }
    Step& designer() { mode = 0; return *this; }
};
struct Lesson {
    const char* id;
    const char* title;
    const char* blurb;
    const char* example;         // preset to open, "" for none, "map" for the map editor
    std::vector<Step> steps;
};

// what steps watch for
static bool orbited(const TutState& w, const TutState& n) { return std::fabs(n.yaw - w.yaw) + std::fabs(n.pitch - w.pitch) > 0.3f; }
static bool zoomed(const TutState& w, const TutState& n) { return std::fabs(n.dist - w.dist) > w.dist * 0.15f; }
static bool panned(const TutState& w, const TutState& n) { return Vector3Distance(n.target, w.target) > 1.0f; }
static bool sideToggled(const TutState& w, const TutState& n) { return n.ortho != w.ortho; }
static bool lookInside(const TutState& w, const TutState& n) { return n.xray != w.xray || n.cutaway != w.cutaway; }
static bool onBench(const TutState&, const TutState& n) { return n.mode == Mode::Test; }
static bool newPart(const TutState& w, const TutState& n) { return n.bodies > w.bodies; }
static bool twoParts(const TutState& w, const TutState& n) { return n.bodies >= w.bodies + 2; }
static bool reshaped(const TutState& w, const TutState& n) { return n.snap != w.snap && n.bodies == w.bodies; }
static bool moreCorners(const TutState& w, const TutState& n) { return n.verts > w.verts; }
static bool rounded(const TutState& w, const TutState& n) { return n.curved > w.curved; }
static bool newPin(const TutState& w, const TutState& n) { return n.pins > w.pins; }
static bool newWeld(const TutState& w, const TutState& n) { return n.welds > w.welds; }
static bool newTrack(const TutState& w, const TutState& n) { return n.tracks > w.tracks; }
static bool newSpring(const TutState& w, const TutState& n) { return n.springs > w.springs; }
static bool fired(const TutState& w, const TutState& n) { return n.shots > w.shots; }
static bool firedTwice(const TutState& w, const TutState& n) { return n.shots >= w.shots + 2; }
static bool fedOne(const TutState& w, const TutState& n) { return n.mode == Mode::Test && n.magRounds < w.magRounds; }
static bool builtIn(const TutState&, const TutState& n) { return n.ignition == "button"; }
static bool magOut(const TutState& w, const TutState& n) { return n.seatedMag != w.seatedMag || n.looseMags > w.looseMags; }
static bool roundsHandled(const TutState& w, const TutState& n) { return n.looseRounds > w.looseRounds || n.magRounds > w.magRounds; }

// ---------------------------------------------------------------- the lessons
// One idea per step, in plain words. The outline shows where; the text says why.
static const std::vector<Lesson>& lessons() {
    static const std::vector<Lesson> L = {
    {"camera", "Getting around", "Move the camera and switch between building and testing.", "blowback", {
        Step("Welcome", "This is your workshop. The gun is in the middle, your tools are on the left, and the settings for whatever you select are on the right.\n\nLessons open an example gun. Your own design is safe: press Ctrl+Z when you're done to get it back."),
        Step("Turn the camera", "Hold the right mouse button and drag.").check(orbited, "Right-drag in the view"),
        Step("Zoom", "Scroll the mouse wheel.").check(zoomed, "Scroll in the view"),
        Step("Slide the view", "Hold the mouse wheel down and drag. If you get lost, press F (or this button) to fit the gun back in view.").check(panned, "Middle-drag in the view").at("view:fit"),
        Step("Side view", "You'll draw most parts from the side. Press V, or click this button. Press it again to go back to 3D.").check(sideToggled, "Press V").at("view:side"),
        Step("See inside", "Guns hide their insides. X-ray turns see-through parts into outlines. Cutaway hides the near side so you can watch the parts inside.").check(lookInside, "Press X or C").at("view:xray|view:cutaway"),
        Step("Build, then test", "The designer is for building: nothing moves. The test bench runs your gun for real. Testing never changes your design.").check(onBench, "Press Tab, or click Test bench").at("mode:test"),
    }},
    {"drawing", "Drawing and shaping parts", "Draw parts, then shape their corners, edges and curves.", "blank", {
        Step("Parts are outlines", "Every part is a flat outline drawn from the side, then given a thickness. You'll start on a blank frame.").designer(),
        Step("Draw a rectangle", "Pick Rectangle, then drag in the view.").check(newPart, "Draw a rectangle").at("tool:Rectangle"),
        Step("Draw any shape", "Pick Polygon and click to place corners. Click the first corner again to close the shape.").check(newPart, "Draw a polygon").at("tool:Polygon"),
        Step("Move a corner", "Pick Select and click a part. Its corners are the little squares: drag one.").check(reshaped, "Drag a corner").at("tool:Select"),
        Step("Edges", "The diamonds sit in the middle of each edge. Drag one to move the whole edge. Right-click one to split the edge and get a new corner.").check(moreCorners, "Right-click a diamond"),
        Step("Round it off", "Round corners rounds every corner. Smoothness rounds the front and back edges: good for grips.").check(rounded, "Round the corners or add smoothness").at("shape:round|shape:smooth").onTab(0),
        Step("Cut", "The Knife cuts a part in two along a line you drag. Delete the offcut to carve a notch.").check(newPart, "Cut a part with the knife").at("tool:Knife"),
        Step("Undo", "Made a mess? Ctrl+Z undoes, Ctrl+Y redoes.").at("undo"),
    }},
    {"depth", "Thickness, depth and collisions", "How flat parts stack into a 3D gun, and which parts touch.", "blowback", {
        Step("Parts sit side by side", "This is the right wall of the slide. Thickness is how thick it is. Depth is where it sits across the gun: 0 is the middle, + is towards you.").show("wall_r").at("part:thickness|part:depth"),
        Step("What touches what", "Solid parts bump into each other wherever they overlap. This grip panel isn't solid: it's just for looks, so everything passes through it.").show("grip_r").at("part:solid"),
        Step("The frame", "Fixed parts are the frame. They never move, and everything else hangs off them.").show("grip_r").at("part:fixed"),
        Step("Pieces that move together", "The slide is five pieces welded together, so they move as one. Build complicated parts from simple pieces this way.").show("slide"),
        Step("Look inside", "Press C to hide the near side and see the parts in the middle.").check(lookInside, "Press C").at("view:cutaway"),
    }},
    {"joints", "Joints: pins, welds and tracks", "Connect parts so they pivot, slide or move as one.", "blank", {
        Step("Two parts", "Joints connect two parts. Draw two small rectangles on the frame.").check(twoParts, "Draw two parts").at("tool:Rectangle").designer(),
        Step("Pin", "A pin lets parts swing, like a trigger. Pick Pin and click where two parts overlap.").check(newPin, "Add a pin").at("tool:Pin"),
        Step("Weld", "A weld glues two parts together so they move as one. Pick Weld and click where two parts overlap.").check(newWeld, "Add a weld").at("tool:Weld"),
        Step("Track", "A track makes a point slide along a path. Pick Track, click the point that should move, click the ends of its path, then press Enter.").check(newTrack, "Add a track").at("tool:Track"),
        Step("Try it", "Go to the test bench and drag your parts around.").check(onBench, "Open the test bench").at("mode:test"),
    }},
    {"springs", "Springs", "Coil and leaf springs, and why long springs buckle.", "blowback", {
        Step("A recoil spring", "This spring throws the slide back home after each shot. Its settings are on the right: stiffness is how hard it pushes, free length is how long it is with nothing pressing on it.").showSpring("nose").onTab(0),
        Step("Add a spring", "Pick Spring, click one end on a part, then the other end on another part.").check(newSpring, "Add a spring").at("tool:Spring"),
        Step("Buckling", "A long, thin spring bends sideways and goes weak (it turns red on the bench). Fix it with a guide rod: a rod part through the middle, picked as the spring's guide."),
        Step("Leaf springs", "A leaf spring is a flat strip that only pushes. It's good where a coil won't fit, like this trigger.").show("trigger"),
        Step("Watch it work", "On the bench, drag the slide back and let go: the recoil spring throws it home.").check(onBench, "Open the test bench").at("mode:test"),
    }},
    {"breech", "Barrel, breech and cartridge", "Where the round sits, and what it fires.", "blowback", {
        Step("The Firing tab", "Everything about the shot lives in the Firing tab.").at("tab:Firing").onTab(2),
        Step("Breech face and muzzle", "The two red marks in the view: the breech face is where the back of the round sits, and the muzzle is the end of the barrel. These buttons move them.").at("firing:breechface").onTab(2),
        Step("Closed to fire", "The gun only fires when everything is closed up on a round, back where you drew it."),
        Step("Your own ammo", "Pick a round here, or click ... to design your own: the case, the bullet's shape, its weight and speed.").at("firing:cartridge").onTab(2),
    }},
    {"feeding", "Feeding and magazines", "How rounds get from the magazine into the chamber.", "blowback", {
        Step("Three ways to feed", "A magazine you swap, a built-in supply, or one round at a time by hand.").at("firing:feeding").onTab(2),
        Step("The magazine", "A magazine slides along its well (the green line) and clicks in. Fit to cartridge builds it around the round for you.").show("mag"),
        Step("Lips and spring", "The magazine's spring lifts each round up to the lips. If it's too weak, the slide comes back before the round is up, and the gun fails to feed.").show("mag").at("mag:lips|mag:spring"),
        Step("Feed one", "Drag the slide back and let go. It strips a round from the magazine and pushes it into the chamber.").check(fedOne, "Rack the slide").bench(),
    }},
    {"extraction", "Extraction and ejection", "Getting the empty case out.", "blowback", {
        Step("The extractor", "When the breech closes, a hook grabs the rim of the round. It comes with the breech face, so there's nothing to draw."),
        Step("The ejector", "As the slide runs back, the empty case hits this fixed block and flips out. Move it, and the case goes somewhere else.").show("ejector"),
        Step("The port", "The case has to escape through a hole cut in the slide. Too small, or in the wrong place, and the case gets stuck.").show("wall_r"),
        Step("Watch it", "Fire twice. Each shot slows down so you can watch the case fly out.").check(firedTwice, "Fire twice (Space)").at("fire").bench(),
    }},
    {"firecontrol", "Triggers, hammers and strikers", "Use the simple built-in fire control, or design every part.", "blowback", {
        Step("Two ways to fire", "Built in: no hammer to draw, but the action still has to cock it. Drawn parts: you build the striker or hammer, the sear and the trigger yourself.").at("firing:control").onTab(2),
        Step("Built in", "Switch to Built in. You can then pick semi-auto, full-auto or burst.").check(builtIn, "Choose Built in").at("firing:control").onTab(2),
        Step("Drawn parts", "In this pistol the sear holds the striker back until the trigger pushes it free. Every piece is a real part.").show("sear"),
        Step("Fire it", "Pull the trigger.").check(fired, "Fire (Space)").at("fire").bench(),
    }},
    {"operating", "Locking the breech", "Why strong rounds need the breech locked, and one way to do it.", "tilting", {
        Step("Blowback", "Small rounds can use blowback: a heavy slide and spring hold the breech shut. Strong rounds need it locked for a moment."),
        Step("A locking barrel", "This barrel locks into the slide, and they recoil together for the first 2 mm.").show("barrel"),
        Step("Unlocking", "Then this fixed pin, riding in a slot in the barrel, pulls the barrel down and unlocks it. No special joint: just shapes.").show("xpin"),
        Step("Watch it", "Fire, and watch the barrel drop in slow motion.").check(fired, "Fire (Space)").at("fire").bench(),
        Step("Other ways", "Rifles often turn the bolt to lock it. Try the bolt-action example from the File tab: drag its handle up and back.").at("tab:File"),
    }},
    {"bench", "The test bench", "Work the gun by hand, load it, fire it, clear jams.", "blowback", {
        Step("Hands on", "On the bench everything moves for real. Drag any moving part: rack the slide, pull the trigger, pick up rounds.").bench(),
        Step("Fire", "Space or the FIRE button pulls the trigger. Hold it for full-auto guns.").check(fired, "Fire").at("fire"),
        Step("Magazines", "M drops the magazine. N puts a fresh one on the bench: drag it up into the gun. T tops up every magazine.").check(magOut, "Press M").at("bench:section|bench:newmag"),
        Step("Loading rounds", "L puts a loose round on the bench. Drag it onto the top of a magazine to load it.").check(roundsHandled, "Press L"),
        Step("Jams", "If it jams, the panel says why. Clear it like a real gun: rack the slide, or drag the stuck case out. Backspace restarts."),
    }},
    {"reliability", "Reliability testing", "Fire hundreds of rounds and see how often it jams.", "blowback", {
        Step("Run it", "This fires your gun over and over in the background, clearing jams like a shooter would.").at("bench:reliability").bench(),
        Step("Like real shooting", "Every shot is a little different: hotter or weaker rounds, a dirtier action, a firmer or looser grip."),
        Step("Read the result", "You get the jam rate and what kind of jams. Hover a jam type for what usually causes it. Change one thing and run it again to compare."),
    }},
    {"maps", "Building a range", "Draw a shooting range and walk around it.", "map", {
        Step("Blocks", "Maps are built from solid blocks you draw, cut and paint, like the editors behind early-2000s shooters."),
        Step("Look around", "Hold the right mouse button to look, and fly with WASD. V gives a top view."),
        Step("Draw and shape", "Pick a shape in the top bar and drag on the ground. Drag a block to move it, and drag the squares on its faces to resize it. H hollows a block into a room."),
        Step("Paint", "Select a block (Ctrl+click for a single face), then click a material.").at("map:Textures"),
        Step("Walk it", "Walk your range: WASD to move, Space to jump, C to crouch. Esc comes back.").at("map:walk"),
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

// ---------------------------------------------------------------- anchors: where the widgets steps point at are
struct Anchor { ImVec2 a, b; int frame = -10; };
static std::map<std::string, Anchor> gAnchors;
void tutAnchor(const char* id) {
    if (A.tutLesson < 0) return;
    gAnchors[id] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetFrameCount()};
}

static TutState gWas;
static bool gStepDone = false;
static double gDoneAt = 0;
static void enterStep(int s) {
    const Lesson& L = lessons()[A.tutLesson];
    A.tutStep = std::clamp(s, 0, (int)L.steps.size() - 1);
    const Step& st = L.steps[A.tutStep];
    if (A.screen == Screen::Workshop) {
        if (st.mode == 0 && A.mode != Mode::Build) setMode(Mode::Build);
        if (st.mode == 1 && A.mode != Mode::Test) setMode(Mode::Test);
        if ((st.part || st.spring) && A.mode == Mode::Test) setMode(Mode::Build);   // parts are shown in the designer
        if (st.part) {
            int i = A.design.bodyIndex(st.part);
            if (i >= 0) { A.sel = {1, i, -1}; A.selVs.clear(); gPanelTab = 0; }
        }
        if (st.spring)
            for (size_t j = 0; j < A.design.joints.size(); j++) {
                auto& jd = A.design.joints[j];
                if (jd.type == "spring" && (jd.a == st.spring || jd.b == st.spring)) { A.sel = {2, (int)j, -1}; A.selVs.clear(); gPanelTab = 0; break; }
            }
        if (st.tab >= 0) gPanelTab = st.tab;
    }
    gWas = snapshot();
    gStepDone = false;
}
static void startLesson(int i);
void startTutorial(int i) {
    if (i < 0 || i >= (int)lessons().size()) return;
    startLesson(i);
    if (const char* e = std::getenv("GUNSMITH_TUTSTEP")) enterStep(std::atoi(e));   // (automated screenshots)
}
static void startLesson(int i) {
    const Lesson& L = lessons()[i];
    A.showTutorials = false;
    std::string ex = L.example;
    if (ex == "map") enterMapEditor();
    else {
        if (A.screen != Screen::Workshop) enterWorkshop(false);
        if (A.mode != Mode::Build) setMode(Mode::Build);
        if (!ex.empty()) openExample(ex.c_str(), (std::string("Opened an example for the lesson \"") + L.title + "\". Ctrl+Z brings back your design.").c_str());
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
    ImGui::TextDisabled("Short lessons, a few minutes each. Start with the first, or pick what you need.");
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
        char meta[160];
        std::snprintf(meta, sizeof meta, "%s  \xC2\xB7  %d steps", L.blurb, (int)L.steps.size());
        dl->AddText({p.x + 38 * s, p.y + 28 * s}, ImGui::GetColorU32(ImGuiCol_TextDisabled), meta);
        ImGui::PopID();
        i++;
    }
    ImGui::EndChild();
    if (ImGui::Button("Close", {120 * s, 30 * s}) || IsKeyPressed(KEY_ESCAPE)) { uiTick(); A.showTutorials = false; }
    ImGui::End();
}

// ---------------------------------------------------------------- pointing at things
static void highlight(const char* targets) {
    if (!targets) return;
    std::string t = targets;
    int now = ImGui::GetFrameCount();
    float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 5.0f), s = CFG.uiScale;
    auto* fg = ImGui::GetForegroundDrawList();
    size_t at = 0;
    while (at <= t.size()) {
        size_t bar = t.find('|', at);
        std::string id = t.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
        auto it = gAnchors.find(id);
        if (it != gAnchors.end() && it->second.frame >= now - 1) {
            ImVec2 a = it->second.a, b = it->second.b;
            float g = (3 + 3 * pulse) * s;
            fg->AddRect({a.x - g, a.y - g}, {b.x + g, b.y + g}, IM_COL32(233, 161, 59, (int)(120 + 135 * pulse)), 6 * s, 0, 2.5f * s);
            fg->AddRect({a.x - g - 3 * s, a.y - g - 3 * s}, {b.x + g + 3 * s, b.y + g + 3 * s}, IM_COL32(233, 161, 59, (int)(40 * pulse)), 8 * s, 0, 4 * s);
        }
        if (bar == std::string::npos) break;
        at = bar + 1;
    }
}

// ---------------------------------------------------------------- the lesson in progress
void tutorialPanel() {
    if (A.tutLesson < 0 || A.tutLesson >= (int)lessons().size()) return;
    const Lesson& L = lessons()[A.tutLesson];
    bool mapLesson = std::string(L.example) == "map";
    if (mapLesson != (A.screen == Screen::MapEditor) || A.screen == Screen::Menu) { A.tutLesson = -1; return; }  // left the lesson's screen
    if (mapLesson && ME.walking) return;
    const Step& st = L.steps[A.tutStep];
    bool last = A.tutStep + 1 >= (int)L.steps.size();
    if (st.done && !gStepDone && st.done(gWas, snapshot())) { gStepDone = true; gDoneAt = GetTime(); uiTick(); }
    if (gStepDone && !last && GetTime() - gDoneAt > 1.2) { enterStep(A.tutStep + 1); return; }   // done: on to the next by itself
    if (!gStepDone) highlight(st.target);

    float s = CFG.uiScale, w = 400 * s;
    float x = mapLesson ? 10 * s : 92 * s;
    ImGui::SetNextWindowPos({x, GetScreenHeight() - 84 * s}, ImGuiCond_Always, {0, 1});   // above the hint strip and messages
    ImGui::SetNextWindowSize({w, 0});
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16 * s, 14 * s});
    ImGui::Begin("##tutstep", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    // the lesson, and a dot per step
    ImGui::TextDisabled("%s", L.title);
    {
        float r = 3.5f * s, gap = 11 * s, dx = ImGui::GetWindowPos().x + w - 16 * s - gap * (L.steps.size() - 1) - r;
        float dy = ImGui::GetCursorScreenPos().y - ImGui::GetTextLineHeight() / 2 - 2 * s;
        auto* dl = ImGui::GetWindowDrawList();
        for (size_t k = 0; k < L.steps.size(); k++) {
            ImU32 c = (int)k < A.tutStep ? ImGui::GetColorU32(colAccent()) : (int)k == A.tutStep ? IM_COL32(255, 255, 255, 230) : IM_COL32(255, 255, 255, 50);
            dl->AddCircleFilled({dx + gap * k, dy}, r, c);
        }
    }
    ImGui::Dummy({0, 2 * s});
    ImGui::PushFont(gFontBold, 19.0f);
    ImGui::TextUnformatted(st.title);
    ImGui::PopFont();
    ImGui::Dummy({0, 2 * s});
    ImGui::PushTextWrapPos(w - 16 * s);
    ImGui::TextUnformatted(st.text);
    ImGui::PopTextWrapPos();
    if (st.tryIt) {
        ImGui::Dummy({0, 4 * s});
        if (gStepDone) ImGui::TextColored(colGood(), ICON_FA_CHECK "  Nice!");
        else ImGui::TextColored(colAccent(), ICON_FA_HAND_POINTER "  %s", st.tryIt);
    }
    ImGui::Dummy({0, 6 * s});
    if (A.tutStep > 0) { if (ImGui::Button(ICON_FA_ARROW_LEFT)) { uiTick(); enterStep(A.tutStep - 1); } ImGui::SameLine(); }
    bool next = false;
    bool primary = !st.tryIt || gStepDone;   // a step that only explains: Next is the thing to press
    if (primary) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.91f, 0.63f, 0.23f, 0.85f)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.07f, 0.02f, 1)); }
    if (ImGui::Button(last ? "Finish  " ICON_FA_CHECK : (primary ? "Next  " ICON_FA_ARROW_RIGHT : "Skip  " ICON_FA_ARROW_RIGHT))) next = true;
    if (primary) ImGui::PopStyleColor(2);
    ImGui::SameLine(w - 16 * s - ImGui::CalcTextSize("Lessons").x - ImGui::CalcTextSize(ICON_FA_XMARK).x - 4 * ImGui::GetStyle().FramePadding.x - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Lessons")) { uiTick(); A.showTutorials = true; }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK)) { uiTick(); A.tutLesson = -1; }
    if (tipHover()) ImGui::SetTooltip("Close the lesson");
    if (next) {
        uiTick();
        if (last) { markDone(L.id); A.tutLesson = -1; toast(std::string("Finished \"") + L.title + "\"."); A.showTutorials = true; }
        else enterStep(A.tutStep + 1);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
