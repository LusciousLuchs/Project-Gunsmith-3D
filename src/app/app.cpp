// Gunsmith3D - Shared app state, small helpers, undo, modes.
#include "app/app.hpp"
#include "app/fitbench.hpp"
#include "app/paths.hpp"
#include "app/npcedit.hpp"
#include "app/range.hpp"
#include "app/mapedit.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "app/gfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

// ---------------------------------------------------------------- small helpers
Color hexColor(const std::string& h, Color def) {
    if (h.size() != 7 || h[0] != '#') return def;
    unsigned v = 0;
    if (std::sscanf(h.c_str() + 1, "%x", &v) != 1) return def;
    return Color{(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, 255};
}
std::string colorHex(const float c[3]) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", (int)(c[0] * 255 + .5f), (int)(c[1] * 255 + .5f), (int)(c[2] * 255 + .5f));
    return b;
}
Color shade(Color c, float f) {
    return Color{(unsigned char)std::clamp(c.r * f, 0.f, 255.f), (unsigned char)std::clamp(c.g * f, 0.f, 255.f),
                 (unsigned char)std::clamp(c.b * f, 0.f, 255.f), c.a};
}
Vector3 W3(V2 mm, float z) { return {(float)mm.x * U, -(float)mm.y * U, z}; }
V2 toMM(Vector3 w) { return {w.x / U, -w.y / U}; }
App A;               // the one app state, shared by every module
double gSnap = 0.5;  // mm
double snapv(double v, bool on) { return on && gSnap > 0 ? std::round(v / gSnap) * gSnap : std::round(v * 100) / 100; }
std::string uid() {
    static int n = 0;
    char b[16];
    std::snprintf(b, sizeof b, "p%06x", (unsigned)(GetRandomValue(0, 0xffffff) ^ (++n * 7919)));
    return b;
}

