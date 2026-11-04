// Gunsmith3D - Tutorials: short guided lessons, one mechanic at a time.
// Each lesson opens an example (or a blank sheet) and walks through it one small step at a time. A step can point at
// the button or setting it's about (a glowing outline: widgets mark themselves with tutAnchor), select the part it
// talks about, and watch for what it asks you to do: when you've done it, it ticks off and moves on by itself.
// You can always go back or skip. Finished lessons are remembered.
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/keys.hpp"
#include "app/mapedit.hpp"
#include "app/view.hpp"
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
    // for building a gun: the parts and settings it needs, recognised by what they are rather than by name
    int movingParts = 0, heavyMoving = 0;
    int tubeRounds = 0;                  // rounds in built-in magazines (a tube)
    int carves = 0, carvesAcross = 0, carvesThrough = 0;  // cuts; cuts not made from the near or far side; right-through cuts
    bool carveTool = false;
    bool breechOn = false, hasFace = false, hasMuzzle = false, chamberOk = false, handFeed = false;
    // the building tools
    int tool = 0, barrels = 0, mirrors = 0, marks = 0, shapedDepth = 0, textured = 0;
    bool measured = false;
    double measureLen = 0;
    // posing and animations
    bool posing = false, animEdit = false, animPlaying = false;
    std::string animSlot;
    int animKeys = 0, gripKeys = 0, animEvents = 0;
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
        if (!b.weld.empty()) t.welds++;
        for (auto& c : b.carves) {
            t.carves++;
            double l = std::sqrt(c.dx * c.dx + c.dy * c.dy + c.dz * c.dz);
            if (l > 0 && std::fabs(c.dz) / l < 0.7) t.carvesAcross++;
            if (c.depth <= 0) t.carvesThrough++;
        }
    }
    for (auto& b : D.bodies) if (!b.fixed && !b.magazine) { t.movingParts++; if (b.mass >= 250) t.heavyMoving++; }
    {
        auto& br = D.breech;
        t.breechOn = br.on; t.hasFace = br.on && br.hasFace; t.hasMuzzle = br.on && br.hasMuzzle;
        int ci = D.bodyIndex(br.chamber);
        t.chamberOk = br.on && ci >= 0 && D.bodies[ci].fixed && br.chamber != "frame";
        t.handFeed = br.on && br.feedMode == "single" && br.physicalFeed;
    }
    for (auto& j : D.joints) {
        if (j.type == "pin") t.pins++;
        if (j.type == "track") t.tracks++;
        if (j.type == "spring") t.springs++;
    }
    for (auto& b : D.bodies) {
        if (b.barrel.on) t.barrels++;
        if (!b.mirror.empty()) t.mirrors++;
        t.marks += (int)b.marks.size();
        if (depthShaped(b)) t.shapedDepth++;
        if (!b.texture.empty()) t.textured++;
    }
    t.tool = (int)A.tool; t.measured = A.measured; t.measureLen = std::hypot(A.measureB.x - A.measureA.x, A.measureB.y - A.measureA.y);
    t.posing = A.posing; t.animEdit = A.animEdit; t.animSlot = A.animSlot;
    for (auto& [slot, an] : D.anims) {
        t.animKeys += (int)an.keys.size();
        for (auto& k : an.keys) { if (k.gripF || k.gripS) t.gripKeys++; if (!k.event.empty()) t.animEvents++; }
    }
    t.yaw = A.yaw; t.pitch = A.pitch; t.dist = A.dist; t.target = A.target;
    t.carveTool = A.tool == Tool::Carve;
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
            if (b.builtInMag) t.tubeRounds += b.rounds;
        }
        t.looseRounds = sim::looseRounds(S);
        t.animPlaying = sim::animPlaying(S);
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
    void (*helper)() = nullptr;    // "Do it for me": makes the step's part or setting, for anyone stuck
    bool sideView = false;         // switch to the side view when the step starts
    Step(const char* t, const char* x) : title(t), text(x) {}
    Step& doIt(void (*h)()) { helper = h; return *this; }
    Step& side() { sideView = true; return *this; }
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
static bool carvePicked(const TutState&, const TutState& n) { return n.carveTool; }
static bool newCarve(const TutState& w, const TutState& n) { return n.carves > w.carves; }
static bool carvedAcross(const TutState& w, const TutState& n) { return n.carvesAcross > w.carvesAcross; }
static bool carvedThrough(const TutState& w, const TutState& n) { return n.carvesThrough > w.carvesThrough; }
static bool tubeFed(const TutState& w, const TutState& n) { return n.mode == Mode::Test && n.tubeRounds < w.tubeRounds; }
static bool roundsHandled(const TutState& w, const TutState& n) { return n.looseRounds > w.looseRounds || n.magRounds > w.magRounds; }

