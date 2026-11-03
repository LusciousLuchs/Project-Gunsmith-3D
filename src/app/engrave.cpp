// Gunsmith3D - the Engrave tool: an engraving pen for drawing lines and writing text on any part's surface. Looks only:
// engravings never change how a part moves or collides. They go with their part (see followMarks).
#include "app/engrave.hpp"
#include "app/app.hpp"
#include "app/carvetool.hpp"
#include "app/view.hpp"
#include "app/gfx.hpp"
#include "app/render.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

namespace {
// a surface point the mouse is over, on the selected part (or any part: `any`), in design mm
bool surfaceAt(int& body, P3d& p, P3d& n) {
    Vector3 wp, wn;
    if (!carvePick(body, wp, wn)) return false;
    const BodyDef& d = A.design.bodies[body];
    p = {wp.x / U, -wp.y / U, (wp.z - bodyZ(body)) / U + d.z};
    n = {wn.x, -wn.y, wn.z};
    return true;
}
double dist(P3d a, P3d b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }
bool selectedPart() { return A.sel.kind == 1 && A.sel.idx >= 0 && A.sel.idx < (int)A.design.bodies.size(); }
void finishLine() {
    if (A.engraving && A.engraveBody >= 0 && A.engraveBody < (int)A.design.bodies.size() && A.engraveCur.pts.size() >= 2) {
        auto& b = A.design.bodies[A.engraveBody];
        if (b.marks.empty()) { b.markRef = b.pts; b.markRefZ = b.z; b.markRefThick = b.thick; }
        b.marks.push_back(A.engraveCur);
    }
    A.engraveCur = MarkDef();
    A.engraving = false;
}
MarkDef textMark(P3d p, P3d n) {
    MarkDef m;
    m.pts = {p}; m.n = {n};
    m.text = A.engraveText; m.size = A.engraveSize; m.width = A.engraveWidth; m.color = A.engraveColor;
    m.right = textRightFor(n, A.engraveTurn);
    return m;
}
// the design mm -> world mapping of part i's own drawing frame (its mesh's), and how the part sits now
Vector3 local(const BodyDef& d, V2 c, P3d p) { return {(float)(p.x - c.x) * U, -(float)(p.y - c.y) * U, (float)(p.z - d.z) * U}; }
}  // namespace

