// Gunsmith3D - the NPC editor (see npcedit.hpp).
#include "app/npcedit.hpp"
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
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
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
bool npcCoreBone(const std::string& name) {
    static std::vector<std::string> core;
    if (core.empty()) for (auto& b : npcPreset("human").bones) core.push_back(b.name);
    return std::find(core.begin(), core.end(), name) != core.end();
}
void enterCharacterEditor() {
    enterNpcEditor();   // (loads the NPC editor's own work the first time, so it can be set aside)
    npcStash = NE; stashed = true;
    NE = NpcEdit();
    NE.player = true;
    std::ifstream in(CHARACTER_FILE);
    std::stringstream ss; ss << in.rdbuf();
    if (!in || !npcFromJson(ss.str(), NE.npc)) { NE.npc = npcPreset("human"); NE.npc.name = "My character"; }
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
    const int SEG = 28;
    const double PI_D = 3.14159265358979;
    std::string sh = b.shape;
    bool squarish = sh == "box" || sh == "wedge";
    double ex = squarish ? 6 : 2;   // the cross-section: an ellipse, or a rounded square
    struct Ring { double y, r, dk; };   // where along, its radius, its depth factor (a wedge thins out)
    std::vector<Ring> rings;
    const int BODY = 14, CAP = 7;
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
    for (size_t i = 0; i < P.size(); i++) {
        const NpcBone& b = n.bones[i];
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
    DrawText("Right drag: turn   Wheel: zoom   Middle drag: up and down   Click a bone to select it   Ctrl+Z: undo", 12, GetScreenHeight() - 26, 14, Color{170, 176, 186, 255});
    DrawText("1.8 m", (int)toScreenV({-12, 18.3f, 0}).x - 16, (int)toScreenV({-12, 18.3f, 0}).y - 16, 14, Color{220, 150, 90, 255});
}
