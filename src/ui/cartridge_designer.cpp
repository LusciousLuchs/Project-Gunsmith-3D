// Gunsmith3D - Cartridge designer window.
#include "app/app.hpp"
#include "app/render.hpp"
#include "ui/ui.hpp"
#include "app/settings.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

void cartridgeDesigner() {
    if (!A.showCartDesigner) return;
    ImGui::SetNextWindowSizeConstraints({460 * CFG.uiScale, 0}, {460 * CFG.uiScale, GetScreenHeight() - TOOLBAR_H - 70 * CFG.uiScale});  // scrolls on short screens
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
    // profile preview, to scale. A drawn ("custom") bullet's corners can be dragged here.
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float W = ImGui::GetContentRegionAvail().x, Hh = 150 * CFG.uiScale;
    ImGui::InvisibleButton("##preview", {W, Hh});
    bool prevHover = ImGui::IsItemHovered(), prevClick = ImGui::IsItemClicked(0);
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, {p0.x + W, p0.y + Hh}, IM_COL32(18, 21, 28, 255), 4);
    double len = std::max(c.oal, c.caseLen) + 4, dia = std::max({c.rimDia, c.baseDia, c.bulletDia * 1.3});
    float sc = (float)std::min((W - 20) / len, (Hh - 34) / dia);
    float x0 = p0.x + 10, yc = p0.y + Hh / 2 - 8;
    auto drawProf = [&](const std::vector<V2>& pr, ImU32 fill) {
        std::vector<ImVec2> top, bot;
        for (auto& q : pr) { top.push_back({x0 + (float)q.x * sc, yc - (float)q.y * sc}); bot.push_back({x0 + (float)q.x * sc, yc + (float)q.y * sc}); }
        for (size_t k = 0; k + 1 < pr.size(); k++) {
            ImVec2 quad[4] = {top[k], top[k + 1], bot[k + 1], bot[k]};
            if (pr[k + 1].x < pr[k].x) std::swap(quad[0], quad[1]), std::swap(quad[2], quad[3]);  // a cavity runs backwards
            dl->AddConvexPolyFilled(quad, 4, fill);
        }
    };
    drawProf(bulletProfile(c), IM_COL32(192, 106, 58, 255));
    if (c.kind == "shotshell") {
        unsigned h = c.hullColor;
        drawProf(hullProfile(c, false), IM_COL32(h >> 24, (h >> 16) & 255, (h >> 8) & 255, 255));
        if (c.shot()) {  // the pellets, packed in the hull's front part, to scale
            double R = c.baseDia / 2 - 0.6, r = c.shotDia / 2, x = c.caseLen - 4 - r;
            int shown = 0;
            for (; x - r > c.rimThick + 10 && shown < c.projectiles; x -= c.shotDia * 0.95)   // (behind them: the powder and wad)
                for (double y = -R + r; y <= R - r + 1e-6 && shown < c.projectiles; y += c.shotDia * 0.95, shown++) {
                    ImVec2 q{x0 + (float)x * sc, yc + (float)y * sc};
                    dl->AddCircleFilled(q, std::max(1.f, (float)r * sc), IM_COL32(112, 116, 126, 235));
                    if (r * sc > 3) dl->AddCircle(q, (float)r * sc, IM_COL32(60, 62, 70, 255));
                }
        }
    }
    drawProf(caseProfile(c), c.kind == "caseless" ? IM_COL32(122, 98, 70, 255) : IM_COL32(217, 164, 65, 255));
    if (c.nose == "hollow") {  // show the cavity
        auto sh = bulletShape(c);
        if (sh.size() >= 3) {
            V2 a = sh[sh.size() - 3], b = sh[sh.size() - 2], e = sh.back();
            ImVec2 q[4] = {{x0 + (float)a.x * sc, yc - (float)a.y * sc}, {x0 + (float)b.x * sc, yc - (float)b.y * sc}, {x0 + (float)b.x * sc, yc + (float)b.y * sc}, {x0 + (float)a.x * sc, yc + (float)a.y * sc}};
            dl->AddConvexPolyFilled(q, 4, IM_COL32(18, 21, 28, 255));
            (void)e;
        }
    }
    dl->AddLine({x0, yc}, {x0 + (float)len * sc, yc}, IM_COL32(255, 255, 255, 40));
    static int dragPt = -1;
    if (c.nose == "custom") {
        double xb = bulletBase(c), R = c.bulletDia / 2;
        auto scr = [&](V2 q) { return ImVec2{x0 + (float)(xb + q.x) * sc, yc - (float)q.y * sc}; };
        ImVec2 m = ImGui::GetIO().MousePos;
        int near = -1; float nd = 8 * CFG.uiScale;
        for (size_t i = 0; i < c.custom.size(); i++) { ImVec2 s2 = scr(c.custom[i]); float d = std::hypot(s2.x - m.x, s2.y - m.y); if (d < nd) { nd = d; near = (int)i; } }
        if (prevClick && near >= 0) dragPt = near;
        if (dragPt >= 0 && dragPt < (int)c.custom.size() && ImGui::IsMouseDown(0)) {
            V2& q = c.custom[dragPt];
            double nx = (m.x - x0) / sc - xb, ny = (yc - m.y) / sc;
            q.y = std::clamp(ny, 0.0, R * 1.5);
            if (dragPt > 0) q.x = std::clamp(nx, 0.0, c.oal - xb + 2);   // the base corner stays at the base
        } else dragPt = -1;
        if (prevHover && near >= 0 && ImGui::IsMouseClicked(1) && near > 0 && c.custom.size() > 3) c.custom.erase(c.custom.begin() + near);
        if (prevHover && near < 0 && ImGui::IsMouseDoubleClicked(0)) {  // add a corner on the nearest edge
            int best = -1; float bd = 12 * CFG.uiScale;
            for (size_t i = 0; i + 1 < c.custom.size(); i++) {
                ImVec2 a = scr(c.custom[i]), b = scr(c.custom[i + 1]);
                float vx = b.x - a.x, vy = b.y - a.y, l2 = vx * vx + vy * vy, t = l2 > 0 ? std::clamp(((m.x - a.x) * vx + (m.y - a.y) * vy) / l2, 0.f, 1.f) : 0;
                float d = std::hypot(a.x + vx * t - m.x, a.y + vy * t - m.y);
                if (d < bd) { bd = d; best = (int)i; }
            }
            if (best >= 0) c.custom.insert(c.custom.begin() + best + 1, {(m.x - x0) / sc - xb, std::max(0.0, (double)(yc - m.y) / sc)});
        }
        for (size_t i = 0; i < c.custom.size(); i++) {
            ImVec2 s2 = scr(c.custom[i]);
            bool hot = (int)i == near || (int)i == dragPt;
            dl->AddRectFilled({s2.x - 3.5f, s2.y - 3.5f}, {s2.x + 3.5f, s2.y + 3.5f}, hot ? IM_COL32(255, 205, 60, 255) : IM_COL32(20, 24, 32, 255));
            dl->AddRect({s2.x - 3.5f, s2.y - 3.5f}, {s2.x + 3.5f, s2.y + 3.5f}, IM_COL32(255, 205, 60, 255));
        }
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "OAL %s   case %s   bullet %s", fmtQ(Q::Length, c.oal, 2).c_str(), fmtQ(Q::Length, c.caseLen, 2).c_str(), fmtQ(Q::Length, c.bulletDia, 2).c_str());
    dl->AddText({x0, p0.y + Hh - 18}, IM_COL32(200, 205, 215, 255), buf);
    if (c.nose == "custom") { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("Drag the corners. Double-click an edge to add one, right-click a corner to remove it."); ImGui::PopTextWrapPos(); }
    char nm[64];
    std::snprintf(nm, sizeof nm, "%s", c.name.c_str());
    prop("Name");
    if (ImGui::InputText("##cname", nm, sizeof nm)) c.name = nm;
    int dec = CFG.units == Units::Metric ? 2 : 3;
    {
        int ki = cartKindIndex(c.kind);
        prop("Kind", "Centrefire: the primer is in the middle of the head.\n"
                     "Rimfire: the priming is in the rim. A drawn firing pin has to strike the rim's edge, not the middle.\n"
                     "Shotshell: a plastic hull on a brass head, crimped shut, holding shot or a slug.\n"
                     "Caseless: the round is a block of propellant that burns away. Nothing is left to extract.");
        if (ImGui::Combo("##kind", &ki, cartKinds())) {
            std::string was = c.kind;
            c.kind = cartKindId(ki);
            if (c.kind == "shotshell" && was != "shotshell") {  // start from a 12 gauge buckshot shell
                for (auto& p : cartridgePresets()) if (p.kind == "shotshell") { std::string n = c.name; c = p; c.name = n; break; }
            }
            if (c.kind == "rimfire") c.rimmed = true;
            if (c.kind == "caseless") { c.rimDia = c.baseDia; c.rimThick = 0; c.rimmed = false; }
            if (was == "shotshell" && c.kind != "shotshell") { c.shotDia = 0; c.oal = std::max(c.oal, c.caseLen + c.bulletDia); c.projectiles = 1; }
        }
    }
    bool shell = c.kind == "shotshell", caseless = c.kind == "caseless";
    ImGui::PushFont(gFontBold, 15.0f); ImGui::TextUnformatted(shell ? "Shell" : caseless ? "Propellant block" : "Case"); ImGui::PopFont();
    pNumU(shell ? "Length (crimped)" : caseless ? "Length" : "Case length", c.caseLen, 0.1, Q::Length, dec);
    if (!caseless) {
        pNumU("Rim", c.rimDia, 0.05, Q::Length, dec);
        pNumU("Rim thickness", c.rimThick, 0.05, Q::Length, dec);
    }
    pNumU(caseless ? "Diameter##block" : "Base", c.baseDia, 0.05, Q::Length, dec);
    if (caseless) c.rimDia = c.baseDia;
    if (!shell) {
        pNumU("Shoulder", c.shoulderDia, 0.05, Q::Length, dec);
        pNumU("Shoulder at", c.shoulderAt, 0.1, Q::Length, dec);
        pNumU("Neck", c.neckDia, 0.05, Q::Length, dec);
        if (!caseless && c.kind != "rimfire") pCheck("Rimmed", c.rimmed);
    } else {
        c.shoulderDia = c.neckDia = c.baseDia; c.shoulderAt = c.caseLen; c.rimmed = true;
        pNumU("Brass head", c.headLen, 0.5, Q::Length, dec, "How far the brass reaches up the hull. Low brass for light loads, high brass for heavy ones.");
        c.headLen = std::clamp(c.headLen, c.rimThick + 0.5, c.caseLen - 2);
        float col[3] = {(c.hullColor >> 24) / 255.f, ((c.hullColor >> 16) & 255) / 255.f, ((c.hullColor >> 8) & 255) / 255.f};
        prop("Hull colour");
        if (ImGui::ColorEdit3("##hull", col, ImGuiColorEditFlags_NoInputs))
            c.hullColor = ((unsigned)(col[0] * 255) << 24) | ((unsigned)(col[1] * 255) << 16) | ((unsigned)(col[2] * 255) << 8) | 255u;
        ImGui::PushFont(gFontBold, 15.0f); ImGui::TextUnformatted("Load"); ImGui::PopFont();
        bool slug = c.shotDia <= 0;
        int li = slug ? 1 : 0;
        prop("Load", "Shot: a number of lead pellets. Their weight comes from their size.\nSlug: one heavy projectile.");
        if (ImGui::Combo("##load", &li, "Shot\0Slug\0")) {
            if (li == 1) { c.shotDia = 0; c.projectiles = 1; if (c.bulletMass < 5) c.bulletMass = 28.35; }
            else { c.shotDia = 8.38; c.projectiles = 9; }
        }
        pNumU("Bore", c.bulletDia, 0.05, Q::Length, dec, "The barrel's bore: 18.5 mm for 12 gauge.");
        if (li == 0) {
            pNumU("Pellet size", c.shotDia, 0.05, Q::Length, dec, "00 buck is 8.4 mm, #4 buck 6.1 mm, #7 1/2 birdshot 2.4 mm.");
            c.shotDia = std::clamp(c.shotDia, 0.5, c.bulletDia);
            double n = c.projectiles;
            if (pNum("Pellets", n, 1, "%.0f")) c.projectiles = std::clamp((int)std::lround(n), 1, 2000);
            ImGui::TextDisabled("%s each, %s in all", fmtQ(Q::BulletMass, c.pelletMass()).c_str(), fmtQ(Q::BulletMass, c.payloadMass()).c_str());
        } else pNumU("Slug weight", c.bulletMass, 0.5, Q::BulletMass);
    }
    if (!shell) {
    ImGui::PushFont(gFontBold, 15.0f); ImGui::TextUnformatted("Bullet"); ImGui::PopFont();
    {
        int ni = noseIndex(c.nose);
        prop("Shape", "The bullet's shape is its real outline: it's what meets the feed ramp and the chamber mouth.\n"
                      "Round noses feed most easily. Wide flat fronts can catch on a feed ramp in a self-loader.\n"
                      "Pointed (spitzer) and boat-tailed bullets keep their speed best downrange.");
        if (ImGui::Combo("##nose", &ni, noseStyles())) {
            std::string was = c.nose;
            if (ni == 8) {  // drawn: start from whatever it looks like now
                std::vector<V2> sh = bulletShape(c);
                if (sh.empty()) { Cartridge t = c; t.nose = "round"; sh = bulletShape(t); }
                double xb = bulletBase(c);
                c.custom.clear();
                for (auto& q : sh) c.custom.push_back({q.x - xb, q.y});
            }
            c.nose = noseId(ni);
            if (c.nose == "ball") { c.oal = std::max(c.oal, bulletBase(c) + c.bulletDia); }
        }
    }
    pNumU("Diameter", c.bulletDia, 0.05, Q::Length, dec);
    pNumU("Overall length", c.oal, 0.1, Q::Length, dec);
    if (c.nose != "ball" && c.nose != "wadcutter" && c.nose != "custom") pNumU("Nose length", c.noseLen, 0.1, Q::Length, dec);
    if (c.nose == "spitzer" || c.nose == "round" || c.nose == "flat" || c.nose == "hollow" || c.nose == "semiwad") {
        double mp = c.meplat * 100;
        if (pNum("Flat tip", mp, 1, "%.0f%% of the diameter", "A flat on the tip (a meplat). Wider flats feed less smoothly and slow down sooner.")) c.meplat = std::clamp(mp / 100, 0.0, 0.95);
    }
    if (c.nose != "auto" && c.nose != "ball" && c.nose != "custom" && c.nose != "wadcutter") {
        pNumU("Boat tail", c.tailLen, 0.1, Q::Length, dec, "A tapered base: keeps its speed better downrange. 0 for a flat base.");
        c.tailLen = std::max(0.0, c.tailLen);
        if (c.tailLen > 0.05) {
            double bd = c.tailDia * 100;
            if (pNum("Tail base", bd, 1, "%.0f%% of the diameter")) c.tailDia = std::clamp(bd / 100, 0.3, 1.0);
        }
    }
    if (c.nose != "auto") {
        double sd = c.seat > 0 ? c.seat : c.caseLen - bulletBase(c);
        if (pNumU("Seated depth", sd, 0.1, Q::Length, dec, "How far the bullet sits inside the case.")) c.seat = std::clamp(sd, 0.1, c.caseLen - 0.5);
    }
    if (c.nose != "auto") {
        pCheck("Weight from shape", c.massFromShape, "Works the weight out from the bullet's shape, as jacketed lead.");
        if (c.massFromShape) c.bulletMass = std::round(bulletShapeMass(c) * 100) / 100;
    }
    ImGui::BeginDisabled(c.massFromShape && c.nose != "auto");
    pNumU("Weight", c.bulletMass, 0.1, Q::BulletMass);
    ImGui::EndDisabled();
    {
        double n = c.projectiles;
        if (pNum("Bullets", n, 1, "%.0f", "Bullets fired at once, one behind the other (a duplex load) or side by side. Recoil and energy count them all.")) c.projectiles = std::clamp((int)std::lround(n), 1, 50);
    }
    }  // (not a shell)
    pNumU("Velocity", c.velocity, 5, Q::Velocity);
    c.tailDia = std::clamp(c.tailDia, 0.3, 1.0);
    c.caseLen = std::max(3.0, c.caseLen); c.oal = shell ? c.caseLen : std::max(c.caseLen + 1, c.oal);
    c.shoulderAt = std::clamp(c.shoulderAt, 1.0, c.caseLen); c.noseLen = std::clamp(c.noseLen, 0.5, c.oal - c.caseLen + 2);
    ImGui::TextDisabled("%s, recoil impulse %s", fmtQ(Q::Energy, c.energy()).c_str(), fmtQ(Q::Impulse, c.impulse()).c_str());
    if (!shell) ImGui::TextDisabled("Ballistic coefficient %.3f (G1)", ballisticCoefficient(c));
    if (!shell && tipHover()) ImGui::SetTooltip("How well it keeps its speed through the air: higher is better.\nEstimated from the shape and weight. The firing range will use it for drop and drift.");
    if (!shell && c.neckDia < c.bulletDia) ImGui::TextColored(colBad(), "The neck is narrower than the bullet.");
    if (!caseless && c.rimDia < c.baseDia && c.rimmed) ImGui::TextColored(colBad(), "A rimmed case needs a rim wider than its base.");
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
