// Gunsmith3D - Designer panel. Four tabs: Inspect (what's selected), Outline (every part and joint),
// Firing (breech, cartridge, feeding, ignition) and File (save, load, examples). Each shows only what matters
// for the job at hand; less-used settings fold away.
#include "carve.hpp"
#include "mounts.hpp"
#include "app/usertex.hpp"
#include "app/pieces.hpp"
#include "app/app.hpp"
#include "app/scaletool.hpp"
#include "app/gfx.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "app/paths.hpp"
#include "app/keys.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "imgui.h"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static float S() { return CFG.uiScale; }
static int lenDec() { return CFG.units == Units::Metric ? 2 : 3; }
static void muted(const char* fmt, ...) IM_FMTARGS(1);
static void muted(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0);
    ImGui::TextV(fmt, ap);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    va_end(ap);
}
static void warn(const char* t) { ImGui::PushTextWrapPos(0); ImGui::TextColored(colBad(), "%s", t); ImGui::PopTextWrapPos(); }
int gPanelTab = -1;
static void loadDesign(const Design& nd, const std::string& msg);
void openExample(const char* key, const char* msg) { loadDesign(loadPreset(key), msg); }
void openDesignInEditor(const Design& d, const std::string& msg) { loadDesign(d, msg); }   // (career mode's office)
static void loadDesign(const Design& nd, const std::string& msg) {
    A.fitItems.clear(); A.fitSel = -1;   // (attachments belong with the gun they were fitted to)
    A.hist.push_back(designToJson(A.design));
    A.future.clear();
    A.design = nd; syncBarrels(A.design); A.lastSnap = designToJson(A.design); A.sel = {}; A.selVs.clear();
    A.saveName[0] = 0;  // saves under its own name until told otherwise
    save(); fitView();
    toast(msg);
}


// ---------------------------------------------------------------- carves
static bool shapeButtons(std::string& shape) {
    static const char* keys[] = {"circle", "half", "rect", "tri"};
    static const char* names[] = {ICON_FA_CIRCLE " Circle", ICON_FA_CIRCLE_HALF_STROKE " Half", ICON_FA_SQUARE " Rect", ICON_FA_PLAY " Tri"};
    float w = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
    bool ch = false;
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        if (accentButton(names[i], shape == keys[i], {w, 0})) { if (shape != keys[i]) ch = true; shape = keys[i]; }
    }
    return ch;
}
static const char* shapeName(const std::string& s) {
    return s == "half" ? "Half circle" : s == "rect" ? "Rectangle" : s == "tri" ? "Triangle" : "Circle";
}
// The Carve tool's own settings: what the next cut will be.
static void carveToolPanel() {
    bool wasEdited = editedAny;
    ImGui::SeparatorText("Carve tool");
    muted("Click a part's surface to cut into it, or drag to size the cut. It goes straight in from the face you click: "
          "turn the view to cut from the top, the ends or the far side.");
    ImGui::PushID("tool"); shapeButtons(A.carveShape); ImGui::PopID();  // (the selected cut has its own set below)
    tutAnchor("carve:shape");
    if (A.carveShape == "circle") pNumU("Diameter##tool", A.carveW, 0.5, Q::Length, -1, "Used for a plain click. Dragging sets the size instead.");
    else {
        pNumU("Width##tool", A.carveW, 0.5, Q::Length, -1, "Used for a plain click. Dragging sets the size instead.");
        pNumU("Height##tool", A.carveH, 0.5, Q::Length);
    }
    pCheck("Right through##tool", A.carveThrough, "Cut all the way through whatever is in the way of this part.");
    tutAnchor("carve:through");
    if (!A.carveThrough) { pNumU("Depth##tool", A.carveDepth, 0.5, Q::Length, -1, "How deep the cut goes, measured straight in from where you click."); tutAnchor("carve:depth"); }
    A.carveW = std::max(0.2, A.carveW); A.carveH = std::max(0.2, A.carveH); A.carveDepth = std::max(0.1, A.carveDepth);
    editedAny = wasEdited;  // tool settings aren't part of the design
    ImGui::Spacing();
}
// The Measure tool: the line's numbers, and keeping it shown while you move parts with the other tools
static void measureToolPanel() {
    ImGui::SeparatorText("Measure");
    if (!A.measured) { muted("Drag between two points to measure them. It snaps to corners and joints."); return; }
    double dx = A.measureB.x - A.measureA.x, dy = A.measureA.y - A.measureB.y, L = std::hypot(dx, dy);
    auto row = [](const char* k, double v) {
        ImGui::TextDisabled("%s", k); ImGui::SameLine(80 * S());
        ImGui::Text("%.2f mm   %.3f in   %s", v, v / 25.4, fracInch(v).c_str());
    };
    row("Length", L); row("Across", dx); row("Up", dy);
    ImGui::TextDisabled("Angle"); ImGui::SameLine(80 * S()); ImGui::Text("%.1f\xc2\xb0", std::atan2(dy, dx) * 180 / 3.14159265358979);
    ImGui::Checkbox("Keep it shown##measure", &A.measureKeep);
    if (tipHover()) ImGui::SetTooltip("Keep the measuring line on screen when you switch tools,\nso you can move a part and see it line up. (It stays where you drew it.)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##measure")) { A.measured = false; A.measureKeep = false; }
    ImGui::Spacing();
}
// The Scale tool: the box's size, typed; a percentage; and what a corner drag does
static void scaleToolPanel() {
    auto& D = A.design;
    ImGui::SeparatorText("Scale");
    if (!(A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)D.bodies.size())) {
        muted("Click a part to scale it. Then drag a corner of its box to make it bigger or smaller (Shift: stretch freely), or an edge to stretch it one way. Ctrl: about its middle. Or type sizes here.");
        return;
    }
    std::vector<int> parts = weldedWith(D, A.sel.idx);
    std::vector<int> corners = A.selVs.size() >= 2 ? A.selVs : std::vector<int>{};
    V2 lo, hi;
    if (!scaleBox(D, parts, corners, lo, hi)) return;
    if (!corners.empty()) muted("Scaling the %d picked corners. (Click the part's outline away from its corners to scale all of it.)", (int)corners.size());
    else if (parts.size() > 1) muted("Scaling the welded piece: %d parts together.", (int)parts.size());
    V2 c{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2};
    double W = hi.x - lo.x, Hh = hi.y - lo.y, T = 0;
    for (int i : parts) T = std::max(T, D.bodies[i].thick);
    double w = W, h = Hh, t = T;
    bool changed = false;
    double sx = 1, sy = 1, sz = 1;
    if (pNumU("Width##scale", w, 0.5, Q::Length, 1, "Front to back, in the side view.") && W > 1e-6 && w > 0.1) { sx = w / W; if (A.scaleKeep) sy = sz = sx; changed = true; }
    if (pNumU("Height##scale", h, 0.5, Q::Length, 1, "Top to bottom, in the side view.") && Hh > 1e-6 && h > 0.1) { sy = h / Hh; if (A.scaleKeep) sx = sz = sy; changed = true; }
    if (corners.empty() && pNumU("Thickness##scale", t, 0.5, Q::Length, 1, "Across the gun (the thickest part of the piece). Round parts take theirs from their outline.") && T > 1e-6 && t > 0.1) {
        sz = t / T; if (A.scaleKeep) sx = sy = sz; changed = true;
    }
    if (!corners.empty()) sz = 1;
    if (changed) { scaleParts(D, parts, c, sx, sy, sz, corners); editedAny = true; }
    static double pct = 100;
    float wb = ImGui::GetContentRegionAvail().x;
    pNum("By", pct, 1, "%.0f%%", "Scale everything by this much, about its middle (100% = no change).");
    pct = std::clamp(pct, 2.0, 5000.0);
    if (ImGui::Button("Scale by that", {wb, 0}) && std::fabs(pct - 100) > 1e-6) {
        double s = pct / 100;
        scaleParts(D, parts, c, s, s, corners.empty() && A.scaleDepth ? s : 1, corners);
        editedAny = true;
    }
    pCheck("Keep proportions", A.scaleKeep, "Typing one size scales the others to match.");
    pCheck("Thickness too", A.scaleDepth, "Dragging a corner (without Shift) and Scale by that also scale the thickness.");
    muted("Pins, slots, spring ends, the breech face, muzzle and hands on the part go with it. Springs keep their length and strength.");
}
static void engraveToolPanel() {
    bool wasEdited = editedAny;
    ImGui::SeparatorText("Engraving pen");
    muted("Click a part to pick it, then draw on it, or write on it. Any face, from any side: turn the view to engrave the top or the far side. Engravings are looks only.");
    float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    const char* modes[3] = {"Draw", "Text", "Erase"};
    for (int k = 0; k < 3; k++) { if (k) ImGui::SameLine(); if (accentButton(modes[k], A.engraveMode == k, {w3, 0})) A.engraveMode = k; }
    if (A.engraveMode == 1) {
        prop("Text", "Capitals, figures and the usual signs (. , - / : # and more). Small letters are written as capitals.");
        inputText("##etext", A.engraveText);
        pNumU("Letter height", A.engraveSize, 0.5, Q::Length, -1, "How tall the capitals are.");
        pNum("Turn", A.engraveTurn, 5, "%.0f\xc2\xb0", "Turns the text on the surface. 0 reads along the gun.");
        A.engraveSize = std::clamp(A.engraveSize, 0.5, 100.0);
    }
    if (A.engraveMode != 2) {
        pNumU("Line width", A.engraveWidth, 0.1, Q::Length, -1, "How wide the pen cuts.");
        A.engraveWidth = std::clamp(A.engraveWidth, 0.1, 5.0);
        prop("Colour", "What shows in the cut: bare metal, a dark fill, gold inlay or a white fill.");
        static const char* names[] = {"Bright (bare metal)", "Dark", "Gold inlay", "White fill", "Red fill"};
        static const char* hexes[] = {"#dcd8d0", "#161616", "#d4a640", "#f2f2ee", "#b83228"};
        int ci = -1; for (int k = 0; k < 5; k++) if (A.engraveColor == hexes[k]) ci = k;
        if (ImGui::BeginCombo("##ecol", ci >= 0 ? names[ci] : "Your own")) {
            for (int k = 0; k < 5; k++) if (ImGui::Selectable(names[k], ci == k)) A.engraveColor = hexes[k];
            ImGui::EndCombo();
        }
        Color cc = hexColor(A.engraveColor);
        float col[3] = {cc.r / 255.f, cc.g / 255.f, cc.b / 255.f};
        ImGui::SameLine();
        if (ImGui::ColorEdit3("##ecolpick", col, ImGuiColorEditFlags_NoInputs)) A.engraveColor = TextFormat("#%02x%02x%02x", (int)(col[0] * 255), (int)(col[1] * 255), (int)(col[2] * 255));
    }
    editedAny = wasEdited;  // tool settings aren't part of the design
    if (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size()) {
        auto& b = A.design.bodies[A.sel.idx];
        ImGui::TextDisabled("%d engraving%s on this part.", (int)b.marks.size(), b.marks.size() == 1 ? "" : "s");
        if (!b.marks.empty() && dangerButton("Clear this part's engravings", {-1, 0})) { b.marks.clear(); commit(); }
    }
    ImGui::Spacing();
}
// A Picatinny rail: which way it faces, how long, its slots
static void railSection(BodyDef& b) {
    if (!b.rail.on || !section("Picatinny rail", true)) return;
    double L = std::hypot(b.rail.b.x - b.rail.a.x, b.rail.b.y - b.rail.a.y);
    ImGui::TextDisabled("%s long, %d slots (MIL-STD-1913: 10.01 mm apart, 21.2 mm wide).", fmtQ(Q::Length, L).c_str(), (int)mounts::railSlots(b).size());
    static const char* faces[] = {"On top", "Underneath", "On the right side", "On the left side"};
    int f = b.rail.face;
    prop("Faces", "Which way attachments on it point. On a side, the rail lies flat against the part it's welded to.");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##railface", &f, faces, 4) && f != b.rail.face) {
        int bi = A.design.bodyIndex(b.rail.base);
        double z = bi >= 0 ? mounts::sideRailZ(A.design.bodies[bi], f) : b.z;
        if ((f <= 1) != (b.rail.face <= 1) && bi < 0) z = b.z;
        b.rail.face = f; b.z = z;
        mounts::regenRail(b);
        editedAny = true;
    }
    double len = L;
    if (pNumU("Length##rail", len, 1, Q::Length, -1, "Longer or shorter, from where it starts: the slots are cut again to match.") && len >= 10) {
        b.rail.b = {b.rail.a.x + (b.rail.b.x - b.rail.a.x) * len / L, b.rail.a.y + (b.rail.b.y - b.rail.a.y) * len / L};
        mounts::regenRail(b);
    }
    muted("Attachments clamp on anywhere along it, slot to slot. It's a real part: it has weight, and it's welded to what it sits on.");
}

// Attachment points on this part: their kinds, which way attachments face on them
static void mountsSection(BodyDef& b) {
    if (b.mounts.empty() && A.tool != Tool::Mount) return;
    if (!section(b.mounts.empty() ? "Attachment points" : TextFormat("Attachment points (%d)###mounts", (int)b.mounts.size()), true)) return;
    if (b.mounts.empty()) { muted("None yet: click this part's surface with the Mount tool."); return; }
    if (A.selMount >= (int)b.mounts.size()) A.selMount = -1;
    for (int k = 0; k < (int)b.mounts.size(); k++)
        if (ImGui::Selectable(TextFormat("%s##mt%d", b.mounts[k].type.c_str(), k), A.selMount == k)) A.selMount = A.selMount == k ? -1 : k;
    if (A.selMount < 0) { muted("Click one to change it."); return; }
    MountDef& m = b.mounts[A.selMount];
    prop("Kind", "Attachments fit points of the same kind. Pick a common one or type your own\n(a maker's own mount, say): the attachment designer will offer the same names.");
    char tb[64]; std::snprintf(tb, sizeof tb, "%s", m.type.c_str());
    ImGui::SetNextItemWidth(-28 * S());
    if (ImGui::InputText("##mtype", tb, sizeof tb)) { m.type = tb; A.mountType = m.type; }
    if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
    ImGui::SameLine(0, 2 * S());
    if (ImGui::BeginCombo("##mtypes", nullptr, ImGuiComboFlags_NoPreview)) {
        for (auto& t : mounts::commonMountTypes()) if (ImGui::Selectable(t.c_str(), m.type == t)) { m.type = t; A.mountType = t; editedAny = true; }
        ImGui::EndCombo();
    }
    pNum("Turn##mount", m.spin, 5, "%.0f\xc2\xb0", "Which way an attachment on it faces, turned about the point.");
    pNumU("X##mount", m.at.x, 0.5, Q::Length, -1, "Along the gun: + toward the muzzle.", SIGNED);
    double y = -m.at.y;
    if (pNumU("Y##mount", y, 0.5, Q::Length, -1, "+ up.", SIGNED)) m.at.y = -y;
    pNumU("Z##mount", m.atZ, 0.5, Q::Length, -1, "Across, from the part's middle: + toward the gun's right side.", SIGNED);
    if (dangerButton("Remove##mount", {-1, 0})) { b.mounts.erase(b.mounts.begin() + A.selMount); A.selMount = -1; editedAny = true; }
}

