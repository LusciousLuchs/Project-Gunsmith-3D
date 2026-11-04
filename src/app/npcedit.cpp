// Gunsmith3D - the NPC editor (see npcedit.hpp).
#include "app/npcedit.hpp"
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "app/paths.hpp"
#include "app/settings.hpp"
#include "raymath.h"
#include "rlgl.h"
#include "imgui.h"
#include "map.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
NpcEdit NE;
static const char* NPC_AUTOSAVE = "user/npcs/_editing.json";   // what's in the editor, kept between sessions

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
    std::ofstream(NPC_AUTOSAVE) << now;
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
        std::ifstream in(NPC_AUTOSAVE);
        std::stringstream ss; ss << in.rdbuf();
        if (!in || !npcFromJson(ss.str(), NE.npc)) NE.npc = npcPreset("human");
        NE.last = npcToJson(NE.npc);
    }
    npcFrame();
    setLighting(Lighting{});
    EnableCursor();
}
void leaveNpcEditor() { std::ofstream(NPC_AUTOSAVE) << npcToJson(NE.npc); }

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
        if (IsKeyPressed(KEY_DELETE) && NE.sel > 0) { npcRemoveBone(NE.npc, NE.sel); NE.sel = 0; npcCommit(); }
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

void drawNpcAt(const NpcDef& n, Vector3 origin, float yawDeg, float k, int highlight) {
    auto P = npcPose(n);
    // the NPC's own frame faces +z; turned so that it faces yawDeg (0 along +x)
    float th = (90 - yawDeg) * DEG2RAD, c0 = std::cos(th), s0 = std::sin(th);
    auto place = [&](const double p[3]) { float x = (float)p[0] * k, y = (float)p[1] * k, z = (float)p[2] * k;
        return Vector3{origin.x + x * c0 + z * s0, origin.y + y, origin.z - x * s0 + z * c0}; };
    auto axis = [&](const double v[3]) { return Vector3{(float)v[0] * c0 + (float)v[2] * s0, (float)v[1], -(float)v[0] * s0 + (float)v[2] * c0}; };
    for (size_t i = 0; i < P.size(); i++) {
        const NpcBone& b = n.bones[i];
        Color c = hexColor(b.color);
        if ((int)i == highlight) c = Color{(unsigned char)std::min(255, c.r + 50), (unsigned char)std::min(255, c.g + 40), (unsigned char)std::min(255, c.b + 10), 255};
        Vector3 a = place(P[i].a), e = place(P[i].b);
        float r0 = (float)b.r0 * k, r1 = (float)b.r1 * k;
        if (b.shape == "box" || b.shape == "ball") {
            Vector3 X = axis(P[i].x), Y = axis(P[i].y), Z = axis(P[i].z);
            Matrix R = {X.x, Y.x, Z.x, 0, X.y, Y.y, Z.y, 0, X.z, Y.z, Z.z, 0, 0, 0, 0, 1};
            Vector3 mid = Vector3Lerp(a, e, 0.5f);
            float L = (float)b.len * k;
            if (b.shape == "box") drawBox(mid, {(r0 + r1), L, (r0 + r1)}, c, R);
            else drawMesh(G.sphere, MatrixMultiply(MatrixMultiply(MatrixScale((r0 + r1) / 2, L / 2, (r0 + r1) / 2), R), MatrixTranslate(mid.x, mid.y, mid.z)), c);
        } else {   // a tapered capsule: a few rings, rounded ends
            const int N = 6;
            for (int q = 0; q < N; q++) {
                float t0 = (float)q / N, t1 = (float)(q + 1) / N;
                drawCyl(Vector3Lerp(a, e, t0), Vector3Lerp(a, e, t1), r0 + (r1 - r0) * (t0 + t1) / 2, c);
            }
            drawSphereLit(a, r0, c);
            drawSphereLit(e, r1, c);
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
