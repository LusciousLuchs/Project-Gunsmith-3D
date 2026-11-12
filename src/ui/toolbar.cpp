// Gunsmith3D - Top bar: menu, designer / bench switch, tools as icons, view toggles.
#include "app/pose.hpp"
#include "app/app.hpp"
#include "app/careermode.hpp"
#include "app/range.hpp"
#include "app/fitbench.hpp"
#include "app/audio.hpp"
#include "app/keys.hpp"
#include "app/scene.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "app/units.hpp"
#include "ui/ui.hpp"
#include "imgui.h"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>

// A square icon button; `on` draws it lit. The tooltip names it (and its key, if any).
static bool iconButton(const char* icon, const char* tip, bool on = false) {
    float sz = TOOLBAR_H - 14 * CFG.uiScale;
    if (on) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.91f, 0.63f, 0.23f, 0.22f)); ImGui::PushStyleColor(ImGuiCol_Text, colAccent()); }
    ImGui::PushID(tip);   // (two buttons can share an icon)
    bool r = ImGui::Button(icon, {sz, sz});
    ImGui::PopID();
    if (on) ImGui::PopStyleColor(2);
    if (tipHover()) ImGui::SetTooltip("%s", tip);
    return r;
}
static void gap(float px = 14) { ImGui::SameLine(0, px * CFG.uiScale); }

// Snap: on/off, the step typed in any unit ("1/16 in", "0.5 mm"), common steps in both systems, and mm | in
static void snapControls(float sz) {
    if (iconButton(ICON_FA_MAGNET, A.snapOn ? "Snap is on: corners and parts jump to the step beside it" : "Snap is off: corners and parts move freely", A.snapOn)) {
        A.snapOn = !A.snapOn; CFG.snapOn = A.snapOn; saveSettings();
    }
    tutAnchor("snap");
    ImGui::SameLine(0, 2 * CFG.uiScale);
    static char buf[48];
    static bool typing = false;
    if (!typing) std::snprintf(buf, sizeof buf, "%s", typedQ(Q::Length, gSnap).c_str());
    float y = ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2;
    ImGui::SetCursorPosY(y);
    ImGui::SetNextItemWidth(84 * CFG.uiScale);
    ImGui::BeginDisabled(!A.snapOn);
    ImGui::InputText("##snapstep", buf, sizeof buf, ImGuiInputTextFlags_AutoSelectAll);
    typing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        double mm;
        if (parseQ(Q::Length, buf, mm) && mm > 0) { gSnap = std::clamp(mm, 0.001, 200.0); CFG.snap = gSnap; saveSettings(); }
        else toast("Couldn't read that snap step. Try 0.5 mm, 1/16 in or 0.02\".");
    }
    if (tipHover()) ImGui::SetTooltip("Snap step. Type it in any unit: 0.5 mm, 1/16 in, 0.02\", 1 mm + 1/32 in.\nA number alone is in the unit shown (mm or in, on the right).");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 0);
    ImGui::SetCursorPosY(y);
    if (ImGui::BeginCombo("##snappick", nullptr, ImGuiComboFlags_NoPreview | ImGuiComboFlags_HeightLarge)) {
        static const double mm[] = {0.1, 0.25, 0.5, 1, 2, 5, 10};   // (10 mm = 1 cm)
        static const char* inch[] = {"1/32 in", "1/16 in", "1/8 in", "1/4 in", "1/2 in", "1 in"};
        bool picked = false;
        ImGui::TextDisabled("Metric");
        for (double m : mm) {
            char l[24]; std::snprintf(l, sizeof l, "%g mm", m);
            if (ImGui::Selectable(l, A.snapOn && std::fabs(gSnap - m) < 1e-9)) { gSnap = m; A.snapOn = true; picked = true; }
        }
        ImGui::TextDisabled("Imperial");
        for (auto t : inch) {
            double v = 0; parseQ(Q::Length, t, v);
            if (ImGui::Selectable(t, A.snapOn && std::fabs(gSnap - v) < 1e-9)) { gSnap = v; A.snapOn = true; picked = true; }
        }
        if (picked) { CFG.snap = gSnap; CFG.snapOn = true; saveSettings(); }
        ImGui::EndCombo();
    }
    if (tipHover()) ImGui::SetTooltip("Common snap steps, metric and imperial");
    ImGui::SameLine(0, 6 * CFG.uiScale);
    ImGui::SetCursorPosY(y);
    unitsSwitch(ImGui::GetFrameHeight());
}

