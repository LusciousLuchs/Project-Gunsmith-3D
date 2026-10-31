// Gunsmith3D - Workspace look (see scene.hpp).
#include "app/scene.hpp"
#include "app/gfx.hpp"
#include "app/settings.hpp"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- themes
static Color rgb(unsigned v, unsigned char a = 255) { return Color{(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, a}; }
static Color mixC(Color a, Color b, float t) {
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

const char* backdropNames() { return "Blueprint\0Drafting paper\0Dark workshop\0Daylight studio\0Solid colour\0"; }

SceneLook sceneLook() {
    switch (CFG.backdrop) {
    case Backdrop::Blueprint:
        return {rgb(0x24578C), rgb(0x0F2C50), 1, 0.006f,
                rgb(0xA8CCF0, 20), rgb(0xA8CCF0, 50), rgb(0xD2E6FF, 100),
                rgb(0x1A4472, 235), rgb(0x173E69), false, rgb(0xE6F0FF, 120)};
    case Backdrop::Paper:
        return {rgb(0xF1EFE7), rgb(0xD6D2C4), 1, 0.014f,
                rgb(0x5A7896, 16), rgb(0x5A7896, 44), rgb(0x46648A, 100),
                rgb(0xE2DFD3, 240), rgb(0xDCD8CB), false, rgb(0x2A3A52, 140)};
    case Backdrop::Dark:
        return {rgb(0x31363F), rgb(0x131518), 1, 0.006f,
                rgb(0xFFFFFF, 8), rgb(0xFFFFFF, 20), rgb(0xFFFFFF, 46),
                rgb(0x23272D, 240), rgb(0x1C1F24), false, rgb(0xE6ECF5, 110)};
    case Backdrop::Studio:
        return {rgb(0x76AADE), rgb(0xD6E6F2), 0, 0.004f,
                rgb(0xFFFFFF, 14), rgb(0xFFFFFF, 34), rgb(0xFFFFFF, 80),
                rgb(0x7C848C, 255), rgb(0xC7DBEF), true, rgb(0xE6ECF5, 110)};
    case Backdrop::Solid: default: {
        Color c = rgb(CFG.solidColor);
        float lum = (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.f;
        Color ink = lum > 0.55f ? Color{0, 0, 0, 255} : Color{255, 255, 255, 255};
        Color edge = mixC(c, Color{0, 0, 0, 255}, lum > 0.55f ? 0.12f : 0.35f);
        auto a = [&](unsigned char al) { Color k = ink; k.a = al; return k; };
        return {c, edge, 1, 0.006f, a(12), a(30), a(70), mixC(c, edge, 0.5f), mixC(c, edge, 0.6f), false, a(120)};
    }
    }
}

// ---------------------------------------------------------------- shaders
static const char* BG_VS = R"(#version 330
in vec3 vertexPosition; uniform mat4 mvp;
void main(){ gl_Position=mvp*vec4(vertexPosition,1.0); })";
static const char* BG_FS = R"(#version 330
uniform vec4 rect; uniform vec3 cInner; uniform vec3 cOuter; uniform float radial; uniform float grain;
out vec4 finalColor;
void main(){
 vec2 uv=(gl_FragCoord.xy-rect.xy)/rect.zw;
 float asp=rect.z/rect.w;
 vec2 d=(uv-vec2(0.5,0.56))*vec2(asp,1.0);
 float r=length(d)/length(vec2(asp,1.0)*0.5);
 float t=mix(1.0-uv.y, smoothstep(0.05,1.05,r), radial);
 vec3 c=mix(cInner,cOuter,t);
 float n=fract(sin(dot(floor(gl_FragCoord.xy),vec2(12.9898,78.233)))*43758.5453);
 c+=(n-0.5)*grain;
 finalColor=vec4(c,1.0); })";

static const char* GRID_VS = R"(#version 330
in vec3 vertexPosition; uniform mat4 mvp; uniform mat4 matModel;
out vec3 fragPos;
void main(){ fragPos=vec3(matModel*vec4(vertexPosition,1.0)); gl_Position=mvp*vec4(vertexPosition,1.0); })";
// Anti-aliased lines from screen-space derivatives: they stay one pixel wide at any zoom, and a level of lines
// fades out once its cells get too small to read, so there is no shimmer when zoomed out or at grazing angles.
static const char* GRID_FS = R"(#version 330
in vec3 fragPos; out vec4 finalColor;
uniform int plane; uniform vec3 viewPos;
uniform vec4 cBase; uniform vec4 cFine; uniform vec4 cMinor; uniform vec4 cMajor;
uniform vec3 steps; uniform vec2 centre; uniform float radius;
float gridLine(vec2 p, float s, float w){
 vec2 c=p/s; vec2 fw=max(fwidth(c),vec2(1e-6));
 vec2 g=abs(fract(c-0.5)-0.5)/fw;
 float a=1.0-clamp(min(g.x,g.y)-(w-1.0)*0.5,0.0,1.0);
 float px=1.0/max(fw.x,fw.y);
 return a*smoothstep(4.0,12.0,px);
}
vec4 over(vec4 top, vec4 under){
 float a=top.a+under.a*(1.0-top.a);
 if(a<1e-5) return vec4(0.0);
 return vec4((top.rgb*top.a+under.rgb*under.a*(1.0-top.a))/a, a);
}
void main(){
 vec2 p = plane==0 ? fragPos.xy : fragPos.xz;
 vec3 nrm = plane==0 ? vec3(0.0,0.0,1.0) : vec3(0.0,1.0,0.0);
 float facing=dot(normalize(viewPos-fragPos),nrm);
 vec4 c=cBase;
 c=over(vec4(cFine.rgb, cFine.a*gridLine(p,steps.x,1.0)), c);
 c=over(vec4(cMinor.rgb, cMinor.a*gridLine(p,steps.y,1.0)), c);
 c=over(vec4(cMajor.rgb, cMajor.a*gridLine(p,steps.z,1.4)), c);
 float r=length(p-centre);
 c.a*=(1.0-smoothstep(radius*0.45,radius,r))*smoothstep(0.0,0.2,facing);
 finalColor=c; })";