void save() {  // the sandbox design: while another gun is on the bench, that's the one kept aside
    std::ofstream(autosavePath()) << designToJson(A.stash ? *A.stash : A.design, true);
    A.saveDue = false; A.lastSave = GetTime();
}
void autosaveTick() {
    if (!A.saveDue) return;
    double every = CFG.autosave == AutosaveEvery::Minute ? 60 : CFG.autosave == AutosaveEvery::FiveMinutes ? 300 : -1;
    if (CFG.autosave == AutosaveEvery::Change || (every > 0 && GetTime() - A.lastSave >= every)) save();
}
// what a step did, in words, from the design before it and after (for the History list when nothing named it)
static std::string partWord(const BodyDef& b) { return b.name.empty() ? std::string("a part") : b.name; }
std::string stepLabel(const std::string& beforeJson, const Design& now) {
    Design was;
    if (!designFromJson(beforeJson, was)) return "Changed the gun";
    auto& P = was.bodies; auto& N = now.bodies;
    if (N.size() > P.size()) {
        for (auto& b : N) { bool old = false; for (auto& o : P) if (o.id == b.id) { old = true; break; } if (!old) return "Added " + partWord(b) + (N.size() - P.size() > 1 ? " (and more)" : ""); }
        return "Added parts";
    }
    if (N.size() < P.size()) {
        for (auto& o : P) { bool kept = false; for (auto& b : N) if (o.id == b.id) { kept = true; break; } if (!kept) return "Removed " + partWord(o) + (P.size() - N.size() > 1 ? " (and more)" : ""); }
        return "Removed parts";
    }
    int moved = 0; std::string movedName;
    for (size_t i = 0; i < N.size(); i++) {
        const BodyDef &o = P[i], &b = N[i];
        if (o.pts.size() != b.pts.size()) return "Reshaped " + partWord(b);
        bool same = true, shifted = b.pts.size() > 0;
        double dx = b.pts.empty() ? 0 : b.pts[0].x - o.pts[0].x, dy = b.pts.empty() ? 0 : b.pts[0].y - o.pts[0].y;
        for (size_t k = 0; k < b.pts.size(); k++) {
            if (std::fabs(b.pts[k].x - o.pts[k].x) > 1e-6 || std::fabs(b.pts[k].y - o.pts[k].y) > 1e-6) same = false;
            if (std::fabs(b.pts[k].x - o.pts[k].x - dx) > 1e-6 || std::fabs(b.pts[k].y - o.pts[k].y - dy) > 1e-6) shifted = false;
        }
        if (!same && shifted) { if (!moved++) movedName = partWord(b); continue; }
        if (!same) return "Reshaped " + partWord(b);
        if (std::fabs(o.z - b.z) > 1e-9 && std::fabs(o.thick - b.thick) < 1e-9) { if (!moved++) movedName = partWord(b); continue; }
        if (std::fabs(o.thick - b.thick) > 1e-9) return "Changed the thickness of " + partWord(b);
        if (o.carves.size() != b.carves.size()) return (b.carves.size() > o.carves.size() ? "Cut into " : "Filled a cut in ") + partWord(b);
        if (o.color != b.color || o.material != b.material || o.texture != b.texture) return "Changed the look of " + partWord(b);
        if (o.name != b.name) return "Renamed " + partWord(b);
        if (o.round != b.round || o.mesh.v.size() != b.mesh.v.size() || o.tiltX != b.tiltX || o.tiltY != b.tiltY) return "Reshaped " + partWord(b);
    }
    if (moved) return "Moved " + movedName + (moved > 1 ? " (and its piece)" : "");
    if (now.joints.size() > was.joints.size()) return "Added a " + now.joints.back().type;
    if (now.joints.size() < was.joints.size()) return "Removed a joint";
    for (size_t i = 0; i < now.joints.size(); i++) {
        const auto &o = was.joints[i], &j = now.joints[i];
        if (o.p.x != j.p.x || o.p.y != j.p.y || o.pa.x != j.pa.x || o.pa.y != j.pa.y || o.pb.x != j.pb.x || o.pb.y != j.pb.y || o.path.size() != j.path.size()) return "Moved a " + j.type;
        if (o.k != j.k || o.rest != j.rest) return "Changed a " + j.type;
    }
    return "Changed a setting";
}
void commit() {
    {   // new parts go into the layer you're working on (if it's still there and not locked)
        EdLayer al = layerOf(A.design, A.activeLayer);
        int into = al.id == A.activeLayer && !al.locked ? A.activeLayer : 0;
        for (auto& b : A.design.bodies) if (b.edLayer < 0) b.edLayer = into;
    }
    magazineLayer(A.design);   // (a magazine sits in a layer of its own)
    for (auto& b : A.design.bodies) normalizeDepth(b);
    for (auto& b : A.design.bodies) meshSync(b);   // free 3D meshes: the outline follows the mesh
    for (auto& b : A.design.bodies) followMarks(b);   // engravings go with a part that moved as a whole   // shaped in depth: follow added or removed corners
    bool mirrored = false;
    for (auto& b : A.design.bodies) if (!b.mirror.empty()) { mirrored = true; break; }
    if (mirrored) {   // live mirrors: whichever side changed, the other follows
        Design prev;
        if (designFromJson(A.lastSnap, prev)) syncMirrors(A.design, &prev, A.sel.kind == 1 ? A.sel.idx : -1);
    }
    syncBarrels(A.design);  // barrels set the firing system's chambers and muzzles
    autoAttach(A.design);   // an attachment: where it mounts and its lenses, unless set by hand
    for (auto& b : A.design.bodies) {   // a fitted magazine reshaped by hand keeps its new shape (it's not refitted over it)
        if (!b.gen.on || b.gen.byHand || b.gen.pts0.empty()) continue;
        bool same = b.pts.size() == b.gen.pts0.size();
        for (size_t k = 0; same && k < b.pts.size(); k++) same = std::fabs(b.pts[k].x - b.gen.pts0[k].x) < 0.02 && std::fabs(b.pts[k].y - b.gen.pts0[k].y) < 0.02;
        if (!same) { freezeMagazine(b, A.design.breech.cart); toast("Reshaped by hand: the magazine keeps this shape, and its rounds keep their place (drag their dots to move them)."); }
    }
    std::string label = A.nextLabel;
    A.nextLabel.clear();
    if (label.empty()) label = stepLabel(A.lastSnap, A.design);
    A.hist.push_back(A.lastSnap);
    A.histLabel.resize(A.hist.size() - 1);
    A.histLabel.push_back(label);
    if (A.hist.size() > 120) { A.hist.erase(A.hist.begin()); A.histLabel.erase(A.histLabel.begin()); }
    A.future.clear(); A.futureLabel.clear();
    A.lastSnap = designToJson(A.design);
    A.saveDue = true;
    autosaveTick();
}
void undo() {
    if (A.hist.empty()) return;
    A.histLabel.resize(A.hist.size());
    A.futureLabel.resize(A.future.size());
    A.futureLabel.push_back(A.histLabel.back()); A.histLabel.pop_back();
    A.future.push_back(A.lastSnap);
    designFromJson(A.hist.back(), A.design);
    A.hist.pop_back();
    A.lastSnap = designToJson(A.design);
    A.sel = {};
    save();
}
void redo() {
    if (A.future.empty()) return;
    A.histLabel.resize(A.hist.size());
    A.futureLabel.resize(A.future.size());
    A.histLabel.push_back(A.futureLabel.back()); A.futureLabel.pop_back();
    A.hist.push_back(A.lastSnap);
    designFromJson(A.future.back(), A.design);
    A.future.pop_back();
    A.lastSnap = designToJson(A.design);
    A.sel = {};
    save();
}
void toast(const std::string& t) { A.toast = t; A.toastT = std::min(8.f, 2.5f + t.size() / 40.f); }  // longer messages stay up longer (8 s at most)