// The workspace look: each choice with a little swatch of its backdrop.
static void swatch(ImVec2 p, float sw, const SceneLook& L) {
    auto* dl = ImGui::GetWindowDrawList();
    ImU32 a = IM_COL32(L.inner.r, L.inner.g, L.inner.b, 255), b = IM_COL32(L.outer.r, L.outer.g, L.outer.b, 255);
    ImVec2 q0{p.x, p.y}, q1{p.x + sw * 1.6f, p.y + sw};
    dl->AddRectFilledMultiColor(q0, q1, a, a, b, b);
    ImU32 g = IM_COL32(L.major.r, L.major.g, L.major.b, 170);
    dl->AddLine({(q0.x + q1.x) / 2, q0.y}, {(q0.x + q1.x) / 2, q1.y}, g);
    dl->AddLine({q0.x, (q0.y + q1.y) / 2}, {q1.x, (q0.y + q1.y) / 2}, g);
    dl->AddRect(q0, q1, IM_COL32(255, 255, 255, 40));
}
static SceneLook lookOf(Backdrop b) { Backdrop keep = CFG.backdrop; CFG.backdrop = b; SceneLook L = sceneLook(); CFG.backdrop = keep; return L; }
static const char* nameOf(int i) { const char* n = backdropNames(); while (i-- > 0) n += std::strlen(n) + 1; return n; }
bool backdropList() {
    bool changed = false;
    float sw = ImGui::GetTextLineHeight();
    for (int i = 0; i < 5; i++) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::Selectable("##bd", CFG.backdrop == (Backdrop)i, 0, {190 * CFG.uiScale, sw + 4})) { CFG.backdrop = (Backdrop)i; changed = true; }
        ImGui::PopID();
        swatch({p.x + 2, p.y + 2}, sw, lookOf((Backdrop)i));
        ImGui::GetWindowDrawList()->AddText({p.x + sw * 1.6f + 10, p.y + 2}, ImGui::GetColorU32(ImGuiCol_Text), nameOf(i));
    }
    if (changed) saveSettings();
    return changed;
}
bool backdropCombo() {
    bool changed = false;
    if (ImGui::BeginCombo("##backdrop", nameOf((int)CFG.backdrop))) { changed = backdropList(); ImGui::EndCombo(); }
    return changed;
}
bool backdropColour() {
    if (CFG.backdrop != Backdrop::Solid) return false;
    float c[3] = {((CFG.solidColor >> 16) & 255) / 255.f, ((CFG.solidColor >> 8) & 255) / 255.f, (CFG.solidColor & 255) / 255.f};
    if (!ImGui::ColorEdit3("##solid", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel)) return false;
    CFG.solidColor = ((unsigned)(c[0] * 255 + 0.5f) << 16) | ((unsigned)(c[1] * 255 + 0.5f) << 8) | (unsigned)(c[2] * 255 + 0.5f);
    saveSettings();
    return true;
}
static const char* GRID_TIP = "Lines every 1, 10 and 50 mm (or 0.1, 0.5 and 1 inch) behind the parts in the designer.";
bool backdropGrid(const char* label) {
    bool r = ImGui::Checkbox(label, &CFG.grid);
    if (tipHover()) ImGui::SetTooltip("%s", GRID_TIP);
    if (r) saveSettings();
    return r;
}

// ---------------------------------------------------------------- tool rail
// The designer's tools down the left of the view: each with its icon and name, in three groups —
// Draw (make a part), Shape (change parts) and Join (connect them). Short windows fall back to icons only.
struct RailTool { const char* icon; const char* label; const char* tip; Tool t; int act; };
static const RailTool RAIL_SELECT = {ICON_FA_ARROW_POINTER, "Select", "Click parts and joints to select them, drag to move.\nSquares are corners, diamonds are edges.", Tool::Select, -1};
static const RailTool RAIL_DRAW[] = {
    {ICON_FA_DRAW_POLYGON, "Polygon", "Click corners to draw any outline. Click the first corner again, or press Enter, to close it.", Tool::Body, -1},
    {ICON_FA_SQUARE, "Rectangle", "Drag out a rectangle.", Tool::Rect, -1},
    {ICON_FA_GRIP_LINES, "Barrel", "Drag from the breech end to the muzzle. A round barrel with a bore, and a chamber at the\nbreech end cut for the cartridge you choose (in its panel). Rifled or smoothbore.", Tool::Barrel, -1},
    {ICON_FA_CIRCLE, "Circle", "Click the centre and drag out the radius.", Tool::Circle, -1},
    {ICON_FA_BARS, "Rail", "A Picatinny rail: drag along the top of a part, as long as you want it.\nSlotted to the standard (10 mm apart) and welded to the part; attachments clamp on anywhere.\nIt makes a handy ruler too.", Tool::Rail, -1}};
