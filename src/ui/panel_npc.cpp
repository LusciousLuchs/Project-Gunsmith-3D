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
    ImGui::TextDisabled(NE.player ? "Your character" : "NPC editor");
    ImGui::SameLine(0, 18 * CFG.uiScale);
    if (NE.player) {   // the character is kept as you go; Start again is the only other choice
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "  Start again")) ImGui::OpenPopup("##charreset");
        if (ImGui::BeginPopup("##charreset")) {
            ImGui::TextUnformatted("Start your character again from the character maker's first choices? (Undo brings it back.)");
            if (ImGui::Button("Start again")) { std::string nm = NE.npc.name; CharLook L; L.on = true; NE.npc = characterFromLook(L); NE.npc.name = nm; NE.sel = 0; npcCommit(); npcFrame(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    } else {
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save")) {
        if (npcSave(NE.file.empty() ? NE.npc.name : NE.file)) toast("Saved " + NE.file + " to your NPCs folder.");
        else toast("Couldn't save it.");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open")) showOpen = true;
    }
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
        if (files.empty()) ImGui::TextDisabled("Nothing saved in your NPCs folder yet.");
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

// ---------------------------------------------------------------- your character: the character maker
namespace {
// a row of choice buttons; true if one was clicked
bool choices(const char* id, std::string& v, std::initializer_list<std::pair<const char*, const char*>> opts) {
    bool hit = false;
    float avail = ImGui::GetContentRegionAvail().x, x = 0, sp = 2 * CFG.uiScale;
    ImGui::PushID(id);
    for (auto& [key, label] : opts) {
        float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
        if (x > 0 && x + sp + w <= avail) ImGui::SameLine(0, sp); else x = 0;
        if (accentButton(label, v == key)) { if (v != key) { v = key; hit = true; } }
        x += w + sp;
    }
    ImGui::PopID();
    return hit;
}
bool colour(const char* id, std::string& hex) {
    Color c = hexColor(hex);
    float col[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f};
    if (ImGui::ColorEdit3(id, col, ImGuiColorEditFlags_NoInputs)) { hex = colorHex(col); return true; }
    return false;
}
// a bone of your own gear: where it hangs, its shape and size, its look
void gearEditor(NpcDef& n, NpcBone& b, bool& done) {
    auto fin = [&]() { if (ImGui::IsItemDeactivatedAfterEdit()) done = true; };
    prop("Name"); inputText("##gname", b.name); fin();
    prop("Hangs off");
    if (ImGui::BeginCombo("##gparent", n.bones[b.parent].name.c_str())) {
        for (int k = 0; k < NE.sel; k++)
            if (ImGui::Selectable((n.bones[k].name + "##gp" + std::to_string(k)).c_str(), b.parent == k)) { b.parent = k; done = true; }
        ImGui::EndCombo();
    }
    pNum("Where on it", b.at, 0.01, "%.2f", "0: its start, 1: its end."); b.at = std::clamp(b.at, 0.0, 1.0); fin();
    pNumU("Across", b.side, 1, Q::Length, 0, "To its right (+) or left.", SIGNED); fin();
    pNumU("In front", b.fwd, 1, Q::Length, 0, "In front of it (+) or behind.", SIGNED); fin();
    pNumU("Length", b.len, 2, Q::Length, 0); b.len = std::max(1.0, b.len); fin();
    pNumU("Thick at start", b.r0, 1, Q::Length, 0); b.r0 = std::max(0.5, b.r0); fin();
    pNumU("Thick at end", b.r1, 1, Q::Length, 0); b.r1 = std::max(0.5, b.r1); fin();
    static const char* shapes[] = {"capsule", "cylinder", "box", "ball", "wedge"};
    int sh = 0;
    for (int k = 0; k < 5; k++) if (b.shape == shapes[k]) sh = k;
    prop("Shape");
    if (ImGui::Combo("##gshape", &sh, "Rounded\0Cylinder\0Box\0Ball / egg\0Wedge\0")) { b.shape = shapes[sh]; done = true; }
    double wp = b.width * 100, dp = b.depth * 100;
    if (pNum("Width", wp, 1, "%.0f%%")) b.width = std::clamp(wp / 100, 0.05, 10.0);
    fin();
    if (pNum("Depth", dp, 1, "%.0f%%")) b.depth = std::clamp(dp / 100, 0.05, 10.0);
    fin();
    prop("Colour"); colour("##gcol", b.color); fin();
    prop("Picture", "One of the gun finishes, or your own from your Textures folder.");
    if (texturePicker(b.texture)) done = true;
    pNum("Bend", b.bend, 1, "%.0f\xc2\xb0", "Forward (+) or back. Or drag the dot at its end in the view."); fin();
    pNum("Turn", b.turn, 1, "%.0f\xc2\xb0"); fin();
    pNum("Twist", b.twist, 1, "%.0f\xc2\xb0"); fin();
}
}  // namespace

static void characterPanel() {
    auto& n = NE.npc;
    CharLook& L = n.look;
    bool rebuild = false, done = false;
    auto fin = [&]() { if (ImGui::IsItemDeactivatedAfterEdit()) done = true; };
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Your character holds your guns in Pose (Test bench > Pose). It's kept as you go.");
    ImGui::PopTextWrapPos();
    prop("Name"); inputText("##cname", n.name); fin();
    ImGui::TextDisabled("%.2f m tall", npcHeight(n) / 1000);
    if (section("Body", true)) {
        rebuild |= choices("body", L.body, {{"male", "Male"}, {"female", "Female"}});
        double hp = L.height * 100, bp = L.build * 100;
        if (pNum("Height", hp, 0.5, "%.0f%%", "Taller or shorter than average: arms and legs too.")) { L.height = std::clamp(hp / 100, 0.85, 1.15); rebuild = true; }
        fin();
        if (pNum("Build", bp, 0.5, "%.0f%%", "Slim (low) to heavy (high).")) { L.build = std::clamp(bp / 100, 0.8, 1.3); rebuild = true; }
        fin();
        prop("Skin"); rebuild |= colour("##skin", L.skin); fin();
        ImGui::SameLine();
        for (const char* sk : {"#f1d3bd", "#e0b394", "#c8a088", "#a5785a", "#7a5238", "#4e3424"}) {   // a few to start from
            Color c = hexColor(sk);
            ImGui::PushID(sk);
            if (ImGui::ColorButton("##sw", ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, 1), ImGuiColorEditFlags_NoTooltip, {16 * CFG.uiScale, 16 * CFG.uiScale})) { L.skin = sk; rebuild = done = true; }
            ImGui::PopID();
            ImGui::SameLine(0, 2 * CFG.uiScale);
        }
        ImGui::NewLine();
        prop("Eyes"); rebuild |= colour("##eyes", L.eyes); fin();
    }
    if (section("Hair", true)) {
        rebuild |= choices("hair", L.hair, {{"none", "None"}, {"buzz", "Buzz cut"}, {"short", "Short"}, {"long", "Long"}, {"ponytail", "Ponytail"}, {"bun", "Bun"}});
        if (L.hair != "none") { prop("Colour"); rebuild |= colour("##hairc", L.hairColor); fin(); }
    }
    if (section("Clothes", true)) {
        prop("Top");
        rebuild |= choices("top", L.top, {{"longsleeve", "Long sleeves"}, {"tshirt", "T-shirt"}, {"tank", "Tank top"}});
        prop("Its colour"); rebuild |= colour("##topc", L.topColor); fin();
        prop("Pants");
        rebuild |= choices("pants", L.pants, {{"long", "Long"}, {"shorts", "Shorts"}});
        prop("Their colour"); rebuild |= colour("##pantc", L.pantsColor); fin();
        prop("Shoes"); rebuild |= colour("##shoes", L.shoes); fin();
    }
    if (section("Extras", true)) {
        auto extra = [&](const char* label, bool& on, std::string& col, const char* id) {
            if (ImGui::Checkbox(label, &on)) { rebuild = done = true; }
            if (on) { ImGui::SameLine(PANEL_W * 0.55f); rebuild |= colour(id, col); fin(); }
        };
        extra("Belt", L.belt, L.beltColor, "##beltc");
        extra("Cap", L.hat, L.hatColor, "##hatc");
        extra("Ear defenders", L.headphones, L.headphonesColor, "##hpc");
        extra("Glasses", L.glasses, L.glassesColor, "##glc");
    }
    if (rebuild) {
        int keepSel = NE.sel;
        n = characterFromLook(L, &n);
        if (keepSel >= (int)n.bones.size()) keepSel = 0;
        NE.sel = keepSel;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) done = true;
    }
    if (section("Your own gear", true)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Anything else: a helmet, a vest, pouches. Click a part of your character in the view, then add gear to it.");
        ImGui::PopTextWrapPos();
        for (size_t i = 0; i < n.bones.size(); i++) {
            const NpcBone& g = n.bones[i];
            if (g.gen || npcCoreBone(g.name)) continue;
            ImGui::PushID((int)i);
            if (ImGui::Selectable(g.name.c_str(), NE.sel == (int)i)) { NE.sel = (int)i; NE.endPicked = false; }
            ImGui::PopID();
        }
        if (NE.sel >= (int)n.bones.size()) NE.sel = 0;
        float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Add gear", {w2, 0})) {
            NpcBone b; b.parent = NE.sel; b.name = "Gear " + std::to_string(n.bones.size() + 1);
            const NpcBone& p = n.bones[NE.sel];
            b.len = std::max(30.0, p.len * 0.5); b.r0 = b.r1 = p.r1 * 1.1; b.at = 0.5; b.color = "#4a5240"; b.shape = "box";
            n.bones.push_back(b); NE.sel = (int)n.bones.size() - 1; done = true;
        }
        if (tipHover()) ImGui::SetTooltip("A piece of gear on %s.", n.bones[NE.sel].name.c_str());
        ImGui::SameLine();
        NpcBone& b = n.bones[NE.sel];
        bool mine = !b.gen && !npcCoreBone(b.name);
        ImGui::BeginDisabled(!mine);
        if (dangerButton("Delete", {w2, 0})) { npcRemoveBone(n, NE.sel); NE.sel = 0; done = true; }
        ImGui::EndDisabled();
        if (NE.sel < (int)n.bones.size()) {
            NpcBone& sb = n.bones[NE.sel];
            if (!sb.gen && !npcCoreBone(sb.name)) { heading(sb.name.c_str()); gearEditor(n, sb, done); }
            else ImGui::TextDisabled("Picked: %s (part of your character).", sb.name.c_str());
        }
    }
    if (done) npcCommit();
}

