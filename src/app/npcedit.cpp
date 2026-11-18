// Gunsmith3D - the NPC editor (see npcedit.hpp).
#include "app/npcedit.hpp"
#include "app/gizmo.hpp"
#include "app/charmodel.hpp"
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "app/paths.hpp"
#include "app/settings.hpp"
#include "app/usertex.hpp"
#include "raymath.h"
#include "rlgl.h"
#include "imgui.h"
#include "map.hpp"

#include <algorithm>
#include <functional>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>

namespace fs = std::filesystem;
NpcEdit NE;
static std::string npcAutosave() { return std::string(NPCS_DIR) + "/_editing.json"; }   // what's in the editor, kept between sessions

namespace {
constexpr float S = 0.01f;   // drawing units per millimetre (1 unit = 10 cm)
Camera3D cam{};
Vector3 W(const double p[3]) { return {(float)p[0] * S, (float)p[1] * S, (float)p[2] * S}; }
Vector3 V(const double p[3]) { return {(float)p[0], (float)p[1], (float)p[2]}; }

void updateCam() {
    Vector3 t{0, NE.targetY, 0};
    cam.target = t;
    cam.position = {t.x + NE.dist * std::cos(NE.pitch) * std::sin(NE.yaw), t.y + NE.dist * std::sin(NE.pitch), t.z + NE.dist * std::cos(NE.pitch) * std::cos(NE.yaw)};
    cam.up = {0, 1, 0}; cam.fovy = 40; cam.projection = CAMERA_PERSPECTIVE;
}
float viewW() { return (float)GetScreenWidth() - PANEL_W; }
Ray rayAt(Vector2 m) {   // the 3D view fills the screen left of the panel, under the toolbar
    float vw = viewW(), vh = (float)GetScreenHeight() - TOOLBAR_H;
    // build it by hand for the view's aspect
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    Matrix proj = MatrixPerspective(cam.fovy * DEG2RAD, vw / std::max(1.f, vh), 0.05, 1000);
    float nx = 2 * m.x / vw - 1, ny = 1 - 2 * (m.y - TOOLBAR_H) / vh;
    Vector3 a = Vector3Unproject({nx, ny, -1}, proj, view), b = Vector3Unproject({nx, ny, 1}, proj, view);
    return {a, Vector3Normalize(Vector3Subtract(b, a))};
}
Vector2 toScreenV(Vector3 w) {
    float vw = viewW(), vh = (float)GetScreenHeight() - TOOLBAR_H;
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    Matrix proj = MatrixPerspective(cam.fovy * DEG2RAD, vw / std::max(1.f, vh), 0.05, 1000);
    Quaternion q = {w.x, w.y, w.z, 1};
    q = QuaternionTransform(q, view); q = QuaternionTransform(q, proj);
    if (q.w <= 0) return {-1e5f, -1e5f};
    return {(q.x / q.w + 1) / 2 * vw, TOOLBAR_H + (1 - q.y / q.w) / 2 * vh};
}
// the bone nearest the ray, within its thickness
Rings gRings;   // the selected bone's rotation rings
int pickBone(Ray r) {
    auto P = npcPose(NE.npc);
    int best = -1; float bt = 1e30f;
    for (size_t i = 0; i < P.size(); i++) {
        Vector3 a = W(P[i].a), b = W(P[i].b);
        const NpcBone& bn = NE.npc.bones[i];
        for (int k = 0; k <= 16; k++) {   // along the bone: where the ray passes closest
            float t = k / 16.f;
            Vector3 p = Vector3Lerp(a, b, t);
            float rad = (float)(bn.r0 + (bn.r1 - bn.r0) * t) * S;
            Vector3 to = Vector3Subtract(p, r.position);
            float along = Vector3DotProduct(to, r.direction);
            if (along <= 0) continue;
            float off = Vector3Length(Vector3Subtract(to, Vector3Scale(r.direction, along)));
            if (off < rad && along < bt) { bt = along; best = (int)i; }
        }
    }
    return best;
}
void snapshotIfChanged() {
    std::string now = npcToJson(NE.npc);
    if (now == NE.last) return;
    NE.undo.push_back(NE.last);
    if (NE.undo.size() > 100) NE.undo.erase(NE.undo.begin());
    NE.redo.clear();
    NE.last = now;
    NE.dirty = true;
    std::ofstream(NE.player ? CHARACTER_FILE : npcAutosave()) << now;   // (the character is simply kept: no save as)
}
}  // namespace

void npcCommit() { snapshotIfChanged(); }
void npcFrame() {   // the camera on the whole NPC, whatever its size
    double h = npcHeight(NE.npc), len = 0;
    for (auto& p : npcPose(NE.npc)) len = std::max({len, std::fabs(p.b[2]), std::fabs(p.a[2]), std::fabs(p.b[0])});
    float size = (float)std::max(h, len * 1.6) * S;
    NE.targetY = (float)h * S / 2;
    NE.dist = std::clamp(size * 2.6f, 8.f, 150.f);
}
void npcUndo() {
    if (NE.undo.empty()) return;
    NE.redo.push_back(NE.last);
    NE.last = NE.undo.back(); NE.undo.pop_back();
    npcFromJson(NE.last, NE.npc);
    NE.sel = std::min(NE.sel, (int)NE.npc.bones.size() - 1);
}
void npcRedo() {
    if (NE.redo.empty()) return;
    NE.undo.push_back(NE.last);
    NE.last = NE.redo.back(); NE.redo.pop_back();
    npcFromJson(NE.last, NE.npc);
    NE.sel = std::min(NE.sel, (int)NE.npc.bones.size() - 1);
}

std::vector<std::string> npcFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(NPCS_DIR, ec))
        if (e.path().extension() == ".json" && e.path().stem().string()[0] != '_') out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}
