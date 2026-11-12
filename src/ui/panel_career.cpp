// Gunsmith3D - Career mode's office: your shop's day, your guns and their rating sheets, building copies, the online
// store with its reviews, and customers' contracts. (Rules: core/career; the save and the bench test: app/career.)
#include "app/careermode.hpp"
#include "app/app.hpp"
#include "app/fonts.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

void openDesignInEditor(const Design& d, const std::string& msg);   // (panel_build.cpp)

namespace {
constexpr ImGuiWindowFlags FIXED = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
const char* STORE_NAME = "GunBench";   // (the online store: our own)
std::string usd(double v) { char b[32]; std::snprintf(b, sizeof b, v < 0 ? "-$%.0f" : "$%.0f", std::fabs(v)); return b; }
void stars(int n) {
    std::string s;
    for (int i = 0; i < 5; i++) s += i < n ? ICON_FA_STAR : "";
    ImGui::TextColored(n >= 4 ? ImVec4(1.0f, 0.78f, 0.25f, 1) : n == 3 ? ImVec4(0.85f, 0.8f, 0.6f, 1) : ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", s.c_str());
}
void wrapped(const std::string& t) { ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(t.c_str()); ImGui::PopTextWrapPos(); }
void wrappedDim(const std::string& t) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", t.c_str()); ImGui::PopTextWrapPos(); }
std::string briefLines(const career::Contract& k) {
    std::string s;
    char b[96];
    auto add = [&](const char* t) { s += (s.empty() ? "" : ", ") + std::string(t); };
    if (k.hands == 1) add("one-handed");
    if (k.hands == 2) add("a long gun");
    if (k.minEnergy > 0 && k.maxEnergy > 0) { std::snprintf(b, sizeof b, "a round of %.0f-%.0f J", k.minEnergy, k.maxEnergy); add(b); }
    else if (k.minEnergy > 0) { std::snprintf(b, sizeof b, "a round of %.0f J or more", k.minEnergy); add(b); }
    else if (k.maxEnergy > 0) { std::snprintf(b, sizeof b, "a round up to %.0f J", k.maxEnergy); add(b); }
    if (k.maxKg > 0) { std::snprintf(b, sizeof b, "%.2f kg at most", k.maxKg); add(b); }
    if (k.minCapacity > 0) { std::snprintf(b, sizeof b, "holds %d or more", k.minCapacity); add(b); }
    if (k.maxMoa > 0) { std::snprintf(b, sizeof b, "groups of %.1f MOA or better", k.maxMoa); add(b); }
    if (k.maxJam > 0) { std::snprintf(b, sizeof b, "%.1f%% jams at most", k.maxJam); add(b); }
    return s.empty() ? "anything goes" : s;
}
int tab = 0;
}  // namespace
void careerSetTab(int t) { tab = t; }

static void todayTab() {
    auto& C = CAREER;
    heading("What happened");
    for (auto& n : C.news) { ImGui::Bullet(); ImGui::SameLine(); wrapped(n); }
    heading("Your shop");
    int stock = 0, building = 0, listed = 0, taken = 0, offers = 0;
    for (auto& [g, k] : C.stock) stock += k;
    for (auto& b : C.builds) building += b.qty;
    for (auto& L : C.listings) if (L.active) listed++;
    for (auto& k : C.contracts) { if (k.state == 1) taken++; if (k.state == 0) offers++; }
    ImGui::Text("%d gun%s in stock, %d being built, %d listed online.", stock, stock == 1 ? "" : "s", building, listed);
    ImGui::Text("%d contract%s on the go, %d offer%s waiting.", taken, taken == 1 ? "" : "s", offers, offers == 1 ? "" : "s");
    for (auto& k : C.contracts)
        if (k.state == 1 && k.dueDay - C.day <= 2)
            ImGui::TextColored({1, 0.75f, 0.35f, 1}, "%s for %s is due %s.", k.title.c_str(), k.client.c_str(), k.dueDay <= C.day ? "today" : "tomorrow");
    ImGui::Dummy({0, 10 * CFG.uiScale});
    heading("How it works");
    wrappedDim("Design guns in the designer (every tool is yours). Rate each one on the bench here: the test fires it and works out what it's worth. "
               "Build copies: each takes hours of your working day and its material up front. List them in your online store: buyers order "
               "from your stock at the end of each day, and some leave reviews that say how your gun really ran. Customers' contracts pay more, "
               "but the delivered guns are tested against their brief. End the day when you're done.");
}

static void gunsTab() {
    auto& C = CAREER;
    const auto& guns = careerGuns();
    if (guns.empty()) { wrappedDim("No guns in your Blueprints folder yet. Open the designer, make one and save it."); return; }
    float prog = 0; std::string testing;
    bool busy = careerTestRunning(&prog, &testing);
    static std::map<std::string, int> qty;
    if (ImGui::BeginTable("##guns", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Gun", 0, 2.2f); ImGui::TableSetupColumn("Round", 0, 1.3f); ImGui::TableSetupColumn("Weight", 0, 0.9f);
        ImGui::TableSetupColumn("Jams", 0, 0.9f); ImGui::TableSetupColumn("Groups", 0, 0.9f); ImGui::TableSetupColumn("To build", 0, 1.2f);
        ImGui::TableSetupColumn("Worth", 0, 0.9f); ImGui::TableSetupColumn("In stock", 0, 0.8f); ImGui::TableSetupColumn("", 0, 3.6f);
        ImGui::TableHeadersRow();
        for (auto& g : guns) {
            ImGui::PushID(g.name.c_str());
            auto it = C.sheets.find(g.name);
            bool rated = it != C.sheets.end() && it->second.rated;
            career::GunSheet s = g.fresh;   // the gun as it is now, with the bench's numbers if it's been rated
            if (rated) { s.rated = true; s.jamRate = it->second.jamRate; s.fired = it->second.fired; s.manual = it->second.manual; }
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(g.name.c_str());
            ImGui::TextDisabled("%s, %d parts", !s.posed ? "hold not set" : s.twoHands ? "two hands" : "one hand", s.parts);
            if (!s.posed && tipHover()) ImGui::SetTooltip("Place its firing hand in Pose (Test bench > Pose): that's what says whether it's one- or two-handed.");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.caliber.c_str()); ImGui::TextDisabled("%.0f J", s.energy);
            ImGui::TableNextColumn(); ImGui::Text("%.2f kg", s.massKg);
            if (tipHover()) ImGui::SetTooltip("As drawn: every part is solid steel unless you set its weight in the designer (the part's Mass),\nso a wooden stock drawn solid counts as steel. Set real weights for a fair rating.");
            ImGui::TableNextColumn();
            if (rated) ImGui::TextColored(s.jamRate < 1 ? ImVec4(0.5f, 0.9f, 0.55f, 1) : s.jamRate < 5 ? ImVec4(0.95f, 0.8f, 0.4f, 1) : ImVec4(1, 0.45f, 0.4f, 1), "%.1f%%", s.jamRate);
            else ImGui::TextDisabled("not rated");
            ImGui::TableNextColumn(); if (s.moa > 0) ImGui::Text("%.1f MOA", s.moa); else ImGui::TextDisabled("shot");
            ImGui::TableNextColumn(); ImGui::Text("%.1f h", s.buildHours); ImGui::TextDisabled("%s material", usd(s.materialCost).c_str());
            ImGui::TableNextColumn(); if (rated) ImGui::Text("%s", usd(s.value()).c_str()); else ImGui::TextDisabled("?");
            ImGui::TableNextColumn(); ImGui::Text("%d", C.stock[g.name]);
            ImGui::TableNextColumn();
            if (busy && testing == g.name) ImGui::ProgressBar(prog, {-1, 0}, "On the bench...");
            else {
                ImGui::BeginDisabled(busy);
                if (ImGui::SmallButton(rated ? "Rate again" : "Rate on the bench")) careerTestStart(g.name);
                ImGui::EndDisabled();
                if (tipHover()) ImGui::SetTooltip("Fires it 30 times on the bench (as the reliability test does) and works out what it's worth.\nRate it again after you change it.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Designer")) { openDesignInEditor(g.d, "Opened " + g.name + ". Save it, then come back to the office (top left)."); enterWorkshop(false); }
                if (rated) {
                    int& q = qty[g.name]; if (q < 1) q = 1;
                    ImGui::SetNextItemWidth(110 * CFG.uiScale);
                    ImGui::InputInt("##q", &q, 1, 5); q = std::clamp(q, 1, 99);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Build")) {
                        C.sheets[g.name] = s;   // (built as it is now)
                        std::string why;
                        if (career::startBuild(C, g.name, q, &why)) { careerSave(); toast("Started " + std::to_string(q) + " x " + g.name + "."); }
                        else toast("Can't: " + why + ".");
                    }
                    if (tipHover()) ImGui::SetTooltip("%d copies: %s of material now, %.1f hours of work (your day is %.0f hours).", q, usd(s.materialCost * q).c_str(), s.buildHours * q, C.hoursPerDay);
                    bool listed = false; for (auto& L : C.listings) if (L.gun == g.name) listed = true;
                    if (!listed) { ImGui::SameLine(); if (ImGui::SmallButton("Sell online")) { C.listings.push_back({g.name, std::round(s.value() / 10) * 10, true, 0}); careerSave(); tab = 2; } }
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!C.builds.empty()) {
        heading("On the workbench");
        double h = 0;
        for (auto& b : C.builds) {
            auto it = C.sheets.find(b.gun);
            double per = it != C.sheets.end() ? it->second.buildHours : 1;
            h += b.qty * per - b.hoursDone;
            ImGui::Text("%d x %s", b.qty, b.gun.c_str());
            ImGui::SameLine(); ImGui::TextDisabled("(%.1f h left)", b.qty * per - b.hoursDone);
        }
        ImGui::TextDisabled("About %.0f day%s of work at %.0f hours a day. More hands come later (employees).", std::ceil(h / C.hoursPerDay), std::ceil(h / C.hoursPerDay) == 1 ? "" : "s", C.hoursPerDay);
    }
}

static void storeTab() {
    auto& C = CAREER;
    heading((std::string(STORE_NAME) + ": your listings").c_str());
    if (C.listings.empty()) wrappedDim("Nothing listed. Under Your guns, rate a gun and press Sell online.");
    for (size_t i = 0; i < C.listings.size(); i++) {
        auto& L = C.listings[i];
        ImGui::PushID((int)i);
        auto it = C.sheets.find(L.gun);
        double worth = it != C.sheets.end() ? it->second.value() : 0;
        ImGui::Checkbox("##on", &L.active);
        if (ImGui::IsItemEdited()) careerSave();
        if (tipHover()) ImGui::SetTooltip("Listed (ticked) or taken down.");
        ImGui::SameLine(); ImGui::Text("%s", L.gun.c_str());
        ImGui::SameLine(260 * CFG.uiScale);
        ImGui::SetNextItemWidth(110 * CFG.uiScale);
        double p = L.price;
        if (ImGui::InputDouble("##price", &p, 10, 100, "$%.0f")) L.price = std::max(1.0, p);
        if (ImGui::IsItemDeactivatedAfterEdit()) careerSave();
        ImGui::SameLine();
        double ratio = worth > 0 ? L.price / worth : 1;
        ImGui::TextColored(ratio < 0.9 ? ImVec4(0.5f, 0.9f, 0.55f, 1) : ratio < 1.15 ? ImVec4(0.85f, 0.85f, 0.8f, 1) : ImVec4(1, 0.55f, 0.4f, 1),
                           ratio < 0.9 ? "a bargain" : ratio < 1.15 ? "a fair price" : ratio < 1.6 ? "on the dear side" : "nobody will buy at that");
        ImGui::SameLine(); ImGui::TextDisabled("(worth about %s)  %d in stock, %d sold", usd(worth).c_str(), C.stock[L.gun], L.sold);
        ImGui::PopID();
    }
    heading("Reviews");
    if (C.reviews.empty()) wrappedDim("No reviews yet: buyers leave them after they've shot what they bought.");
    int shown = 0;
    for (auto r = C.reviews.rbegin(); r != C.reviews.rend() && shown < 40; ++r, ++shown) {
        stars(r->stars); ImGui::SameLine();
        ImGui::Text("%s", r->gun.c_str()); ImGui::SameLine(); ImGui::TextDisabled("%s, day %d", r->who.c_str(), r->day);
        ImGui::Indent(18 * CFG.uiScale); wrapped(r->text); ImGui::Unindent(18 * CFG.uiScale);
        ImGui::Dummy({0, 4 * CFG.uiScale});
    }
}

static void contractsTab() {
    auto& C = CAREER;
    float prog = 0; std::string testing;
    bool busy = careerTestRunning(&prog, &testing);
    auto card = [&](career::Contract& k) {
        ImGui::PushID(k.id);
        ImGui::BeginChild("##card", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::PushFont(gFontBold, 15.0f); ImGui::Text("%s", k.title.c_str()); ImGui::PopFont();
        ImGui::SameLine(); ImGui::TextDisabled("for %s", k.client.c_str());
        wrapped("\"" + k.brief + "\"");
        wrappedDim("They want: " + briefLines(k) + ".");
        ImGui::Text("%d gun%s, pays %s", k.qty, k.qty == 1 ? "" : "s", usd(k.pay).c_str());
        ImGui::SameLine(); ImGui::TextDisabled(k.state == 0 ? "  offer open until day %d" : "  due by day %d", k.state == 0 ? k.dueDay - 4 : k.dueDay);
        if (k.state == 0) {
            if (accentButton("Take it on", true)) { k.state = 1; careerSave(); }
            ImGui::SameLine();
            if (ImGui::Button("Turn it down")) { k.state = 3; k.verdict = "Turned down."; careerSave(); }
        } else if (k.state == 1) {
            static std::map<int, std::string> pickGun;
            std::string& pg = pickGun[k.id];
            ImGui::SetNextItemWidth(220 * CFG.uiScale);
            if (ImGui::BeginCombo("##gun", pg.empty() ? "Which gun?" : pg.c_str())) {
                for (auto& g : careerGuns()) {
                    int have = C.stock[g.name];
                    if (ImGui::Selectable((g.name + "  (" + std::to_string(have) + " built)").c_str(), pg == g.name)) pg = g.name;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (busy && testing == pg && !pg.empty()) ImGui::ProgressBar(prog, {160 * CFG.uiScale, 0}, "Testing...");
            else {
                ImGui::BeginDisabled(busy || pg.empty() || C.stock[pg] < k.qty);
                if (accentButton("Deliver", true)) careerTestStart(pg, k.id);
                ImGui::EndDisabled();
                if (tipHover()) ImGui::SetTooltip("They test it on the bench against their brief before they pay. You need %d built.", k.qty);
            }
            if (!k.verdict.empty()) wrappedDim(k.verdict);
        } else wrappedDim(k.verdict);
        ImGui::EndChild();
        ImGui::PopID();
    };
    bool any = false;
    heading("Your contracts");
    for (auto& k : C.contracts) if (k.state == 1) { card(k); any = true; }
    if (!any) ImGui::TextDisabled("None taken on.");
    any = false;
    heading("Offers");
    for (auto& k : C.contracts) if (k.state == 0) { card(k); any = true; }
    if (!any) ImGui::TextDisabled("None today: new customers come as the days pass.");
    if (ImGui::CollapsingHeader("Finished")) for (auto it = C.contracts.rbegin(); it != C.contracts.rend(); ++it) if (it->state >= 2) card(*it);
}

void careerScreen() {
    auto& C = CAREER;
    careerTestPoll();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)GetScreenWidth(), (float)GetScreenHeight()});
    ImGui::Begin("##career", nullptr, FIXED);
    // the top: your shop's day, money and name
    ImGui::PushFont(gFontBold, 22.0f); ImGui::Text("Your gun shop"); ImGui::PopFont();
    ImGui::SameLine(0, 30 * CFG.uiScale);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Day %d", C.day);
    ImGui::SameLine(0, 20 * CFG.uiScale);
    ImGui::TextColored(C.money < 0 ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.55f, 0.92f, 0.6f, 1), "%s", usd(C.money).c_str());
    ImGui::SameLine(0, 20 * CFG.uiScale);
    ImGui::TextDisabled("Reputation");
    ImGui::SameLine();
    ImGui::ProgressBar((float)(C.reputation / 100), {120 * CFG.uiScale, 0}, TextFormat("%.0f", C.reputation));
    if (tipHover()) ImGui::SetTooltip("Good reviews and contracts done well raise it: more buyers online, and bigger orders.");
    float right = GetScreenWidth() - 470 * CFG.uiScale;
    ImGui::SameLine(std::max(right, ImGui::GetCursorPosX() + 10));
    if (ImGui::Button(ICON_FA_PEN_RULER "  Designer")) enterWorkshop(false);
    if (tipHover()) ImGui::SetTooltip("Design and change guns. Save them, then come back (Office, top left of the designer).");
    ImGui::SameLine();
    float prog = 0;
    bool busy = careerTestRunning(&prog);
    ImGui::BeginDisabled(busy);
    if (accentButton(ICON_FA_MOON "  End the day", true)) { career::endDay(C); careerSave(); tab = 0; }
    ImGui::EndDisabled();
    if (tipHover()) ImGui::SetTooltip("Your working hours go into the builds; online buyers order; new customers write in.");
    ImGui::SameLine();
    if (ImGui::Button("Main menu")) { careerSave(); backToMenu(); ImGui::End(); return; }
    ImGui::Separator();
    if (ImGui::BeginTabBar("##ctabs")) {
        const char* names[] = {"Today", "Your guns", "Online store", "Contracts"};
        for (int i = 0; i < 4; i++) {
            ImGuiTabItemFlags f = tab == i ? ImGuiTabItemFlags_SetSelected : 0;
            bool open = ImGui::BeginTabItem(names[i], nullptr, f);
            if (ImGui::IsItemClicked()) tab = i;
            if (open) {
                ImGui::BeginChild("##ctab");
                if (i == 0) todayTab(); else if (i == 1) gunsTab(); else if (i == 2) storeTab(); else contractsTab();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        // (a tab picked by code is only forced once)
        static int lastForced = -1;
        if (lastForced != tab) lastForced = tab;
        ImGui::EndTabBar();
    }
    ImGui::End();
}
