// Gunsmith3D - the pose editor (see pose.hpp).
#include "app/pose.hpp"
#include "app/app.hpp"
#include "app/fonts.hpp"
#include "app/gfx.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "raymath.h"

#include <algorithm>
#include <cmath>

Ray mouseRay();  // (view.cpp)

// ---------------------------------------------------------------- the body
// Body frame, millimetres: forward, up, right, from the aiming (right) eye. A grown man, roughly.
namespace {
constexpr double UPPER_ARM = 320, FOREARM = 290;
struct Frame { Vector3 o, f, u, r; };
const Color SKIN{212, 168, 138, 255}, BODY{140, 146, 156, 255}, STRAIN{224, 92, 78, 255};

BodyHold cur() { return holdOf(A.design, A.poseHold); }
BodyHold& editable() {
    BodyHold& h = A.design.pose.hold[A.poseHold];
    if (!h.set) { h = holdOf(A.design, A.poseHold); h.set = true; }
    return h;
}
Vector3 eyeWorld(const BodyHold& h) { return W3(h.eye, (float)h.eyeZ * U); }
Frame bodyFrame() {
    BodyHold h = cur();
    double y = h.yaw * DEG2RAD;
    return {eyeWorld(h), {(float)std::cos(y), 0, (float)std::sin(y)}, {0, 1, 0}, {-(float)std::sin(y), 0, (float)std::cos(y)}};
}
Vector3 bp(const Frame& F, double f, double u, double r) {
    return Vector3Add(F.o, Vector3Scale(Vector3Add(Vector3Add(Vector3Scale(F.f, (float)f), Vector3Scale(F.u, (float)u)), Vector3Scale(F.r, (float)r)), U));
}
Vector3 gunPt(V2 mm, double zmm) { return Vector3Transform(W3(mm, (float)zmm * U), poseGunMatrix()); }
double partZ(const std::string& id) { int i = A.design.bodyIndex(id); return i >= 0 ? A.design.bodies[i].z : 0; }

// where the wrist is for a hand round this grip line: off the side of the line away from the gun (behind a pistol
// grip, under a fore-end)
V2 wristOff(double angleDeg) {
    double a = angleDeg * DEG2RAD;
    V2 n1{-std::sin(a), std::cos(a)}, n2{std::sin(a), -std::cos(a)};
    auto score = [](V2 n) { return -0.5 * n.x + n.y; };
    return score(n1) >= score(n2) ? n1 : n2;
}

struct Arm { Vector3 s, e, w; double shortBy = 0; };
Arm reach(Vector3 s, Vector3 target, Vector3 pole) {
    Arm A_{s, s, target, 0};
    float L1 = (float)UPPER_ARM * U, L2 = (float)FOREARM * U;
    Vector3 d = Vector3Subtract(target, s);
    float dist = Vector3Length(d);
    if (dist < 1e-4f) return A_;
    Vector3 dir = Vector3Scale(d, 1 / dist);
    if (dist > L1 + L2 - 1e-3f) { A_.shortBy = (dist - (L1 + L2)) / U; dist = L1 + L2 - 1e-3f; A_.w = Vector3Add(s, Vector3Scale(dir, dist)); }
    float a = (L1 * L1 - L2 * L2 + dist * dist) / (2 * dist), h = std::sqrt(std::max(0.f, L1 * L1 - a * a));
    Vector3 p = Vector3Subtract(pole, Vector3Scale(dir, Vector3DotProduct(pole, dir)));
    if (Vector3Length(p) < 1e-5f) p = {0, -1, 0};
    p = Vector3Normalize(p);
    A_.e = Vector3Add(Vector3Add(s, Vector3Scale(dir, a)), Vector3Scale(p, h));
    return A_;
}
struct HandGeo { bool on = false; Vector3 a, b, c, wrist; };
HandGeo handGeo(const HandHold& h, int side) {
    HandGeo g;
    if (!h.on) return g;
    double z = partZ(h.part), an = h.angle * DEG2RAD;
    V2 d{std::cos(an), std::sin(an)}, n = wristOff(h.angle);
    g.on = true;
    g.a = gunPt({h.at.x - d.x * 42, h.at.y - d.y * 42}, z);
    g.b = gunPt({h.at.x + d.x * 42, h.at.y + d.y * 42}, z);
    g.c = gunPt(h.at, z);
    g.wrist = gunPt({h.at.x + n.x * 45, h.at.y + n.y * 45}, z + side * 22);
    return g;
}
void capsule(Vector3 a, Vector3 b, float r, Color c) { drawCyl(a, b, r, c); drawSphereLit(a, r, c); drawSphereLit(b, r, c); }
}  // namespace

