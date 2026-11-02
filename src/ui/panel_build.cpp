// Gunsmith3D - Designer panel. Four tabs: Inspect (what's selected), Outline (every part and joint),
// Firing (breech, cartridge, feeding, ignition) and File (save, load, examples). Each shows only what matters
// for the job at hand; less-used settings fold away.
#include "carve.hpp"
#include "app/app.hpp"
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
static void loadDesign(const Design& nd, const std::string& msg) {
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
    for (int k = 0; k < (int)b.carves.size(); k++) {
        auto& c = b.carves[k];
        std::string t = std::string(shapeName(c.shape)) + " " + fmtQ(Q::Length, c.w);
        if (c.shape != "circle" || c.h != c.w) t += " x " + fmtQ(Q::Length, c.h);
        t += c.depth > 0 ? ", " + fmtQ(Q::Length, c.depth) + " deep" : ", right through";
        if (ImGui::Selectable((t + "##cv" + std::to_string(k)).c_str(), A.selCarve == k)) A.selCarve = A.selCarve == k ? -1 : k;
        if (k == 0) tutAnchor("carves:list");
    }
    if (A.selCarve < 0) { muted("Click one to change it. Del removes the selected one."); return; }
    CarveDef& c = b.carves[A.selCarve];
    ImGui::Spacing();
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
    float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("Duplicate", {w2, 0})) { CarveDef d = c; d.at.x += std::max(c.w, c.h) + 2; b.carves.push_back(d); A.selCarve = (int)b.carves.size() - 1; editedAny = true; }
    ImGui::SameLine();
    if (ImGui::Button("Remove", {w2, 0})) { b.carves.erase(b.carves.begin() + A.selCarve); A.selCarve = -1; editedAny = true; }
}

// ---------------------------------------------------------------- inspect: a part

// ---------------------------------------------------------------- where it's held (posed on the bench)
static void heldSummary() {
    std::string h = heldWith(A.design);
    if (h.empty()) muted("Not posed yet: on the Test bench, press Pose to place the hands and shoulder.");
    else muted("Held with %s%s. Change it with Pose on the Test bench.", h.c_str(), A.design.pose.butt.on ? ", against the shoulder" : "");
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
                autoMass = vol * 7.85e-3;
            }
            pNumU("Mass", b.mass, 10, Q::PartMass, -1, "0 works it out from the shape, as steel.");
            tutAnchor("part:mass");
            if (b.mass <= 0) muted("Worked out from the shape: %s.", fmtQ(Q::PartMass, autoMass).c_str());
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
        if (ImGui::Button("Copy to the other side", {-1, 0})) {
            BodyDef cpy = b;
            cpy.id = uid(); cpy.name += " (other side)"; cpy.z = -b.z; cpy.magazine = false; cpy.weld.clear();
            carve::flipCarvesZ(cpy);
            D.bodies.push_back(cpy);
            A.sel = {1, (int)D.bodies.size() - 1, -1};
            commit();
            return;
        }
        if (tipHover()) ImGui::SetTooltip("Makes the matching part on the far side of the gun: side walls, rails, grip panels.");
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

    carvesSection(b);

    if (section("Surface", !b.texture.empty())) {
        prop("Texture", "A picture laid over the part. Its colour tints the picture: white shows it as it is.");
        ImGui::SetNextItemWidth(-1);
        if (texturePicker(b.texture)) editedAny = true;
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
            double g = R.grooves;
            if (pNum("Grooves", g, 1, "%.0f")) R.grooves = std::clamp((int)std::lround(g), 2, 16);
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
        }
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
            pNum("Push direction", b.ctrlDir, 15, "%.0f\xc2\xb0", "0 forward, 90 down, 180 back.");
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
            if (mi >= 0 && D.joints[mi].path.size() >= 2) {
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
            if (b.gen.on) {
                bool regen = false;
                int st = b.gen.stack - 1;
                prop("Stack");
                if (ImGui::Combo("##stack", &st, "Single\0Double\0")) { b.gen.stack = st + 1; regen = true; }
                if (b.gen.stack == 2) {
                    int fp = b.doubleFeed ? 1 : 0;
                    prop("Feeds from", "Single position: the two columns taper into one, and every round feeds from the centre.\n"
                                       "Straight in, so short pistol rounds like it; the taper's friction costs some spring (about a fifth).\n"
                                       "Double position: rounds feed alternately from the left and right lips.\n"
                                       "Easy on the spring, but each round comes in at an angle sideways: fine for long rifle rounds,\n"
                                       "marginal for 9 mm, and fat short rounds like .45 ACP may not make it into the chamber.");
                    if (ImGui::Combo("##feedpos", &fp, "Single position (centre)\0Double position (alternating)\0")) { b.doubleFeed = fp == 1; editedAny = true; }
                }
                float curve = (float)b.gen.curve;
                prop("Curve");
                if (ImGui::SliderFloat("##curve", &curve, -15, 80, "%.0f\xc2\xb0")) { b.gen.curve = curve; regen = true; } typeable();
                if (pNumU("Wall", b.gen.wall, 0.1, Q::Length, CFG.units == Units::Metric ? 1 : 3)) { b.gen.wall = std::clamp(b.gen.wall, 0.6, 4.0); regen = true; }
                static int lastCap = -1;
                if (lastCap != b.magCap) { lastCap = b.magCap; regen = true; }
                if (regen) { generateMagazine(D, A.sel.idx); editedAny = true; }
                muted("%s, %d rounds: %s long.", D.breech.cart.name.c_str(), b.magCap, fmtQ(Q::Length, b.gen.H, 0).c_str());
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
            if (j.guide.empty() && j.rest > 4 * j.coilDia) warn("Longer than four times its diameter with no guide rod: it will buckle. Draw a rod part and pick it under Coil.");
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
            pCheck("Double action", br.doubleAction, "Pulling the trigger cocks and drops it, so it also fires when it wasn't cocked\n"
                                                     "(revolvers, double-action pistols).");
            pNum("Lock time", br.lockTime, 0.1, "%.1f ms", "Time from the trigger breaking to the shot, while the hammer or striker falls.");
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
            muted("Semi or full-auto is up to your parts: the sear, disconnector and trigger you draw.");
        }
    }
    ImGui::Dummy({0, 6 * S()});
    if (dangerButton("Remove firing system", {-1, 0})) { br.on = false; commit(); }
}

