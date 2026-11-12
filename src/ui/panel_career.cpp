// Gunsmith3D - Career mode's office: your shop's day, your guns and their rating sheets, building copies, the online
// store with its reviews, and customers' contracts. (Rules: core/career; the save and the bench test: app/career.)
#include "app/careermode.hpp"
#include "places.hpp"
#include "app/shop3d.hpp"
#include "app/mapedit.hpp"
#include "app/app.hpp"
#include "app/fonts.hpp"
#include "app/settings.hpp"
#include "ui/ui.hpp"
#include "extras/IconsFontAwesome6.h"

#include <algorithm>
#include <cstdlib>
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
    heading("Taxes");
    ImGui::Text("Profit this week so far: %s. Tax on profit: %.1f%% (federal %.0f%%, state %.1f%%), paid at the end of each week.", usd(C.taxable).c_str(), career::taxRate(C),
                places::FEDERAL_TAX, career::taxRate(C) - places::FEDERAL_TAX);
    ImGui::TextDisabled("Paid so far: %s. Sales tax is added to what customers pay and passed on, so it doesn't touch your money.", usd(C.taxesPaid).c_str());
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
            if (tipHover()) ImGui::SetTooltip("As drawn: each part's volume times its material (the part's Material in the designer; steel if not chosen),\nunless you set its Mass. A wooden stock left as steel weighs ten times too much.");
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
            if (k.demo) ImGui::TextDisabled("A big order: after their bench test, you show it on the range.");
        } else if (k.state == 4) {   // passed the bench test: the range demonstration
            wrappedDim(k.verdict);
            ImGui::BeginDisabled(C.stock[k.delivered] < k.qty);
            if (accentButton(ICON_FA_BULLSEYE "  Range demonstration", true)) { careerDemoStart(k.id); ImGui::EndDisabled(); ImGui::EndChild(); ImGui::PopID(); return; }
            ImGui::EndDisabled();
            if (tipHover()) ImGui::SetTooltip("Shoot the course for them on the range with the %s: %d hits on the paper in %d rounds.\nNeeds the %d copies still in stock. Leaving early counts what you've shot.", k.delivered.c_str(), career::DEMO_HITS, career::DEMO_SHOTS, k.qty);
        } else wrappedDim(k.verdict);
        ImGui::EndChild();
        ImGui::PopID();
    };
    bool any = false;
    heading("Your contracts");
    for (auto& k : C.contracts) if (k.state == 1 || k.state == 4) { card(k); any = true; }
    if (!any) ImGui::TextDisabled("None taken on.");
    any = false;
    heading("Offers");
    for (auto& k : C.contracts) if (k.state == 0) { card(k); any = true; }
    if (!any) ImGui::TextDisabled("None today: new customers come as the days pass.");
    if (ImGui::CollapsingHeader("Finished")) for (auto it = C.contracts.rbegin(); it != C.contracts.rend(); ++it) if (it->state == 2 || it->state == 3) card(*it);
}