Matrix poseGunMatrix() {  // the muzzle drops about the butt (in the shoulder), or the firing hand, or the eye
    BodyHold h = cur();
    const Pose& P = A.design.pose;
    Vector3 e = P.butt.on ? W3(P.butt.at, (float)partZ(P.butt.part) * U) : P.firing.on ? W3(P.firing.at, (float)partZ(P.firing.part) * U) : eyeWorld(h);
    return MatrixMultiply(MatrixMultiply(MatrixTranslate(-e.x, -e.y, -e.z), MatrixRotateZ((float)(-h.tilt * DEG2RAD))), MatrixTranslate(e.x, e.y, e.z));
}

void poseDefaultHands() {
    auto& P = A.design.pose;
    if (P.firing.on) return;
    const auto& br = A.design.breech;
    V2 c{0, 0};
    if (br.on && br.hasFace) c = {br.face.x - 40, br.face.y + 45};
    else if (!A.design.bodies.empty()) c = centroidOf(A.design.bodies[0]);
    P.firing.on = true; P.firing.at = c; P.firing.angle = defaultHandAngle("firing");
    for (int i = (int)A.design.bodies.size() - 1; i >= 0; i--)
        if (sim::inPoly(c.x, c.y, A.design.bodies[i].pts)) { P.firing.part = A.design.bodies[i].id; break; }
    if (P.firing.part.empty() && !A.design.bodies.empty()) P.firing.part = A.design.bodies[0].id;
}

void poseView() {
    Frame F = bodyFrame();
    Vector3 chest = bp(F, -60, -220, -32);
    A.target = Vector3Lerp(chest, W3({A.design.pose.firing.at.x, A.design.pose.firing.at.y}, 0), 0.35f);
    A.ortho = false;
    A.dist = 1900 * U;
    A.yaw = 0.95f; A.pitch = 0.22f;
}

void enterPose() {
    A.posing = true;
    A.running = false;
    poseDefaultHands();
    poseView();
}
void leavePose() { A.posing = false; A.poseDrag = -1; fitView(); }

static Arm armTo(const Frame& F, const HandGeo& g, bool right) {
    Vector3 s = right ? bp(F, -140, -215, 153) : bp(F, -140, -215, -217);
    Vector3 pole = Vector3Add(Vector3Scale(F.u, -1), Vector3Add(Vector3Scale(F.r, right ? 0.7f : -0.7f), Vector3Scale(F.f, -0.3f)));
    if (!g.on) {  // a free arm hangs at the side
        Arm a{s, bp(F, -150, -500, right ? 175 : -239), bp(F, -110, -770, right ? 175 : -239), 0};
        return a;
    }
    return reach(s, g.wrist, pole);
}

PoseReport poseReport() {
    PoseReport R;
    Frame F = bodyFrame();
    const Pose& P = A.design.pose;
    R.rightShort = armTo(F, handGeo(P.firing, 1), true).shortBy;
    R.leftShort = armTo(F, handGeo(P.support, -1), false).shortBy;
    if (P.butt.on) {
        Vector3 pocket = bp(F, -90, -120, 25), b = gunPt(P.butt.at, partZ(P.butt.part));
        R.buttOff = Vector3Distance(pocket, b) / U;
        R.buttIn = R.buttOff < 60;
    }
    return R;
}

void drawPoseBody() {
    Frame F = bodyFrame();
    const Pose& P = A.design.pose;
    // head, neck, shoulders, chest
    drawSphereLit(bp(F, -82, 15, -32), 86 * U, BODY);
    capsule(bp(F, -95, -80, -32), bp(F, -120, -175, -32), 52 * U, BODY);
    Vector3 sR = bp(F, -140, -215, 153), sL = bp(F, -140, -215, -217);
    capsule(sR, sL, 62 * U, BODY);
    capsule(bp(F, -150, -240, 50), bp(F, -150, -560, 40), 105 * U, BODY);
    capsule(bp(F, -150, -240, -114), bp(F, -150, -560, -104), 105 * U, BODY);
    // the shoulder pocket, where a butt beds in
    if (P.butt.on) {
        PoseReport R = poseReport();
        drawSphereLit(bp(F, -90, -120, 25), 30 * U, R.buttIn ? Color{110, 230, 140, 255} : Color{232, 161, 58, 255}, 0.5f);
    }
    // arms and hands
    for (int side = 0; side < 2; side++) {
        bool right = side == 0;
        HandGeo g = handGeo(right ? P.firing : P.support, right ? 1 : -1);
        Arm a = armTo(F, g, right);
        Color c = a.shortBy > 0.5 ? STRAIN : BODY;
        capsule(a.s, a.e, 46 * U, c);
        capsule(a.e, a.w, 38 * U, c);
        if (g.on) {
            capsule(g.a, g.b, 21 * U, SKIN);
            drawCyl(a.w, g.c, 26 * U, SKIN);
        } else drawSphereLit(a.w, 45 * U, SKIN);
    }
}

