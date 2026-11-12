// Gunsmith3D - the pose editor (see pose.hpp).
#include "app/pose.hpp"
#include "app/app.hpp"
#include "app/fonts.hpp"
#include "app/gfx.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "app/gizmo.hpp"
#include <cstdio>
#include <functional>
#include "raymath.h"
#include "hand.hpp"
#include "app/npcedit.hpp"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>

Ray mouseRay();  // (view.cpp)

// the hands as shown, and (an animation taking one onto or off the gun) the two places it's moving between
struct ViewHands { Pose P; HandHold from[2], to[2]; double u[2] = {-1, -1}; };

// The pose as shown: the hold's hands, or an animation's where it has got to (playing on the gun, or on the
// timeline being edited). The butt and the body always stay as posed.
ViewHands viewHands() {
    ViewHands V;
    V.P = A.design.pose;
    AnimHands h;
    bool anim = false;
    if (A.S && sim::animPlaying(*A.S)) { h = sim::animHandsNow(*A.S); anim = true; }
    else if (A.posing && A.animEdit) {
        auto it = A.design.anims.find(A.animSlot);
        if (it != A.design.anims.end()) { h = animAt(A.design, it->second, A.animT); anim = true; }
    }
    if (anim) {
        V.P.firing = h.firing; V.P.support = h.support;
        V.from[0] = h.fromF; V.to[0] = h.toF; V.u[0] = h.uF;
        V.from[1] = h.fromS; V.to[1] = h.toS; V.u[1] = h.uS;
    }
    return V;
}
Pose viewPose() { return viewHands().P; }
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
AnimKey* editKey() {
    if (!A.animEdit) return nullptr;
    Animation& a = A.design.anims[A.animSlot];
    int k = animKeyAt(a, A.animT);
    if (k < 0) { editHand(0); k = animKeyAt(a, A.animT); }
    return k >= 0 ? &a.keys[k] : nullptr;
}
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

void poseApplyRig();   // (below) the shoulders and arm lengths: the mannequin's, or your character's

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
    poseApplyRig();
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