// ---------------------------------------------------------------- your workshop and your people
static void workshopTab() {
    auto& C = CAREER;
    career::ensureProperties(C);
    heading("Your properties");
    static const char* roles[] = {"store", "workshop", "both"};
    static const char* roleNames[] = {"Store (sells to walk-ins)", "Workshop (makes guns)", "Store and workshop"};
    int sellId = -1;
    for (auto& p : C.props) {
        const career::PropertyKind& k = career::propertyKind(p.kind);
        ImGui::PushID(p.id);
        ImGui::Separator();
        ImGui::PushFont(gFontBold, 0.0f);
        ImGui::Text("%s%s", p.name.empty() ? k.name : p.name.c_str(), p.id == C.here ? "  (you're here)" : "");
        ImGui::PopFont();
        {
            std::string a = career::propertyAddress(p);
            static int choosing = -1;
            if (!a.empty()) ImGui::TextDisabled("%s", a.c_str());
            else {
                ImGui::TextColored({1, 0.75f, 0.35f, 1}, "Not placed on the map yet.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Choose where it is")) choosing = p.id;
            }
            if (choosing == p.id) {
                int city = p.city;
                if (usMapPicker("##placeit", city, std::min(ImGui::GetContentRegionAvail().x, 620 * CFG.uiScale))) { career::setLocation(C, p, city); choosing = -1; careerSave(); }
            }
        }
        int placed = 0; for (auto& [kk, nn] : p.stations) if (kk != "counter") placed += nn;
        ImGui::TextDisabled("%d of %d workstations   staff room %d   upkeep %s a day", std::min(placed, k.stations), k.stations, k.staff, usd(k.upkeep).c_str());
        if (placed > k.stations) ImGui::TextColored({1, 0.6f, 0.4f, 1}, "%d workstations don't fit: only %d count.", placed - k.stations, k.stations);
        if (p.sells()) {
            ImGui::TextDisabled("Store rating:"); ImGui::SameLine();
            stars((int)std::lround(p.rating));
            ImGui::SameLine(); ImGui::TextDisabled("(%.1f)%s", p.rating, p.shown.empty() ? "" : TextFormat("   %d gun%s on display", (int)p.shown.size(), p.shown.size() == 1 ? "" : "s"));
        }
        int ri = p.role == "store" ? 0 : p.role == "workshop" ? 1 : 2;
        ImGui::SetNextItemWidth(230 * CFG.uiScale);
        if (ImGui::Combo("##role", &ri, roleNames, 3)) {
            p.role = roles[ri];
            career::sumStations(C);
            while ((int)C.staff.size() > career::staffRoom(C) && !C.staff.empty()) { toast(C.staff.back().name + " had to go: no room for them now."); C.staff.pop_back(); }
            careerSave();
        }
        if (p.id != C.here) {
            ImGui::SameLine();
            if (ImGui::Button("Go there")) { ImGui::PopID(); careerGoTo(p.id); return; }
        }
        if (C.props.size() > 1) {
            ImGui::SameLine();
            if (dangerButton(TextFormat("Sell (%s)", usd(k.price * 0.6).c_str()))) sellId = p.id;
        }
        ImGui::PopID();
    }
    if (sellId >= 0) {
        bool wasHere = sellId == C.here;
        std::string why;
        if (career::sellProperty(C, sellId, &why)) { careerSave(); toast("Sold."); if (wasHere) { careerOpenWorkshop(true); return; } }
        else toast(why);
    }
    heading("Accessories");
    ImGui::TextWrapped("Plain boxes of magazines, ammunition, holsters, slings and cleaning kits. Your stores sell them to customers and passers-by each day: "
                       "small money, but it adds up. Better-rated stores sell more.");
    ImGui::Text("%d box%s in stock.", C.accessories, C.accessories == 1 ? "" : "es");
    for (int n : {10, 50, 200}) {
        ImGui::SameLine();
        if (ImGui::Button(TextFormat("Buy %d (%s)", n, usd(n * career::ACC_COST).c_str()))) { std::string why; if (career::buyAccessories(C, n, &why)) careerSave(); else toast(why); }
    }
    {
        float pr = (float)C.accPrice;
        ImGui::SetNextItemWidth(220 * CFG.uiScale);
        if (ImGui::SliderFloat("Price per box", &pr, 20, 120, "$%.0f")) { C.accPrice = std::round(pr); careerSave(); }
        if (C.accPrice > 70) ImGui::TextDisabled("At that price fewer people buy.");
    }
    heading("Buy a property");
    static int buyRole = 2;
    static int buyCity = -1;
    if (buyCity < 0) { buyCity = 0; for (auto& p : C.props) if (p.city >= 0) { buyCity = p.city; break; } }
    ImGui::TextDisabled("Where: click a city on the map.");
    usMapPicker("##buycity", buyCity, std::min(ImGui::GetContentRegionAvail().x, 620 * CFG.uiScale));
    for (auto& k : career::propertyKinds()) {
        if (k.price <= 0) continue;
        ImGui::PushID(k.id);
        ImGui::Separator();
        ImGui::PushFont(gFontBold, 0.0f); ImGui::Text("%s", k.name); ImGui::PopFont();
        ImGui::SameLine(); ImGui::TextColored({0.55f, 0.92f, 0.6f, 1}, "%s", usd(k.price).c_str());
        wrappedDim(std::string(k.blurb) + TextFormat("  Room for %d workstations and %d staff; %s a day to keep.", k.stations, k.staff, usd(k.upkeep).c_str()));
        ImGui::SetNextItemWidth(230 * CFG.uiScale);
        ImGui::Combo("##brole", &buyRole, roleNames, 3);
        ImGui::SameLine();
        ImGui::BeginDisabled(C.money < k.price);
        if (accentButton("Buy", true)) {
            std::string why;
            if (career::buyProperty(C, k.id, roles[buyRole], &why)) { career::setLocation(C, C.props.back(), buyCity); careerSave(); toast(std::string("Bought: ") + k.name + " at " + C.props.back().address + ". Go there from this list or the front door."); }
            else toast(why);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    heading("This property");
    auto st = [&](const char* k) { auto it = C.stations.find(k); return it == C.stations.end() ? 0 : it->second; };
    ImGui::Text("Workbenches %d   Lathes %d   Milling machines %d   Test benches %d   Counter: %s", st("bench"), st("lathe"), st("mill"), st("qc"), st("counter") ? "yes" : "none");
    if (ImGui::Button(ICON_FA_TOOLBOX "  Build mode (B)")) { shopBuildMode(); return; }
    if (tipHover()) ImGui::SetTooltip("Build and rearrange your shop with the level editor's tools: walls, lights, and the Workstation tool for\nbenches, lathes, mills, test benches, the counter, the computer and the front door. B (or Walk) takes you back in.");
    heading("Your people");
    ImGui::Text("A working day: %.0f hours (yours %.0f).", career::workHours(C), C.hoursPerDay);
    if (C.staff.empty()) ImGui::TextDisabled("Just you so far.");
    for (size_t i = 0; i < C.staff.size(); i++) {
        auto& e = C.staff[i];
        ImGui::PushID((int)i);
        ImGui::Text("%s", e.name.c_str()); ImGui::SameLine(200 * CFG.uiScale);
        ImGui::TextDisabled("%s", e.specialty.c_str()); ImGui::SameLine(300 * CFG.uiScale);
        ImGui::ProgressBar((float)e.skill, {110 * CFG.uiScale, 0}, TextFormat("skill %.0f", e.skill * 100)); ImGui::SameLine();
        ImGui::Text("%s a day, %d days with you", usd(e.wage).c_str(), e.days); ImGui::SameLine();
        if (ImGui::SmallButton("Let go")) { career::letGo(C, (int)i); careerSave(); ImGui::PopID(); return; }
        ImGui::PopID();
    }
    heading("Looking for work");
    wrappedDim("Machinists work fastest at a lathe or a mill; assemblers at a bench; finishers at a test bench, where they check copies and catch most flaws. "
               "Skill makes them faster and their copies less often flawed (flawed copies jam more, and buyers say so). Everyone needs a workstation of their own; you take a bench.");
    for (size_t i = 0; i < C.candidates.size(); i++) {
        auto& e = C.candidates[i];
        ImGui::PushID(1000 + (int)i);
        ImGui::Text("%s", e.name.c_str()); ImGui::SameLine(200 * CFG.uiScale);
        ImGui::TextDisabled("%s", e.specialty.c_str()); ImGui::SameLine(300 * CFG.uiScale);
        ImGui::ProgressBar((float)e.skill, {110 * CFG.uiScale, 0}, TextFormat("skill %.0f", e.skill * 100)); ImGui::SameLine();
        ImGui::Text("asks %s a day", usd(e.wage).c_str()); ImGui::SameLine();
        if (accentButton("Hire", true)) {
            std::string why;
            if (career::hire(C, (int)i, &why)) { careerSave(); toast("Hired."); ImGui::PopID(); return; }
            toast("Can't: " + why + ".");
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("New people come looking each week.");
}

// ---------------------------------------------------------------- the counter
static void counterTab() {
    auto& C = CAREER;
    auto st = C.stations.find("counter");
    if (st == C.stations.end() || st->second == 0) { wrappedDim("You have no counter: place one in your workshop (build mode, the Workstation tool) and customers will come in."); return; }
    static int sel = -1;
    int waiting = 0;
    auto hereW = [&](const career::Walkin& w) { return C.props.empty() || w.prop == C.here; };
    for (auto& w : C.walkins) if (w.state == 0 && hereW(w)) waiting++;
    if (const career::Property* hp = career::propertyById(C, C.here)) {
        if (!hp->sells()) { wrappedDim("This property is set up as a workshop only: customers go to your stores. Change it under Properties & staff on the computer."); return; }
        ImGui::TextDisabled("Store rating:"); ImGui::SameLine(); stars((int)std::lround(hp->rating)); ImGui::SameLine(); ImGui::TextDisabled("(%.1f)", hp->rating);
        if (shopActive()) {
            std::vector<std::string> tips;
            gmap::storeRating(ME.map, &tips);
            if (!tips.empty()) { std::string t = "To raise it: "; for (size_t i = 0; i < tips.size(); i++) t += (i ? "; " : "") + tips[i]; wrappedDim(t + "."); }
        }
    }
    ImGui::Text("Today, day %d, 2031. %d customer%s waiting.", C.day, waiting, waiting == 1 ? "" : "s");
    if (C.walkins.empty()) { wrappedDim("Nobody's come in yet: they come each day."); return; }
    ImGui::BeginChild("##queue", {230 * CFG.uiScale, 0}, ImGuiChildFlags_Borders);
    for (auto& w : C.walkins) {
        if (!hereW(w)) continue;
        const char* tag = w.state == 0 ? "" : w.state == 1 ? "  (bought)" : w.state == 2 ? "  (left)" : "  (taken away)";
        if (ImGui::Selectable((w.name + tag + "##w" + std::to_string(w.id)).c_str(), sel == w.id)) sel = w.id;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    career::Walkin* W = nullptr;
    for (auto& w : C.walkins) if (w.id == sel && hereW(w)) W = &w;
    if (!W) { for (auto& w : C.walkins) if (w.state == 0 && hereW(w)) { W = &w; sel = w.id; break; } }
    if (!W) { ImGui::TextDisabled("Pick a customer."); return; }
    auto& w = *W;
    ImGui::BeginChild("##customer", {0, 0});
    Texture2D face = careerPortrait(w.id);
    if (face.id) {
        float pw = 200 * CFG.uiScale, ph = 250 * CFG.uiScale;
        ImGui::Image((ImTextureID)(intptr_t)face.id, {pw, ph}, {0, 1}, {1, 0});
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushFont(gFontBold, 16.0f); ImGui::Text("%s", w.name.c_str()); ImGui::PopFont();
    wrapped("\"" + w.request + "\"");
    if (!w.interest.empty()) ImGui::TextColored({0.95f, 0.8f, 0.45f, 1}, "They saw the %s on display and want a look at it.", w.interest.c_str());
    ImGui::TextDisabled("They mention about %s to spend.", usd(w.budget).c_str());
    ImGui::Dummy({0, 6 * CFG.uiScale});
    // their form and their ID, side by side: they should match, and the ID shouldn't have run out
    ImGui::BeginChild("##form", {270 * CFG.uiScale, 120 * CFG.uiScale}, ImGuiChildFlags_Borders);
    ImGui::TextDisabled("FORM TR-1: Firearm transfer record");
    ImGui::Text("Name: %s", w.name.c_str());
    ImGui::Text("Date of birth: %s", w.dob.c_str());
    ImGui::Text("Barred from owning firearms? No");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##idcard", {270 * CFG.uiScale, 120 * CFG.uiScale}, ImGuiChildFlags_Borders);
    ImGui::TextDisabled("DRIVER LICENSE");
    ImGui::Text("Name: %s", w.idName.c_str());
    ImGui::Text("Date of birth: %s", w.idDob.c_str());
    ImGui::Text("Expires: %s", w.idExpires.c_str());
    ImGui::EndChild();
    ImGui::EndGroup();
    if (w.state != 0) { ImGui::TextDisabled(w.state == 1 ? "They bought a gun." : w.state == 2 ? "You sent them away." : "The police took them away."); ImGui::EndChild(); return; }
    heading("Background check");
    if (!w.checked) { if (accentButton("Run the background check", true)) { career::runCheck(C, w.id); careerSave(); } }
    else ImGui::TextColored(w.result == "PROCEED" ? ImVec4(0.5f, 0.9f, 0.55f, 1) : ImVec4(1, 0.4f, 0.35f, 1), "Result: %s", w.result.c_str());
    if (w.checked) ImGui::TextDisabled("The check only knows the name it's given: compare the form with their ID, and look at when it expires.");
    heading("Sell");
    static std::map<int, std::string> pick; static std::map<int, double> price;
    std::string& pg = pick[w.id];
    ImGui::SetNextItemWidth(220 * CFG.uiScale);
    if (ImGui::BeginCombo("##sellgun", pg.empty() ? "Show them..." : pg.c_str())) {
        for (auto& [g, n] : C.stock) if (n > 0 && ImGui::Selectable((g + "  (" + std::to_string(n) + ")").c_str(), pg == g)) {
            pg = g;
            auto it = C.sheets.find(g); double v = it != C.sheets.end() ? it->second.value() : 500;
            for (auto& L : C.listings) if (L.gun == g) v = L.price;
            price[w.id] = std::round(v / 10) * 10;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110 * CFG.uiScale);
    ImGui::InputDouble("##sellprice", &price[w.id], 10, 100, "$%.0f");
    ImGui::SameLine();
    ImGui::BeginDisabled(pg.empty());
    if (accentButton("Sell", true)) {
        std::string why;
        if (career::sellToWalkin(C, w.id, pg, price[w.id], &why)) { careerSave(); toast("Sold."); }
        else toast(why);
    }
    ImGui::EndDisabled();
    ImGui::Dummy({0, 4 * CFG.uiScale});
    if (ImGui::Button("Send them away")) { career::refuseWalkin(C, w.id, false); careerSave(); }
    ImGui::SameLine();
    if (dangerButton("Refuse and call the police")) { career::refuseWalkin(C, w.id, true); careerSave(); toast(C.news.empty() ? "" : C.news.back()); }
    ImGui::EndChild();
}

// Career on foot: the window a thing in your shop opens (F). False once it's closed.
bool careerThingWindow(int thing) {
    auto& C = CAREER;
    careerTestPoll();
    float w = std::min((float)GetScreenWidth() - 60, 1100 * CFG.uiScale), h = std::min((float)GetScreenHeight() - 80, 720 * CFG.uiScale);
    ImGui::SetNextWindowPos({(GetScreenWidth() - w) / 2, (GetScreenHeight() - h) / 2}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({w, h}, ImGuiCond_Always);
    static const char* titles[] = {"Office computer", "Workbench", "Counter", "Front door"};
    bool open = true;
    ImGui::Begin(TextFormat("%s###shopthing", titles[std::clamp(thing, 0, 3)]), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    // the shop's day, money and name, as at the top of the office
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Day %d", C.day);
    ImGui::SameLine(0, 20 * CFG.uiScale);
    ImGui::TextColored(C.money < 0 ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.55f, 0.92f, 0.6f, 1), "%s", usd(C.money).c_str());
    ImGui::SameLine(0, 20 * CFG.uiScale);
    ImGui::TextDisabled("Reputation");
    ImGui::SameLine();
    ImGui::ProgressBar((float)(C.reputation / 100), {120 * CFG.uiScale, 0}, TextFormat("%.0f", C.reputation));
    ImGui::SameLine(); ImGui::TextColored({0.95f, 0.8f, 0.45f, 1}, "%s", career::tierName(C.reputation));
    if (thing == 0 || thing == 1) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PEN_RULER "  Designer")) { ImGui::End(); shopCloseThing(); enterWorkshop(false); return false; }
        if (tipHover()) ImGui::SetTooltip("Design and change guns. Save them, then come back (Office, top left of the designer).");
    }
    ImGui::Separator();
    std::vector<int> tabs;
    if (thing == 0) tabs = {0, 2, 3, 4};
    else if (thing == 1) tabs = {1};
    else if (thing == 2) tabs = {5};
    if (thing == 3) {   // the front door: the day's news, and closing up
        todayTab();
        ImGui::Dummy({0, 8 * CFG.uiScale});
        float prog = 0;
        bool busy = careerTestRunning(&prog);
        ImGui::BeginDisabled(busy);
        if (accentButton(ICON_FA_MOON "  Close up and end the day", true)) { career::endDay(C); careerSave(); shopNewDay(); }
        ImGui::EndDisabled();
        if (busy && tipHover()) ImGui::SetTooltip("A bench test is still running.");
        ImGui::SameLine();
        if (ImGui::Button("Save and go to the main menu")) { careerSave(); ImGui::End(); shopCloseThing(); backToMenu(); return false; }
        if (C.props.size() > 1) {
            heading("Go to another of your places");
            for (auto& p : C.props) {
                if (p.id == C.here) continue;
                const career::PropertyKind& k = career::propertyKind(p.kind);
                if (ImGui::Button(TextFormat("%s  (%s)##go%d", p.name.empty() ? k.name : p.name.c_str(), p.role.c_str(), p.id))) { int id = p.id; ImGui::End(); shopCloseThing(); careerGoTo(id); return false; }
            }
        }
    } else if (ImGui::BeginTabBar("##thingtabs")) {
        const char* names[] = {"Today", "Your guns", "Online store", "Contracts", "Properties & staff", "Counter"};
        static int forced = -2;
        if (forced == -2) { const char* e = std::getenv("GUNSMITH_SHOPTAB"); forced = e ? std::atoi(e) : -1; }   // (screenshots) which tab
        for (int i : tabs) {
            if (ImGui::BeginTabItem(names[i], nullptr, forced == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::BeginChild("##thingtab");
                if (i == 0) todayTab(); else if (i == 1) gunsTab(); else if (i == 2) storeTab(); else if (i == 3) contractsTab(); else if (i == 4) workshopTab(); else counterTab();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        forced = -1;
    }
    ImGui::End();
    return open;
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
    if (tipHover()) ImGui::SetTooltip("Good reviews and contracts done well raise it: more buyers online, more at your counter, and bigger orders\n(police from 35, the military from 60).");
    ImGui::SameLine(); ImGui::TextColored({0.95f, 0.8f, 0.45f, 1}, "%s", career::tierName(C.reputation));
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
        const char* names[] = {"Today", "Your guns", "Online store", "Contracts", "Properties & staff", "Counter"};
        for (int i = 0; i < 6; i++) {
            ImGuiTabItemFlags f = tab == i ? ImGuiTabItemFlags_SetSelected : 0;
            bool open = ImGui::BeginTabItem(names[i], nullptr, f);
            if (ImGui::IsItemClicked()) tab = i;
            if (open) {
                ImGui::BeginChild("##ctab");
                if (i == 0) todayTab(); else if (i == 1) gunsTab(); else if (i == 2) storeTab(); else if (i == 3) contractsTab(); else if (i == 4) workshopTab(); else counterTab();
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
