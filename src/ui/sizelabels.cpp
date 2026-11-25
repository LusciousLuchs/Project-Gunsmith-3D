// Gunsmith3D - size labels (0.9.0, the building rework): the selected part's length, height and thickness shown
// beside it in the designer; click one and type the real size (any unit: 4.5 in, 114 mm, 4 1/2"). The part is
// stretched along that way only, from its back, top or middle, with everything attached to it (as the Scale tool does).
#include "ui/ui.hpp"
#include "app/app.hpp"
#include "app/view.hpp"
#include "app/settings.hpp"
#include "app/units.hpp"
#include "app/faces.hpp"
#include "scale.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
int gEdit = -1;          // the label being typed in (0 length, 1 height, 2 thickness), -1 none
int gEditPart = -1;
char gBuf[48] = "";
bool gFocus = false;

bool label(int which, Vector2 at, const char* name, double mm) {
    float S = CFG.uiScale;
    char id[32]; snprintf(id, sizeof id, "##size%d", which);
    ImGui::SetNextWindowPos({at.x, at.y}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowBgAlpha(gEdit == which ? 0.95f : 0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {5 * S, 3 * S});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5 * S);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {10, 10});
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    bool applied = false;
    if (gEdit == which) {
        ImGui::TextDisabled("%s", name); ImGui::SameLine();
        ImGui::SetNextItemWidth(110 * S);
        if (gFocus) { ImGui::SetKeyboardFocusHere(); gFocus = false; }
        bool enter = ImGui::InputText("##v", gBuf, sizeof gBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (enter) applied = true;
        else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape)) gEdit = -1;
    } else {
        std::string t = std::string(name) + "  " + fmtQ(Q::Length, mm);
        ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
        if (ImGui::SmallButton(t.c_str())) {
            gEdit = which; gEditPart = A.sel.idx; gFocus = true;
            snprintf(gBuf, sizeof gBuf, "%s", typedQ(Q::Length, mm).c_str());
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click and type its real %s (mm or inches: 4.5 in, 4 1/2\", 114 mm)", which == 0 ? "length" : which == 1 ? "height" : "thickness");
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    return applied;
}
}  // namespace

void sizeLabels() {
    if (A.mode != Mode::Build || A.benchOnly || A.screen != Screen::Workshop) return;
    if (A.tool != Tool::Faces && A.tool != Tool::Select) { gEdit = -1; return; }
    if (A.sel.kind != 1 || A.sel.idx < 0 || A.sel.idx >= (int)A.design.bodies.size() || facesActive() || A.dragKind != 0) { gEdit = -1; return; }
    if (gEdit >= 0 && gEditPart != A.sel.idx) gEdit = -1;
    int i = A.sel.idx;
    const BodyDef& d = A.design.bodies[i];
    if (!partShown(A.design, d)) return;
    auto o = outlineOf(d);
    if (o.size() < 2) return;
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& p : o) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    float zN = bodyZ(i) + bodyDepth(d) / 2;
    float gap = 18 * CFG.uiScale;
    Vector2 bottom = toScreen(W3({(x0 + x1) / 2, y1}, zN)), right = toScreen(W3({x1, (y0 + y1) / 2}, zN)), corner = toScreen(W3({x1, y0}, zN));
    bool thickOk = !d.round;
    double typed = 0;
    int which = -1;
    if (label(0, {bottom.x, bottom.y + gap}, "Length", x1 - x0)) which = 0;
    if (label(1, {right.x + gap * 2.6f, right.y}, "Height", y1 - y0)) which = 1;
    if (thickOk && label(2, {corner.x + gap * 2.6f, corner.y - gap * 2.2f}, "Thick", d.thick)) which = 2;
    if (which < 0) return;
    gEdit = -1;
    if (!parseQ(Q::Length, gBuf, typed) || typed <= 0.05) { toast("That isn't a size: type a number, like 120 or 4.5 in."); return; }
    double now = which == 0 ? x1 - x0 : which == 1 ? y1 - y0 : d.thick;
    if (now < 1e-6) return;
    double k = typed / now;
    if (std::fabs(k - 1) < 1e-9) return;
    // from its back (length), its top (height); thickness about its middle
    scaleParts(A.design, {i}, {x0, y0}, which == 0 ? k : 1, which == 1 ? k : 1, which == 2 ? k : 1);
    commit();
    toast(std::string(which == 0 ? "Length" : which == 1 ? "Height" : "Thickness") + " set to " + fmtQ(Q::Length, typed) + ".");
}
