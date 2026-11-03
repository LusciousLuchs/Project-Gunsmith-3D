// Gunsmith3D - Build mode: editing operations and the drawing tools.
#include "app/app.hpp"
#include "app/settings.hpp"
#include "carve.hpp"
#include "app/carvetool.hpp"
#include "app/gfx.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "app/faces.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ---------------------------------------------------------------- editing operations
bool splitReady() {
    static double last = -1;
    double now = GetTime();
    if (now - last < 0.2) return false;
    last = now;
    return true;
}
void cornerLimitToast() { toast(TextFormat("A part can have at most %d corners. Remove one (right-click a corner) to add another.", MAX_CORNERS)); }

void moveBody(int i, double dx, double dy) {
    BodyDef& d = A.design.bodies[i];
    for (auto& p : d.pts) { p.x += dx; p.y += dy; }
    for (auto& p : d.roundPath) { p.x += dx; p.y += dy; }
    carve::moveCarves(d, dx, dy);
    auto mv = [&](V2& p) { p.x += dx; p.y += dy; };
    for (auto& j : A.design.joints) {
        if ((j.type == "pin" || j.type == "weld") && j.a == d.id) mv(j.p);
        if (j.type == "track") { if (j.a == d.id) mv(j.p); if (j.b == d.id) for (auto& q : j.path) mv(q); }
        if (j.type == "magwell" && j.a == d.id) { mv(j.p); for (auto& q : j.path) mv(q); }
        if (j.type == "spring") { if (j.a == d.id) mv(j.pa); if (j.b == d.id) mv(j.pb); }
    }
    auto& br = A.design.breech;
    if (br.on) { if (br.breech == d.id && br.hasFace) mv(br.face); if (br.chamber == d.id && br.hasMuzzle) mv(br.muzzle); }
    for (HandHold* h : {&A.design.pose.firing, &A.design.pose.support, &A.design.pose.butt}) if (h->on && h->part == d.id) mv(h->at);
    if (br.on) for (auto& c : br.more) { if ((c.breech.empty() ? br.breech : c.breech) == d.id) mv(c.face); if (c.chamber == d.id && c.hasMuzzle) mv(c.muzzle); }
}
// Turn a part in the side view (about Z) by deg about c: its outline and cuts, and what's attached at points on it.
void rotateBody(int i, V2 c, double deg) {
    BodyDef& d = A.design.bodies[i];
    double a = deg * 3.14159265358979 / 180, cs = std::cos(a), sn = std::sin(a);
    auto rot = [&](V2& p) { double x = p.x - c.x, y = p.y - c.y; p = {c.x + x * cs - y * sn, c.y + x * sn + y * cs}; };
    for (auto& p : d.pts) rot(p);
    for (auto& o : d.depthOff) { double x = o.x, y = o.y; o = {x * cs - y * sn, x * sn + y * cs}; }   // (the far face turns with it)
    if (!d.depthRef.empty()) d.depthRef = d.pts;
    for (auto& p : d.roundPath) rot(p);
    for (auto& cv : d.carves) {
        rot(cv.at);
        double dx = cv.dx * cs - cv.dy * sn, dy = cv.dx * sn + cv.dy * cs;
        cv.dx = dx; cv.dy = dy;
        if (std::fabs(cv.dz) > 0.7) cv.spin += deg * (cv.dz > 0 ? 1 : -1);  // a cut from the side turns with it
    }
    for (auto& j : A.design.joints) {
        if ((j.type == "pin" || j.type == "weld") && j.a == d.id) rot(j.p);
        if (j.type == "track") { if (j.a == d.id) rot(j.p); if (j.b == d.id) for (auto& q : j.path) rot(q); }
        if (j.type == "magwell" && j.a == d.id) { rot(j.p); for (auto& q : j.path) rot(q); }
        if (j.type == "spring") { if (j.a == d.id) rot(j.pa); if (j.b == d.id) rot(j.pb); }
    }
    auto& br = A.design.breech;
    if (br.on) { if (br.breech == d.id && br.hasFace) rot(br.face); if (br.chamber == d.id && br.hasMuzzle) rot(br.muzzle); }
    for (HandHold* h : {&A.design.pose.firing, &A.design.pose.support, &A.design.pose.butt})
        if (h->on && h->part == d.id) { rot(h->at); h->angle += deg; }
    if (br.on) for (auto& cm : br.more) { if ((cm.breech.empty() ? br.breech : cm.breech) == d.id) rot(cm.face); if (cm.chamber == d.id && cm.hasMuzzle) rot(cm.muzzle); }
    fixShape(d);
}
// Measuring snaps to a corner or joint point within a few pixels of the cursor.
V2 measureSnap(V2 w) {
    Vector2 m = GetMousePosition();
    V2 best = w; float bd = 9;
    auto consider = [&](V2 p, float z) { float d = Vector2Distance(toScreen(W3(p, z)), m); if (d < bd) { bd = d; best = p; } };
    for (size_t i = 0; i < A.design.bodies.size(); i++) for (auto& p : A.design.bodies[i].pts) consider(p, bodyFront((int)i));
    for (auto& j : A.design.joints) { consider(j.p, 0); for (auto& q : j.path) consider(q, 0); }
    return best;
}
// A barrel from the breech end (rear) to the muzzle: thick over the chamber, slimmer beyond it; turned round.
BodyDef makeBarrel(V2 rear, V2 muzzle, const Cartridge& cart) {
    BodyDef b;
    double dx = muzzle.x - rear.x, dy = muzzle.y - rear.y, L = std::hypot(dx, dy);
    double ang = std::atan2(dy, dx) * 180 / 3.14159265358979;
    bool level = std::fabs(ang) < 3 || std::fabs(std::fabs(ang) - 180) < 3;
    if (level) { dy = 0; muzzle.y = rear.y; L = std::fabs(dx); }
    V2 u{dx / L, dy / L}, n{-u.y, u.x};
    double rc = cart.headDia() / 2 + 4, rm = std::min(rc, std::max(cart.bulletDia / 2 + 3.5, rc * 0.55)), lc = cart.caseLen + 8;
    auto at = [&](double s, double r) { return V2{rear.x + u.x * s + n.x * r, rear.y + u.y * s + n.y * r}; };
    if (L > lc + 20) b.pts = {at(0, rc), at(lc, rc), at(lc + 15, rm), at(L, rm), at(L, -rm), at(lc + 15, -rm), at(lc, -rc), at(0, -rc)};
    else b.pts = {at(0, rc), at(L, rc), at(L, -rc), at(0, -rc)};
    b.layer = "ABC"; b.fixed = true; b.color = "#3f4654"; b.round = true; b.roundAxis = level ? 0 : 2; b.thick = 2 * rc;
    b.barrel.on = true; b.barrel.cart = cart;
    b.barrel.rifled = cart.kind != "shotshell";   // shot goes down a smooth bore; bullets, a rifled one
    fixShape(b);
    carve::RoundAxis ax = carve::roundAxis(b);
    b.barrel.muzzleEnd = dx * ax.dir.x + dy * ax.dir.y >= 0 ? 1 : -1;
    return b;
}
void deleteSel() {
    auto& D = A.design;
    if (A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size() && A.selCarve >= 0 && A.selCarve < (int)D.bodies[A.sel.idx].carves.size()) {
        auto& cs = D.bodies[A.sel.idx].carves;  // a selected carve goes, not the part
        cs.erase(cs.begin() + A.selCarve);
        A.selCarve = -1;
        commit();
        return;
    }
    if (A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size()) {
        BodyDef& d = D.bodies[A.sel.idx];
        if (!A.selVs.empty() && d.pts.size() >= A.selVs.size() + 3) {
            fixShape(d);
            std::vector<int> vs = A.selVs;
            std::sort(vs.rbegin(), vs.rend());
            for (int v : vs) if (v < (int)d.pts.size()) { d.pts.erase(d.pts.begin() + v); d.rad.erase(d.rad.begin() + v); d.bulge.erase(d.bulge.begin() + v); }
            A.selVs.clear(); A.sel.v = -1;
        } else if (!A.selVs.empty()) { toast("A part needs at least 3 vertices."); return; }
        else {
            std::string id = d.id;
            D.bodies.erase(D.bodies.begin() + A.sel.idx);
            D.joints.erase(std::remove_if(D.joints.begin(), D.joints.end(), [&](const JointDef& j) { return j.a == id || j.b == id || j.rel == id; }), D.joints.end());
            for (HandHold* h : {&D.pose.firing, &D.pose.support, &D.pose.butt}) if (h->part == id) h->on = false;  // nothing left to hold
            A.sel = {};
        }
    } else if (A.sel.kind == 2 && A.sel.idx < (int)D.joints.size()) {
        D.joints.erase(D.joints.begin() + A.sel.idx);
        A.sel = {};
    } else return;
    commit();
}
void duplicateSel() {
    if (A.sel.kind != 1 || A.sel.idx >= (int)A.design.bodies.size()) return;
    BodyDef c = A.design.bodies[A.sel.idx];
    c.id = uid(); c.name += " copy"; c.magazine = false; c.weld.clear(); c.mirror.clear();  // a copy is a part on its own
    for (auto& p : c.pts) { p.x += 10; p.y += 10; }
    carve::moveCarves(c, 10, 10);
    if (c.ctrl != "none") { c.ctrlPt.x += 10; c.ctrlPt.y += 10; }
    A.design.bodies.push_back(c);
    A.sel = {1, (int)A.design.bodies.size() - 1, -1}; A.selVs.clear();
    commit();
}