float bodyDepth(const BodyDef& b) { return (float)std::max(1.0, b.thick) * U; }
// a hair's nudge in depth so two parts with faces in the same plane don't flicker through each other. Kept tiny and
// bounded (it used to grow with every part: a barrel late in a big gun sat most of a millimetre off its bore), and none for
// round parts, whose curved sides never share a plane with anything
float bodyBias(int i) {
    if (i < 0 || i >= (int)A.design.bodies.size() || A.design.bodies[i].round) return 0.f;
    return (float)(i % 4) * 0.004f;
}
float bodyZ(int i) { return (float)A.design.bodies[i].z * U + bodyBias(i); }                       // centre in depth
float bodyFront(int i) {   // the face toward you: handles sit on it, so a part can be shaped from either side
    float c = bodyZ(i), h = bodyDepth(A.design.bodies[i]) / 2;
    return A.cam.position.z >= c ? c + h : c - h;
}
std::string bname(const std::string& id) {
    if (id.empty() || id == "world") return "Ground";
    int i = A.design.bodyIndex(id);
    return i >= 0 ? A.design.bodies[i].name : "?";
}
V2 curMM(const std::string& id, V2 p) { return A.S ? sim::designToWorld(*A.S, id, p) : p; }
float frontZ(const std::string& id) {
    int i = A.design.bodyIndex(id);
    return i < 0 ? 0.f : bodyFront(i);
}
int magwellIndex(const std::string& magId) {
    for (size_t i = 0; i < A.design.joints.size(); i++)
        if (A.design.joints[i].type == "magwell" && A.design.joints[i].a == magId) return (int)i;
    return -1;
}