static const RailTool RAIL_SHAPE[] = {
    {ICON_FA_CUBE, "Faces", "Point at any face of a part (front, back, top, bottom, ends) and drag to push or pull it.\nE extrudes the face: move the mouse to draw it out, click to finish.", Tool::Faces, -1},
    {ICON_FA_SCISSORS, "Knife", "Drag a line across parts to cut them into separate pieces.", Tool::Knife, (int)Act::Knife},
    {ICON_FA_LEFT_RIGHT, "Mirror", "Drag a line to make a mirrored copy of the selected part.\nIt stays a live mirror: change either one and the other follows.", Tool::Mirror, -1},
    {ICON_FA_RULER, "Measure", "Drag between two points to measure: the length, how far across and up, and the angle.\nSnaps to corners and joints. The line stays until you measure again;\ntick Keep it shown (right) to see it while you move parts.", Tool::Measure, -1},
    {ICON_FA_CIRCLE_MINUS, "Carve", "Cut a hole, slot or pocket into a part from any side: circles, half circles,\nrectangles and triangles, to any depth.", Tool::Carve, -1},
    {ICON_FA_PEN_NIB, "Engrave", "An engraving pen: draw designs or write text on any face of a part,\nlike roll marks, serial numbers or scrollwork. Looks only.", Tool::Engrave, -1}};
static const RailTool RAIL_JOIN[] = {
    {ICON_FA_THUMBTACK, "Pin", "A pivot: two parts share a point and can turn (triggers, hammers, levers).", Tool::Pin, -1},
    {ICON_FA_LINK, "Weld", "Click two parts to make them one solid piece. Nothing is added to the gun:\nthey just move as one. Unweld from the part's panel.", Tool::Weld, -1},
    {ICON_FA_ROUTE, "Track", "A point on one part rides a slot or rail in another (cam slots, guide rails).", Tool::Track, -1},
    {ICON_FA_WAVE_SQUARE, "Spring", "A coil or leaf spring between two parts (recoil, striker and sear springs).", Tool::Spring, -1},
    {ICON_FA_PLUG, "Mount", "An attachment point: click a part's surface to put one there, of a kind you name\n(bayonet lug, side rail, muzzle thread, your own). Attachments fit points of the same kind.", Tool::Mount, -1}};

