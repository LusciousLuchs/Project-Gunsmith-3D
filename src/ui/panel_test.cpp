// Gunsmith3D - Test mode panels: bench controls and motion trace.
#include "hand.hpp"
#include "app/pose.hpp"
#include "app/app.hpp"
#include "app/render.hpp"
#include "app/reltest.hpp"
#include "app/settings.hpp"
#include "app/keys.hpp"
#include "ui/ui.hpp"
#include "imgui.h"

#include <cstring>
#include <algorithm>
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
        if (std::string(which) != "butt" && ImGui::TreeNode("Fingers")) {
            int hand = std::string(which) == "firing" ? 0 : 1;
            A.poseHand = hand;  // (its fingertips show in the view)
            if (ImGui::Button("Zoom to this hand", {-1, 0})) poseFocusHand(hand);
            if (!h.digitsSet) { digitsOf(h, which, h.digits); h.digitsSet = true; }
            prop("Preset", "A starting point for the whole hand. Then pick a digit (here, or its fingertip in the view) and bend it.");
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
            for (int k = 0; k < 3; k++) { pNum(jn[k], dp.bend[k], 1, "%.0f\xc2\xb0", "+ curls it toward the palm."); dp.bend[k] = std::clamp(dp.bend[k], -25.0, 110.0); }
            pNum("Spread", dp.spread, 1, "%.0f\xc2\xb0", A.poseDigit == DIGIT_THUMB ? "+ swings the thumb away from the palm." : "+ swings it toward the little finger.");
            dp.spread = std::clamp(dp.spread, -40.0, 60.0);
            if (ImGui::IsItemDeactivatedAfterEdit()) commit();
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
}
static void posePanel() {
    auto& P = A.design.pose;
    ImGui::PushFont(gFontBold, 18.0f); ImGui::TextUnformatted("Pose"); ImGui::PopFont();
    std::string held = heldWith(A.design);
    ImGui::TextDisabled("%s", held.empty() ? "Place the firing hand to hold it." : ("Held with " + held + (P.butt.on ? ", against the shoulder." : ".")).c_str());
    ImGui::Dummy({0, 4 * CFG.uiScale});
    // which hold
    for (int h = 0; h < HOLD_COUNT; h++) {
        if (h) ImGui::SameLine();
        if (accentButton(holdName(h), A.poseHold == h, {0, 0})) { A.poseHold = h; poseView(); }
    }
    ImGui::Dummy({0, 4 * CFG.uiScale});
    if (section("Body", true)) {
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
        if (std::memcmp(&was, &b, sizeof b) != 0 || !was.set) { b.set = true; }
        if (ImGui::IsItemDeactivatedAfterEdit()) commit();
        if (ImGui::Button("Reset this hold", {-1, 0})) { b = BodyHold(); b = holdOf(A.design, A.poseHold); b.set = false; commit(); poseView(); }
    }
    if (section("Hands and shoulder", true)) {
        muted("Drag a green dot to move it onto any part, and the dot at the end of its line to turn it. All three holds share them.");
        handControls("Firing hand", P.firing, "firing", "The hand on the trigger.");
        handControls("Support hand", P.support, "support", "The other hand. Off: held with one hand.");
        handControls("Stock in the shoulder", P.butt, "butt", "Where the gun beds into the shoulder (a stock's butt). Off: nothing touches the shoulder.");
    }
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
    if (!S.strikerIgnition && br.cockAt > 0)  // the built-in hammer or striker
        line += std::string("   \xC2\xB7   ") + (br.hammer == "hammer" ? "hammer " : "striker ") + (S.cocked ? "cocked" : "down");
    ImGui::TextDisabled("%s", line.c_str());

    // ---- more numbers for those who want them (Settings > Interface > Statistics)
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