// ---------------------------------------------------------------- modes and simulation control
static void buildSim() {
    Design d = A.design;
    for (auto& it : A.fitItems) {   // attachments fitted on: their weight on the part they ride on (the physics feels it)
        std::string c = fitCarrier(it);
        if (!c.empty()) d.addMass.push_back({c, attachmentMass(it.att)});
    }
    A.S = sim::build(d);
    A.S->handStrength = A.handN;
    A.handleBody = nullptr; A.hoverHandle = false;
    if (A.S->hasFace && !A.S->magTmpl && A.S->feedMode != "internal")  // hand-loaded designs start with ammunition on the bench
        for (int k = 0; k < 5; k++) spawnBenchRound(true);
    A.trace.clear(); A.shells.clear(); A.puffs.clear(); A.flash = 0; A.hover = nullptr;
}
static void resetSim() { A.S.reset(); A.running = false; A.shells.clear(); A.puffs.clear(); A.trace.clear(); A.hover = nullptr; A.dragKind = 0; }
void setMode(Mode m) {
    if (m == A.mode) return;
    if (m == Mode::Test && A.design.attach.on) { toast("This is an attachment: it's tried on a gun's rails and mounts, not on the test bench."); return; }
    A.mode = m;
    A.drafting = false; A.draft.clear(); A.draftHasF = false; A.dragKind = 0;
    A.posing = false; A.poseDrag = -1; A.fitting = false; A.fitPick.clear();
    if (m == Mode::Test) { buildSim(); A.running = true; }
    else resetSim();
}
void restartTest() { buildSim(); A.running = true; }
void enterWorkshop(bool benchOnly) {
    A.screen = Screen::Workshop;
    A.benchOnly = benchOnly;
    A.showPause = A.showSettings = A.showBenchPicker = false;
    A.sel = {};
    resetSim(); A.mode = Mode::Build;
    if (benchOnly) setMode(Mode::Test);
    fitView();
}
void enterBenchWith(const Design& d) {
    if (!A.stash) A.stash = std::make_unique<Design>(A.design);
    A.design = d;
    enterWorkshop(true);
}
void careerLeave();   // (app/career.cpp)
void backToMenu() {
    careerLeave();
    if (A.screen == Screen::MapEditor) leaveMapEditor();
    if (A.screen == Screen::Range) leaveRange();
    if (A.screen == Screen::NpcEditor) leaveNpcEditor();
    resetSim(); A.mode = Mode::Build;
    if (A.stash) { A.design = *A.stash; A.stash.reset(); A.lastSnap = designToJson(A.design); }
    if (A.saveDue && CFG.autosave != AutosaveEvery::Off) save();
    A.screen = Screen::Menu;
    A.benchOnly = false;
    A.showPause = A.showSettings = A.showBenchPicker = A.showRangePicker = false;
    A.sel = {};
    fitView();
}
void fireDown() {
    if (A.mode != Mode::Test) setMode(Mode::Test);
    A.running = true; A.S->trigger = true; A.S->pressed = true;
}
void spawnBenchRound(bool quiet) {
    if (!A.S || !A.S->hasFace) { toast("Set a breech face first: rounds load in front of it."); return; }
    const Cartridge& c = A.design.breech.cart;
    V2 f = curMM(A.design.breech.breech, A.design.breech.face);
    double ground = groundLevelMM(A.design), y = ground - c.headDia() / 2 - 0.5;
    // the first empty spot in a row on the baseplate, starting under the breech face and working outwards
    for (int k = 0; k < 40; k++) {
        int slot = (k % 2 ? -1 : 1) * ((k + 1) / 2);
        V2 head{f.x - c.oal * 0.3 + slot * (c.oal + 6), y};
        bool taken = false;
        for (auto& b : A.S->bodies) {
            if (!b.isRound || b.gone) continue;
            V2 h = sim::wpt(b, {b.headLx, 0});
            if (std::fabs(h.x * 1000 - head.x) < c.oal * 0.8 && std::fabs(h.y * 1000 - head.y) < c.headDia() * 2) { taken = true; break; }
        }
        if (taken) continue;
        if (sim::spawnRound(*A.S, head)) { if (!quiet) toast(TextFormat("Round on the bench (%d there now).", sim::looseRounds(*A.S))); }
        else toast("The bench is full. Restart to clear it.");
        return;
    }
    toast("No room left on the bench. Pick some rounds up or restart.");
}
void topUpMagazines() {
    if (!A.S || !sim::seatedMag(*A.S)) { toast("This design has no magazine."); return; }
    int n = sim::fillMagazines(*A.S);
    toast(n ? TextFormat("Topped up the magazines (%d round%s).", n, n == 1 ? "" : "s") : "The magazines are already full.");
}
void spawnSpareMag() {
    if (!A.S || !A.S->magTmpl) { toast("This design has no removable magazine to swap."); return; }
    auto* J = sim::magwell(*A.S);
    V2 ex = J ? V2{sim::wpt(*J->B, J->path.back()).x * 1000, sim::wpt(*J->B, J->path.back()).y * 1000} : V2{0, 0};
    V2 c = centroidOf(*A.S->magTmpl);
    double ymax = -1e9;
    for (auto& p : outlineOf(*A.S->magTmpl)) ymax = std::max(ymax, p.y);
    double drop = groundLevelMM(A.design) - ymax - 1.5;
    int n = sim::looseMagazines(*A.S);
    if (!sim::spawnMagazine(*A.S, {ex.x + 70 + 45 * (n % 5), c.y + drop}, false)) { toast("No room for another magazine on the bench."); return; }
    if (n >= CFG.maxLooseMags) {   // over the limit: the oldest one lying out goes now
        sim::tidy(*A.S, 0, 1e9, 1e9, CFG.maxLooseMags);
        toast(TextFormat("At most %d magazines lie out at once: the oldest was cleared away (Settings > Test bench).", CFG.maxLooseMags));
    }
}

