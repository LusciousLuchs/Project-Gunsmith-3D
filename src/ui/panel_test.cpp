// Gunsmith3D - Test mode panels: bench controls and motion trace.
#include "hand.hpp"
#include "app/pose.hpp"
#include "app/app.hpp"
#include "app/npcedit.hpp"
#include "app/render.hpp"
#include "app/reltest.hpp"
#include "app/settings.hpp"
#include "app/keys.hpp"
#include "ui/ui.hpp"
#include "imgui.h"

#include <cstring>
#include <algorithm>
#include <map>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

void traceWindow() {
    if (!A.S || A.trace.size() < 2 || CFG.stats != StatsDetail::Engineering) return;
    float sc = CFG.uiScale;
    ImGui::SetNextWindowPos({A.vw - 400.f * sc, TOOLBAR_H + A.vh - 176.f * sc}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({390 * sc, 166 * sc});
    double t0 = A.trace.front().first, t1 = A.trace.back().first;
    char title[64];
    std::snprintf(title, sizeof title, "Travel (%s), last %.0f ms###trace", unitOf(Q::Length), (t1 - t0) * 1000);
    ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImVec2 p0 = ImGui::GetCursorScreenPos(), sz{362 * sc, 92 * sc};
    ImGui::Dummy(sz);
    auto* dl = ImGui::GetWindowDrawList();
    float mn = -1, mx = 1;
    for (auto& r : A.trace) for (float v : r.second) { mn = std::min(mn, v); mx = std::max(mx, v); }
    auto X = [&](double t) { return p0.x + (float)((t - t0) / std::max(1e-9, t1 - t0)) * sz.x; };
    auto Y = [&](float v) { return p0.y + sz.y - (v - mn) / (mx - mn) * sz.y; };
    dl->AddRectFilled(p0, {p0.x + sz.x, p0.y + sz.y}, IM_COL32(15, 18, 26, 255));
    dl->AddLine({p0.x, Y(0)}, {p0.x + sz.x, Y(0)}, IM_COL32(255, 255, 255, 50));
    std::vector<const sim::Body*> movable;
    for (auto& b : A.S->bodies) if (mainPart(b)) movable.push_back(&b);
    for (size_t k = 0; k < movable.size(); k++) {
        Color c = hexColor(movable[k]->d->color);
        std::vector<ImVec2> pts;
        for (auto& r : A.trace) if (k < r.second.size()) pts.push_back({X(r.first), Y(r.second[k])});
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(c.r, c.g, c.b, 255), 0, 1.8f);
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, CFG.units == Units::Imperial ? "%.2f" : "%.0f", toDisp(Q::Length, mn));
    dl->AddText({p0.x + sz.x - 44 * sc, p0.y + sz.y - 18 * sc}, IM_COL32(180, 190, 210, 255), buf);
    for (size_t k = 0; k < movable.size(); k++) {
        Color c = hexColor(movable[k]->d->color);
        ImGui::TextColored(ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, 1), "%s", movable[k]->d->name.c_str());
        if (k + 1 < movable.size()) ImGui::SameLine();
    }
    ImGui::End();
}


// The reliability test: shoots the design many times with real-world variation and counts the jams.
static void reliabilitySection() {
    heading("Reliability test");
    static const int counts[] = {25, 50, 100, 200};
    if (REL.running) {
        float f = REL.target > 0 ? (float)REL.done.load() / REL.target : 0;
        char buf[48];
        std::snprintf(buf, sizeof buf, "%d of %d rounds", REL.done.load(), REL.target);
        ImGui::ProgressBar(f, ImVec2(-1, 0), buf);
        if (ImGui::Button("Stop")) relStop();
        ImGui::SameLine(); ImGui::TextDisabled("Shooting in the background...");
        return;
    }
    static bool init = false;
    if (!init) { REL.rounds = CFG.relRounds; init = true; }
    int ci = 1;
    for (int i = 0; i < 4; i++) if (REL.rounds == counts[i]) ci = i;
    ImGui::SetNextItemWidth(120 * CFG.uiScale);
    if (ImGui::Combo("##relcount", &ci, "25 rounds\0" "50 rounds\0" "100 rounds\0" "200 rounds\0")) REL.rounds = counts[ci];
    ImGui::SameLine();
    if (ImGui::Button("Run reliability test")) relStart();
    tutAnchor("bench:reliability");
    if (tipHover())
        ImGui::SetTooltip("Fires the design over and over with the differences real shooting has: ammunition a little hot\n"
                          "or weak, a clean or dirty action, a firm or loose grip. Every jam is cleared the way you would\n"
                          "(rack it, pick out the stuck case) and counted. Runs in the background; your test bench stays live.");
    if (!REL.have) {
        ImGui::TextWrapped("Shoots this design many times and tells you how often it jams, and why.");
        return;
    }
    std::lock_guard<std::mutex> lk(REL.m);
    const auto& R = REL.report;
    ImGui::Text("%s: %d rounds fired%s", REL.designName.c_str(), R.fired, R.manual ? " (action worked by hand)" : "");
    ImVec4 col = R.jams == 0 ? colGood() : R.jamRate() < 2 ? colWarn() : colBad();
    if (R.jams == 0) ImGui::TextColored(col, "No jams.");
    else ImGui::TextColored(col, "%d jam%s: %.1f%% of rounds, about one every %.0f", R.jams, R.jams == 1 ? "" : "s", R.jamRate(), R.mrbf());
    for (int m = 1; m < (int)sim::Malf::Count; m++) {
        if (!R.byType[m]) continue;
        ImGui::BulletText("%s: %d", sim::malfName((sim::Malf)m), R.byType[m]);
        if (tipHover()) ImGui::SetTooltip("%s", sim::malfAdvice((sim::Malf)m));
    }
    if (!R.note.empty()) ImGui::TextColored(colBad(), "%s", R.note.c_str());
    if (!R.events.empty() && ImGui::TreeNode("When they happened")) {
        for (size_t i = 0; i < R.events.size() && i < 40; i++) {
            const auto& e = R.events[i];
            int pc = (int)std::lround((e.power - 1) * 100);
            ImGui::Text("after %3d: %s  (round %+d%%, %s grip)", e.shot, sim::malfName(e.malf), pc,
                        e.limp < 0.08 ? "firm" : e.limp < 0.17 ? "average" : "loose");
        }
        ImGui::TreePop();
    }
    if (R.jams) ImGui::TextDisabled("Hover a jam type for what usually causes it.");
}