// the arms' lengths (mm): the mannequin's, or the player's character's
double gUpperArm[2] = {UPPER_ARM, UPPER_ARM}, gForearm[2] = {FOREARM, FOREARM};   // [0] right (firing), [1] left
struct Arm { Vector3 s, e, w; double shortBy = 0; };
// Two bones from the shoulder to the wrist. The elbow points along `pole`, swung round the shoulder-wrist line by
// `elbowDeg` (+ up). An arm that's short first pushes its shoulder forward toward the hand (up to 70 mm), as people do.
Arm reach(Vector3 s, Vector3 target, Vector3 pole, double elbowDeg = 0, Vector3 up = {0, 1, 0}, int armIdx = 0) {
    Arm A_{s, s, target, 0};
    float L1 = (float)gUpperArm[armIdx] * U, L2 = (float)gForearm[armIdx] * U;
    Vector3 d = Vector3Subtract(target, s);
    float dist = Vector3Length(d);
    if (dist < 1e-4f) return A_;
    if (dist > L1 + L2 - 1e-3f) {   // the shoulder comes forward
        float push = std::min(dist - (L1 + L2) + 1e-3f, 70.f * U);
        s = Vector3Add(s, Vector3Scale(d, push / dist));
        A_.s = s;
        d = Vector3Subtract(target, s);
        dist = Vector3Length(d);
    }
    Vector3 dir = Vector3Scale(d, 1 / dist);
    if (dist > L1 + L2 - 1e-3f) { A_.shortBy = (dist - (L1 + L2)) / U; dist = L1 + L2 - 1e-3f; A_.w = Vector3Add(s, Vector3Scale(dir, dist)); }
    float a = (L1 * L1 - L2 * L2 + dist * dist) / (2 * dist), h = std::sqrt(std::max(0.f, L1 * L1 - a * a));
    Vector3 p = Vector3Subtract(pole, Vector3Scale(dir, Vector3DotProduct(pole, dir)));
    if (Vector3Length(p) < 1e-5f) p = {0, -1, 0};
    p = Vector3Normalize(p);
    if (elbowDeg != 0) {   // swung round the shoulder-wrist line, + toward up
        float sg = Vector3DotProduct(Vector3CrossProduct(dir, p), up) >= 0 ? 1.f : -1.f;
        p = Vector3RotateByAxisAngle(p, dir, sg * (float)(elbowDeg * DEG2RAD));
    }
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
    f.o = {h.at.x + n.x * 12, h.at.y + n.y * 12, zc + side * (half + 12 + h.dz)};   // (dz: lifted off the gun's side)
    f.X = {-n.x, -n.y, 0}; f.Y = {0, 0, (double)-side}; f.Z = {toPinky.x, toPinky.y, 0};
    if (h.twist != 0) {   // rolled about the grip line (Z)
        double c = std::cos(h.twist * DEG2RAD), s = std::sin(h.twist * DEG2RAD);
        HV X = f.X, Y = f.Y;
        f.X = {X.x * c + Y.x * s, X.y * c + Y.y * s, X.z * c + Y.z * s};
        f.Y = {-X.x * s + Y.x * c, -X.y * s + Y.y * c, -X.z * s + Y.z * c};
    }
    return f;
}
// a hand's frame in the world: a point in hand space (mm) is o + x p.x + y p.y + z p.z
struct WF { Vector3 o, x, y, z; };
Vector3 handPt(const WF& f, HV p) {
    return Vector3Add(f.o, Vector3Add(Vector3Scale(f.x, (float)p.x), Vector3Add(Vector3Scale(f.y, (float)p.y), Vector3Scale(f.z, (float)p.z))));
}
WF worldFrame(const HandFrame& f) {
    auto G = [](HV p) { return gunPt({p.x, p.y}, p.z); };
    Vector3 o = G(f.o);
    return {o, Vector3Subtract(G({f.o.x + f.X.x, f.o.y + f.X.y, f.o.z + f.X.z}), o), Vector3Subtract(G({f.o.x + f.Y.x, f.o.y + f.Y.y, f.o.z + f.Y.z}), o),
            Vector3Subtract(G({f.o.x + f.Z.x, f.o.y + f.Z.y, f.o.z + f.Z.z}), o)};
}
struct HandGeo { bool on = false; Vector3 c, wrist; WF f; std::vector<HandBone> bones; };
void finishGeo(HandGeo& g, const HandHold& h, int side) {
    DigitPose dg[DIGIT_COUNT];
    digitsOf(h, side > 0 ? "firing" : "support", dg);
    g.bones = handBones(dg);
    g.c = handPt(g.f, {0, 0, 0});
    g.wrist = handPt(g.f, handWrist());
}
HandGeo handGeo(const HandHold& h, int side) {   // a hand on the gun
    HandGeo g;
    if (!h.on) return g;
    g.f = worldFrame(handFrame(h, side));
    g.on = true;
    finishGeo(g, h, side);
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

void enterPose() {
    A.posing = true;
    A.running = false;
    poseDefaultHands();
    poseView();
}
void leavePose() { A.posing = false; A.poseDrag = -1; fitView(); }

// The shoulders (body frame mm): the mannequin's, or the character's
double gShoulder[2][3] = {{-140, -215, 153}, {-140, -215, -217}};

// ---- your character in place of the mannequin: its body as it stands, placed so its aiming (right) eye is the
// pose's eye; its arms reach for the hands with its own shoulders and lengths
struct CharFit {
    bool ok = false;
    const NpcDef* n = nullptr;
    std::vector<NpcBoneAt> P;    // standing (NPC frame, mm: y up, facing +z)
    double eye[3] = {0, 0, 0};
    double rs = 1;               // +1: its right is +x
    int upper[2] = {-1, -1}, fore[2] = {-1, -1}, hand[2] = {-1, -1};   // [0] right, [1] left
};
static CharFit charFit() {
    CharFit c;
    if (!CFG.poseCharacter) return c;
    const NpcDef* n = playerCharacter();
    if (!n) return c;
    auto find = [&](const char* nm) { for (int i = 0; i < (int)n->bones.size(); i++) if (n->bones[i].name == nm) return i; return -1; };
    int head = find("Head");
    c.upper[0] = find("Upper arm (right)"); c.upper[1] = find("Upper arm (left)");
    c.fore[0] = find("Forearm (right)"); c.fore[1] = find("Forearm (left)");
    c.hand[0] = find("Hand (right)"); c.hand[1] = find("Hand (left)");
    if (head < 0 || c.upper[0] < 0 || c.upper[1] < 0 || c.fore[0] < 0 || c.fore[1] < 0) return c;
    c.n = n;
    c.P = npcPose(*n);
    c.rs = c.P[c.upper[0]].a[0] >= c.P[c.upper[1]].a[0] ? 1 : -1;
    const NpcBone& hb = n->bones[head];
    const NpcBoneAt& hp = c.P[head];
    double R = (hb.r0 + hb.r1) / 2 * hb.depth;
    for (int q = 0; q < 3; q++) c.eye[q] = (hp.a[q] + hp.b[q]) / 2;
    c.eye[0] += c.rs * 32; c.eye[1] += 8; c.eye[2] += R * 0.82;   // the right eye, on the front of the head
    c.ok = true;
    return c;
}
// a point / a direction of its own frame, in the pose's body frame (forward, up, right of the eye)
static void toBody(const CharFit& c, const double q[3], double out[3]) { out[0] = q[2] - c.eye[2]; out[1] = q[1] - c.eye[1]; out[2] = (q[0] - c.eye[0]) * c.rs; }
static Vector3 charPt(const Frame& F, const CharFit& c, const double q[3]) { double b[3]; toBody(c, q, b); return bp(F, b[0], b[1], b[2]); }
static Vector3 charDir(const Frame& F, const CharFit& c, const double v[3]) {
    return Vector3Add(Vector3Scale(F.f, (float)v[2]), Vector3Add(Vector3Scale(F.u, (float)v[1]), Vector3Scale(F.r, (float)(v[0] * c.rs))));
}
void poseApplyRig() {
    CharFit c = charFit();
    if (!c.ok) {
        const double s0[2][3] = {{-140, -215, 153}, {-140, -215, -217}};
        std::memcpy(gShoulder, s0, sizeof gShoulder);
        gUpperArm[0] = gUpperArm[1] = UPPER_ARM; gForearm[0] = gForearm[1] = FOREARM;
        return;
    }
    for (int k = 0; k < 2; k++) {
        toBody(c, c.P[c.upper[k]].a, gShoulder[k]);
        gUpperArm[k] = c.n->bones[c.upper[k]].len;
        gForearm[k] = c.n->bones[c.fore[k]].len;
    }
}
static Matrix boneM(Vector3 o, Vector3 X, Vector3 Y, Vector3 Z) {   // (X, Y, Z already scaled to world units per mm)
    return Matrix{X.x, Y.x, Z.x, o.x, X.y, Y.y, Z.y, o.y, X.z, Y.z, Z.z, o.z, 0, 0, 0, 1};
}
static Vector3 shoulderPt(const Frame& F, bool right) { const double* s = gShoulder[right ? 0 : 1]; return bp(F, s[0], s[1], s[2]); }
static Vector3 elbowPole(const Frame& F, bool right) {
    return Vector3Add(Vector3Scale(F.u, -1), Vector3Add(Vector3Scale(F.r, right ? 0.7f : -0.7f), Vector3Scale(F.f, -0.3f)));
}
// where a hand off the gun goes: placed, or hanging at the side
static Vector3 offGunWrist(const Frame& F, const HandHold& h, bool right) {
    if (h.free) return bp(F, h.pos[0], h.pos[1], h.pos[2]);
    const double* s = gShoulder[right ? 0 : 1];
    return bp(F, s[0] + 30, s[1] - 555, s[2] + (right ? 22 : -22));
}
// which way round a hand is built (its frame's handedness), so a hand off the gun matches the same hand on it
static float handedness(int side) {
    const HandHold& h = side > 0 ? A.design.pose.firing : A.design.pose.support;
    if (h.on) { WF f = worldFrame(handFrame(h, side)); return Vector3DotProduct(f.x, Vector3CrossProduct(f.y, f.z)) >= 0 ? 1.f : -1.f; }
    return (float)-side;
}
// a hand off the gun: its fingers carry on from the forearm, the palm turned toward the body, rolled by its twist
static HandGeo freeGeo(const Frame& F, const HandHold& h, int side, const Arm& a) {
    HandGeo g;
    g.on = true;
    Vector3 X = Vector3Normalize(Vector3Subtract(a.w, a.e));
    Vector3 Y0 = side > 0 ? Vector3Negate(F.r) : F.r;
    Vector3 Y = Vector3Subtract(Y0, Vector3Scale(X, Vector3DotProduct(X, Y0)));
    if (Vector3Length(Y) < 1e-4f) Y = F.f;
    Y = Vector3Normalize(Y);
    if (h.twist != 0) Y = Vector3RotateByAxisAngle(Y, X, (float)(h.twist * DEG2RAD));
    Vector3 Z = Vector3CrossProduct(X, Y);
    if (handedness(side) < 0) Z = Vector3Negate(Z);
    g.f = {{0, 0, 0}, Vector3Scale(X, U), Vector3Scale(Y, U), Vector3Scale(Z, U)};
    HV hw = handWrist();
    g.f.o = Vector3Subtract(a.w, Vector3Add(Vector3Scale(g.f.x, (float)hw.x), Vector3Add(Vector3Scale(g.f.y, (float)hw.y), Vector3Scale(g.f.z, (float)hw.z))));
    finishGeo(g, h, side);
    return g;
}
struct Limb { Arm arm; HandGeo g; };
static Limb limbFor(const Frame& F, const HandHold& h, int side) {
    bool right = side > 0;
    Limb L;
    if (h.on) {
        L.g = handGeo(h, side);
        L.arm = reach(shoulderPt(F, right), L.g.wrist, elbowPole(F, right), h.elbow, F.u, right ? 0 : 1);
    } else {
        L.arm = reach(shoulderPt(F, right), offGunWrist(F, h, right), elbowPole(F, right), h.elbow, F.u, right ? 0 : 1);
        L.g = freeGeo(F, h, side, L.arm);
    }
    return L;
}
// a side's arm as shown: where its hand is, or (an animation taking it onto or off the gun) on its way between two
static Limb sideLimb(const Frame& F, const ViewHands& V, int k) {
    int side = k == 0 ? 1 : -1;
    const HandHold& h = k == 0 ? V.P.firing : V.P.support;
    if (V.u[k] < 0) return limbFor(F, h, side);
    Limb a = limbFor(F, V.from[k], side), b = limbFor(F, V.to[k], side);
    float u = (float)V.u[k];
    Limb L = u < 0.5f ? a : b;   // (the fingers: the nearer key's)
    auto lerp3 = [&](Vector3 p, Vector3 q) { return Vector3Lerp(p, q, u); };
    Vector3 wrist = lerp3(a.g.wrist, b.g.wrist);
    Vector3 X = Vector3Normalize(lerp3(a.g.f.x, b.g.f.x)), Y = lerp3(a.g.f.y, b.g.f.y);
    Y = Vector3Normalize(Vector3Subtract(Y, Vector3Scale(X, Vector3DotProduct(X, Y))));
    Vector3 Z = Vector3CrossProduct(X, Y);
    if (Vector3DotProduct(Z, lerp3(a.g.f.z, b.g.f.z)) < 0) Z = Vector3Negate(Z);
    L.g.f = {{0, 0, 0}, Vector3Scale(X, U), Vector3Scale(Y, U), Vector3Scale(Z, U)};
    HV hw = handWrist();
    L.g.f.o = Vector3Subtract(wrist, Vector3Add(Vector3Scale(L.g.f.x, (float)hw.x), Vector3Add(Vector3Scale(L.g.f.y, (float)hw.y), Vector3Scale(L.g.f.z, (float)hw.z))));
    L.g.c = handPt(L.g.f, {0, 0, 0}); L.g.wrist = wrist;
    double elbow = V.from[k].elbow + (V.to[k].elbow - V.from[k].elbow) * u;
    L.arm = reach(shoulderPt(F, side > 0), wrist, elbowPole(F, side > 0), elbow, F.u, side > 0 ? 0 : 1);
    return L;
}

void poseFocusHand(int hand) {
    HandGeo g = sideLimb(bodyFrame(), viewHands(), hand == 0 ? 0 : 1).g;
    if (!g.on) return;
    A.poseHand = hand;
    A.target = g.c;
    A.dist = 420 * U;
    A.ortho = false;
    A.yaw = hand == 0 ? 0.6f : -2.4f;  // from the side the hand is on
    A.pitch = 0.25f;
}

PoseReport poseReport() {
    PoseReport R;
    Frame F = bodyFrame();
    const Pose P = viewPose();
    ViewHands V = viewHands();
    R.rightShort = sideLimb(F, V, 0).arm.shortBy;
    R.leftShort = sideLimb(F, V, 1).arm.shortBy;
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
    CharFit CF = charFit();
    // your character: its body as it stands (everything but its arms, which reach for the hands below)
    std::vector<int> armOf, anchor;      // which arm a bone belongs to (-1: none); the arm bone it rides with
    std::vector<Matrix> stand;           // each bone's standing frame, in the world
    std::vector<char> onHand;            // a hand, or a finger on one
    float handed = 1;                    // (a mirrored fit draws its meshes inside out unless culling is off)
    if (CF.ok) {
        const NpcDef& n = *CF.n;
        armOf.assign(n.bones.size(), -1); anchor.assign(n.bones.size(), -1); onHand.assign(n.bones.size(), 0);
        for (int i = 0; i < (int)n.bones.size(); i++) {
            for (int k = 0; k < 2; k++) if (i == CF.upper[k]) armOf[i] = k;
            if (armOf[i] < 0 && n.bones[i].parent >= 0) armOf[i] = armOf[n.bones[i].parent];
            onHand[i] = i == CF.hand[0] || i == CF.hand[1] || (n.bones[i].parent >= 0 && n.bones[i].parent < i && onHand[n.bones[i].parent] && n.bones[i].gen);
            anchor[i] = (i == CF.upper[0] || i == CF.upper[1] || i == CF.fore[0] || i == CF.fore[1]) ? i
                      : (i == CF.hand[0] ? CF.fore[0] : i == CF.hand[1] ? CF.fore[1] : n.bones[i].parent >= 0 ? anchor[n.bones[i].parent] : -1);
            const NpcBoneAt& q = CF.P[i];
            stand.push_back(boneM(charPt(F, CF, q.a), Vector3Scale(charDir(F, CF, q.x), U), Vector3Scale(charDir(F, CF, q.y), U), Vector3Scale(charDir(F, CF, q.z), U)));
        }
        Vector3 X = charDir(F, CF, CF.P[0].x), Y = charDir(F, CF, CF.P[0].y), Z = charDir(F, CF, CF.P[0].z);
        handed = Vector3DotProduct(X, Vector3CrossProduct(Y, Z)) >= 0 ? 1.f : -1.f;
        if (handed < 0) rlDisableBackfaceCulling();
        for (int i = 0; i < (int)n.bones.size(); i++)
            if (armOf[i] < 0 && !npcBodyBone(n, i)) drawNpcBone(n.bones[i], stand[i], hexColor(n.bones[i].color));
        if (handed < 0) rlEnableBackfaceCulling();
    } else {
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
    }
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
    ViewHands VH = viewHands();
    std::vector<Matrix> bodyM = stand;   // (the one-piece body: as it stands, its arms where they reach, drawn after both)
    bool bodyStrain = false;
    for (int side = 0; side < 2; side++) {
        Limb LB = sideLimb(F, VH, side);
        HandGeo& g = LB.g;
        Arm& a = LB.arm;
        bool strain = a.shortBy > 0.5;
        Color handCol = SKIN;
        if (CF.ok) {   // its own upper arm and forearm along the arm as it reaches; its gear on them rides along
            const NpcDef& n = *CF.n;
            auto segM = [&](int bi, Vector3 from, Vector3 to) {
                Vector3 Y = Vector3Normalize(Vector3Subtract(to, from));
                const NpcBoneAt& q = CF.P[bi];
                Vector3 X0 = charDir(F, CF, q.x);
                Vector3 X = Vector3Subtract(X0, Vector3Scale(Y, Vector3DotProduct(X0, Y)));
                if (Vector3Length(X) < 1e-4f) X = Vector3CrossProduct(Y, F.u);
                X = Vector3Normalize(X);
                Vector3 Z = Vector3Scale(Vector3CrossProduct(X, Y), handed);
                return boneM(from, Vector3Scale(X, U), Vector3Scale(Y, U), Vector3Scale(Z, U));
            };
            Matrix now[2] = {segM(CF.upper[side], a.s, a.e), segM(CF.fore[side], a.e, a.w)};
            bodyM[CF.upper[side]] = now[0]; bodyM[CF.fore[side]] = now[1];
            bodyStrain |= strain;
            if (handed < 0) rlDisableBackfaceCulling();
            for (int i = 0; i < (int)n.bones.size(); i++) {
                if (armOf[i] != side || onHand[i] || npcBodyBone(n, i)) continue;   // (the hand and its fingers: the posed finger skeleton, below)
                Color c = strain ? STRAIN : hexColor(n.bones[i].color);
                if (i == CF.upper[side]) drawNpcBone(n.bones[i], now[0], c);
                else if (i == CF.fore[side]) drawNpcBone(n.bones[i], now[1], c);
                else if (anchor[i] >= 0) {   // gear: where it sits on its arm bone, as that bone has moved
                    Matrix an = anchor[i] == CF.upper[side] ? now[0] : now[1];
                    drawNpcBone(n.bones[i], MatrixMultiply(MatrixMultiply(stand[i], MatrixInvert(stand[anchor[i]])), an), c);
                }
            }
            if (handed < 0) rlEnableBackfaceCulling();
            if (CF.hand[side] >= 0) handCol = hexColor(n.bones[CF.hand[side]].color);
        } else {
        drawSphereLit(a.s, 54 * U, strain ? STRAIN : SHIRT);   // the shoulder
        limb(SH.upper, a.s, a.e, strain ? STRAIN : SKIN);
        limb(SH.sleeve, a.s, a.e, strain ? STRAIN : SHIRT);
        limb(SH.fore, a.e, a.w, strain ? STRAIN : SKIN);
        }
        const Color SKIN = handCol;   // (the hand: the character's hand colour)
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
    if (CF.ok && CF.n->look.on) { Color red = STRAIN; drawCharacterBody(*CF.n, bodyM, bodyStrain ? &red : nullptr); }   // the one-piece body, arms reaching
}

static void poseRingsDraw();   // (below)
// the gun's own handles in an animation: its middle (move it) and its muzzle (aim it)
static Vector3 gunMidWorld() {
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& b : A.design.bodies) if (!b.magazine) for (auto& q : b.pts) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
    if (x0 > x1) return gunPt({0, 0}, 0);
    return gunPt({(x0 + x1) / 2, (y0 + y1) / 2 - 40}, 0);
}
static Vector3 muzzleWorld() {
    const auto& br = A.design.breech;
    if (br.on && br.hasMuzzle) return gunPt(br.muzzle, 0);
    double x1 = -1e9, y = 0;
    for (auto& b : A.design.bodies) for (auto& q : b.pts) if (q.x > x1) { x1 = q.x; y = q.y; }
    return gunPt({x1, y}, 0);
}

// ---------------------------------------------------------------- handles
namespace {
Vector3 dragN, dragP0;          // a free drag: the plane it moves in (facing the camera at the press), and where it began
double dragStart[6];            // ...and the gun's move at the press
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
        HandGeo g = sideLimb(bodyFrame(), viewHands(), A.poseHand == 0 ? 0 : 1).g;
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
    {   // the elbows (drag one to swing it round), and hands off the gun (drag one to place it anywhere: a pouch, the
        // magazine on a vest, the charging handle's far side)
        ViewHands VH = viewHands();
        const Color EL{120, 190, 255, 255};
        for (int k = 0; k < 2; k++) {
            const HandHold& h = k == 0 ? VH.P.firing : VH.P.support;
            Limb L = sideLimb(F, VH, k);
            bool hot = A.posePicked == 8 + k || A.poseDrag == 8 + k;
            dot(toScreen(L.arm.e), 8 + k, EL, hot ? "Elbow: drag to swing it round" : nullptr);
            if (!h.on) {
                bool hh = A.posePicked == 10 + k || A.poseDrag == 10 + k;
                dot(toScreen(L.g.wrist), 10 + k, EL, hh ? "Drag to place the hand anywhere" : (k == 0 ? "Firing hand (off the gun)" : "Support hand (off the gun)"));
            }
        }
    }
    if (A.animEdit) {   // the gun itself at this moment of the animation: move it, aim it
        const Color GC{255, 140, 210, 255};
        dot(toScreen(gunMidWorld()), 12, GC, A.posePicked == 12 || A.poseDrag == 12 ? "Gun: drag to move it" : "Gun");
        dot(toScreen(muzzleWorld()), 13, GC, A.posePicked == 13 || A.poseDrag == 13 ? "Aim: drag to point the gun (Shift: roll it)" : "Aim");
    }
    dot(toScreen(bp(F, -90, 140, -32)), 6, Color{232, 161, 58, 255}, "Drag to move the body");
    if (P.butt.on) dot(toScreen(pocketPt(F)), 7, Color{232, 161, 58, 255}, "Shoulder");
    {   // name the line of sight, a little way out past the eye
        BodyHold h = cur();
        Vector3 e = eyeWorld(h), far = Vector3Transform(W3({h.eye.x + 450, h.eye.y}, (float)h.eyeZ * U), poseGunMatrix());
        (void)e;
        Vector2 s = toScreen(far);
        viewText("Line of sight", s.x + 6, s.y - 18 * CFG.uiScale, 11, Color{255, 120, 110, 255});
    }
    poseRingsDraw();
}

// ---------------------------------------------------------------- rotation rings
// The picked joint's rings: a hand on the gun (its angle round the gun's side, its roll round the grip), an elbow (its
// swing round the shoulder-wrist line), a finger (curl, spread), the body (turn, tilt) and, in an animation, the gun
// (muzzle up, turn, roll). Each axis says what it changes; which way is measured at the press by nudging the value.
namespace {
struct PJ { std::function<void(int, double)> set; std::function<Vector3(int)> probe; };
Rings gPR;
PJ gPJ;
double gPRsign = 1;
Pose gPoseSnap;
std::map<std::string, Animation> gAnimSnap;
void restoreSnap() { A.design.pose = gPoseSnap; A.design.anims = gAnimSnap; }
Vector3 tipOf(const HandGeo& g, int digit) {
    for (auto& b : g.bones) if (b.digit == digit && b.seg == 2) return handPt(g.f, b.b);
    return g.wrist;
}
// set up the rings for joint `id` (A.posePicked's numbering); false if it has none
bool jointFor(int id, Rings& g, PJ& J) {
    g.ax[0] = g.ax[1] = g.ax[2] = {0, 0, 0};
    g.label[0] = g.label[1] = g.label[2] = nullptr;
    const Pose P = viewPose();
    Frame F = bodyFrame();
    auto nrm = [](Vector3 v) { return Vector3Length(v) > 1e-9f ? Vector3Normalize(v) : Vector3{0, 0, 0}; };
    if (id >= 0 && id <= 5) {   // a hand (or the butt) on the gun
        int k = id / 2;
        const HandHold& hv = k == 0 ? P.firing : k == 1 ? P.support : P.butt;
        if (!hv.on) return false;
        double z = partZ(hv.part), an = hv.angle * DEG2RAD;
        g.c = gunPt(hv.at, z);
        g.ax[0] = nrm(Vector3Subtract(gunPt(hv.at, z + 10), g.c)); g.label[0] = "Angle ";
        if (k < 2) { g.ax[1] = nrm(Vector3Subtract(gunPt({hv.at.x + std::cos(an) * 10, hv.at.y + std::sin(an) * 10}, z), g.c)); g.label[1] = "Roll "; }
        J.set = [k](int a, double d) { HandHold& h = *editHand(k); if (a == 0) h.angle += d; else h.twist += d; };
        J.probe = [k](int a) {
            HandHold& h = *editHand(k);
            if (a == 0) { double an2 = h.angle * DEG2RAD; return gunPt({h.at.x + std::cos(an2) * 42, h.at.y + std::sin(an2) * 42}, partZ(h.part)); }
            return tipOf(limbFor(bodyFrame(), h, k == 0 ? 1 : -1).g, 1);
        };
        return true;
    }
    if (id == 8 || id == 9) {   // an elbow: swung round the line from the shoulder to the wrist
        int k = id - 8, side = k == 0 ? 1 : -1;
        Limb L = limbFor(F, k == 0 ? P.firing : P.support, side);
        g.ax[0] = nrm(Vector3Subtract(L.arm.w, L.arm.s)); g.label[0] = "Elbow ";
        g.c = Vector3Add(L.arm.s, Vector3Scale(g.ax[0], Vector3DotProduct(Vector3Subtract(L.arm.e, L.arm.s), g.ax[0])));   // (on the line it swings round)
        J.set = [k](int, double d) { editHand(k)->elbow += d; };
        J.probe = [k, side](int) { return limbFor(bodyFrame(), *editHand(k), side).arm.e; };
        return true;
    }
    if ((id == 12 || id == 13) && A.animEdit) {   // the gun, in an animation
        Vector3 O = gunPt({0, 0}, 0);
        g.c = gunMidWorld();
        g.ax[0] = nrm(Vector3Subtract(gunPt({0, 0}, 100), O)); g.label[0] = "Muzzle up ";
        g.ax[1] = nrm(Vector3Subtract(gunPt({0, -100}, 0), O)); g.label[1] = "Turn ";
        g.ax[2] = nrm(Vector3Subtract(gunPt({100, 0}, 0), O)); g.label[2] = "Roll ";
        J.set = [](int a, double d) { if (AnimKey* key = editKey()) key->gun[3 + a] += d; };
        J.probe = [](int a) { return a < 2 ? gunPt({5000, 0}, 0) : gunPt({0, -500}, 0); };   // (far points: where it pivots hardly matters)
        return true;
    }
    if (id == 6) {   // the body: turned against the gun, and the muzzle tilted
        g.c = F.o;   // (the body turns about the aiming eye)
        g.ax[0] = F.u; g.label[0] = "Turn ";
        g.ax[1] = nrm(Vector3Subtract(gunPt({0, 0}, 100), gunPt({0, 0}, 0))); g.label[1] = "Tilt ";   // (the muzzle tilts about the gun's side)
        J.set = [](int a, double d) { BodyHold& h = editable(); if (a == 0) h.yaw += d; else h.tilt += d; };
        J.probe = [](int a) { return a == 0 ? bp(bodyFrame(), 300, 0, 0) : gunPt({5000, 0}, 0); };   // (far down the bore: where the pivot is hardly matters)
        return true;
    }
    if (id >= 20 && A.poseHand >= 0) {   // a finger: curled (all its joints together) and spread
        int digit = id - 20, k = A.poseHand == 0 ? 0 : 1, side = k == 0 ? 1 : -1;
        Limb L = limbFor(F, k == 0 ? P.firing : P.support, side);
        if (!L.g.on) return false;
        const HandBone* b0 = nullptr;
        for (auto& b : L.g.bones) if (b.digit == digit && b.seg == 0) b0 = &b;
        if (!b0) return false;
        HV dir{b0->b.x - b0->a.x, b0->b.y - b0->a.y, b0->b.z - b0->a.z}, up = b0->up;
        HV side3{dir.y * up.z - dir.z * up.y, dir.z * up.x - dir.x * up.z, dir.x * up.y - dir.y * up.x};
        g.c = handPt(L.g.f, b0->a);
        g.ax[0] = nrm(Vector3Subtract(handPt(L.g.f, {b0->a.x + side3.x, b0->a.y + side3.y, b0->a.z + side3.z}), g.c)); g.label[0] = "Curl ";
        g.ax[1] = nrm(Vector3Subtract(handPt(L.g.f, {b0->a.x + up.x, b0->a.y + up.y, b0->a.z + up.z}), g.c)); g.label[1] = "Spread ";
        J.set = [k, digit](int a, double d) {
            HandHold& h = *editHand(k);
            if (!h.digitsSet) { digitsOf(h, k == 0 ? "firing" : "support", h.digits); h.digitsSet = true; }
            DigitPose& p = h.digits[digit];
            const double share[3] = {0.35, 0.4, 0.25};
            if (a == 0) for (int s2 = 0; s2 < 3; s2++) p.bend[s2] += d * share[s2] * 1.6;   // (the fingertip goes round about as far as the ring)
            else p.spread += d;
            clampDigit(digit, p);
        };
        J.probe = [k, side, digit](int) { return tipOf(limbFor(bodyFrame(), *editHand(k), side).g, digit); };
        return true;
    }
    return false;
}
ToScreen poseTS() { return [](Vector3 w) { return toScreen(w); }; }
// true while the rings have the mouse
bool poseRingsInput(bool overUI) {
    if (gPR.axis >= 0) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { gPR.axis = -1; commit(); return true; }
        double deg = ringsDrag(gPR, GetMousePosition(), IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
        int a = gPR.axis;
        double v = ringsSolve([&](double x) { restoreSnap(); gPJ.set(a, x); return gPJ.probe(a); }, gPR.c, gPR.ax[a], deg, 1.0 / gPRsign);
        restoreSnap();
        gPJ.set(a, v);
        return true;
    }
    gPR.hot = -1;
    if (A.poseDrag >= 0 || A.posePicked < 0) return false;
    PJ J;
    if (!jointFor(A.posePicked, gPR, J)) return false;
    ringsFit(gPR, poseTS(), 55);
    gPR.hot = overUI ? -1 : ringsPick(gPR, poseTS(), GetMousePosition());
    if (gPR.hot < 0 || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    // the press: make sure what's edited exists (an animation's key at this moment), set it aside, and find which way
    // the value turns things
    J.set(gPR.hot, 0);
    gPoseSnap = A.design.pose; gAnimSnap = A.design.anims;
    gPJ = J;
    int a = gPR.hot;
    Vector3 c = gPR.c, ax = gPR.ax[a];
    gPRsign = 1.0 / ringsGain([&](double v) { restoreSnap(); gPJ.set(a, v); return gPJ.probe(a); }, c, ax);
    restoreSnap();
    ringsBegin(gPR, a, poseTS(), GetMousePosition());
    return true;
}
}  // namespace

// (tests) turn joint `id`'s ring `axis` by `deg`, as a drag would
void poseRingTest(int id, int axis, double deg) {
    A.posePicked = id;
    PJ J;
    if (!jointFor(id, gPR, J) || Vector3Length(gPR.ax[axis]) < 0.5f) { std::printf("RINGTEST: no ring %d on %d\n", axis, id); return; }
    J.set(axis, 0);
    gPoseSnap = A.design.pose; gAnimSnap = A.design.anims;
    gPJ = J;
    Vector3 c = gPR.c, ax = gPR.ax[axis];
    Vector3 before = gPJ.probe(axis);
    gPRsign = 1.0 / ringsGain([&](double v) { restoreSnap(); gPJ.set(axis, v); return gPJ.probe(axis); }, c, ax);
    double v = ringsSolve([&](double x) { restoreSnap(); gPJ.set(axis, x); return gPJ.probe(axis); }, c, ax, deg, 1.0 / gPRsign);
    restoreSnap();
    gPJ.set(axis, v);
    Vector3 after = gPJ.probe(axis);
    // how far round the axis the probe went
    Vector3 a0 = Vector3Subtract(before, c), a1 = Vector3Subtract(after, c);
    a0 = Vector3Subtract(a0, Vector3Scale(ax, Vector3DotProduct(a0, ax))); a1 = Vector3Subtract(a1, Vector3Scale(ax, Vector3DotProduct(a1, ax)));
    double turned = std::atan2(Vector3DotProduct(Vector3CrossProduct(a0, a1), ax), Vector3DotProduct(a0, a1)) * RAD2DEG;
    std::printf("RINGTEST joint %d axis %d (%s): asked %+.1f, scale %+.2f, the joint turned %+.1f degrees about the ring's axis\n", id, axis, gPR.label[axis] ? gPR.label[axis] : "", deg, gPRsign, turned);
    commit();
}
static void poseRingsDraw() {
    if (A.S && sim::animPlaying(*A.S)) return;
    PJ J;
    if (gPR.axis < 0) {   // (what's picked now: its rings, where it is now)
        if (A.poseDrag >= 0 || A.posePicked < 0 || !jointFor(A.posePicked, gPR, J)) return;
        ringsFit(gPR, poseTS(), 55);
    }
    ringsDraw(gPR, poseTS(), A.cam.position);
}
void poseInput(bool overUI) {
    if (!(A.S && sim::animPlaying(*A.S)) && poseRingsInput(overUI)) return;
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
        {   // a free drag starts: the plane facing the camera through what's dragged
            Ray r = mouseRay();
            dragN = Vector3Negate(r.direction);
            if (id == 10 || id == 11) { ViewHands VH = viewHands(); dragP0 = sideLimb(bodyFrame(), VH, id - 10).g.wrist; }
            if (id == 12) dragP0 = gunMidWorld();
            if (id == 12 || id == 13) { double g[6]; viewGun(g); for (int q = 0; q < 6; q++) dragStart[q] = g[q]; }
        }
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
        HandGeo g = limbFor(bodyFrame(), h, side).g;
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
    if (A.poseDrag >= 8 && A.poseDrag <= 13) {
        Ray rr = mouseRay();
        auto onDragPlane = [&](Vector3& out) {
            float den = Vector3DotProduct(rr.direction, dragN);
            if (std::fabs(den) < 1e-6f) return false;
            float t = Vector3DotProduct(Vector3Subtract(dragP0, rr.position), dragN) / den;
            if (t < 0) return false;
            out = Vector3Add(rr.position, Vector3Scale(rr.direction, t));
            return true;
        };
        Frame F = bodyFrame();
        if (A.poseDrag == 8 || A.poseDrag == 9) {   // swing the elbow round the shoulder-wrist line
            int k = A.poseDrag - 8, side = k == 0 ? 1 : -1;
            HandHold& h = *editHand(k);
            Limb L = limbFor(F, h, side);
            Vector3 s = L.arm.s, w = L.arm.w, dir = Vector3Normalize(Vector3Subtract(w, s));
            Vector3 pole = elbowPole(F, side > 0);
            Vector3 p0 = Vector3Normalize(Vector3Subtract(pole, Vector3Scale(dir, Vector3DotProduct(pole, dir))));
            Vector3 c = Vector3Lerp(s, w, 0.5f);
            float den = Vector3DotProduct(rr.direction, dir);
            Vector3 q;
            if (std::fabs(den) > 1e-4f) q = Vector3Add(rr.position, Vector3Scale(rr.direction, Vector3DotProduct(Vector3Subtract(c, rr.position), dir) / den));
            else q = Vector3Add(rr.position, Vector3Scale(rr.direction, Vector3Distance(rr.position, c)));
            Vector3 v = Vector3Subtract(q, c);
            v = Vector3Subtract(v, Vector3Scale(dir, Vector3DotProduct(v, dir)));
            if (Vector3Length(v) < 1e-5f) return;
            v = Vector3Normalize(v);
            double th = std::atan2(Vector3DotProduct(Vector3CrossProduct(p0, v), dir), Vector3DotProduct(p0, v)) * RAD2DEG;
            float sg = Vector3DotProduct(Vector3CrossProduct(dir, p0), F.u) >= 0 ? 1.f : -1.f;
            h.elbow = std::round(std::remainder(th * sg, 360.0));
            return;
        }
        if (A.poseDrag == 10 || A.poseDrag == 11) {   // place the hand: off the gun, anywhere
            HandHold& h = *editHand(A.poseDrag - 10);
            Vector3 q;
            if (!onDragPlane(q)) return;
            Vector3 d = Vector3Scale(Vector3Subtract(q, F.o), 1 / U);
            h.on = false; h.free = true;
            h.pos[0] = std::round(Vector3DotProduct(d, F.f)); h.pos[1] = std::round(Vector3DotProduct(d, F.u)); h.pos[2] = std::round(Vector3DotProduct(d, F.r));
            return;
        }
        AnimKey* k = editKey();
        if (!k) return;
        if (A.poseDrag == 12) {   // move the gun (its hands go with it)
            Vector3 q;
            if (!onDragPlane(q)) return;
            Vector3 d = Vector3Scale(Vector3Subtract(q, dragP0), 1 / U);
            k->gun[0] = std::round(dragStart[0] + d.x); k->gun[1] = std::round(dragStart[1] + d.y); k->gun[2] = std::round(dragStart[2] + d.z);
            return;
        }
        Vector2 md = Vector2Subtract(GetMousePosition(), A.posePress);   // aim it: across turns, up and down raises; Shift rolls
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) k->gun[5] = std::round(dragStart[5] + md.x * 0.4);
        else { k->gun[4] = std::round(dragStart[4] + md.x * 0.35); k->gun[3] = std::round(dragStart[3] - md.y * 0.35); }
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