void finishBody() {
    if (A.draft.size() >= 3) {
        static const char* pal[] = {"#3f7fe0", "#c9ccd2", "#f0a43a", "#5cb85c", "#d9534f", "#9b7fd4", "#e8d44d"};
        BodyDef b;
        b.id = uid(); b.name = "Part " + std::to_string(A.design.bodies.size() + 1);
        b.layer = "ABC"; b.thick = 12; b.color = pal[A.design.bodies.size() % 7]; b.pts = A.draft;
        fixShape(b);
        A.design.bodies.push_back(b);
        A.sel = {1, (int)A.design.bodies.size() - 1, -1};
        commit();
    }
    A.drafting = false; A.draft.clear(); A.tool = Tool::Select;
}
void finishRoundPath() {
    auto& D = A.design;
    if (A.sel.kind == 1 && A.sel.idx < (int)D.bodies.size() && D.bodies[A.sel.idx].magazine && A.draft.size() >= 2) {
        D.bodies[A.sel.idx].roundPath = A.draft;
        commit();
        toast("Round path set. Drag its points to adjust it.");
    } else if (A.draft.size() < 2) toast("A round path needs at least two points.");
    A.draft.clear(); A.drafting = false; A.tool = Tool::Select;
}
int trackHolderAt(V2 w) {  // the part a new track's slot is cut in: the topmost one under the path's first point
    std::vector<std::string> group;  // never a part welded to the one that slides: it moves with it
    for (int k : weldedWith(A.design, A.design.bodyIndex(A.draftA))) group.push_back(A.design.bodies[k].id);
    if (group.empty()) group.push_back(A.draftA);
    for (int i = (int)A.design.bodies.size() - 1; i >= 0; i--) {
        auto& d = A.design.bodies[i];
        if (std::find(group.begin(), group.end(), d.id) == group.end() && sim::inPoly(w.x, w.y, d.pts)) return i;
    }
    return -1;  // nothing there: the slot is fixed to the ground
}
static std::string partName(int i) {
    if (i < 0) return "the ground";
    auto& d = A.design.bodies[i];
    return "the " + (d.name.empty() ? d.id : d.name);
}
std::string trackHint() {  // prompt while a track is being drawn
    if (!A.draftHasF) return "Track: click the part that should slide, on the spot that rides in the slot (for a bolt, its middle). "
                             "If it's hidden under another part, select it first.";
    int ri = -1;
    for (int i = 0; i < (int)A.design.bodies.size(); i++) if (A.design.bodies[i].id == A.draftA) ri = i;
    std::string who = partName(ri), where = partName(trackHolderAt(A.draft[0]));
    who[0] = 'T';
    if (A.draft.size() < 2) return who + " will slide in a slot in " + where + ". Now click where it slides to. Hold Shift to add a bend. Esc cancels.";
    return "Click the next point of the slot. Click without Shift (or press Enter) on the last one to finish.";
}
void finishTrack() {
    if (A.draftHasF && A.draft.size() >= 2) {
        JointDef j; j.type = "track"; j.a = A.draftA; j.p = A.draftF; j.path = A.draft; j.b = "world";
        int h = trackHolderAt(A.draft[0]);
        if (h >= 0) j.b = A.design.bodies[h].id;
        A.design.joints.push_back(j);
        A.sel = {2, (int)A.design.joints.size() - 1, -1};
        commit();
    }
    A.drafting = false; A.draft.clear(); A.draftHasF = false; A.tool = Tool::Select;
}
void makeMagwell(const BodyDef& b) {  // give a magazine a straight well, sliding out downward
    double y0 = 1e9, y1 = -1e9;
    for (auto& p : b.pts) { y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    V2 c = sim::polyCentroid(b.pts);
    JointDef j; j.type = "magwell"; j.a = b.id; j.b = "world"; j.p = {std::round(c.x * 2) / 2, std::round(c.y * 2) / 2};
    for (int i = (int)A.design.bodies.size() - 1; i >= 0; i--) {
        auto& d = A.design.bodies[i];
        if (d.fixed && sim::inPoly(c.x, c.y, d.pts)) { j.b = d.id; break; }
    }
    if (j.b == "world") for (auto& d : A.design.bodies) if (d.fixed) { j.b = d.id; break; }
    j.path = {j.p, {j.p.x, j.p.y + std::round(y1 - y0 + 4)}};
    A.design.joints.push_back(j);
}

void buildInput(bool overUI) {
    {   // vertex selection belongs to one part: drop it when the selection moves elsewhere
        static std::string selVsPart;
        std::string cur = (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size()) ? A.design.bodies[A.sel.idx].id : "";
        if (cur != selVsPart) { A.selVs.clear(); if (!selVsPart.empty()) A.selCarve = -1; selVsPart = cur; }
        if (!cur.empty()) { int n = (int)A.design.bodies[A.sel.idx].pts.size(); A.selVs.erase(std::remove_if(A.selVs.begin(), A.selVs.end(), [n](int v) { return v < 0 || v >= n; }), A.selVs.end()); }
    }
    if (A.tool == Tool::Carve || A.carveDrag) { carveInput(overUI); return; }
    if (A.tool == Tool::Faces || A.faceDragging) { facesInput(overUI); return; }
    if (A.tool == Tool::Select && A.sel.kind == 1 && A.dragKind == 0 && IsKeyPressed(KEY_E)) { facesInput(overUI); if (A.faceDragging) return; }
    Vector2 m = GetMousePosition();
    V2 w = snapMM(mouseMM(0));
    if (!overUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        float now = (float)GetTime();
        bool dbl = now - A.lastClickT < 0.32f && Vector2Distance(m, A.lastClickPos) < 6;
        A.lastClickT = now; A.lastClickPos = m;
        switch (A.tool) {
        case Tool::Body: {
            if (A.draft.size() >= 3 && Vector2Distance(toScreen(W3(A.draft[0], 0)), m) < 10) { finishBody(); return; }
            if ((int)A.draft.size() >= MAX_CORNERS) { toast(TextFormat("%d corners is the most a part can have. Click the first corner or press Enter to close it.", MAX_CORNERS)); return; }
            A.drafting = true; A.draft.push_back(w);
            return;
        }
        case Tool::Weld: {  // click one part, then another: they become one piece. Nothing is added to the gun.
            auto hits = bodiesUnderMouse();
            if (hits.empty()) { toast("Click on a part."); return; }
            std::string id = A.design.bodies[hits[0].body].id;
            if (!A.draftHasF) {
                A.draftHasF = true; A.draftA = id; A.draftF = w; A.sel = {1, hits[0].body, -1};
                return;
            }
            if (id == A.draftA) { if (hits.size() > 1) id = A.design.bodies[hits[1].body].id; else return; }
            weldParts(A.design, A.draftA, id);
            int i = A.design.bodyIndex(id);
            toast(A.design.bodies[A.design.bodyIndex(A.draftA)].name + " and " + A.design.bodies[i].name + " are one piece now.");
            A.draftHasF = false; A.sel = {1, i, -1};
            commit(); A.tool = Tool::Select;
            return;
        }
        case Tool::Pin: {
            auto hits = bodiesUnderMouse();
            if (hits.empty()) { toast("Click on a part."); return; }
            JointDef j; j.type = "pin";
            j.a = A.design.bodies[hits[0].body].id;
            j.b = hits.size() > 1 ? A.design.bodies[hits[1].body].id : "world";
            j.p = w;
            A.design.joints.push_back(j); A.sel = {2, (int)A.design.joints.size() - 1, -1}; commit();
            return;
        }
        case Tool::Spring: {
            auto hits = bodiesUnderMouse();
            if (!A.draftHasF) { A.draftHasF = true; A.draftF = w; A.draftA = hits.empty() ? "world" : A.design.bodies[hits[0].body].id; return; }
            JointDef j; j.type = "spring"; j.a = A.draftA; j.pa = A.draftF; j.pb = w; j.b = "world";
            for (auto& h : hits) if (A.design.bodies[h.body].id != j.a) { j.b = A.design.bodies[h.body].id; break; }
            j.rest = std::round(std::hypot(j.pb.x - j.pa.x, j.pb.y - j.pa.y) * 1.15); j.k = 1;
            j.kind = "coil"; j.coilDia = 6; j.wire = 0.8; j.turns = std::max(4.0, std::round(j.rest / 5));
            A.design.joints.push_back(j); A.sel = {2, (int)A.design.joints.size() - 1, -1};
            A.draftHasF = false; commit(); A.tool = Tool::Select;
            return;
        }
        case Tool::RoundPath: {
            if (dbl && A.draft.size() >= 2) { finishRoundPath(); return; }
            A.draft.push_back(w); A.drafting = true;
            return;
        }
        case Tool::Track: {  // two clicks: the spot on the part that slides, then where it slides to (Shift adds bends)
            if (!A.draftHasF) {
                auto hits = bodiesUnderMouse();
                if (hits.empty()) { toast("Click on the part that should slide."); return; }
                int ri = hits[0].body;  // a selected part wins, so a part hidden under another can still be picked
                for (auto& h : hits) if (A.sel.kind == 1 && h.body == A.sel.idx) ri = h.body;
                A.draftHasF = true; A.draftF = w; A.draftA = A.design.bodies[ri].id; A.drafting = true;
                A.draft = {w};
                return;
            }
            if (dbl && A.draft.size() >= 2) { finishTrack(); return; }
            if (std::hypot(w.x - A.draft.back().x, w.y - A.draft.back().y) < 0.5) { toast("Click where it slides to."); return; }
            A.draft.push_back(w);
            if (!(IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))) finishTrack();
            return;
        }
        case Tool::Face: case Tool::Muzzle: {
            auto& br = A.design.breech;
            if (!br.on) { br = BreechDef(); br.on = true; }
            if (A.chamberEdit >= 0 && A.chamberEdit < (int)br.more.size()) {  // a further chamber's point
                auto& c = br.more[A.chamberEdit];
                if (A.tool == Tool::Face) c.face = w; else { c.muzzle = w; c.hasMuzzle = true; }
            } else if (A.tool == Tool::Face) { br.face = w; br.hasFace = true; } else { br.muzzle = w; br.hasMuzzle = true; }
            A.chamberEdit = -1;
            commit(); A.tool = Tool::Select;
            return;
        }
        case Tool::Rect: case Tool::Circle: case Tool::Knife: case Tool::Barrel: A.shapeStart = w; A.shaping = true; return;
        case Tool::Measure: A.measureA = A.measureB = measureSnap(w); A.measured = true; A.shaping = true; return;
        case Tool::Mirror:
            if (A.sel.kind != 1) { toast("Select the part to mirror first."); A.tool = Tool::Select; return; }
            A.shapeStart = w; A.shaping = true; return;
        case Tool::CtrlPt: {
            if (A.sel.kind == 1 && A.sel.idx < (int)A.design.bodies.size()) { A.design.bodies[A.sel.idx].ctrlPt = w; commit(); }
            A.tool = Tool::Select; return;
        }
        case Tool::StrikerTip: {
            auto& br = A.design.breech;
            br.strikerTip = w; commit(); A.tool = Tool::Select; return;
        }
        case Tool::Select: break;
        }
        if (dbl && A.sel.kind == 1) {  // insert vertex on an edge of the selected part
            auto& d = A.design.bodies[A.sel.idx];
            if (auto* h = handleAt(m); h && h->kind == 7 && h->i == A.sel.idx) {  // on an edge's diamond: exactly halfway
                if (!splitReady()) return;
                int nv = splitEdge(d, h->v);
                if (nv < 0) { cornerLimitToast(); return; }
                A.sel.v = nv; A.selVs = {nv}; commit(); return;
            }
            float z = bodyFront(A.sel.idx);
            int best = -1; float bd = 10;
            for (size_t k = 0; k < d.pts.size(); k++) {
                Vector2 a = toScreen(W3(d.pts[k], z)), b = toScreen(W3(d.pts[(k + 1) % d.pts.size()], z));
                double d2; sim::closestOnSeg({m.x, m.y}, {a.x, a.y}, {b.x, b.y}, &d2);
                if (std::sqrt(d2) < bd) { bd = (float)std::sqrt(d2); best = (int)k; }
            }
            if (best >= 0) {
                if (!splitReady()) return;
                if ((int)d.pts.size() >= MAX_CORNERS) { cornerLimitToast(); return; }
                fixShape(d);
                d.pts.insert(d.pts.begin() + best + 1, snapMM(mouseMM(z)));
                d.rad.insert(d.rad.begin() + best + 1, 0.0);
                d.bulge.insert(d.bulge.begin() + best + 1, 0.0);
                d.bulge[best] = 0;
                A.sel.v = best + 1; A.selVs = {best + 1}; commit(); return;
            }
        }
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (auto* h = handleAt(m)) {
            Handle hh = *h;
            // everything is picked with one press and moved with the next, so a point can be selected without moving it:
            // a corner or edge of the selected part (its own selection), and any other handle (a joint's point, the
            // breech face or muzzle, a magazine's round path point), which also selects its joint
            bool own = hh.kind == 0 || hh.kind == 7;
            if (own) {
                auto& d = A.design.bodies[hh.i];
                int n = (int)d.pts.size();
                bool picked = hh.kind == 0 ? std::find(A.selVs.begin(), A.selVs.end(), hh.v) != A.selVs.end()
                                           : A.sel.v == -1 && A.selVs.size() == 2 && std::find(A.selVs.begin(), A.selVs.end(), hh.v) != A.selVs.end() &&
                                                 std::find(A.selVs.begin(), A.selVs.end(), (hh.v + 1) % n) != A.selVs.end();
                if (hh.kind == 0 && shift) {   // Shift-click adds a corner to the selection, or takes it out
                    auto it = std::find(A.selVs.begin(), A.selVs.end(), hh.v);
                    if (it != A.selVs.end()) A.selVs.erase(it); else A.selVs.push_back(hh.v);
                    A.sel.v = hh.v; A.dragKind = 0;
                    return;
                }
                if (!picked) {
                    if (hh.kind == 0) { A.selVs = {hh.v}; A.sel.v = hh.v; }
                    else { A.selVs = {hh.v, (hh.v + 1) % n}; A.sel.v = -1; }
                    A.armedKind = -1; A.dragKind = 0;
                    return;
                }
            } else if (!(A.armedKind == hh.kind && A.armedI == hh.i && A.armedV == hh.v)) {
                A.armedKind = hh.kind; A.armedI = hh.i; A.armedV = hh.v; A.dragKind = 0;
                if (hh.kind >= 1 && hh.kind <= 4 && hh.i >= 0) { A.sel = {2, hh.i, -1}; A.selVs.clear(); }
                return;
            }
            A.dragKind = 1; A.dragH = hh; A.dragMoved = false; A.dragPress = m; A.dragLive = false; A.dragExtrude = false;
            if (hh.kind == 0 || hh.kind == 7) {
                auto& d = A.design.bodies[hh.i];
                fixShape(d);
                float z = bodyFront(hh.i);
                if (hh.kind == 0) {
                    A.sel.v = hh.v;
                } else {
                    int n = (int)d.pts.size(), v = hh.v;
                    if (shift && n + 2 > MAX_CORNERS) { cornerLimitToast(); shift = false; }  // too many corners: just move the edge
                    A.selVs = {v, (v + 1) % n};
                    A.dragExtrude = shift;   // extrude: the edge's two corners are copied once the drag really starts
                    A.sel.v = -1;
                }
                A.dragOrig = d.pts;
                A.dragMM = mouseMM(z);
            } else if (hh.i >= 0) { A.sel = {2, hh.i, -1}; A.selVs.clear(); }
            return;
        }
        auto hits = bodiesUnderMouse();  // nearest first
        if (!hits.empty()) {
            // the selected part (or its welded piece) under the cursor: a drag moves it, and a plain click picks the
            // next part behind it. Anything else: the first click only selects it, and doesn't move it.
            int pos = -1;
            if (A.sel.kind == 1 && A.sel.idx < (int)A.design.bodies.size()) {
                auto grp = weldedWith(A.design, A.sel.idx);
                for (size_t k = 0; k < hits.size() && pos < 0; k++)
                    if (hits[k].body == A.sel.idx || std::find(grp.begin(), grp.end(), hits[k].body) != grp.end()) pos = (int)k;
                for (size_t k = 0; k < hits.size(); k++) if (hits[k].body == A.sel.idx) pos = (int)k;  // (itself, if it's there)
            }
            A.armedKind = -1;
            if (pos >= 0) {
                A.dragKind = 2; A.dragStart = w; A.dragLast = {0, 0}; A.dragMoved = false; A.dragPress = m; A.dragLive = false;
                A.pressNext = hits.size() > 1 ? hits[(pos + 1) % hits.size()].body : -1;
            } else {
                A.sel = {1, hits[0].body, -1}; A.selVs.clear(); A.selCarve = -1;
                A.dragKind = 0;
                if (hits.size() > 1) toast("Selected " + A.design.bodies[hits[0].body].name + ". Click it again for the part behind it (" + std::to_string(hits.size()) + " here). Drag to move it.");
            }
            return;
        }
        // empty space: box-select vertices of the selected part (a plain click deselects)
        A.boxing = true; A.boxStart = m; A.armedKind = -1;
        return;
    }
    if (A.boxing && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        A.boxing = false;
        Rectangle r{std::min(A.boxStart.x, m.x), std::min(A.boxStart.y, m.y), std::fabs(m.x - A.boxStart.x), std::fabs(m.y - A.boxStart.y)};
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (r.width < 4 && r.height < 4) { if (!shift) { A.sel = {}; A.selVs.clear(); } return; }
        if (A.sel.kind == 1 && A.sel.idx < (int)A.design.bodies.size()) {
            auto& d = A.design.bodies[A.sel.idx];
            float z = bodyFront(A.sel.idx);
            if (!shift) A.selVs.clear();
            for (size_t v = 0; v < d.pts.size(); v++)
                if (CheckCollisionPointRec(toScreen(W3(d.pts[v], z)), r) && std::find(A.selVs.begin(), A.selVs.end(), (int)v) == A.selVs.end()) A.selVs.push_back((int)v);
            if (!A.selVs.empty()) toast(TextFormat("%d vert%s selected. Drag one to move them together; arrow keys nudge.", (int)A.selVs.size(), A.selVs.size() == 1 ? "ex" : "ices"));
        }
        return;
    }
    if (A.shaping && A.tool == Tool::Measure) {  // the line follows the mouse until you let go; it stays shown after
        A.measureB = measureSnap(w);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) A.shaping = false;
        return;
    }
    if (A.shaping && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (A.tool == Tool::Knife || A.tool == Tool::Mirror)) {
        A.shaping = false;
        V2 a = A.shapeStart;
        if (std::hypot(w.x - a.x, w.y - a.y) < 1) { A.tool = Tool::Select; return; }
        auto& D = A.design;
        if (A.tool == Tool::Mirror && A.sel.kind == 1) {
            BodyDef& src = D.bodies[A.sel.idx];
            std::string tag = "m-" + uid();
            src.mirror = tag; src.mirrorKind = "line"; src.mirrorA = a; src.mirrorB = w;
            BodyDef c = mirrorImage(src);
            c.id = uid(); c.name += " (mirror)"; c.magazine = false; c.weld.clear();
            D.bodies.push_back(c);
            A.sel = {1, (int)D.bodies.size() - 1, -1}; A.selVs.clear();
            commit(); toast("Mirrored copy added. It's live: change either and the other follows (Unlink in its panel to stop that).");
        } else if (A.tool == Tool::Knife) {
            std::vector<int> targets;
            if (A.sel.kind == 1) targets.push_back(A.sel.idx);
            else for (int i = 0; i < (int)D.bodies.size(); i++) targets.push_back(i);
            int made = 0, last = -1;
            for (int ti = (int)targets.size() - 1; ti >= 0; ti--) {
                int i = targets[ti];
                auto pcs = splitBody(D.bodies[i], a, w);
                if (pcs.size() < 2) continue;
                std::string base = D.bodies[i].name;
                D.bodies[i] = pcs[0];
                for (size_t k = 1; k < pcs.size(); k++) {
                    BodyDef nb = pcs[k];
                    nb.id = uid(); nb.name = base + " " + std::to_string(k + 1); nb.magazine = false; nb.ctrl = "none"; nb.weld.clear();
                    D.bodies.push_back(nb);
                    last = (int)D.bodies.size() - 1;
                }
                made += (int)pcs.size() - 1;
            }
            if (made) { if (last >= 0) A.sel = {1, last, -1}; A.selVs.clear(); commit(); toast(TextFormat("Cut: %d new part%s. Delete one to carve, or keep it.", made, made > 1 ? "s" : "")); }
            else toast("The cut must cross a part completely.");
        }
        A.tool = Tool::Select;
        return;
    }
    if (A.shaping && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {  // finish a rectangle or circle
        A.shaping = false;
        V2 a = A.shapeStart;
        BodyDef b;
        static const char* pal[] = {"#3f7fe0", "#c9ccd2", "#f0a43a", "#5cb85c", "#d9534f", "#9b7fd4", "#e8d44d"};
        b.id = uid(); b.name = "Part " + std::to_string(A.design.bodies.size() + 1);
        b.layer = "ABC"; b.thick = 12; b.color = pal[A.design.bodies.size() % 7];
        if (A.tool == Tool::Barrel) {
            if (std::hypot(w.x - a.x, w.y - a.y) >= 10) {
                const Cartridge& cart = A.design.breech.on ? A.design.breech.cart : cartridgePresets()[0];
                BodyDef br = makeBarrel(a, w, cart);
                br.id = uid(); br.name = barrelCount(A.design) ? "Barrel " + std::to_string(barrelCount(A.design) + 1) : "Barrel";
                A.design.bodies.push_back(br);
                A.sel = {1, (int)A.design.bodies.size() - 1, -1};
                syncBarrels(A.design);
                commit();
                toast(A.design.breech.breech.empty() ? "Barrel drawn. In Firing, pick the part that closes its breech (a bolt, a slide, a standing breech)."
                                                    : "Barrel drawn, chambered for " + cart.name + ". Change it in its panel.");
            } else toast("Drag from the breech end to the muzzle.");
            A.tool = Tool::Select;
            return;
        }
        if (A.tool == Tool::Rect) {
            double x0 = std::min(a.x, w.x), x1 = std::max(a.x, w.x), y0 = std::min(a.y, w.y), y1 = std::max(a.y, w.y);
            if (x1 - x0 >= 1 && y1 - y0 >= 1) b.pts = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        } else {
            double r = std::hypot(w.x - a.x, w.y - a.y);
            if (r >= 1) b.pts = circlePts(a, r, &b.bulge);
        }
        if (b.pts.size() >= 3) {
            fixShape(b);
            A.design.bodies.push_back(b);
            A.sel = {1, (int)A.design.bodies.size() - 1, -1};
            commit();
        }
        A.tool = Tool::Select;
        return;
    }
    if (A.dragKind == 1 || A.dragKind == 2) {
        // a click that wobbles a little isn't a drag: nothing moves until the mouse has gone a few pixels
        if (!A.dragLive && Vector2Distance(m, A.dragPress) >= 5 * CFG.uiScale) {
            A.dragLive = true;
            if (A.dragKind == 1 && A.dragExtrude && A.dragH.kind == 7 && A.dragH.i < (int)A.design.bodies.size()) {
                // Shift-drag on an edge: duplicate its two corners and drag the copies, pulling out a new step
                auto& d = A.design.bodies[A.dragH.i];
                int n = (int)d.pts.size(), v = A.dragH.v;
                if (v < n) {
                    V2 a = d.pts[v], b = d.pts[(v + 1) % n];
                    int at = v + 1;
                    d.pts.insert(d.pts.begin() + at, {a, b});
                    d.rad.insert(d.rad.begin() + at, {0.0, 0.0});
                    d.bulge.insert(d.bulge.begin() + at, {0.0, 0.0});
                    d.bulge[v] = 0;
                    A.selVs = {at, at + 1};
                    A.dragOrig = d.pts;
                    A.dragMoved = true;
                }
            }
            A.dragExtrude = false;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !A.dragLive) {}
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            // X, Y or Z while dragging: keep to one axis (press it again to free it). Z moves a part in depth.
            for (auto [key, ax] : {std::pair{KEY_X, 1}, std::pair{KEY_Y, 2}, std::pair{KEY_Z, 3}})
                if (IsKeyPressed(key)) {
                    if (ax == 3 && A.dragKind != 2) { toast("Only a whole part moves in depth: corners stay on its outline."); continue; }
                    A.axisLock = A.axisLock == ax ? 0 : ax;
                    A.zDrag = A.zDragLast = 0;
                }
            if (A.dragKind == 2 && A.axisLock == 3) {  // depth: the mouse's up and down pushes it toward you or away
                float perPx = (A.ortho ? A.orthoH : A.dist * 2 * std::tan(20 * DEG2RAD)) / std::max(1, A.vh) / U;
                A.zDrag -= GetMouseDelta().y * perPx;
                double dz = snapv(A.zDrag, A.snapOn) - A.zDragLast;
                if (dz != 0) {
                    for (int k : weldedWith(A.design, A.sel.idx)) A.design.bodies[k].z += dz;
                    A.zDragLast += dz; A.dragMoved = true;
                }
            }
            if (A.dragKind == 2 && A.axisLock != 3) {
                V2 d{snapv(w.x - A.dragStart.x, A.snapOn), snapv(w.y - A.dragStart.y, A.snapOn)};
                if (A.axisLock == 1) d.y = 0;
                if (A.axisLock == 2) d.x = 0;
                double ddx = d.x - A.dragLast.x, ddy = d.y - A.dragLast.y;
                if (ddx != 0 || ddy != 0) { for (int k : weldedWith(A.design, A.sel.idx)) moveBody(k, ddx, ddy); A.dragLast = d; A.dragMoved = true; }
            } else if (A.dragKind == 1) {
                auto& h = A.dragH;
                auto& br = A.design.breech;
                V2 p = w;
                if (h.kind == 0 || h.kind == 7) {  // move every selected vertex by the same snapped amount
                    auto& d = A.design.bodies[h.i];
                    float z = bodyFront(h.i);
                    V2 mm = mouseMM(z);
                    double dx = snapv(mm.x - A.dragMM.x, A.snapOn), dy = snapv(mm.y - A.dragMM.y, A.snapOn);
                    if (A.axisLock == 1) dy = 0;
                    if (A.axisLock == 2) dx = 0;
                    if (A.dragOrig.size() == d.pts.size())
                        for (int v : A.selVs) if (v >= 0 && v < (int)d.pts.size()) d.pts[v] = {A.dragOrig[v].x + dx, A.dragOrig[v].y + dy};
                    if (dx != 0 || dy != 0) A.dragMoved = true;
                }
                else if (h.kind == 1) A.design.joints[h.i].p = p;
                else if (h.kind == 2) A.design.joints[h.i].path[h.v] = p;
                else if (h.kind == 8 && h.i < (int)A.design.bodies.size() && h.v < (int)A.design.bodies[h.i].roundPath.size()) A.design.bodies[h.i].roundPath[h.v] = p;
                else if (h.kind == 3) A.design.joints[h.i].pa = p;
                else if (h.kind == 4) A.design.joints[h.i].pb = p;
                else if (h.kind == 5) { if (h.i >= 0 && h.i < (int)br.more.size()) br.more[h.i].face = p; else br.face = p; }
                else if (h.kind == 6) { if (h.i >= 0 && h.i < (int)br.more.size()) br.more[h.i].muzzle = p; else br.muzzle = p; }
                if (h.kind != 0 && h.kind != 7) A.dragMoved = true;   // (kind 8: a magazine's round path point)
            }
        } else {
            if (A.dragMoved) commit();
            else if (A.dragKind == 2 && A.pressNext >= 0 && A.pressNext < (int)A.design.bodies.size()) {  // a click: the next one behind
                A.sel = {1, A.pressNext, -1}; A.selVs.clear(); A.selCarve = -1;
            }
            A.pressNext = -1;
            A.dragKind = 0; A.axisLock = 0;
        }
    }
}
