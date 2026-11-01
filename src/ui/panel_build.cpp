// Gunsmith3D - Designer panel. Four tabs: Inspect (what's selected), Outline (every part and joint),
// Firing (breech, cartridge, feeding, ignition) and File (save, load, examples). Each shows only what matters
// for the job at hand; less-used settings fold away.
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "app/keys.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "imgui.h"

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
    A.design = nd; A.lastSnap = designToJson(A.design); A.sel = {}; A.selVs.clear();
    save(); fitView();
    toast(msg);
}

// ---------------------------------------------------------------- inspect: a part
static void inspectPart(BodyDef& b) {
    auto& D = A.design;
    // colour swatch and name on one line
    Color c = hexColor(b.color);
    float col[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f};
    if (ImGui::ColorEdit3("##col", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) { b.color = colorHex(col); editedAny = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    inputText("##pname", b.name);

    if (section("Part", true)) {
        pCheck("Fixed", b.fixed, "Fixed parts are the frame: they don't move.");
        tutAnchor("part:fixed");
        bool solid = b.layer != "none";
        bool solidHit = pCheck("Solid", solid, "Solid parts push on each other wherever they overlap, in the side view and in depth.\nParts that aren't solid (grip panels, covers) pass through everything.");
        tutAnchor("part:solid");
        if (solidHit)
            b.layer = solid ? "ABC" : "none";
        pNumU("Thickness", b.thick, 1, Q::Length);
        tutAnchor("part:thickness");
        pNumU("Depth", b.z, 0.5, Q::Length, -1, "Where the part sits across the gun: 0 is on the bore, positive is the near side.\nParts only touch where their depths overlap.");
        tutAnchor("part:depth");
        if (!b.fixed) {
            double area = std::fabs(sim::polyArea(b.pts)), autoMass = area * b.thick * 7.85e-3;
            pNumU("Mass", b.mass, 10, Q::PartMass, -1, "0 works it out from the shape, as steel.");
            tutAnchor("part:mass");
            if (b.mass <= 0) muted("Worked out from the shape: %s.", fmtQ(Q::PartMass, autoMass).c_str());
        }
        if (ImGui::Button("Copy to the other side", {-1, 0})) {
            BodyDef cpy = b;
            cpy.id = uid(); cpy.name += " (other side)"; cpy.z = -b.z; cpy.magazine = false;
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
            pNumU("X", b.pts[v].x, 0.5, Q::Length, lenDec());
            pNumU("Y", b.pts[v].y, 0.5, Q::Length, lenDec());
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
        if (splitHit) {
            std::vector<int> made;
            for (int k = (int)edges.size() - 1; k >= 0; k--) {
                int nv = splitEdge(b, edges[k]);
                for (int& m : made) if (m >= nv) m++;
                made.push_back(nv);
            }
            A.selVs = made; A.sel.v = made.size() == 1 ? made[0] : -1; editedAny = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && CFG.tooltips)
            ImGui::SetTooltip("%s", edges.empty() ? "Click an edge's diamond to select it, then split it in half.\nOr right-click or double-click the diamond."
                                            : "Adds a corner halfway along the selected edge (right-click or double-click a diamond does the same).");
        ImGui::SameLine(); if (ImGui::Button("Split all edges", {w2, 0})) { subdivideBody(b); A.selVs.clear(); A.sel.v = -1; editedAny = true; }
        if (tipHover()) ImGui::SetTooltip("Adds a corner halfway along every edge, for finer shaping.");
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
        tutAnchor("shape:smooth");
        if (tipHover()) ImGui::SetTooltip("Drag, or Ctrl+click to type a value.");
    }

    if (section("Motion", b.rollOn || b.damp > 0)) {
        pCheck("Turns on the bore", b.rollOn, "For rotating bolts: the part can also turn about the bore axis.");
        if (b.rollOn) {
            pNum("From", b.rollMin, 5, "%.0f\xc2\xb0");
            pNum("To", b.rollMax, 5, "%.0f\xc2\xb0");
            pNumU("Handle length", b.handle, 1, Q::Length, -1, "Draws a bolt handle you can grab and drag on the bench. 0 for none.");
        }
        pNum("Damping", b.damp, 0.5, "%.1f /s", "Friction-like slowing of this part on its own: buffers, heavy grease.");
    }

    if (section("Control", b.ctrl != "none")) {
        const char* kinds[] = {"none", "button", "lever", "trigger"};
        int ki = 0;
        for (int i = 0; i < 4; i++) if (b.ctrl == kinds[i]) ki = i;
        prop("Type");
        if (ImGui::Combo("##ctrl", &ki, "Not a control\0Button\0Lever\0Trigger\0")) {
            b.ctrl = kinds[ki]; editedAny = true;
            if (ki > 0 && b.ctrlPt.x == 0 && b.ctrlPt.y == 0) b.ctrlPt = centroidOf(b);
            if (ki == 3 && b.key == "none") b.key = "fire";
        }
        if (b.ctrl != "none") {
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
                if (ImGui::Combo("##rstyle", &st, "Side by side\0Nose to tail\0")) { b.roundStyle = st ? "line" : "side"; editedAny = true; }
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
                if (st == 1 && D.breech.physicalFeed)
                    muted("For now a tube feeds the simple way: the round reaches the chamber once the action opens far enough. Feeding through a carrier you draw comes next.");
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
                if (!j.rel.empty()) pNumU("when it moves", j.releaseAt, 0.5, Q::Length);
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
                if (ImGui::SliderFloat("##curve", &curve, -15, 80, "%.0f\xc2\xb0")) { b.gen.curve = curve; regen = true; }
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
                  "Too low and the breech face rides over its rim instead of pushing it (failure to feed).");
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
    if (j.type == "pin" || j.type == "weld") { pBody("Part", j.a, false); pBody("Joined to", j.b, true); }
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
        pNumU("Catches at", j.catchAt, 0.5, Q::Length);
        pBody("Released by", j.rel, false);
        pNumU("at travel", j.releaseAt, 0.5, Q::Length);
    }
    ImGui::Dummy({0, 6 * S()});
    if (dangerButton("Delete joint", {-1, 0})) deleteSel();
}

// ---------------------------------------------------------------- outline
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
            if (ImGui::Selectable("##row", sel, 0, {0, h + 4 * S()})) A.sel = {1, (int)i, -1};
            ImGui::PopID();
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled({p.x + 6 * S(), p.y + (h + 4 * S()) / 2}, 4 * S(), IM_COL32(c.r, c.g, c.b, 255));
            dl->AddText({p.x + 18 * S(), p.y + 2 * S()}, ImGui::GetColorU32(ImGuiCol_Text), b.name.c_str());
            const char* tag = b.fixed ? "frame" : b.magazine ? "magazine" : b.rollOn ? "turns" : b.layer == "none" ? "not solid" : "";
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
            if (ImGui::Selectable(d.c_str(), A.sel.kind == 2 && A.sel.idx == (int)i)) A.sel = {2, (int)i, -1};
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
    if (!br.hasFace) warn("Click where the cartridge head sits against the breech to finish setting it up.");
    if (section("Breech and barrel", true)) {
        pBody("Breech", br.breech, false);
        if (tipHover()) ImGui::SetTooltip("The bolt or slide: the part whose face holds the cartridge.");
        pBody("Chamber", br.chamber, true);
        tutAnchor("firing:chamber");
        if (tipHover()) ImGui::SetTooltip("The barrel, or whatever holds the chamber.");
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Place breech face", {w2, 0})) setTool(Tool::Face);
        tutAnchor("firing:breechface");
        ImGui::SameLine(); if (ImGui::Button("Place muzzle", {w2, 0})) setTool(Tool::Muzzle);
        tutAnchor("firing:muzzle");
    }
    if (section("Cartridge", true)) {
        prop("Cartridge");
        float bw = 30 * S();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##cart", br.cart.name.c_str())) {
            auto pick = [&](const Cartridge& c) {
                br.cart = c; br.cartImpulse = true; applyCartridge(br);
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
        muted("%s at %s, %s", fmtQ(Q::BulletMass, br.cart.bulletMass).c_str(), fmtQ(Q::Velocity, br.cart.velocity).c_str(), fmtQ(Q::Energy, br.cart.energy()).c_str());
        pCheck("Recoil from it", br.cartImpulse, "Works the recoil impulse out from the bullet and powder gas.");
        if (br.cartImpulse) applyCartridge(br);
        else pNumU("Recoil impulse", br.impulse, 0.1, Q::Impulse);
        if (CFG.stats != StatsDetail::Minimal) pNum("Pressure time", br.dur, 0.1, "%.2f ms", "How long the chamber pressure pushes on the breech.");
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
static void fileTab() {
    auto& D = A.design;
    if (section("Save", true)) {
        float bw = 70 * S();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##fname", "Blueprint name", A.saveName, sizeof A.saveName);
        ImGui::SameLine();
        if (ImGui::Button("Save", {bw, 0}) && A.saveName[0]) {
            fs::create_directories("blueprints");
            std::string fn = std::string("blueprints/") + A.saveName + ".json";
            std::ofstream(fn) << designToJson(D, true);
            toast(std::string("Saved ") + A.saveName + ".");
        }
        muted("Your work also saves itself as you go (Settings > Designer).");
    }
    if (section("Your blueprints", true)) {
        std::vector<fs::path> files;
        if (fs::exists("blueprints"))
            for (auto& e : fs::directory_iterator("blueprints")) if (e.path().extension() == ".json") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        if (files.empty()) muted("None saved yet.");
        for (auto& f : files) {
            std::string n = f.stem().string();
            if (!ImGui::Selectable(n.c_str())) continue;
            std::ifstream in(f); std::stringstream ss; ss << in.rdbuf();
            Design nd; std::string err;
            if (designFromJson(ss.str(), nd, &err)) { std::string vn = blueprintVersionNote(nd); loadDesign(nd, vn.empty() ? "Opened " + n + ". Undo brings back what you had." : vn); }
            else toast("Couldn't read " + n + ": " + err);
        }
    }
    if (section("Start from", true)) {
        for (auto& p : presetList())
            if (ImGui::Selectable(p.label)) loadDesign(loadPreset(p.key), "Opened the example. Undo brings back what you had.");
    }
}

void buildPanel() {
    panelHeader();
    auto& D = A.design;
    // jump to Inspect whenever something new is picked in the view
    static int lastKind = 0, lastIdx = -1;
    bool newSel = A.sel.kind != 0 && (A.sel.kind != lastKind || A.sel.idx != lastIdx);
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
            if (A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size()) inspectPart(D.bodies[A.sel.idx]);
            else if (A.sel.kind == 2 && A.sel.idx < (int)D.joints.size()) inspectJoint(D.joints[A.sel.idx]);
            else {
                ImGui::Dummy({0, 8 * S()});
                ImGui::TextUnformatted("Nothing selected");
                muted("Click a part or a joint in the view, or pick one from the Outline.");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        bool outOpen = ImGui::BeginTabItem("Outline", nullptr, tabFlag(1)); tutAnchor("tab:Outline");
        if (outOpen) { outlineTab(); ImGui::EndTabItem(); }
        bool fireOpen = ImGui::BeginTabItem("Firing", nullptr, tabFlag(2)); tutAnchor("tab:Firing");
        if (fireOpen) { ImGui::BeginChild("##firing", {0, 0}); firingTab(); ImGui::EndChild(); ImGui::EndTabItem(); }
        bool fileOpen = ImGui::BeginTabItem("File", nullptr, tabFlag(3)); tutAnchor("tab:File");
        if (fileOpen) { ImGui::BeginChild("##file", {0, 0}); fileTab(); ImGui::EndChild(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    ImGui::End();
}