std::vector<std::string> npcGunFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(GUNS_DIR, ec)) {
        if (e.path().extension() != ".json") continue;
        std::ifstream in(e.path()); std::stringstream ss; ss << in.rdbuf();
        Design d;
        if (designFromJson(ss.str(), d) && d.npcGun.on) out.push_back(e.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}
bool npcSave(const std::string& name) {
    std::string n = safeFileName(name.empty() ? NE.npc.name : name);
    if (n.empty()) n = "NPC";
    std::ofstream f(std::string(NPCS_DIR) + "/" + n + ".json");
    if (!f) return false;
    f << npcToJson(NE.npc);
    NE.file = n; NE.dirty = false;
    return true;
}
bool npcOpen(const std::string& file) {
    std::ifstream in(std::string(NPCS_DIR) + "/" + file + ".json");
    std::stringstream ss; ss << in.rdbuf();
    NpcDef n;
    if (!npcFromJson(ss.str(), n)) return false;
    NE.npc = n; NE.sel = 0; NE.file = file; NE.dirty = false; NE.endPicked = false;
    snapshotIfChanged();
    NE.dirty = false;
    npcFrame();
    return true;
}

void enterNpcEditor() {
    A.screen = Screen::NpcEditor;
    A.showPause = A.showSettings = false;
    setupUserFolders();
    static bool loaded = false;
    if (!loaded) {   // what was being edited last time, or a person to start from
        loaded = true;
        std::ifstream in(npcAutosave());
        std::stringstream ss; ss << in.rdbuf();
        if (!in || !npcFromJson(ss.str(), NE.npc)) NE.npc = npcPreset("human");
        NE.last = npcToJson(NE.npc);
    }
    npcFrame();
    setLighting(Lighting{});
    EnableCursor();
}
namespace { NpcEdit npcStash; bool stashed = false; int characterVersion = 0; }
void leaveNpcEditor() {
    if (NE.player) {   // the character is kept; the NPC editor comes back as it was
        std::ofstream(CHARACTER_FILE) << npcToJson(NE.npc);
        characterVersion++;
        if (stashed) { NE = npcStash; stashed = false; } else NE = NpcEdit();
        return;
    }
    std::ofstream(npcAutosave()) << npcToJson(NE.npc);
}
void enterCharacterEditor() {
    enterNpcEditor();   // (loads the NPC editor's own work the first time, so it can be set aside)
    npcStash = NE; stashed = true;
    NE = NpcEdit();
    NE.player = true;
    std::ifstream in(CHARACTER_FILE);
    std::stringstream ss; ss << in.rdbuf();
    if (!in || !npcFromJson(ss.str(), NE.npc)) { CharLook L; L.on = true; NE.npc = characterFromLook(L); NE.npc.name = "My character"; }
    else if (NE.npc.look.on) { std::string nm = NE.npc.name; NE.npc = characterFromLook(NE.npc.look, &NE.npc); NE.npc.name = nm; }   // (built again: newer games build it better)
    // a human: any of a human's bones missing (an older or hand-edited file) and it starts again from the human
    for (auto& b : npcPreset("human").bones) {
        bool has = false;
        for (auto& o : NE.npc.bones) if (o.name == b.name) has = true;
        if (!has) { std::string nm = NE.npc.name; NE.npc = npcPreset("human"); NE.npc.name = nm; break; }
    }
    NE.last = npcToJson(NE.npc);
    std::ofstream(CHARACTER_FILE) << NE.last;
    characterVersion++;
    npcFrame();
}
const NpcDef* playerCharacter() {
    static NpcDef ch; static bool have = false; static int seen = -1; static double checked = -10;
    if (NE.player && A.screen == Screen::NpcEditor) return &NE.npc;
    if (seen != characterVersion || GetTime() - checked > 2) {   // (a file dropped in by hand is picked up too)
        seen = characterVersion; checked = GetTime();
        std::ifstream in(CHARACTER_FILE);
        std::stringstream ss; ss << in.rdbuf();
        have = in && npcFromJson(ss.str(), ch);
    }
    return have ? &ch : nullptr;
}

void npcEditorUpdate(float dt, bool modal) {
    (void)dt;
    updateCam();
    if (modal) return;
    ImGuiIO& io = ImGui::GetIO();
    Vector2 m = GetMousePosition();
    bool overUI = io.WantCaptureMouse || m.x > viewW() || m.y < TOOLBAR_H;
    if (IsKeyPressed(KEY_ESCAPE)) { A.showPause = true; return; }
    if (!io.WantCaptureKeyboard) {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (ctrl && IsKeyPressed(KEY_Z)) npcUndo();
        if (ctrl && IsKeyPressed(KEY_Y)) npcRedo();
        if (IsKeyPressed(KEY_DELETE) && NE.sel > 0 && !(NE.player && npcCoreBone(NE.npc.bones[NE.sel].name))) { npcRemoveBone(NE.npc, NE.sel); NE.sel = 0; npcCommit(); }
    }
    // the camera: right drag turns round it, the wheel zooms, middle drag raises and lowers
    if (!overUI) {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { Vector2 d = GetMouseDelta(); NE.yaw -= d.x * 0.008f; NE.pitch = std::clamp(NE.pitch + d.y * 0.006f, -1.4f, 1.4f); }
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) NE.targetY += GetMouseDelta().y * 0.02f * NE.dist / 30;
        float wh = GetMouseWheelMove();
        if (wh != 0) NE.dist = std::clamp(NE.dist * std::pow(0.88f, wh), 3.f, 200.f);
    }
    if (NE.sel >= (int)NE.npc.bones.size()) NE.sel = 0;
    auto P = npcPose(NE.npc);
    {   // the selected bone's rotation rings, at its joint: drag one to turn the bone about that axis
        static NpcDef snap;
        ToScreen ts = [](Vector3 w) { return toScreenV(w); };
        if (gRings.axis < 0) {
            gRings.c = W(P[NE.sel].a);
            gRings.ax[0] = Vector3Normalize(V(P[NE.sel].x)); gRings.ax[1] = Vector3Normalize(V(P[NE.sel].y)); gRings.ax[2] = Vector3Normalize(V(P[NE.sel].z));
            gRings.label[0] = "Bend "; gRings.label[1] = "Twist "; gRings.label[2] = "Swing ";
            ringsFit(gRings, ts, 55);
        }
        if (gRings.axis >= 0) {
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { gRings.axis = -1; npcCommit(); return; }
            double deg = ringsDrag(gRings, m, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
            NE.npc = snap;
            Vector3 a = gRings.ax[gRings.axis];
            double ax[3] = {a.x, a.y, a.z};
            npcRotateBone(NE.npc, NE.sel, ax, deg);
            return;
        }
        gRings.hot = overUI || NE.aiming ? -1 : ringsPick(gRings, ts, m);
        if (gRings.hot >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { snap = NE.npc; ringsBegin(gRings, gRings.hot, ts, m); return; }
    }
    Vector2 endS = toScreenV(W(P[NE.sel].b));
    if (NE.aiming) {   // the bone points at the mouse, in the plane through its end facing you (Shift: and stretches to it)
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { NE.aiming = false; npcCommit(); return; }
        Ray r = rayAt(m);
        Vector3 e = W(P[NE.sel].b), n = Vector3Normalize(Vector3Subtract(cam.position, e));
        float den = Vector3DotProduct(r.direction, n);
        if (std::fabs(den) < 1e-5f) return;
        float t = Vector3DotProduct(Vector3Subtract(e, r.position), n) / den;
        Vector3 hit = Vector3Add(r.position, Vector3Scale(r.direction, t));
        Vector3 a = W(P[NE.sel].a);
        Vector3 d = Vector3Subtract(hit, a);
        double dir[3] = {d.x, d.y, d.z};
        npcAimBone(NE.npc, NE.sel, dir);
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) NE.npc.bones[NE.sel].len = std::max(5.0, (double)Vector3Length(d) / S);
        return;
    }
    if (overUI || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    if (Vector2Distance(m, endS) < 10) {   // the selected bone's end: pick it, then press again and drag to aim the bone
        if (!NE.endPicked) { NE.endPicked = true; return; }
        NE.aiming = true;
        return;
    }
    int b = pickBone(rayAt(m));
    if (b >= 0 && b != NE.sel) { NE.sel = b; NE.endPicked = false; }
    else if (b < 0) NE.endPicked = false;
}

// ---------------------------------------------------------------- how a bone looks
// Each bone is a smooth mesh in its own frame (millimetres; y along it from its start, x its right, z its front):
// rings round its length, each ring the cross-section (round, or a rounded square for boxes and wedges) stretched by
// its width and depth, swelled by its bulge, closed by rounded or flat ends. Built once per shape and size and kept.
namespace {
Mesh buildBoneMesh(const NpcBone& b) {
    const bool small = std::max(b.r0, b.r1) * std::max(b.width, b.depth) < 15 && b.len < 80;   // (a finger, an eye: fewer sides will do)
    const int SEG = small ? 16 : 40;
    const double PI_D = 3.14159265358979;
    std::string sh = b.shape;
    bool squarish = sh == "box" || sh == "wedge";
    double ex = squarish ? 6 : 2;   // the cross-section: an ellipse, or a rounded square
    struct Ring { double y, r, dk; };   // where along, its radius, its depth factor (a wedge thins out)
    std::vector<Ring> rings;
    const int BODY = small ? 6 : 20, CAP = small ? 4 : 10;
    double L = b.len;
    if (sh == "ball") {
        double R = (b.r0 + b.r1) / 2;
        for (int j = 0; j <= BODY + 4; j++) {
            double ph = PI_D * j / (BODY + 4);
            rings.push_back({L / 2 - std::cos(ph) * L / 2, std::sin(ph) * R * (1 + b.bulge * std::sin(ph)), 1});
        }
    } else {
        if (sh == "capsule")
            for (int j = 0; j < CAP; j++) { double ph = PI_D / 2 * j / CAP; rings.push_back({-b.r0 * std::cos(ph), b.r0 * std::sin(ph), 1}); }
        for (int j = 0; j <= BODY; j++) {
            double t = (double)j / BODY;
            rings.push_back({L * t, npcBoneRadius(b, t), sh == "wedge" ? std::max(0.03, 1 - t) : 1.0});
        }
        if (sh == "capsule")
            for (int j = 1; j <= CAP; j++) { double ph = PI_D / 2 * j / CAP; rings.push_back({L + b.r1 * std::sin(ph), b.r1 * std::cos(ph), 1}); }
    }
    double perim = 0;   // (texture: one copy across texScale mm, round the widest ring)
    for (auto& r : rings) perim = std::max(perim, 2 * PI_D * r.r * (b.width + b.depth) / 2);
    std::vector<float> P, N, T;
    std::vector<unsigned short> I;
    auto cs = [&](double th, double& cx, double& cz) {
        double c = std::cos(th), s = std::sin(th);
        cx = (c < 0 ? -1 : 1) * std::pow(std::fabs(c), 2 / ex);
        cz = (s < 0 ? -1 : 1) * std::pow(std::fabs(s), 2 / ex);
    };
    for (auto& r : rings)
        for (int q = 0; q <= SEG; q++) {
            double th = 2 * PI_D * q / SEG, cx, cz;
            cs(th, cx, cz);
            P.insert(P.end(), {(float)(cx * r.r * b.width), (float)r.y, (float)(cz * r.r * b.depth * r.dk)});
            N.insert(N.end(), {0, 0, 0});
            T.insert(T.end(), {(float)(perim / b.texScale * q / SEG), (float)(r.y / b.texScale)});
        }
    int W = SEG + 1;
    for (size_t j = 0; j + 1 < rings.size(); j++)
        for (int q = 0; q < SEG; q++) {
            unsigned short a = (unsigned short)(j * W + q), c = (unsigned short)(a + W);
            I.insert(I.end(), {a, c, (unsigned short)(a + 1), (unsigned short)(a + 1), c, (unsigned short)(c + 1)});   // (counter-clockwise from outside)
        }
    // smooth normals: each corner's faces added up (by area), the seam's two copies shared
    auto V = [&](int k) { return Vector3{P[k * 3], P[k * 3 + 1], P[k * 3 + 2]}; };
    for (size_t t = 0; t < I.size(); t += 3) {
        Vector3 n = Vector3CrossProduct(Vector3Subtract(V(I[t + 1]), V(I[t])), Vector3Subtract(V(I[t + 2]), V(I[t])));
        for (int k = 0; k < 3; k++) { N[I[t + k] * 3] += n.x; N[I[t + k] * 3 + 1] += n.y; N[I[t + k] * 3 + 2] += n.z; }
    }
    for (size_t j = 0; j < rings.size(); j++)
        for (int k = 0; k < 3; k++) { float s = N[(j * W) * 3 + k] + N[(j * W + SEG) * 3 + k]; N[(j * W) * 3 + k] = N[(j * W + SEG) * 3 + k] = s; }
    // a pole (a ring shrunk to a point): pointing straight out along the bone
    for (size_t j = 0; j < rings.size(); j++)
        if (rings[j].r < 1e-6)
            for (int q = 0; q <= SEG; q++) { size_t v = (j * W + q) * 3; N[v] = 0; N[v + 1] = j == 0 ? -1.f : 1.f; N[v + 2] = 0; }
    for (size_t v = 0; v < N.size(); v += 3) {
        float l = std::sqrt(N[v] * N[v] + N[v + 1] * N[v + 1] + N[v + 2] * N[v + 2]);
        if (l > 1e-12f) { N[v] /= l; N[v + 1] /= l; N[v + 2] /= l; } else N[v + 1] = 1;
    }
    // flat ends (cylinders, boxes, wedges): a disc at each end, its own corners so it stays sharp
    if (sh == "cylinder" || squarish)
        for (int end = 0; end < 2; end++) {
            const Ring& r = end == 0 ? rings.front() : rings.back();
            if (r.r < 1e-6) continue;
            float ny = end == 0 ? -1.f : 1.f;
            unsigned short c0 = (unsigned short)(P.size() / 3);
            P.insert(P.end(), {0, (float)r.y, 0}); N.insert(N.end(), {0, ny, 0}); T.insert(T.end(), {0, 0});
            for (int q = 0; q <= SEG; q++) {
                double th = 2 * PI_D * q / SEG, cx, cz;
                cs(th, cx, cz);
                float x = (float)(cx * r.r * b.width), z = (float)(cz * r.r * b.depth * r.dk);
                P.insert(P.end(), {x, (float)r.y, z}); N.insert(N.end(), {0, ny, 0});
                T.insert(T.end(), {(float)(x / b.texScale), (float)(z / b.texScale)});
            }
            for (int q = 0; q < SEG; q++) {
                unsigned short a = (unsigned short)(c0 + 1 + q);
                if (end == 0) I.insert(I.end(), {c0, a, (unsigned short)(a + 1)});
                else I.insert(I.end(), {c0, (unsigned short)(a + 1), a});
            }
        }
    Mesh m{};
    m.vertexCount = (int)P.size() / 3;
    m.triangleCount = (int)I.size() / 3;
    m.vertices = (float*)MemAlloc((unsigned)(P.size() * sizeof(float)));
    m.normals = (float*)MemAlloc((unsigned)(N.size() * sizeof(float)));
    m.texcoords = (float*)MemAlloc((unsigned)(T.size() * sizeof(float)));
    m.indices = (unsigned short*)MemAlloc((unsigned)(I.size() * sizeof(unsigned short)));
    std::copy(P.begin(), P.end(), m.vertices);
    std::copy(N.begin(), N.end(), m.normals);
    std::copy(T.begin(), T.end(), m.texcoords);
    std::copy(I.begin(), I.end(), m.indices);
    UploadMesh(&m, false);
    return m;
}
struct CachedMesh { Mesh mesh; double used; };
std::unordered_map<std::string, CachedMesh> gBoneMeshes;
const Mesh& boneMesh(const NpcBone& b) {
    char key[200];
    std::snprintf(key, sizeof key, "%s|%.2f|%.2f|%.2f|%.3f|%.3f|%.3f|%.1f", b.shape.c_str(), b.len, b.r0, b.r1, b.width, b.depth, b.bulge, b.texScale);
    auto it = gBoneMeshes.find(key);
    if (it == gBoneMeshes.end()) {
        if (gBoneMeshes.size() > 400)   // (sizes tried while editing, long gone: let them go)
            for (auto e = gBoneMeshes.begin(); e != gBoneMeshes.end();)
                if (GetTime() - e->second.used > 3) { UnloadMesh(e->second.mesh); e = gBoneMeshes.erase(e); } else ++e;
        it = gBoneMeshes.emplace(key, CachedMesh{buildBoneMesh(b), 0}).first;
    }
    it->second.used = GetTime();
    return it->second.mesh;
}
}  // namespace

void drawNpcBone(const NpcBone& b, Matrix M, Color c) {
    const Mesh& mesh = boneMesh(b);
    if (!b.texture.empty()) drawMeshTex(mesh, M, c, userTexture(b.texture));
    else drawMesh(mesh, M, c);
}
void drawNpcAt(const NpcDef& n, Vector3 origin, float yawDeg, float k, int highlight, Vector3 fallAxis, float fallAngle) {
    auto P = npcPose(n);
    // the NPC's own frame faces +z; turned so that it faces yawDeg (0 along +x); fallen: turned about its feet
    float th = (90 - yawDeg) * DEG2RAD, c0 = std::cos(th), s0 = std::sin(th);
    auto axis = [&](const double v[3]) { return Vector3{(float)v[0] * c0 + (float)v[2] * s0, (float)v[1], -(float)v[0] * s0 + (float)v[2] * c0}; };
    Matrix fall = fallAngle != 0 && Vector3Length(fallAxis) > 1e-6f ? MatrixRotate(Vector3Normalize(fallAxis), fallAngle) : MatrixIdentity();
    Matrix toWorld = MatrixMultiply(fall, MatrixTranslate(origin.x, origin.y, origin.z));
    auto frame = [&](int i, const double at[3], Vector3 X, Vector3 Y, Vector3 Z) {
        Vector3 a = axis(at);
        Matrix R = {X.x, Y.x, Z.x, 0, X.y, Y.y, Z.y, 0, X.z, Y.z, Z.z, 0, 0, 0, 0, 1};
        (void)i;
        return MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(k, k, k), R), MatrixTranslate(a.x * k, a.y * k, a.z * k)), toWorld);
    };
    if (!n.model.empty() && charModelUsable(n)) {   // a modelled character: its skin over these bones, instead of their shapes
        std::vector<Matrix> M(P.size());
        for (size_t i = 0; i < P.size(); i++) M[i] = frame((int)i, P[i].a, axis(P[i].x), axis(P[i].y), axis(P[i].z));
        if (highlight >= 0) { Color h{255, 214, 120, 255}; drawNpcBone(n.bones[highlight], M[highlight], h); }   // (the picked bone, over it)
        drawCharModel(n, M);
        return;
    }
    std::vector<Matrix> bodyM(n.look.on ? P.size() : 0);
    for (size_t i = 0; i < P.size(); i++) {
        const NpcBone& b = n.bones[i];
        if (npcBodyBone(n, (int)i)) {   // (part of the one-piece body, drawn below)
            bodyM[i] = frame((int)i, P[i].a, axis(P[i].x), axis(P[i].y), axis(P[i].z));
            continue;
        }
        if (n.look.on) bodyM[i] = frame((int)i, P[i].a, axis(P[i].x), axis(P[i].y), axis(P[i].z));
        Color c = hexColor(b.color);
        if ((int)i == highlight) c = Color{(unsigned char)std::min(255, c.r + 50), (unsigned char)std::min(255, c.g + 40), (unsigned char)std::min(255, c.b + 10), 255};
        Vector3 X = axis(P[i].x), Y = axis(P[i].y), Z = axis(P[i].z);
        Matrix M = frame((int)i, P[i].a, X, Y, Z);
        const Mesh& mesh = boneMesh(b);
        if (!b.texture.empty()) drawMeshTex(mesh, M, c, userTexture(b.texture));
        else drawMesh(mesh, M, c);
        // a joint: a bone with flat or square ends, hanging off the end of another, gets a rounded knuckle where they
        // meet, so an elbow or a knee bends without a gap
        if (i > 0 && b.at > 0.97 && std::fabs(b.side) < 1 && b.shape != "capsule" && b.shape != "ball") {
            const NpcBone& pb = n.bones[b.parent];
            double r = std::min(b.r0, npcBoneRadius(pb, 1)) * 0.98;
            Matrix J = MatrixMultiply(MatrixMultiply(MatrixScale((float)(r * std::min(b.width, pb.width)), (float)r, (float)(r * std::min(b.depth, pb.depth))),
                                                     Matrix{X.x, Y.x, Z.x, 0, X.y, Y.y, Z.y, 0, X.z, Y.z, Z.z, 0, 0, 0, 0, 1}),
                                      MatrixTranslate(0, 0, 0));
            Vector3 a = axis(P[i].a);
            J = MatrixMultiply(MatrixMultiply(J, MatrixScale(k, k, k)), MatrixMultiply(MatrixTranslate(a.x * k, a.y * k, a.z * k), toWorld));
            if (!b.texture.empty()) drawMeshTex(G.sphere, J, c, userTexture(b.texture));
            else drawMesh(G.sphere, J, c);
        }
    }
    if (n.look.on) drawCharacterBody(n, bodyM);   // the torso, neck, arms and legs: one skin
}