// ---------------------------------------------------------------- file
// ---------------------------------------------------------------- files (in the top bar)
static void openList() {
    ImGui::TextDisabled("Your blueprints");
    std::vector<fs::path> files;
    if (fs::exists(GUNS_DIR))
        for (auto& e : fs::directory_iterator(GUNS_DIR)) if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) muted("None saved yet.");
    for (auto& f : files) {
        std::string n = f.stem().string();
        if (!ImGui::Selectable(n.c_str())) continue;
        std::ifstream in(f); std::stringstream ss; ss << in.rdbuf();
        Design nd; std::string err;
        if (designFromJson(ss.str(), nd, &err)) {
            std::string vn = blueprintVersionNote(nd);
            loadDesign(nd, vn.empty() ? "Opened " + n + ". Undo brings back what you had." : vn);
            std::snprintf(A.saveName, sizeof A.saveName, "%s", n.c_str());  // Ctrl+S saves back to this file
        }
        else toast("Couldn't read " + n + ": " + err);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Examples");
    for (auto& p : presetList())
        if (ImGui::Selectable(p.label)) { loadDesign(loadPreset(p.key), "Opened the example. Undo brings back what you had."); A.saveName[0] = 0; }
}
void fileButtons() {
    float sz = TOOLBAR_H - 14 * S();
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save", {0, sz})) saveBlueprint();
    if (tipHover()) ImGui::SetTooltip("Save to user/guns (Ctrl+S, from anywhere).\nSaving as: %s", A.saveName[0] ? A.saveName : safeFileName(A.design.name).c_str());
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
        muted("Saved in the game folder, under user/guns.");
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, 2);
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open", {0, sz})) ImGui::OpenPopup("##open");
    tutAnchor("file:open");
    if (ImGui::BeginPopup("##open")) { openList(); ImGui::EndPopup(); }
    ImGui::SameLine(0, 2);
    if (ImGui::Button(ICON_FA_FILE "  New", {0, sz})) {
        loadDesign(loadPreset("blank"), "A new design. Undo brings back what you had (and your last one is still in Open if you saved it).");
        A.saveName[0] = 0;
    }
    if (tipHover()) ImGui::SetTooltip("Start a new design on a blank frame. Undo brings back what you had.");
    tutAnchor("file:new");
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
        bool fireOpen = ImGui::BeginTabItem("Firing", nullptr, tabFlag(2)); tutAnchor("tab:Firing");
        if (fireOpen) { ImGui::BeginChild("##firing", {0, 0}); firingTab(); ImGui::EndChild(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    ImGui::End();
}
