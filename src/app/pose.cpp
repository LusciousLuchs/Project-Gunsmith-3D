// Gunsmith3D - the pose editor (see pose.hpp).
#include "app/pose.hpp"
#include "app/app.hpp"
#include "app/fonts.hpp"
#include "app/gfx.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "raymath.h"
#include "hand.hpp"

#include <algorithm>
#include <cmath>

Ray mouseRay();  // (view.cpp)

// The pose as shown: the hold's hands, or an animation's where it has got to (playing on the gun, or on the
// timeline being edited). The butt and the body always stay as posed.
Pose viewPose() {
    Pose P = A.design.pose;
    if (A.S && sim::animPlaying(*A.S)) {
        AnimHands h = sim::animHandsNow(*A.S);
        P.firing = h.firing; P.support = h.support;
    } else if (A.posing && A.animEdit) {
        auto it = A.design.anims.find(A.animSlot);
        if (it != A.design.anims.end()) { AnimHands h = animAt(A.design, it->second, A.animT); P.firing = h.firing; P.support = h.support; }
    }
    return P;
}
// the gun as an animation moves it from the hold (playing on the gun, or on the timeline being edited); false: not moved
bool viewGun(double g[6]) {
    for (int q = 0; q < 6; q++) g[q] = 0;
    AnimHands h;
    if (A.S && sim::animPlaying(*A.S)) h = sim::animHandsNow(*A.S);
    else if (A.posing && A.animEdit) {
        auto it = A.design.anims.find(A.animSlot);
        if (it == A.design.anims.end()) return false;
        h = animAt(A.design, it->second, A.animT);
    } else return false;
    for (int q = 0; q < 6; q++) g[q] = h.gun[q];
    return gunMoved(g);
}
// the move as a matrix in the gun's drawing frame: turned about `pivot` (drawing units), then shifted
Matrix gunMoveMatrix(const double g[6], Vector3 pivot) {
    Matrix R = MatrixMultiply(MatrixMultiply(MatrixRotateX((float)(g[5] * DEG2RAD)), MatrixRotateZ((float)(g[3] * DEG2RAD))), MatrixRotateY((float)(-g[4] * DEG2RAD)));
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-pivot.x, -pivot.y, -pivot.z), R), MatrixTranslate(pivot.x, pivot.y, pivot.z)),
                          MatrixTranslate((float)g[0] * U, (float)g[1] * U, (float)g[2] * U));
}
int animKeyAt(const Animation& a, double t) {   // the key at this moment on the timeline, or -1
    for (int i = 0; i < (int)a.keys.size(); i++) if (std::fabs(a.keys[i].t - t) < 0.03) return i;
    return -1;
}
// The hand that dragging edits: the hold's, or (editing an animation) the key's at the timeline's moment, made there
// from where the hands are then if there's no key yet. 0 firing, 1 support, 2 butt.
HandHold* editHand(int which) {
    auto& P = A.design.pose;
    if (which == 2 || !A.animEdit) return which == 0 ? &P.firing : which == 1 ? &P.support : &P.butt;
    Animation& a = A.design.anims[A.animSlot];
    int k = animKeyAt(a, A.animT);
    if (k < 0) {
        AnimHands h = animAt(A.design, a, A.animT);
        AnimKey nk; nk.t = A.animT; nk.firing = h.firing; nk.support = h.support; nk.gripF = h.gripF; nk.gripS = h.gripS;
        for (int q = 0; q < 6; q++) nk.gun[q] = h.gun[q];
        auto pos = std::upper_bound(a.keys.begin(), a.keys.end(), nk, [](const AnimKey& x, const AnimKey& y) { return x.t < y.t; });
        k = (int)(pos - a.keys.begin());
        a.keys.insert(pos, nk);
    }
    return which == 0 ? &a.keys[k].firing : &a.keys[k].support;
}