bool npcByName(const gmap::Map& m, const std::string& name, NpcDef& out) {
    for (auto& [nm, text] : m.npcs) if (nm == name && npcFromJson(text, out)) return true;
    std::ifstream in(std::string(NPCS_DIR) + "/" + safeFileName(name) + ".json");
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    return npcFromJson(ss.str(), out);
}
void embedMapNpcs(gmap::Map& m) {
    std::vector<std::pair<std::string, std::string>> keep, guns;
    auto readFile = [](const std::string& path, std::string& out) { std::ifstream in(path); if (!in) return false; std::stringstream ss; ss << in.rdbuf(); out = ss.str(); return true; };
    for (auto& e : m.ents) {
        if (e.type != "npc_spawn" || e.ref.empty()) continue;
        bool have = false;
        for (auto& k : keep) if (k.first == e.ref) have = true;
        if (have) continue;
        std::string text;
        NpcDef t;
        if (readFile(std::string(NPCS_DIR) + "/" + safeFileName(e.ref) + ".json", text) && npcFromJson(text, t)) keep.push_back({e.ref, text});   // the latest version, if it's here
        else for (auto& [nm, tx] : m.npcs) if (nm == e.ref) { keep.push_back({nm, tx}); npcFromJson(tx, t); break; }   // else the copy it came with
        if (t.attack == "ranged" && !t.gun.empty()) {   // and the gun it carries
            bool got = false;
            for (auto& g : guns) if (g.first == t.gun) got = true;
            if (got) continue;
            std::string gt;
            if (readFile(std::string(GUNS_DIR) + "/" + t.gun + ".json", gt)) guns.push_back({t.gun, gt});
            else for (auto& [nm, tx] : m.npcGuns) if (nm == t.gun) { guns.push_back({nm, tx}); break; }
        }
    }
    m.npcs = keep;
    m.npcGuns = guns;
}
int installMapNpcs(const gmap::Map& m) {
    int n = 0;
    std::error_code ec;
    fs::create_directories(NPCS_DIR, ec);
    for (auto& [nm, text] : m.npcs) {
        std::string path = std::string(NPCS_DIR) + "/" + safeFileName(nm) + ".json";
        if (fs::exists(path, ec)) continue;
        std::ofstream(path) << text;
        n++;
    }
    for (auto& [nm, text] : m.npcGuns) {   // their guns, into user/guns
        std::string path = std::string(GUNS_DIR) + "/" + nm + ".json";
        if (fs::exists(path, ec)) continue;
        std::ofstream(path) << text;
    }
    return n;
}
bool npcGunByName(const gmap::Map& m, const std::string& name, Design& out) {
    for (auto& [nm, text] : m.npcGuns) if (nm == name && designFromJson(text, out)) return true;
    std::ifstream in(std::string(GUNS_DIR) + "/" + name + ".json");
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    return designFromJson(ss.str(), out);
}