static struct {
    Shader bg{}, grid{};
    int bRect = -1, bInner = -1, bOuter = -1, bRadial = -1, bGrain = -1;
    int gPlane = -1, gView = -1, gBase = -1, gFine = -1, gMinor = -1, gMajor = -1, gSteps = -1, gCentre = -1, gRadius = -1;
    Mesh quad{};
    Material mat{};
    bool ok = false;
} SC;

void initScene() {
    SC.bg = LoadShaderFromMemory(BG_VS, BG_FS);
    SC.bRect = GetShaderLocation(SC.bg, "rect"); SC.bInner = GetShaderLocation(SC.bg, "cInner"); SC.bOuter = GetShaderLocation(SC.bg, "cOuter");
    SC.bRadial = GetShaderLocation(SC.bg, "radial"); SC.bGrain = GetShaderLocation(SC.bg, "grain");
    SC.grid = LoadShaderFromMemory(GRID_VS, GRID_FS);
    SC.grid.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(SC.grid, "matModel");
    SC.gPlane = GetShaderLocation(SC.grid, "plane"); SC.gView = GetShaderLocation(SC.grid, "viewPos");
    SC.gBase = GetShaderLocation(SC.grid, "cBase"); SC.gFine = GetShaderLocation(SC.grid, "cFine");
    SC.gMinor = GetShaderLocation(SC.grid, "cMinor"); SC.gMajor = GetShaderLocation(SC.grid, "cMajor");
    SC.gSteps = GetShaderLocation(SC.grid, "steps"); SC.gCentre = GetShaderLocation(SC.grid, "centre"); SC.gRadius = GetShaderLocation(SC.grid, "radius");
    SC.quad = GenMeshPlane(1, 1, 1, 1);  // XZ plane, facing +Y
    SC.mat = LoadMaterialDefault();
    SC.mat.shader = SC.grid;
    SC.ok = true;
}

static void vec3Of(Color c, float out[3]) { out[0] = c.r / 255.f; out[1] = c.g / 255.f; out[2] = c.b / 255.f; }
static void vec4Of(Color c, float out[4]) { out[0] = c.r / 255.f; out[1] = c.g / 255.f; out[2] = c.b / 255.f; out[3] = c.a / 255.f; }