// the building tools
static bool selectedPart(const TutState&, const TutState& n) { return n.selKind == 1; }
static bool newBarrel(const TutState& w, const TutState& n) { return n.barrels > w.barrels; }
static bool newMirror(const TutState& w, const TutState& n) { return n.mirrors > w.mirrors; }
static bool newMark(const TutState& w, const TutState& n) { return n.marks > w.marks; }
static bool shapedInDepth(const TutState& w, const TutState& n) { return n.shapedDepth > w.shapedDepth; }
static bool finished(const TutState& w, const TutState& n) { return n.textured > w.textured; }
static bool measuredIt(const TutState& w, const TutState& n) { return n.measured && n.measureLen > 1 && std::fabs(n.measureLen - w.measureLen) > 0.01; }
static bool facesPushed(const TutState& w, const TutState& n) { return n.tool == (int)Tool::Faces && n.snap != w.snap; }
// posing and animations
static bool posingNow(const TutState&, const TutState& n) { return n.posing; }
static bool poseChanged(const TutState& w, const TutState& n) { return n.posing && n.snap != w.snap; }
static bool editingAnim(const TutState&, const TutState& n) { return n.animEdit; }
static bool reloadPicked(const TutState&, const TutState& n) { return n.animEdit && n.animSlot == "reload"; }
static bool newKey(const TutState& w, const TutState& n) { return n.animKeys > w.animKeys; }
static bool newGrip(const TutState& w, const TutState& n) { return n.gripKeys > w.gripKeys; }
static bool newEvent(const TutState& w, const TutState& n) { return n.animEvents > w.animEvents; }
static bool animPlayed(const TutState&, const TutState& n) { return n.animPlaying; }

// building a gun
static bool newFixed(const TutState& w, const TutState& n) { return n.fixedParts > w.fixedParts; }
static bool newMoving(const TutState& w, const TutState& n) { return n.movingParts > w.movingParts; }
static bool heavyBolt(const TutState&, const TutState& n) { return n.heavyMoving > 0; }
static bool faceSet(const TutState&, const TutState& n) { return n.hasFace; }
static bool muzzleSet(const TutState&, const TutState& n) { return n.hasMuzzle; }
static bool chamberSet(const TutState&, const TutState& n) { return n.chamberOk; }
static bool handFed(const TutState&, const TutState& n) { return n.handFeed; }
static bool reloaded(const TutState&, const TutState& n) { return n.mode == Mode::Test && n.chambered; }