static void railTip(const RailTool& t) {
    if (!tipHover()) return;
    ImGui::BeginTooltip();
    ImGui::PushFont(gFontBold, 0);
    ImGui::TextUnformatted(t.label);
    ImGui::PopFont();
    if (t.act >= 0) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", keyName(keyOf((Act)t.act))); }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22);
    ImGui::TextDisabled("%s", t.tip);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
static void railButton(const RailTool& t, float w, float h, bool labels) {
    bool on = A.tool == t.t;
    ImGui::PushID(t.label);
    bool click = ImGui::InvisibleButton("##t", {w, h});
    tutAnchor((std::string("tool:") + t.label).c_str());
    bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImVec2 a = ImGui::GetItemRectMin(), c = ImGui::GetItemRectMax();
    auto* dl = ImGui::GetWindowDrawList();
    float rr = 5 * CFG.uiScale;
    if (on) dl->AddRectFilled(a, c, IM_COL32(233, 161, 59, 56), rr);
    else if (held) dl->AddRectFilled(a, c, IM_COL32(255, 255, 255, 34), rr);
    else if (hov) dl->AddRectFilled(a, c, IM_COL32(255, 255, 255, 20), rr);
    ImU32 col = on ? ImGui::GetColorU32(colAccent()) : ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 is = ImGui::CalcTextSize(t.icon);
    if (labels) {
        float fs = ImGui::GetFontSize() * 0.82f;
        ImVec2 ls = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, t.label);
        float top = a.y + (h - is.y - ls.y - 3 * CFG.uiScale) / 2;
        dl->AddText({(a.x + c.x - is.x) / 2, top}, col, t.icon);
        dl->AddText(ImGui::GetFont(), fs, {(a.x + c.x - ls.x) / 2, top + is.y + 3 * CFG.uiScale},
                    on ? col : ImGui::GetColorU32(ImGuiCol_TextDisabled), t.label);
    } else dl->AddText({(a.x + c.x - is.x) / 2, (a.y + c.y - is.y) / 2}, col, t.icon);
    ImGui::PopID();
    railTip(t);
    if (click) setTool(t.t);
}
void toolRail() {
    if (A.mode != Mode::Build || A.benchOnly) return;
    float s = CFG.uiScale, m = 10 * s;
    float availH = GetScreenHeight() - TOOLBAR_H - 2 * m - 40 * s;   // keep clear of the hint strip at the bottom
    // how tall it is: every tool (counted, so a new tool can't push one off the bottom), three captions, padding
    const int tools = 1 + (int)(sizeof RAIL_DRAW / sizeof RAIL_DRAW[0]) + (int)(sizeof RAIL_SHAPE / sizeof RAIL_SHAPE[0]) + (int)(sizeof RAIL_JOIN / sizeof RAIL_JOIN[0]);
    float pad = 5 * s, gapY = 2 * s;
    auto railH = [&](bool lab) {
        float bh = lab ? 50 * s : 36 * s;
        float capH = lab ? ImGui::GetFontSize() * 0.72f + 2 * s + 3 * s : 6 * s;
        return 2 * pad + tools * (bh + gapY) + 3 * (capH + 2 * gapY);
    };
    bool labels = railH(true) <= availH;   // names under the icons if they fit, else icons only
    float w = labels ? 64 * s : 40 * s, h = labels ? 50 * s : 36 * s;
    float needH = railH(labels), winH = std::min(needH, std::max(availH, 120 * s));
    bool scroll = needH > winH + 0.5f;     // still too tall (a small window, a big scale): it scrolls with the wheel
    float sb = 6 * s;
    ImGui::SetNextWindowPos({m, TOOLBAR_H + m});
    ImGui::SetNextWindowSize({w + 2 * pad + (scroll ? sb + 2 * s : 0), winH});
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {pad, pad});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, gapY});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, sb);
    ImGui::Begin("##toolrail", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | (scroll ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
    auto caption = [&](const char* t) {
        ImGui::Dummy({0, 3 * s});
        if (labels) {
            float fs = ImGui::GetFontSize() * 0.72f;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, t);
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), fs, {p.x + (w - ts.x) / 2, p.y}, ImGui::GetColorU32(ImGuiCol_TextDisabled), t);
            ImGui::Dummy({w, ts.y + 2 * s});
        } else {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine({p.x + 6 * s, p.y}, {p.x + w - 6 * s, p.y}, IM_COL32(255, 255, 255, 40));
            ImGui::Dummy({w, 3 * s});
        }
    };
    railButton(RAIL_SELECT, w, h, labels);
    caption("DRAW");
    for (auto& t : RAIL_DRAW) railButton(t, w, h, labels);
    caption("SHAPE");
    for (auto& t : RAIL_SHAPE) railButton(t, w, h, labels);
    caption("JOIN");
    for (auto& t : RAIL_JOIN) railButton(t, w, h, labels);
    ImGui::End();
    ImGui::PopStyleVar(4);
}

