// Gunsmith3D - Shared app state, small helpers, undo, modes.
#include "app/app.hpp"
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
    std::ofstream(AUTOSAVE) << designToJson(A.stash ? *A.stash : A.design, true);
    A.saveDue = false; A.lastSave = GetTime();
}
void autosaveTick() {
    if (!A.saveDue) return;
    double every = CFG.autosave == AutosaveEvery::Minute ? 60 : CFG.autosave == AutosaveEvery::FiveMinutes ? 300 : -1;
    if (CFG.autosave == AutosaveEvery::Change || (every > 0 && GetTime() - A.lastSave >= every)) save();
}
void commit() {
    A.hist.push_back(A.lastSnap);
    if (A.hist.size() > 120) A.hist.erase(A.hist.begin());
    A.future.clear();
    A.lastSnap = designToJson(A.design);
    A.saveDue = true;
    autosaveTick();
}
void undo() {
    if (A.hist.empty()) return;
    A.future.push_back(A.lastSnap);
    designFromJson(A.hist.back(), A.design);
    A.hist.pop_back();
    A.lastSnap = designToJson(A.design);
    A.sel = {};
    save();
}
void redo() {
    if (A.future.empty()) return;
    A.hist.push_back(A.lastSnap);
    designFromJson(A.future.back(), A.design);
    A.future.pop_back();
    A.lastSnap = designToJson(A.design);
    A.sel = {};
    save();
}
void toast(const std::string& t) { A.toast = t; A.toastT = 2.5f + t.size() / 40.f; }  // longer messages stay up longer

float bodyDepth(const BodyDef& b) { return (float)std::max(1.0, b.thick) * U; }
float bodyBias(int i) { return i * 0.004f; }
float bodyZ(int i) { return (float)A.design.bodies[i].z * U + bodyBias(i); }                       // centre in depth
float bodyFront(int i) { return bodyZ(i) + bodyDepth(A.design.bodies[i]) / 2; }                    // near face
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
    A.S = sim::build(A.design);
    A.S->handStrength = A.handN;
    A.handleBody = nullptr; A.hoverHandle = false;
    if (A.S->hasFace && !A.S->magTmpl && A.S->feedMode != "internal")  // hand-loaded designs start with ammunition on the bench
        for (int k = 0; k < 5; k++) spawnBenchRound(true);
    A.trace.clear(); A.shells.clear(); A.puffs.clear(); A.flash = 0; A.hover = nullptr;
}
static void resetSim() { A.S.reset(); A.running = false; A.shells.clear(); A.puffs.clear(); A.trace.clear(); A.hover = nullptr; A.dragKind = 0; }
void setMode(Mode m) {
    if (m == A.mode) return;
    A.mode = m;
    A.drafting = false; A.draft.clear(); A.draftHasF = false; A.dragKind = 0;
    A.posing = false; A.poseDrag = -1;
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
void backToMenu() {
    if (A.screen == Screen::MapEditor) leaveMapEditor();
    resetSim(); A.mode = Mode::Build;
    if (A.stash) { A.design = *A.stash; A.stash.reset(); A.lastSnap = designToJson(A.design); }
    if (A.saveDue && CFG.autosave != AutosaveEvery::Off) save();
    A.screen = Screen::Menu;
    A.benchOnly = false;
    A.showPause = A.showSettings = A.showBenchPicker = false;
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
    int n = 0;
    for (auto& b : A.S->bodies) if (b.isMag && b.loose) n++;
    if (!sim::spawnMagazine(*A.S, {ex.x + 70 + 45 * (n % 5), c.y + drop}, false)) toast("Too many magazines on the bench. Restart to clear them.");
}

const char* TOOL_HINT[] = {
    "Click a part to select it and drag it to move: X, Y or Z while dragging keeps it to one axis. Squares are corners (right-click removes one). Diamonds move an edge, Shift-drag extrudes it, right-click splits it in half.",
    "Click to place corners. Click the first one again, or press Enter, to close the part.",
    "Click where two parts overlap to pin them so they pivot. Over one part, it pins it to the ground.",
    "Click one part, then another: they become one solid piece. Nothing is added to the gun.",
    "Click the part that should slide, then where it slides to. Shift adds bends. The ends are hard stops.",
    "Click one end of the spring, then the other.",
    "Click where the cartridge head sits against the bolt or slide.",
    "Click the muzzle.",
    "Drag out a rectangle.",
    "Click the centre and drag out the radius.",
    "Click where the finger presses this control.",
    "Click the tip of the striker or firing pin.",
    "Drag a line across a part to cut it into pieces.",
    "Drag the mirror axis. A mirrored copy of the selected part appears across it.",
    "Click along the inside of the magazine, starting where rounds are fed from. Enter (or a double-click) finishes.",
    "Click a part's surface to cut into it there, or drag to size the cut. It goes straight in from that face: turn the view to cut from any side. Shape and depth are in the panel."};
void setTool(Tool t) {
    if (A.mode == Mode::Test) setMode(Mode::Build);
    A.tool = t; A.drafting = false; A.draft.clear(); A.draftHasF = false;
}