// "Do it for me": each makes its piece where it fits whatever is already there (the player's own parts, if drawn)
struct Box { double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; double cx() const { return (x0 + x1) / 2; } double cy() const { return (y0 + y1) / 2; } };
static Box boxOf(const BodyDef& b) { Box r; for (auto& p : b.pts) { r.x0 = std::min(r.x0, p.x); r.y0 = std::min(r.y0, p.y); r.x1 = std::max(r.x1, p.x); r.y1 = std::max(r.y1, p.y); } return r; }
static std::vector<V2> rectPts(double x0, double y0, double x1, double y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }
static int framePart() { auto& D = A.design; int i = D.bodyIndex("frame"); if (i >= 0) return i; for (size_t k = 0; k < D.bodies.size(); k++) if (D.bodies[k].fixed) return (int)k; return -1; }
static int barrelPart() {   // the longest fixed part that isn't the frame
    auto& D = A.design; int best = -1, f = framePart(); double bw = 0;
    for (size_t k = 0; k < D.bodies.size(); k++) { if (!D.bodies[k].fixed || (int)k == f) continue; Box b = boxOf(D.bodies[k]); if (b.x1 - b.x0 > bw && b.x1 - b.x0 > 2 * (b.y1 - b.y0)) { bw = b.x1 - b.x0; best = (int)k; } }
    return best;
}
static int boltPart() { auto& D = A.design; for (int k = (int)D.bodies.size() - 1; k >= 0; k--) if (!D.bodies[k].fixed && !D.bodies[k].magazine) return k; return -1; }
static Box barrelBox() { int i = barrelPart(); if (i >= 0) return boxOf(A.design.bodies[i]); Box b; b.x0 = 120; b.y0 = 22; b.x1 = 300; b.y1 = 38; return b; }
static BodyDef& addPart(const char* name, std::vector<V2> pts, bool fixed, double thick, const char* color) {
    BodyDef b; b.id = uid(); b.name = name; b.fixed = fixed; b.layer = "ABC"; b.thick = thick; b.color = color; b.pts = pts;
    fixShape(b);
    A.design.bodies.push_back(b);
    return A.design.bodies.back();
}
static void helpBarrel() { if (barrelPart() < 0) addPart("Barrel", rectPts(120, 22, 300, 38), true, 16, "#c9ccd2"); commit(); }
static void helpBolt() {
    int i = boltPart();
    if (i < 0) { Box bb = barrelBox(); double bore = bb.cy(); addPart("Bolt", rectPts(bb.x0 - 60, bore - 10, bb.x0, bore + 3), false, 14, "#3f7fe0").mass = 400; }
    else A.design.bodies[i].mass = 400;
    commit();
}
static void helpTrack() {
    int i = boltPart(), f = framePart();
    if (i < 0) helpBolt(), i = boltPart();
    Box bo = boxOf(A.design.bodies[i]), bb = barrelBox();
    double y = std::clamp(bb.cy() - 3, bo.y0 + 1, bo.y1 - 1), cx = bo.cx();
    JointDef j; j.type = "track"; j.a = A.design.bodies[i].id; j.b = f >= 0 ? A.design.bodies[f].id : "world";
    j.p = {cx, y}; j.path = {{cx - 50, y}, {cx, y}}; j.lockRot = true;
    A.design.joints.push_back(j);
    commit();
}
static void helpFace() {
    auto& br = A.design.breech; int i = boltPart();
    if (!br.on) { br = BreechDef(); br.on = true; }
    Box bb = barrelBox();
    if (i >= 0) { br.breech = A.design.bodies[i].id; br.face = {boxOf(A.design.bodies[i]).x1, bb.cy()}; }
    else br.face = {bb.x0, bb.cy()};
    br.hasFace = true;
    if (A.tool == Tool::Face) A.tool = Tool::Select;
    commit();
}
static void helpMuzzle() { auto& br = A.design.breech; if (!br.on) helpFace(); Box bb = barrelBox(); br.muzzle = {bb.x1, bb.cy()}; br.hasMuzzle = true; if (A.tool == Tool::Muzzle) A.tool = Tool::Select; commit(); }
static void helpChamber() { auto& br = A.design.breech; if (!br.on) helpFace(); int b = barrelPart(); if (b >= 0) br.chamber = A.design.bodies[b].id; commit(); }
static void helpFeed() { auto& br = A.design.breech; if (!br.on) helpFace(); br.feedMode = "single"; br.physicalFeed = true; br.loaded = true; commit(); }
static void helpEjector() {
    int i = boltPart();
    if (i < 0) return;
    Box bo = boxOf(A.design.bodies[i]);
    double travel = 50;
    for (auto& j : A.design.joints) if (j.type == "track" && j.a == A.design.bodies[i].id && j.path.size() >= 2) travel = std::fabs(j.path.back().x - j.path.front().x);
    double front = bo.x1 - travel + 12;   // the bolt's face when it's all the way open, plus a little: the case meets it just before
    addPart("Ejector", rectPts(front - 4, bo.y1 + 0.6, front, bo.y1 + 5), true, 4, "#3f4654");
    commit();
}