void engraveInput(bool overUI) {
    if (A.engraving) {   // drawing a line: follow the pen while it's on the part
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { finishLine(); commit(); return; }
        int i; P3d p, n;
        bool on = surfaceAt(i, p, n) && i == A.engraveBody;
        if (!on) {   // off the part: this line ends; another starts where the pen comes back on
            if (A.engraveCur.pts.size() >= 2) { int keep = A.engraveBody; finishLine(); A.engraving = true; A.engraveBody = keep; }
            else A.engraveCur = MarkDef();
            return;
        }
        if (A.engraveCur.pts.empty()) { A.engraveCur.width = A.engraveWidth; A.engraveCur.color = A.engraveColor; }
        if (A.engraveCur.pts.empty() || dist(p, A.engraveCur.pts.back()) > std::max(0.25, A.engraveWidth * 0.6)) {
            A.engraveCur.pts.push_back(p); A.engraveCur.n.push_back(n);
        }
        return;
    }
    if (overUI || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    int i; P3d p, n;
    if (!surfaceAt(i, p, n)) { toast("Click on a part to engrave it."); return; }
    if (!(selectedPart() && A.sel.idx == i)) {   // the first click picks the part
        A.sel = {1, i, -1}; A.selVs.clear();
        toast(A.engraveMode == 1 ? "Picked. Now click where the text goes." : A.engraveMode == 2 ? "Picked. Now click a line or a word to erase it." : "Picked. Now draw on it.");
        return;
    }
    auto& b = A.design.bodies[i];
    if (A.engraveMode == 0) {
        A.engraving = true; A.engraveBody = i;
        A.engraveCur = MarkDef(); A.engraveCur.width = A.engraveWidth; A.engraveCur.color = A.engraveColor;
        A.engraveCur.pts = {p}; A.engraveCur.n = {n};
    } else if (A.engraveMode == 1) {
        if (A.engraveText.empty()) { toast("Type the text in the panel first."); return; }
        if (b.marks.empty()) { b.markRef = b.pts; b.markRefZ = b.z; b.markRefThick = b.thick; }
        b.marks.push_back(textMark(p, n));
        commit();
    } else {   // erase: the line or word nearest the click, on screen
        Vector2 m = GetMousePosition();
        int best = -1; float bd = 10;
        for (int k = 0; k < (int)b.marks.size(); k++)
            for (auto& l : markLines(b.marks[k]))
                for (size_t q = 0; q + 1 < l.size(); q++) {
                    Vector2 a = toScreen(W3({l[q].p.x, l[q].p.y}, bodyZ(i) + (float)(l[q].p.z - b.z) * U));
                    Vector2 e = toScreen(W3({l[q + 1].p.x, l[q + 1].p.y}, bodyZ(i) + (float)(l[q + 1].p.z - b.z) * U));
                    Vector2 ab = Vector2Subtract(e, a);
                    float t = Vector2DotProduct(ab, ab) > 0 ? std::clamp(Vector2DotProduct(Vector2Subtract(m, a), ab) / Vector2DotProduct(ab, ab), 0.f, 1.f) : 0;
                    float dd = Vector2Distance(m, Vector2Add(a, Vector2Scale(ab, t)));
                    if (dd < bd) { bd = dd; best = k; }
                }
        if (best < 0) { toast("Nothing engraved there."); return; }
        b.marks.erase(b.marks.begin() + best);
        commit();
    }
}

// lines as thin strips lying on the surface (a hair above it, so they show)
static void strip(const std::vector<MarkPt>& l, const BodyDef& d, V2 c, Matrix M, float width, Color col) {
    for (size_t q = 0; q + 1 < l.size(); q++) {
        Vector3 a = Vector3Transform(local(d, c, l[q].p), M), b = Vector3Transform(local(d, c, l[q + 1].p), M);
        P3d nd = l[q].n;
        Vector3 nl = {(float)nd.x, -(float)nd.y, (float)nd.z};
        Vector3 n = Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Add(local(d, c, l[q].p), nl), M), a));
        Vector3 dir = Vector3Subtract(b, a);
        if (Vector3Length(dir) < 1e-7f) continue;
        Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(n, dir)), width * U / 2);
        Vector3 lift = Vector3Scale(n, 0.08f * U);
        Vector3 a1 = Vector3Add(Vector3Add(a, side), lift), a2 = Vector3Add(Vector3Subtract(a, side), lift);
        Vector3 b1 = Vector3Add(Vector3Add(b, side), lift), b2 = Vector3Add(Vector3Subtract(b, side), lift);
        DrawTriangle3D(a1, b1, b2, col); DrawTriangle3D(a1, b2, b1, col);
        DrawTriangle3D(a1, b2, a2, col); DrawTriangle3D(a1, a2, b2, col);
    }
}
void drawMarks(int tmpl, Matrix M) {
    const BodyDef& d = A.design.bodies[tmpl];
    if (d.marks.empty() && !(A.engraving && A.engraveBody == tmpl)) return;
    V2 c = centroidOf(d);
    for (auto& m : d.marks)
        for (auto& l : markLines(m)) strip(l, d, c, M, (float)m.width, hexColor(m.color));
    if (A.engraving && A.engraveBody == tmpl)
        for (auto& l : markLines(A.engraveCur)) strip(l, d, c, M, (float)A.engraveCur.width, hexColor(A.engraveCur.color));
}
void drawEngravePreview() {   // Text: the words, faint, where a click would put them
    if (A.tool != Tool::Engrave || A.mode != Mode::Build || A.engraveMode != 1 || A.engraveText.empty() || !selectedPart()) return;
    int i; P3d p, n;
    if (!surfaceAt(i, p, n) || i != A.sel.idx) return;
    const BodyDef& d = A.design.bodies[i];
    V2 c = centroidOf(d);
    Matrix M = MatrixTranslate((float)c.x * U, -(float)c.y * U, bodyZ(i));
    MarkDef m = textMark(p, n);
    Color col = hexColor(m.color); col.a = 140;
    for (auto& l : markLines(m)) strip(l, d, c, M, (float)m.width, col);
}