// The part's carves: pick one to change it.
static void carvesSection(BodyDef& b) {
    if (!section(b.carves.empty() ? "Carves" : TextFormat("Carves (%d)###carves", (int)b.carves.size()), !b.carves.empty() || A.tool == Tool::Carve)) return;
    if (b.carves.empty()) { muted("None yet. Pick the Carve tool, then click this part's surface to cut a hole, slot or pocket."); return; }
    if (A.selCarve >= (int)b.carves.size()) A.selCarve = -1;
    if (b.rollOn || b.magazine) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(colWarn(), "%s", b.rollOn ? "This part turns on the bore: for now its cuts only change how it looks. The physics uses the plain outline."
                                                     : "On magazines, cuts only change the look for now. The physics uses the plain outline.");
        ImGui::PopTextWrapPos();
    }
    if (accentButton(A.cutPick ? ICON_FA_CROSSHAIRS "  Picking cuts in the view" : ICON_FA_CROSSHAIRS "  Pick cuts in the view", A.cutPick, {-1, 0})) A.cutPick = !A.cutPick;
    if (tipHover()) ImGui::SetTooltip("On: this part's cuts are all shown, and a click on one in the view picks it\n(on its dot or anywhere inside its outline); drag it to move it. Clicks elsewhere work as usual.");
    for (int k = 0; k < (int)b.carves.size(); k++) {
        auto& c = b.carves[k];
        std::string t = std::string(shapeName(c.shape)) + " " + fmtQ(Q::Length, c.w);
        if (c.shape != "circle" || c.h != c.w) t += " x " + fmtQ(Q::Length, c.h);
        t += c.depth > 0 ? ", " + fmtQ(Q::Length, c.depth) + " deep" : ", right through";
        if (!c.name.empty()) t = c.name + "   (" + t + ")";
        if (ImGui::Selectable((t + "##cv" + std::to_string(k)).c_str(), A.selCarve == k)) A.selCarve = A.selCarve == k ? -1 : k;
        if (A.selCarve == k && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        if (k == 0) tutAnchor("carves:list");
    }
    if (A.selCarve < 0) { muted("Click one to change it, or pick it in the view. Del removes the selected one."); return; }
    CarveDef& c = b.carves[A.selCarve];
    ImGui::Spacing();
    {   // its name
        prop("Name", "Your name for this cut: \"Front serrations\", \"Ejection port\". Shown in the list and in the view.");
        char nb[64]; std::snprintf(nb, sizeof nb, "%s", c.name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##cutname", ("Cut " + std::to_string(A.selCarve + 1)).c_str(), nb, sizeof nb)) c.name = nb;
        if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
    }
    ImGui::PushID("cut");
    bool shapeHit = shapeButtons(c.shape);
    ImGui::PopID();
    if (shapeHit) { if (c.shape == "circle") c.h = c.w; editedAny = true; }
    if (c.shape == "circle" && c.h == c.w) { if (pNumU("Diameter##cut", c.w, 0.5, Q::Length)) c.h = c.w; }
    else { pNumU("Width##cut", c.w, 0.5, Q::Length); pNumU("Height##cut", c.h, 0.5, Q::Length); }
    bool through = c.depth <= 0;
    if (pCheck("Right through##cut", through, "Cut all the way through the part.")) c.depth = through ? 0 : 4;
    tutAnchor("carve:through");
    if (!through) pNumU("Depth##cut", c.depth, 0.5, Q::Length, -1, "How deep it goes, straight in from where it starts.");
    // aim: which way it goes in. 0, 0 is straight into the near side.
    double L = std::sqrt(c.dx * c.dx + c.dy * c.dy + c.dz * c.dz);
    if (L < 1e-9) { c.dz = -1; L = 1; }
    double tilt = std::asin(std::clamp(c.dy / L, -1.0, 1.0)) * 180 / PI, turn = std::atan2(c.dx / L, -c.dz / L) * 180 / PI;
    if (std::fabs(std::fabs(tilt) - 90) < 1e-6) turn = 0;
    bool aim = pNum("Turn##cut", turn, 5, "%.1f\xc2\xb0", "Which way it points, seen from above: 0 cuts into the near side, 180 into the far side,\n"
                                                   "90 from the back toward the muzzle, -90 from the muzzle end backward.");
    tutAnchor("carve:aim");
    aim |= pNum("Tilt##cut", tilt, 5, "%.1f\xc2\xb0", "Up or down: 90 cuts straight down from the top, -90 straight up from below.");
    if (aim) {
        tilt = std::clamp(tilt, -90.0, 90.0);
        double tr = turn * PI / 180, pr = tilt * PI / 180;
        c.dx = std::cos(pr) * std::sin(tr); c.dy = std::sin(pr); c.dz = -std::cos(pr) * std::cos(tr);
    }
    pNum("Spin##cut", c.spin, 5, "%.1f\xc2\xb0", "Turns the shape about the way it goes in.");
    if (section("Position (X, Y, Z)", true)) {  // where the cut starts, on the face it goes in from
        pNumU("X##cut", c.at.x, 0.5, Q::Length, -1, "Along the gun: + toward the muzzle.", SIGNED);
        double y = -c.at.y;   // shown with + up
        if (pNumU("Y##cut", y, 0.5, Q::Length, -1, "Up and down: + is up.", SIGNED)) c.at.y = -y;
        pNumU("Z##cut", c.atZ, 0.5, Q::Length, -1, "Across the gun, from the part's middle: + is the gun's right side (toward you in the side view).", SIGNED);
    }
    c.w = std::max(0.2, c.w); c.h = std::max(0.2, c.h); c.depth = std::max(0.0, c.depth);
    // copies: as many as you like, each a set step on from the last (0 = right on top of it, to move by hand)
    if (section("Duplicate and mirror", true)) {
        prop("Copies", "How many copies Duplicate makes.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##dupn", &A.dupCount); A.dupCount = std::clamp(A.dupCount, 1, 100);
        double sy = -A.dupStep[1];
        pNumU("Step X##dup", A.dupStep[0], 0.5, Q::Length, -1, "Each copy this far on from the last: + toward the muzzle.", SIGNED);
        if (pNumU("Step Y##dup", sy, 0.5, Q::Length, -1, "+ up.", SIGNED)) A.dupStep[1] = -sy;
        pNumU("Step Z##dup", A.dupStep[2], 0.5, Q::Length, -1, "+ toward the gun's right side.", SIGNED);
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button(A.dupCount > 1 ? TextFormat("Duplicate x%d##cut", A.dupCount) : "Duplicate##cut", {w2, 0})) {
            CarveDef base = c;
            for (int n = 1; n <= A.dupCount; n++) {
                CarveDef d = base; d.at.x += A.dupStep[0] * n; d.at.y += A.dupStep[1] * n; d.atZ += A.dupStep[2] * n;
                if (!d.name.empty()) d.name += " " + std::to_string(n + 1);
                b.carves.push_back(d);
            }
            A.selCarve = (int)b.carves.size() - 1; editedAny = true;
            return;
        }
        if (tipHover()) ImGui::SetTooltip("Copies of this cut, each Step on from the last.\nWith the step at 0 the copy sits exactly on the original: drag it where you want it.");
        ImGui::SameLine();
        if (dangerButton("Remove##cut", {w2, 0})) { b.carves.erase(b.carves.begin() + A.selCarve); A.selCarve = -1; editedAny = true; return; }
        // mirror: a copy the other way round, across the part's middle
        float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        double lo[2] = {1e9, 1e9}, hi[2] = {-1e9, -1e9};
        for (auto& p : b.pts) { lo[0] = std::min(lo[0], p.x); hi[0] = std::max(hi[0], p.x); lo[1] = std::min(lo[1], p.y); hi[1] = std::max(hi[1], p.y); }
        static const char* lab[] = {"Mirror X##cut", "Mirror Y##cut", "Mirror Z##cut"};
        static const char* tip[] = {"A copy mirrored front to back, across the middle of the part.",
                                    "A copy mirrored top to bottom, across the middle of the part.",
                                    "A copy mirrored across the gun, onto the other side of the part (left to right)."};
        for (int a = 0; a < 3; a++) {
            if (a) ImGui::SameLine();
            if (ImGui::Button(lab[a], {w3, 0}) && !b.pts.empty()) {
                CarveDef m = carve::mirrored(c, a, a == 2 ? 0 : (lo[a] + hi[a]) / 2);
                if (!m.name.empty()) m.name += " (mirrored)";
                b.carves.push_back(m); A.selCarve = (int)b.carves.size() - 1; editedAny = true;
                return;
            }
            if (tipHover()) ImGui::SetTooltip("%s", tip[a]);
        }
    }
}

// ---------------------------------------------------------------- inspect: a part

