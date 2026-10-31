// Gunsmith3D - Cartridge designer window.
#include "app/app.hpp"
#include "app/render.hpp"
#include "ui/ui.hpp"
#include "app/settings.hpp"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <vector>

void cartridgeDesigner() {
    if (!A.showCartDesigner) return;
    ImGui::SetNextWindowSize({460 * CFG.uiScale, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({(float)A.vw / 2 - 230 * CFG.uiScale, TOOLBAR_H + 40 * CFG.uiScale}, ImGuiCond_Appearing);
    if (!ImGui::Begin("Cartridge designer", &A.showCartDesigner, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    Cartridge& c = A.editCart;
    ImGui::PushItemWidth(-1);
    prop("Start from");
    if (ImGui::BeginCombo("##from", "Choose a cartridge...")) {
        for (auto& p : cartridgePresets()) if (ImGui::Selectable(p.name.c_str())) c = p;
        for (auto& p : A.userCarts) if (ImGui::Selectable((p.name + "  (mine)").c_str())) c = p;
        ImGui::EndCombo();
    }
    // profile preview, to scale
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float W = ImGui::GetContentRegionAvail().x, Hh = 110 * CFG.uiScale;
    ImGui::Dummy({W, Hh});
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, {p0.x + W, p0.y + Hh}, IM_COL32(18, 21, 28, 255), 4);
    double len = std::max(c.oal, c.caseLen) + 4, dia = std::max(c.rimDia, c.baseDia);
    float sc = (float)std::min((W - 20) / len, (Hh - 30) / dia);
    float x0 = p0.x + 10, yc = p0.y + Hh / 2 - 6;
    auto drawProf = [&](const std::vector<V2>& pr, ImU32 fill) {
        std::vector<ImVec2> top, bot;
        for (auto& q : pr) { top.push_back({x0 + (float)q.x * sc, yc - (float)q.y * sc}); bot.push_back({x0 + (float)q.x * sc, yc + (float)q.y * sc}); }
        for (size_t k = 0; k + 1 < pr.size(); k++) {
            ImVec2 quad[4] = {top[k], top[k + 1], bot[k + 1], bot[k]};
            dl->AddConvexPolyFilled(quad, 4, fill);
        }
    };
    drawProf(bulletProfile(c), IM_COL32(192, 106, 58, 255));
    drawProf(caseProfile(c), IM_COL32(217, 164, 65, 255));
    dl->AddLine({x0, yc}, {x0 + (float)len * sc, yc}, IM_COL32(255, 255, 255, 40));
    char buf[64];
    std::snprintf(buf, sizeof buf, "OAL %s   case %s   bullet %s", fmtQ(Q::Length, c.oal, 2).c_str(), fmtQ(Q::Length, c.caseLen, 2).c_str(), fmtQ(Q::Length, c.bulletDia, 2).c_str());
    dl->AddText({x0, p0.y + Hh - 18}, IM_COL32(200, 205, 215, 255), buf);
    char nm[64];
    std::snprintf(nm, sizeof nm, "%s", c.name.c_str());
    prop("Name");
    if (ImGui::InputText("##cname", nm, sizeof nm)) c.name = nm;
    ImGui::PushFont(gFontBold, 15.0f); ImGui::TextUnformatted("Case"); ImGui::PopFont();
    pNumU("Case length", c.caseLen, 0.1, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Rim", c.rimDia, 0.05, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Rim thickness", c.rimThick, 0.05, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Base", c.baseDia, 0.05, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Shoulder", c.shoulderDia, 0.05, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Shoulder at", c.shoulderAt, 0.1, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Neck", c.neckDia, 0.05, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pCheck("Rimmed", c.rimmed);
    ImGui::PushFont(gFontBold, 15.0f); ImGui::TextUnformatted("Bullet"); ImGui::PopFont();
    pNumU("Diameter", c.bulletDia, 0.05, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Overall length", c.oal, 0.1, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Nose length", c.noseLen, 0.1, Q::Length, CFG.units == Units::Metric ? 2 : 3);
    pNumU("Weight", c.bulletMass, 0.1, Q::BulletMass);
    pNumU("Velocity", c.velocity, 5, Q::Velocity);
    c.caseLen = std::max(3.0, c.caseLen); c.oal = std::max(c.caseLen + 1, c.oal);
    c.shoulderAt = std::clamp(c.shoulderAt, 1.0, c.caseLen); c.noseLen = std::clamp(c.noseLen, 0.5, c.oal - c.caseLen + 2);
    ImGui::TextDisabled("%s, recoil impulse %s", fmtQ(Q::Energy, c.energy()).c_str(), fmtQ(Q::Impulse, c.impulse()).c_str());
    if (c.neckDia < c.bulletDia) ImGui::TextColored(colBad(), "The neck is narrower than the bullet.");
    if (c.rimDia < c.baseDia && c.rimmed) ImGui::TextColored(colBad(), "A rimmed case needs a rim wider than its base.");
    ImGui::Separator();
    if (ImGui::Button("Use in this design")) {
        auto& br = A.design.breech;
        br.cart = c; br.cartImpulse = true; applyCartridge(br);
        for (size_t i = 0; i < A.design.bodies.size(); i++) if (A.design.bodies[i].magazine && A.design.bodies[i].gen.on) generateMagazine(A.design, (int)i);
        commit(); toast("Using " + c.name + ".");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save to my cartridges")) {
        bool found = false;
        for (auto& u : A.userCarts) if (u.name == c.name) { u = c; found = true; }
        if (!found) A.userCarts.push_back(c);
        toast(saveUserCartridges(A.userCarts) ? "Saved " + c.name + " to cartridges.json." : "Could not write cartridges.json.");
    }
    bool mine = false;
    for (auto& u : A.userCarts) if (u.name == c.name) mine = true;
    if (mine) {
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            A.userCarts.erase(std::remove_if(A.userCarts.begin(), A.userCarts.end(), [&](const Cartridge& u) { return u.name == c.name; }), A.userCarts.end());
            saveUserCartridges(A.userCarts); toast("Deleted.");
        }
    }
    ImGui::PopItemWidth();
    ImGui::End();
}
