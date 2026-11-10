// Gunsmith3D - 2D overlay drawn over the 3D view: handles, joints, labels.
#include "app/app.hpp"
#include "app/scaletool.hpp"
#include "app/faces.hpp"
#include "app/gfx.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "app/render.hpp"
#include "app/overlay.hpp"
#include "app/fonts.hpp"
#include "app/units.hpp"
#include "app/settings.hpp"
#include "app/carvetool.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <string>

void drawOverlay() {
    drawFaces();
    auto& D = A.design;
    A.handles.clear();
    Vector2 m = GetMousePosition();
    if (A.mode == Mode::Test) {  // magazine round counters
        if (!A.S) return;
        for (auto& r : renderList()) {
            if (!r.sb || !r.sb->isMag) continue;
            const BodyDef& d = D.bodies[r.tmpl];
            V2 c = centroidOf(d);
            double y1 = -1e9;
            for (auto& p : outlineOf(d)) y1 = std::max(y1, p.y);
            Vector2 s = toScreen(Vector3Transform({0, -(float)(y1 - c.y) * U - 0.6f, bodyDepth(d) / 2}, r.M));
            const char* t = TextFormat("%d/%d", r.sb->rounds, d.magCap);
            float w = viewTextWidth(t, 13), h = 20 * CFG.uiScale;
            Color bg = r.sb->rounds > 0 ? Color{20, 23, 28, 200} : Color{120, 30, 20, 220};
            DrawRectangleRounded({s.x - w / 2 - 7, s.y + 2, w + 14, h}, 0.4f, 6, bg);
            viewText(t, s.x - w / 2, s.y + 2 + (h - 13 * CFG.uiScale) / 2, 13, RAYWHITE);
        }
        if (A.hover && !(A.S->hand.on)) {
            std::string nm = A.hover->d && !A.hover->isRound ? A.hover->d->name : "";
            std::string tt = A.hoverHandle ? "Drag the handle: lift to unlock, pull back to open"
                          : A.hover->isRound ? (A.hover->staged ? "Push it home, or close the action on it" : "Drag into the open action, in front of the breech face")
                          : nm.empty() ? "Drag to move" : nm + ": drag to move";
            const char* t = tt.c_str();
            float w = viewTextWidth(t, 12);
            DrawRectangleRounded({m.x + 12, m.y + 12, w + 12, 20 * CFG.uiScale}, 0.4f, 6, Color{20, 23, 28, 190});
            viewText(t, m.x + 18, m.y + 12 + 4 * CFG.uiScale, 12, Color{236, 238, 242, 255});
        }
        return;
    }
    auto& br = D.breech;
    auto label = [](Vector2 s, const char* t, Color c) { viewText(t, s.x + 9, s.y - 17 * CFG.uiScale, 11, c); };
    for (size_t ji = 0; ji < D.joints.size(); ji++) {
        auto& j = D.joints[ji];
        float z = std::max(frontZ(j.a), frontZ(j.b)) + 0.25f;
        if (j.type == "pin" || j.type == "weld" || j.type == "track") A.handles.push_back({toScreen(W3(j.p, z)), 1, (int)ji, -1});
        if (j.type == "track") for (size_t k = 0; k < j.path.size(); k++) A.handles.push_back({toScreen(W3(j.path[k], frontZ(j.b))), 2, (int)ji, (int)k});
        if (j.type == "magwell" && j.path.size() >= 2) {   // the seat (drag: the magazine moves, well and all), bends, and the mouth
            if (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)D.bodies.size() && D.bodies[A.sel.idx].id == j.a) {
                Vector2 s = toScreen(W3(j.path[0], frontZ(j.a) + 0.15f));
                A.handles.push_back({s, 2, (int)ji, 0});
                viewText("Seated here", s.x - 9 - MeasureText("Seated here", (int)(11 * CFG.uiScale)), s.y + 4, 11, Color{110, 220, 150, 255});   // (left: the feed spot's name is on the right)
            }
            for (size_t k = 1; k < j.path.size(); k++) {
                Vector2 s = toScreen(W3(j.path[k], frontZ(j.a) + 0.15f));
                A.handles.push_back({s, 2, (int)ji, (int)k});
                if (k + 1 == j.path.size()) label(s, "Magazine well exit", Color{110, 220, 150, 255});
            }
        }
        if (j.type == "spring") { A.handles.push_back({toScreen(W3(j.pa, 0)), 3, (int)ji, -1}); A.handles.push_back({toScreen(W3(j.pb, 0)), 4, (int)ji, -1}); }
    }
    const bool fromBarrels = barrelCount(D) > 0;  // (then the barrels place these: nothing to drag)
    if (br.on && br.hasFace) { Vector2 s = toScreen(W3(br.face, frontZ(br.breech))); if (!fromBarrels) A.handles.push_back({s, 5, -1, -1}); label(s, "Breech face", Color{255, 122, 89, 255}); }
    if (br.on && br.hasMuzzle) { Vector2 s = toScreen(W3(br.muzzle, frontZ(br.chamber))); if (!fromBarrels) A.handles.push_back({s, 6, -1, -1}); label(s, "Muzzle", Color{255, 122, 89, 255}); }
    if (br.on) for (size_t k = 0; k < br.more.size(); k++) {  // further chambers: numbered from 2
        auto& c = br.more[k];
        std::string n = std::to_string(k + 2);
        { Vector2 s = toScreen(W3(c.face, frontZ(c.breech.empty() ? br.breech : c.breech))); if (!fromBarrels) A.handles.push_back({s, 5, (int)k, -1}); label(s, ("Breech face " + n).c_str(), Color{255, 122, 89, 255}); }
        if (c.hasMuzzle) { Vector2 s = toScreen(W3(c.muzzle, frontZ(c.chamber))); if (!fromBarrels) A.handles.push_back({s, 6, (int)k, -1}); label(s, ("Muzzle " + n).c_str(), Color{255, 122, 89, 255}); }
    }
    if (br.on) for (size_t gi = 0; gi < br.gas.size(); gi++) {   // gas ports: on the bore, a line to the piston they push
        const GasPortDef& g = br.gas[gi];
        int k = std::clamp(g.chamber, 0, (int)br.more.size());
        V2 f = k == 0 ? br.face : br.more[k - 1].face, mz = k == 0 ? br.muzzle : br.more[k - 1].muzzle;
        bool hm = k == 0 ? br.hasMuzzle : br.more[k - 1].hasMuzzle;
        if (!hm) continue;
        double L = std::hypot(mz.x - f.x, mz.y - f.y);
        if (L < 1) continue;
        V2 p{f.x + (mz.x - f.x) / L * g.at, f.y + (mz.y - f.y) / L * g.at};
        const std::string& chId = k == 0 ? br.chamber : br.more[k - 1].chamber;
        Vector2 s = toScreen(W3(p, frontZ(chId) + 0.1f));
        Color gc{120, 200, 255, 255};
        DrawCircleV(s, 4.5f, gc);
        int pi = D.bodyIndex(g.piston);
        if (pi >= 0) {
            Vector2 t = toScreen(W3(centroidOf(D.bodies[pi]), frontZ(g.piston)));
            DrawLineEx(s, t, 1.5f, Color{gc.r, gc.g, gc.b, 140});
            DrawCircleLinesV(t, 6, gc);
        }
        label(s, br.gas.size() > 1 ? ("Gas port " + std::to_string(gi + 1)).c_str() : "Gas port", gc);
    }
    for (auto& h : A.handles)   // the handle picked: a second press drags it
        if (h.kind != 0 && h.kind != 7 && h.kind == A.armedKind && h.i == A.armedI && h.v == A.armedV) DrawCircleLinesV(h.s, 9, Color{255, 205, 60, 255});
    if (A.axisLock && (A.dragKind == 1 || A.dragKind == 2)) {  // which way a drag is held to
        static const char* what[] = {"", "X: side to side only", "Y: up and down only", "Z: depth only (move the mouse up and down)"};
        Color lc = A.axisLock == 1 ? Color{235, 90, 80, 255} : A.axisLock == 2 ? Color{110, 220, 120, 255} : Color{90, 150, 240, 255};
        Vector2 s0 = toScreen(W3(A.dragKind == 2 ? A.dragStart : A.dragMM, 0));
        if (A.axisLock == 1) DrawLineEx({0, s0.y}, {(float)A.vw, s0.y}, 1.5f, Color{lc.r, lc.g, lc.b, 150});
        if (A.axisLock == 2) DrawLineEx({s0.x, (float)TOOLBAR_H}, {s0.x, (float)(TOOLBAR_H + A.vh)}, 1.5f, Color{lc.r, lc.g, lc.b, 150});
        viewText(what[A.axisLock], m.x + 16, m.y + 12, 13, lc);
    }
    if (A.mode == Mode::Build && CFG.workbench && !A.showcase) {  // the ruler's numbers, as many as fit
        BenchLayout L = benchLayout();
        bool imp = CFG.units == Units::Imperial;
        double unit = imp ? 25.4 : 10, len = L.rulerLen;
        Vector2 a = toScreen(W3({L.rulerX0, L.top - 11}, (float)(L.rulerZ + 1) * U)), b = toScreen(W3({L.rulerX0 + unit, L.top - 11}, (float)(L.rulerZ + 1) * U));
        float px = Vector2Distance(a, b);
        int every = px > 26 ? 1 : px > 12 ? (imp ? 2 : 5) : (imp ? 4 : 10);
        if (px > 3)
            for (int k = 0; k * unit <= len + 1e-6; k += every) {
                Vector2 s = toScreen(W3({L.rulerX0 + k * unit, L.top - 11}, (float)(L.rulerZ + 1) * U));
                std::string t = std::to_string(k) + (k == 0 ? (imp ? " in" : " cm") : "");
                viewText(t.c_str(), s.x + 2, s.y - 4, 10, Color{30, 32, 36, 255});
            }
    }
    drawScaleOverlay();
    if (A.mode == Mode::Build && A.measured && (A.tool == Tool::Measure || A.shaping || A.measureKeep)) {  // the measuring line
        Vector2 a = toScreen(W3(A.measureA, 0)), b = toScreen(W3(A.measureB, 0));
        Color mc{255, 226, 90, 255};
        DrawLineEx(a, b, 2, mc); DrawCircleV(a, 4, mc); DrawCircleV(b, 4, mc);
        double dx = A.measureB.x - A.measureA.x, dy = A.measureA.y - A.measureB.y, L = std::hypot(dx, dy);
        double ang = std::atan2(dy, dx) * 180 / 3.14159265358979;
        auto len = [](double v) { return CFG.units == Units::Imperial ? fmtQ(Q::Length, v, 3) + " (" + fracInch(v) + ")" : fmtQ(Q::Length, v, 2); };
        std::string t = len(L) + "   across " + len(dx) + "   up " + len(dy) + "   " + TextFormat("%.1f\xc2\xb0", ang);
        viewText(t.c_str(), (a.x + b.x) / 2 + 10, (a.y + b.y) / 2 - 22, 13, mc);
    }
    // joints that belong to what you're working on are drawn strongly; the rest stay faint until hovered
    bool jointTool = A.tool == Tool::Pin || A.tool == Tool::Weld || A.tool == Tool::Track || A.tool == Tool::Spring;
    std::string selId = A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size() ? D.bodies[A.sel.idx].id : "";
    for (auto& h : A.handles) {
        bool hot = Vector2Distance(h.s, m) < 9;
        bool strong = hot || jointTool || h.kind >= 5;
        if (!strong && h.i >= 0 && h.i < (int)D.joints.size()) {
            auto& j = D.joints[h.i];
            strong = (A.sel.kind == 2 && A.sel.idx == h.i) || (!selId.empty() && (j.a == selId || j.b == selId));
        }
        Color c = h.kind >= 9 ? Color{110, 230, 140, 255} : h.kind >= 5 ? Color{255, 122, 89, 255} : Color{255, 205, 60, 255};
        if (!strong) { c.a = 90; DrawCircleV(h.s, 2.5f, Color{20, 24, 32, 110}); DrawCircleLinesV(h.s, 2.5f, c); continue; }
        DrawCircleV(h.s, hot ? 6.f : 4.f, Color{20, 24, 32, 220});
        DrawCircleLinesV(h.s, hot ? 6.f : 4.f, c);
    }
    if (A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size() && A.tool != Tool::Faces) {   // (the Faces tool shows corners on both faces itself)
        auto& d = D.bodies[A.sel.idx];
        float z = bodyFront(A.sel.idx);
        for (size_t v = 0; v < d.pts.size(); v++) {
            Vector2 s = toScreen(W3(d.pts[v], z));
            bool hot = Vector2Distance(s, m) < 9;
            float r = hot ? 6.f : 4.5f;
            bool picked = std::find(A.selVs.begin(), A.selVs.end(), (int)v) != A.selVs.end();
            DrawRectangleRec({s.x - r, s.y - r, 2 * r, 2 * r}, picked ? Color{255, 205, 60, 255} : Color{20, 24, 32, 230});
            DrawRectangleLinesEx({s.x - r, s.y - r, 2 * r, 2 * r}, 1.5f, Color{255, 205, 60, 255});
            A.handles.insert(A.handles.begin(), {s, 0, A.sel.idx, (int)v});
        }
        if (A.tool == Tool::Select) for (size_t v = 0; v < d.pts.size(); v++) {  // edge handles: drag to move an edge, Shift-drag to extrude it
            V2 a = d.pts[v], b = d.pts[(v + 1) % d.pts.size()];
            Vector2 s = toScreen(W3({(a.x + b.x) / 2, (a.y + b.y) / 2}, z));
            bool hot = Vector2Distance(s, m) < 8;
            float r = hot ? 5.5f : 3.5f;
            Vector2 q[4] = {{s.x, s.y - r}, {s.x + r, s.y}, {s.x, s.y + r}, {s.x - r, s.y}};
            DrawTriangle(q[0], q[3], q[2], Color{120, 230, 200, (unsigned char)(hot ? 255 : 170)});
            DrawTriangle(q[0], q[2], q[1], Color{120, 230, 200, (unsigned char)(hot ? 255 : 170)});
            A.handles.push_back({s, 7, A.sel.idx, (int)v});
        }
    }
    if (A.boxing) {
        Rectangle r{std::min(A.boxStart.x, m.x), std::min(A.boxStart.y, m.y), std::fabs(m.x - A.boxStart.x), std::fabs(m.y - A.boxStart.y)};
        DrawRectangleRec(r, Color{255, 205, 60, 30});
        DrawRectangleLinesEx(r, 1, Color{255, 205, 60, 200});
    }
    if (A.shaping && (A.tool == Tool::Knife || A.tool == Tool::Mirror)) {
        V2 w = snapMM(mouseMM(0));
        Vector2 p0 = toScreen(W3(A.shapeStart, 0)), p1 = toScreen(W3(w, 0));
        Color c = A.tool == Tool::Knife ? Color{255, 90, 70, 255} : Color{120, 230, 200, 255};
        Vector2 d = Vector2Subtract(p1, p0);
        float L = Vector2Length(d);
        if (A.tool == Tool::Mirror && L > 1) { p0 = Vector2Subtract(p0, Vector2Scale(d, 2000 / L)); p1 = Vector2Add(p1, Vector2Scale(d, 2000 / L)); }
        DrawLineEx(p0, p1, 2, c);
        viewText(A.tool == Tool::Knife ? "Cut" : "Mirror axis", m.x + 14, m.y + 12, 12, c);
    } else if (A.shaping) {
        V2 a = A.shapeStart, w = snapMM(mouseMM(0));
        if (A.tool == Tool::Barrel) {
            const Cartridge& cart = A.design.breech.on ? A.design.breech.cart : cartridgePresets()[0];
            if (std::hypot(w.x - a.x, w.y - a.y) >= 10) {
                BodyDef pv = makeBarrel(a, w, cart);
                for (size_t k = 0; k < pv.pts.size(); k++) DrawLineEx(toScreen(W3(pv.pts[k], 0)), toScreen(W3(pv.pts[(k + 1) % pv.pts.size()], 0)), 2, Color{255, 205, 60, 255});
            }
            viewText((fmtQ(Q::Length, std::hypot(w.x - a.x, w.y - a.y)) + "  breech  \xe2\x86\x92  muzzle").c_str(), m.x + 14, m.y + 12, 12, RAYWHITE);
        } else if (A.tool == Tool::Rect) {
            Vector2 p[4] = {toScreen(W3({a.x, a.y}, 0)), toScreen(W3({w.x, a.y}, 0)), toScreen(W3({w.x, w.y}, 0)), toScreen(W3({a.x, w.y}, 0))};
            for (int k = 0; k < 4; k++) DrawLineEx(p[k], p[(k + 1) % 4], 2, Color{255, 205, 60, 255});
            viewText((fmtQ(Q::Length, std::fabs(w.x - a.x)) + "  x  " + fmtQ(Q::Length, std::fabs(w.y - a.y))).c_str(), m.x + 14, m.y + 12, 12, RAYWHITE);
        } else {
            double r = std::hypot(w.x - a.x, w.y - a.y);
            Vector2 prev{};
            for (int k = 0; k <= 48; k++) {
                double t = 2 * PI * k / 48;
                Vector2 q = toScreen(W3({a.x + r * std::cos(t), a.y + r * std::sin(t)}, 0));
                if (k) DrawLineEx(prev, q, 2, Color{255, 205, 60, 255});
                prev = q;
            }
            viewText(("R " + fmtQ(Q::Length, r)).c_str(), m.x + 14, m.y + 12, 12, RAYWHITE);
        }
    }
    // the selected magazine's round path: where its rounds sit, fed from the first point
    if (A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size() && D.bodies[A.sel.idx].magazine && A.tool == Tool::Select) {
        auto& d = D.bodies[A.sel.idx];
        float z = bodyFront(A.sel.idx) + 0.1f;
        Color pc{120, 200, 255, 255};
        // drawn, or (until you move one) where they're laid out automatically: either way their dots can be dragged
        bool autoPath = d.roundPath.size() < 2;
        std::vector<V2> rp = autoPath ? autoRoundPath(d, D.breech.cart) : d.roundPath;
        Color lc = autoPath ? Color{pc.r, pc.g, pc.b, 110} : pc;
        for (size_t k = 0; k + 1 < rp.size(); k++) DrawLineEx(toScreen(W3(rp[k], z)), toScreen(W3(rp[k + 1], z)), 2, lc);
        for (size_t k = 0; k < rp.size(); k++) {
            if (autoPath && k != 0 && k + 1 != rp.size()) continue;   // (automatic: just the two ends)
            Vector2 s = toScreen(W3(rp[k], z));
            bool hot = Vector2Distance(s, m) < 8;
            DrawCircleV(s, hot ? 6.f : (k == 0 ? 5.f : 4.f), Color{20, 24, 32, 230});
            DrawCircleLinesV(s, hot ? 6.f : (k == 0 ? 5.f : 4.f), pc);
            A.handles.push_back({s, 8, A.sel.idx, (int)k});
            if (k == 0) viewText("Feeds from here (top round)", s.x + 9, s.y - 17 * CFG.uiScale, 11, pc);
            else if (k + 1 == rp.size()) viewText("Rounds lie this way", s.x + 9, s.y - 17 * CFG.uiScale, 11, pc);
        }
    }
    if (A.drafting || A.draftHasF) {
        std::vector<Vector2> pts;
        for (auto& p : A.draft) pts.push_back(toScreen(W3(p, 0)));
        for (size_t k = 0; k + 1 < pts.size(); k++) DrawLineEx(pts[k], pts[k + 1], 2, Color{255, 205, 60, 255});
        if (!pts.empty() && mouseInViewport()) DrawLineEx(pts.back(), m, 1.5f, Color{255, 205, 60, 160});
        for (size_t k = 0; k < pts.size(); k++) DrawCircleV(pts[k], k == 0 ? 5.f : 3.5f, k == 0 ? Color{255, 205, 60, 255} : RAYWHITE);
        if (A.draftHasF) {
            Vector2 f = toScreen(W3(A.draftF, 0));
            DrawCircleV(f, 6, Color{240, 170, 60, 255});
            if (A.tool == Tool::Spring && mouseInViewport()) DrawLineEx(f, m, 2, Color{120, 180, 235, 255});
            if (A.tool == Tool::Track) {  // name both ends of the job: what slides, and what holds the slot
                std::string rider = A.draftA;
                for (auto& b : A.design.bodies) if (b.id == A.draftA && !b.name.empty()) rider = b.name;
                viewText((rider + " slides").c_str(), f.x + 9, f.y - 17 * CFG.uiScale, 11, Color{240, 170, 60, 255});
                Vector2 at = pts.empty() ? m : pts[0];
                if (!pts.empty() || mouseInViewport()) {
                    int h = trackHolderAt(pts.empty() ? snapMM(mouseMM(0)) : A.draft[0]);
                    std::string holder = h < 0 ? "slot fixed to the ground" : "slot in " + (A.design.bodies[h].name.empty() ? A.design.bodies[h].id : A.design.bodies[h].name);
                    viewText(holder.c_str(), at.x + 9, at.y + 6 * CFG.uiScale, 11, Color{255, 205, 60, 230});
                }
            }
        }
    }
    drawCarveOverlay();
    // the name of the part under the cursor, so parts can be told apart without selecting them
    if (!A.drafting && !A.draftHasF && !A.carveDrag && A.dragKind == 0 && mouseInViewport() && !ImGui::GetIO().WantCaptureMouse) {
        int i; Vector3 p, n;
        if (carvePick(i, p, n) && i < (int)D.bodies.size() && !D.bodies[i].name.empty()) {
            const char* t = D.bodies[i].name.c_str();
            float w = viewTextWidth(t, 12);
            DrawRectangleRounded({m.x + 14, m.y + 14, w + 12, 20 * CFG.uiScale}, 0.4f, 6, Color{20, 23, 28, 190});
            viewText(t, m.x + 20, m.y + 14 + 4 * CFG.uiScale, 12, Color{236, 238, 242, 255});
        }
    }
}
