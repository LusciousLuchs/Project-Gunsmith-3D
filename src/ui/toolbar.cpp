// Gunsmith3D - Top bar: menu, designer / bench switch, tools as icons, view toggles.
#include "app/app.hpp"
#include "app/audio.hpp"
#include "app/scene.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "ui/ui.hpp"
#include "imgui.h"
#include "extras/IconsFontAwesome6.h"

#include <cmath>
#include <cstring>

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

void toolbar() {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)GetScreenWidth(), TOOLBAR_H});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10 * CFG.uiScale, 7 * CFG.uiScale});
    ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    if (iconButton(ICON_FA_BARS, "Menu (Esc)")) { uiTick(); A.showPause = true; }
    gap();
    // designer / bench
    bool b = A.mode == Mode::Build;
    float sz = TOOLBAR_H - 14 * CFG.uiScale;
    if (!A.benchOnly) {
        if (accentButton("Designer", b, {0, sz})) setMode(Mode::Build);
        ImGui::SameLine(0, 2);
        if (accentButton("Test bench", !b, {0, sz})) setMode(Mode::Test);
        gap(18);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Test bench");
        gap(18);
    }
    if (b) {
        struct TB { const char* icon; const char* tip; Tool t; } tools[] = {
            {ICON_FA_ARROW_POINTER, "Select and move", Tool::Select}, {ICON_FA_DRAW_POLYGON, "Polygon part", Tool::Body},
            {ICON_FA_SQUARE, "Rectangle part", Tool::Rect}, {ICON_FA_CIRCLE, "Circle part", Tool::Circle},
            {ICON_FA_SCISSORS, "Knife: cut parts apart (K)", Tool::Knife}, {ICON_FA_LEFT_RIGHT, "Mirror copy", Tool::Mirror},
            {ICON_FA_THUMBTACK, "Pin: a pivot between two parts", Tool::Pin}, {ICON_FA_LINK, "Weld: join two parts rigidly", Tool::Weld},
            {ICON_FA_ROUTE, "Track: a point riding a slot or rail", Tool::Track}, {ICON_FA_WAVE_SQUARE, "Spring", Tool::Spring}};
        int i = 0;
        for (auto& t : tools) {
            if (iconButton(t.icon, t.tip, A.tool == t.t)) setTool(t.t);
            gap(i == 0 || i == 3 || i == 5 ? 12 : 3);
            i++;
        }
        if (iconButton(ICON_FA_ROTATE_LEFT, "Undo (Ctrl+Z)")) undo();
        gap(18);
        static const double steps[] = {0, 0.1, 0.5, 1, 2, 5};
        static const char* metric[] = {"Snap off", "Snap 0.1 mm", "Snap 0.5 mm", "Snap 1 mm", "Snap 2 mm", "Snap 5 mm"};
        static const char* imp[] = {"Snap off", "Snap 0.004 in", "Snap 0.02 in", "Snap 0.04 in", "Snap 0.08 in", "Snap 0.2 in"};
        int si = 2;
        for (int k = 0; k < 6; k++) if (std::fabs(gSnap - steps[k]) < 1e-9) si = k;
        if (!A.snapOn) si = 0;
        ImGui::SetNextItemWidth(118 * CFG.uiScale);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetFrameHeight()) / 2);
        if (ImGui::Combo("##snap", &si, CFG.units == Units::Imperial ? imp : metric, 6)) { gSnap = steps[si]; A.snapOn = si > 0; CFG.snap = gSnap; saveSettings(); }
    } else {
        if (iconButton(A.running ? ICON_FA_PAUSE : ICON_FA_PLAY, A.running ? "Pause" : "Resume")) A.running = !A.running;
        gap(3);
        if (iconButton(ICON_FA_ARROWS_ROTATE, "Restart the test")) restartTest();
        gap(12);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.14f, 0.10f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.66f, 0.19f, 0.13f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.32f, 0.2f, 1));
        ImGui::Button("FIRE", {72 * CFG.uiScale, sz});
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
    gap(4);
    if (iconButton(ICON_FA_EXPAND, "Fit the view (F)")) fitView();
    gap(4);
    if (iconButton(ICON_FA_EYE, "X-ray: see-through parts drawn as ghosts (X)", A.xray)) A.xray = !A.xray;
    gap(4);
    if (iconButton(ICON_FA_LAYER_GROUP, "Cutaway: near-side parts drawn as ghosts (C)", A.cutaway)) A.cutaway = !A.cutaway;
    ImGui::End();
}