void npcEditorRender() {
    updateCam();
    float vw = viewW(), vh = (float)GetScreenHeight() - TOOLBAR_H;
    DrawRectangleGradientV(0, (int)TOOLBAR_H, (int)vw, (int)vh, Color{44, 50, 60, 255}, Color{22, 25, 30, 255});
    BeginMode3D(cam);
    float sx = (float)GetRenderWidth() / GetScreenWidth(), sy = (float)GetRenderHeight() / GetScreenHeight();
    rlViewport(0, (int)(GetRenderHeight() - (TOOLBAR_H + vh) * sy), (int)(vw * sx), (int)(vh * sy));
    rlMatrixMode(RL_PROJECTION); rlLoadIdentity();
    {
        double top = 0.05 * std::tan(cam.fovy * 0.5 * DEG2RAD), right = top * vw / std::max(1.f, vh);
        rlFrustum(-right, right, -top, top, 0.05, 1000);
    }
    rlMatrixMode(RL_MODELVIEW);
    float vp[3] = {cam.position.x, cam.position.y, cam.position.z};
    SetShaderValue(G.lit, G.locView, vp, SHADER_UNIFORM_VEC3);
    // the floor, a 1 m grid, and a 1.8 m pole for scale
    drawBox({0, -0.05f, 0}, {40, 0.1f, 40}, Color{58, 62, 68, 255});
    for (int k = -10; k <= 10; k++) {
        DrawLine3D({(float)k, 0.01f, -10}, {(float)k, 0.01f, 10}, Color{90, 96, 106, 255});
        DrawLine3D({-10, 0.01f, (float)k}, {10, 0.01f, (float)k}, Color{90, 96, 106, 255});
    }
    drawCyl({-12, 0, 0}, {-12, 18, 0}, 0.08f, Color{200, 120, 60, 255});
    for (int k = 0; k <= 18; k += 5) drawCyl({-12, (float)k, 0}, {-11.4f, (float)k, 0}, 0.05f, Color{200, 120, 60, 255});
    // the bones
    auto P = npcPose(NE.npc);
    drawNpcAt(NE.npc, {0, 0, 0}, 90, S, NE.sel);
    EndMode3D();
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());   // (back to the whole window for the panels)
    // the selected bone: its end's dot (drag it to aim the bone) and its name
    if (NE.sel < (int)P.size()) {
        Vector2 s = toScreenV(W(P[NE.sel].b));
        Vector2 m = GetMousePosition();
        bool hot = Vector2Distance(s, m) < 10 || NE.endPicked || NE.aiming;
        Color y{255, 205, 60, 255};
        DrawCircleV(s, hot ? 7.f : 5.f, Color{20, 24, 32, 230});
        DrawCircleLinesV(s, hot ? 7.f : 5.f, y);
        const char* t = NE.aiming ? "Aiming the bone (Shift: stretch it to the mouse)"
                      : NE.endPicked ? "Picked: press again and drag to point the bone (Shift: stretch)"
                      : "Click the dot to pick it, then drag it to pose the bone";
        if (hot) DrawText(t, (int)s.x + 12, (int)s.y - 6, 14, y);
        Vector2 a = toScreenV(W(P[NE.sel].a));
        DrawText(NE.npc.bones[NE.sel].name.c_str(), (int)a.x + 10, (int)a.y + 6, 14, Color{230, 232, 236, 255});
    }
    if (NE.sel < (int)P.size()) ringsDraw(gRings, [](Vector3 w) { return toScreenV(w); }, cam.position);
    DrawText("Right drag: turn   Wheel: zoom   Middle drag: up and down   Click a bone to select it   Drag a ring to turn it (Ctrl: 15\xc2\xb0 steps)   Ctrl+Z: undo", 12, GetScreenHeight() - 26, 14, Color{170, 176, 186, 255});
    DrawText("1.8 m", (int)toScreenV({-12, 18.3f, 0}).x - 16, (int)toScreenV({-12, 18.3f, 0}).y - 16, 14, Color{220, 150, 90, 255});
}