static bool detail(StatsDetail d) { return (int)CFG.stats >= (int)d; }

static void muted(const char* t) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", t); ImGui::PopTextWrapPos(); }
// The arm's freedom: the elbow's swing, the wrist's roll, the hand lifted off the gun, or placed anywhere off it.
// Returns true when something was let go of (so it goes on the undo list).
static bool armFreedom(HandHold& h, const char* which) {
    if (std::string(which) == "butt") return false;
    bool done = false;
    auto fin = [&]() { if (ImGui::IsItemDeactivatedAfterEdit()) done = true; };
    if (!h.on) {
        if (pCheck("Placed (not hanging)", h.free, "Off the gun, the hand hangs at the side. Placed: it's wherever you put it\n(a pouch, a vest, holding a magazine up). Or drag the blue dot at the hand.")) done = true;
        if (h.free) {
            pNumU("Forward##hand", h.pos[0], 5, Q::Length, -1, "The wrist, from the aiming eye: + forward.", SIGNED); fin();
            pNumU("Up##hand", h.pos[1], 5, Q::Length, -1, "+ up (it's usually below the eye: negative).", SIGNED); fin();
            pNumU("Right##hand", h.pos[2], 5, Q::Length, -1, "+ to the right.", SIGNED); fin();
        }
    }
    pNum("Elbow", h.elbow, 1, "%.0f\xc2\xb0", "Swings the elbow round (+ up and out, - down and in). Or drag the blue dot at the elbow."); fin();
    h.elbow = std::clamp(h.elbow, -180.0, 180.0);
    pNum("Wrist roll", h.twist, 1, "%.0f\xc2\xb0", h.on ? "Rolls the hand about the grip (to come over the top, or under)." : "Rolls the hand about the forearm."); fin();
    h.twist = std::clamp(h.twist, -180.0, 180.0);
    if (h.on) { pNumU("Off the gun's side", h.dz, 1, Q::Length, -1, "Lifts the hand away from the side of the gun (a loose hold, fingers off a part).", SIGNED); fin(); }
    return done;
}
static void handControls(const char* title, HandHold& h, const char* which, const char* tip) {
    ImGui::PushID(which);
    bool on = h.on;
    if (ImGui::Checkbox(title, &on)) {
        h.on = on;
        if (on && h.part.empty()) {  // start it in the middle of the gun's side; drag it where it goes
            const auto& br = A.design.breech;
            V2 c = br.on && br.hasFace ? V2{br.face.x + (std::string(which) == "support" ? 120 : std::string(which) == "butt" ? -250 : -40), br.face.y + 25} : V2{0, 0};
            h.at = c; h.angle = defaultHandAngle(which);
            for (int i = (int)A.design.bodies.size() - 1; i >= 0; i--) if (sim::inPoly(c.x, c.y, A.design.bodies[i].pts)) { h.part = A.design.bodies[i].id; break; }
            if (h.part.empty() && !A.design.bodies.empty()) h.part = A.design.bodies[0].id;
        }
        commit();
    }
    if (tip && tipHover()) ImGui::SetTooltip("%s", tip);
    if (std::string(which) != "butt" && ImGui::TreeNode(h.on ? "Arm and wrist" : "Where the hand is")) {
        if (armFreedom(h, which)) commit();
        ImGui::TreePop();
    }
    if (h.on) {
        if (std::string(which) != "butt") {
            bool firing = std::string(which) == "firing";
            double a0 = defaultHandAngle(which, ""), a1 = defaultHandAngle(which, firing ? "wrist" : "vertical");
            int st = std::fabs(h.angle - a0) < 0.5 ? 0 : std::fabs(h.angle - a1) < 0.5 ? 1 : 2;
            prop("Style");
            if (ImGui::Combo("##style", &st, firing ? "Pistol grip\0Rifle wrist\0Custom\0" : "Fore-end\0Vertical grip\0Custom\0") && st < 2) { h.angle = st == 0 ? a0 : a1; commit(); }
        }
        pNum("Angle", h.angle, 1, "%.0f\xc2\xb0", "The line the fingers wrap round (the butt pad's line, for the butt). 0 points at the muzzle, 90 straight down.\nOr drag the dot at the end of its green line.");
        int bi = A.design.bodyIndex(h.part);
        ImGui::TextDisabled("On: %s", bi >= 0 ? (A.design.bodies[bi].name.empty() ? h.part.c_str() : A.design.bodies[bi].name.c_str()) : "nothing");
        bool fingersOpen = std::string(which) != "butt" && ImGui::TreeNode("Fingers");
        if (std::string(which) == "firing") tutAnchor("pose:fingers");
        if (fingersOpen) {
            int hand = std::string(which) == "firing" ? 0 : 1;
            A.poseHand = hand;  // (its fingertips show in the view)
            if (ImGui::Button("Zoom to this hand", {-1, 0})) poseFocusHand(hand);
            if (!h.digitsSet) { digitsOf(h, which, h.digits); h.digitsSet = true; }
            prop("Preset", "A starting point for the whole hand. Then drag any fingertip in the view where you want it (the joints follow, within what a real hand can do; Shift-drag just curls it), or pick a digit here and set its joints.");
            if (ImGui::BeginCombo("##preset", "Choose...")) {
                const char* n = handPresets();
                for (int i = 0; *n; i++, n += std::strlen(n) + 1) if (ImGui::Selectable(n)) { applyHandPreset(h, which, handPresetId(i)); commit(); }
                ImGui::EndCombo();
            }
            for (int d = 0; d < DIGIT_COUNT; d++) {
                if (d) ImGui::SameLine(0, 3);
                if (accentButton(digitName(d), A.poseDigit == d, {0, 0})) A.poseDigit = d;
            }
            DigitPose& dp = h.digits[std::clamp(A.poseDigit, 0, DIGIT_COUNT - 1)];
            const char* jn[3] = {"Base joint", "Middle joint", "Tip joint"};
            if (A.poseDigit == DIGIT_THUMB) { jn[0] = "Root (by the wrist)"; jn[1] = "Knuckle"; }
            for (int k = 0; k < 3; k++) pNum(jn[k], dp.bend[k], 1, "%.0f\xc2\xb0", "+ curls it toward the palm.");
            pNum("Spread", dp.spread, 1, "%.0f\xc2\xb0", A.poseDigit == DIGIT_THUMB ? "+ opens the thumb out from the index finger, into an L." : "+ swings it toward the little finger.");
            if (A.poseDigit == DIGIT_THUMB)
                pNum("Across the palm", dp.oppose, 1, "%.0f\xc2\xb0", "Swings the thumb round in front of the palm, turning it so its pad meets the fingers' pads (to touch the little finger, about 80).");
            clampDigit(std::clamp(A.poseDigit, 0, DIGIT_COUNT - 1), dp);
            if (ImGui::IsItemDeactivatedAfterEdit()) commit();
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
}
// Recorded animations: keys on a timeline where both hands are; the hands go smoothly through them
static void animSection() {
    bool open = section("Animations", A.animEdit);
    tutAnchor("anim:section");
    if (!open) return;
    auto& D = A.design;
    bool playing = A.S && sim::animPlaying(*A.S);
    if (pCheck("Edit an animation", A.animEdit, "On: the hands you drag are the animation's, at the moment on its timeline.\n"
                                               "Off: they're the hold's again.")) { A.animPreview = false; if (!A.animEdit) A.animT = 0; }
    tutAnchor("anim:edit");
    if (!A.animEdit) { muted("Reload, inspect, clear a jam, or work a control, with hands you place. Not animated, it just happens."); return; }
    auto slots = animSlots(D);
    if (std::find(slots.begin(), slots.end(), A.animSlot) == slots.end()) A.animSlot = slots.front();
    prop("Animation");
    if (ImGui::BeginCombo("##aslot", animSlotName(D, A.animSlot).c_str())) {
        for (auto& s : slots) {
            auto it = D.anims.find(s);
            int n = it == D.anims.end() ? 0 : (int)it->second.keys.size();
            std::string lab = animSlotName(D, s) + (n ? "  (" + std::to_string(n) + (n == 1 ? " key)" : " keys)") : "  (not animated)");
            if (ImGui::Selectable(lab.c_str(), s == A.animSlot)) { A.animSlot = s; A.animT = 0; A.animPreview = false; }
        }
        ImGui::EndCombo();
    }
    tutAnchor("anim:slot");
    Animation& an = D.anims[A.animSlot];
    static std::map<std::string, double> spans;   // how long each timeline is shown (it grows with its keys)
    double len = animLength(an), span = std::max({2.0, len + 0.5, spans[A.animSlot]});
    if (an.keys.empty()) muted("Not animated yet: on the bench the plain action happens. Move the time slider, then drag a hand: a key is made there. The hands start and end in the aiming hold.");
    else muted(TextFormat("%d %s, %.2f s long. Move the slider and drag a hand to add a key; drag one at a key to change it.", (int)an.keys.size(), an.keys.size() == 1 ? "key" : "keys", len));
    // the timeline, with the keys marked on it
    float t = (float)A.animT;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##atime", &t, 0, (float)span, "%.2f s")) { A.animT = t; A.animPreview = false; }
    tutAnchor("anim:time");
    {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        auto* dl = ImGui::GetWindowDrawList();
        float pad = ImGui::GetStyle().GrabMinSize / 2 + 2;
        for (auto& k : an.keys) {
            float x = a.x + pad + (b.x - a.x - 2 * pad) * (float)(k.t / span);
            bool here = std::fabs(k.t - A.animT) < 0.03;
            dl->AddTriangleFilled({x, b.y - 2}, {x - 4, b.y + 5}, {x + 4, b.y + 5}, here ? IM_COL32(255, 205, 60, 255) : IM_COL32(110, 230, 140, 255));
        }
        ImGui::Dummy({0, 6 * CFG.uiScale});
    }
    {
        double s = span;
        if (pNum("Timeline", s, 0.1, "%.1f s", "How long the timeline is. Make it longer for a longer animation (up to 60 s).\nIt never gets shorter than its last key.")) {
            spans[A.animSlot] = std::clamp(s, 0.5, 60.0);
        }
    }
    float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    if (ImGui::Button("< Key", {w3, 0})) { for (int i = (int)an.keys.size() - 1; i >= 0; i--) if (an.keys[i].t < A.animT - 0.031) { A.animT = an.keys[i].t; break; } A.animPreview = false; }
    ImGui::SameLine();
    if (ImGui::Button(A.animPreview ? "Stop" : "Preview", {w3, 0})) { A.animPreview = !A.animPreview; if (A.animPreview && A.animT >= len - 1e-3) A.animT = 0; }
    if (tipHover()) ImGui::SetTooltip("Plays the hands only, the gun standing still.");
    ImGui::SameLine();
    if (ImGui::Button("Key >", {w3, 0})) { for (auto& k : an.keys) if (k.t > A.animT + 0.031) { A.animT = k.t; break; } A.animPreview = false; }
    ImGui::BeginDisabled(an.keys.empty() || playing);
    bool playIt = ImGui::Button("Play on the gun", {-1, 0});
    tutAnchor("anim:play");
    if (playIt) {
        A.animPreview = false;
        A.S->design.anims = D.anims;
        sim::playAnim(*A.S, A.animSlot);
        A.running = true;
    }
    if (tipHover()) ImGui::SetTooltip("Runs it for real: gripping hands pull their parts through the physics.\nIf the gun doesn't work that way, it shows (a slide not pulled back far enough won't chamber).");
    ImGui::EndDisabled();
    if (A.animPreview) {   // the hands move along; the slider follows
        A.animT += GetFrameTime();
        if (A.animT > len) { A.animT = len; A.animPreview = false; }
    }
    // the key here, if any
    int ki = animKeyAt(an, A.animT);
    if (ki >= 0) {
        AnimKey& k = an.keys[ki];
        heading("This key");
        double kt = k.t;
        if (pNum("Time", kt, 0.01, "%.2f s", "When the hands get here, from the start.")) {
            k.t = std::max(0.0, kt); A.animT = k.t;
            std::stable_sort(an.keys.begin(), an.keys.end(), [](const AnimKey& x, const AnimKey& y) { return x.t < y.t; });
            ki = animKeyAt(an, A.animT); if (ki < 0) return;
        }
        AnimKey& kk = an.keys[ki];
        bool edit = false;
        edit |= pCheck("Firing hand", kk.firing.on, "On the gun at this key. Off: away from the gun (it hangs at the side).");
        if (kk.firing.on) edit |= pCheck("  grips its part", kk.gripF, "From here the hand holds the part it's on and moves it, until a key lets go.");
        edit |= pCheck("Support hand", kk.support.on, "On the gun at this key. Off: away from the gun.");
        if (kk.support.on) edit |= pCheck("  grips its part##s", kk.gripS, "From here the hand holds the part it's on (a slide, a bolt handle, a catch)\nand moves it, until a key lets go.");
        tutAnchor("anim:grip");
        if (kk.firing.on && kk.gripF) ImGui::TextDisabled("Firing hand holds: %s", bname(kk.firing.part).c_str());
        if (kk.support.on && kk.gripS) ImGui::TextDisabled("Support hand holds: %s", bname(kk.support.part).c_str());
        int ev = kk.event == "dropmag" ? 1 : kk.event == "freshmag" ? 2 : 0;
        prop("Magazine", "What happens to the magazine as the hands reach this key.\nDrop: it's let go and falls out (or slides out sideways).\nFresh one in: a full magazine goes into the well.");
        if (ImGui::Combo("##aev", &ev, "Nothing\0Drop it\0Fresh one in\0")) { kk.event = ev == 1 ? "dropmag" : ev == 2 ? "freshmag" : ""; edit = true; }
        tutAnchor("anim:mag");
        if (ImGui::TreeNode("Arms at this key")) {   // elbows, wrists, hands placed off the gun
            ImGui::TextDisabled("Firing hand"); ImGui::PushID("fk"); edit |= armFreedom(kk.firing, "firing"); ImGui::PopID();
            ImGui::TextDisabled("Support hand"); ImGui::PushID("sk"); edit |= armFreedom(kk.support, "support"); ImGui::PopID();
            ImGui::TreePop();
        }
        // the gun itself at this key: lifted to look at, tipped to see into the port
        if (ImGui::TreeNodeEx("The gun at this key", ImGuiTreeNodeFlags_DefaultOpen)) {
            muted("Moves the whole gun from where the hold has it, about the firing hand. Hands on it go with it. Or drag the pink Gun dot to move it, and the Aim dot to point it (Shift: roll).");
            bool g = false;
            g |= pNumU("Forward", kk.gun[0], 1, Q::Length, -1, "+ away from you.", SIGNED);
            g |= pNumU("Up", kk.gun[1], 1, Q::Length, -1, "+ up.", SIGNED);
            g |= pNumU("Right", kk.gun[2], 1, Q::Length, -1, "+ toward the gun's right side.", SIGNED);
            g |= pNum("Muzzle up", kk.gun[3], 1, "%.0f\xc2\xb0", "+ raises the muzzle.");
            g |= pNum("Turn", kk.gun[4], 1, "%.0f\xc2\xb0", "+ swings the muzzle to the right.");
            g |= pNum("Roll", kk.gun[5], 1, "%.0f\xc2\xb0", "+ rolls the top of the gun to the right (to look into a port on the left, roll it right).");
            static bool gunDirty = false;   // saved (for undo) once the field is let go
            if (g) gunDirty = true;
            if (gunDirty && !ImGui::IsAnyItemActive()) { gunDirty = false; edit = true; }
            if (gunMoved(kk.gun) && ImGui::Button("Back to the hold##gun", {-1, 0})) { for (double& v : kk.gun) v = 0; edit = true; }
            ImGui::TreePop();
        }
        if (edit) commit();
        if (ImGui::Button("Delete this key", {-1, 0})) { an.keys.erase(an.keys.begin() + ki); commit(); }
    }
    if (ki < 0 && ImGui::Button("Add a key here", {-1, 0})) { editHand(0); commit(); }
    if (ki < 0 && tipHover()) ImGui::SetTooltip("A key at this moment, with the hands and the gun where they are now.\nThen move the gun or drag a hand.");
    if (!an.keys.empty()) {
        ImGui::Dummy({0, 2 * CFG.uiScale});
        if (ImGui::Button("Clear this animation", {-1, 0})) ImGui::OpenPopup("##aclear");
        if (ImGui::BeginPopup("##aclear")) {
            ImGui::TextUnformatted("Remove every key? (Undo brings them back.)");
            if (ImGui::Button("Clear it")) { an.keys.clear(); A.animT = 0; commit(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
}
static void posePanel() {
    auto& P = A.design.pose;
    ImGui::PushFont(gFontBold, 18.0f); ImGui::TextUnformatted("Pose"); ImGui::PopFont();
    std::string held = heldWith(A.design);
    ImGui::TextDisabled("%s", held.empty() ? "Place the firing hand to hold it." : ("Held with " + held + (P.butt.on ? ", against the shoulder." : ".")).c_str());
    if (playerCharacter()) {
        if (pCheck("My character", CFG.poseCharacter, "On: your character (main menu > Your character) holds the gun, with its own arm lengths.\nOff: the plain mannequin.")) saveSettings();
    } else muted("Make your own character on the main menu (Your character) and it holds the gun here instead of the mannequin.");
    ImGui::Dummy({0, 4 * CFG.uiScale});
    // which hold
    for (int h = 0; h < HOLD_COUNT; h++) {
        if (h) ImGui::SameLine();
        if (accentButton(holdName(h), A.poseHold == h, {0, 0})) { A.poseHold = h; poseView(); }
        tutAnchor(h == 0 ? "pose:holds" : "pose:holds2");
    }
    ImGui::Dummy({0, 4 * CFG.uiScale});
    if (section("Body", !A.animEdit)) {
        BodyHold& b = P.hold[A.poseHold];
        if (!b.set) b = holdOf(A.design, A.poseHold);
        BodyHold was = b;
        muted("Drag the orange dot over the head to move the body. These are where the aiming eye sits against the gun.");
        pNumU("Eye X", b.eye.x, 2, Q::Length, -1, "Along the gun: + toward the muzzle.", SIGNED);
        double up = -b.eye.y;
        if (pNumU("Eye Y", up, 2, Q::Length, -1, "Up and down: + is up.", SIGNED)) b.eye.y = -up;
        pNumU("Eye Z", b.eyeZ, 2, Q::Length, -1, "Across the gun: + is the gun's right side.", SIGNED);
        pNum("Body turned", b.yaw, 1, "%.0f\xc2\xb0", "The body turned against the gun (+ to the right). Rifle shooters stand bladed.");
        pNum("Muzzle down", b.tilt, 1, "%.0f\xc2\xb0", "The gun tipped against the body: about 35 for a lowered gun, 0 when aiming.");
        if (P.butt.on) {
            pNumU("Shoulder forward", b.pocket[0], 2, Q::Length, -1, "Where the butt beds into the shoulder, from the eye: + forward. Or drag the orange Shoulder dot.", SIGNED);
            double down = -b.pocket[1];
            if (pNumU("Shoulder down", down, 2, Q::Length, -1, "How far below the eye the shoulder pocket is.", SIGNED)) b.pocket[1] = -down;
            pNumU("Shoulder across", b.pocket[2], 2, Q::Length, -1, "Across the body from the eye: + toward the right.", SIGNED);
        }
        if (std::memcmp(&was, &b, sizeof b) != 0 || !was.set) { b.set = true; }
        if (ImGui::IsItemDeactivatedAfterEdit()) commit();
        if (P.butt.on && ImGui::Button("Put the shoulder on the butt", {-1, 0})) { poseBedShoulder(); commit(); }
        if (P.butt.on && tipHover()) ImGui::SetTooltip("Moves the shoulder pocket to wherever the butt is now. The eye stays where it is,\nso line the eye up with the sights first.");
        if (ImGui::Button("Reset this hold", {-1, 0})) { b = BodyHold(); b = holdOf(A.design, A.poseHold); b.set = false; commit(); poseView(); }
    }
    if (section("Hands and shoulder", !A.animEdit)) {
        muted("Drag a green dot to move it onto any part, and the dot at the end of its line to turn it. All three holds share them.");
        handControls("Firing hand", P.firing, "firing", "The hand on the trigger.");
        handControls("Support hand", P.support, "support", "The other hand. Off: held with one hand.");
        handControls("Stock in the shoulder", P.butt, "butt", "Where the gun beds into the shoulder (a stock's butt). Off: nothing touches the shoulder.");
    }
    animSection();
    PoseReport R = poseReport();
    if (R.rightShort > 0.5) ImGui::TextColored(colBad(), "The firing arm can't reach: %s short.", fmtQ(Q::Length, R.rightShort, 0).c_str());
    if (R.leftShort > 0.5) ImGui::TextColored(colBad(), "The support arm can't reach: %s short.", fmtQ(Q::Length, R.leftShort, 0).c_str());
    if (P.butt.on) {
        if (R.buttIn) ImGui::TextColored(colGood(), "The butt sits in the shoulder.");
        else ImGui::TextColored(colWarn(), "The butt is %s from the shoulder pocket (the orange ball).", fmtQ(Q::Length, R.buttOff, 0).c_str());
    }
    ImGui::Dummy({0, 6 * CFG.uiScale});
    if (ImGui::Button("Done posing", {-1, 0})) { leavePose(); A.running = true; }
}
void testPanel() {
    panelHeader();
    if (!A.S) { ImGui::PopItemWidth(); ImGui::End(); return; }
    if (A.posing) { posePanel(); ImGui::PopItemWidth(); ImGui::End(); return; }
    auto& S = *A.S;
    bool batt = sim::inBattery(S);
    sim::Body* mag = sim::seatedMag(S);   // removable (latched) or built in

    // ---- the state of the gun, in one line, then what to do about it
    ImGui::PushFont(gFontBold, 18.0f);
    if (S.malf != sim::Malf::None) ImGui::TextColored(colBad(), "Jammed: %s", sim::malfName(S.malf));
    else if (!A.running) ImGui::TextDisabled("Paused");
    else if (batt && S.chambered) ImGui::TextColored(colGood(), "Ready");
    else if (batt && S.spent) ImGui::TextColored(colWarn(), "Empty case in the chamber");
    else if (batt) ImGui::TextColored(colWarn(), "Chamber empty");
    else ImGui::TextUnformatted("Action open");
    ImGui::PopFont();
    if (S.malf != sim::Malf::None) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", sim::malfAdvice(S.malf)); ImGui::PopTextWrapPos(); }
    std::string line = std::to_string(S.shots) + (S.shots == 1 ? " shot" : " shots");
    if (S.magTmpl) line += mag ? "   \xC2\xB7   magazine " + std::to_string(mag->rounds) + "/" + std::to_string(S.magTmpl->magCap) : "   \xC2\xB7   no magazine";
    else if (mag && mag->d) line += "   \xC2\xB7   magazine " + std::to_string(mag->rounds) + "/" + std::to_string(mag->d->magCap) + " (built in)";
    else if (S.feedMode == "internal") line += "   \xC2\xB7   " + std::to_string(S.rounds) + " left";
    if (S.hasFace && !S.magTmpl) line += "   \xC2\xB7   " + std::to_string(sim::looseRounds(S)) + " on the bench";
    const auto& br = S.design.breech;
    if (!S.strikerIgnition && br.openBolt)    // an open bolt: held back on the sear, or running
        line += std::string("   \xC2\xB7   ") + (S.searHeld ? "bolt held open on the sear" : S.boltFree ? "bolt running" : "bolt closed (rack it to cock)");
    else if (!S.strikerIgnition && br.cockAt > 0)  // the built-in hammer or striker
        line += std::string("   \xC2\xB7   ") + (br.hammer == "hammer" ? "hammer " : "striker ") + (S.cocked ? "cocked" : "down");
    ImGui::TextDisabled("%s", line.c_str());

    // ---- more numbers for those who want them (Settings > Interface > Statistics)
    for (size_t gi = 0; gi < S.gas.size(); gi++)   // what the gas gave its piston on the last shot
        if (S.gas[gi].impulse > 0) {
            const sim::Body* P = S.gas[gi].P;
            double m = P ? (P->root ? P->root->m : P->m) : 0;
            ImGui::TextDisabled("%s: %.2f N s last shot%s", S.gas.size() > 1 ? ("Gas port " + std::to_string(gi + 1)).c_str() : "Gas",
                                S.gas[gi].impulse, m > 0 ? TextFormat(" (about %.1f m/s to the piston)", S.gas[gi].impulse / m) : "");
        }
    if (detail(StatsDetail::Standard)) {
        heading("Details");
        if (S.shots > 0) {
            ImGui::Text("Felt recoil  %s", fmtQ(Q::Impulse, S.recoilImpulse).c_str());
            if (tipHover()) ImGui::SetTooltip("What the frame (your hands) took from the last shot: the push from the moving parts, springs and stops.\nSofter springs, heavier slides and buffers spread it out.");
            if (detail(StatsDetail::Engineering)) { ImGui::SameLine(); ImGui::TextDisabled("peak %s", fmtQ(Q::Force, S.recoilPeak).c_str()); }
        }
        if (int jams = sim::jamCount(S)) {
            ImGui::Text("Jams so far  %d", jams);
            if (ImGui::IsItemHovered() && CFG.tooltips) {
                std::string tip;
                for (int m = 1; m < (int)sim::Malf::Count; m++)
                    if (S.malfCount[m]) tip += std::string(sim::malfName((sim::Malf)m)) + ": " + std::to_string(S.malfCount[m]) + "\n";
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
        for (auto& J : S.joints) {
            if (J.type == sim::JT::Lug) { ImGui::Text("Lugs (%s)", J.A->d->name.c_str()); ImGui::SameLine(); ImGui::TextColored(J.engaged ? colGood() : colBad(), J.engaged ? "locked" : "unlocked"); }
            if (J.type == sim::JT::Latch) ImGui::Text("Catch (%s)  %s", J.A->d->name.c_str(), J.state == sim::LatchState::Hold ? "holding" : J.state == sim::LatchState::Armed ? "armed" : "released");
        }
        for (auto& b : S.bodies) {
            if (!mainPart(b)) continue;
            ImGui::Text("%s", b.d->name.c_str());
            ImGui::SameLine(170 * CFG.uiScale);
            ImGui::TextDisabled("%s", fmtQ(Q::Length, (b.x - b.x0) * 1000).c_str());
            if (detail(StatsDetail::Engineering)) {
                ImGui::SameLine(250 * CFG.uiScale); ImGui::TextDisabled("%s", fmtQ(Q::Velocity, std::hypot(b.vx, b.vy), 1).c_str());
                if (b.invIr > 0) { ImGui::SameLine(); ImGui::TextDisabled("%.0f\xC2\xB0", b.roll / DEG2RAD); }
            }
        }
        if (detail(StatsDetail::Engineering)) ImGui::TextDisabled("Simulated time %.1f ms", S.t * 1000);
    }

    // ---- how fast it runs, and the slow motion after each shot (saved with the settings)
    if (section("Speed", true)) {
        bool ch = false, wasEdited = editedAny;
        static const float sp[] = {1, 0.5f, 0.25f, 0.1f};
        int bi = 0;
        for (int i = 0; i < 4; i++) if (std::fabs(CFG.benchSpeed - sp[i]) < 1e-4f) bi = i;
        prop("Speed", "How fast the bench runs between shots. The simulation is the same at any speed.");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##bs", &bi, "Real time\0Half speed\0Quarter speed\0One tenth\0")) { CFG.benchSpeed = sp[bi]; ch = true; }
        ch |= pCheck("Slow motion on shots", CFG.slowOnShot, "Slows right down for a moment after every shot, so you can watch the action cycle.");
        if (CFG.slowOnShot) {
            static const float ss[] = {0.25f, 0.1f, 0.05f, 0.02f, 0.01f};
            int si = 3;
            for (int i = 0; i < 5; i++) if (std::fabs(CFG.slowSpeed - ss[i]) < 1e-4f) si = i;
            prop("How slow");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##ss", &si, "1/4\0" "1/10\0" "1/20\0" "1/50\0" "1/100\0")) { CFG.slowSpeed = ss[si]; ch = true; }
            prop("For how long", "Of simulated time after the shot.");
            ImGui::SetNextItemWidth(-1);
            ch |= ImGui::SliderFloat("##sw", &CFG.slowWindowMs, 20, 400, "%.0f ms"); typeable();
        }
        if (ch) saveSettings();
        editedAny = wasEdited;  // bench settings never touch the design
    }

    reliabilitySection();

    // ---- bench conditions and the controls, folded away
    bool benchOpen = section("Bench", false);
    tutAnchor("bench:section");
    if (benchOpen) {
        {
            float hd = (float)toDisp(Q::Force, A.handN), lo = (float)toDisp(Q::Force, 20), hi = (float)toDisp(Q::Force, 600);
            char fmt[24]; std::snprintf(fmt, sizeof fmt, "%%.0f %s", unitOf(Q::Force));
            prop("Hand strength", "How hard your hand can push and pull when you work the action. A test condition, like a weak or strong shooter.");
            if (ImGui::SliderFloat("##hand", &hd, lo, hi, fmt)) A.handN = (float)fromDisp(Q::Force, hd); typeable();
        }
        if (!S.magTmpl && mag) { if (ImGui::Button("Top up the magazine")) topUpMagazines(); }
        if (S.magTmpl) {
            ImGui::Button("Hold to release magazine");
            A.releaseBtnHeld = A.releaseBtnHeld || ImGui::IsItemActive();
            ImGui::SameLine(); if (ImGui::Button("New magazine")) spawnSpareMag();
            tutAnchor("bench:newmag");
            if (ImGui::Button("Top up magazines")) topUpMagazines();
            if (tipHover()) ImGui::SetTooltip("Fills every magazine, in the gun and on the bench.\nTo load one round at a time, drag a loose round onto the top of a magazine that's out of the gun.");
        }
        if (S.hasFace) { if (ImGui::Button("Put a round on the bench")) spawnBenchRound(); }
    }
    if (section("Controls", false)) {
        auto k = [](Act a) { return keyName(keyOf(a)); };
        ImGui::TextDisabled("Left-drag any moving part to work it by hand: rack slides, work bolts, pull magazines, pick up rounds.");
        ImGui::TextDisabled("Drag a bolt handle to lift and pull it in one motion.");
        ImGui::Text("%s  fire", k(Act::Fire)); ImGui::SameLine(160 * CFG.uiScale); ImGui::Text("%s  magazine release", k(Act::MagRelease));
        ImGui::Text("%s  quick reload", k(Act::Reload)); ImGui::SameLine(160 * CFG.uiScale); ImGui::Text("%s  new magazine", k(Act::NewMag));
        ImGui::Text("%s  loose round", k(Act::LooseRound)); ImGui::SameLine(160 * CFG.uiScale); ImGui::Text("%s  pause", k(Act::Pause));
        ImGui::Text("%s  restart", k(Act::Restart)); ImGui::SameLine(160 * CFG.uiScale); ImGui::Text("1-4  your controls");
        ImGui::Text("%s  top up magazines", k(Act::FillMags));
        ImGui::TextDisabled("Drag a loose round onto the top of a magazine that's out of the gun to load it.\nSpent cases are cleared after 10 seconds, and empty magazines left alone after 30.");
        ImGui::TextDisabled("Rebind keys in Settings > Controls.");
    }
    ImGui::PopItemWidth();
    ImGui::End();
}
