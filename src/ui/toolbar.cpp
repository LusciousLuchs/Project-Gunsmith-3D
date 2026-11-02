// Gunsmith3D - Top bar: menu, designer / bench switch, tools as icons, view toggles.
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/keys.hpp"
#include "app/scene.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "ui/ui.hpp"
#include "imgui.h"
#include "extras/IconsFontAwesome6.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>

// A square icon button; `on` draws it lit. The tooltip names it (and its key, if any).
static bool iconButton(const char* icon, const char* tip, bool on = false) {
    float sz = TOOLBAR_H - 14 * CFG.uiScale;
    if (on) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.91f, 0.63f, 0.23f, 0.22f)); ImGui::PushStyleColor(ImGuiCol_Text, colAccent()); }
    bool r = ImGui::Button(icon, {sz, sz});
    if (on) ImGui::PopStyleColor(2);
    if (tipHover()) ImGui::SetTooltip("%s", tip);
    return r;
}
static void gap(float px = 14) { ImGui::SameLine(0, px * CFG.uiScale); }

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
    {ICON_FA_CIRCLE, "Circle", "Click the centre and drag out the radius.", Tool::Circle, -1}};
static const RailTool RAIL_SHAPE[] = {
    {ICON_FA_SCISSORS, "Knife", "Drag a line across parts to cut them into separate pieces.", Tool::Knife, (int)Act::Knife},
    {ICON_FA_LEFT_RIGHT, "Mirror", "Drag a line to make a mirrored copy of the selected part.", Tool::Mirror, -1},
    {ICON_FA_CIRCLE_MINUS, "Carve", "Cut a hole, slot or pocket into a part from any side: circles, half circles,\nrectangles and triangles, to any depth.", Tool::Carve, -1}};
static const RailTool RAIL_JOIN[] = {
    {ICON_FA_THUMBTACK, "Pin", "A pivot: two parts share a point and can turn (triggers, hammers, levers).", Tool::Pin, -1},
    {ICON_FA_LINK, "Weld", "Click two parts to make them one solid piece. Nothing is added to the gun:\nthey just move as one. Unweld from the part's panel.", Tool::Weld, -1},
    {ICON_FA_ROUTE, "Track", "A point on one part rides a slot or rail in another (cam slots, guide rails).", Tool::Track, -1},
    {ICON_FA_WAVE_SQUARE, "Spring", "A coil or leaf spring between two parts (recoil, striker and sear springs).", Tool::Spring, -1}};

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
    float capH = ImGui::GetFontSize() * 0.8f + 6 * s;
    bool labels = 10 * 50 * s + 3 * capH + 26 * s <= availH;
    float w = labels ? 64 * s : 40 * s, h = labels ? 50 * s : 36 * s;
    ImGui::SetNextWindowPos({m, TOOLBAR_H + m});
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {5 * s, 5 * s});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 2 * s});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * s);
    ImGui::Begin("##toolrail", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
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
    ImGui::PopStyleVar(3);
}

void toolbar() {
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
    if (!A.benchOnly) {
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
        ImGui::BeginDisabled(A.hist.empty());
        if (iconButton(ICON_FA_ROTATE_LEFT, "Undo (Ctrl+Z)")) undo();
        tutAnchor("undo");
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(A.future.empty());
        if (iconButton(ICON_FA_ROTATE_RIGHT, "Redo (Ctrl+Y)")) redo();
        ImGui::EndDisabled();
        gap(16);
        static const double steps[] = {0, 0.1, 0.5, 1, 2, 5};
        static const char* metric[] = {"Snap off", "Snap 0.1 mm", "Snap 0.5 mm", "Snap 1 mm", "Snap 2 mm", "Snap 5 mm"};
        static const char* imp[] = {"Snap off", "Snap 0.004 in", "Snap 0.02 in", "Snap 0.04 in", "Snap 0.08 in", "Snap 0.2 in"};
        int si = 2;
        for (int k = 0; k < 6; k++) if (std::fabs(gSnap - steps[k]) < 1e-9) si = k;
        if (!A.snapOn) si = 0;
        ImGui::SetNextItemWidth(118 * CFG.uiScale);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
        if (ImGui::Combo("##snap", &si, CFG.units == Units::Imperial ? imp : metric, 6)) { gSnap = steps[si]; A.snapOn = si > 0; CFG.snap = gSnap; saveSettings(); }
        if (tipHover()) ImGui::SetTooltip("Corners and parts jump to this step when you draw or drag them.");
    } else {
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
    }
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
        ImGui::EndPopup();
    }
    gap(4);
    if (iconButton(ICON_FA_CUBE, A.ortho ? "3D view (V)" : "Side view (V)", A.ortho)) A.ortho = !A.ortho;
    tutAnchor("view:side");
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