void toolbar() {
    {   // Ctrl + / Ctrl - : interface scale, anywhere
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) { setUiScale(CFG.uiScale + 0.25f, true); toast("Interface scale " + std::to_string((int)std::lround(CFG.uiScale * 100)) + "%"); }
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) { setUiScale(CFG.uiScale - 0.25f, true); toast("Interface scale " + std::to_string((int)std::lround(CFG.uiScale * 100)) + "%"); }
        }
    }
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)GetScreenWidth(), TOOLBAR_H});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10 * CFG.uiScale, 7 * CFG.uiScale});
    ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    if (iconButton(ICON_FA_BARS, "Menu (Esc)")) { uiTick(); A.showPause = true; }
    ImGui::SameLine(0, 4 * CFG.uiScale);
    if (iconButton(ICON_FA_GRADUATION_CAP, "Tutorials: short guided lessons on every mechanic", A.tutLesson >= 0)) { uiTick(); A.showTutorials = true; }
    gap();
    // designer / bench
    bool b = A.mode == Mode::Build;
    float sz = TOOLBAR_H - 14 * CFG.uiScale;
    if (inCareer()) {   // career mode: back to your shop's office
        if (accentButton(ICON_FA_STORE "  Office", true, {0, sz})) { careerBackToOffice(); return; }
        if (tipHover()) ImGui::SetTooltip("Back to your shop. Save your gun first (Ctrl+S) so the office sees the change.");
        gap(10);
    }
    if (A.rangeFit) {   // from a crate on the range: only fitting, then back
        if (accentButton("Back to the range", true, {0, sz})) backToRange();
        gap(18);
        goto rightSide;
    }
    if (A.design.attach.on && b) {   // the attachment designer: no test bench (attachments are tried on a gun's mounts)
        accentButton(ICON_FA_PLUG "  Attachment designer", true, {0, sz});
        if (tipHover()) ImGui::SetTooltip("You're designing an attachment. It's saved in your Attachments folder;\nNew > A new gun (or Open) goes back to guns.");
        gap(18);
    } else if (!A.benchOnly) {
        if (accentButton("Designer", b, {0, sz})) setMode(Mode::Build);
        tutAnchor("mode:designer");
        ImGui::SameLine(0, 2);
        if (accentButton("Test bench", !b, {0, sz})) setMode(Mode::Test);
        tutAnchor("mode:test");
        gap(18);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Test bench");
        gap(18);
    }
    if (b) {
        fileButtons();
        gap(16);
        ImGui::BeginDisabled(A.hist.empty());
        if (iconButton(ICON_FA_ROTATE_LEFT, "Undo (Ctrl+Z)")) undo();
        tutAnchor("undo");
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(A.future.empty());
        if (iconButton(ICON_FA_ROTATE_RIGHT, "Redo (Ctrl+Y)")) redo();
        ImGui::EndDisabled();
        gap(16);
        {   // what dragging the selected part does: move it, turn it (rings), or scale it (a box)
            bool sel = A.tool == Tool::Select, mv = sel && !A.rotateMode, rt = sel && A.rotateMode, sc = A.tool == Tool::Scale;
            if (iconButton(ICON_FA_UP_DOWN_LEFT_RIGHT, "Move: pick parts and drag them about.\nCorners, edges and faces of the selected part drag too.", mv)) { A.tool = Tool::Select; A.rotateMode = false; }
            tutAnchor("tool:Select");
            ImGui::SameLine(0, 2);
            if (iconButton(ICON_FA_ROTATE, "Rotate: rings round the selected part. Drag a ring to turn it about that axis (Ctrl: 15 degree steps):\nthe blue one turns it in the side view, red and green lean it toward or away from you.", rt)) { A.tool = Tool::Select; A.rotateMode = true; }
            ImGui::SameLine(0, 2);
            if (iconButton(ICON_FA_MAXIMIZE, "Scale: a box round the selected part. Drag a corner to make it bigger or smaller (Shift: stretch freely),\nor an edge to stretch it one way. Ctrl scales about its middle. Pins, slots and springs on it go with it.\nOr type exact sizes in its panel. With corners picked, only those corners are scaled.", sc)) { A.tool = Tool::Scale; A.rotateMode = false; }
            tutAnchor("tool:Scale");
        }
        gap(16);
        snapControls(sz);
    } else {
        if (accentButton(ICON_FA_PERSON "  Pose", A.posing, {0, sz})) { if (A.fitting) leaveFitting(); if (A.posing) { leavePose(); A.running = true; } else { restartTest(); enterPose(); } }
        if (tipHover()) ImGui::SetTooltip("How a person holds this gun: place the hands and the stock's butt on it,\nand stand the body against it for aiming, from the hip and lowered.");
        tutAnchor("mode:pose");
        gap(12);
        gap(4);
        if (accentButton(ICON_FA_PLUG "  Attachments", A.fitting, {0, sz})) { if (A.fitting) { leaveFitting(); A.running = true; } else { if (A.posing) leavePose(); restartTest(); enterFitting(); } }
        if (tipHover()) ImGui::SetTooltip("Fit attachments from your Attachments folder to this gun's rails and attachment points.\nThey aren't part of the gun's blueprint: save them as a loadout.");
        gap(12);
        if (A.posing || A.fitting) goto rightSide;
        if (iconButton(A.running ? ICON_FA_PAUSE : ICON_FA_PLAY, A.running ? "Pause" : "Resume")) A.running = !A.running;
        gap(3);
        if (iconButton(ICON_FA_ARROWS_ROTATE, "Restart the test")) restartTest();
        gap(12);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.14f, 0.10f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.66f, 0.19f, 0.13f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.32f, 0.2f, 1));
        ImGui::Button("FIRE", {72 * CFG.uiScale, sz});
        tutAnchor("fire");
        if (ImGui::IsItemActivated()) fireDown();
        A.fireBtnHeld = ImGui::IsItemActive();
        ImGui::PopStyleColor(3);
        gap(3);
        if (ImGui::Button("Reload", {0, sz})) { if (A.S) { sim::quickReload(*A.S); toast("Reloaded."); } }
        gap(16);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
        unitsSwitch(ImGui::GetFrameHeight());
    }