// ---------------------------------------------------------------- the one-piece body
// The character maker's torso, neck, arms and legs as one continuous skin: rings of points round the bones, joined
// into tubes that run through the joints (a ring at a joint follows both bones half and half, so an elbow or a knee
// bends without a gap), smooth-shaded. Clothes are colours on it: a sleeve is where the shirt's colour stops. The
// head is sculpted (npcHeadPoint in the core), with its lips, eyebrows and hair painted on and a shell of hair over
// it; it doesn't bend, so it's built once per look and kept.
namespace {
struct RingAt { int a = -1, b = -1; double ta = 0, tb = 0, w = 0, shrink = 1, bust = 0, ex = 0; };   // where: on bone a at ta, blended toward b at tb
                                                                                       // (bust: how far its front swells, in two curves; ex: roundness across, 0 = the bone's own)
struct Tube { std::vector<RingAt> rings; std::vector<Color> cols; int sides = 8; };
int boneNamed(const NpcDef& n, const std::string& nm) { for (int i = 0; i < (int)n.bones.size(); i++) if (n.bones[i].name == nm) return i; return -1; }

unsigned colKey(Color c) { return ((unsigned)c.r << 24) | ((unsigned)c.g << 16) | ((unsigned)c.b << 8) | c.a; }
struct Buckets { std::map<unsigned, std::vector<float>> pos, nrm; std::map<unsigned, Color> col; };

// A closed grid of points (rows along, columns round), each quad its own colour, smooth normals (a vertex's normal from
// its neighbours, pointing away from its row's middle), the ends closed with fans.
void emitGrid(const std::vector<std::vector<Vector3>>& P, const std::vector<std::vector<Color>>& C, Buckets& B, bool caps = true) {
    const int R = (int)P.size();
    if (R < 2) return;
    const int K = (int)P[0].size();
    std::vector<Vector3> mid(R, Vector3{0, 0, 0});
    for (int i = 0; i < R; i++) { for (auto& v : P[i]) mid[i] = Vector3Add(mid[i], v); mid[i] = Vector3Scale(mid[i], 1.f / K); }
    std::vector<std::vector<Vector3>> N(R, std::vector<Vector3>(K));
    for (int i = 0; i < R; i++)
        for (int k = 0; k < K; k++) {
            Vector3 round = Vector3Subtract(P[i][(k + 1) % K], P[i][(k + K - 1) % K]);
            Vector3 along = Vector3Subtract(P[std::min(i + 1, R - 1)][k], P[std::max(i - 1, 0)][k]);
            Vector3 nn = Vector3CrossProduct(round, along);
            Vector3 out = Vector3Subtract(P[i][k], mid[i]);
            if (Vector3Length(nn) < 1e-9f || Vector3Length(round) < 1e-6f * Vector3Length(along)) {   // (a row shrunk to a point: away from the next row's middle)
                int j = i == 0 ? 1 : R - 2;
                nn = Vector3Subtract(P[i][k], mid[j]);
            } else if (Vector3DotProduct(nn, out) < 0) nn = Vector3Negate(nn);
            N[i][k] = Vector3Normalize(nn);
        }
    auto put = [&](Color c, int i0, int k0, int i1, int k1, int i2, int k2) {
        unsigned key = colKey(c); B.col[key] = c;
        auto& p = B.pos[key]; auto& n = B.nrm[key];
        for (auto [i, k] : {std::pair{i0, k0}, std::pair{i1, k1}, std::pair{i2, k2}}) {
            p.insert(p.end(), {P[i][k].x, P[i][k].y, P[i][k].z});
            n.insert(n.end(), {N[i][k].x, N[i][k].y, N[i][k].z});
        }
    };
    for (int i = 0; i + 1 < R; i++)
        for (int k = 0; k < K; k++) {
            int k2 = (k + 1) % K;
            Color c = C[std::min<size_t>(i, C.size() - 1)][k];
            // (wound so the outside faces out: checked against the normal)
            Vector3 f = Vector3CrossProduct(Vector3Subtract(P[i + 1][k], P[i][k]), Vector3Subtract(P[i + 1][k2], P[i][k]));
            if (Vector3DotProduct(f, N[i][k]) + Vector3DotProduct(f, N[i + 1][k2]) >= 0) { put(c, i, k, i + 1, k, i + 1, k2); put(c, i, k, i + 1, k2, i, k2); }
            else { put(c, i, k, i + 1, k2, i + 1, k); put(c, i, k, i, k2, i + 1, k2); }
        }
    if (!caps) return;
    for (int end = 0; end < 2; end++) {   // (inside the body, but never open)
        int i = end ? R - 1 : 0;
        Color c = end ? C.back()[0] : C.front()[0];
        unsigned key = colKey(c); B.col[key] = c;
        auto& p = B.pos[key]; auto& n = B.nrm[key];
        Vector3 axis = Vector3Normalize(Vector3Subtract(mid[i], mid[end ? R - 2 : 1]));
        for (int k = 0; k < K; k++) {
            int k2 = (k + 1) % K;
            Vector3 a = P[i][k], b = P[i][k2];
            Vector3 f = Vector3CrossProduct(Vector3Subtract(a, mid[i]), Vector3Subtract(b, mid[i]));
            if (Vector3DotProduct(f, axis) < 0) std::swap(a, b);
            for (Vector3 v : {mid[i], a, b}) { p.insert(p.end(), {v.x, v.y, v.z}); n.insert(n.end(), {axis.x, axis.y, axis.z}); }
        }
    }
}

Mesh meshOf(const std::vector<float>& P, const std::vector<float>& N, bool own) {
    Mesh m{};
    m.vertexCount = (int)P.size() / 3; m.triangleCount = m.vertexCount / 3;
    if (own) {   // (kept: raylib frees these with the mesh)
        m.vertices = (float*)RL_MALLOC(P.size() * sizeof(float)); std::copy(P.begin(), P.end(), m.vertices);
        m.normals = (float*)RL_MALLOC(N.size() * sizeof(float)); std::copy(N.begin(), N.end(), m.normals);
        m.texcoords = (float*)RL_CALLOC(m.vertexCount * 2, sizeof(float));
    } else { m.vertices = const_cast<float*>(P.data()); m.normals = const_cast<float*>(N.data()); }
    return m;
}

Color mix(Color a, Color b, float t) {
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

// the head: its sculpted surface with its skin painted on (lips, eyebrows, the hairline, a buzz cut's stubble) and a
// shell of hair over the top; in the head bone's frame (mm), kept per look
struct HeadMesh { Mesh head{}, hair{}, drape{}; bool hasHair = false, hasDrape = false; Texture2D tex{}; double used = 0; };
std::map<std::string, HeadMesh> gHeads;

double hairline(double ang) {   // how far up the head (t) the hair starts, all the way round: high on the forehead, low at the back
    double f = std::fabs(ang) / PI;
    if (f < 0.2) return 0.745 - 0.03 * f / 0.2;
    if (f < 0.42) return 0.715 - 0.115 * (f - 0.2) / 0.22;   // (the temples down to above the ears)
    return 0.6 - 0.27 * std::min(1.0, (f - 0.42) / 0.5);
}
double ssf(double a, double b, double x) { double t = std::clamp((x - a) / (b - a), 0.0, 1.0); return t * t * (3 - 2 * t); }

// the paint: u round the head (0 and 1 at the back, 0.5 straight ahead), v up it (= t)
Image headPaint(const NpcBone& h, const CharLook& L) {
    const int W = 512, H = 256;
    Image img = GenImageColor(W, H, WHITE);
    Color* px = (Color*)img.data;
    const Color skin = hexColor(L.skin), hair = hexColor(L.hairColor);
    const Color lips = mix(skin, Color{165, 68, 72, 255}, 0.5f), brows = mix(hair, Color{24, 18, 16, 255}, 0.25f);
    const Color lid = mix(skin, Color{60, 40, 36, 255}, 0.35f);
    const bool fem = L.body == "female";
    const double R = (h.r0 + h.r1) / 2;
    for (int j = 0; j < H; j++) {
        double t = (j + 0.5) / H;
        double u = 2 * t - 1, prof = std::pow(std::max(0.0, 1 - u * u), t > 0.5 ? 0.5 : 0.42) * (1 + h.bulge * std::sin(PI * t));
        double wx = 0.8 + 0.2 * ssf(0.08, 0.5, t);
        for (int i = 0; i < W; i++) {
            double ang = ((i + 0.5) / W - 0.5) * 2 * PI;
            double ax = std::sin(ang) * prof * wx * (R / R), fax = std::fabs(ax);
            float r = skin.r, g = skin.g, b = skin.b;
            auto blend = [&](Color c, double a) { a = std::clamp(a, 0.0, 1.0); r += (c.r - r) * a; g += (c.g - g) * a; b += (c.b - b) * a; };
            if (std::cos(ang) > 0.2) {
                // a touch of colour on the cheeks and the nose's tip; shade under the brow
                blend(Color{200, 110, 100, 255}, 0.07 * std::exp(-std::pow((t - 0.46) / 0.06, 2)) * std::exp(-std::pow((fax - 0.5) / 0.15, 2)));
                // the lips, soft-edged, the upper one a bow
                double lw = 0.33 - 0.08 * std::fabs(t - 0.29) / 0.05;
                double inLip = ssf(0.016, 0.0, std::fabs(t - 0.29) - 0.042) * ssf(0.03, 0.0, fax - lw);
                blend(lips, inLip * (fem ? 1.0 : 0.8));
                blend(Color{90, 45, 42, 255}, 0.4 * ssf(0.004, 0.0, std::fabs(t - 0.29)) * ssf(0.03, 0.0, fax - lw * 0.95));   // (where they meet)
                // lash lines round the eyes (over the socket's shade)
                double ex = (fax - 0.39) / 0.13, et = (t - 0.565) / 0.05, er = std::sqrt(ex * ex + et * et);
                blend(lid, 0.55 * ssf(0.3, 0.0, std::fabs(er - 0.95)) * ssf(-0.1, 0.5, et));   // (the upper lid only)
                // eyebrows: an arch over each eye, soft at the ends
                double arch = 0.643 + 0.016 * (1 - std::pow((fax - 0.4) / 0.24, 2)) + (fem ? 0.008 : 0);
                double bw = fem ? 0.008 : 0.013;
                blend(brows, 0.92 * ssf(0.005, 0.0, std::fabs(t - arch) - bw) * ssf(0.04, 0.0, std::fabs(fax - 0.42) - 0.24));
            }
            if (L.hair != "none") {
                double hl = hairline(ang);
                if (L.hair == "buzz") blend(mix(hair, skin, 0.3f), 0.85 * ssf(hl - 0.012, hl + 0.012, t));
                else blend(hair, ssf(hl - 0.01, hl + 0.01, t));
            } else if (!fem) blend(mix(skin, Color{40, 34, 30, 255}, 0.1f), 0.0);
            px[j * W + i] = Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
        }
    }
    return img;
}

const HeadMesh& headMesh(const NpcBone& h, const CharLook& L) {
    char key[256];
    std::snprintf(key, sizeof key, "%.2f %.2f %.3f %.3f %.3f|%s|%s|%s|%s", h.r0, h.len, h.width, h.depth, h.bulge, L.skin.c_str(), L.hair.c_str(), L.hairColor.c_str(), L.body.c_str());
    auto it = gHeads.find(key);
    if (it != gHeads.end()) { it->second.used = GetTime(); return it->second; }
    if (gHeads.size() > 96) {   // (the longest unused go)
        auto old = gHeads.begin();
        for (auto i = gHeads.begin(); i != gHeads.end(); ++i) if (i->second.used < old->second.used) old = i;
        UnloadMesh(old->second.head); if (old->second.hasHair) UnloadMesh(old->second.hair); if (old->second.hasDrape) UnloadMesh(old->second.drape); UnloadTexture(old->second.tex);
        gHeads.erase(old);
    }
    const int ROWS = 46, COLS = 44;   // (the last column is the first again, its u 1 instead of 0)
    auto tOf = [&](int i) { return 0.012 + 0.988 * (double)i / (ROWS - 1); };
    auto angOf = [&](int k) { double u = -1 + 2.0 * k / COLS; return PI * (u < 0 ? -1 : 1) * std::pow(std::fabs(u), 1.45); };   // (more of them round the face)
    std::vector<std::vector<Vector3>> P(ROWS, std::vector<Vector3>(COLS + 1));
    for (int i = 0; i < ROWS; i++)
        for (int k = 0; k <= COLS; k++) { double x, z; npcHeadPoint(h, tOf(i), angOf(k), x, z); P[i][k] = {(float)x, (float)(tOf(i) * h.len), (float)z}; }
    auto normals = [&](const std::vector<std::vector<Vector3>>& Q) {
        const int R = (int)Q.size();
        std::vector<std::vector<Vector3>> N(R, std::vector<Vector3>(COLS + 1));
        for (int i = 0; i < R; i++)
            for (int k = 0; k <= COLS; k++) {
                int kp = k == COLS ? 1 : k + 1, km = k == 0 ? COLS - 1 : k - 1;
                Vector3 round = Vector3Subtract(Q[i][kp], Q[i][km]);
                Vector3 along = Vector3Subtract(Q[std::min(i + 1, R - 1)][k], Q[std::max(i - 1, 0)][k]);
                Vector3 nn = Vector3CrossProduct(round, along);
                Vector3 c{0, Q[i][k].y, 0};
                if (Vector3Length(round) < 1e-4f) nn = Vector3{0, i == 0 ? -1.f : 1.f, 0};
                else if (Vector3DotProduct(nn, Vector3Subtract(Q[i][k], c)) < 0) nn = Vector3Negate(nn);
                N[i][k] = Vector3Normalize(nn);
            }
        return N;
    };
    auto build = [&](const std::vector<std::vector<Vector3>>& Q, const std::vector<std::vector<Vector3>>& N, int i0, std::function<bool(int, int)> keep, bool allHair = false) {
        std::vector<float> pos, nrm, uv;
        auto put = [&](int i, int k) {
            pos.insert(pos.end(), {Q[i][k].x, Q[i][k].y, Q[i][k].z});
            nrm.insert(nrm.end(), {N[i][k].x, N[i][k].y, N[i][k].z});
            if (allHair) uv.insert(uv.end(), {0.01f, 0.97f});   // (the back of the crown: hair)
            else uv.insert(uv.end(), {(float)k / COLS, (float)tOf(i0 + i)});
        };
        for (int i = 0; i + 1 < (int)Q.size(); i++)
            for (int k = 0; k < COLS; k++) {
                if (!keep(i, k)) continue;
                Vector3 f = Vector3CrossProduct(Vector3Subtract(Q[i + 1][k], Q[i][k]), Vector3Subtract(Q[i + 1][k + 1], Q[i][k]));
                bool out = Vector3DotProduct(f, Vector3Add(N[i][k], N[i + 1][k + 1])) >= 0;
                if (out) { put(i, k); put(i + 1, k); put(i + 1, k + 1); put(i, k); put(i + 1, k + 1); put(i, k + 1); }
                else { put(i, k); put(i + 1, k + 1); put(i + 1, k); put(i, k); put(i, k + 1); put(i + 1, k + 1); }
            }
        Mesh m{};
        m.vertexCount = (int)pos.size() / 3; m.triangleCount = m.vertexCount / 3;
        m.vertices = (float*)RL_MALLOC(pos.size() * sizeof(float)); std::copy(pos.begin(), pos.end(), m.vertices);
        m.normals = (float*)RL_MALLOC(nrm.size() * sizeof(float)); std::copy(nrm.begin(), nrm.end(), m.normals);
        m.texcoords = (float*)RL_MALLOC(uv.size() * sizeof(float)); std::copy(uv.begin(), uv.end(), m.texcoords);
        UploadMesh(&m, false);
        return m;
    };
    HeadMesh hm; hm.used = GetTime();
    hm.head = build(P, normals(P), 0, [](int, int) { return true; });
    if (L.hair == "short" || L.hair == "long" || L.hair == "ponytail" || L.hair == "bun") {
        // the hair: the head pushed out along its normals, more toward the crown, meeting the scalp at the hairline
        auto N = normals(P);
        int first = 0;
        while (first < ROWS - 2 && tOf(first + 1) < 0.31) first++;
        std::vector<std::vector<Vector3>> S;
        for (int i = first; i < ROWS; i++) {
            std::vector<Vector3> row(COLS + 1);
            for (int k = 0; k <= COLS; k++) {
                double t = tOf(i), a = angOf(k);
                double up = ssf(hairline(a) - 0.01, hairline(a) + 0.13, t);
                double thick = (L.hair == "short" ? 3.5 : 3) + 7 * ssf(0.6, 0.95, t);   // (thin at the sides, fuller on top)
                double g = thick * up - 2.5 * (1 - up);   // (below the hairline it sinks under the skin: no edge)
                row[k] = Vector3Add(P[i][k], Vector3Scale(N[i][k], (float)g));
            }
            S.push_back(row);
        }
        auto SN = normals(S);
        hm.hair = build(S, SN, first, [&](int i, int k) {   // (where it's under the skin all round a quad, left out)
            double t = tOf(first + i + 1);
            return t > std::min(hairline(angOf(k)), hairline(angOf(k + 1))) - 0.03;
        });
        if (L.hair == "long") {   // long hair: a curtain round the back of the head, from above the ears down past the shoulders
            std::vector<std::vector<Vector3>> C;
            const int CR = 18;
            for (int i = 0; i < CR; i++) {
                double y = h.len * (0.62 - 1.55 * i / (CR - 1.0));   // (head frame: down the neck to the shoulders)
                double t = std::clamp(y / h.len, 0.0, 1.0);
                std::vector<Vector3> row(COLS + 1);
                for (int k = 0; k <= COLS; k++) {
                    double a = angOf(k), back = std::fabs(a);
                    double ac = std::clamp((back - 1.2) / (PI - 1.2), 0.0, 1.0);   // (0 at the front edge by the ear, 1 at the back)
                    double ra = 1.2 + ac * (PI - 1.2);
                    double x, z;
                    npcHeadPoint(h, std::max(t, 0.45), (a < 0 ? -1 : 1) * ra, x, z);
                    double below = std::clamp(-(y - 0.45 * h.len) / h.len, 0.0, 1.2);
                    double out = 10 + 30 * below * below + 6 * below;   // (falls clear of the neck, flaring onto the shoulders)
                    double l = std::max(1e-6, std::hypot(x / h.width, z));
                    row[k] = {(float)(x + out * x / l / std::max(0.5, h.width) * h.width * 0.9), (float)y, (float)(z + out * z / l - 12 * below)};
                }
                C.push_back(row);
            }
            auto CN = normals(C);
            hm.drape = build(C, CN, 0, [&](int, int k) { return std::fabs(angOf(k)) > 1.25 && std::fabs(angOf(k + 1)) > 1.25; }, true);
            hm.hasDrape = true;
        }
        hm.hasHair = true;
    }
    Image img = headPaint(h, L);
    hm.tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&hm.tex);
    SetTextureFilter(hm.tex, TEXTURE_FILTER_TRILINEAR);
    return gHeads[key] = hm;
}
}  // namespace