void drawBackdrop(float x, float y, float w, float h) {
    SceneLook L = sceneLook();
    float sx = (float)GetRenderWidth() / GetScreenWidth(), sy = (float)GetRenderHeight() / GetScreenHeight();
    float rect[4] = {x * sx, GetRenderHeight() - (y + h) * sy, w * sx, h * sy};
    float in[3], out[3];
    vec3Of(L.inner, in); vec3Of(L.outer, out);
    SetShaderValue(SC.bg, SC.bRect, rect, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.bg, SC.bInner, in, SHADER_UNIFORM_VEC3);
    SetShaderValue(SC.bg, SC.bOuter, out, SHADER_UNIFORM_VEC3);
    SetShaderValue(SC.bg, SC.bRadial, &L.radial, SHADER_UNIFORM_FLOAT);
    SetShaderValue(SC.bg, SC.bGrain, &L.grain, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(SC.bg);
    DrawRectangle((int)x, (int)y, (int)w, (int)h, WHITE);
    EndShaderMode();
}

// Grid spacing in world units: 1 / 10 / 50 mm, or 0.1 / 0.5 / 1 inch.
static void gridSteps(float s[3]) {
    if (CFG.units == Units::Imperial) { s[0] = 2.54f * U; s[1] = 12.7f * U; s[2] = 25.4f * U; }
    else { s[0] = 1 * U; s[1] = 10 * U; s[2] = 50 * U; }
}

static void drawGridQuad(int plane, Matrix M, Vector2 centre, float radius, Color base, const SceneLook& L, float strength) {
    float v[3] = {A.cam.position.x, A.cam.position.y, A.cam.position.z}, st[3], c[2] = {centre.x, centre.y};
    float b[4], f[4], mi[4], ma[4];
    gridSteps(st);
    auto scaled = [&](Color k, float out[4]) { vec4Of(k, out); out[3] *= strength; };
    vec4Of(base, b); scaled(L.fine, f); scaled(L.minor, mi); scaled(L.major, ma);
    SetShaderValue(SC.grid, SC.gPlane, &plane, SHADER_UNIFORM_INT);
    SetShaderValue(SC.grid, SC.gView, v, SHADER_UNIFORM_VEC3);
    SetShaderValue(SC.grid, SC.gBase, b, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.grid, SC.gFine, f, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.grid, SC.gMinor, mi, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.grid, SC.gMajor, ma, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.grid, SC.gSteps, st, SHADER_UNIFORM_VEC3);
    SetShaderValue(SC.grid, SC.gCentre, c, SHADER_UNIFORM_VEC2);
    SetShaderValue(SC.grid, SC.gRadius, &radius, SHADER_UNIFORM_FLOAT);
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    DrawMesh(SC.quad, SC.mat, M);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void drawBoard(float zBack) {
    if (!SC.ok || !CFG.grid) return;
    SceneLook L = sceneLook();
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (auto& b : A.design.bodies) for (auto& p : b.pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    if (x0 > x1) { x0 = 0; x1 = 200; y0 = 0; y1 = 100; }
    float cx = (float)(x0 + x1) / 2 * U, cy = -(float)(y0 + y1) / 2 * U;
    float half = (float)std::max(150.0, std::hypot(x1 - x0, y1 - y0) / 2 + 70) * U;
    float radius = half * 1.5f, size = radius * 2.2f;
    // the XZ plane stood up to face +Z
    Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(size, 1, size), MatrixRotateX(PI / 2)), MatrixTranslate(cx, cy, zBack));
    drawGridQuad(0, M, {cx, cy}, radius, Color{0, 0, 0, 0}, L, 1.f);
}

void drawFloor(float y) {
    if (!SC.ok) return;
    SceneLook L = sceneLook();
    float radius = 90.f, size = radius * 2.2f;  // 900 mm out from the gun, then gone
    Matrix M = MatrixMultiply(MatrixScale(size, 1, size), MatrixTranslate(A.target.x, y, 0));
    drawGridQuad(1, M, {A.target.x, 0}, radius, L.floor, L, 0.8f);
}

void drawWorldGrid(float y, float cx, float cz, float radius, const float steps[3], Color fine, Color minor, Color major) {
    if (!SC.ok) return;
    float v[3] = {A.cam.position.x, A.cam.position.y, A.cam.position.z}, st[3] = {steps[0], steps[1], steps[2]}, c[2] = {cx, cz};
    float b[4] = {0, 0, 0, 0}, f[4], mi[4], ma[4];
    vec4Of(fine, f); vec4Of(minor, mi); vec4Of(major, ma);
    int plane = 1;
    SetShaderValue(SC.grid, SC.gPlane, &plane, SHADER_UNIFORM_INT);
    SetShaderValue(SC.grid, SC.gView, v, SHADER_UNIFORM_VEC3);
    SetShaderValue(SC.grid, SC.gBase, b, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.grid, SC.gFine, f, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.grid, SC.gMinor, mi, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.grid, SC.gMajor, ma, SHADER_UNIFORM_VEC4);
    SetShaderValue(SC.grid, SC.gSteps, st, SHADER_UNIFORM_VEC3);
    SetShaderValue(SC.grid, SC.gCentre, c, SHADER_UNIFORM_VEC2);
    SetShaderValue(SC.grid, SC.gRadius, &radius, SHADER_UNIFORM_FLOAT);
    float size = radius * 2.2f;
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    DrawMesh(SC.quad, SC.mat, MatrixMultiply(MatrixScale(size, 1, size), MatrixTranslate(cx, y, cz)));
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}