// ---------------------------------------------------------------- the body
// Body frame, millimetres: forward, up, right, from the aiming (right) eye. A grown man, roughly.
namespace {
constexpr double UPPER_ARM = 320, FOREARM = 290;
struct Frame { Vector3 o, f, u, r; };
const Color SKIN{212, 168, 138, 255}, SHIRT{72, 86, 78, 255}, MUFF{44, 48, 52, 255}, MUFF2{196, 120, 46, 255}, STRAIN{224, 92, 78, 255};

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
Vector3 pocketPt(const Frame& F) { BodyHold h = cur(); return bp(F, h.pocket[0], h.pocket[1], h.pocket[2]); }
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
// The hand's frame on the gun (gun millimetres, 3D): the palm against the side of whatever is held there, the knuckles
// along the grip line (index finger at its upper / forward end), the fingers reaching across, curling over the far side.
struct HandFrame { bool on = false; HV o, X, Y, Z; };
void gunSpan(V2 at, const std::string& part, double& zc, double& half) {  // how wide the gun is where the hand holds it
    double z0 = 1e9, z1 = -1e9;
    for (auto& b : A.design.bodies)
        if (!b.magazine && sim::inPoly(at.x, at.y, b.pts)) { z0 = std::min(z0, b.z - b.thick / 2); z1 = std::max(z1, b.z + b.thick / 2); }
    if (z0 > z1) { int i = A.design.bodyIndex(part); double t = i >= 0 ? A.design.bodies[i].thick : 20; z0 = partZ(part) - t / 2; z1 = partZ(part) + t / 2; }
    zc = (z0 + z1) / 2; half = (z1 - z0) / 2;
}
HandFrame handFrame(const HandHold& h, int side) {
    HandFrame f;
    if (!h.on) return f;
    double zc, half;
    gunSpan(h.at, h.part, zc, half);
    double an = h.angle * DEG2RAD;
    V2 g{std::cos(an), std::sin(an)}, n = wristOff(h.angle);
    // the index finger's end of the grip line: the higher one (or the one toward the muzzle)
    V2 toPinky = (-(-g.y) + 0.3 * (-g.x)) >= (-g.y + 0.3 * g.x) ? g : V2{-g.x, -g.y};
    f.on = true;
    f.o = {h.at.x + n.x * 12, h.at.y + n.y * 12, zc + side * (half + 12)};
    f.X = {-n.x, -n.y, 0}; f.Y = {0, 0, (double)-side}; f.Z = {toPinky.x, toPinky.y, 0};
    return f;
}
Vector3 handPt(const HandFrame& f, HV p) {
    return gunPt({f.o.x + f.X.x * p.x + f.Y.x * p.y + f.Z.x * p.z, f.o.y + f.X.y * p.x + f.Y.y * p.y + f.Z.y * p.z}, f.o.z + f.X.z * p.x + f.Y.z * p.y + f.Z.z * p.z);
}
struct HandGeo { bool on = false; Vector3 c, wrist; HandFrame f; std::vector<HandBone> bones; };
HandGeo handGeo(const HandHold& h, int side) {
    HandGeo g;
    if (!h.on) return g;
    g.f = handFrame(h, side);
    g.on = true;
    DigitPose dg[DIGIT_COUNT];
    digitsOf(h, side > 0 ? "firing" : "support", dg);
    g.bones = handBones(dg);
    g.c = handPt(g.f, {0, 0, 0});
    g.wrist = handPt(g.f, handWrist());
    return g;
}
void capsule(Vector3 a, Vector3 b, float r, Color c) { drawCyl(a, b, r, c); drawSphereLit(a, r, c); drawSphereLit(b, r, c); }

// ---- the mannequin's shapes: smooth tubes through rings (each a rounded-square section: a half-width across, a
// half-depth front to back), in millimetres. Built once.
struct Ring { float y, cx, a, b; };   // height (or length along a limb, 0..1), forward offset, half-width, half-depth
Mesh loft(const std::vector<Ring>& R, float p, int seg = 28, bool caps = true) {
    int n = (int)R.size();
    std::vector<Vector3> P((size_t)n * seg);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < seg; j++) {
            float th = 2 * PI * j / seg, c = std::cos(th), s = std::sin(th);
            float x = R[i].b * (c < 0 ? -1 : 1) * std::pow(std::fabs(c), 2 / p), z = R[i].a * (s < 0 ? -1 : 1) * std::pow(std::fabs(s), 2 / p);
            P[(size_t)i * seg + j] = {R[i].cx + x, R[i].y, z};
        }
    auto at = [&](int i, int j) { return P[(size_t)std::clamp(i, 0, n - 1) * seg + ((j % seg) + seg) % seg]; };
    auto nrm = [&](int i, int j) {
        Vector3 t1 = Vector3Subtract(at(i, j + 1), at(i, j - 1)), t2 = Vector3Subtract(at(i + 1, j), at(i - 1, j));
        Vector3 nn = Vector3Normalize(Vector3CrossProduct(t1, t2));
        Vector3 out = Vector3Subtract(at(i, j), Vector3{R[i].cx, R[i].y, 0});
        return Vector3DotProduct(nn, out) < 0 ? Vector3Negate(nn) : nn;
    };
    struct Vtx { Vector3 p, n; };
    std::vector<Vtx> V;
    auto tri = [&](Vtx a, Vtx b, Vtx c) {   // wound so the face looks outward
        Vector3 f = Vector3CrossProduct(Vector3Subtract(b.p, a.p), Vector3Subtract(c.p, a.p));
        if (Vector3DotProduct(f, Vector3Add(Vector3Add(a.n, b.n), c.n)) < 0) std::swap(b, c);
        V.push_back(a); V.push_back(b); V.push_back(c);
    };
    for (int i = 0; i + 1 < n; i++)
        for (int j = 0; j < seg; j++) {
            Vtx a{at(i, j), nrm(i, j)}, b{at(i, j + 1), nrm(i, j + 1)}, c{at(i + 1, j + 1), nrm(i + 1, j + 1)}, d{at(i + 1, j), nrm(i + 1, j)};
            tri(a, b, c); tri(a, c, d);
        }
    for (int e = 0; e < (caps ? 2 : 0); e++) {   // flat caps at both ends
        int i = e ? n - 1 : 0;
        Vector3 cn{0, e ? 1.f : -1.f, 0};
        Vtx m{{R[i].cx, R[i].y, 0}, cn};
        for (int j = 0; j < seg; j++) tri(m, Vtx{at(i, j), cn}, Vtx{at(i, j + 1), cn});
    }
    Mesh m{};
    m.vertexCount = (int)V.size(); m.triangleCount = m.vertexCount / 3;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    for (int k = 0; k < m.vertexCount; k++) {
        m.vertices[k * 3] = V[k].p.x; m.vertices[k * 3 + 1] = V[k].p.y; m.vertices[k * 3 + 2] = V[k].p.z;
        m.normals[k * 3] = V[k].n.x; m.normals[k * 3 + 1] = V[k].n.y; m.normals[k * 3 + 2] = V[k].n.z;
        m.texcoords[k * 2] = 0; m.texcoords[k * 2 + 1] = 0;
    }
    UploadMesh(&m, false);
    return m;
}
struct Shapes { bool ok = false; Mesh torso, upper, sleeve, fore, neck; };
Shapes& shapes() {
    static Shapes S;
    if (S.ok) return S;
    // the torso, from the base of the neck down to the hips, centred across under the head (body frame: x forward, y up)
    S.torso = loft({{-150, -105, 58, 52}, {-182, -118, 118, 74}, {-212, -128, 165, 98}, {-250, -132, 178, 116}, {-330, -128, 168, 121},
                    {-420, -134, 150, 108}, {-500, -141, 137, 98}, {-575, -147, 148, 103}, {-645, -150, 152, 100}, {-672, -150, 140, 88}}, 2.6f, 36);
    // limbs: 0..1 along their length, round
    S.upper = loft({{0, 0, 50, 50}, {0.15f, 0, 52, 52}, {0.4f, 0, 46, 46}, {0.7f, 0, 40, 40}, {1, 0, 35, 35}, {1.07f, 0, 30, 30}, {1.11f, 0, 18, 18}}, 2, 28, true);
    S.sleeve = loft({{-0.02f, 0, 58, 58}, {0.3f, 0, 55, 55}, {0.55f, 0, 51, 51}}, 2);
    S.fore = loft({{-0.11f, 0, 18, 18}, {-0.07f, 0, 30, 30}, {0, 0, 35, 35}, {0.2f, 0, 40, 40}, {0.5f, 0, 35, 35}, {0.85f, 0, 28, 28}, {1, 0, 25, 25}}, 2, 28, true);
    S.neck = loft({{0, 0, 50, 46}, {0.6f, 0, 52, 48}, {1, 0, 60, 54}}, 2);
    S.ok = true;
    return S;
}
// a limb shape from a to b (its rings round, so any turn about its length will do)
void limb(const Mesh& m, Vector3 a, Vector3 b, Color c) {
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 1e-6f) return;
    Vector3 y = Vector3Scale(d, 1 / len), ref = std::fabs(y.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    Vector3 x = Vector3Normalize(Vector3CrossProduct(ref, y)), z = Vector3CrossProduct(x, y);
    drawMesh(m, Matrix{x.x * U, d.x, z.x * U, a.x, x.y * U, d.y, z.y * U, a.y, x.z * U, d.z, z.z * U, a.z, 0, 0, 0, 1}, c);
}
}  // namespace

