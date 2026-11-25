// Gunsmith3D - reference pictures (see refpics.hpp).
#include "app/refpics.hpp"
#include "app/app.hpp"
#include "app/paths.hpp"
#include "app/view.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <cstdlib>

namespace fs = std::filesystem;

namespace {
struct Tex { Texture2D t{}; bool ok = false; };
std::map<std::string, Tex> gTex;
const Tex& texOf(const std::string& file) {
    auto it = gTex.find(file);
    if (it != gTex.end()) return it->second;
    Tex T;
    Image img = LoadImage((fs::path(REFS_DIR) / file).string().c_str());
    if (img.data) {
        int big = std::max(img.width, img.height);
        if (big > 4096) ImageResize(&img, img.width * 4096 / big, img.height * 4096 / big);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        T.t = LoadTextureFromImage(img);
        UnloadImage(img);
        GenTextureMipmaps(&T.t);
        SetTextureFilter(T.t, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(T.t, TEXTURE_WRAP_CLAMP);
        T.ok = true;
    }
    return gTex[file] = T;
}
// the planes the pictures lie on (world units): the side picture just behind the backmost part, the top one under the
// lowest point of the gun
float sidePlane() {
    float back = 0;
    for (size_t i = 0; i < A.design.bodies.size(); i++) back = std::min(back, bodyZ((int)i) - bodyDepth(A.design.bodies[i]) / 2);
    return back - 0.3f;
}
float topPlane() {
    double low = 0;
    for (auto& b : A.design.bodies) for (auto& p : b.pts) low = std::max(low, p.y);
    return -(float)low * U - 0.5f;
}
// a picture's pixel (u, v) in its plane's millimetres (side: design x, y; top: design x, z)
V2 pixToPlane(const RefPicture& r, double u, double v) {
    double a = r.angle * DEG2RAD, c = std::cos(a), s = std::sin(a);
    return {r.x + (c * u - s * v) * r.mm, r.y + (s * u + c * v) * r.mm};
}
Vector3 planeToWorld(int view, V2 p) {
    if (view == 0) return {(float)p.x * U, -(float)p.y * U, sidePlane()};
    return {(float)p.x * U, topPlane(), (float)p.y * U};
}
bool mouseOnPlane(int view, V2& out) {
    Ray r = mouseRay();
    if (view == 0) {
        float zw = sidePlane();
        if (std::fabs(r.direction.z) < 1e-4f) return false;
        float t = (zw - r.position.z) / r.direction.z;
        if (t <= 0) return false;
        out = {(r.position.x + r.direction.x * t) / U, -(r.position.y + r.direction.y * t) / U};
    } else {
        float yw = topPlane();
        if (std::fabs(r.direction.y) < 1e-4f) return false;
        float t = (yw - r.position.y) / r.direction.y;
        if (t <= 0) return false;
        out = {(r.position.x + r.direction.x * t) / U, (r.position.z + r.direction.z * t) / U};
    }
    return true;
}
void drawOne(int view, bool front) {
    const RefPicture& r = refOf(view);
    if (!r.on || !r.shown || r.front != front || r.file.empty()) return;
    const Tex& T = texOf(r.file);
    if (!T.ok) return;
    double W = T.t.width, H = T.t.height;
    Vector3 c[4] = {planeToWorld(view, pixToPlane(r, 0, 0)), planeToWorld(view, pixToPlane(r, W, 0)), planeToWorld(view, pixToPlane(r, W, H)), planeToWorld(view, pixToPlane(r, 0, H))};
    float u0 = r.flip ? 1.f : 0.f, u1 = r.flip ? 0.f : 1.f;
    float tc[4][2] = {{u0, 0}, {u1, 0}, {u1, 1}, {u0, 1}};
    unsigned char al = (unsigned char)std::clamp(r.opacity * 255.0, 0.0, 255.0);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    if (front) rlDisableDepthTest();
    rlSetTexture(T.t.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, al);
    rlNormal3f(0, view == 0 ? 0.f : 1.f, view == 0 ? 1.f : 0.f);
    for (int k = 0; k < 4; k++) { rlTexCoord2f(tc[k][0], tc[k][1]); rlVertex3f(c[k].x, c[k].y, c[k].z); }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    if (front) rlEnableDepthTest();
    rlEnableBackfaceCulling();
}
int gMode = 0, gView = 0, gClicks = 0;
V2 gPts[2], gDragStart, gOrigin0;
bool gDragging = false;
}  // namespace

RefPicture& refOf(int view) { return view == 1 ? A.design.refTop : A.design.refSide; }
int refMode() { return gMode; }
int refView() { return gView; }
void refSetMode(int view, int mode) { gView = view; gMode = mode; gClicks = 0; gDragging = false; }
bool refMarked() { return gMode == 2 && gClicks == 2; }
double refMarkedMm() { return gClicks == 2 ? std::hypot(gPts[1].x - gPts[0].x, gPts[1].y - gPts[0].y) : 0; }
bool refPictureMissing(int view) { const RefPicture& r = refOf(view); return r.on && !texOf(r.file).ok; }

bool refLoad(int view, const std::string& path) {
    std::error_code ec;
    fs::create_directories(REFS_DIR, ec);
    fs::path src(path), dst = fs::path(REFS_DIR) / src.filename();
    if (!(fs::exists(dst, ec) && fs::equivalent(src, dst, ec))) {
        for (int k = 2; fs::exists(dst, ec); k++) dst = fs::path(REFS_DIR) / (src.stem().string() + " (" + std::to_string(k) + ")" + src.extension().string());
        fs::copy_file(src, dst, ec);
        if (ec) return false;
    }
    std::string file = dst.filename().string();
    gTex.erase(file);
    const Tex& T = texOf(file);
    if (!T.ok) return false;
    RefPicture& r = refOf(view);
    RefPicture keep = r;
    r = RefPicture{};
    r.on = true; r.file = file; r.opacity = keep.on ? keep.opacity : 0.55;
    // a first guess at its size: as long as the gun drawn so far (or 200 mm), over its middle
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& b : A.design.bodies) for (auto& p : b.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    if (x0 > x1 || x1 - x0 < 30) { x0 = 0; x1 = 200; y0 = -40; y1 = 60; }
    r.mm = (x1 - x0) / T.t.width;
    double cx = (x0 + x1) / 2, cy = view == 0 ? (y0 + y1) / 2 : 0;
    r.x = cx - T.t.width * r.mm / 2; r.y = cy - T.t.height * r.mm / 2;
    return true;
}

void refDrawBehind() {
    static bool envDone = false;   // screenshot hooks: GUNSMITH_REFPIC=file (side), GUNSMITH_REFTOP=file
    if (!envDone) {
        envDone = true;
        if (const char* e = getenv("GUNSMITH_REFPIC")) refLoad(0, e);
        if (const char* e = getenv("GUNSMITH_REFTOP")) refLoad(1, e);
    }
    drawOne(0, false); drawOne(1, false);
}
void refDrawFront() { drawOne(0, true); drawOne(1, true); }

void refOverlay() {
    if (gMode < 2 || gClicks == 0) return;
    Color c = gMode == 2 ? Color{255, 170, 40, 255} : Color{90, 200, 255, 255};
    Vector2 s0 = toScreen(planeToWorld(gView, gPts[0]));
    V2 second = gPts[1];
    if (gClicks == 1) mouseOnPlane(gView, second);
    Vector2 s1 = toScreen(planeToWorld(gView, second));
    DrawLineEx(s0, s1, 2.f, c);
    DrawCircleV(s0, 5, c);
    DrawCircleV(s1, 5, c);
    double len = std::hypot(second.x - gPts[0].x, second.y - gPts[0].y);
    DrawText(TextFormat(gMode == 2 ? "%.1f mm on the picture now" : "level line", len), (int)((s0.x + s1.x) / 2 + 8), (int)((s0.y + s1.y) / 2 - 22), 16, c);
}

bool refInput(bool overUI) {
    if (gMode == 0) return false;
    if (IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { refSetMode(gView, 0); return true; }
    RefPicture& r = refOf(gView);
    if (!r.on) { refSetMode(gView, 0); return false; }
    V2 p;
    bool hit = mouseOnPlane(gView, p);
    if (gMode == 1) {   // move: drag it about its plane
        if (!overUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit) { gDragging = true; gDragStart = p; gOrigin0 = {r.x, r.y}; }
        if (gDragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hit) { r.x = gOrigin0.x + p.x - gDragStart.x; r.y = gOrigin0.y + p.y - gDragStart.y; }
        if (gDragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { gDragging = false; commit(); }
        return true;
    }
    if (!overUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit) {
        if (gClicks >= 2) gClicks = 0;
        gPts[gClicks++] = p;
        if (gMode == 3 && gClicks == 2) {   // level: turn the picture about the first point so the line runs straight across
            double th = std::atan2(gPts[1].y - gPts[0].y, gPts[1].x - gPts[0].x) * RAD2DEG;
            if (th > 90) th -= 180;
            if (th < -90) th += 180;
            double d = -th * DEG2RAD, c = std::cos(d), s = std::sin(d);
            double ox = r.x - gPts[0].x, oy = r.y - gPts[0].y;
            r.x = gPts[0].x + c * ox - s * oy; r.y = gPts[0].y + s * ox + c * oy;
            r.angle -= th;
            commit();
            toast(TextFormat("Levelled: turned %.1f degrees.", -th));
            refSetMode(gView, 0);
        }
    }
    return !overUI;
}

void refApplyLength(double realMm) {
    double now = refMarkedMm();
    if (now < 1e-6 || realMm <= 0) return;
    RefPicture& r = refOf(gView);
    double k = realMm / now;
    r.mm *= k;
    r.x = gPts[0].x + (r.x - gPts[0].x) * k;
    r.y = gPts[0].y + (r.y - gPts[0].y) * k;
    commit();
    toast("The picture is to scale now.");
    refSetMode(gView, 0);
}