bool npcBodyBone(const NpcDef& n, int i) { return n.look.on && i >= 0 && i < (int)n.bones.size() && n.bones[i].body; }

void drawCharacterBody(const NpcDef& n, const std::vector<Matrix>& M, const Color* tint) {
    if (!n.look.on || M.size() < n.bones.size()) return;
    const CharLook& L = n.look;
    Color skin = hexColor(L.skin), top = hexColor(L.topColor), pants = hexColor(L.pantsColor);
    auto T = [&](Color c) { return tint ? *tint : c; };
    std::vector<Tube> tubes;
    int pel = boneNamed(n, "Pelvis"), che = boneNamed(n, "Chest"), nec = boneNamed(n, "Neck"), hed = boneNamed(n, "Head");
    if (pel < 0 || che < 0 || nec < 0) return;
    {   // the torso: from below the hips up through the chest, closing in to the base of the neck
        Tube t; t.sides = 22;
        const double b = L.body == "female" ? 1 : 0;   // (a woman's chest swells forward: part of the one skin, not added pieces)
        t.rings = {{pel, -1, -0.12, 0, 0, 0.55}, {pel, -1, 0.0, 0, 0, 0.95}, {pel, -1, 0.5, 0, 0, 1}, {pel, che, 1, 0, 0.5, 1}, {che, -1, 0.2, 0, 0, 0.97},
                   {che, -1, 0.35, 0, 0, 1}, {che, -1, 0.47, 0, 0, 1, 0.24 * b}, {che, -1, 0.56, 0, 0, 1, 0.4 * b}, {che, -1, 0.65, 0, 0, 1, 0.34 * b},
                   {che, -1, 0.74, 0, 0, 1, 0.12 * b}, {che, -1, 0.84, 0, 0, 1, 0, 3.0}, {che, -1, 0.92, 0, 0, 1.0, 0, 2.8}, {che, -1, 0.98, 0, 0, 0.93, 0, 2.5},
                   {che, -1, 1.03, 0, 0, 0.76, 0, 2.2}, {che, -1, 1.07, 0, 0, 0.5, 0, 2}, {che, -1, 1.1, 0, 0, 0.28, 0, 2}};   // (round shoulders sloping in to the neck)
        t.cols = {T(pants), T(pants), T(pants)};
        while (t.cols.size() + 1 < t.rings.size()) t.cols.push_back(T(top));
        tubes.push_back(t);
    }
    {   // the neck: from inside the shoulders up into the head
        Tube t; t.sides = 12;
        t.rings = {{nec, -1, -0.35, 0, 0, 1}, {nec, -1, 0.0, 0, 0, 1}, {nec, -1, 0.5, 0, 0, 0.95}, {nec, -1, 1.0, 0, 0, 0.95}, {nec, -1, 1.3, 0, 0, 0.85}};
        t.cols = {T(L.top == "tank" ? skin : top), T(skin), T(skin), T(skin)};
        tubes.push_back(t);
    }
    for (const char* sd : {"left", "right"}) {
        std::string s = std::string(" (") + sd + ")";
        int ua = boneNamed(n, "Upper arm" + s), fa = boneNamed(n, "Forearm" + s), th = boneNamed(n, "Thigh" + s), sh = boneNamed(n, "Shin" + s);
        if (ua >= 0 && fa >= 0) {   // an arm: from the round of the shoulder to the wrist
            Tube t; t.sides = 12;
            t.rings = {{ua, -1, -0.04, 0, 0, 0.4}, {ua, -1, 0.0, 0, 0, 0.8}, {ua, -1, 0.07, 0, 0, 0.98}, {ua, -1, 0.17, 0, 0, 1.0}, {ua, -1, 0.3, 0, 0, 1}, {ua, -1, 0.55, 0, 0, 1},
                       {ua, -1, 0.8, 0, 0, 0.96}, {ua, fa, 1, 0, 0.5, 0.92}, {fa, -1, 0.2, 0, 0, 1}, {fa, -1, 0.5, 0, 0, 1}, {fa, -1, 0.8, 0, 0, 0.95},
                       {fa, -1, 1.0, 0, 0, 0.9}, {fa, -1, 1.05, 0, 0, 0.6}};
            Color sl = L.top == "longsleeve" ? top : skin;
            Color upper = L.top == "tank" ? skin : top;
            t.cols = {T(upper), T(upper), T(upper), T(upper), T(upper), T(sl), T(sl), T(sl), T(sl), T(sl), T(sl), T(sl)};   // (a T-shirt's sleeve ends half way down the upper arm)
            tubes.push_back(t);
        }
        if (th >= 0 && sh >= 0) {   // a leg: from inside the hips to the ankle
            Tube t; t.sides = 12;
            Color low = L.pants == "shorts" ? skin : pants;
            t.rings = {{th, -1, -0.15, 0, 0, 0.6}, {th, -1, 0.0, 0, 0, 1}, {th, -1, 0.3, 0, 0, 1}, {th, -1, 0.6, 0, 0, 0.98}, {th, -1, 0.85, 0, 0, 0.95},
                       {th, sh, 1, 0, 0.5, 0.93}, {sh, -1, 0.2, 0, 0, 1}, {sh, -1, 0.45, 0, 0, 1.02}, {sh, -1, 0.75, 0, 0, 0.95}, {sh, -1, 1.0, 0, 0, 0.88},
                       {sh, -1, 1.06, 0, 0, 0.5}};
            t.cols = {T(pants), T(pants), T(pants), T(pants), T(L.pants == "shorts" ? pants : pants), T(low), T(low), T(low), T(low), T(low)};
            tubes.push_back(t);
        }
    }
    Buckets B;
    auto ringPts = [&](const RingAt& r, int sides) {
        std::vector<Vector3> out(sides);
        auto on = [&](int bi, double t, double cx, double cz) {
            const NpcBone& b = n.bones[bi];
            double rad = npcBoneRadius(b, std::clamp(t, 0.0, 1.0)) * r.shrink;
            bool sq = b.shape == "box" || b.shape == "wedge";
            double ex = r.ex > 0 ? r.ex : sq ? 3.5 : 2;   // a rounded square across a box-shaped bone (the chest), round elsewhere
            double px = (cx < 0 ? -1 : 1) * std::pow(std::fabs(cx), 2 / ex), pz = (cz < 0 ? -1 : 1) * std::pow(std::fabs(cz), 2 / ex);
            if (r.bust > 0 && cz > 0) {   // two soft curves either side of the middle, on the front only
                double lobe = std::exp(-std::pow((std::fabs(cx) - 0.42) / 0.3, 2));
                pz += r.bust * std::pow(cz, 1.5) * (0.35 + 0.65 * lobe);
            }
            return Vector3Transform(Vector3{(float)(px * rad * b.width), (float)(t * b.len), (float)(pz * rad * b.depth)}, M[bi]);
        };
        for (int k = 0; k < sides; k++) {
            double th = 2 * PI * (k + 0.5) / sides, cx = std::cos(th), cz = std::sin(th);
            Vector3 p = on(r.a, r.ta, cx, cz);
            if (r.b >= 0 && r.w > 0) p = Vector3Lerp(p, on(r.b, r.tb, cx, cz), (float)r.w);
            out[k] = p;
        }
        return out;
    };
    for (auto& t : tubes) {
        std::vector<std::vector<Vector3>> R;
        for (auto& r : t.rings) R.push_back(ringPts(r, t.sides));
        std::vector<std::vector<Color>> C;
        for (size_t i = 0; i + 1 < R.size(); i++) C.push_back(std::vector<Color>(t.sides, t.cols[std::min(i, t.cols.size() - 1)]));
        emitGrid(R, C, B);
    }
    rlDisableBackfaceCulling();   // (a mirrored frame turns the winding round: the shader lights either side)
    for (auto& [k, P] : B.pos) {
        Mesh m = meshOf(P, B.nrm[k], false);
        std::vector<float> uv(m.vertexCount * 2, 0.f);
        m.texcoords = uv.data();
        UploadMesh(&m, false);
        drawMesh(m, MatrixIdentity(), B.col[k]);
        rlUnloadVertexArray(m.vaoId);   // (the arrays are ours: free only what the upload made)
        for (int b = 0; b < 9; b++) if (m.vboId[b])   // (raylib keeps up to 9 buffers a mesh)
             rlUnloadVertexBuffer(m.vboId[b]);
        RL_FREE(m.vboId);
    }
    if (hed >= 0 && n.bones[hed].body) {   // the head: built once per look, carried by its bone
        const HeadMesh& hm = headMesh(n.bones[hed], L);
        drawMeshTex(hm.head, M[hed], tint ? *tint : WHITE, hm.tex);
        if (hm.hasHair) drawMeshTex(hm.hair, M[hed], tint ? *tint : WHITE, hm.tex);
        if (hm.hasDrape) drawMeshTex(hm.drape, M[hed], tint ? *tint : WHITE, hm.tex);
    }
    rlEnableBackfaceCulling();
}
