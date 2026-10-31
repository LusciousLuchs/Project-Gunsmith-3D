// Gunsmith3D - 2D overlay drawn over the 3D view: handles, joints, labels.
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "app/view.hpp"
#include "app/render.hpp"
#include "app/overlay.hpp"
#include "app/fonts.hpp"
#include "app/units.hpp"
#include "app/settings.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

void drawOverlay() {
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
            const char* t = A.hoverHandle ? "Drag the handle: lift to unlock, pull back to open"
                          : A.hover->isRound ? (A.hover->staged ? "Push it home, or close the action on it" : "Drag into the open action, in front of the breech face")
                          : A.hover->isMag ? "Drag to move the magazine" : "Drag to move";
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
        if (j.type == "magwell" && j.path.size() >= 2) {
            Vector2 s = toScreen(W3(j.path.back(), frontZ(j.a) + 0.15f));
            A.handles.push_back({s, 2, (int)ji, (int)j.path.size() - 1});
            label(s, "Magazine well exit", Color{110, 220, 150, 255});
        }
        if (j.type == "spring") { A.handles.push_back({toScreen(W3(j.pa, 0)), 3, (int)ji, -1}); A.handles.push_back({toScreen(W3(j.pb, 0)), 4, (int)ji, -1}); }
    }
    if (br.on && br.hasFace) { Vector2 s = toScreen(W3(br.face, frontZ(br.breech))); A.handles.push_back({s, 5, -1, -1}); label(s, "Breech face", Color{255, 122, 89, 255}); }
    if (br.on && br.hasMuzzle) { Vector2 s = toScreen(W3(br.muzzle, frontZ(br.chamber))); A.handles.push_back({s, 6, -1, -1}); label(s, "Muzzle", Color{255, 122, 89, 255}); }
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
        Color c = h.kind >= 5 ? Color{255, 122, 89, 255} : Color{255, 205, 60, 255};
        if (!strong) { c.a = 90; DrawCircleV(h.s, 2.5f, Color{20, 24, 32, 110}); DrawCircleLinesV(h.s, 2.5f, c); continue; }
        DrawCircleV(h.s, hot ? 6.f : 4.f, Color{20, 24, 32, 220});
        DrawCircleLinesV(h.s, hot ? 6.f : 4.f, c);
    }
    if (A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size()) {
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
        if (A.tool == Tool::Rect) {
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
        }
    }
}