void npcPanel() {
    auto& n = NE.npc;
    if (NE.player && n.look.on) {
        ImGui::SetNextWindowPos({GetScreenWidth() - PANEL_W, TOOLBAR_H});
        ImGui::SetNextWindowSize({PANEL_W, GetScreenHeight() - TOOLBAR_H});
        ImGui::Begin("##npcpanel", nullptr, FIXED);
        characterPanel();
        ImGui::End();
        return;
    }
    ImGui::SetNextWindowPos({GetScreenWidth() - PANEL_W, TOOLBAR_H});
    ImGui::SetNextWindowSize({PANEL_W, GetScreenHeight() - TOOLBAR_H});
    ImGui::Begin("##npcpanel", nullptr, FIXED);
    bool done = false;   // an edit finished this frame: it goes on the undo list
    auto fin = [&]() { if (ImGui::IsItemDeactivatedAfterEdit()) done = true; };
    if (NE.player) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("This character was shaped bone by bone. The character maker is simpler: pick a body, hair, clothes and extras.");
        ImGui::PopTextWrapPos();
        if (accentButton("Use the character maker", true, {-1, 0})) {
            CharLook L; L.on = true;
            n = characterFromLook(L, &n);   // (your own gear comes along; Undo brings the old one back)
            NE.sel = 0; npcCommit(); npcFrame();
        }
        if (tipHover()) ImGui::SetTooltip("Builds your character from choices. Gear you added is kept. Undo brings this one back.");
        ImGui::Dummy({0, 4 * CFG.uiScale});
    }
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
        if (ImGui::Button(NE.player ? "Add gear" : "Add a bone", {w3, 0})) {
            NpcBone b; b.parent = NE.sel; b.name = "Bone " + std::to_string(n.bones.size() + 1);
            const NpcBone& p = n.bones[NE.sel];
            b.len = p.len * 0.8; b.r0 = p.r1; b.r1 = p.r1 * 0.8; b.color = p.color;
            n.bones.push_back(b); NE.sel = (int)n.bones.size() - 1; done = true;
        }
        if (tipHover()) ImGui::SetTooltip(NE.player ? "A piece of gear hanging off the selected bone (a helmet off the head, a pouch off the pelvis).\nShape and place it like any bone." : "A new bone hanging off the end of the selected one.");
        ImGui::SameLine();
        bool core = NE.player && NE.sel < (int)n.bones.size() && npcCoreBone(n.bones[NE.sel].name);   // (a human keeps its own bones)
        ImGui::BeginDisabled(NE.sel == 0 || core);
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
        if (NE.player && npcCoreBone(b.name)) ImGui::TextDisabled("One of a human's own bones: shape it, it stays.");
        if (NE.sel > 0 && !(NE.player && npcCoreBone(b.name))) {
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
        static const char* shapes[] = {"capsule", "cylinder", "box", "ball", "wedge"};
        int sh = 0;
        for (int k = 0; k < 5; k++) if (b.shape == shapes[k]) sh = k;
        prop("Shape", "Rounded: round ends. Cylinder: flat ends. Box: a rounded square across.\nBall: an egg as long as the bone. Wedge: a box thinning to an edge at its end (a foot, a blade).");
        if (ImGui::Combo("##bshape", &sh, "Rounded\0Cylinder\0Box\0Ball / egg\0Wedge\0")) { b.shape = shapes[sh]; done = true; }
        double wp = b.width * 100, dp = b.depth * 100, bp = b.bulge * 100;
        if (pNum("Width", wp, 1, "%.0f%%", "Stretches it side to side: 50% is half as wide (a flat chest, a hand).")) b.width = std::clamp(wp / 100, 0.05, 10.0);
        fin();
        if (pNum("Depth", dp, 1, "%.0f%%", "Stretches it front to back.")) b.depth = std::clamp(dp / 100, 0.05, 10.0);
        fin();
        if (pNum("Bulge", bp, 1, "%+.0f%%", "Swells its middle (a calf, a bicep, a belly). Negative pinches it in.")) b.bulge = std::clamp(bp / 100, -0.9, 3.0);
        fin();
        Color c = hexColor(b.color);
        float col[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f};
        prop("Colour");
        if (ImGui::ColorEdit3("##bcol", col, ImGuiColorEditFlags_NoInputs)) b.color = colorHex(col);
        fin();
        prop("Picture", "A picture on the bone: one of the gun finishes, or your own from your Textures folder. Its colour tints it (white shows it true).");
        if (texturePicker(b.texture)) done = true;
        if (!b.texture.empty()) { pNumU("Picture size", b.texScale, 5, Q::Length, 0, "How big one copy of the picture is before it repeats."); b.texScale = std::clamp(b.texScale, 1.0, 5000.0); fin(); }
        if (ImGui::Button("Use this look on its mirror twin", {-1, 0})) {   // the same bone on the other side: same look
            std::string nm = b.name, other = nm;
            auto swap = [&](const char* x, const char* y) { size_t q = nm.find(x); if (q != std::string::npos) other = nm.substr(0, q) + y + nm.substr(q + std::strlen(x)); };
            swap("(left)", "(right)"); if (other == nm) swap("(right)", "(left)");
            for (auto& o : n.bones)
                if (&o != &b && o.name == other) { o.shape = b.shape; o.width = b.width; o.depth = b.depth; o.bulge = b.bulge; o.color = b.color; o.texture = b.texture; o.texScale = b.texScale; o.r0 = b.r0; o.r1 = b.r1; o.len = b.len; done = true; }
        }
        if (tipHover()) ImGui::SetTooltip("Copies shape, size, colour and picture to the bone with the same name on the other side: (left) and (right).");
        heading("Pose");
        ImGui::TextDisabled("Or drag the dot at its end in the view.");
        pNum("Bend", b.bend, 1, "%.0f\xc2\xb0", "Forward (+) or back, from the bone it hangs off."); fin();
        pNum("Turn", b.turn, 1, "%.0f\xc2\xb0", "To its right (+) or left."); fin();
        pNum("Twist", b.twist, 1, "%.0f\xc2\xb0", "About its own length."); fin();
        if (!NE.player) {   // (your character: armour is the armour bar, plates you pick up)
            heading("When it's hit");
            pCheck("Weak spot", b.vital, "Hits here do triple damage: a head, a glowing core.");
            if (ImGui::IsItemEdited()) done = true;
            double ar = b.armor * 100;
            if (pNum("Armour", ar, 1, "%.0f%%", "How much of a hit it stops.")) b.armor = std::clamp(ar / 100, 0.0, 1.0);
            fin();
        }
    }
    if (!NE.player && section("In a fight", false)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Saved with it now; used once NPCs come alive on maps (coming next).");
        ImGui::PopTextWrapPos();
        pNum("Health", n.health, 1, "%.0f"); n.health = std::max(1.0, n.health); fin();
        pNum("Armour", n.armorBar, 5, "%.0f", "Its armour bar, as armour plates give the player (50 a plate, so 150 is three plates).\nWhile it lasts it soaks 70% of each hit; the rest goes to health. 0: no armour.");
        n.armorBar = std::clamp(n.armorBar, 0.0, 1000.0); fin();
        pNum("Speed", n.speed, 0.1, "%.1f m/s", "Walking pace about 1.4, a sprint about 6."); n.speed = std::max(0.0, n.speed); fin();
        int at = n.attack == "ranged" ? 1 : n.attack == "explode" ? 2 : 0;
        prop("Attacks");
        if (ImGui::Combo("##natt", &at, "Up close\0With a gun\0Explodes\0")) { n.attack = at == 1 ? "ranged" : at == 2 ? "explode" : "melee"; done = true; }
        if (at == 1) {   // a gun made for NPCs (Firing > Made for NPCs in the gun designer)
            prop("Gun", "A gun made for NPCs: in the gun designer, Firing > Made for NPCs.\nIt never needs reloading and never runs out.");
            if (ImGui::BeginCombo("##ngun", n.gun.empty() ? "Choose..." : n.gun.c_str())) {
                static std::vector<std::string> guns; static double when = -10;
                if (GetTime() - when > 2) { guns = npcGunFiles(); when = GetTime(); }
                if (guns.empty()) ImGui::TextDisabled("None yet. In the gun designer, tick Firing > Made for NPCs and save it.");
                for (auto& g : guns) if (ImGui::Selectable(g.c_str(), n.gun == g)) { n.gun = g; done = true; }
                ImGui::EndCombo();
            }
        }
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
    if (!NE.player) {
    heading("Save as");
    if (!saveName[0]) std::snprintf(saveName, sizeof saveName, "%s", NE.file.empty() ? n.name.c_str() : NE.file.c_str());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##nsave", saveName, sizeof saveName);
    if (ImGui::Button("Save to my NPCs", {-1, 0})) {
        if (npcSave(saveName)) toast("Saved " + NE.file + " to your NPCs folder."); else toast("Couldn't save it.");
    }
    }
    ImGui::End();
    if (done) npcCommit();
}