// ---------------------------------------------------------------- handles
namespace {
struct PH { Vector2 s; int id; };  // 0/1 firing middle/end, 2/3 support, 4/5 butt, 6 the head
std::vector<PH> handles;
}
void drawPoseOverlay() {
    handles.clear();
    const Pose& P = A.design.pose;
    Vector2 m = GetMousePosition();
    auto dot = [&](Vector2 s, int id, Color c, const char* label) {
        bool hot = Vector2Distance(s, m) < 10 || A.poseDrag == id;
        DrawCircleV(s, hot ? 7.f : 5.f, Color{20, 24, 32, 230});
        DrawCircleLinesV(s, hot ? 7.f : 5.f, c);
        if (label) viewText(label, s.x + 10, s.y - 18 * CFG.uiScale, 12, c);
        handles.push_back({s, id});
    };
    const HandHold* hs[3] = {&P.firing, &P.support, &P.butt};
    const char* names[3] = {"Firing hand", "Support hand", "Butt"};
    for (int k = 0; k < 3; k++) {
        const HandHold& h = *hs[k];
        if (!h.on) continue;
        double z = partZ(h.part), an = h.angle * DEG2RAD, half = k == 2 ? 60 : 42;
        Vector2 a = toScreen(gunPt({h.at.x - std::cos(an) * half, h.at.y - std::sin(an) * half}, z));
        Vector2 b = toScreen(gunPt({h.at.x + std::cos(an) * half, h.at.y + std::sin(an) * half}, z));
        Color c{110, 230, 140, 255};
        DrawLineEx(a, b, 2.5f, Color{c.r, c.g, c.b, 160});
        dot(toScreen(gunPt(h.at, z)), k * 2, c, names[k]);
        dot(b, k * 2 + 1, c, nullptr);
    }
    Frame F = bodyFrame();
    dot(toScreen(bp(F, -75, 110, -32)), 6, Color{232, 161, 58, 255}, "Drag to move the body");
}

void poseInput(bool overUI) {
    auto& P = A.design.pose;
    if (A.poseDrag < 0) {
        if (overUI || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        Vector2 m = GetMousePosition();
        float best = 12;
        for (auto& h : handles) { float d = Vector2Distance(h.s, m); if (d < best) { best = d; A.poseDrag = h.id; } }
        return;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { A.poseDrag = -1; commit(); return; }
    Ray r = mouseRay();
    auto onPlane = [&](float z, Vector3& out) {
        if (std::fabs(r.direction.z) < 1e-6f) return false;
        float t = (z - r.position.z) / r.direction.z;
        if (t < 0) return false;
        out = Vector3Add(r.position, Vector3Scale(r.direction, t));
        return true;
    };
    Matrix inv = MatrixInvert(poseGunMatrix());
    if (A.poseDrag == 6) {  // the body: the eye follows the cursor across the gun's side
        BodyHold& h = editable();
        Vector3 p;
        if (!onPlane((float)h.eyeZ * U, p)) return;
        Vector3 e = eyeWorld(h), d = Vector3Subtract(p, e);
        Vector3 g = Vector3Transform(d, MatrixRotateZ((float)(h.tilt * DEG2RAD)));  // back into the gun's frame
        h.eye.x += g.x / U; h.eye.y -= g.y / U;
        return;
    }
    HandHold& h = A.poseDrag < 2 ? P.firing : A.poseDrag < 4 ? P.support : P.butt;
    Vector3 p;
    if (!onPlane((float)partZ(h.part) * U, p)) return;
    Vector3 q = Vector3Transform(p, inv);
    V2 mm{q.x / U, -q.y / U};
    if (A.poseDrag % 2 == 0) {  // move it, onto whichever part is there
        h.at = mm;
        for (int i = (int)A.design.bodies.size() - 1; i >= 0; i--)
            if (sim::inPoly(mm.x, mm.y, A.design.bodies[i].pts)) { h.part = A.design.bodies[i].id; break; }
    } else {                     // turn it
        double a = std::atan2(mm.y - h.at.y, mm.x - h.at.x) * RAD2DEG;
        h.angle = std::round(a < 0 ? a + 360 : a);
    }
}