// ---------------------------------------------------------------- where it's held (posed on the bench)
static void heldSummary() {
    std::string h = heldWith(A.design);
    if (h.empty()) muted("Not posed yet: on the Test bench, press Pose to place the hands and shoulder.");
    else muted("Held with %s%s. Change it with Pose on the Test bench.", h.c_str(), A.design.pose.butt.on ? ", against the shoulder" : "");
}
// Merge, keep the overlap, or cut away: this part's outline combined with another's. The other part goes unless kept.
static bool combineSection(BodyDef& b) {
    Design& D = A.design;
    static std::string other;
    static bool keepOther = false;
    if (!ImGui::TreeNodeEx("Combine with another part", ImGuiTreeNodeFlags_SpanAvailWidth)) return false;
    if (tipHover()) ImGui::SetTooltip("Merge two parts into one outline, keep only where they overlap, or cut one out of the other.");
    int oi = D.bodyIndex(other);
    if (oi < 0 || D.bodies[oi].id == b.id) { other.clear(); oi = -1; }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##combwith", oi >= 0 ? D.bodies[oi].name.c_str() : "Pick the other part...")) {
        for (auto& o : D.bodies) {
            if (o.id == b.id) continue;
            ImGui::PushID(o.id.c_str());
            if (ImGui::Selectable(o.name.c_str(), o.id == other)) other = o.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Keep the other part", &keepOther);
    if (tipHover()) ImGui::SetTooltip("Off: the other part is used up (removed, with its joints). On: it stays as it is.");
    bool done = false;
    if (oi >= 0) {
        const BodyDef& o = D.bodies[oi];
        float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        int op = -1;
        if (ImGui::Button("Merge", {w3, 0})) op = 0;
        if (tipHover()) ImGui::SetTooltip("One outline covering both. It takes the depth of both together.");
        ImGui::SameLine(); if (ImGui::Button("Keep overlap", {w3, 0})) op = 1;
        if (tipHover()) ImGui::SetTooltip("Only where the two overlap is left, in the depth they share.");
        ImGui::SameLine(); if (ImGui::Button("Cut away", {w3, 0})) op = 2;
        if (tipHover()) ImGui::SetTooltip("The other part's outline is cut out of this one, through its whole depth.\n(For a cut only partway in, use the Carve tool.)");
        if (op >= 0) {
            std::vector<V2> out; std::vector<double> rad; std::string why;
            double n0 = b.z - b.thick / 2, n1 = b.z + b.thick / 2, m0 = o.z - o.thick / 2, m1 = o.z + o.thick / 2;
            if (b.magazine && b.gen.on) toast("This magazine's shape comes from its settings: turn off its generated shape first.");
            else if (op == 1 && std::min(n1, m1) - std::max(n0, m0) < 0.05) toast("They don't overlap in depth: nothing would be left.");
            else if (!combineOutlines(b, o, op, out, &why, &rad)) toast("Can't: " + why + ".");
            else {
                b.pts = out; b.rad = rad; b.bulge.assign(out.size(), 0.0);
                if (op == 0) { b.z = (std::min(n0, m0) + std::max(n1, m1)) / 2; b.thick = std::max(n1, m1) - std::min(n0, m0); }
                if (op == 1) { double z0 = std::max(n0, m0), z1 = std::min(n1, m1); b.z = (z0 + z1) / 2; b.thick = z1 - z0; }
                std::string me = b.id, oid = o.id, oname = o.name;
                if (!keepOther) {
                    D.bodies.erase(D.bodies.begin() + oi);
                    D.joints.erase(std::remove_if(D.joints.begin(), D.joints.end(), [&](const JointDef& j) { return j.a == oid || j.b == oid || j.rel == oid; }), D.joints.end());
                    for (HandHold* h : {&D.pose.firing, &D.pose.support, &D.pose.butt}) if (h->part == oid) h->part = me;
                }
                A.sel = {1, D.bodyIndex(me), -1}; A.selVs.clear(); A.selCarve = -1;
                other.clear();
                commit();
                const char* verb = op == 0 ? "Merged with " : op == 1 ? "Kept the overlap with " : "Cut away ";
                bool depths = std::fabs(n0 - m0) > 0.05 || std::fabs(n1 - m1) > 0.05;
                std::string note = op == 0 && depths ? " They were at different depths: a part has one depth, so it now fills both all over (use Weld to keep them as they were)." : "";
                toast(std::string(verb) + oname + (why.empty() ? "." : ": " + why + ".") + note + " Ctrl+Z undoes it.");
                done = true;
            }
        }
        if (!done) muted("The result is one outline. Corners keep their rounding where they survive; curved edges come back as short straight pieces.");
    }
    ImGui::TreePop();
    return done;
}

static void inspectPart(BodyDef& b) {
    auto& D = A.design;
    // colour swatch and name on one line
    Color c = hexColor(b.color);
    float col[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f};
    if (ImGui::ColorEdit3("##col", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) { b.color = colorHex(col); editedAny = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s", b.name.c_str());
        if (ImGui::InputTextWithHint("##pname", "Name this part", buf, sizeof buf)) { b.name = buf; editedAny = true; }
        if (tipHover()) ImGui::SetTooltip("The part's name: shown when you point at it, in the Outline, and in joint and firing settings.");
    }

    {   // welded: one solid piece with other parts
        auto piece = weldedWith(D, A.sel.idx);
        if (piece.size() > 1) {
            std::string names;
            for (int k : piece) if (k != A.sel.idx) names += (names.empty() ? "" : ", ") + D.bodies[k].name;
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(colAccent(), ICON_FA_LINK "  One piece with %s", names.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Unweld this part", {-1, 0})) { unweld(D, b.id); editedAny = true; }
            if (tipHover()) ImGui::SetTooltip("Makes this part separate again. The rest stay welded together.");
        }
    }
    if (section("Part", true)) {
        pCheck("Fixed", b.fixed, "Fixed parts are the frame: they don't move.");
        tutAnchor("part:fixed");
        bool solid = b.layer != "none";
        bool solidHit = pCheck("Solid", solid, "Solid parts push on each other wherever they overlap, in the side view and in depth.\nParts that aren't solid (grip panels, covers) pass through everything.");
        tutAnchor("part:solid");
        if (solidHit)
            b.layer = solid ? "ABC" : "none";
        double oldThick = b.thick;
        if (b.round) {
            carve::syncRound(b);
            prop("Thickness", "A round part is as thick as it is wide: change its outline instead.");
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s across (round)", fmtQ(Q::Length, b.thick).c_str());
            if (oldThick != b.thick) carve::rethick(b, oldThick);
        } else if (pNumU("Thickness", b.thick, 1, Q::Length, -1, nullptr, 0.5)) carve::rethick(b, oldThick);
        tutAnchor("part:thickness");
        {
            bool wasRound = b.round;
            if (pCheck("Round", b.round, "Turns the part on a lathe: its outline is spun around its long middle line, so a rectangle\n"
                                         "becomes a round bar or barrel, and steps in the outline become shoulders. For barrels, bolts,\n"
                                         "firing pins, rods and tubes. Its thickness becomes its diameter.") && b.round != wasRound) {
                double ot = b.thick;
                if (b.round) carve::syncRound(b);
                carve::rethick(b, ot);
            }
            tutAnchor("part:round");
        }
        if (b.round) {  // turned down from bar stock: how many flats, turned which way, and how far round
            prop("Axis", "The line it turns about, through the middle of the outline.\n"
                         "For a lug: draw it right across the bolt, centred on the bore, then set Sweep.");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##raxis", &b.roundAxis, "Level (front to back)\0Upright\0Along its length\0")) editedAny = true;
            int sl = b.roundSides >= 3 ? b.roundSides : 33;
            prop("Sides", "4 is the square bar as drawn, 6 a hex, 8 an octagon, and all the way up is a full circle.\n"
                          "The flats touch the outline you drew.");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##sides", &sl, 3, 33, sl >= 33 ? "Circle" : "%d sides")) { b.roundSides = sl >= 33 ? 0 : sl; editedAny = true; }
            typeable();
            pNum("Sweep", b.roundSweep, 5, "%.0f\xc2\xb0", "How far round it goes. 360 is all the way round. Less makes a wedge, like a locking lug,\n"
                                                         "pointing where Turned to says.");
            if (b.roundSides >= 3 || b.roundSweep < 360)
                pNum("Turned to", b.roundTurn, 5, "%.1f\xc2\xb0", "Turns the shape about its axis: 0 points up, 90 toward you, 180 down, -90 away.\n"
                                                              "A square turned 45 is a diamond.");
            b.roundSweep = std::clamp(b.roundSweep, 1.0, 360.0);
            if (b.rollOn) muted("This part turns on the bore: on the bench it still collides as a plain flat part for now.");
        }
        pNumU("Z (offset)", b.z, 0.5, Q::Length, -1, "Where the part sits across the gun: 0 is on the bore, + is the gun's right side (toward you in the side view).\nParts only touch where they overlap across the gun too.\nWhile dragging a part, Z moves it this way.", SIGNED);
        tutAnchor("part:depth");
        {   // what it's made of: its weight, how it slides on other parts, (career) its cost
            const MaterialInfo& mt = materialOf(b.material);
            prop("Material", "What the part is made of. It sets how heavy the part is (and so how the gun kicks and how fast its parts move),\nhow it slides against other parts, and what it costs to make in career mode.");
            if (ImGui::BeginCombo("##material", mt.name)) {
                for (auto& m : materials()) {
                    if (ImGui::Selectable(m.name, &m == &mt)) { b.material = std::string(m.id) == "steel" ? "" : m.id; editedAny = true; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%.2f g/cm3, %s against steel.", m.tip, m.density,
                                                                m.friction > 1.5 ? "grips" : m.friction > 1.05 ? "rubs a little harder" : m.friction < 0.95 ? "slides easily" : "slides as steel does");
                }
                ImGui::EndCombo();
            }
            if (ImGui::SmallButton(TextFormat("Its usual colour##mc"))) { b.color = mt.color; b.texture.clear(); editedAny = true; }
            if (tipHover()) ImGui::SetTooltip("Paint the part as %s usually looks (takes off a picture on it).", mt.name);
            auto piece = weldedWith(A.design, A.sel.idx);
            if (piece.size() > 1) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Whole piece##mw")) {   // every part welded to it: one material (a slide and all its pieces)
                    for (int k : piece) A.design.bodies[k].material = b.material;
                    editedAny = true;
                    toast(std::string("The whole piece is ") + mt.name + " now (" + std::to_string(piece.size()) + " parts).");
                }
                if (tipHover()) ImGui::SetTooltip("Make every part welded to this one %s too (%d parts): a slide is often several.", mt.name, (int)piece.size());
            }
        }
        if (!b.fixed) {
            double autoMass;
            {   // (carved volume takes a moment to work out: only when the shape changes)
                static std::string key; static double vol = 0;
                std::string k = std::to_string(b.thick) + b.id;
                for (auto& p : b.pts) k += std::to_string(p.x) + "," + std::to_string(p.y) + ";";
                for (double r : b.rad) k += std::to_string(r) + "r";
                for (double q : b.bulge) k += std::to_string(q) + "b";
                for (auto& c : b.carves) k += c.shape + std::to_string(c.at.x) + std::to_string(c.at.y) + std::to_string(c.atZ) + std::to_string(c.dx) + std::to_string(c.dy) + std::to_string(c.dz) + std::to_string(c.spin) + std::to_string(c.w) + std::to_string(c.h) + std::to_string(c.depth);
                if (k != key) { key = k; vol = carve::volume(b); }
                autoMass = vol * materialOf(b.material).density * 1e-3;
            }
            pNumU("Mass", b.mass, 10, Q::PartMass, -1, "0 works it out from the shape and its material.");
            tutAnchor("part:mass");
            if (b.mass <= 0) muted("Worked out from the shape: %s.", fmtQ(Q::PartMass, autoMass).c_str());
        }
        {   // where it is: its middle, as X and Y (+ up). Typing moves it, with its welded piece and what's attached
            V2 c = centroidOf(b);
            double px = c.x, py = -c.y;
            bool mx = pNumU("Position X", px, 0.5, Q::Length, lenDec(), "Where its middle is along the gun (+ toward the muzzle). Moves its welded piece with it.", SIGNED);
            bool my = pNumU("Position Y", py, 0.5, Q::Length, lenDec(), "Where its middle is, up and down (+ up).", SIGNED);
            if (mx || my) {
                px = std::clamp(px, -5000.0, 5000.0); py = std::clamp(py, -5000.0, 5000.0);
                double dx = px - c.x, dy = -py - c.y;
                for (int k : weldedWith(D, D.bodyIndex(b.id))) moveBody(k, dx, dy);
            }
        }
        {   // turn it: in the side view (Z, the whole welded piece about this part's middle), or lean it out of it (X, Y)
            prop("Turn (Z)", "Turns it in the side view, about its middle (a welded piece turns together).\nWhat's attached to it (pins, slots, hands) turns with it.");
            float w4 = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
            double turn = 0;
            if (ImGui::Button("-90##rz", {w4, 0})) turn = -90;
            ImGui::SameLine(); if (ImGui::Button("-15##rz", {w4, 0})) turn = -15;
            ImGui::SameLine(); if (ImGui::Button("+15##rz", {w4, 0})) turn = 15;
            ImGui::SameLine(); if (ImGui::Button("+90##rz", {w4, 0})) turn = 90;
            static double typed = 5;
            pNum("By##rz", typed, 0.5, "%.1f\xc2\xb0");
            ImGui::SameLine();
            if (ImGui::Button("Turn##rz")) turn = typed;
            if (turn != 0) {
                int me = D.bodyIndex(b.id);
                V2 c = centroidOf(b);
                for (int k : weldedWith(D, me)) rotateBody(k, c, -turn);   // (+ is anticlockwise as you look at it: y is down)
                editedAny = true;
            }
            bool was = carve::tilted(b);
            pNum("Tilt X", b.tiltX, 1, "%.1f\xc2\xb0", "Leans it about the gun's length (X), out of the side view: + tips its top toward you.");
            pNum("Tilt Y", b.tiltY, 1, "%.1f\xc2\xb0", "Swings it about the upright (Y), out of the side view: + brings its front toward you.");
            b.tiltX = std::clamp(b.tiltX, -85.0, 85.0); b.tiltY = std::clamp(b.tiltY, -85.0, 85.0);
            if (carve::tilted(b) || was) muted("Tilted parts collide by their tilted shape, sliced through the depth. Their cuts show but don't collide yet. Corners are edited as drawn, before the tilt.");
        }
        {   // copy, and keep for other guns
            float w2c = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
            if (ImGui::Button("Copy (Ctrl+C)", {w2c, 0})) copySelection();
            if (tipHover()) ImGui::SetTooltip("Copies it, with its welded piece and the joints between them. Ctrl+V pastes it, here or in another gun.");
            ImGui::SameLine();
            if (ImGui::Button("Save to my parts", {w2c, 0})) ImGui::OpenPopup("##savepart");
            if (tipHover()) ImGui::SetTooltip("Keeps it (with its welded piece) in your parts library, to drop into any gun from Parts in the top bar.");
            if (ImGui::BeginPopup("##savepart")) {
                static char nm[64];
                if (ImGui::IsWindowAppearing()) { std::snprintf(nm, sizeof nm, "%s", b.name.c_str()); ImGui::SetKeyboardFocusHere(); }
                ImGui::SetNextItemWidth(220 * S());
                bool go = ImGui::InputText("##pname", nm, sizeof nm, ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if (ImGui::Button("Save") || go) {
                    toast(savePieceToLibrary(D.bodyIndex(b.id), nm) ? std::string("Saved ") + nm + " to your parts (your Parts folder)." : "Couldn't save it.");
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        int partner = mirrorPartner(D, D.bodyIndex(b.id));
        if (partner >= 0) {   // a live mirror
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(colAccent(), "%s", ICON_FA_LEFT_RIGHT);
            ImGui::SameLine();
            ImGui::TextWrapped("Mirrored with %s%s", D.bodies[partner].name.c_str(), b.mirrorKind == "line" ? "" : " (the other side)");
            if (tipHover()) ImGui::SetTooltip("Change either one and the other follows: shape, cuts, depth, thickness, tilt and look.\nHow each moves and what it's joined to stay its own.");
            float w2m = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
            if (ImGui::Button("Select the other", {w2m, 0})) { A.sel = {1, partner, -1}; A.selVs.clear(); A.selCarve = -1; return; }
            ImGui::SameLine();
            if (ImGui::Button("Unlink", {w2m, 0})) {
                D.bodies[partner].mirror.clear(); b.mirror.clear();
                commit(); toast("Unlinked: the two are separate parts now.");
                return;
            }
            if (tipHover()) ImGui::SetTooltip("Stops the mirroring, so each side can be shaped on its own.");
        } else if (tutAnchorAfter(ImGui::Button("Copy to the other side", {-1, 0}), "part:otherside")) {
            std::string tag = "m-" + uid();
            b.mirror = tag; b.mirrorKind = "depth";
            BodyDef cpy = mirrorImage(b);
            cpy.id = uid(); cpy.name += " (other side)"; cpy.magazine = false; cpy.weld.clear();
            D.bodies.push_back(cpy);
            A.sel = {1, (int)D.bodies.size() - 1, -1};
            commit();
            toast("Made the matching part on the other side. It's a live mirror: change either one and the other follows.");
            return;
        }
        if (partner < 0 && tipHover()) ImGui::SetTooltip("Makes the matching part on the far side of the gun (side walls, rails, grip panels),\nkept as a live mirror: change either one and the other follows.");
        if (combineSection(b)) return;   // parts went away: this frame's references are stale
    }

    fixShape(b);
    if (section("Shape", !b.gen.on)) {
        if (A.sel.v >= 0 && A.sel.v < (int)b.pts.size()) {
            int v = A.sel.v;
            pNumU("X", b.pts[v].x, 0.5, Q::Length, lenDec(), "Along the gun: + toward the muzzle.", SIGNED);
            {   double y = -b.pts[v].y;   // shown with + up
                if (pNumU("Y", y, 0.5, Q::Length, lenDec(), "Up and down: + is up.", SIGNED)) b.pts[v].y = -y; }
            pNumU("Corner radius", b.rad[v], 0.5, Q::Length);
            pNumU("Edge curve", b.bulge[v], 0.5, Q::Length, -1, "Bows the edge to the next corner. Positive bows outward.");
            b.rad[v] = std::max(0.0, b.rad[v]);
        } else muted("Click a corner (a square) to edit it. Shift-click or drag a box for several.");
        // edges whose two corners are both selected (clicking an edge's diamond selects it)
        std::vector<int> edges;
        {
            int n = (int)b.pts.size();
            auto picked = [&](int v) { return std::find(A.selVs.begin(), A.selVs.end(), v) != A.selVs.end(); };
            for (int e = 0; e < n; e++) if (picked(e) && picked((e + 1) % n) && (n > 2 || e == 0)) edges.push_back(e);
        }
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        ImGui::BeginDisabled(edges.empty());
        bool splitHit = ImGui::Button(edges.size() > 1 ? "Split edges" : "Split edge", {w2, 0});
        tutAnchor("shape:split");
        if (splitHit && splitReady()) {
            std::vector<int> made;
            for (int k = (int)edges.size() - 1; k >= 0; k--) {
                int nv = splitEdge(b, edges[k]);
                if (nv < 0) { cornerLimitToast(); break; }
                for (int& m : made) if (m >= nv) m++;
                made.push_back(nv);
            }
            A.selVs = made; A.sel.v = made.size() == 1 ? made[0] : -1; editedAny = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && CFG.tooltips)
            ImGui::SetTooltip("%s", edges.empty() ? "Click an edge's diamond to select it, then split it in half.\nOr right-click or double-click the diamond."
                                            : "Adds a corner halfway along the selected edge (right-click or double-click a diamond does the same).");
        ImGui::SameLine(); if (ImGui::Button("Split all edges", {w2, 0}) && splitReady()) {
            if (subdivideBody(b) == 0) cornerLimitToast();
            else { A.selVs.clear(); A.sel.v = -1; editedAny = true; }
        }
        if (tipHover()) ImGui::SetTooltip("Adds a corner halfway along every edge, for finer shaping.");
        {
            int nc = (int)b.pts.size();
            if (nc > MAX_CORNERS) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(colWarn(), "%d corners: more than the %d a part can have. So many slow the game down badly.", nc, MAX_CORNERS);
                ImGui::PopTextWrapPos();
                if (ImGui::Button(TextFormat("Simplify to %d corners", MAX_CORNERS), {-1, 0})) { simplifyBody(b); A.selVs.clear(); A.sel.v = -1; editedAny = true; }
                if (tipHover()) ImGui::SetTooltip("Removes the corners that change the shape least.");
            } else if (nc == MAX_CORNERS) ImGui::TextColored(colWarn(), "%d of %d corners: the most a part can have.", nc, MAX_CORNERS);
            else muted("%d of %d corners.", nc, MAX_CORNERS);
        }
        // thickness corner by corner: the picked corners' own thickness, and where their middle sits across the gun
        if (!A.selVs.empty() && !b.round && !carve::tilted(b)) {
            int n = (int)b.pts.size();
            int c0 = -1;
            for (int v : A.selVs) if (v >= 0 && v < n) { c0 = v; break; }
            if (c0 >= 0) {
                bool shaped = depthShaped(b);
                double half = std::max(0.5, b.thick) / 2;
                double zn = shaped ? b.depthZn[c0] : 0, zf = shaped ? b.depthZf[c0] : 0;
                double th = (b.z + half + zn) - (b.z - half + zf), mid = b.z + (zn + zf) / 2;
                double th0 = th, mid0 = mid;
                heading(A.selVs.size() > 1 ? TextFormat("The %d picked corners", (int)A.selVs.size()) : "This corner");
                pNumU("Thickness here", th, 0.5, Q::Length, lenDec(), "How thick the part is at this corner. Between corners it tapers evenly, so different thicknesses\n"
                      "at different corners make wedges, tapers and odd shapes. The physics uses the real shape.");
                pNumU("Across##cmid", mid, 0.5, Q::Length, lenDec(), "Where the middle of the part sits across the gun at this corner (+ is the gun's right side).\n"
                      "Move it to bend or lean the part in depth.", SIGNED);
                th = std::max(0.1, th);
                if (th != th0 || mid != mid0) {
                    if (!shaped) { b.depthOff.assign(n, V2{0, 0}); b.depthZn.assign(n, 0.0); b.depthZf.assign(n, 0.0); b.depthRef = b.pts; }
                    for (int v : A.selVs) {
                        if (v < 0 || v >= n) continue;
                        double vzn = b.depthZn[v], vzf = b.depthZf[v];
                        double vth = (half * 2 + vzn - vzf), vmid = b.z + (vzn + vzf) / 2;
                        if (th != th0) vth = th;            // every picked corner gets the thickness typed
                        if (mid != mid0) vmid += mid - mid0;   // and moves across by the same amount
                        b.depthZn[v] = vmid + vth / 2 - (b.z + half);
                        b.depthZf[v] = vmid - vth / 2 - (b.z - half);
                    }
                    editedAny = true;
                }
            }
        } else if (A.selVs.empty() && !b.round && !carve::tilted(b) && b.pts.size() >= 3)
            muted("Pick a corner (or several) to give it its own thickness.");
        if (depthShaped(b)) {   // corners placed on each face (the Faces tool)
            muted("Shaped in depth: its front and back faces have their own corners. Edges are straight and corners sharp (Chamfer softens them).");
            auto& cp = A.cornerDrag;
            if (cp.body == D.bodyIndex(b.id) && cp.idx >= 0 && cp.idx < (int)b.pts.size()) {   // the corner last dragged
                std::vector<P3d> N, F; depthRings(b, N, F);
                const P3d& p = cp.far ? F[cp.idx] : N[cp.idx];
                heading(cp.far ? "Back corner" : "Front corner");
                double x = p.x, y = -p.y, z = p.z, x0 = x, y0 = y, z0 = z;
                pNumU("X##dc", x, 0.5, Q::Length, lenDec(), "Along the gun: + toward the muzzle.", SIGNED);
                pNumU("Y##dc", y, 0.5, Q::Length, lenDec(), "Up and down: + is up.", SIGNED);
                pNumU("Z##dc", z, 0.5, Q::Length, lenDec(), "Across the gun: + is the gun's right side.", SIGNED);
                if (x != x0 || y != y0 || z != z0) {
                    int i = cp.idx;
                    double base = cp.far ? b.z - std::max(0.5, b.thick) / 2 : b.z + std::max(0.5, b.thick) / 2;
                    if (cp.far) { b.depthOff[i] = {x - b.pts[i].x, -y - b.pts[i].y}; b.depthZf[i] = z - base; }
                    else {
                        V2 farP{b.pts[i].x + b.depthOff[i].x, b.pts[i].y + b.depthOff[i].y};
                        b.pts[i] = {x, -y}; b.depthOff[i] = {farP.x - x, farP.y + y}; b.depthZn[i] = z - base; b.depthRef = b.pts;
                    }
                    editedAny = true;
                }
            }
            if (ImGui::Button("Straight through again", {-1, 0})) { b.depthOff.clear(); b.depthZn.clear(); b.depthZf.clear(); b.depthRef.clear(); editedAny = true; }
            if (tipHover()) ImGui::SetTooltip("Puts the back face's corners right behind the front face's again: one outline, straight through.");
        } else if (!b.round && !carve::tilted(b)) muted("Shape it in depth with the Faces tool: drag a corner on the front or back face on its own.");
        if (ImGui::Button("Flip H", {w2, 0})) { flipBody(b, true); editedAny = true; }
        ImGui::SameLine(); if (ImGui::Button("Flip V", {w2, 0})) { flipBody(b, false); editedAny = true; }
        static double allR = 2;
        prop("Round corners");
        float bw = 64 * S();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2 * bw - 2 * ImGui::GetStyle().ItemSpacing.x);
        {   // the amount alone changes nothing, so it isn't an edit
            double d = toDisp(Q::Length, allR), st = niceStep(Q::Length, 0.5);
            char fmt[24]; std::snprintf(fmt, sizeof fmt, "%%.%df %s", decimalsOf(Q::Length), unitOf(Q::Length));
            (void)st;
            if (ImGui::InputDouble("##allr", &d, 0, 0, fmt)) allR = std::max(0.0, fromDisp(Q::Length, d));
        }
        ImGui::SameLine(); if (ImGui::Button("All", {bw, 0})) { for (auto& r : b.rad) r = allR; editedAny = true; }
        tutAnchor("shape:round");
        ImGui::SameLine(); if (ImGui::Button("Sharp", {bw, 0})) { for (auto& r : b.rad) r = 0; for (auto& q : b.bulge) q = 0; editedAny = true; }
        {   // flat-cut corners, by the same amount
            bool some = !A.selVs.empty();
            if (ImGui::Button(some ? "Chamfer the selected corners" : "Chamfer all corners", {-1, 0})) {
                int n = chamferCorners(b, allR, A.selVs);
                A.selVs.clear(); A.sel.v = -1;
                if (n) editedAny = true;
                else toast((int)b.pts.size() >= MAX_CORNERS ? TextFormat("No room: a part has at most %d corners.", MAX_CORNERS) : "Nothing to cut.");
            }
            if (tipHover()) ImGui::SetTooltip("Cuts the corners off flat, the amount above back along each edge: a bevel as seen from the side.\n"
                                              "Select corners first (click them, Shift for more) to cut only those.");
        }
        prop("Smoothness", "Rounds the edges where the sides meet the front and back faces, like a grip, a stock\n"
                           "or a dehorned slide. 100% makes the part round across its thickness.\n"
                           "It only changes how the part looks: the physics uses the outline.");
        float sm = (float)(b.smooth * 100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##smooth", &sm, 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) { b.smooth = sm / 100.0; editedAny = true; }
        typeable();
        tutAnchor("shape:smooth");
        if (tipHover()) ImGui::SetTooltip("Drag, or double-click to type a value.");
    }

    railSection(b);
    mountsSection(b);
    carvesSection(b);

    bool surfOpen = section("Surface", !b.texture.empty());
    tutAnchor("part:surface");
    if (surfOpen) {
        prop("Texture", "A picture laid over the part. Its colour tints the picture: white shows it as it is.");
        ImGui::SetNextItemWidth(-1);
        std::string wasTex = b.texture;
        if (texturePicker(b.texture)) {
            editedAny = true;
            for (auto& bt : builtinTextures())   // a gun finish: shown in its own colours, at its natural size
                if (bt.file == b.texture && bt.file != wasTex) { b.color = "#ffffff"; b.texScale = bt.scale; toast("The part's colour is set to white so the finish shows true. Pick a colour to tint it."); }
        }
        if (!b.texture.empty()) { pNumU("Picture size", b.texScale, 5, Q::Length, -1, "How big one copy of the picture is before it repeats."); b.texScale = std::clamp(b.texScale, 1.0, 5000.0); }
        muted("Looks only. Drag pictures onto the game window to add them.");
    }

    if (section("Motion", b.rollOn || b.damp > 0)) {
        pCheck("Turns on the bore", b.rollOn, "For rotating bolts: the part can also turn about the bore axis.");
        if (b.rollOn) {
            pNum("From", b.rollMin, 5, "%.0f\xc2\xb0");
            pNum("To", b.rollMax, 5, "%.0f\xc2\xb0");
            pNumU("Handle length", b.handle, 1, Q::Length, -1, "Draws a bolt handle you can grab and drag on the bench. 0 for none.");
        }
        pNum("Damping", b.damp, 0.5, "%.1f /s", "Friction-like slowing of this part on its own: buffers, heavy grease.");
        b.damp = std::max(0.0, b.damp);
    }

    if (b.barrel.on && section("Barrel", true)) {
        auto& R = b.barrel;
        prop("Chambered for", "The cartridge its chamber is cut for. Each barrel can take its own.");
        if (ImGui::BeginCombo("##bcart", R.cart.name.c_str())) {
            for (auto& c : cartridgePresets()) if (ImGui::Selectable(c.name.c_str(), R.cart.name == c.name)) { R.cart = c; editedAny = true; }
            if (!A.userCarts.empty()) ImGui::Separator();
            for (auto& c : A.userCarts) if (ImGui::Selectable((c.name + "##ub").c_str(), R.cart.name == c.name)) { R.cart = c; editedAny = true; }
            ImGui::EndCombo();
        }
        muted("Bore %s, chamber %s deep.", fmtQ(Q::Length, R.cart.bulletDia, 2).c_str(), fmtQ(Q::Length, R.cart.caseLen, 1).c_str());
        int bore = R.rifled ? 0 : 1;
        prop("Bore", "Rifled: spiral grooves spin the bullet, which keeps it flying point first.\nSmoothbore: for shot (pellets spread more from rifling) and for slugs made for it.\nWhat it does to accuracy comes with the firing range.");
        if (ImGui::Combo("##rifling", &bore, "Rifled\0Smoothbore\0")) { R.rifled = bore == 0; editedAny = true; }
        if (R.rifled) {
            double inches = R.twist / 25.4;
            if (pNum("Twist", inches, 0.5, "1 turn in %.1f\"", "How far the bullet travels down the bore for one turn of the rifling. Faster twists (fewer inches) suit longer, heavier bullets.")) R.twist = std::clamp(inches, 2.0, 80.0) * 25.4;
            V2 r0, m0;
            double rifled = barrelEnds(b, r0, m0) ? std::max(0.0, std::hypot(m0.x - r0.x, m0.y - r0.y) - R.cart.caseLen) : 0;
            double turns = rifled / std::max(1.0, R.twist);
            if (pNum("Turns", turns, 0.05, "%.2f along the bore", "How many times the rifling goes round from the chamber to the muzzle. Setting it sets the twist to match this barrel's length.") && turns > 0.01 && rifled > 1)
                R.twist = std::clamp(rifled / turns, 2.0 * 25.4, 80.0 * 25.4);
            double g = R.grooves;
            if (pNum("Grooves", g, 1, "%.0f")) R.grooves = std::clamp((int)std::lround(g), 2, 16);
        }
        {   // what it does to the bullet: spun fast enough, it flies point first
            double sg = spinStability(R.cart, R.twist, R.rifled);
            bool shot = R.cart.shot();
            if (shot) muted(R.rifled ? "Shot comes out of a rifled bore spinning, and spreads wider." : "Shot from a smooth bore: the usual pattern.");
            else if (R.cart.kind == "shotshell" && !R.rifled) muted("A slug from a smooth bore: made for it, it flies straight enough.");
            else if (!R.rifled) ImGui::TextColored(colBad(), "A bullet from a smooth bore isn't spun: it tumbles and goes wide.");
            else if (sg >= 1.4) ImGui::TextColored(colGood(), "Spins the bullet well (stability %.1f): it flies point first.", sg);
            else if (sg >= 1.0) ImGui::TextColored(colWarn(), "Spins the bullet only just enough (stability %.2f): groups open up. A faster twist steadies it.", sg);
            else ImGui::TextColored(colBad(), "Too slow for this bullet (stability %.2f): it tumbles and goes wide. Use a faster twist.", sg);
            if (tipHover()) ImGui::SetTooltip("From the bullet's weight, width and length and its speed (the Miller rule).\nAbout 1.4 and up flies point first; under 1 it tumbles. On the firing range it changes how far shots spread.");
        }
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Swap breech and muzzle", {w2, 0})) { R.muzzleEnd = -R.muzzleEnd; editedAny = true; }
        ImGui::SameLine();
        if (ImGui::Button("Not a barrel", {w2, 0})) { R.on = false; editedAny = true; }
        if (tipHover()) ImGui::SetTooltip("Keeps the shape as a plain round part.");
    }
    if (section("Control", b.ctrl != "none")) {
        const char* kinds[] = {"none", "button", "lever", "trigger", "handle"};
        int ki = 0;
        for (int i = 0; i < 5; i++) if (b.ctrl == kinds[i]) ki = i;
        prop("Type", "Worked by hand: the part a shooter grabs to work the action (a pump's fore-end, a charging handle,\n"
                     "a bolt). The reliability test works the gun by this part. Without one, it works the breech itself.");
        bool ctrlHit = ImGui::Combo("##ctrl", &ki, "Not a control\0Button\0Lever\0Trigger\0Worked by hand\0");
        tutAnchor("ctrl:type");
        if (ctrlHit) {
            b.ctrl = kinds[ki]; editedAny = true;
            if (ki > 0 && b.ctrlPt.x == 0 && b.ctrlPt.y == 0) b.ctrlPt = centroidOf(b);
            if (ki == 3 && b.key == "none") b.key = "fire";
            double td;
            if (ki == 1 && trackDirection(A.design, b.id, td)) b.ctrlDir = td;   // a button: pushed the way its track goes
        }
        if (b.ctrl != "none" && b.fixed)
            ImGui::TextColored(colWarn(), "This part is Fixed, so it can't move when pressed. Untick Fixed and join it with a track or pin.");
        if (b.ctrl == "handle") muted("Drag it on the bench to work the action. The reliability test pulls it all the way back along its track and pushes it home.");
        if (b.ctrl != "none" && b.ctrl != "handle") {
            const char* keys[] = {"none", "fire", "magrelease", "k1", "k2", "k3", "k4"};
            Act acts[] = {Act::Count, Act::Fire, Act::MagRelease, Act::Control1, Act::Control2, Act::Control3, Act::Control4};
            const char* names[] = {"Hand only", "Fire", "Magazine release", "Your control 1", "Your control 2", "Your control 3", "Your control 4"};
            int kk = 0;
            for (int i = 0; i < 7; i++) if (b.key == keys[i]) kk = i;
            prop("Pressed by");
            std::string cur = std::string(names[kk]) + (kk ? std::string("  (") + keyName(keyOf(acts[kk])) + ")" : "");
            if (ImGui::BeginCombo("##key", cur.c_str())) {
                for (int i = 0; i < 7; i++) if (ImGui::Selectable(names[i], kk == i)) { b.key = keys[i]; editedAny = true; }
                ImGui::EndCombo();
            }
            {
                double dir = std::fmod(std::fmod(b.ctrlDir, 360) + 360, 360);
                const char* way = dir < 22.5 || dir >= 337.5 ? "forward" : dir < 67.5 ? "forward and down" : dir < 112.5 ? "down" : dir < 157.5 ? "back and down"
                                : dir < 202.5 ? "back" : dir < 247.5 ? "back and up" : dir < 292.5 ? "up" : "forward and up";
                pNum("Push direction", b.ctrlDir, 15, TextFormat("%%.0f\xc2\xb0 (%s)", way),
                     "Which way the finger pushes: 0 forward (toward the muzzle), 90 down, 180 back, 270 up.\nThe arrow on the part in the view shows it. It has to be a way the part can move.");
                double td;
                if (trackDirection(A.design, b.id, td) && std::fabs(std::remainder(td - b.ctrlDir, 360.0)) > 1) {
                    if (ImGui::Button(TextFormat("Push along its track (%.0f\xc2\xb0)", td), {-1, 0})) { b.ctrlDir = td; editedAny = true; }
                    if (tipHover()) ImGui::SetTooltip("Points the push the way this part's track lets it slide.");
                }
            }
            pNumU("Finger force", b.ctrlForce, 1, Q::Force);
            if (ImGui::Button("Place the push point", {-1, 0})) A.tool = Tool::CtrlPt;
            muted("Join it with a pin (levers, triggers) or a track (buttons), and give it a spring to return.");
        }
    }

    if (section("Magazine", b.magazine)) {
        int kind = !b.magazine ? 0 : b.magFixed ? 2 : 1, wasKind = kind;
        prop("Magazine", "Removable: you swap it. It slides in and out along its insertion path (the green line).\n"
                         "Built in: part of the gun, like an internal box or a tube. You load it in place,\n"
                         "a round at a time (drag rounds to its feed end) or topped up (T on the bench).");
        if (ImGui::Combo("##magkind", &kind, "Not a magazine\0Removable\0Built in\0")) editedAny = true;
        tutAnchor("mag:kind");
        if (kind != wasKind) {
            std::string id = b.id;
            auto dropWell = [&] { D.joints.erase(std::remove_if(D.joints.begin(), D.joints.end(), [&](const JointDef& j) { return j.type == "magwell" && j.a == id; }), D.joints.end()); };
            if (kind == 0) { b.magazine = false; b.magFixed = false; dropWell(); }
            else if (kind == 1) { b.magazine = true; b.magFixed = false; if (b.fixed) b.fixed = false; if (magwellIndex(b.id) < 0) makeMagwell(b); b.ghost = true; }
            else { b.magazine = true; b.magFixed = true; b.fixed = true; dropWell(); }
        }
        if (b.magazine) {
            prop("Capacity");
            ImGui::DragInt("##cap", &b.magCap, 0.2f, 1, 200, "%d rounds"); markEdit();
            b.magCap = std::clamp(b.magCap, 1, 200);
            {   // how the rounds sit inside: along a path you draw, side by side or nose to tail
                int st = b.roundStyle == "line" ? 1 : 0;
                prop("Rounds sit", "Side by side: stacked across the path, like a box magazine.\n"
                                   "Nose to tail: one behind another along the path, like a tube under a barrel.");
                if (ImGui::Combo("##rstyle", &st, "Side by side\0Nose to tail\0")) {
                    b.roundStyle = st ? "line" : "side"; editedAny = true;
                    if (st) b.tubeFeed = "physical";  // a tube made now feeds the real way
                }
                if (st && A.design.breech.physicalFeed) {
                    int tf = b.tubeFeed == "physical" ? 0 : 1;
                    prop("Rounds leave", "Pushed out: the tube's spring pushes each round out of its mouth, head first, as a real round.\n"
                                         "Something you draw has to take it from there, like a lifter that raises it to the bore,\n"
                                         "and anything in the way (a lifter that's up) holds the next one back. See the pump-action example.\n"
                                         "When the action opens: the round simply reaches the chamber once the action has opened far enough\n"
                                         "(how tubes worked in older versions).");
                    if (ImGui::Combo("##tubefeed", &tf, "Pushed out of the mouth\0When the action opens (simple)\0")) { b.tubeFeed = tf == 0 ? "physical" : "simple"; editedAny = true; }
                    tutAnchor("mag:tubefeed");
                }
                if (b.roundPath.size() >= 2)
                    muted("Round path: %d points. Drag them in the view. The first is where rounds are fed from.", (int)b.roundPath.size());
                else
                    muted(st ? "Draw the path the rounds follow, starting at the mouth." : "Laid out automatically. Draw a path to arrange them yourself, starting where the top round sits.");
                float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
                if (ImGui::Button("Draw the round path", {b.roundPath.empty() ? -1.f : w2, 0})) {
                    A.tool = Tool::RoundPath; A.draft.clear(); A.drafting = true; A.draftHasF = false;
                    toast("Click along the inside of the magazine, starting at the feed end. Enter to finish.");
                }
                tutAnchor("mag:roundpath");
                if (!b.roundPath.empty()) { ImGui::SameLine(); if (ImGui::Button("Automatic", {w2, 0})) { b.roundPath.clear(); editedAny = true; } }
                if (st == 0) {  // a belt instead of a spring
                    int by = b.feedPawl.empty() ? 0 : 1;
                    prop("Moved by", "Its spring: a follower pushes each round up to the lips (a box).\n"
                                     "A pawl: a belt. A part you choose moves it on one round each time it travels the stroke\n"
                                     "away from where it rests and comes back. The next round lies in the tray at the first point\n"
                                     "of the round path; the rest of the belt hangs out of the side. A round still in the tray\n"
                                     "stops the belt. See the belt-fed example.");
                    if (ImGui::Combo("##movedby", &by, "Its spring\0A pawl (a belt)\0")) {
                        if (by == 0) b.feedPawl.clear();
                        else { b.feedPawl = D.breech.on ? D.breech.breech : ""; if (b.feedPawl.empty()) for (auto& o : D.bodies) if (!o.fixed && !o.magazine) { b.feedPawl = o.id; break; } }
                        editedAny = true;
                    }
                    if (by == 1) {
                        pBody("Pawl", b.feedPawl, false);
                        if (tipHover()) ImGui::SetTooltip("The part whose stroke moves the belt: the bolt itself, or an arm you draw and link to it.");
                        pNumU("Pawl stroke", b.pawlStroke, 0.5, Q::Length, 1, "How far the pawl must travel from where it rests to catch the next link.\nLonger than the part can actually travel, and the belt never moves.");
                        b.pawlStroke = std::clamp(b.pawlStroke, 0.5, 500.0);
                        if (b.roundPath.size() < 2) warn("Draw the round path: its first point is where the round lies in the tray.");
                    }
                }
                {
                    int ck = b.clip == "stripper" ? 1 : b.clip == "enbloc" ? 2 : 0;
                    prop("Loaded with", "Loose rounds: reloading fills it.\n"
                                        "Stripper clips: reloading pushes in one clip's worth at a time.\n"
                                        "En-bloc clip: the clip goes in with the rounds. Only a full clip fits, so it can't be topped up,\n"
                                        "and the empty clip is thrown out after the last round.");
                    if (ImGui::Combo("##clip", &ck, "Loose rounds\0Stripper clips\0En-bloc clip\0")) { b.clip = ck == 1 ? "stripper" : ck == 2 ? "enbloc" : "none"; editedAny = true; }
                    if (ck == 1) {
                        prop("Rounds per clip");
                        ImGui::DragInt("##clipsize", &b.clipSize, 0.2f, 1, 200, "%d"); markEdit();
                        b.clipSize = std::clamp(b.clipSize, 1, 200);
                    }
                }
            }
            int mi = magwellIndex(b.id);
            if (mi >= 0 && b.gen.on && b.gen.side) muted("It slides in and out sideways, across the gun.");
            else if (mi >= 0 && D.joints[mi].path.size() >= 2) {
                auto& j = D.joints[mi];
                double dx = j.path.back().x - j.path.front().x, dy = j.path.back().y - j.path.front().y;
                double travel = std::hypot(dx, dy), ang = std::atan2(dx, dy) / DEG2RAD, t0 = travel, a0 = ang;
                if (j.pathAng.size() != j.path.size()) j.pathAng.resize(j.path.size(), 0.0);
                if (j.path.size() == 2) {   // a straight path: its length and angle
                    pNumU("Drop-out travel", travel, 1, Q::Length);
                    pNum("Well angle", ang, 1, "%.1f\xc2\xb0", "0 is straight down.");
                    if (travel != t0 || ang != a0) {
                        travel = std::max(5.0, travel);
                        j.path = {j.path.front(), {j.path.front().x + travel * std::sin(ang * DEG2RAD), j.path.front().y + travel * std::cos(ang * DEG2RAD)}};
                        if (b.gen.on && ang != a0) { b.gen.angle = ang; generateMagazine(D, A.sel.idx); }  // (this is what turns it, even right over)
                        editedAny = true;
                    }
                }
                // the insertion path: bends, and how far the magazine has rocked at each point (rock-in magazines)
                for (size_t k = 1; k < j.path.size(); k++) {
                    char lb[40];
                    if (k + 1 == j.path.size()) std::snprintf(lb, sizeof lb, "Tilt at the mouth"); else std::snprintf(lb, sizeof lb, "Tilt at bend %d", (int)k);
                    pNum(lb, j.pathAng[k], 0.5, "%.1f\xc2\xb0", "How far the magazine has rocked by the time it reaches this point.\n"
                                                          "Rock-in magazines go in at an angle and rock back as they seat.");
                }
                {
                    float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
                    if (ImGui::Button("Add a bend", {w2, 0})) {   // halfway along the last stretch
                        size_t n = j.path.size();
                        V2 a = j.path[n - 2], c = j.path[n - 1];
                        j.path.insert(j.path.end() - 1, {(a.x + c.x) / 2, (a.y + c.y) / 2});
                        j.pathAng.insert(j.pathAng.end() - 1, (j.pathAng[n - 2] + j.pathAng[n - 1]) / 2);
                        editedAny = true;
                    }
                    if (tipHover()) ImGui::SetTooltip("Adds a point to the insertion path. Drag the green points in the view to shape it.");
                    ImGui::SameLine();
                    ImGui::BeginDisabled(j.path.size() <= 2);
                    if (ImGui::Button("Straighten", {w2, 0})) { j.path = {j.path.front(), j.path.back()}; j.pathAng = {j.pathAng.front(), j.pathAng.back()}; editedAny = true; }
                    ImGui::EndDisabled();
                }
                pBody("Well is in", j.b, true);
                prop("Released by");
                if (ImGui::BeginCombo("##rel", j.rel.empty() ? "The key only" : bname(j.rel).c_str())) {
                    if (ImGui::Selectable("The key only", j.rel.empty())) { j.rel = ""; editedAny = true; }
                    for (auto& o : D.bodies) if (o.id != b.id && ImGui::Selectable((o.name + "##rel" + o.id).c_str(), j.rel == o.id)) { j.rel = o.id; editedAny = true; }
                    ImGui::EndCombo();
                }
                if (!j.rel.empty()) pNumU("when it moves", j.releaseAt, 0.5, Q::Length, -1, nullptr, SIGNED);
            }
            bool gen = b.gen.on;
            if (pCheck("Fit to cartridge", gen, "Shapes the magazine around the cartridge: the body, lips and follower.")) {
                b.gen.on = false;
                if (gen) generateMagazine(D, A.sel.idx);
            }
            if (b.gen.on && b.gen.byHand) {
                muted("Reshaped by hand: it keeps the shape you gave it. Its rounds lie along the blue dots: drag them to line them up.");
                if (ImGui::Button("Fit it to the cartridge again", {-1, 0})) {
                    b.gen.byHand = false; b.gen.extraH = b.gen.extraW = 0; b.roundPath.clear();
                    generateMagazine(D, A.sel.idx); editedAny = true;
                }
                if (tipHover()) ImGui::SetTooltip("Back to the fitted shape, sized by the cartridge and capacity, its rounds laid out automatically.");
            }
            if (b.gen.on && !b.gen.byHand) {
                bool regen = false;
                {   // where it sits: under the gun, on top, or sticking out of either side
                    int pos = b.gen.side > 0 ? 3 : b.gen.side < 0 ? 2 : std::fabs(std::remainder(b.gen.angle, 360.0)) > 90 ? 1 : 0;
                    prop("Position", "Below or on top: it sits in the side view, and drops out of its well (set its angle below).\n"
                                     "Left or right side: it sticks out across the gun, like a Sten's. The rounds come in sideways,\n"
                                     "and only its mouth is in the side view: the bolt strips the round lying there, and pushes it\n"
                                     "back into the magazine as it rides over it. Released, it slides out sideways.\n"
                                     "Cut an opening in the receiver wall for it (Carve), or it goes through the wall.");
                    if (ImGui::Combo("##magpos", &pos, "Below\0On top\0Left side\0Right side\0")) {
                        bool wasSide = b.gen.side != 0;
                        b.gen.side = pos == 3 ? 1 : pos == 2 ? -1 : 0;
                        if (pos <= 1) {
                            b.gen.angle = pos == 1 ? 180 : 0;
                            if (wasSide) b.z = 0;
                            int wi = magwellIndex(b.id);   // a fresh straight well, the new way out
                            if (wi >= 0) D.joints[wi].path.resize(std::min<size_t>(D.joints[wi].path.size(), 2));
                            if (wi >= 0 && D.joints[wi].path.size() == 2) { auto& j = D.joints[wi]; j.path[1] = {j.path[0].x + std::sin(b.gen.angle * DEG2RAD), j.path[0].y + std::cos(b.gen.angle * DEG2RAD)}; }
                        }
                        regen = true;
                    }
                }
                float curve = (float)b.gen.curve;
                prop("Curve");
                if (ImGui::SliderFloat("##curve", &curve, -15, 80, "%.0f\xc2\xb0")) { b.gen.curve = curve; regen = true; } typeable();
                if (pNumU("Wall", b.gen.wall, 0.1, Q::Length, CFG.units == Units::Metric ? 1 : 3)) { b.gen.wall = std::clamp(b.gen.wall, 0.6, 4.0); regen = true; }
                if (b.gen.side == 0) {   // its size: as fitted, or longer / wider than it needs to be
                    double L = b.gen.H, Wd = b.gen.W;
                    if (pNumU("Length", L, 0.5, Q::Length, 1, "From the lips to the floorplate. Fitted: just long enough for its capacity.\nLonger leaves room below the follower; shorter than its rounds need, and they won't all fit.")) {
                        b.gen.extraH += L - b.gen.H; regen = true;
                    }
                    if (pNumU("Width", Wd, 0.5, Q::Length, 1, "Front to back. Fitted: the round's length plus the walls.")) { b.gen.extraW += Wd - b.gen.W; regen = true; }
                    if ((b.gen.extraH != 0 || b.gen.extraW != 0) && ImGui::Button("Back to the fitted size", {-1, 0})) { b.gen.extraH = b.gen.extraW = 0; regen = true; }
                }
                static int lastCap = -1;
                if (lastCap != b.magCap) { lastCap = b.magCap; regen = true; }
                if (regen) { generateMagazine(D, A.sel.idx); editedAny = true; }
                muted("%s, %d rounds: %s long.", D.breech.cart.name.c_str(), b.magCap, fmtQ(Q::Length, b.gen.H, 0).c_str());
            }
            {   // how the rounds sit and come out: any box magazine, fitted or drawn by hand
                double rd = 0, fd = 90;
                if (magFeedAngles(D, b, rd, fd)) {
                    ImGui::Dummy({0, 4 * S()});
                    ImGui::TextDisabled("Rounds inside");
                    int st = magStackOf(b) - 1;
                    prop("Stack", "Single: one column of rounds. Double: two staggered columns, about half as long for the same rounds.\n"
                                  "Works on any magazine: fitted, reshaped or drawn by hand. A fitted one is refitted; your shape is kept.");
                    if (ImGui::Combo("##stack", &st, "Single\0Double\0")) {
                        b.magStack = st + 1;
                        if (b.gen.on) { b.gen.stack = st + 1; if (!b.gen.byHand) generateMagazine(D, A.sel.idx); }
                        editedAny = true;
                    }
                    if (magStackOf(b) == 2) {
                        int fp = b.doubleFeed ? 1 : 0;
                        prop("Feeds from", "Single position: the two columns taper into one, and every round feeds from the centre.\n"
                                           "Straight in, so short pistol rounds like it; the taper's friction costs some spring (about a fifth).\n"
                                           "Double position: rounds feed alternately from the left and right lips.\n"
                                           "Easy on the spring, but each round comes in at an angle sideways: fine for long rifle rounds,\n"
                                           "marginal for 9 mm, and fat short rounds like .45 ACP may not make it into the chamber.");
                        if (ImGui::Combo("##feedpos", &fp, "Single position (centre)\0Double position (alternating)\0")) { b.doubleFeed = fp == 1; editedAny = true; }
                    }
                    auto angleRow = [&](const char* label, const char* id, bool& set, double& val, double autoVal, double lo, double hi, const char* tip) {
                        prop(label, tip);
                        bool on = set;
                        if (ImGui::Checkbox(id, &on)) { set = on; if (on) val = std::round(autoVal); editedAny = true; }
                        if (tipHover()) ImGui::SetTooltip("Ticked: set by hand. Unticked: worked out from the magazine's shape.");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(-1);
                        float v = (float)(set ? val : autoVal);
                        ImGui::BeginDisabled(!set);
                        if (ImGui::DragFloat((std::string(id) + "v").c_str(), &v, 0.5f, (float)lo, (float)hi, set ? "%.1f\xc2\xb0" : "%.1f\xc2\xb0 (automatic)")) { val = std::clamp((double)v, lo, hi); editedAny = true; }
                        typeable();
                        ImGui::EndDisabled();
                    };
                    angleRow("Round angle", "##rndset", b.roundSet, b.roundAngle, rd, -60, 60,
                             "The angle the top round lies at, from the bore: 0 is parallel to it, + nose up.\n"
                             "It slides off the lips along this line when the bolt pushes it. Nose up meets the feed ramp\n"
                             "and chamber sooner; nose down can stub on the ramp or the barrel's face (failure to feed).");
                    angleRow("Feed direction", "##fdset", b.feedSet, b.feedAngle, fd, -180, 180,
                             "The way the follower pushes the rounds out, from the bore: 90 is straight up toward it,\n"
                             "less leans forward, more leans back. A raked or curved magazine feeds at its own angle;\n"
                             "set this to match where the lips actually present the round. Kept at least 25\xc2\xb0 off the round's line.");
                }
            }
            // feed lips and spring: fitted to the cartridge to start with; these nudge them (physical feeding only)
            ImGui::Dummy({0, 4 * S()});
            ImGui::TextDisabled("Feed lips and spring");
            if (!D.breech.physicalFeed) muted("These only matter with Firing > Physical rounds ticked.");
            pNumU("Lip height", b.lipHeight, 0.1, Q::Length, CFG.units == Units::Metric ? 1 : 3,
                  "Raises (+) or lowers (-) the top round from where the lips normally hold it.\n"
                  "Too low and the breech face rides over its rim instead of pushing it (failure to feed).", SIGNED);
            tutAnchor("mag:lips");
            b.lipHeight = std::clamp(b.lipHeight, -5.0, 5.0);
            {
                double sp = b.magSpring * 100;
                if (pNum("Spring strength", sp, 1, "%.0f%%", "The follower spring, against a standard one (100%).\n"
                         "A weak spring brings the next round up slowly: a fast action or a long magazine outruns it\n"
                         "and the breech closes on nothing (failure to feed). Pistols that cycle fast feel it first; a bolt action you work by hand barely does."))
                    b.magSpring = std::clamp(sp / 100, 0.1, 3.0);
                tutAnchor("mag:spring");
            }
            if (b.lipHeight != 0 || b.magSpring != 1) {
                if (ImGui::Button("Reset lips and spring", {-1, 0})) { b.lipHeight = 0; b.lipRelease = 0.45; b.magSpring = 1; editedAny = true; }
            }
        }
    }

    if (section("Advanced", false)) {
        pCheck("See-through in X-ray", b.ghost, "Drawn as a ghost when X-ray is on, so you can see what's inside it.");
        if (b.layer != "none") {
            prop("Collision groups", "Solid parts only touch if they share a group. Depth usually does this job better.");
            bool la = b.layer.find('A') != std::string::npos, lb = b.layer.find('B') != std::string::npos, lc = b.layer.find('C') != std::string::npos;
            bool ch = ImGui::Checkbox("A##la", &la); ImGui::SameLine();
            ch |= ImGui::Checkbox("B##lb", &lb); ImGui::SameLine();
            ch |= ImGui::Checkbox("C##lc", &lc);
            if (ch) { b.layer = std::string(la ? "A" : "") + (lb ? "B" : "") + (lc ? "C" : ""); if (b.layer.empty()) b.layer = "none"; editedAny = true; }
        }
    }

    ImGui::Dummy({0, 6 * S()});
    float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("Duplicate", {w2, 0})) duplicateSel();
    if (tipHover()) ImGui::SetTooltip("Ctrl+D");
    ImGui::SameLine();
    if (dangerButton("Delete", {w2, 0})) deleteSel();
}

// ---------------------------------------------------------------- inspect: a joint
static void inspectJoint(JointDef& j) {
    auto& D = A.design;
    ImGui::PushFont(gFontBold, 15.0f);
    ImGui::TextUnformatted(JT_LABEL(j.type));
    ImGui::PopFont();
    muted("%s", jointDesc(j).c_str());
    ImGui::Dummy({0, 2 * S()});
    if (j.type == "pin") { pBody("Part", j.a, false); pBody("Joined to", j.b, true); }
    if (j.type == "pin") {
        pNumU("Diameter", j.pinDia, 0.5, Q::Length, -1, "How thick the pin is drawn. The parts pivot about its centre either way.");
        j.pinDia = std::clamp(j.pinDia, 0.3, 40.0);
    }
    if (j.type == "track") {
        pBody("Rider", j.a, true); pBody("Track is in", j.b, true);
        pCheck("No turning", j.lockRot, "The rider keeps its angle, like a slide on a straight rail.");
        muted("%d points. The ends are hard stops.", (int)j.path.size());
    }
    if (j.type == "magwell") { pBody("Magazine", j.a, false); pBody("Well is in", j.b, true); muted("Select the magazine part to set its capacity, travel and angle."); }
    if (j.type == "spring") {
        int kind = j.kind == "leaf" ? 1 : 0;
        prop("Kind");
        if (ImGui::Combo("##kind", &kind, "Coil\0Leaf\0")) { j.kind = kind ? "leaf" : "coil"; editedAny = true; }
        if (j.kind == "coil") {
            pBody("End 1", j.a, true); pBody("End 2", j.b, true);
            pNumU("Stiffness", j.k, 0.1, Q::SpringRate);
            pNumU("Free length", j.rest, 1, Q::Length);
            if (section("Coil", false)) {
                pNumU("Coil diameter", j.coilDia, 0.5, Q::Length);
                pNumU("Wire", j.wire, 0.1, Q::Length, lenDec());
                pNum("Turns", j.turns, 1, "%.0f");
                j.turns = std::max(1.0, j.turns);
                pNumU("Damping", j.damp, 1, Q::Damping);
                prop("Guide rod");
                if (ImGui::BeginCombo("##guide", j.guide.empty() ? "None" : bname(j.guide).c_str())) {
                    if (ImGui::Selectable("None", j.guide.empty())) { j.guide = ""; editedAny = true; }
                    for (auto& o : D.bodies) if (ImGui::Selectable((o.name + "##g" + o.id).c_str(), j.guide == o.id)) { j.guide = o.id; editedAny = true; }
                    ImGui::EndCombo();
                }
            }
            double L = std::hypot(j.pb.x - j.pa.x, j.pb.y - j.pa.y), F = j.k * (j.rest - L), solid = (j.turns + 2) * j.wire;
            muted("%s %s as drawn. Solid at %s.", fmtQ(Q::Force, std::fabs(F)).c_str(), F >= 0 ? "pushing" : "pulling", fmtQ(Q::Length, solid).c_str());
            if (j.guide.empty()) {
                double sk = bucklingSqueeze(j.rest, j.coilDia, j.wire);
                if (sk < j.rest) {
                    std::string m = "With nothing inside it, it bows out sideways once squeezed more than " + fmtQ(Q::Length, sk) +
                                    " (" + std::to_string((int)std::lround(100 * sk / j.rest)) + "% of its length) and goes soft. A guide rod (under Coil) stops that.";
                    if (j.rest - L > sk) warn(m.c_str()); else muted("%s", m.c_str());
                }
            }
            if (L < solid) warn("Drawn shorter than its solid height, so it can't fit. Use thinner wire or fewer turns.");
        } else {
            pBody("Clamped to", j.a, true); pBody("Presses on", j.b, true);
            pNumU("Stiffness", j.k, 0.1, Q::SpringRate);
            pNumU("Preload", j.preload, 0.25, Q::Length, lenDec());
            pCheck("Push the other way", j.flip);
            muted("A flat spring: its tip pushes on the other part and never pulls. For triggers, sears and tight spaces.");
        }
    }
    if (j.type == "cam") {
        pBody("Turning part", j.a, false); pBody("Cam is in", j.b, true);
        std::string t;
        for (size_t i = 0; i < j.table.size(); i++) { char bb[48]; std::snprintf(bb, sizeof bb, "%s%g:%g", i ? ", " : "", j.table[i].x, j.table[i].y); t += bb; }
        static char buf[256];
        if (!ImGui::IsAnyItemActive()) std::snprintf(buf, sizeof buf, "%s", t.c_str());
        prop("Profile", "Travel in mm : rotation in degrees, comma separated. Negative travel is rearward.");
        ImGui::InputText("##prof", buf, sizeof buf);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            std::vector<V2> tab;
            std::stringstream ss(buf);
            std::string item;
            while (std::getline(ss, item, ',')) { double a, b2; if (std::sscanf(item.c_str(), " %lf : %lf", &a, &b2) == 2) tab.push_back({a, b2}); }
            if (!tab.empty()) { j.table = tab; editedAny = true; }
        }
    }
    if (j.type == "lug") {
        pBody("Turning part", j.a, false); pBody("Locks into", j.b, true);
        pNum("Unlocks past", j.unlock, 5, "%.0f\xc2\xb0");
        pCheck("Only locks home", j.blockOpen);
        muted("A rule-based lock from the older examples. Real lugs welded to the bolt lock by their shape.");
    }
    if (j.type == "latch") {
        pBody("Held part", j.a, false); pBody("Measured from", j.b, true);
        pNumU("Catches at", j.catchAt, 0.5, Q::Length, -1, nullptr, SIGNED);
        pBody("Released by", j.rel, false);
        pNumU("at travel", j.releaseAt, 0.5, Q::Length, -1, nullptr, SIGNED);
        pCheck("Holds it closed", j.holdShut, "An action lock: the held part can't go back past where it catches (a shot can't blow it open)\n"
                                              "until the release part has been drawn back this far against it (a pump's fore-end moving\n"
                                              "in its free play before it takes the bolt along). It catches again when it comes home.");
    }
    ImGui::Dummy({0, 6 * S()});
    if (dangerButton("Delete joint", {-1, 0})) deleteSel();
}

// ---------------------------------------------------------------- outline
static bool gPickedInOutline = false;  // a pick in the Outline keeps the Outline open (so a double-click can rename)
static void outlineTab() {
    auto& D = A.design;
    static char filter[64] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof filter);
    auto match = [&](const std::string& s) {
        if (!filter[0]) return true;
        std::string a = s, f = filter;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower); std::transform(f.begin(), f.end(), f.begin(), ::tolower);
        return a.find(f) != std::string::npos;
    };
    ImGui::BeginChild("##outline", {0, 0});
    if (section("Parts", true)) {
        for (size_t i = 0; i < D.bodies.size(); i++) {
            auto& b = D.bodies[i];
            if (!match(b.name)) continue;
            Color c = hexColor(b.color);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float h = ImGui::GetTextLineHeight();
            ImGui::PushID((int)i);
            bool sel = A.sel.kind == 1 && A.sel.idx == (int)i;
            static int renaming = -1;
            static char rbuf[128];
            if (renaming == (int)i) {  // renaming in place
                ImGui::SetCursorScreenPos({p.x + 14 * S(), p.y});
                ImGui::SetNextItemWidth(-1);
                if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
                bool done = ImGui::InputText("##rename", rbuf, sizeof rbuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (done || ImGui::IsItemDeactivated()) {
                    if (rbuf[0] && b.name != rbuf) { b.name = rbuf; editedAny = true; }
                    renaming = -1;
                }
                ImGui::PopID();
                continue;
            }
            if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_AllowDoubleClick, {0, h + 4 * S()})) {
                A.sel = {1, (int)i, -1};
                gPickedInOutline = true;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { renaming = (int)i; std::snprintf(rbuf, sizeof rbuf, "%s", b.name.c_str()); }
            }
            if (tipHover()) ImGui::SetTooltip("Click to pick it out in the view. Double-click to rename it.");
            ImGui::PopID();
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled({p.x + 6 * S(), p.y + (h + 4 * S()) / 2}, 4 * S(), IM_COL32(c.r, c.g, c.b, 255));
            dl->AddText({p.x + 18 * S(), p.y + 2 * S()}, ImGui::GetColorU32(ImGuiCol_Text), b.name.c_str());
            const char* tag = b.fixed ? "frame" : b.magazine ? "magazine" : b.rollOn ? "turns" : !b.weld.empty() ? "welded" : b.layer == "none" ? "not solid" : "";
            if (*tag) {
                float tw = ImGui::CalcTextSize(tag).x;
                dl->AddText({p.x + ImGui::GetContentRegionAvail().x - tw - 4 * S(), p.y + 2 * S()}, ImGui::GetColorU32(ImGuiCol_TextDisabled), tag);
            }
        }
    }
    if (section("Joints and springs", true)) {
        for (size_t i = 0; i < D.joints.size(); i++) {
            auto& j = D.joints[i];
            std::string d = jointDesc(j);
            if (!match(d) && !match(JT_LABEL(j.type))) continue;
            ImGui::PushID(1000 + (int)i);
            if (ImGui::Selectable(d.c_str(), A.sel.kind == 2 && A.sel.idx == (int)i)) { A.sel = {2, (int)i, -1}; gPickedInOutline = true; }
            ImGui::PopID();
            if (tipHover()) ImGui::SetTooltip("%s", JT_LABEL(j.type));
        }
    }
    if (section("Rule-based joints", false)) {
        muted("From the older examples. New designs lock and cam with real shapes instead.");
        auto addLogic = [&](const char* type) {
            int rb = -1, fr = -1;
            for (size_t i = 0; i < D.bodies.size(); i++) { if (D.bodies[i].rollOn && rb < 0) rb = (int)i; if (D.bodies[i].fixed && fr < 0) fr = (int)i; }
            if (rb < 0) for (size_t i = 0; i < D.bodies.size(); i++) if (!D.bodies[i].fixed) { rb = (int)i; break; }
            if (rb < 0) { toast("Draw a moving part first."); return; }
            JointDef jn; jn.type = type; jn.a = D.bodies[rb].id; jn.b = fr >= 0 ? D.bodies[fr].id : "world";
            if (jn.type == "cam") jn.table = {{0, 0}, {-10, 0}, {-20, 45}};
            if (jn.type == "latch") jn.rel = jn.a;
            if (jn.type != "latch" && !D.bodies[rb].rollOn) { D.bodies[rb].rollOn = true; D.bodies[rb].rollMax = 90; }
            D.joints.push_back(jn); A.sel = {2, (int)D.joints.size() - 1, -1}; commit();
        };
        float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        if (ImGui::Button("Cam", {w3, 0})) addLogic("cam");
        ImGui::SameLine(); if (ImGui::Button("Lugs", {w3, 0})) addLogic("lug");
        ImGui::SameLine(); if (ImGui::Button("Catch", {w3, 0})) addLogic("latch");
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- firing system
// ---------------------------------------------------------------- the attachment designer's tab
// A reticle, drawn in milliradians: lines, dots and rings, edited on a round canvas like the view through the sight.
static void reticleEditor(OpticDef& op) {
    static int tool = 0;              // 0 line, 1 dot, 2 ring, 3 erase
    static double width = 0.3;
    static bool dragging = false;
    static ImVec2 from;
    float side = std::min(ImGui::GetContentRegionAvail().x, 280 * S());
    double R = op.kind == "reflex" ? 15 : 20;            // mrad from the middle to the canvas's edge
    double k = side / 2 / R;                               // pixels a milliradian
    ImVec2 p0 = ImGui::GetCursorScreenPos(), c{p0.x + side / 2, p0.y + side / 2};
    ImGui::InvisibleButton("##reticle", {side, side});
    bool hov = ImGui::IsItemHovered();
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, side / 2, op.kind == "reflex" ? IM_COL32(150, 170, 180, 255) : IM_COL32(205, 214, 222, 255), 64);   // the view through it
    for (int g = -(int)R; g <= (int)R; g += 5) {   // a faint grid every 5 mrad
        dl->AddLine({c.x + (float)(g * k), c.y - side / 2}, {c.x + (float)(g * k), c.y + side / 2}, IM_COL32(0, 0, 0, 22));
        dl->AddLine({c.x - side / 2, c.y + (float)(g * k)}, {c.x + side / 2, c.y + (float)(g * k)}, IM_COL32(0, 0, 0, 22));
    }
    unsigned rc = op.color;
    ImU32 col = IM_COL32((rc >> 24) & 255, (rc >> 16) & 255, (rc >> 8) & 255, 255);
    auto P = [&](double x, double y) { return ImVec2{c.x + (float)(x * k), c.y - (float)(y * k)}; };
    for (auto& m : op.reticle) {
        float w = std::max(1.f, (float)(m.size * k));
        if (m.kind == 0) dl->AddLine(P(m.x0, m.y0), P(m.x1, m.y1), col, w);
        else if (m.kind == 1) dl->AddCircleFilled(P(m.x0, m.y0), std::max(1.5f, w / 2), col, 20);
        else dl->AddCircle(P(m.x0, m.y0), (float)(m.x1 * k), col, 64, w);
    }
    dl->AddCircle(c, side / 2, IM_COL32(20, 22, 26, 255), 64, 3);
    auto toMrad = [&](ImVec2 q) { double x = (q.x - c.x) / k, y = -(q.y - c.y) / k; return std::pair<double, double>{std::round(x * 10) / 10, std::round(y * 10) / 10}; };
    ImVec2 m = ImGui::GetIO().MousePos;
    if (hov && ImGui::IsMouseClicked(0)) { dragging = true; from = m; }
    if (dragging && tool != 3) {
        if (tool == 0) dl->AddLine(from, m, IM_COL32(60, 160, 255, 200), std::max(1.f, (float)(width * k)));
        if (tool == 2) dl->AddCircle(from, std::hypot(m.x - from.x, m.y - from.y), IM_COL32(60, 160, 255, 200), 48, std::max(1.f, (float)(width * k)));
    }
    if (dragging && ImGui::IsMouseReleased(0)) {
        dragging = false;
        auto a = toMrad(from), b = toMrad(m);
        bool shift = ImGui::GetIO().KeyShift;
        if (tool == 0 && std::hypot(b.first - a.first, b.second - a.second) > 0.2) {
            if (shift) { if (std::fabs(b.first - a.first) > std::fabs(b.second - a.second)) b.second = a.second; else b.first = a.first; }   // level or upright
            op.reticle.push_back({0, a.first, a.second, b.first, b.second, width}); editedAny = true;
        } else if (tool == 1) { op.reticle.push_back({1, a.first, a.second, 0, 0, width}); editedAny = true; }
        else if (tool == 2) { double r = std::hypot(b.first - a.first, b.second - a.second); if (r > 0.2) { op.reticle.push_back({2, a.first, a.second, std::round(r * 10) / 10, 0, width}); editedAny = true; } }
        else if (tool == 3) {   // the mark nearest the click
            int best = -1; double bd = 1.0;
            for (int i = 0; i < (int)op.reticle.size(); i++) {
                auto& q = op.reticle[i];
                double d;
                if (q.kind == 0) {
                    double vx = q.x1 - q.x0, vy = q.y1 - q.y0, L2 = vx * vx + vy * vy;
                    double t = L2 > 0 ? std::clamp(((a.first - q.x0) * vx + (a.second - q.y0) * vy) / L2, 0.0, 1.0) : 0;
                    d = std::hypot(a.first - (q.x0 + vx * t), a.second - (q.y0 + vy * t));
                } else if (q.kind == 1) d = std::hypot(a.first - q.x0, a.second - q.y0);
                else d = std::fabs(std::hypot(a.first - q.x0, a.second - q.y0) - q.x1);
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) { op.reticle.erase(op.reticle.begin() + best); editedAny = true; }
        }
    }
    if (hov) { auto q = toMrad(m); ImGui::SetTooltip("%.1f, %.1f mrad\n(1 mrad = 10 cm at 100 m; Shift keeps a line level or upright)", q.first, q.second); }
    static const char* tools[] = {"Line", "Dot", "Ring", "Erase"};
    for (int t = 0; t < 4; t++) { if (t) ImGui::SameLine(0, 2 * S()); if (accentButton(tools[t], tool == t)) tool = t; }
    pNum("Thickness##ret", width, 0.05, "%.2f mrad", "How thick the next line or ring is, or how big the next dot.");
    width = std::clamp(width, 0.05, 5.0);
    static const char* presets[] = {"Dot", "Dot and ring", "Crosshair", "Duplex", "Chevron", "Mil-dot"};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##retpreset", "Start from...")) {
        for (int i = 0; i < 6; i++) if (ImGui::Selectable(presets[i])) {
            std::vector<ReticleMark> r;
            if (i == 0) r = {{1, 0, 0, 0, 0, 0.58}};
            if (i == 1) r = defaultReticle("reflex");
            if (i == 2) r = {{0, -15, 0, 15, 0, 0.1}, {0, 0, -15, 0, 15, 0.1}};
            if (i == 3) r = defaultReticle("scope");
            if (i == 4) r = {{0, -1.5, -1.5, 0, 0, 0.25}, {0, 0, 0, 1.5, -1.5, 0.25}, {0, 0, -1.6, 0, -8, 0.25}};
            if (i == 5) { r = {{0, -15, 0, 15, 0, 0.08}, {0, 0, -15, 0, 15, 0.08}}; for (int d = -5; d <= 5; d++) if (d) { r.push_back({1, (double)d, 0, 0, 0, 0.25}); r.push_back({1, 0, (double)d, 0, 0, 0.25}); } }
            op.reticle = r; editedAny = true;
        }
        ImGui::EndCombo();
    }
    float colf[3] = {((rc >> 24) & 255) / 255.f, ((rc >> 16) & 255) / 255.f, ((rc >> 8) & 255) / 255.f};
    prop("Colour");
    if (ImGui::ColorEdit3("##retcol", colf, ImGuiColorEditFlags_NoInputs)) {
        op.color = ((unsigned)(colf[0] * 255) << 24) | ((unsigned)(colf[1] * 255) << 16) | ((unsigned)(colf[2] * 255) << 8) | 255u; editedAny = true;
    }
    if (op.kind == "scope") pCheck("Lit##ret", op.lit, "It glows (illuminated), instead of black etched lines.");
    if (ImGui::SmallButton("Clear the reticle")) { op.reticle.clear(); editedAny = true; }
}