// carving: a block to cut into, and cuts made the way the tool would
static void helpBlock() {
    if (boltPart() >= 0) return;
    int f = framePart();
    Box fb = f >= 0 ? boxOf(A.design.bodies[f]) : Box{};
    if (f < 0) { fb.x0 = 0; fb.y0 = 60; fb.x1 = 200; fb.y1 = 80; }
    addPart("Block", rectPts(fb.x0 + 40, fb.y0 - 40, fb.x0 + 130, fb.y0 - 8), false, 20, "#3f7fe0");
    A.sel = {1, (int)A.design.bodies.size() - 1, -1};
    commit();
}
static int carvePart() { if (boltPart() < 0) helpBlock(); return boltPart(); }
static void selectCut(int i) { A.sel = {1, i, -1}; A.selCarve = (int)A.design.bodies[i].carves.size() - 1; commit(); }
static void helpCarve() {
    int i = carvePart();
    if (i < 0) return;
    auto& b = A.design.bodies[i];
    Box bx = boxOf(b);
    CarveDef c; c.shape = A.carveShape; c.w = A.carveW; c.h = c.shape == "circle" ? A.carveW : A.carveH;
    c.at = {bx.x0 + (bx.x1 - bx.x0) * std::min(0.85, 0.2 + 0.15 * b.carves.size()), bx.cy()}; c.atZ = b.thick / 2; c.dz = -1;
    c.depth = A.carveThrough ? 0 : A.carveDepth;
    b.carves.push_back(c);
    selectCut(i);
}
static void helpCarveTop() {
    int i = carvePart();
    if (i < 0) return;
    auto& b = A.design.bodies[i];
    Box bx = boxOf(b);
    CarveDef c; c.shape = "rect"; c.w = 8; c.h = std::min(8.0, b.thick * 0.5);
    c.at = {bx.x0 + (bx.x1 - bx.x0) * 0.75, bx.y0}; c.atZ = 0; c.dx = 0; c.dy = 1; c.dz = 0; c.depth = 6;
    b.carves.push_back(c);
    selectCut(i);
}
static void helpPickCarve() { setTool(Tool::Carve); }
static void helpChangeCut() {
    int i = carvePart();
    if (i < 0) return;
    if (A.design.bodies[i].carves.empty()) helpCarve();
    auto& b = A.design.bodies[i];
    b.carves.front().depth += 2;   // the first cut, 2 mm deeper
    selectCut(i);
    A.selCarve = 0;
}
static void helpThrough() {
    int i = carvePart();
    if (i < 0) return;
    if (A.design.bodies[i].carves.empty()) helpCarve();
    A.design.bodies[i].carves.front().depth = 0;
    selectCut(i);
    A.selCarve = 0;
}

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
    {"tools", "The building tools", "Every tool in the designer, one by one: drawing, shaping, carving, engraving and joining parts.", "blank", {
        Step("Your workshop", "The tools are down the left, in three groups. Draw makes new parts, Shape changes them, and Join connects them. Whatever you select shows its settings on the right.\n\nEvery part is a flat outline drawn from the side and given a thickness, so most drawing happens in the side view.").designer().side(),
        Step("Select", "Pick Select. Click a part to pick it: it lights up and its settings appear. Click it again and drag to move it. The first click never moves anything, so you can always look before you touch.").check(selectedPart, "Click a part").at("tool:Select"),
        Step("Rectangle", "Pick Rectangle and drag in the view. Most parts start as a rectangle.").check(newPart, "Draw a rectangle").at("tool:Rectangle"),
        Step("Polygon", "Pick Polygon and click to place corners. Click the first corner again (or press Enter) to close the shape.").check(newPart, "Draw a polygon").at("tool:Polygon"),
        Step("Circle", "Pick Circle, click the centre and drag out the radius. Good for pins, knobs and buttons.").check(newPart, "Draw a circle").at("tool:Circle"),
        Step("Barrel", "Pick Barrel and drag from the breech end to the muzzle. You get a real round barrel with a bore, and a chamber cut for the cartridge you choose in its panel. It also sets up where the round sits and where the muzzle is.").check(newBarrel, "Draw a barrel").at("tool:Barrel"),
        Step("Corners", "Select a part. Its corners are the little squares: click one to pick it (its X and Y show on the right), then drag it. Shift-click picks several, and arrow keys nudge them.").check(reshaped, "Pick a corner, then drag it").at("tool:Select"),
        Step("Edges", "The diamonds sit in the middle of each edge. Pick one, then drag to move the whole edge, or Shift-drag to pull out a step. Right-click (or double-click) a diamond to split the edge and get a new corner. A part can have up to 1024 corners.").check(moreCorners, "Right-click a diamond").at("shape:split"),
        Step("Rounding", "Round corners rounds every corner; pick one corner to round just that one. Chamfer cuts corners off flat. Smoothness rounds the edges between the sides and the faces, good for grips.").check(rounded, "Round the corners or add smoothness").at("shape:round|shape:smooth").onTab(0),
        Step("The part's settings", "Thickness is how thick the part is. Z (offset) is where it sits across the gun: 0 is the middle, + is towards you. Fixed parts are the frame and never move. Solid parts bump into each other where they overlap; untick it for parts that are just for looks, like grip panels. Mass is how heavy it is. Every number can be typed: click it without dragging.").at("part:thickness|part:depth|part:fixed|part:solid"),
        Step("Round parts", "Tick Round to turn a part on a lathe: a rectangle becomes a round bar, and steps in its outline become shoulders. Sides turns it down to flats instead (8 is an octagon).").at("part:round").onTab(0),
        Step("Finishes", "Under Surface, give a part a finish: stained wood, black polymer, blued steel, or a picture of your own.").check(finished, "Give a part a finish").at("part:surface").onTab(0),
        Step("Faces", "Turn the view so you see a part at an angle and select it: each face you can see (top, bottom, ends, front, back) gets an arrow dot. Click one, then drag it to push or pull that face. The Faces tool does the same by pointing at any face, and E extrudes a face, drawing it out into a new step or piece.").check(facesPushed, "Push or pull a face").at("tool:Faces"),
        Step("Shaped in depth", "With Faces, the selected part's corners show on both faces: squares at the front, circles at the back. Pick one, then drag it on its own to give the front and back different shapes, like a taper. From the top or the end it moves in the plane you're looking at.").check(shapedInDepth, "Drag a corner on one face only").at("tool:Faces"),
        Step("Knife", "The Knife cuts a part into two separate parts along a line you drag. Delete the offcut to cut a notch.").check(newPart, "Cut a part with the knife").at("tool:Knife"),
        Step("Mirror", "Select a part, pick Mirror and drag a line: a mirrored copy appears across it. It stays a live mirror, so change either one and the other follows. Copy to the other side makes the matching part on the far side of the gun.").check(newMirror, "Mirror a part").at("tool:Mirror|part:otherside"),
        Step("Measure", "Pick Measure and drag between two points. It shows the length, how far across and up, and the angle, snapping to corners and joints.").check(measuredIt, "Measure something").at("tool:Measure"),
        Step("Carve", "Pick Carve to cut a hole, slot or pocket into a part: choose the cutter's shape, size and depth on the right, then click the part's surface. It goes straight in from the face you click, so turn the view to cut from the top or the far side. Cuts are real for the physics too.").check(newCarve, "Carve into a part").at("tool:Carve"),
        Step("Engrave", "Pick Engrave, click a part, then draw on it like an engraving pen. Switch to Text to write roll marks or a serial number, and Erase to remove a line or a word. Engravings are for looks only.").check(newMark, "Engrave a part").at("tool:Engrave"),
        Step("Pin", "Join tools connect parts. A pin lets parts swing, like a trigger or a hammer. Pick Pin and click where two parts overlap. Over a single part, it pins it to the ground.").check(newPin, "Add a pin").at("tool:Pin"),
        Step("Weld", "Welding makes two parts one solid piece: they move as one, and nothing is added to the gun. Pick Weld, click one part, then the other. Build complicated parts from simple pieces this way.").check(newWeld, "Weld two parts").at("tool:Weld"),
        Step("Track", "A track lets a part slide along a slot: bolts, slides and cams. Pick Track, click the part that should slide, then click where it slides to. Hold Shift on that click to add bends.").check(newTrack, "Add a track").at("tool:Track"),
        Step("Spring", "Pick Spring, click one end on a part, then the other end on another part. A coil spring pushes and pulls; a leaf spring is a flat strip that only pushes, good where a coil won't fit. A long, thin coil buckles and goes weak (it turns red on the bench): run a guide rod through it.").check(newSpring, "Add a spring").at("tool:Spring"),
        Step("Copy, paste and keep", "Ctrl+C and Ctrl+V copy a part with its welded pieces and joints. Save to my parts keeps it in your parts library, to drop into any gun from Parts in the top bar. Combine with another part merges, cuts or overlaps two outlines.").at("file:parts"),
        Step("Undo, then try it", "Ctrl+Z undoes and Ctrl+Y redoes. When you've built something, open the test bench and drag your parts around: there everything moves for real.").check(onBench, "Open the test bench").at("mode:test|undo"),
    }},
    {"firstgun", "Build your first gun", "From a blank frame to a gun that fires, ejects and reloads.", "blank", {
        Step("What you'll build", "A simple single-shot: a barrel, a heavy bolt that slides, and an ejector. Firing blows the bolt open and throws out the empty case. You put in the next round by hand and push the bolt shut.\n\nEvery step has a \"Do it for me\" button if you get stuck.").designer().side(),
        Step("The barrel", "Pick Rectangle and draw a long, thin barrel along the top of the frame's front half. Then tick Fixed: a barrel doesn't move.").check(newFixed, "Draw a barrel and tick Fixed").at("tool:Rectangle|part:fixed").doIt(helpBarrel),
        Step("The bolt", "Draw a block behind the barrel, touching its back end. Its bottom edge should sit just below the barrel's middle line, so the bottom of the round's rim peeks out underneath (the ejector will catch it there). The bolt holds the round in, and the shot pushes it back.").check(newMoving, "Draw the bolt").at("tool:Rectangle").doIt(helpBolt),
        Step("Make it heavy", "With the bolt selected, set Mass to 400 g. The gun doesn't lock, so only the bolt's weight holds the round in for the instant the bullet needs. A light bolt slams open and bounces.").check(heavyBolt, "Set the bolt's Mass to 400 g").at("part:mass").onTab(0).doIt(helpBolt),
        Step("Let it slide", "Pick Track, click the middle of the bolt, then click about 50 mm behind it. The bolt can now slide back that far.").check(newTrack, "Add a track for the bolt").at("tool:Track").doIt(helpTrack),
        Step("Where the round sits", "In the Firing tab, add a firing system, then click the front face of the bolt, level with the middle of the barrel. That's where the back of the round sits.").check(faceSet, "Add a firing system and click the breech face").at("tab:Firing|firing:add").onTab(2).doIt(helpFace),
        Step("The muzzle", "Click Place muzzle, then click the front end of the barrel.").check(muzzleSet, "Place the muzzle").at("firing:muzzle").onTab(2).doIt(helpMuzzle),
        Step("The chamber", "Set Chamber to your barrel, so the round goes into the barrel and not the frame.").check(chamberSet, "Choose the barrel as the chamber").at("firing:chamber").onTab(2).doIt(helpChamber),
        Step("Loaded by hand", "Set Feeds from to Loaded by hand, and tick Physical rounds so the round and its empty case are real objects.").check(handFed, "Loaded by hand, physical rounds").at("firing:feeding|firing:physical").onTab(2).doIt(helpFeed),
        Step("The ejector", "Draw a small block just under the bolt's bottom edge, near the back end of its travel, and tick Fixed. As the bolt flies back, the bottom of the case's rim hits it and the case flips out.").check(newFixed, "Draw the ejector and tick Fixed").at("tool:Rectangle|part:fixed").onTab(0).doIt(helpEjector),
        Step("Fire!", "Go to the test bench and press Space. Watch the bolt blow open and the empty case fly out (the shot is slowed down for you).").check(fired, "Press Space on the test bench").at("mode:test|fire"),
        Step("Load the next round", "Grab one of the rounds lying on the bench (L adds more). Drag it into the open action, just in front of the bolt and in line with the barrel: it snaps into place. Then drag the bolt forward to close it.").check(reloaded, "Load a round and close the bolt").bench(),
        Step("Fire again", "You built a working gun. Fire it again!\n\nIdeas for next time: a spring to close the bolt by itself (then you'll need a catch to hold it open while you load), a magazine, or a trigger you draw.").check(fired, "Fire").at("fire"),
    }},
    {"breech", "Barrel, breech and cartridge", "Where the round sits, and what it fires.", "blowback", {
        Step("The Firing tab", "Everything about the shot lives in the Firing tab.").at("tab:Firing").onTab(2),
        Step("Breech face and muzzle", "The two red marks in the view: the breech face is where the back of the round sits, and the muzzle is the end of the barrel. These buttons move them.").at("firing:breechface").onTab(2),
        Step("Closed to fire", "The gun only fires when everything is closed up on a round, back where you drew it."),
        Step("Your own ammo", "Pick a round here, or click ... to design your own: the case, the bullet's shape, its weight and speed.").at("firing:cartridge").onTab(2),
    }},
    {"feeding", "Feeding and magazines", "How rounds get from the magazine into the chamber.", "blowback", {
        Step("Ways to feed", "Most guns feed from a magazine. You can also load one round at a time by hand, or use a simple built-in counter that skips the physics of feeding.").at("firing:feeding").onTab(2),
        Step("Any part can be a magazine", "In a part's Magazine section, make it Removable (it slides into a well and you swap it) or Built in (part of the gun and loaded in place, like an internal box or a tube). Draw the path the rounds sit along, side by side or nose to tail.").show("mag").at("mag:kind|mag:roundpath").onTab(0),
        Step("The magazine", "A magazine slides along its well (the green line) and clicks in. Fit to cartridge builds it around the round for you.").show("mag"),
        Step("Lips and spring", "The magazine's spring lifts each round up to the lips. If it's too weak, the slide comes back before the round is up, and the gun fails to feed.").show("mag").at("mag:lips|mag:spring"),
        Step("Feed one", "Drag the slide back and let go. It strips a round from the magazine and pushes it into the chamber.").check(fedOne, "Rack the slide").bench(),
    }},
    {"tubes", "Tube magazines and lifters", "Rounds pushed out of a tube, lifted to the bore by a part you draw.", "pump", {
        Step("A tube", "This pump-action carbine keeps six rounds nose to tail in a tube under the barrel. With physical rounds, the tube's spring pushes each one out of the mouth at the back, head first, as a real round. What happens next is up to the parts you draw.").show("tube").at("mag:tubefeed").onTab(0).designer(),
        Step("The lifter", "This brass fork is the lifter. While it's up, it sits in front of the tube's mouth and holds the next round back. It drops only at the very end of the pull, letting a round out onto it, then lifts it to the bore line for the bolt to push into the chamber.").show("lifter_r"),
        Step("Free play and the lock", "Pulling the fore-end first moves it 6 mm on its own. That lifts this lock out of the bolt's way, and only then does it take the bolt along. While it's closed, a catch joint set to hold it closed keeps the shot from blowing the action open.").show("lock"),
        Step("Worked by hand", "The fore-end is marked Worked by hand. It's the part you grab, and the part the reliability test pulls.").show("forend").at("ctrl:type").onTab(0),
        Step("Work it", "On the bench, drag the fore-end all the way back, then push it home. Watch a round come out of the tube, ride up on the lifter and go into the chamber.").check(tubeFed, "Work the fore-end").bench(),
        Step("Fire", "Fire it, then work it again.").check(fired, "Fire (Space)").at("fire"),
        Step("Your turn", "Make the tube's spring much weaker (Magazine section) and run the reliability test: too weak, and the round can't get out of the tube before the lifter comes back up. That's a failure to feed."),
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
        Step("Open bolt", "Built in also offers Open bolt: the sear holds the bolt back, and the trigger lets it fly forward to strip, chamber and fire a round as it closes. Most submachine guns work this way.").at("firing:control").onTab(2),
        Step("Fire it", "Pull the trigger.").check(fired, "Fire (Space)").at("fire").bench(),
    }},
    {"operating", "Locking the breech", "Why strong rounds need the breech locked, and one way to do it.", "tilting", {
        Step("Blowback", "Small rounds can use blowback: a heavy slide and spring hold the breech shut. Strong rounds need it locked for a moment."),
        Step("A locking barrel", "This barrel locks into the slide, and they recoil together for the first 2 mm.").show("barrel"),
        Step("Unlocking", "Then this fixed pin, riding in a slot in the barrel, pulls the barrel down and unlocks it. No special joint: just shapes.").show("xpin"),
        Step("Watch it", "Fire, and watch the barrel drop in slow motion.").check(fired, "Fire (Space)").at("fire").bench(),
        Step("Other ways", "Rifles often turn the bolt to lock it. Try the bolt-action example from Open in the top bar: drag its handle up and back.").at("file:open"),
        Step("Gas", "A locked breech needs something to open it after the shot. Firing > Gas drills a port in the bore: the gas behind the bullet pushes a part you drew (a piston, or the carrier itself) until it has moved its stroke. Give that part a spring to bring it back, and let it unlock the breech.").at("firing:gas").onTab(2),
    }},
    {"bench", "The test bench", "Work the gun by hand, load it, fire it, clear jams.", "blowback", {
        Step("Hands on", "On the bench everything moves for real. Drag any moving part: rack the slide, pull the trigger, pick up rounds.").bench(),
        Step("Fire", "Space or the FIRE button pulls the trigger. Hold it for full-auto guns.").check(fired, "Fire").at("fire"),
        Step("Magazines", "M drops the magazine. N puts a fresh one on the bench: drag it up into the gun. T tops up every magazine.").check(magOut, "Press M").at("bench:section|bench:newmag"),
        Step("Loading rounds", "L puts a loose round on the bench. Drag it onto the top of a magazine to load it.").check(roundsHandled, "Press L"),
        Step("Jams", "If it jams, the panel says why. Clear it like a real gun: rack the slide, or drag the stuck case out. Backspace restarts."),
        Step("Take it shooting", "When it works on the bench, take it to the range: Firing range on the main menu. Pick the map and the gun, then left mouse fires, right mouse aims, R reloads and B works the action. Every part still moves for real, so it jams for real too."),
    }},
    {"animations", "Posing and animations", "How the gun is held, and recording hands that reload, inspect and work the gun.", "blowback", {
        Step("Pose mode", "On the test bench, Pose shows a body holding the gun. Everything about how it's held lives here, and the animations too.").check(posingNow, "Click Pose").at("mode:pose").bench(),
        Step("Three holds", "Aim, Hip and Lowered are three ways of holding the gun, each posed on its own. Drag the green dots to put the hands on the gun (click a dot to pick it, then drag it), and the dot at the end of a green line to turn the hand. The orange dot over the head moves the body.").at("pose:holds"),
        Step("Fingers", "Open Fingers, or pick a fingertip in the view, then drag it: the whole finger bends to put the tip where you drag, within what a real hand can do. The thumb can swing across the palm to meet any fingertip. Shift-drag just curls a finger.").check(poseChanged, "Move a finger").at("pose:fingers"),
        Step("Edit an animation", "Open Animations and tick Edit an animation. From now on the hands you drag belong to the animation, at the moment on its timeline.").check(editingAnim, "Tick Edit an animation").at("anim:section|anim:edit"),
        Step("Pick one", "Choose Reload. There's also Reload (empty), Inspect, Clear a jam, Work the action, and one for each control on the gun. Anything you don't animate just happens, as before.").check(reloadPicked, "Choose Reload").at("anim:slot"),
        Step("Make a key", "Move the time slider to about 0.4 s, then drag a hand somewhere: a key is made there. The hands start and end in the aiming hold and move smoothly through your keys.").check(newKey, "Move the slider, then drag a hand").at("anim:time"),
        Step("Grip a part", "Put the support hand on the slide at a key and tick its grips its part. From that key the hand holds the slide and moves it, through the physics, until a later key lets go. So a slide pulled back too short really won't chamber a round.").check(newGrip, "Tick grips its part").at("anim:grip"),
        Step("Magazine moments", "A key can drop the magazine or put a fresh one in, so a reload works without animating the magazine itself.").check(newEvent, "Set a key's Magazine").at("anim:mag"),
        Step("Play it", "Preview plays the hands alone. Play on the gun runs it for real, and the animation waits for the gun: a hand gripping a slide that's still moving waits for it.").check(animPlayed, "Play on the gun").at("anim:play"),
        Step("Using them", "On the bench, R plays your Reload and B works the action (your Work the action animation, or one made from your parts). Start from the automatic one gives you a working start to change. They play at the firing range too, and I plays Inspect there (with none, the gun is simply looked over). A key can also move the whole gun (The gun at this key): lift it, turn it, roll it to see into the port."),
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
bool tutAnchorAfter(bool r, const char* id) { tutAnchor(id); return r; }
void tutAnchor(const char* id) {
    if (A.tutLesson < 0) return;
    gAnchors[id] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetFrameCount()};
}

static TutState gWas;
static bool gStepDone = false;
static double gDoneAt = 0, gEnteredAt = 0;
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
        if (st.sideView && !A.ortho) { sideView(); fitView(); }
    }
    gWas = snapshot();
    gStepDone = false;
    gEnteredAt = GetTime();
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
    if (std::getenv("GUNSMITH_TUTAUTO") && !gStepDone) {   // (automated checks: press "Do it for me", Next, and fire)
        if (st.helper) st.helper();
        else if (!st.done && !last && GetTime() - gEnteredAt > 0.3) { enterStep(A.tutStep + 1); return; }
        else if (st.done == fired && A.screen == Screen::Workshop) { if (A.mode != Mode::Test) setMode(Mode::Test); if (A.S && A.S->shots == gWas.shots) fireDown(); }
    }
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
    if (st.helper && !gStepDone) {
        ImGui::Dummy({0, 2 * s});
        if (ImGui::SmallButton(ICON_FA_WAND_MAGIC_SPARKLES "  Do it for me")) { uiTick(); st.helper(); }
        if (tipHover()) ImGui::SetTooltip("Makes this step's part or setting for you, so you can carry on.");
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
