// Gunsmith3D - Build mode: editing operations and the drawing tools.
#include "app/refpics.hpp"
#include "app/units.hpp"
#include "imgui.h"
#include "app/settings.hpp"
#include "app/app.hpp"
#include "app/settings.hpp"
#include "carve.hpp"
#include "app/carvetool.hpp"
#include "app/engrave.hpp"
#include "app/scaletool.hpp"
#include "app/gfx.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "mounts.hpp"
#include "app/faces.hpp"
#include "app/meshtool.hpp"
#include "app/gizmo.hpp"

#include <algorithm>
#include <cstdio>
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
    if (d.gen.on) { d.gen.top.x += dx; d.gen.top.y += dy; for (auto& p : d.gen.pts0) { p.x += dx; p.y += dy; } }   // a fitted magazine moved, still fitted
    carve::moveCarves(d, dx, dy);
    mounts::moveMounts(d, dx, dy);
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
// ---------------------------------------------------------------- the selected part's rotation rings
// Z (toward you): turn it in the side view, with what's attached (as Turn does). X and Y: lean it out of the side
// view (its Tilt X and Tilt Y).
namespace {
Rings gPartR;
Design gPartSnap;
double gPartSign = 1;
bool partRingsShown() {
    return A.mode == Mode::Build && A.tool == Tool::Select && A.rotateMode && A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size()
        && A.dragKind == 0 && !A.faceDragging && !A.posing;
}
void partRingsSetup() {
    int i = A.sel.idx;
    V2 c = centroidOf(A.design.bodies[i]);
    gPartR.c = W3(c, bodyZ(i));
    gPartR.ax[0] = {1, 0, 0}; gPartR.ax[1] = {0, 1, 0}; gPartR.ax[2] = {0, 0, 1};
    gPartR.label[0] = "Lean X "; gPartR.label[1] = "Lean Y "; gPartR.label[2] = "Turn ";
    ringsFit(gPartR, [](Vector3 w) { return toScreen(w); }, 65);
}
void partRingsApply(int axis, double deg) {
    int i = A.sel.idx;
    BodyDef& d = A.design.bodies[i];
    if (axis == 0) d.tiltX = std::remainder(d.tiltX + deg, 360.0);
    else if (axis == 1) d.tiltY = std::remainder(d.tiltY - deg, 360.0);   // (+ Tilt Y: its front toward you, the other way round the upright)
    else rotateBody(i, centroidOf(gPartSnap.bodies[i]), deg * gPartSign);
}
}  // namespace
bool gPartEnterHold = false;   // (Enter ended a turn: the held button does nothing more until let go)
std::string gRotTyped;   // degrees typed while turning a part on its rings (0.9.2)
bool partRingsInput(bool overUI) {
    ToScreen ts = [](Vector3 w) { return toScreen(w); };
    if (gPartR.axis >= 0) {
        bool enter = false;
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            for (int c = GetCharPressed(); c; c = GetCharPressed())
                if (((c >= '0' && c <= '9') || c == '.' || (c == '-' && gRotTyped.empty())) && gRotTyped.size() < 12) gRotTyped += (char)c;
            if (IsKeyPressed(KEY_BACKSPACE) && !gRotTyped.empty()) gRotTyped.pop_back();
            enter = (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && !gRotTyped.empty();
        }
        double deg = ringsDrag(gPartR, GetMousePosition(), IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
        if (!gRotTyped.empty()) {   // exactly the typed angle, the way the mouse turned it
            double v = std::atof(gRotTyped.c_str());
            deg = deg < 0 && v > 0 ? -v : v;
        }
        bool done = enter || !IsMouseButtonDown(MOUSE_BUTTON_LEFT) || A.sel.kind != 1 || A.sel.idx >= (int)gPartSnap.bodies.size();
        if (A.sel.kind == 1 && A.sel.idx < (int)gPartSnap.bodies.size()) { A.design = gPartSnap; partRingsApply(gPartR.axis, deg); }
        if (done) {
            static const char* axn[3] = {"Leaned", "Swung", "Turned"};
            if (A.sel.kind == 1 && A.sel.idx < (int)A.design.bodies.size()) A.nextLabel = std::string(axn[std::clamp(gPartR.axis, 0, 2)]) + " " + A.design.bodies[A.sel.idx].name + TextFormat(" %.1f\xc2\xb0", deg);
            gPartR.axis = -1; gRotTyped.clear(); commit();
            if (enter) gPartEnterHold = true;
        }
        return true;
    }
    if (gPartEnterHold) { if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return true; gPartEnterHold = false; }
    gPartR.hot = -1;
    if (!partRingsShown()) return false;
    partRingsSetup();
    gPartR.hot = overUI ? -1 : ringsPick(gPartR, ts, GetMousePosition());
    if (gPartR.hot < 0 || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    gPartSnap = A.design;
    if (gPartR.hot == 2) {   // which way turning goes on the screen: try a degree on its first corner
        int i = A.sel.idx;
        Vector3 c = gPartR.c;
        gPartSign = ringsSign([&](double v) {
            A.design = gPartSnap;
            if (v != 0) rotateBody(i, centroidOf(gPartSnap.bodies[i]), v);
            return W3(A.design.bodies[i].pts[0], bodyZ(i));
        }, c, gPartR.ax[2]);
        A.design = gPartSnap;
    }
    ringsBegin(gPartR, gPartR.hot, ts, GetMousePosition());
    return true;
}
// ---------------------------------------------------------------- move arrows on the picked corners (Select tool)
// Red along the gun, green up, blue across it (only for a part shaped in depth: those corners' depth). Every picked
// corner moves together; Ctrl or the snap steps by the grid.
namespace {
Arrows gCornerA;
std::vector<V2> gCornerOrig;
std::vector<double> gCornerZn, gCornerZf;
bool cornerArrowsShown() {
    if (!(A.mode == Mode::Build && A.tool == Tool::Select && !A.rotateMode && A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size()
          && !A.selVs.empty() && !A.faceDragging && !A.posing && CFG.editArrows)) return false;
    const BodyDef& d = A.design.bodies[A.sel.idx];
    return !meshShaped(d) && d.pts.size() >= 3;
}
void cornerArrowsSetup() {
    int i = A.sel.idx;
    const BodyDef& d = A.design.bodies[i];
    V2 c{0, 0}; int n = 0;
    for (int v : A.selVs) if (v >= 0 && v < (int)d.pts.size()) { c.x += d.pts[v].x; c.y += d.pts[v].y; n++; }
    if (n) { c.x /= n; c.y /= n; }
    gCornerA.c = W3(c, bodyFront(i));
    gCornerA.ax[0] = {1, 0, 0}; gCornerA.ax[1] = {0, 1, 0};
    gCornerA.ax[2] = d.depthZn.size() == d.pts.size() && d.depthZf.size() == d.pts.size() ? Vector3{0, 0, 1} : Vector3{0, 0, 0};
    gCornerA.label[0] = "Along"; gCornerA.label[1] = "Up"; gCornerA.label[2] = "Depth";
    arrowsFit(gCornerA, [](Vector3 w) { return toScreen(w); }, 64);
}
}  // namespace
bool cornerArrowsInput(bool overUI) {
    ToScreen ts = [](Vector3 w) { return toScreen(w); };
    if (gCornerA.axis >= 0) {
        int i = A.sel.idx;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || A.sel.kind != 1 || i < 0 || i >= (int)A.design.bodies.size()) { gCornerA.axis = -1; commit(); return true; }
        BodyDef& d = A.design.bodies[i];
        bool snap = (A.snapOn && !IsKeyDown(KEY_LEFT_ALT)) || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        double mm = arrowsDrag(gCornerA, GetMousePosition()) / U;
        if (snap) mm = snapv(mm, true);
        gCornerA.moved = mm * U;
        if (gCornerOrig.size() != d.pts.size()) return true;
        for (int v : A.selVs) {
            if (v < 0 || v >= (int)d.pts.size()) continue;
            d.pts[v] = gCornerOrig[v];
            if (gCornerA.axis == 0) d.pts[v].x += mm;
            if (gCornerA.axis == 1) d.pts[v].y -= mm;   // (the designer's y runs down the screen)
            if (gCornerA.axis == 2 && gCornerZn.size() == d.pts.size() && d.depthZn.size() == d.pts.size() && d.depthZf.size() == d.pts.size()) {
                d.depthZn[v] = gCornerZn[v] + mm; d.depthZf[v] = gCornerZf[v] + mm;
            }
        }
        return true;
    }
    gCornerA.hot = -1;
    if (!cornerArrowsShown() || A.dragKind != 0) return false;
    cornerArrowsSetup();
    gCornerA.hot = overUI ? -1 : arrowsPick(gCornerA, ts, GetMousePosition());
    if (gCornerA.hot < 0 || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    const BodyDef& d = A.design.bodies[A.sel.idx];
    gCornerOrig = d.pts; gCornerZn = d.depthZn; gCornerZf = d.depthZf;
    arrowsBegin(gCornerA, gCornerA.hot, ts, GetMousePosition());
    return true;
}
void cornerArrowsDraw() {
    if (gCornerA.axis < 0 && (!cornerArrowsShown() || A.dragKind != 0)) return;
    if (A.sel.kind != 1 || A.sel.idx < 0 || A.sel.idx >= (int)A.design.bodies.size()) return;
    cornerArrowsSetup();   // (while dragging too: they follow the corners)
    arrowsDraw(gCornerA, [](Vector3 w) { return toScreen(w); }, "mm", 1 / U);
}
void partRingTest(int axis, double deg) {   // (tests) as a drag of ring `axis` by `deg` would
    if (!partRingsShown()) { std::printf("PARTRING: nothing selected\n"); return; }
    partRingsSetup();
    gPartSnap = A.design;
    int i = A.sel.idx;
    Vector3 c = gPartR.c;
    Vector3 p0 = W3(A.design.bodies[i].pts[0], bodyZ(i));
    gPartSign = ringsSign([&](double v) { A.design = gPartSnap; if (v != 0) rotateBody(i, centroidOf(gPartSnap.bodies[i]), v); return W3(A.design.bodies[i].pts[0], bodyZ(i)); }, c, gPartR.ax[2]);
    A.design = gPartSnap;
    partRingsApply(axis, deg);
    Vector3 p1 = W3(A.design.bodies[i].pts[0], bodyZ(i));
    double turned = std::atan2(Vector3DotProduct(Vector3CrossProduct(Vector3Subtract(p0, c), Vector3Subtract(p1, c)), {0, 0, 1}), Vector3DotProduct(Vector3Subtract(p0, c), Vector3Subtract(p1, c))) * RAD2DEG;
    std::printf("PARTRING axis %d by %+.0f: tilt X %.1f, tilt Y %.1f, its first corner went %+.1f degrees round the Z axis\n", axis, deg, A.design.bodies[i].tiltX, A.design.bodies[i].tiltY, turned);
    commit();
}
void partRingsDraw() {
    if (gPartR.axis < 0) { if (!partRingsShown()) return; partRingsSetup(); }
    ringsDraw(gPartR, [](Vector3 w) { return toScreen(w); }, A.cam.position);
}

void rotateBody(int i, V2 c, double deg) {
    BodyDef& d = A.design.bodies[i];
    turnRoundAxis(d, deg);   // (a round part's axis turns with it: else it's spun about the old line, an hourglass)
    double a = deg * 3.14159265358979 / 180, cs = std::cos(a), sn = std::sin(a);
    auto rot = [&](V2& p) { double x = p.x - c.x, y = p.y - c.y; p = {c.x + x * cs - y * sn, c.y + x * sn + y * cs}; };
    for (auto& p : d.pts) rot(p);
    for (auto& o : d.depthOff) { double x = o.x, y = o.y; o = {x * cs - y * sn, x * sn + y * cs}; }   // (the far face turns with it)
    if (!d.depthRef.empty()) d.depthRef = d.pts;
    for (auto& p : d.roundPath) rot(p);
    if (d.gen.on) { rot(d.gen.top); d.gen.angle -= deg; for (auto& p : d.gen.pts0) rot(p); }   // a fitted magazine turned: still fitted, at its new angle
    mounts::rotateMounts(d, c, deg);
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
    for (size_t i = 0; i < A.design.bodies.size(); i++)
        if (partShown(A.design, A.design.bodies[i])) for (auto& p : A.design.bodies[i].pts) consider(p, bodyFront((int)i));   // (not a hidden layer's corners)
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
    c.ghost = false;   // (solid: see-through is only for parts you mark, and magazines)
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
    A.drafting = false; A.draft.clear(); A.tool = Tool::Faces;   // (then push and pull it into shape)
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

// ---- moving parts (0.9.2): snapping a dragged piece's edges and middle onto other parts' (within 7 px; Alt: free), and
// a typed distance (while dragging, or just after: a number and Enter moves it exactly that far along the drag)
namespace {
struct MoveGuide { bool x = false, y = false; double gx = 0, gy = 0; double y0 = 0, y1 = 0, x0 = 0, x1 = 0; } gMoveGuide;
std::string gMoveTyped;
bool gMoveEnter = false;   // Enter while dragging with a typed number: the move ends there
struct LastMove { bool on = false; std::string before, after; std::vector<std::string> ids; V2 dir{1, 0}; double dist = 0; } gLastMove;
struct Box2 { double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9; };
Box2 boxOf(const std::vector<int>& parts) {
    Box2 b;
    for (int k : parts) for (auto& p : outlineOf(A.design.bodies[k])) { b.x0 = std::min(b.x0, p.x); b.x1 = std::max(b.x1, p.x); b.y0 = std::min(b.y0, p.y); b.y1 = std::max(b.y1, p.y); }
    return b;
}
double pxPerMm() {   // at the selected part, in the view
    Vector3 a = W3({0, 0}, 0), b = W3({10, 0}, 0);
    if (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size()) { V2 c = centroidOf(A.design.bodies[A.sel.idx]); a = W3(c, bodyZ(A.sel.idx)); b = W3({c.x + 10, c.y}, bodyZ(A.sel.idx)); }
    return Vector2Distance(toScreen(a), toScreen(b)) / 10;
}
// d: the move so far (mm); snapped so the group's left, middle or right lines up with another part's, and the same up and down
V2 snapMove(V2 d, const std::vector<int>& group, V2 already) {
    gMoveGuide = {};
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) return d;
    double ppm = pxPerMm();
    if (ppm < 1e-6) return d;
    Box2 g = boxOf(group);   // where it is now, less the move already made: where it started
    g.x0 -= already.x; g.x1 -= already.x; g.y0 -= already.y; g.y1 -= already.y;
    double xs[3] = {g.x0, (g.x0 + g.x1) / 2, g.x1}, ys[3] = {g.y0, (g.y0 + g.y1) / 2, g.y1};
    double bx = 3 / ppm, by = 3 / ppm, gotx = d.x, goty = d.y;
    for (int k = 0; k < (int)A.design.bodies.size(); k++) {
        if (std::find(group.begin(), group.end(), k) != group.end() || !partShown(A.design, A.design.bodies[k])) continue;
        Box2 o = boxOf({k});
        // only parts near where it's going (within 15 mm of its box): on a busy gun every far part's edge matched something
        if (o.x0 > g.x1 + d.x + 15 || o.x1 < g.x0 + d.x - 15 || o.y0 > g.y1 + d.y + 15 || o.y1 < g.y0 + d.y - 15) continue;
        double ox[3] = {o.x0, (o.x0 + o.x1) / 2, o.x1}, oy[3] = {o.y0, (o.y0 + o.y1) / 2, o.y1};
        // edges to edges, middle to middle (not an edge to a middle: too many lines on a busy gun)
        for (int ia = 0; ia < 3; ia++) for (int it = 0; it < 3; it++) { if ((ia == 1) != (it == 1)) continue; double a = xs[ia], t = ox[it]; if (std::fabs(a + d.x - t) < bx) { bx = std::fabs(a + d.x - t); gotx = t - a; gMoveGuide.x = true; gMoveGuide.gx = t; gMoveGuide.y0 = std::min(o.y0, g.y0 + d.y); gMoveGuide.y1 = std::max(o.y1, g.y1 + d.y); } }
        for (int ia = 0; ia < 3; ia++) for (int it = 0; it < 3; it++) { if ((ia == 1) != (it == 1)) continue; double a = ys[ia], t = oy[it]; if (std::fabs(a + d.y - t) < by) { by = std::fabs(a + d.y - t); goty = t - a; gMoveGuide.y = true; gMoveGuide.gy = t; gMoveGuide.x0 = std::min(o.x0, g.x0 + d.x); gMoveGuide.x1 = std::max(o.x1, g.x1 + d.x); } }
    }
    return {gotx, goty};
}
// digits, a point, a minus (and unit letters after a digit); true on Enter
bool moveTypeKeys() {
    if (ImGui::GetIO().WantCaptureKeyboard) return false;
    for (int c = GetCharPressed(); c; c = GetCharPressed()) {
        bool digit = c >= '0' && c <= '9';
        if (digit || c == '.' || (c == '-' && gMoveTyped.empty()) || (!gMoveTyped.empty() && (c == ' ' || c == '/' || c == '"' || (c >= 'a' && c <= 'z' && c != 'x' && c != 'y' && c != 'z'))))
            if (gMoveTyped.size() < 24) gMoveTyped += (char)c;
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !gMoveTyped.empty()) gMoveTyped.pop_back();
    if (IsKeyPressed(KEY_ESCAPE)) gMoveTyped.clear();
    return (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && !gMoveTyped.empty();
}
}  // namespace
void drawMoveGuides() {   // (overlay) the lines a dragged part snapped to, and a number being typed
    Color gc{90, 220, 255, 220};
    if (A.dragKind == 2 && A.dragLive) {
        float z = A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size() ? bodyZ(A.sel.idx) : 0;
        if (gMoveGuide.x) DrawLineEx(toScreen(W3({gMoveGuide.gx, gMoveGuide.y0 - 8}, z)), toScreen(W3({gMoveGuide.gx, gMoveGuide.y1 + 8}, z)), 1.5f, gc);
        if (gMoveGuide.y) DrawLineEx(toScreen(W3({gMoveGuide.x0 - 8, gMoveGuide.gy}, z)), toScreen(W3({gMoveGuide.x1 + 8, gMoveGuide.gy}, z)), 1.5f, gc);
    }
    if (gPartR.axis >= 0) {   // turning on a ring: the angle, or what's being typed
        std::string t = gRotTyped.empty() ? std::string("(or type the angle)") : "Turn " + gRotTyped + "\xc2\xb0   (Enter)";
        Vector2 m = GetMousePosition();
        int tw = MeasureText(t.c_str(), 14) + 10, x = (int)m.x - tw - 12, y = (int)m.y - 30;
        DrawRectangle(x, y, tw, 20, Color{20, 24, 32, 200});
        DrawText(t.c_str(), x + 5, y + 3, 14, Color{255, 205, 60, 255});
    }
    bool after = gLastMove.on && A.lastSnap == gLastMove.after && A.tool == Tool::Select;
    if (!gMoveTyped.empty() && ((A.dragKind == 2 && A.dragLive) || after)) {
        std::string t = (A.dragKind == 2 ? "Move " : "Make the move ") + gMoveTyped + "   (Enter)";
        Vector2 m = GetMousePosition();
        int tw = MeasureText(t.c_str(), 16) + 10, x = (int)m.x - tw - 12, y = (int)m.y - 34;
        DrawRectangle(x, y, tw, 22, Color{20, 24, 32, 220});
        DrawText(t.c_str(), x + 5, y + 3, 16, Color{255, 205, 60, 255});
    } else if (A.dragKind == 2 && A.dragLive) {
        V2 dl = A.dragLast;
        std::string t = fmtQ(Q::Length, std::hypot(dl.x, dl.y)) + "   (or type a number)";
        Vector2 m = GetMousePosition();
        int tw = MeasureText(t.c_str(), 14) + 10, x = (int)m.x - tw - 12, y = (int)m.y - 30;
        DrawRectangle(x, y, tw, 20, Color{20, 24, 32, 200});
        DrawText(t.c_str(), x + 5, y + 3, 14, Color{255, 205, 60, 255});
    }
}
// hover help (0.9.2): when the mouse rests a moment over the view, what a click or drag will do there
void drawHoverHelp() {
    static Vector2 last{}; static double since = 0;
    Vector2 m = GetMousePosition();
    if (Vector2Distance(m, last) > 2 || IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) { last = m; since = GetTime(); return; }
    if (!CFG.hoverHelp || A.mode != Mode::Build || A.benchOnly || GetTime() - since < 0.7 || ImGui::GetIO().WantCaptureMouse || !mouseInViewport()) return;
    if (A.faceDragging || A.dragKind != 0 || A.drafting) return;
    std::string t;
    auto nm = [](int i) { const auto& b = A.design.bodies[i]; return b.name.empty() ? std::string("this part") : b.name; };
    if (A.tool == Tool::Faces) {
        const auto& h = A.faceHover;
        if (A.cornerHover.body >= 0) t = "Drag this corner on its own (squares are the front, circles the back): it shapes the part in depth";
        else if (h.body >= 0 && h.body < (int)A.design.bodies.size()) {
            std::string n = nm(h.body);
            if (h.kind == 0) t = "Drag: push or pull this side of " + n + ". Ctrl-drag pulls out a new piece. Type a number for an exact size";
            else if (h.kind == 1 || h.kind == 2) t = "Drag: make " + n + " thicker or thinner (this face moves). Ctrl-drag pulls out a new piece";
            else if (h.kind == 3) t = "Drag: make " + n + " longer or shorter from this end";
            else t = "Drag: make " + n + " thicker or thinner all round (it stays on its middle line)";
        } else {
            auto hits = bodiesUnderMouse();
            if (!hits.empty()) t = "Click to select " + nm(hits[0].body) + " (its faces can't be pushed: use the size numbers by it)";
        }
    } else if (A.tool == Tool::Select && !A.rotateMode) {
        auto hits = bodiesUnderMouse();
        if (!hits.empty()) {
            int i = hits[0].body;
            bool sel = A.sel.kind == 1 && weldedWith(A.design, A.sel.idx).size() && std::find(weldedWith(A.design, A.sel.idx).begin(), weldedWith(A.design, A.sel.idx).end(), i) != weldedWith(A.design, A.sel.idx).end();
            t = sel ? "Drag: move " + nm(i) + " (and what's welded to it). It snaps to other parts' edges and middles (Alt: free). Type a number while dragging for an exact distance"
                    : "Click to select " + nm(i) + ", then drag to move it";
        }
    }
    if (t.empty()) return;
    int fs = 14, tw = MeasureText(t.c_str(), fs) + 12;
    int x = (int)m.x + 18, y = (int)m.y + 20;
    if (x + tw > A.vw - 6) x = std::max(6, A.vw - 6 - tw);
    DrawRectangle(x, y, tw, fs + 10, Color{20, 24, 32, 215});
    DrawText(t.c_str(), x + 6, y + 5, fs, Color{225, 228, 235, 255});
}
static V2* gDemoW = nullptr;   // (GUNSMITH_SHAPEDEMO: where the next shape is dragged to)
bool moveTypingOpen() { return (A.dragKind == 2 && A.dragLive) || (A.tool == Tool::Select && gLastMove.on && A.lastSnap == gLastMove.after); }
// after a move, with Move: a typed number and Enter does it again, exactly that far the same way
static void moveTestHook() {   // GUNSMITH_MOVETEST: sweep the selected piece along the gun, print where it snaps; then a typed move
    static int frames = 0;
    if (!getenv("GUNSMITH_MOVETEST") || ++frames != 20 || A.sel.kind != 1) return;
    auto group = weldedWith(A.design, A.sel.idx);
    double prev = 1e9; int n = 0;
    for (double x = -40; x <= 40; x += 0.25) {
        V2 d = snapMove({x, 0}, group, {0, 0});
        if (gMoveGuide.x && std::fabs(d.x - prev) > 1e-6) { n++; if (n <= 12) printf("MOVETEST drag %.2f -> %.2f\n", x, d.x); prev = d.x; }
    }
    printf("MOVETEST %d snap spots in 80 mm\n", n);
    // a move, then 12.5 typed and Enter
    std::string before = A.lastSnap;
    std::vector<std::string> ids; for (int k : group) ids.push_back(A.design.bodies[k].id);
    V2 c0 = centroidOf(A.design.bodies[A.sel.idx]);
    for (int k : group) moveBody(k, 7, 0);
    A.nextLabel = "Moved test"; commit();
    gLastMove = {true, before, A.lastSnap, ids, {1, 0}, 7};
    gMoveTyped = "12.5";
    double v; parseQ(Q::Length, gMoveTyped.c_str(), v);
    Design back; designFromJson(gLastMove.before, back); A.design = back;
    if (!A.hist.empty() && A.hist.back() == gLastMove.before) { A.hist.pop_back(); A.histLabel.resize(A.hist.size()); }
    A.lastSnap = gLastMove.before;
    for (auto& id : ids) { int k = A.design.bodyIndex(id); if (k >= 0) moveBody(k, v, 0); }
    A.nextLabel = "Moved exactly"; commit(); gMoveTyped.clear();
    V2 c1 = centroidOf(A.design.bodies[A.sel.idx]);
    printf("MOVETEST typed 12.5: moved %.3f mm; history: %s (%zu steps)\n", c1.x - c0.x, A.histLabel.back().c_str(), A.hist.size());
}
bool moveTypedAfter() {
    moveTestHook();
    if (A.tool != Tool::Select || A.dragKind != 0) return false;
    if (!gLastMove.on || A.lastSnap != gLastMove.after) { gLastMove.on = false; gMoveTyped.clear(); return false; }
    if (!moveTypeKeys()) return false;
    double v;
    if (parseQ(Q::Length, gMoveTyped.c_str(), v)) {
        Design back;
        if (designFromJson(gLastMove.before, back)) {
            A.design = back;
            if (!A.hist.empty() && A.hist.back() == gLastMove.before) { A.hist.pop_back(); A.histLabel.resize(A.hist.size()); }
            A.lastSnap = gLastMove.before;
            for (auto& id : gLastMove.ids) { int k = A.design.bodyIndex(id); if (k >= 0) moveBody(k, gLastMove.dir.x * v, gLastMove.dir.y * v); }
            std::string nm = gLastMove.ids.empty() ? "" : A.design.bodies[std::max(0, A.design.bodyIndex(gLastMove.ids[0]))].name;
            A.nextLabel = "Moved " + nm + " " + fmtQ(Q::Length, v);
            std::string before = gLastMove.before;
            commit();
            gLastMove.after = A.lastSnap; gLastMove.dist = v; gLastMove.before = before;
            toast("Moved exactly " + fmtQ(Q::Length, v) + ".");
        }
    }
    gMoveTyped.clear();
    return true;
}

void buildInput(bool overUI) {
    {   // vertex selection belongs to one part: drop it when the selection moves elsewhere
        static std::string selVsPart;
        std::string cur = (A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size()) ? A.design.bodies[A.sel.idx].id : "";
        static bool started = false;   // (the first frame's selection isn't a change: corners picked at start stay picked)
        if (cur != selVsPart) { if (started) A.selVs.clear(); if (!selVsPart.empty()) { A.selCarve = -1; A.selMount = -1; } selVsPart = cur; }
        started = true;
        if (!cur.empty()) { int n = (int)A.design.bodies[A.sel.idx].pts.size(); A.selVs.erase(std::remove_if(A.selVs.begin(), A.selVs.end(), [n](int v) { return v < 0 || v >= n; }), A.selVs.end()); }
    }
    {   // GUNSMITH_SHAPEDEMO: a rounded block, a wedge, a slotted block and a tube above the gun, one a frame from frame 15
        static int frames = 0, step = 0;
        if (getenv("GUNSMITH_SHAPEDEMO") && ++frames >= 15 && step < 4) {
            struct S { Tool t; int kind; bool tube; V2 a, b; } demo[4] = {
                {Tool::Rect, 1, false, {-10, -70}, {30, -50}}, {Tool::Rect, 2, false, {40, -70}, {80, -50}},
                {Tool::Rect, 3, false, {90, -70}, {140, -50}}, {Tool::Circle, 0, true, {-10, -40}, {80, -30}}};
            auto& d = demo[step++];
            A.tool = d.t; A.blockKind = d.kind; A.circleLong = true; A.circleTube = d.tube;
            A.shaping = true; A.shapeStart = d.a; static V2 dw; dw = d.b; gDemoW = &dw;
            if (step == 4) A.circleTube = false;
        }
    }
    if (refInput(overUI)) return;   // (marking or moving a reference picture)
    if (moveTypedAfter()) return;   // (a number typed just after a move: made exact)
    if (!A.carveDrag && !A.carveMove && attachHandlesInput(overUI)) return;   // (an attachment's sight line and mount spot)
    if (A.tool == Tool::Mesh) { meshInput(overUI); return; }   // the 3D vertices tool
    if (A.tool == Tool::Carve || A.carveDrag || A.carveMove) { carveInput(overUI); return; }
    if (A.tool == Tool::Mount) { mountInput(overUI); return; }
    if (cutPickInput(overUI)) return;   // "Pick cuts in the view": a click on one of the part's cuts picks it
    if (A.tool == Tool::Engrave || A.engraving) { engraveInput(overUI); return; }
    if (A.tool == Tool::Scale) { scaleInput(overUI); return; }
    if (A.tool == Tool::Faces || (A.faceDragging && A.tool != Tool::Select)) { facesInput(overUI); return; }
    if (partRingsInput(overUI)) return;   // the selected part's rotation rings
    if (cornerArrowsInput(overUI)) return;   // the picked corners' move arrows
    if (selectFacesInput(overUI)) return;
    if (A.tool == Tool::Select && A.sel.kind == 1 && A.dragKind == 0 && IsKeyPressed(KEY_E)) { facesInput(overUI); if (A.faceDragging) return; }
    Vector2 m = GetMousePosition();
    V2 w = snapMM(mouseMM(0));
    if (gDemoW) { w = *gDemoW; gDemoW = nullptr; }
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
        case Tool::Rect: case Tool::Circle: case Tool::Knife: case Tool::Barrel: case Tool::Rail: A.shapeStart = w; A.shaping = true; return;
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
            if (hh.kind == 8 && hh.i >= 0 && hh.i < (int)A.design.bodies.size() && A.design.bodies[hh.i].roundPath.size() < 2)
                A.design.bodies[hh.i].roundPath = autoRoundPath(A.design.bodies[hh.i], A.design.breech.cart);   // moving an automatic layout: from here on it's yours
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
            } else if (hh.i >= 0 && hh.kind != 8) { A.sel = {2, hh.i, -1}; A.selVs.clear(); }   // (a round path point: its magazine stays selected)
            return;
        }
        auto hits = bodiesUnderMouse();  // nearest first
        if (A.sel.kind == 1 && A.faceHover.body == A.sel.idx && A.sel.idx >= 0) {   // a face of the selected part (any side: top, end, back) counts
            bool there = false;
            for (auto& h : hits) if (h.body == A.sel.idx) there = true;
            if (!there) hits.insert(hits.begin(), Hit{A.sel.idx, 0.f, w});
        }
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
                // on a face of the selected part that's picked: push it (Shift: extrude). Anywhere else on it: a drag
                // moves the part, a click picks the face there (Alt-click: the part behind it)
                const App::FacePick& fh = A.faceHover;
                const App::FacePick& fp = A.facePicked;
                if (fh.body == A.sel.idx && fh.body >= 0 && fp.body == fh.body && fp.kind == fh.kind && fp.edge == fh.edge) {
                    facePushStart(fh, shift);
                    A.dragKind = 0;
                    return;
                }
                A.pressFace = fh.body == A.sel.idx ? fh : App::FacePick{};
                A.dragKind = 2; A.dragStart = w; A.dragLast = {0, 0}; A.dragMoved = false; A.dragPress = m; A.dragLive = false;
                A.pressNext = hits.size() > 1 ? hits[(pos + 1) % hits.size()].body : -1;
            } else {
                A.sel = {1, hits[0].body, -1}; A.selVs.clear(); A.selCarve = -1;
                A.dragKind = 0;
                if (hits.size() > 1) toast("Selected " + A.design.bodies[hits[0].body].name + ". Alt-click it for the part behind it (" + std::to_string(hits.size()) + " here). Drag to move it.");
            }
            return;
        }
        if (gBlocked >= 0 && gBlocked < (int)A.design.bodies.size()) {   // a part was there, but its layer won't let it be picked: say so
            const BodyDef& bd = A.design.bodies[gBlocked];
            toast(bd.name + " is in the layer " + layerOf(A.design, std::max(0, bd.edLayer)).name + ", which is " + (partLocked(A.design, bd) ? "locked" : "faint") + ". Unlock it or show it fully in Layers to pick it.");
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
            c.id = uid(); c.name += " (mirror)"; c.magazine = false; c.weld.clear(); c.ghost = false;
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
                if (meshShaped(D.bodies[i])) {   // (the knife cuts outlines: a 3D mesh part's pieces are flat parts)
                    for (auto& pc : pcs) { pc.mesh = {}; pc.meshRef.clear(); }
                    toast("The 3D mesh part was cut as a flat outline: its pieces are ordinary parts now.");
                }
                std::string base = D.bodies[i].name;
                {   // "Frame 2" cut again gives "Frame 3", not "Frame 2 2"
                    size_t sp = base.find_last_of(' ');
                    if (sp != std::string::npos && sp + 1 < base.size() && base.find_first_not_of("0123456789", sp + 1) == std::string::npos) base = base.substr(0, sp);
                }
                auto taken = [&](const std::string& nm) { for (auto& o : D.bodies) if (o.name == nm) return true; return false; };
                D.bodies[i] = pcs[0];
                for (size_t k = 1; k < pcs.size(); k++) {
                    BodyDef nb = pcs[k];
                    int num = 2;
                    while (taken(base + " " + std::to_string(num))) num++;
                    nb.id = uid(); nb.name = base + " " + std::to_string(num); nb.magazine = false; nb.ctrl = "none"; nb.weld.clear();
                    nb.ghost = false;   // (a new piece starts solid: see-through is only for parts you mark)
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
        if (A.tool == Tool::Rail) {
            if (std::hypot(w.x - a.x, w.y - a.y) >= 10) {
                // the part it stands on: the one just under the middle of the line drawn (else the one the line runs through)
                BodyDef probe = mounts::makeRail(a, w, 0, 0);
                V2 mid{(a.x + w.x) / 2, (a.y + w.y) / 2}, inward{probe.pts[0].x - probe.pts[3].x, probe.pts[0].y - probe.pts[3].y};   // (away from the rail: into what it stands on)
                double il = std::hypot(inward.x, inward.y); if (il > 0) { inward.x /= il; inward.y /= il; }
                int base = -1;
                for (double off : {1.5, 0.0, 4.0})
                    for (size_t k = 0; k < A.design.bodies.size() && base < 0; k++) {
                        const BodyDef& d = A.design.bodies[k];
                        if (d.rail.on || d.magazine || d.ghost) continue;
                        if (sim::inPoly(mid.x + inward.x * off, mid.y + inward.y * off, d.pts)) base = (int)k;
                    }
                BodyDef r = mounts::makeRail(a, w, 0, base >= 0 ? A.design.bodies[base].z : 0);
                r.id = uid();
                int n = 0; for (auto& d : A.design.bodies) if (d.rail.on) n++;
                if (n) r.name = "Picatinny rail " + std::to_string(n + 1);
                std::string baseId;
                if (base >= 0) { r.fixed = A.design.bodies[base].fixed; baseId = A.design.bodies[base].id; r.rail.base = baseId; }
                else r.fixed = true;
                A.design.bodies.push_back(r);
                if (!baseId.empty()) weldParts(A.design, r.id, baseId);
                A.sel = {1, (int)A.design.bodies.size() - 1, -1};
                commit();
                toast(base >= 0 ? TextFormat("Picatinny rail: %d slots, welded to %s. Put it on a side or underneath in its panel.", (int)mounts::railSlots(r).size(), A.design.bodies[base].name.c_str())
                                : "Picatinny rail drawn, on nothing: draw it along the top edge of a part to weld it there.");
            } else toast("Drag along the top of a part, as long as the rail should be.");
            A.tool = Tool::Select;
            return;
        }
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
            if (x1 - x0 >= 1 && y1 - y0 >= 1) {
                double L = x1 - x0, H = y1 - y0;
                b.pts = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
                if (A.blockKind == 1) { b.rad.assign(4, std::round(std::min(L, H) * 0.25 * 2) / 2); b.name = "Rounded block " + std::to_string(A.design.bodies.size() + 1); }
                if (A.blockKind == 2) { b.pts = {{x0, y0 + H * 0.6}, {x1, y0}, {x1, y1}, {x0, y1}}; b.name = "Wedge " + std::to_string(A.design.bodies.size() + 1); }
                if (A.blockKind == 3) {   // a slot right through it, side to side, in the middle
                    CarveDef cv; cv.shape = "rect"; cv.at = {(x0 + x1) / 2, (y0 + y1) / 2}; cv.atZ = b.thick / 2; cv.dx = 0; cv.dy = 0; cv.dz = -1;
                    cv.w = std::round(L * 0.6 * 2) / 2; cv.h = std::max(1.0, std::round(H * 0.3 * 2) / 2); cv.depth = 0; cv.name = "Slot";
                    b.carves.push_back(cv); b.name = "Slotted block " + std::to_string(A.design.bodies.size() + 1);
                }
            }
        } else if (A.circleLong) {   // lengthwise: drag out its side view; it's round along the longer side
            double x0 = std::min(a.x, w.x), x1 = std::max(a.x, w.x), y0 = std::min(a.y, w.y), y1 = std::max(a.y, w.y);
            if (x1 - x0 >= 1 && y1 - y0 >= 1) {
                b.pts = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
                b.round = true;
                b.roundAxis = x1 - x0 >= y1 - y0 ? 0 : 1;
                b.thick = std::min(x1 - x0, y1 - y0);
                b.name = "Rod " + std::to_string(A.design.bodies.size() + 1);
                if (A.circleTube) {   // a hole down its middle, end to end, 60% across
                    bool lev = b.roundAxis == 0;
                    CarveDef cv; cv.shape = "circle"; cv.at = lev ? V2{x0, (y0 + y1) / 2} : V2{(x0 + x1) / 2, y0};
                    cv.atZ = 0; cv.dx = lev ? 1 : 0; cv.dy = lev ? 0 : 1; cv.dz = 0;
                    cv.w = cv.h = std::max(0.5, std::round(b.thick * 0.6 * 2) / 2); cv.depth = 0; cv.name = "Bore";
                    b.carves.push_back(cv); b.name = "Tube " + std::to_string(A.design.bodies.size() + 1);
                }
            }
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
        A.tool = Tool::Faces;   // (then push and pull it into shape)
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
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !gMoveEnter) {
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
                auto group = weldedWith(A.design, A.sel.idx);
                bool enter = moveTypeKeys();
                double tv;
                if (!gMoveTyped.empty() && parseQ(Q::Length, gMoveTyped.c_str(), tv)) {   // a typed distance, the way the mouse went
                    double dl = std::hypot(w.x - A.dragStart.x, w.y - A.dragStart.y);
                    V2 u = dl > 1e-9 ? V2{(w.x - A.dragStart.x) / dl, (w.y - A.dragStart.y) / dl} : V2{1, 0};
                    if (A.axisLock == 1) u = {u.x >= 0 ? 1.0 : -1.0, 0};
                    if (A.axisLock == 2) u = {0, u.y >= 0 ? 1.0 : -1.0};
                    d = {u.x * tv, u.y * tv};
                    gMoveGuide = {};
                } else d = snapMove(d, group, A.dragLast);
                if (A.axisLock == 1) d.y = 0;
                if (A.axisLock == 2) d.x = 0;
                if (enter) gMoveEnter = true;
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
                else if (h.kind == 2 && h.v == 0 && A.design.joints[h.i].type == "magwell") {   // the seat: the magazine moves, its well with it
                    auto& j = A.design.joints[h.i];
                    double dx = p.x - j.path[0].x, dy = p.y - j.path[0].y;
                    int mi = A.design.bodyIndex(j.a);
                    if (mi >= 0 && (dx != 0 || dy != 0)) for (int k : weldedWith(A.design, mi)) moveBody(k, dx, dy);
                }
                else if (h.kind == 2) A.design.joints[h.i].path[h.v] = p;
                else if (h.kind == 8 && h.i < (int)A.design.bodies.size() && h.v < (int)A.design.bodies[h.i].roundPath.size()) A.design.bodies[h.i].roundPath[h.v] = p;
                else if (h.kind == 3) A.design.joints[h.i].pa = p;
                else if (h.kind == 4) A.design.joints[h.i].pb = p;
                else if (h.kind == 5) { if (h.i >= 0 && h.i < (int)br.more.size()) br.more[h.i].face = p; else br.face = p; }
                else if (h.kind == 6) { if (h.i >= 0 && h.i < (int)br.more.size()) br.more[h.i].muzzle = p; else br.muzzle = p; }
                if (h.kind != 0 && h.kind != 7) A.dragMoved = true;   // (kind 8: a magazine's round path point)
            }
        } else {
            if (A.dragMoved && A.dragKind == 2) {   // remember the move, so a typed number can make it exact
                std::string before = A.lastSnap;
                std::vector<std::string> ids;
                for (int k : weldedWith(A.design, A.sel.idx)) ids.push_back(A.design.bodies[k].id);
                double dl = std::hypot(A.dragLast.x, A.dragLast.y);
                A.nextLabel = "Moved " + (A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size() ? A.design.bodies[A.sel.idx].name : std::string("a part")) + " " + fmtQ(Q::Length, dl);
                commit();
                gLastMove = {dl > 1e-9, before, A.lastSnap, ids, dl > 1e-9 ? V2{A.dragLast.x / dl, A.dragLast.y / dl} : V2{1, 0}, dl};
                gMoveTyped.clear(); gMoveGuide = {};
            }
            else if (A.dragMoved) commit();
            else if (A.dragKind == 2 && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT) || A.pressFace.body < 0) &&
                     A.pressNext >= 0 && A.pressNext < (int)A.design.bodies.size()) {  // an Alt-click (or not on a face): the next one behind
                A.sel = {1, A.pressNext, -1}; A.selVs.clear(); A.selCarve = -1;
            } else if (A.dragKind == 2 && A.pressFace.body >= 0) {   // a click on a face of the selected part: it's picked
                A.facePicked = A.pressFace;
                static bool told = false;
                if (!told) { told = true; toast("Face picked. Press it again and drag to push or pull it (Shift: extrude a new step)."); }
            }
            A.pressNext = -1;
            A.dragKind = 0; A.axisLock = 0; gMoveEnter = false;
        }
    }
}