Matrix poseGunMatrix() {  // the muzzle drops about the butt (in the shoulder), or the firing hand, or the eye
    BodyHold h = cur();
    const Pose& P = A.design.pose;
    Vector3 e = P.butt.on ? W3(P.butt.at, (float)partZ(P.butt.part) * U) : P.firing.on ? W3(P.firing.at, (float)partZ(P.firing.part) * U) : eyeWorld(h);
    Matrix M = MatrixMultiply(MatrixMultiply(MatrixTranslate(-e.x, -e.y, -e.z), MatrixRotateZ((float)(-h.tilt * DEG2RAD))), MatrixTranslate(e.x, e.y, e.z));
    double g[6];
    if (viewGun(g)) {   // an animation moving the gun: about the firing hand where it's held in the hold
        Vector3 pv = P.firing.on ? W3(P.firing.at, (float)partZ(P.firing.part) * U) : e;
        M = MatrixMultiply(M, gunMoveMatrix(g, Vector3Transform(pv, M)));
    }
    return M;
}

void poseBedShoulder() {   // the shoulder pocket moved onto the butt, the eye staying where it is
    const Pose& P = A.design.pose;
    if (!P.butt.on) return;
    BodyHold& h = editable();
    Frame F = bodyFrame();
    Vector3 d = Vector3Subtract(gunPt(P.butt.at, partZ(P.butt.part)), F.o);
    h.pocket[0] = Vector3DotProduct(d, F.f) / U; h.pocket[1] = Vector3DotProduct(d, F.u) / U; h.pocket[2] = Vector3DotProduct(d, F.r) / U;
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

void poseFocusHand(int hand) {
    const Pose P = viewPose();
    HandGeo g = handGeo(hand == 0 ? P.firing : P.support, hand == 0 ? 1 : -1);
    if (!g.on) return;
    A.poseHand = hand;
    A.target = g.c;
    A.dist = 420 * U;
    A.ortho = false;
    A.yaw = hand == 0 ? 0.6f : -2.4f;  // from the side the hand is on
    A.pitch = 0.25f;
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
    const Pose P = viewPose();
    R.rightShort = armTo(F, handGeo(P.firing, 1), true).shortBy;
    R.leftShort = armTo(F, handGeo(P.support, -1), false).shortBy;
    if (P.butt.on) {
        Vector3 pocket = pocketPt(F), b = gunPt(P.butt.at, partZ(P.butt.part));
        R.buttOff = Vector3Distance(pocket, b) / U;
        R.buttIn = R.buttOff < 60;
    }
    return R;
}

void drawPoseBody() {
    Frame F = bodyFrame();
    const Pose P = viewPose();
    Shapes& SH = shapes();
    // the body's own frame (mm): forward, up, right from the aiming eye; the middle of the body is 32 mm left of it
    Matrix BM{F.f.x * U, F.u.x * U, F.r.x * U, F.o.x, F.f.y * U, F.u.y * U, F.r.y * U, F.o.y, F.f.z * U, F.u.z * U, F.r.z * U, F.o.z, 0, 0, 0, 1};
    auto at = [&](float f, float u, float r) { return MatrixMultiply(MatrixTranslate(f, u, r), BM); };
    // torso in a shirt, the neck, the head (eyes, nose), ear defenders on a band over the top
    drawMesh(SH.torso, at(0, 0, -32), SHIRT);
    limb(SH.neck, bp(F, -86, -45, -32), bp(F, -112, -165, -32), SKIN);
    drawMesh(G.sphere, MatrixMultiply(MatrixScale(88, 110, 76), at(-90, 4, -32)), SKIN);
    capsule(bp(F, -9, -24, -32), bp(F, 9, -44, -32), 10 * U, SKIN);                       // the nose
    drawSphereLit(bp(F, -5, 0, -64), 6 * U, Color{60, 50, 46, 255});                       // the other eye
    for (int s = 0; s < 2; s++) {
        float r = s ? 46.f : -110.f, out = s ? 22.f : -22.f;
        drawCyl(bp(F, -98, -8, r), bp(F, -98, -8, r + out), 44 * U, MUFF);
        drawCyl(bp(F, -98, -8, r + out * 0.4f), bp(F, -98, -8, r + out * 1.05f), 38 * U, MUFF2);
    }
    for (int k = 0; k < 8; k++) {   // the headband, over the top of the head
        float a0 = PI * k / 8, a1 = PI * (k + 1) / 8;
        Vector3 p0 = bp(F, -98, 6 + 120 * std::sin(a0), -32 + 98 * std::cos(a0)), p1 = bp(F, -98, 6 + 120 * std::sin(a1), -32 + 98 * std::cos(a1));
        capsule(p0, p1, 8 * U, MUFF);
    }
    Vector3 sR = bp(F, -140, -215, 153), sL = bp(F, -140, -215, -217);
    // the shoulder pocket, where a butt beds in
    if (P.butt.on) {
        PoseReport R = poseReport();
        drawSphereLit(pocketPt(F), 30 * U, R.buttIn ? Color{110, 230, 140, 255} : Color{232, 161, 58, 255}, 0.5f);
    }
    // the line of sight: from the aiming eye, straight along the bore, far out past the muzzle, to line the sights up on
    {
        BodyHold h = cur();
        const auto& br = A.design.breech;
        V2 dir{1, 0};
        if (br.on && br.hasFace && br.hasMuzzle) {
            double dx = br.muzzle.x - br.face.x, dy = br.muzzle.y - br.face.y, l = std::hypot(dx, dy);
            if (l > 1) dir = {dx / l, dy / l};
        }
        Vector3 e = eyeWorld(h);
        Matrix M = poseGunMatrix();   // the eye looks along the gun as the hold (and an animation) turns it
        Vector3 d = Vector3Normalize(Vector3Subtract(Vector3Transform(W3({h.eye.x + dir.x * 100, h.eye.y + dir.y * 100}, (float)h.eyeZ * U), M), Vector3Transform(e, M)));
        drawSphereLit(e, 4 * U, Color{255, 80, 70, 255});
        drawCyl(Vector3Add(e, Vector3Scale(d, 8 * U)), Vector3Add(e, Vector3Scale(d, 6000 * U)), 0.8f * U, Color{255, 80, 70, 255});
        DrawLine3D(e, Vector3Add(e, Vector3Scale(d, 6000 * U)), Color{255, 80, 70, 255});
    }
    // arms and hands
    for (int side = 0; side < 2; side++) {
        bool right = side == 0;
        HandGeo g = handGeo(right ? P.firing : P.support, right ? 1 : -1);
        Arm a = armTo(F, g, right);
        bool strain = a.shortBy > 0.5;
        drawSphereLit(a.s, 54 * U, strain ? STRAIN : SHIRT);   // the shoulder
        limb(SH.upper, a.s, a.e, strain ? STRAIN : SKIN);
        limb(SH.sleeve, a.s, a.e, strain ? STRAIN : SHIRT);
        limb(SH.fore, a.e, a.w, strain ? STRAIN : SKIN);
        if (g.on) {
            const HandBone *thumb = nullptr, *palmIndex = nullptr;
            for (auto& b : g.bones) {
                bool picked = A.poseHand == side && b.digit == A.poseDigit && b.digit >= 0;
                Color col = picked ? Color{240, 196, 150, 255} : SKIN;
                if (b.digit == DIGIT_THUMB && b.seg == 0) {   // the ball of the thumb: fat at the wrist, narrowing to its knuckle
                    thumb = &b;
                    HV m{(b.a.x + b.b.x) / 2, (b.a.y + b.b.y) / 2, (b.a.z + b.b.z) / 2};
                    capsule(handPt(g.f, b.a), handPt(g.f, m), (float)b.radius * U, col);
                    capsule(handPt(g.f, m), handPt(g.f, b.b), (float)(b.radius * 0.8) * U, col);
                    continue;
                }
                if (b.name == "palm_index") palmIndex = &b;
                capsule(handPt(g.f, b.a), handPt(g.f, b.b), (float)b.radius * U, col);
            }
            if (thumb && palmIndex) {   // the web of skin between the thumb and the index finger
                auto lerp = [](HV a, HV b, double t) { return HV{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; };
                for (double t : {0.35, 0.7})
                    capsule(handPt(g.f, lerp(thumb->a, thumb->b, t)), handPt(g.f, lerp(palmIndex->a, palmIndex->b, 0.25 + 0.5 * t)), 9 * U, SKIN);
            }
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
    if (A.S && sim::animPlaying(*A.S)) return;   // playing on the gun: nothing to grab
    const Pose P = viewPose();
    Vector2 m = GetMousePosition();
    auto dot = [&](Vector2 s, int id, Color c, const char* label) {
        bool hot = Vector2Distance(s, m) < 10 || A.poseDrag == id || A.posePicked == id;
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
    // the picked hand's fingertips: drag one and it follows the mouse (Shift: just curl it)
    if (A.poseHand >= 0) {
        HandGeo g = handGeo(A.poseHand == 0 ? P.firing : P.support, A.poseHand == 0 ? 1 : -1);
        if (g.on)
            for (auto& b : g.bones)
                if (b.digit >= 0 && b.seg == 2) {
                    Vector2 s = toScreen(handPt(g.f, b.b));
                    bool picked = b.digit == A.poseDigit;
                    Color c = picked ? Color{255, 205, 60, 255} : Color{240, 196, 150, 255};
                    bool hot = Vector2Distance(s, m) < 8 || A.poseDrag == 20 + b.digit || A.posePicked == 20 + b.digit;
                    DrawCircleV(s, hot || picked ? 5.f : 3.5f, c);
                    if (hot) viewText(TextFormat(A.posePicked == 20 + b.digit ? "%s: drag it anywhere (Shift: curl)" : "%s: click to pick it, then drag", digitName(b.digit)), s.x + 8, s.y - 16 * CFG.uiScale, 11, c);
                    handles.push_back({s, 20 + b.digit});
                }
    }
    Frame F = bodyFrame();
    dot(toScreen(bp(F, -90, 140, -32)), 6, Color{232, 161, 58, 255}, "Drag to move the body");
    if (P.butt.on) dot(toScreen(pocketPt(F)), 7, Color{232, 161, 58, 255}, "Shoulder");
    {   // name the line of sight, a little way out past the eye
        BodyHold h = cur();
        Vector3 e = eyeWorld(h), far = Vector3Transform(W3({h.eye.x + 450, h.eye.y}, (float)h.eyeZ * U), poseGunMatrix());
        (void)e;
        Vector2 s = toScreen(far);
        viewText("Line of sight", s.x + 6, s.y - 18 * CFG.uiScale, 11, Color{255, 120, 110, 255});
    }
}

void poseInput(bool overUI) {
    auto& P = A.design.pose;
    if (A.S && sim::animPlaying(*A.S)) return;   // playing on the gun
    if (A.poseDrag < 0) {
        if (overUI || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        Vector2 m = GetMousePosition();
        float best = 12;
        int id = -1;
        for (auto& h : handles) { float d = Vector2Distance(h.s, m); if (d < best) { best = d; id = h.id; } }
        if (id == 0) A.poseHand = 0;
        if (id == 2) A.poseHand = 1;
        if (id >= 20) A.poseDigit = id - 20;
        // the first click picks a dot; press on it again to drag it, so picking never moves anything
        if (id < 0 || id != A.posePicked) { A.posePicked = id; return; }
        A.poseDrag = id; A.posePress = m; A.poseLive = false;
        return;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { if (A.poseLive) commit(); A.poseDrag = -1; return; }
    if (!A.poseLive) {   // a few pixels before anything moves
        if (Vector2Distance(GetMousePosition(), A.posePress) < 5 * CFG.uiScale) return;
        A.poseLive = true;
    }
    if (A.poseDrag >= 20) {
        int side = A.poseHand == 0 ? 1 : -1, digit = A.poseDrag - 20;
        HandHold& h = *editHand(A.poseHand == 0 ? 0 : 1);
        if (!h.digitsSet) { digitsOf(h, A.poseHand == 0 ? "firing" : "support", h.digits); h.digitsSet = true; }
        DigitPose& d = h.digits[digit];
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {   // Shift: just curl it, up opens, down closes
            float dy = GetMouseDelta().y;
            const double share[3] = {0.35, 0.4, 0.25};
            for (int s = 0; s < 3; s++) d.bend[s] += dy * share[s] * 1.2;
            clampDigit(digit, d);
            return;
        }
        // the tip follows the mouse: along the mouse's line of sight, at the tip's present depth if it can reach that,
        // or else the nearest depth it can reach (so dragging over the palm brings the thumb onto the palm)
        HandGeo g = handGeo(h, side);
        if (!g.on) return;
        Vector3 o = handPt(g.f, {0, 0, 0});   // handPt is affine: origin plus three columns
        Vector3 cx = Vector3Subtract(handPt(g.f, {1, 0, 0}), o), cy = Vector3Subtract(handPt(g.f, {0, 1, 0}), o), cz = Vector3Subtract(handPt(g.f, {0, 0, 1}), o);
        Matrix Mi = MatrixInvert(Matrix{cx.x, cy.x, cz.x, o.x, cx.y, cy.y, cz.y, o.y, cx.z, cy.z, cz.z, o.z, 0, 0, 0, 1});
        Ray r = mouseRay();
        Vector3 ro = Vector3Transform(r.position, Mi), rd = Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Add(r.position, r.direction), Mi), ro));
        HV tip = digitTip(h.digits, digit), root{};
        double reach = 0;
        for (auto& b : g.bones) if (b.digit == digit) { if (b.seg == 0) root = b.a; reach += std::sqrt((b.b.x - b.a.x) * (b.b.x - b.a.x) + (b.b.y - b.a.y) * (b.b.y - b.a.y) + (b.b.z - b.a.z) * (b.b.z - b.a.z)); }
        reach *= 0.95;
        auto along = [&](HV p) { return (p.x - ro.x) * rd.x + (p.y - ro.y) * rd.y + (p.z - ro.z) * rd.z; };
        double t = along(tip), tr = along(root);
        Vector3 c = Vector3Add(ro, Vector3Scale(rd, (float)tr));
        double off2 = (c.x - root.x) * (c.x - root.x) + (c.y - root.y) * (c.y - root.y) + (c.z - root.z) * (c.z - root.z);
        if (off2 < reach * reach) { double hw = std::sqrt(reach * reach - off2); t = std::clamp(t, tr - hw, tr + hw); }
        else t = tr;
        if (t <= 0) return;
        Vector3 hp = Vector3Add(ro, Vector3Scale(rd, (float)t));
        reachDigit(h.digits, digit, {hp.x, hp.y, hp.z});
        return;
    }
    Ray r = mouseRay();
    auto onPlane = [&](float z, Vector3& out) {
        if (std::fabs(r.direction.z) < 1e-6f) return false;
        float t = (z - r.position.z) / r.direction.z;
        if (t < 0) return false;
        out = Vector3Add(r.position, Vector3Scale(r.direction, t));
        return true;
    };
    Matrix inv = MatrixInvert(poseGunMatrix());
    if (A.poseDrag == 7) {  // the shoulder pocket: across the body's side, the eye staying put
        BodyHold& h = editable();
        Frame F = bodyFrame();
        Vector3 c = pocketPt(F);
        float den = Vector3DotProduct(r.direction, F.r);
        if (std::fabs(den) < 1e-6f) return;
        float t = Vector3DotProduct(Vector3Subtract(c, r.position), F.r) / den;
        if (t < 0) return;
        Vector3 d = Vector3Subtract(Vector3Add(r.position, Vector3Scale(r.direction, t)), F.o);
        h.pocket[0] = Vector3DotProduct(d, F.f) / U;
        h.pocket[1] = Vector3DotProduct(d, F.u) / U;
        return;
    }
    if (A.poseDrag == 6) {  // the body: the eye follows the cursor across the gun's side
        BodyHold& h = editable();
        Vector3 p;
        if (!onPlane((float)h.eyeZ * U, p)) return;
        Vector3 e = eyeWorld(h), d = Vector3Subtract(p, e);
        Vector3 g = Vector3Transform(d, MatrixRotateZ((float)(h.tilt * DEG2RAD)));  // back into the gun's frame
        h.eye.x += g.x / U; h.eye.y -= g.y / U;
        return;
    }
    HandHold& h = *editHand(A.poseDrag < 2 ? 0 : A.poseDrag < 4 ? 1 : 2);
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