rightSide:
    // view toggles on the right
    float right = 5 * (sz + 4 * CFG.uiScale) + 10 * CFG.uiScale;
    ImGui::SameLine(GetScreenWidth() - right);
    if (iconButton(ICON_FA_PALETTE, "Workspace background")) ImGui::OpenPopup("##backdrop");
    if (ImGui::BeginPopup("##backdrop")) {
        ImGui::TextDisabled("Background");
        backdropList();
        if (CFG.backdrop == Backdrop::Solid) { backdropColour(); ImGui::SameLine(); ImGui::TextUnformatted("Colour"); }
        ImGui::Separator();
        backdropGrid("Drafting grid");
        if (ImGui::Checkbox("Workbench, tools and ruler", &CFG.workbench)) saveSettings();
        if (tipHover()) ImGui::SetTooltip("A bench under the gun in the designer, with a ruler and a few tools for scale.");
        ImGui::EndPopup();
    }
    gap(4);
    if (iconButton(ICON_FA_CUBE, "View: side, top, front... and orthographic (V: side view)", A.ortho)) ImGui::OpenPopup("##view");
    tutAnchor("view:side");
    if (ImGui::BeginPopup("##view")) {
        bool o = A.ortho;
        if (ImGui::Checkbox("Orthographic (no perspective)", &o)) setOrtho(o);
        if (tipHover()) ImGui::SetTooltip("Parallel lines stay parallel and sizes don't shrink with distance:\nfor lining things up exactly. Turn the view to any angle. (Numpad 5)");
        ImGui::Separator();
        ImGui::TextDisabled("Look straight at");
        struct VS { const char* l; View v; const char* k; };
        const VS vs[] = {{"Right side", View::Right, "V, Numpad 3"}, {"Left side", View::Left, "Ctrl+Numpad 3"}, {"Top", View::Top, "Numpad 7"},
                         {"Bottom", View::Bottom, "Ctrl+Numpad 7"}, {"Front (muzzle)", View::Front, "Numpad 1"}, {"Back", View::Back, "Ctrl+Numpad 1"}};
        for (auto& v : vs) { if (ImGui::Selectable(v.l)) { snapView(v.v); ImGui::CloseCurrentPopup(); } ImGui::SameLine(170 * CFG.uiScale); ImGui::TextDisabled("%s", v.k); }
        ImGui::EndPopup();
    }
    gap(4);
    if (iconButton(ICON_FA_EXPAND, "Fit the view (F)")) fitView();
    tutAnchor("view:fit");
    gap(4);
    if (iconButton(ICON_FA_EYE, "X-ray: see-through parts drawn as ghosts (X)", A.xray)) A.xray = !A.xray;
    tutAnchor("view:xray");
    gap(4);
    if (iconButton(ICON_FA_LAYER_GROUP, "Cutaway: near-side parts drawn as ghosts (C)", A.cutaway)) A.cutaway = !A.cutaway;
    tutAnchor("view:cutaway");
    ImGui::End();
}