static void attachmentTab() {
    auto& D = A.design;
    auto& a = D.attach;
    autoAttach(D);
    heading("What it is");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##job", attachJobName(a.job))) {
        for (auto& j : attachJobs()) if (ImGui::Selectable(attachJobName(j.c_str()), a.job == j)) { a.job = j; editedAny = true; }
        ImGui::EndCombo();
    }
    if (a.job == "adapter") muted("Draw rails (Rail tool) and attachment points (Mount tool) on it: fitted to a gun, they become more places to mount things (a side-rail conversion to Picatinny, say).");
    else if (a.job == "muzzle") {
        muted("It goes on the muzzle: make its mount a Muzzle thread (or your own kind), meeting it with its back end, and put that point on your guns' muzzles.");
        static const char* kinds[] = {"brake", "compensator", "suppressor", "flashhider"};
        static const char* names[] = {"Brake", "Compensator", "Suppressor", "Flash hider"};
        static const char* tips[] = {"Throws the gas sideways and back: less kick.", "Throws the gas up: the muzzle climbs less.",
                                     "Much quieter, no flash, and NPCs only hear it from close by.", "A much smaller flash."};
        for (int k = 0; k < 4; k++) {
            if (k) ImGui::SameLine(0, 2 * S());
            if (accentButton(names[k], a.muzzleKind == kinds[k])) { a.muzzleKind = kinds[k]; editedAny = true; }
            if (tipHover()) ImGui::SetTooltip("%s", tips[k]);
        }
    }
    else if (a.job == "grip") muted("A grip steadies your aim: the gun sways less when you aim.");
    else if (a.job == "stock") muted("A stock or brace puts a one-handed gun in the shoulder: steadier, and it kicks less.");
    else if (a.job == "other") muted("Its weight still counts once it's on a gun.");
    else if (a.job == "bipod") muted("Legs down (on the range: Alt, then its button), aim with its feet near something to rest on (a bench, a wall, the floor when you're prone): the gun hardly sways and kicks less.\nIts feet are its lowest point as drawn: draw the legs down.");
    else if (a.job == "bayonet") {
        muted("V stabs on the range. The blade's point is its front end; it reaches as far as it sticks out past the muzzle.");
        pNum("Stab damage", a.stab, 1, a.stab > 0 ? "%.0f" : "from its length", "What a stab does to an NPC (they have 100 health; weak spots and armour count as for bullets).\n0: worked out from how far the blade sticks out.");
        a.stab = std::clamp(a.stab, 0.0, 1000.0);
    }
    else if (a.job == "light" || a.job == "laser") {
        bool light = a.job == "light";
        muted(light ? "Shines along its beam line, out of its front end. T switches it on and off on the range (or Alt, then its button)."
                    : "A dot where its line meets something. T switches it on and off on the range (or Alt, then its button).");
        if (light) {
            pNum("Beam width", a.beamDeg, 0.5, "%.1f deg", "How wide the beam opens, edge to edge: a narrow throw or a wide flood.");
            a.beamDeg = std::clamp(a.beamDeg, 1.0, 120.0);
            pNum("Brightness", a.lumens, 25, "%.0f lm", "Brighter lights reach farther.");
            a.lumens = std::clamp(a.lumens, 10.0, 10000.0);
        }
        unsigned c = beamColorOf(a);
        float col[3] = {((c >> 24) & 255) / 255.f, ((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f};
        prop("Colour", light ? "The light's colour." : "Red, green, or anything (infrared would need night vision, not in the game yet).");
        if (ImGui::ColorEdit3("##beamc", col, ImGuiColorEditFlags_NoInputs)) {
            a.beamColor = ((unsigned)(col[0] * 255) << 24) | ((unsigned)(col[1] * 255) << 16) | ((unsigned)(col[2] * 255) << 8) | 255u;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
        if (!light) {
            ImGui::SameLine(0, 4 * S());
            if (ImGui::SmallButton("Red")) { a.beamColor = 0xFF2A1EFFu; editedAny = true; }
            ImGui::SameLine(0, 2 * S());
            if (ImGui::SmallButton("Green")) { a.beamColor = 0x30FF50FFu; editedAny = true; }
        }
    }
    heading("How it mounts");
    bool pic = a.mount == "Picatinny";
    if (accentButton("Picatinny clamp", pic)) { a.mount = "Picatinny"; editedAny = true; }
    ImGui::SameLine(0, 2 * S());
    if (accentButton("An attachment point", !pic)) { if (pic) a.mount = "Warsaw Pact side rail"; editedAny = true; }
    if (pic) {
        pNumU("Clamp length", a.clamp, 1, Q::Length, -1, "How much rail it takes: nothing else can clamp on that stretch.");
        a.clamp = std::clamp(a.clamp, 3.0, 600.0);
        ImGui::TextDisabled("Takes about %d slots of a rail.", std::max(1, (int)std::lround(a.clamp / mounts::SLOT_PITCH)));
    } else {
        prop("Kind", "It fits attachment points of this kind on a gun (named in the gun designer's Mount tool).");
        char tb[64]; std::snprintf(tb, sizeof tb, "%s", a.mount.c_str());
        ImGui::SetNextItemWidth(-28 * S());
        if (ImGui::InputText("##amount", tb, sizeof tb)) a.mount = tb;
        if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
        ImGui::SameLine(0, 2 * S());
        if (ImGui::BeginCombo("##amounts", nullptr, ImGuiComboFlags_NoPreview)) {
            for (auto& t : mounts::commonMountTypes()) if (ImGui::Selectable(t.c_str(), a.mount == t)) { a.mount = t; editedAny = true; }
            ImGui::EndCombo();
        }
    }
    {   // which side meets the mount
        static const char* faces[] = {"under", "top", "rear", "front"};
        static const char* names[] = {"Its underside (sits on top of a rail)", "Its top (hangs under a rail, like a grip)",
                                      "Its back end (onto a muzzle)", "Its front end"};
        int fi = 0; for (int k = 0; k < 4; k++) if (a.face == faces[k]) fi = k;
        prop("Meets it with", "Which side of it goes against the rail or the point. It's turned on the gun so that side faces the mount.");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##aface", &fi, names, 4)) { a.face = faces[fi]; a.atSet = false; editedAny = true; }
    }
    static const char* spotName[] = {"the middle of its underside", "the middle of its top", "the middle of its back end", "the middle of its front end"};
    int sfi = a.face == "top" ? 1 : a.face == "rear" ? 2 : a.face == "front" ? 3 : 0;
    ImGui::TextDisabled(a.atSet ? "Where it meets the mount (set by hand):" : "Where it meets the mount: %s.", spotName[sfi]);
    double ay = -a.at.y;
    bool mv = pNumU("X##att", a.at.x, 0.5, Q::Length, -1, "+ toward the front.", SIGNED);
    if (pNumU("Y##att", ay, 0.5, Q::Length, -1, "+ up.", SIGNED)) { a.at.y = -ay; mv = true; }
    mv |= pNumU("Z##att", a.atZ, 0.5, Q::Length, -1, "Across: + toward the right side.", SIGNED);
    if (mv) a.atSet = true;
    if (a.atSet && ImGui::SmallButton(TextFormat("Back to %s", spotName[sfi]))) { a.atSet = false; editedAny = true; }
    if (a.job == "optic") {
        auto& op = a.optic;
        heading("The sight");
        bool refl = op.kind == "reflex";
        if (accentButton("Reflex sight", refl)) { if (!refl) { op.kind = "reflex"; op.mag = 1; op.magMax = 0; op.reticle = defaultReticle("reflex"); } editedAny = true; }
        if (tipHover()) ImGui::SetTooltip("A lit dot on a window: no magnification, your eye can be anywhere behind it.");
        ImGui::SameLine(0, 2 * S());
        if (accentButton("Scope", !refl)) { if (refl) { op.kind = "scope"; op.mag = 4; op.reticle = defaultReticle("scope"); } editedAny = true; }
        if (tipHover()) ImGui::SetTooltip("Magnified, in a tube: your eye has to sit at the right distance behind it (eye relief).");
        if (!refl) {
            pNum("Magnification", op.mag, 0.25, "%.2fx", "How much bigger things look through it.");
            op.mag = std::clamp(op.mag, 1.0, 60.0);
            bool var = op.magMax > op.mag;
            if (pCheck("Variable##mag", var, "It zooms, from its magnification up to a highest one.")) op.magMax = var ? op.mag * 3 : 0;
            if (op.magMax > op.mag || var) { pNum("Up to", op.magMax, 0.25, "%.2fx"); op.magMax = std::clamp(op.magMax, op.mag + 0.25, 60.0); }
            pNumU("Eye relief", op.eyeRelief, 2, Q::Length, -1, "How far behind the rear lens your eye has to be to see the whole picture.\nWhere the scope sits on the rail decides if that works for the gun's hold.");
            pNumU("Front lens", op.objective, 1, Q::Length, -1, "The objective's diameter: bigger is brighter and heavier.");
            pCheck("Grows with zoom", op.ffp, "First focal plane: the reticle's marks stay true at every magnification.\nOff (second focal plane): it stays the same size; its marks are true only at the highest.");
        }
        ImGui::TextDisabled(op.lensSet ? "Its sight line (set by hand):" : "Its sight line: through its longest part, end to end.");
        double ry = -op.rear.y, fy = -op.front.y;
        bool lm = pNumU("Rear X##lens", op.rear.x, 0.5, Q::Length, -1, nullptr, SIGNED);
        if (pNumU("Rear Y##lens", ry, 0.5, Q::Length, -1, "+ up.", SIGNED)) { op.rear.y = -ry; lm = true; }
        lm |= pNumU("Front X##lens", op.front.x, 0.5, Q::Length, -1, nullptr, SIGNED);
        if (pNumU("Front Y##lens", fy, 0.5, Q::Length, -1, "+ up.", SIGNED)) { op.front.y = -fy; lm = true; }
        if (lm) op.lensSet = true;
        if (op.lensSet && ImGui::SmallButton("Back to its ends")) { op.lensSet = false; editedAny = true; }
        muted("On a gun, aiming puts your eye on this line (behind it by the eye relief), whatever the gun's pose says.");
        heading("Reticle");
        reticleEditor(op);
    }
    if (a.job == "light" || a.job == "laser") {
        auto& op = a.optic;
        heading("Its beam line");
        ImGui::TextDisabled(op.lensSet ? "Set by hand:" : "Through its longest part, end to end (it comes out of the front).");
        double ry = -op.rear.y, fy = -op.front.y;
        bool lm = pNumU("Back X##beam", op.rear.x, 0.5, Q::Length, -1, nullptr, SIGNED);
        if (pNumU("Back Y##beam", ry, 0.5, Q::Length, -1, "+ up.", SIGNED)) { op.rear.y = -ry; lm = true; }
        lm |= pNumU("Front X##beam", op.front.x, 0.5, Q::Length, -1, "Where it comes out.", SIGNED);
        if (pNumU("Front Y##beam", fy, 0.5, Q::Length, -1, "+ up.", SIGNED)) { op.front.y = -fy; lm = true; }
        if (lm) op.lensSet = true;
        if (op.lensSet && ImGui::SmallButton("Back to its ends##beam")) { op.lensSet = false; editedAny = true; }
    }
}

static void firingTab() {
    auto& D = A.design;
    auto& br = D.breech;
    if (!br.on) {
        muted("No firing system yet. Add one, then click where the cartridge head sits against the breech.");
        bool addHit = ImGui::Button("Add a firing system", {-1, 0});
        tutAnchor("firing:add");
        if (addHit) {
            br = BreechDef(); br.on = true;
            for (auto& b : D.bodies) { if (!b.fixed && br.breech.empty()) br.breech = b.id; if (b.fixed && br.chamber == "world") br.chamber = b.id; }
            commit(); A.tool = Tool::Face;
        }
        return;
    }
    bool hasWell = std::any_of(D.joints.begin(), D.joints.end(), [](const JointDef& j) { return j.type == "magwell"; });
    const bool barrels = barrelCount(D) > 0;
    if (barrels && section("Barrels", true)) {  // drawn with the Barrel tool: chambers and muzzles come from them
        muted("Each barrel has its chamber and muzzle built in. Pick what closes each one's breech (a bolt, a slide, a standing breech).");
        int k = 0;
        for (auto& b : D.bodies) {
            if (!b.barrel.on) continue;
            ImGui::PushID(k);
            ImGui::PushFont(gFontBold, 15.0f); ImGui::Text("%s", b.name.c_str()); ImGui::PopFont();
            ImGui::SameLine(); ImGui::TextDisabled("%s, %s", b.barrel.cart.name.c_str(), b.barrel.rifled ? "rifled" : "smoothbore");
            if (k == 0) { pBody("Closed by", br.breech, false); if (br.breech.empty()) warn("Pick the part that closes this breech."); }
            else if (k - 1 < (int)br.more.size()) {
                if (pBody("Closed by", br.more[k - 1].breech, false)) commit();
                pCheck("Loaded at the start", br.more[k - 1].loaded);
            }
            ImGui::PopID();
            k++;
        }
        if (br.more.size() > 0) {
            int fo = br.fireOrder == "together" ? 1 : 0;
            prop("Fires", "In turn: each pull fires the next loaded barrel.\nAll at once: every loaded barrel goes off together (a volley). The recoil adds up.");
            if (ImGui::Combo("##fireorder", &fo, "One per pull, in turn\0All at once\0")) { br.fireOrder = fo ? "together" : "turn"; editedAny = true; }
        }
    }
    if (!barrels && !br.hasFace) warn("Click where the cartridge head sits against the breech to finish setting it up.");
    if (!barrels && section("Breech and barrel", true)) {
        muted("Tip: the Barrel tool (on the left) draws a barrel with its chamber, bore and muzzle built in.");
        pBody("Breech", br.breech, false);
        if (tipHover()) ImGui::SetTooltip("The bolt or slide: the part whose face holds the cartridge.");
        pBody("Chamber", br.chamber, true);
        tutAnchor("firing:chamber");
        if (tipHover()) ImGui::SetTooltip("The barrel, or whatever holds the chamber.");
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Place breech face", {w2, 0})) { setTool(Tool::Face); A.chamberEdit = -1; }
        tutAnchor("firing:breechface");
        ImGui::SameLine(); if (ImGui::Button("Place muzzle", {w2, 0})) { setTool(Tool::Muzzle); A.chamberEdit = -1; }
        tutAnchor("firing:muzzle");
    }
    if (!barrels && section("Chambers", !br.more.empty())) {
        muted(br.more.empty() ? "One chamber. Add more for a double, a volley gun, a pepperbox: each gets its own breech face and muzzle."
                              : "Every chamber takes the same cartridge. The magazine, if any, feeds the first.");
        int drop = -1;
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        for (size_t k = 0; k < br.more.size(); k++) {
            auto& c = br.more[k];
            ImGui::PushID((int)k);
            ImGui::PushFont(gFontBold, 15.0f); ImGui::Text("Chamber %d", (int)k + 2); ImGui::PopFont();
            if (c.breech.empty()) c.breech = br.breech;
            pBody("Breech##more", c.breech, false);
            if (tipHover()) ImGui::SetTooltip("The part whose face closes this chamber. It can be the same as the first chamber's.");
            pBody("Chamber##more", c.chamber, true);
            if (ImGui::Button("Place breech face##more", {w2, 0})) { setTool(Tool::Face); A.chamberEdit = (int)k; }
            ImGui::SameLine(); if (ImGui::Button("Place muzzle##more", {w2, 0})) { setTool(Tool::Muzzle); A.chamberEdit = (int)k; }
            pCheck("Loaded at the start##more", c.loaded);
            if (ImGui::Button("Remove this chamber", {-1, 0})) drop = (int)k;
            ImGui::PopID();
        }
        if (drop >= 0) { br.more.erase(br.more.begin() + drop); editedAny = true; }
        if (ImGui::Button("Add a chamber", {-1, 0})) {
            ChamberDef c;
            c.breech = br.breech; c.chamber = br.chamber; c.loaded = br.loaded;
            V2 off{0, br.cart.headDia() + 2};  // just below the last one, to start with: place it where you want it
            const V2 f0 = br.more.empty() ? br.face : br.more.back().face, m0 = br.more.empty() ? br.muzzle : br.more.back().muzzle;
            c.face = {f0.x + off.x, f0.y + off.y};
            c.hasMuzzle = br.hasMuzzle; c.muzzle = {m0.x + off.x, m0.y + off.y};
            br.more.push_back(c);
            A.chamberEdit = (int)br.more.size() - 1;
            setTool(Tool::Face);
            editedAny = true;
            toast("Click where the new chamber's cartridge head sits against its breech.");
        }
        if (!br.more.empty()) {
            int fo = br.fireOrder == "together" ? 1 : 0;
            prop("Fires", "In turn: each pull fires the next loaded chamber.\nAll at once: every loaded chamber goes off together (a volley). The recoil adds up.");
            if (ImGui::Combo("##fireorder", &fo, "One per pull, in turn\0All at once\0")) { br.fireOrder = fo ? "together" : "turn"; editedAny = true; }
        }
    }
    if (section("Cartridge", true)) {
        if (barrels) muted("The first barrel's. Each barrel's own is in its panel; a magazine feeds this one.");
        prop("Cartridge");
        float bw = 30 * S();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##cart", br.cart.name.c_str())) {
            auto pick = [&](const Cartridge& c) {
                br.cart = c; br.cartImpulse = true; applyCartridge(br);
                for (auto& b : D.bodies) if (b.barrel.on) { b.barrel.cart = c; break; }   // (with barrels: the first one's)
                for (size_t i = 0; i < D.bodies.size(); i++) if (D.bodies[i].magazine && D.bodies[i].gen.on) generateMagazine(D, (int)i);
                editedAny = true;
            };
            for (auto& c : cartridgePresets()) if (ImGui::Selectable(c.name.c_str(), br.cart.name == c.name)) pick(c);
            if (!A.userCarts.empty()) ImGui::Separator();
            for (auto& c : A.userCarts) if (ImGui::Selectable((c.name + "##u").c_str(), br.cart.name == c.name)) pick(c);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("...", {bw, 0})) { A.editCart = br.cart; A.showCartDesigner = true; }
        tutAnchor("firing:cartridge");
        if (tipHover()) ImGui::SetTooltip("Open the cartridge designer");
        muted("%s at %s, %s", fmtQ(Q::BulletMass, br.cart.payloadMass()).c_str(), fmtQ(Q::Velocity, br.cart.velocity).c_str(), fmtQ(Q::Energy, br.cart.energy()).c_str());
        pCheck("Recoil from it", br.cartImpulse, "Works the recoil impulse out from the bullet and powder gas.");
        if (br.cartImpulse) applyCartridge(br);
        else pNumU("Recoil impulse", br.impulse, 0.1, Q::Impulse);
        if (CFG.stats != StatsDetail::Minimal) { pNum("Pressure time", br.dur, 0.1, "%.2f ms", "How long the chamber pressure pushes on the breech."); br.dur = std::max(0.05, br.dur); }
    }
    if (section("Feeding", true)) {
        const char* modes[] = {"magazine", "internal", "single"};
        int fm = 0;
        for (int i = 0; i < 3; i++) if (br.feedMode == modes[i]) fm = i;
        prop("Feeds from");
        if (ImGui::Combo("##feed", &fm, "Magazine\0Built-in counter (simple)\0Loaded by hand\0")) { br.feedMode = modes[fm]; editedAny = true; }
        tutAnchor("firing:feeding");
        if (fm == 0 && !hasWell && !std::any_of(D.bodies.begin(), D.bodies.end(), [](const BodyDef& q) { return q.magazine && q.magFixed; }))
            warn("No magazine yet: select a part and set Magazine to Removable or Built in.");
        if (fm == 1) { prop("Rounds"); ImGui::DragInt("##rounds", &br.rounds, 0.2f, 0, 1000); markEdit(); }
        pCheck("Physical rounds", br.physicalFeed, "Every round and case is a real part: the magazine lips, feed ramp, chamber,\nextractor, ejector and port you draw decide whether it feeds and ejects.");
        tutAnchor("firing:physical");
        if (!br.physicalFeed) pNumU("Feeds when open", br.feed, 1, Q::Length);
        if (br.physicalFeed) {
            int ex = br.extractor == "top" ? 1 : br.extractor == "bottom" ? 2 : 0;
            prop("Extractor", "Which edge of the case rim the extractor claw grips as the breech draws it out.\n"
                              "The ejector has to hit the case on the other edge to flick it off the claw.\n"
                              "Automatic: the edge away from the magazine (the top, unless the magazine is on top).");
            if (ImGui::Combo("##extr", &ex, "Automatic\0Top\0Bottom\0")) { br.extractor = ex == 1 ? "top" : ex == 2 ? "bottom" : "auto"; editedAny = true; }
        }
        pCheck("Starts loaded", br.loaded, "Begin each test with a round in the chamber.");
    }
    if (section("Fire control", true)) {
        // Feeding, the bolt and extraction are always real parts. The hammer or striker can be built in or drawn.
        int ig = br.ignition == "striker" ? 1 : 0;
        prop("Hammer / striker", "Built in: no parts to draw. It still has to be cocked by the action, so the bolt or slide\n"
                                 "must open far enough between shots.\nDrawn parts: you design the hammer or striker, sear and disconnector\n"
                                 "yourself, and it fires when your part hits the primer hard enough.");
        bool igHit = ImGui::Combo("##ign", &ig, "Built in\0Drawn parts (advanced)\0");
        tutAnchor("firing:control");
        if (igHit) {
            br.ignition = ig ? "striker" : "button";
            if (!ig && br.cockAt <= 0) br.cockAt = std::min(10.0, std::max(2.0, br.feed * 0.5));
            editedAny = true;
        }
        if (!ig) {
            int hk = br.hammer == "hammer" ? 1 : 0;
            prop("Type", "Only changes the wording on the bench.");
            if (ImGui::Combo("##hk", &hk, "Striker\0Hammer\0")) { br.hammer = hk ? "hammer" : "striker"; editedAny = true; }
            pCheck("Open bolt", br.openBolt, "Fires from an open bolt: the sear holds the bolt (or slide) back, open. The trigger lets it go;\n"
                                             "it runs forward, strips a round, chambers it and fires it as it closes (a firing pin fixed in its face).\n"
                                             "In semi it's caught again on the way back; in full-auto it keeps running while you hold the trigger.\n"
                                             "It has to come back past the sear after each shot, or it stays closed (rack it).");
            if (br.openBolt) {
                pNumU("Sear catches at", br.cockAt, 0.5, Q::Length, 1,
                      "How far open the sear holds the bolt. 0: just far enough back to pick up the next round.\n"
                      "The bolt must come back at least this far after a shot to be caught.");
            } else
            pNumU("Cocks after", br.cockAt, 0.5, Q::Length, 1,
                  "How far the bolt or slide must open to cock it again. The shot is only ready once the action\n"
                  "has cycled at least this far. 0 means it is always cocked.");
            br.cockAt = std::max(0.0, br.cockAt);
            const char* modes[] = {"semi", "auto", "burst"};
            int fm = br.fireMode == "auto" ? 1 : br.fireMode == "burst" ? 2 : 0;
            prop("Fire mode");
            if (ImGui::Combo("##fm", &fm, "Semi-automatic\0Full-auto\0Burst\0")) { br.fireMode = modes[fm]; br.autoFire = fm == 1; editedAny = true; }
            if (fm == 2) {
                double b = br.burst;
                if (pNum("Shots per burst", b, 1, "%.0f")) br.burst = std::clamp((int)std::lround(b), 2, 10);
            }
            if (!br.openBolt)
            pCheck("Double action", br.doubleAction, "Pulling the trigger cocks and drops it, so it also fires when it wasn't cocked\n"
                                                     "(revolvers, double-action pistols).");
            if (!br.openBolt) pNum("Lock time", br.lockTime, 0.1, "%.1f ms", "Time from the trigger breaking to the shot, while the hammer or striker falls.");
            br.lockTime = std::max(0.0, br.lockTime);
        } else {
            prop("Hits the primer");
            if (ImGui::BeginCombo("##str", br.striker.empty() ? "Choose..." : bname(br.striker).c_str())) {
                for (auto& o : D.bodies) if (ImGui::Selectable((o.name + "##s" + o.id).c_str(), br.striker == o.id)) { br.striker = o.id; editedAny = true; }
                ImGui::EndCombo();
            }
            if (br.striker.empty()) warn("Choose the striker or firing pin part.");
            pNumU("Needs speed", br.igniteSpeed, 0.1, Q::Velocity, 1, "The tip must reach the primer at least this fast.");
            if (ImGui::Button("Place the tip", {-1, 0})) A.tool = Tool::StrikerTip;
            muted("Semi or full-auto is up to your parts: the sear, disconnector and trigger you draw. For an open bolt, draw a sear that holds the bolt back and a firing pin welded into the bolt face: it fires as the bolt slams home.");
        }
    }
    if (section("Gas", !br.gas.empty())) {
        muted("A port drilled into the bore lets gas out behind the bullet to push a part you drew: a short-stroke tappet, "
              "a long-stroke piston welded to the carrier, or the carrier itself (direct impingement). It pushes until the "
              "piston has moved its stroke and the gas escapes. Give the piston a spring to bring it back.");
        int remove = -1;
        for (size_t gi = 0; gi < br.gas.size(); gi++) {
            GasPortDef& g = br.gas[gi];
            ImGui::PushID((int)gi);
            heading(br.gas.size() > 1 ? ("Gas port " + std::to_string(gi + 1)).c_str() : "Gas port");
            if (!br.more.empty()) {
                int ch = g.chamber;
                prop("Bore");
                std::string cur = ch == 0 ? "1 (the first)" : std::to_string(ch + 1);
                if (ImGui::BeginCombo("##gb", cur.c_str())) {
                    for (int k = 0; k <= (int)br.more.size(); k++)
                        if (ImGui::Selectable(k == 0 ? "1 (the first)" : std::to_string(k + 1).c_str(), ch == k)) { g.chamber = k; editedAny = true; }
                    ImGui::EndCombo();
                }
            }
            int k = std::clamp(g.chamber, 0, (int)br.more.size());
            V2 f = k == 0 ? br.face : br.more[k - 1].face, mz = k == 0 ? br.muzzle : br.more[k - 1].muzzle;
            double L = std::hypot(mz.x - f.x, mz.y - f.y);
            pNumU("Port at", g.at, 1, Q::Length, 0, "How far along the bore from the breech face. Nearer the chamber the gas is hotter and\n"
                                                    "stronger (and comes sooner); nearer the muzzle, weaker. Past the muzzle: nothing.");
            g.at = std::max(0.0, g.at);
            if (L > 1) muted("The bore is %s long.", fmtQ(Q::Length, L, 0).c_str());
            if (L > 1 && g.at > L) warn("The port is past the muzzle: no gas reaches it.");
            pNumU("Port size", g.portDia, 0.1, Q::Length, 1, "The hole's diameter. Bigger lets the gas fill the cylinder faster: a harder push.");
            g.portDia = std::clamp(g.portDia, 0.3, 10.0);
            prop("Pushes", "The part the gas drives: the piston, or the carrier itself.");
            if (ImGui::BeginCombo("##gp", g.piston.empty() ? "Choose..." : bname(g.piston).c_str())) {
                for (auto& o : D.bodies) if (!o.fixed && !o.magazine && ImGui::Selectable((o.name + "##g" + o.id).c_str(), g.piston == o.id)) { g.piston = o.id; editedAny = true; }
                ImGui::EndCombo();
            }
            if (g.piston.empty()) warn("Choose the part the gas pushes.");
            pNumU("Piston size", g.pistonDia, 0.1, Q::Length, 1, "The piston's diameter: the gas pressure on its face is the push. Bigger pushes harder.");
            g.pistonDia = std::clamp(g.pistonDia, 2.0, 60.0);
            pNumU("Gas stroke", g.stroke, 0.5, Q::Length, 1, "How far the piston moves before the gas gets past it and escapes. Longer: a longer push.");
            g.stroke = std::clamp(g.stroke, 0.5, 300.0);
            pNum("Push angle", g.dir, 1, "%.0f\xc2\xb0", "Which way the gas pushes, turned from straight back along the bore. 0: straight back.");
            if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
            if (ImGui::Button("Remove this port", {-1, 0})) remove = (int)gi;
            ImGui::PopID();
        }
        if (remove >= 0) { br.gas.erase(br.gas.begin() + remove); editedAny = true; }
        if (ImGui::Button("Add a gas port", {-1, 0})) {
            GasPortDef g;
            double L = br.hasMuzzle ? std::hypot(br.muzzle.x - br.face.x, br.muzzle.y - br.face.y) : 200;
            g.at = std::round(L * 0.6);
            br.gas.push_back(g);
            editedAny = true;
        }
        tutAnchor("firing:gas");
    }
    if (section("Made for NPCs", D.npcGun.on)) {
        auto& g = D.npcGun;
        if (pCheck("NPC gun", g.on, "For NPCs to carry, like the simple guns enemies carry in some games.\n"
                                    "It never needs reloading and never runs out, isn't worked part by part, and isn't animated:\n"
                                    "it just fires its cartridge's bullet this fast and this wide. Players can't take one.")) editedAny = true;
        if (g.on) {
            pNum("Rate of fire", g.rpm, 10, "%.0f a minute", "How fast it fires while the NPC holds the trigger."); g.rpm = std::clamp(g.rpm, 1.0, 3000.0);
            if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
            double b = g.burst;
            if (pNum("Shots per pull", b, 1, "%.0f", "1: single shots. More: bursts.")) g.burst = std::clamp((int)std::lround(b), 1, 50);
            if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
            pNum("Spread", g.spread, 0.1, "%.1f\xc2\xb0", "How wide its shots go. NPC guns are meant to be a bit bad: about 2 for a rifle, 5 for a cheap SMG.");
            g.spread = std::clamp(g.spread, 0.0, 45.0);
            if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
            pNum("Damage to you", g.damage, 1, g.damage > 0 ? "%.0f a hit" : "from its bullet", "How much of your 100 health a hit takes.\n"
                 "0: worked out from its cartridge's bullet (about 8 for a 9 mm, 30 for an intermediate rifle round, 55 for a full-power one).");
            g.damage = std::clamp(g.damage, 0.0, 1000.0);
            if (ImGui::IsItemDeactivatedAfterEdit()) editedAny = true;
            muted("Give it to an NPC in the NPC editor (In a fight > With a gun). The parts you drew are just its look.");
        } else muted("Off: a normal gun, for players.");
    }
    ImGui::Dummy({0, 6 * S()});
    if (dangerButton("Remove firing system", {-1, 0})) { br.on = false; commit(); }
}

// ---------------------------------------------------------------- file
// ---------------------------------------------------------------- files (in the top bar)
// The blueprint browser: your saved guns and the examples, with Open, Rename and Delete.
namespace {
struct BpInfo { std::string file, name, cart, held; int parts = 0; bool ok = false; std::string err, when; };
BpInfo readBlueprint(const fs::path& f) {
    BpInfo i; i.file = f.string();
    std::ifstream in(f); std::stringstream ss; ss << in.rdbuf();
    Design d;
    i.ok = designFromJson(ss.str(), d, &i.err);
    if (i.ok) {
        i.name = d.name; i.parts = (int)d.bodies.size();
        i.cart = d.breech.on ? d.breech.cart.name : "";
        i.held = heldWith(d);
    }
    std::error_code ec;
    auto t = fs::last_write_time(f, ec);
    if (!ec) {
        auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sys);
        char b[64]; std::strftime(b, sizeof b, "%d %b %Y, %H:%M", std::localtime(&tt));
        i.when = b;
    }
    return i;
}
void openFile(const fs::path& f) {
    std::ifstream in(f); std::stringstream ss; ss << in.rdbuf();
    Design nd; std::string err;
    std::string n = f.stem().string();
    if (designFromJson(ss.str(), nd, &err)) {
        std::string vn = blueprintVersionNote(nd);
        loadDesign(nd, vn.empty() ? "Opened " + n + ". Undo brings back what you had." : vn);
        std::snprintf(A.saveName, sizeof A.saveName, "%s", n.c_str());  // Ctrl+S saves back to this file
        A.showOpenWindow = false;
    } else toast("Couldn't read " + n + ": " + err);
}
}  // namespace
static void blueprintWindow() {
    static std::string picked;          // the file picked in the list
    static BpInfo info;
    static char filter[64] = "", renameTo[128] = "";
    static bool renaming = false, focusRename = false;
    float w = std::min(760 * S(), GetScreenWidth() - 40.f), h = std::min(540 * S(), GetScreenHeight() - 60.f);
    ImGui::SetNextWindowPos({GetScreenWidth() / 2.f, GetScreenHeight() / 2.f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({w, h});
    if (!ImGui::IsPopupOpen("##bpwin")) ImGui::OpenPopup("##bpwin");   // a modal window: on top of everything, the rest dimmed
    if (!ImGui::BeginPopupModal("##bpwin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::PushFont(gFontBold, 20.0f); ImGui::TextUnformatted("Open a blueprint"); ImGui::PopFont();
    ImGui::TextDisabled("Your saved guns are in your Blueprints folder (Settings > Files shows where).");
    ImGui::Dummy({0, 4 * S()});
    std::vector<fs::path> files;
    if (fs::exists(GUNS_DIR))
        for (auto& e : fs::directory_iterator(GUNS_DIR)) if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end(), [](const fs::path& x, const fs::path& y) {
        std::error_code e1, e2; return fs::last_write_time(x, e1) > fs::last_write_time(y, e2); });   // newest first
    float listW = w * 0.46f, foot = ImGui::GetFrameHeightWithSpacing() + 12 * S();
    // the list
    ImGui::BeginChild("##bplist", {listW, -foot}, ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##bpfilter", "Search", filter, sizeof filter);
    std::string f = filter; std::transform(f.begin(), f.end(), f.begin(), ::tolower);
    ImGui::SeparatorText("Your blueprints");
    if (files.empty()) muted("None saved yet. Save in the top bar keeps your design here.");
    for (auto& p : files) {
        std::string n = p.stem().string(), low = n;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (!f.empty() && low.find(f) == std::string::npos) continue;
        bool sel = picked == p.string();
        if (ImGui::Selectable(n.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (!sel) { picked = p.string(); info = readBlueprint(p); renaming = false; }
            if (ImGui::IsMouseDoubleClicked(0)) openFile(p);
        }
    }
    std::vector<fs::path> atts;
    if (fs::exists(ATTACH_DIR))
        for (auto& e : fs::directory_iterator(ATTACH_DIR)) if (e.path().extension() == ".json") atts.push_back(e.path());
    std::sort(atts.begin(), atts.end());
    ImGui::SeparatorText("Your attachments");
    if (atts.empty()) muted("None yet: New > A new attachment.");
    for (auto& p : atts) {
        std::string n = p.stem().string(), low = n;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (!f.empty() && low.find(f) == std::string::npos) continue;
        bool sel = picked == p.string();
        if (ImGui::Selectable((n + "##att").c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (!sel) { picked = p.string(); info = readBlueprint(p); renaming = false; }
            if (ImGui::IsMouseDoubleClicked(0)) openFile(p);
        }
    }
    ImGui::SeparatorText("Examples");
    for (auto& ex : presetList()) {
        std::string low = ex.label; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (!f.empty() && low.find(f) == std::string::npos) continue;
        if (ImGui::Selectable(ex.label)) { loadDesign(loadPreset(ex.key), "Opened the example. Undo brings back what you had."); A.saveName[0] = 0; A.showOpenWindow = false; }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    // the one picked: what it is, and what to do with it
    ImGui::BeginChild("##bpinfo", {0, -foot}, ImGuiChildFlags_Borders);
    bool exists = !picked.empty() && fs::exists(picked);
    if (!exists) { picked.clear(); muted("Pick a blueprint on the left to see it here. Double-click opens it."); }
    else {
        fs::path p(picked);
        ImGui::PushFont(gFontBold, 17.0f); ImGui::TextUnformatted(p.stem().string().c_str()); ImGui::PopFont();
        if (!info.ok) ImGui::TextColored(colBad(), "This file can't be read: %s", info.err.c_str());
        else {
            if (!info.name.empty() && info.name != p.stem().string()) ImGui::TextDisabled("Design name: %s", info.name.c_str());
            ImGui::TextDisabled("%d part%s", info.parts, info.parts == 1 ? "" : "s");
            if (!info.cart.empty()) ImGui::TextDisabled("Fires %s", info.cart.c_str());
            if (!info.held.empty()) ImGui::TextDisabled("Held with %s", info.held.c_str());
        }
        if (!info.when.empty()) ImGui::TextDisabled("Saved %s", info.when.c_str());
        ImGui::Dummy({0, 10 * S()});
        ImGui::BeginDisabled(!info.ok);
        if (accentButton("Open", true, {-1, 32 * S()})) openFile(p);
        ImGui::EndDisabled();
        ImGui::Dummy({0, 4 * S()});
        if (!renaming) {
            if (ImGui::Button("Rename", {-1, 0})) { renaming = true; focusRename = true; std::snprintf(renameTo, sizeof renameTo, "%s", p.stem().string().c_str()); }
        } else {
            ImGui::SetNextItemWidth(-1);
            if (focusRename) { ImGui::SetKeyboardFocusHere(); focusRename = false; }   // ready to type straight away
            bool go = ImGui::InputText("##bpren", renameTo, sizeof renameTo, ImGuiInputTextFlags_EnterReturnsTrue);
            float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
            if (ImGui::Button("Rename it", {w2, 0}) || go) {
                std::string nn = safeFileName(renameTo);
                fs::path to = fs::path(GUNS_DIR) / (nn + ".json");
                std::error_code ec;
                if (nn.empty()) toast("Give it a name.");
                else if (fs::exists(to) && to != p) toast("There's already a blueprint called " + nn + ".");
                else {
                    fs::rename(p, to, ec);
                    if (ec) toast("Couldn't rename it.");
                    else {
                        if (std::string(A.saveName) == p.stem().string()) std::snprintf(A.saveName, sizeof A.saveName, "%s", nn.c_str());
                        picked = to.string(); info = readBlueprint(to); renaming = false;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {w2, 0})) renaming = false;
        }
        ImGui::Dummy({0, 4 * S()});
        if (dangerButton("Delete", {-1, 0})) ImGui::OpenPopup("##bpdel");
        if (ImGui::BeginPopupModal("##bpdel", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::Text("Delete %s?", p.stem().string().c_str());
            ImGui::TextDisabled("The file is removed from your Blueprints folder. This can't be undone.");
            ImGui::Dummy({0, 4 * S()});
            if (dangerButton("Delete it", {140 * S(), 0})) {
                std::error_code ec;
                fs::remove(p, ec);
                if (ec) toast("Couldn't delete it.");
                else { toast("Deleted " + p.stem().string() + "."); if (std::string(A.saveName) == p.stem().string()) A.saveName[0] = 0; picked.clear(); }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep it", {140 * S(), 0}) || IsKeyPressed(KEY_ESCAPE)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    if (ImGui::Button("Close", {120 * S(), 0}) || (IsKeyPressed(KEY_ESCAPE) && !ImGui::IsPopupOpen("##bpdel"))) A.showOpenWindow = false;
    ImGui::SameLine();
    if (ImGui::Button("Open the guns folder", {200 * S(), 0})) { std::error_code ec; fs::create_directories(GUNS_DIR, ec); openFolder(fs::absolute(GUNS_DIR).string()); }
    if (!A.showOpenWindow) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
void fileButtons() {
    float sz = TOOLBAR_H - 14 * S();
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save", {0, sz})) saveBlueprint();
    if (tipHover()) ImGui::SetTooltip("Save to your Blueprints folder (Ctrl+S, from anywhere).\nSaving as: %s", A.saveName[0] ? A.saveName : safeFileName(A.design.name).c_str());
    tutAnchor("file:save");
    ImGui::SameLine(0, 2);
    if (ImGui::Button("Save as...", {0, sz})) ImGui::OpenPopup("##saveas");
    if (ImGui::BeginPopup("##saveas")) {
        ImGui::TextDisabled("Save as");
        static char nm[sizeof A.saveName];
        if (ImGui::IsWindowAppearing()) { std::snprintf(nm, sizeof nm, "%s", A.saveName[0] ? A.saveName : safeFileName(A.design.name).c_str()); ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(240 * S());
        bool go = ImGui::InputText("##fname", nm, sizeof nm, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Save") || go) { std::snprintf(A.saveName, sizeof A.saveName, "%s", nm); saveBlueprint(); ImGui::CloseCurrentPopup(); }
        muted("Saved in your Blueprints folder (Settings > Files shows where).");
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, 2);
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open", {0, sz})) A.showOpenWindow = true;
    if (tipHover()) ImGui::SetTooltip("Your saved blueprints and the examples: open, rename or delete them.");
    tutAnchor("file:open");
    ImGui::SameLine(0, 2);
    if (ImGui::Button(ICON_FA_PUZZLE_PIECE "  Parts", {0, sz})) ImGui::OpenPopup("##parts");
    if (tipHover()) ImGui::SetTooltip("Your parts library: parts and assemblies you saved (Save to my parts, in a part's panel).");
    tutAnchor("file:parts");
    if (ImGui::BeginPopup("##parts")) {
        auto names = libraryParts();
        ImGui::TextDisabled("Your parts");
        if (names.empty()) muted("None yet. Select a part and press Save to my parts in its panel.");
        for (auto& n : names) if (ImGui::Selectable(n.c_str())) { if (insertLibraryPart(n)) toast("Added " + n + ". It's selected: drag it into place."); else toast("Couldn't read " + n + "."); }
        ImGui::Separator();
        if (ImGui::Selectable("Paste (Ctrl+V)")) pasteClipboard();
        if (ImGui::Selectable("Open the parts folder")) { std::string p = std::filesystem::absolute(PARTS_DIR).string(); openFolder(p); }
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, 2);
    if (ImGui::Button(ICON_FA_FILE "  New", {0, sz})) ImGui::OpenPopup("##new");
    if (tipHover()) ImGui::SetTooltip("Start a new gun on a blank frame, or a new attachment. Undo brings back what you had.");
    if (ImGui::BeginPopup("##new")) {
        if (ImGui::Selectable("A new gun")) {
            loadDesign(loadPreset("blank"), "A new design. Undo brings back what you had (and your last one is still in Open if you saved it).");
            A.saveName[0] = 0;
        }
        if (ImGui::Selectable("A new attachment")) {
            Design d; d.name = "New attachment"; d.attach.on = true; d.attach.job = "optic";
            loadDesign(d, "A new attachment: draw it with the same tools as a gun. What it is and how it mounts are in the Attachment tab.");
            A.saveName[0] = 0;
        }
        if (tipHover()) ImGui::SetTooltip("Optics, grips, bipods, bayonets, lights, muzzle devices, rail adapters...\nSaved in your Attachments folder, apart from your guns, and fitted to guns' rails and mounts.");
        ImGui::EndPopup();
    }
    tutAnchor("file:new");
    if (A.showOpenWindow) blueprintWindow();
}

void buildPanel() {
    panelHeader();
    auto& D = A.design;
    // jump to Inspect whenever something new is picked in the view
    static int lastKind = 0, lastIdx = -1;
    bool newSel = A.sel.kind != 0 && (A.sel.kind != lastKind || A.sel.idx != lastIdx) && !gPickedInOutline;
    gPickedInOutline = false;
    lastKind = A.sel.kind; lastIdx = A.sel.idx;
    ImGui::Dummy({0, 2 * S()});
    static bool envTab = false;
    if (!envTab) { envTab = true; if (std::getenv("GUNSMITH_TAB")) gPanelTab = std::atoi(std::getenv("GUNSMITH_TAB")); }  // (automated screenshots)
    auto tabFlag = [&](int i) { if (gPanelTab == i) { gPanelTab = -1; return (int)ImGuiTabItemFlags_SetSelected; } return 0; };
    if (ImGui::BeginTabBar("##designtabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        bool inspOpen = ImGui::BeginTabItem("Inspect", nullptr, (newSel ? ImGuiTabItemFlags_SetSelected : 0) | tabFlag(0));
        tutAnchor("tab:Inspect");
        if (inspOpen) {
            ImGui::BeginChild("##inspect", {0, 0});
            if (A.tool == Tool::Carve) carveToolPanel();
            if (A.tool == Tool::Engrave) engraveToolPanel();
            if (A.tool == Tool::Scale) scaleToolPanel();
            if (A.tool == Tool::Measure || (A.measureKeep && A.measured)) measureToolPanel();
            if (A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size()) inspectPart(D.bodies[A.sel.idx]);
            else if (A.sel.kind == 2 && A.sel.idx < (int)D.joints.size()) inspectJoint(D.joints[A.sel.idx]);
            else {
                ImGui::Dummy({0, 8 * S()});
                ImGui::TextUnformatted("Nothing selected");
                muted("Click a part or a joint in the view, or pick one from the Outline.");
                ImGui::Dummy({0, 8 * S()});
                ImGui::PushFont(gFontBold, 15.0f); ImGui::TextUnformatted("Where it's held"); ImGui::PopFont();
                heldSummary();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        bool outOpen = ImGui::BeginTabItem("Outline", nullptr, tabFlag(1)); tutAnchor("tab:Outline");
        if (outOpen) { outlineTab(); ImGui::EndTabItem(); }
        if (D.attach.on) {   // an attachment: what it is and how it mounts, instead of a firing system
            bool attOpen = ImGui::BeginTabItem("Attachment", nullptr, tabFlag(2));
            if (attOpen) { ImGui::BeginChild("##attach", {0, 0}); attachmentTab(); ImGui::EndChild(); ImGui::EndTabItem(); }
        } else {
            bool fireOpen = ImGui::BeginTabItem("Firing", nullptr, tabFlag(2)); tutAnchor("tab:Firing");
            if (fireOpen) { ImGui::BeginChild("##firing", {0, 0}); firingTab(); ImGui::EndChild(); ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    ImGui::End();
}