const char* TOOL_HINT[] = {
    "Click anything (a part, a corner, a spring end, a track point) to pick it; drag it after that to move it: X, Y or Z while dragging keeps it to one axis. Turn the view: the selected part's faces (top, bottom, ends, front, back) get arrow dots to push or pull them. Squares are corners (right-click removes one). Diamonds move an edge, Shift-drag extrudes it, right-click splits it in half.",
    "Click to place corners. Click the first one again, or press Enter, to close the part.",
    "Click where two parts overlap to pin them so they pivot. Over one part, it pins it to the ground.",
    "Click one part, then another: they become one solid piece. Nothing is added to the gun.",
    "Click the part that should slide, then where it slides to. Shift adds bends. The ends are hard stops.",
    "Click one end of the spring, then the other.",
    "Click where the cartridge head sits against the bolt or slide.",
    "Click the muzzle.",
    "Drag out a rectangle.",
    "Circle: Lengthwise (strip beside the tools): drag out its side view, it is round along its longer side. Across: click the centre and drag the radius. Then push and pull its ends and round side.",
    "Click where the finger presses this control.",
    "Click the tip of the striker or firing pin.",
    "Drag a line across a part to cut it into pieces.",
    "Drag the mirror axis. A mirrored copy of the selected part appears across it.",
    "Click along the inside of the magazine, starting where rounds are fed from. Enter (or a double-click) finishes.",
    "Click a part's surface to cut into it there, or drag to size the cut. It goes straight in from that face: turn the view to cut from any side. It snaps to the part's middle, in line with its other cuts, and to the grid (hold Alt to place it freely). Drag a cut's dot to move it.",
    "",   // (grip markers: replaced by Pose on the test bench)
    "Drag from the breech end to the muzzle. Its cartridge, rifling and length are in its panel.",
    "Drag between two points to measure the length, how far across and up, and the angle. Snaps to corners and joints.",
    "Press on any face of any part and drag to push or pull it (Ctrl-drag pulls out a new piece). Type a number while dragging, or just after, and press Enter to make it exact. Click the size numbers beside a part to type its real length, height or thickness. Corners: squares front, circles back, drag one to shape the part in depth.",
    "Engrave: click a part to pick it, then draw on its surface with the mouse, or switch to Text and click where the words go. Any face, from any side. Erase removes a line or a word.",
    "Scale: click a part, click a handle of its box, then drag it. Corners keep the proportions (Shift: free), edges stretch one way, Ctrl scales about the middle. Or type sizes in the panel.",
    "Rail: drag along the top (or bottom) of a part to lay a Picatinny rail on it, slotted to the standard. Put it on a side from its panel. Attachments clamp on anywhere along it.",
    "Attachment point: click a part's surface to put a mount there (a bayonet lug, a side rail, a muzzle thread, your own kind). Attachments fit points of the same kind. Its kind is set in the panel.",
    "3D vertices (G): click a part to shape it in 3D. Click corners or faces to pick them (Shift adds more) and drag (Z: in depth). E pushes the picked faces out together, M joins picked corners into one, J connects two across a face, Delete removes, Ctrl+A picks all."};
static_assert(sizeof(TOOL_HINT) / sizeof(TOOL_HINT[0]) == (size_t)Tool::Count, "a hint for every tool");
void setTool(Tool t) {
    if (A.mode == Mode::Test) setMode(Mode::Build);
    A.tool = t; A.drafting = false; A.draft.clear(); A.draftHasF = false;
}
