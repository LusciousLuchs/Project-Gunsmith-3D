// Gunsmith3D - the NPC editor's top bar and side panel (see app/npcedit.hpp).
#include "app/npcedit.hpp"
#include "app/app.hpp"
#include "app/fonts.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr ImGuiWindowFlags FIXED = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
char saveName[96] = "";
bool showOpen = false;
}

void npcToolbar() {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)GetScreenWidth(), TOOLBAR_H});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10 * CFG.uiScale, 7 * CFG.uiScale});
    ImGui::Begin("##npctoolbar", nullptr, FIXED);
    ImGui::PopStyleVar();
    if (ImGui::Button(ICON_FA_BARS)) A.showPause = true;
    if (tipHover()) ImGui::SetTooltip("Menu (Esc)");
    ImGui::SameLine(0, 14 * CFG.uiScale);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("NPC editor");
    ImGui::SameLine(0, 18 * CFG.uiScale);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save")) {
        if (npcSave(NE.file.empty() ? NE.npc.name : NE.file)) toast("Saved to user/npcs/" + NE.file + ".json");
        else toast("Couldn't save it.");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open")) showOpen = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_LEFT)) npcUndo();
    if (tipHover()) ImGui::SetTooltip("Undo (Ctrl+Z)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT)) npcRedo();
    if (tipHover()) ImGui::SetTooltip("Redo (Ctrl+Y)");
    ImGui::End();
    if (showOpen) { ImGui::OpenPopup("Open an NPC"); showOpen = false; }
    ImGui::SetNextWindowSize({420 * CFG.uiScale, 0});
    if (ImGui::BeginPopupModal("Open an NPC", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        auto files = npcFiles();
        if (files.empty()) ImGui::TextDisabled("Nothing saved in user/npcs yet.");
        for (auto& f : files)
            if (ImGui::Selectable(f.c_str())) { if (npcOpen(f)) toast("Opened " + f + "."); ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        ImGui::TextDisabled("Start from:");
        for (auto& [k, label] : npcPresets())
            if (ImGui::Selectable(label.c_str())) { NE.npc = npcPreset(k); NE.sel = 0; NE.file.clear(); npcCommit(); npcFrame(); ImGui::CloseCurrentPopup(); }
        ImGui::Dummy({0, 4});
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void npcPanel() {
    auto& n = NE.npc;
    ImGui::SetNextWindowPos({GetScreenWidth() - PANEL_W, TOOLBAR_H});
    ImGui::SetNextWindowSize({PANEL_W, GetScreenHeight() - TOOLBAR_H});
    ImGui::Begin("##npcpanel", nullptr, FIXED);
    bool done = false;   // an edit finished this frame: it goes on the undo list
    auto fin = [&]() { if (ImGui::IsItemDeactivatedAfterEdit()) done = true; };
    prop("Name"); if (inputText("##nname", n.name)) {} fin();
    ImGui::TextDisabled("%d bones, %.2f m tall", (int)n.bones.size(), npcHeight(n) / 1000);
    ImGui::Dummy({0, 4 * CFG.uiScale});

    if (section("Body", true)) {
        ImGui::TextDisabled("Click a bone in the view, or here.");
        ImGui::BeginChild("##bones", {0, 150 * CFG.uiScale}, ImGuiChildFlags_Borders);
        // the tree, by depth
        std::vector<int> depth(n.bones.size(), 0);
        for (size_t i = 1; i < n.bones.size(); i++) depth[i] = n.bones[i].parent >= 0 && n.bones[i].parent < (int)i ? depth[n.bones[i].parent] + 1 : 1;
        for (size_t i = 0; i < n.bones.size(); i++) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth[i] * 12 * CFG.uiScale);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(n.bones[i].name.c_str(), NE.sel == (int)i)) { NE.sel = (int)i; NE.endPicked = false; }
            ImGui::PopID();
        }
        ImGui::EndChild();
        float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        if (ImGui::Button("Add a bone", {w3, 0})) {
            NpcBone b; b.parent = NE.sel; b.name = "Bone " + std::to_string(n.bones.size() + 1);
            const NpcBone& p = n.bones[NE.sel];
            b.len = p.len * 0.8; b.r0 = p.r1; b.r1 = p.r1 * 0.8; b.color = p.color;
            n.bones.push_back(b); NE.sel = (int)n.bones.size() - 1; done = true;
        }
        if (tipHover()) ImGui::SetTooltip("A new bone hanging off the end of the selected one.");
        ImGui::SameLine();
        ImGui::BeginDisabled(NE.sel == 0);
        if (ImGui::Button("Mirror it", {w3, 0})) { npcMirrorBone(n, NE.sel); done = true; }
        if (tipHover()) ImGui::SetTooltip("A copy of this bone and everything on it, on the other side (an arm, a leg).");
        ImGui::SameLine();
        if (dangerButton("Delete", {w3, 0})) { npcRemoveBone(n, NE.sel); NE.sel = 0; done = true; }
        ImGui::EndDisabled();
    }
    if (NE.sel >= (int)n.bones.size()) NE.sel = 0;
    NpcBone& b = n.bones[NE.sel];
    if (section("This bone", true)) {
        prop("Name"); inputText("##bname", b.name); fin();
        if (NE.sel > 0) {
            prop("Hangs off");
            if (ImGui::BeginCombo("##bparent", n.bones[b.parent].name.c_str())) {
                for (int k = 0; k < NE.sel; k++)
                    if (ImGui::Selectable((n.bones[k].name + "##p" + std::to_string(k)).c_str(), b.parent == k)) { b.parent = k; done = true; }
                ImGui::EndCombo();
            }
            pNum("Where on it", b.at, 0.01, "%.2f", "0: its start, 1: its end."); b.at = std::clamp(b.at, 0.0, 1.0); fin();
            pNumU("Across", b.side, 1, Q::Length, 0, "How far to the side of that point (+ its right).", SIGNED); fin();
        } else {
            pNumU("Height", n.rootHeight, 5, Q::Length, 0, "Where the root bone starts, above the ground."); fin();
            pNum("Lean forward", n.rootBend, 1, "%.0f\xc2\xb0", "90: the body lies forward (four legs)."); fin();
        }
        pNumU("Length", b.len, 2, Q::Length, 0); b.len = std::max(1.0, b.len); fin();
        pNumU("Thick at start", b.r0, 1, Q::Length, 0, "Its radius where it starts."); b.r0 = std::max(0.5, b.r0); fin();
        pNumU("Thick at end", b.r1, 1, Q::Length, 0, "Its radius where it ends."); b.r1 = std::max(0.5, b.r1); fin();
        int sh = b.shape == "box" ? 1 : b.shape == "ball" ? 2 : 0;
        prop("Shape");
        if (ImGui::Combo("##bshape", &sh, "Rounded\0Box\0Ball\0")) { b.shape = sh == 1 ? "box" : sh == 2 ? "ball" : "capsule"; done = true; }
        Color c = hexColor(b.color);
        float col[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f};
        prop("Colour");
        if (ImGui::ColorEdit3("##bcol", col, ImGuiColorEditFlags_NoInputs)) b.color = colorHex(col);
        fin();
        heading("Pose");
        ImGui::TextDisabled("Or drag the dot at its end in the view.");
        pNum("Bend", b.bend, 1, "%.0f\xc2\xb0", "Forward (+) or back, from the bone it hangs off."); fin();
        pNum("Turn", b.turn, 1, "%.0f\xc2\xb0", "To its right (+) or left."); fin();
        pNum("Twist", b.twist, 1, "%.0f\xc2\xb0", "About its own length."); fin();
        heading("When it's hit");
        pCheck("Weak spot", b.vital, "Hits here do triple damage: a head, a glowing core.");
        if (ImGui::IsItemEdited()) done = true;
        double ar = b.armor * 100;
        if (pNum("Armour", ar, 1, "%.0f%%", "How much of a hit it stops.")) b.armor = std::clamp(ar / 100, 0.0, 1.0);
        fin();
    }
    if (section("In a fight", false)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Saved with it now; used once NPCs come alive on maps (coming next).");
        ImGui::PopTextWrapPos();
        pNum("Health", n.health, 1, "%.0f"); n.health = std::max(1.0, n.health); fin();
        pNum("Speed", n.speed, 0.1, "%.1f m/s", "Walking pace about 1.4, a sprint about 6."); n.speed = std::max(0.0, n.speed); fin();
        int at = n.attack == "ranged" ? 1 : n.attack == "explode" ? 2 : 0;
        prop("Attacks");
        if (ImGui::Combo("##natt", &at, "Up close\0With a gun\0Explodes\0")) { n.attack = at == 1 ? "ranged" : at == 2 ? "explode" : "melee"; done = true; }
        if (at == 1) { prop("Gun", "A blueprint from user/guns."); inputText("##ngun", n.gun); fin(); }
        else { pNum("Damage", n.damage, 1, "%.0f"); fin(); }
        pNum(at == 1 ? "Shoots from" : "Reach", n.reach, 0.1, "%.1f m"); fin();
        pNum("Sees you at", n.sight, 1, "%.0f m"); fin();
        int bh = n.behaviour == "stand" ? 0 : n.behaviour == "wander" ? 1 : n.behaviour == "guard" ? 3 : 2;
        prop("Behaviour");
        if (ImGui::Combo("##nbeh", &bh, "Stands still\0Wanders\0Chases you\0Guards its spot\0")) {
            const char* v[] = {"stand", "wander", "chase", "guard"}; n.behaviour = v[bh]; done = true;
        }
    }
    ImGui::Dummy({0, 8 * CFG.uiScale});
    heading("Save as");
    if (!saveName[0]) std::snprintf(saveName, sizeof saveName, "%s", NE.file.empty() ? n.name.c_str() : NE.file.c_str());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##nsave", saveName, sizeof saveName);
    if (ImGui::Button("Save to user/npcs", {-1, 0})) {
        if (npcSave(saveName)) toast("Saved to user/npcs/" + NE.file + ".json"); else toast("Couldn't save it.");
    }
    ImGui::End();
    if (done) npcCommit();
}
