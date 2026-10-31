// Gunsmith3D - Drawing maps (see mapgfx.hpp).
// The textures are generated here rather than loaded, so there are no image files to license or ship:
// tileable noise plus simple patterns, in the spirit of early-2000s shooters.
#include "app/mapgfx.hpp"
#include "app/gfx.hpp"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using gmap::V3;

// ---------------------------------------------------------------- tileable noise
static uint32_t hash3(int x, int y, int s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float lattice(int x, int y, int P, int s) { x = ((x % P) + P) % P; y = ((y % P) + P) % P; return (hash3(x, y, s) & 0xffff) / 65535.f; }
static float vnoise(float x, float y, int P, int s) {  // value noise that repeats every P cells
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = lattice(xi, yi, P, s), b = lattice(xi + 1, yi, P, s), c = lattice(xi, yi + 1, P, s), d = lattice(xi + 1, yi + 1, P, s);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
static float fbm(float u, float v, int P, int oct, int s) {  // u, v in 0..1
    float sum = 0, amp = 0.5f, tot = 0;
    for (int o = 0; o < oct; o++) { sum += amp * vnoise(u * P, v * P, P, s + o * 31); tot += amp; P *= 2; amp *= 0.5f; }
    return sum / tot;
}
static float rnd(int a, int b, int s) { return (hash3(a, b, s) & 0xffff) / 65535.f; }
static unsigned char cl(float v) { return (unsigned char)std::clamp(v, 0.f, 255.f); }
static Color rgbf(float r, float g, float b) { return Color{cl(r), cl(g), cl(b), 255}; }

static constexpr int N = 256;

static Color texel(int m, int i, int j) {
    float u = (i + 0.5f) / N, v = (j + 0.5f) / N;
    switch (m) {
    case 0: {  // dev grid: orange with lines every quarter metre, a brighter border every metre
        float r = 214, g = 128, b = 60;
        int ci = i % (N / 4), cj = j % (N / 4);
        if (ci < 2 || cj < 2) { r = 168; g = 92; b = 42; }
        if (i < 3 || j < 3) { r = 246; g = 182; b = 112; }
        float n = (fbm(u, v, 8, 3, 1) - 0.5f) * 14;
        return rgbf(r + n, g + n, b + n);
    }
    case 1: case 2: {  // concrete slabs, with a seam at the edge of each 2 m slab
        float base = m == 1 ? 152 : 96;
        float n = (fbm(u, v, 8, 5, 2) - 0.5f) * 34 + (fbm(u, v, 64, 2, 3) - 0.5f) * 16;
        if (rnd(i, j, 4) > 0.985f) n -= 26;  // small pits
        if (i < 2 || j < 2) n -= 30;
        return rgbf(base + n, base + n - 1, base + n - 4);
    }
    case 3: {  // plywood: long grain along u
        float g = fbm(u, v, 4, 4, 5);
        float grain = std::sin((v * 56 + g * 2.2f) * 3.14159f) * 0.5f + 0.5f;
        float n = (fbm(u, v * 1, 2, 3, 6) - 0.5f) * 20;
        float k = 0.86f + grain * 0.14f;
        if (j < 2) k *= 0.75f;
        return rgbf((198 + n) * k, (160 + n) * k, (108 + n * 0.6f) * k);
    }
    case 4: {  // brick: 4 bricks across, 16 courses, staggered
        int rowH = N / 16, row = j / rowH, colW = N / 4;
        int x = (i + (row % 2) * colW / 2) % N, col = x / colW;
        bool mortar = (j % rowH) < 2 || (x % colW) < 3;
        if (mortar) { float n = (fbm(u, v, 32, 2, 7) - 0.5f) * 20; return rgbf(176 + n, 170 + n, 158 + n); }
        float t = rnd(col, row, 8) * 30 - 15, n = (fbm(u, v, 32, 3, 9) - 0.5f) * 26;
        return rgbf(150 + t + n, 70 + t * 0.4f + n * 0.6f, 50 + n * 0.5f);
    }
    case 5: {  // plaster
        float n = (fbm(u, v, 8, 5, 10) - 0.5f) * 14;
        return rgbf(214 + n, 208 + n, 196 + n);
    }
    case 6: {  // diamond plate
        float n = (fbm(u, v, 16, 3, 11) - 0.5f) * 18;
        float cu = std::fmod(u * 8, 1.f), cv = std::fmod(v * 8 + (std::fmod(std::floor(u * 8), 2.f)) * 0.5f, 1.f);
        float du = cu - 0.5f, dv = cv - 0.5f;
        float a = std::fabs(du * 0.8f + dv * 1.6f), b = std::fabs(du * 0.8f - dv * 1.6f);
        float lit = (a < 0.07f && std::fabs(du) < 0.35f) ? 34 : (b < 0.07f && std::fabs(du) < 0.35f) ? -22 : 0;
        return rgbf(140 + n + lit, 144 + n + lit, 150 + n + lit);
    }
    case 7: {  // corrugated sheet: ridges across u
        float r = std::sin(u * 20 * 2 * 3.14159f);
        float n = (fbm(u, v, 8, 4, 12) - 0.5f) * 20;
        float rust = std::max(0.f, fbm(u, v, 4, 5, 13) - 0.62f) * 160;
        return rgbf(146 + r * 26 + n + rust * 0.6f, 150 + r * 26 + n - rust * 0.1f, 156 + r * 26 + n - rust * 0.5f);
    }
    case 8: {  // dirt
        float n = (fbm(u, v, 4, 6, 14) - 0.5f) * 48;
        float peb = rnd(i / 3, j / 3, 15) > 0.97f ? 22 : 0;
        return rgbf(122 + n + peb, 94 + n * 0.8f + peb, 64 + n * 0.6f + peb);
    }
    case 9: {  // grass
        float n = (fbm(u, v, 4, 6, 16) - 0.5f) * 40, f = (fbm(u, v, 64, 2, 17) - 0.5f) * 40;
        float blade = rnd(i, j / 2, 18) > 0.8f ? 14 : 0;
        return rgbf(76 + n * 0.6f + f * 0.4f, 112 + n + f + blade, 46 + n * 0.4f);
    }
    case 10: {  // sand
        float n = (fbm(u, v, 8, 5, 19) - 0.5f) * 26 + (rnd(i, j, 20) - 0.5f) * 14;
        return rgbf(202 + n, 182 + n, 138 + n * 0.8f);
    }
    case 11: {  // gravel: random stones over dark grit (cells of 16 px, so it tiles cleanly)
        const int C = 16, CN = N / C;
        float best = 9, val = 0;
        int ci = i / C, cj = j / C;
        for (int a = -1; a <= 1; a++)
            for (int b = -1; b <= 1; b++) {
                int x = ci + a, y = cj + b, wx = ((x % CN) + CN) % CN, wy = ((y % CN) + CN) % CN;
                for (int k = 0; k < 2; k++) {  // two stones per cell
                    float px = (x + rnd(wx, wy, 21 + k * 7)) * C, py = (y + rnd(wx, wy, 22 + k * 7)) * C;
                    float d = std::hypot(px - i, py - j) / (6.5f + 3 * rnd(wx, wy, 40 + k));
                    if (d < best) { best = d; val = rnd(wx, wy, 23 + k * 7); }
                }
            }
        float stone = best < 1 ? (1 - best * best) : 0;
        float g = 92 + val * 70;
        float sh = stone > 0 ? g * (0.75f + 0.25f * stone) : 60;
        return rgbf(sh, sh * 0.98f, sh * 0.94f);
    }
    case 12: {  // wood planks: five boards per tile along u
        int board = j / (N / 5);
        float g = fbm(u + rnd(board, 0, 24), v, 4, 4, 25);
        float grain = std::sin((v * 60 + g * 8) * 3.14159f) * 0.5f + 0.5f;
        float t = rnd(board, 1, 26) * 24 - 12;
        float k = (j % (N / 5)) < 2 ? 0.55f : 0.85f + grain * 0.15f;
        return rgbf((150 + t) * k, (108 + t * 0.7f) * k, (70 + t * 0.4f) * k);
    }
    case 13: {  // asphalt
        float n = (fbm(u, v, 8, 5, 27) - 0.5f) * 18;
        float s = rnd(i, j, 28) > 0.93f ? 30 : 0;
        return rgbf(60 + n + s, 60 + n + s, 63 + n + s);
    }
    case 14: {  // tiles: 4 x 4 per metre
        int t = N / 4;
        bool grout = (i % t) < 3 || (j % t) < 3;
        float n = (fbm(u, v, 16, 3, 29) - 0.5f) * 10;
        return grout ? rgbf(150 + n, 148 + n, 142 + n) : rgbf(226 + n, 226 + n, 220 + n);
    }
    case 15: {  // bare steel
        float n = (fbm(u, v, 8, 5, 30) - 0.5f) * 22 + (fbm(u * 4, v, 64, 2, 31) - 0.5f) * 8;
        return rgbf(98 + n, 103 + n, 110 + n);
    }
    case 16: {  // white paint
        float n = (fbm(u, v, 8, 4, 32) - 0.5f) * 10;
        return rgbf(228 + n, 228 + n, 224 + n);
    }
    case 17: default: {  // hi-vis orange paint (steel targets)
        float n = (fbm(u, v, 8, 4, 33) - 0.5f) * 14;
        float chip = fbm(u, v, 16, 4, 34) > 0.72f ? 1 : 0;
        return chip ? rgbf(110 + n, 108 + n, 104 + n) : rgbf(236 + n, 112 + n * 0.6f, 34 + n * 0.3f);
    }
    }
}

static std::vector<Texture2D> TEX;
static std::vector<Material> MATS;

void mapGfxInit() {
    if (!TEX.empty()) return;
    int count = (int)gmap::materialNames().size();
    std::vector<Color> px(N * N);
    for (int m = 0; m < count; m++) {
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) px[j * N + i] = texel(m, i, j);
        Image img{px.data(), N, N, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        Texture2D t = LoadTextureFromImage(img);
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
        TEX.push_back(t);
        Material mat = LoadMaterialDefault();
        mat.shader = G.lit;
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = t;
        mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        MATS.push_back(mat);
    }
}
Texture2D mapTexture(int m) { return TEX.empty() ? Texture2D{} : TEX[std::clamp(m, 0, (int)TEX.size() - 1)]; }

// ---------------------------------------------------------------- meshes
static std::vector<Mesh> MESH;  // one per material; vertexCount 0 when unused

void mapBuildMeshes(const gmap::Map& m) {
    for (auto& mesh : MESH) if (mesh.vertexCount > 0) UnloadMesh(mesh);
    int count = (int)gmap::materialNames().size();
    MESH.assign(count, Mesh{});
    std::vector<std::vector<float>> pos(count), nrm(count), uv(count);
    for (auto& b : m.brushes)
        for (size_t fi = 0; fi < b.faces.size(); fi++) {
            auto poly = gmap::facePolygon(b, (int)fi);
            if (poly.size() < 3) continue;
            const gmap::Face& f = b.faces[fi];
            int mi = std::clamp(f.mat, 0, count - 1);
            double tile = gmap::materialTile(mi) * (f.scale > 0 ? f.scale : 1);
            double ax = std::fabs(f.n.x), ay = std::fabs(f.n.y), az = std::fabs(f.n.z);
            auto texUV = [&](V3 p, float& tu, float& tv) {  // world-aligned projection onto the face's main axis
                double a, c;
                if (ay >= ax && ay >= az) { a = p.x; c = p.z; }
                else if (ax >= az) { a = p.z; c = -p.y; }
                else { a = p.x; c = -p.y; }
                tu = (float)((a + f.shiftU) / tile); tv = (float)((c + f.shiftV) / tile);
            };
            for (size_t k = 1; k + 1 < poly.size(); k++)
                for (V3 p : {poly[0], poly[k], poly[k + 1]}) {
                    pos[mi].insert(pos[mi].end(), {(float)p.x, (float)p.y, (float)p.z});
                    nrm[mi].insert(nrm[mi].end(), {(float)f.n.x, (float)f.n.y, (float)f.n.z});
                    float tu, tv; texUV(p, tu, tv);
                    uv[mi].insert(uv[mi].end(), {tu, tv});
                }
        }
    for (int i = 0; i < count; i++) {
        int vc = (int)pos[i].size() / 3;
        if (vc == 0) continue;
        Mesh& mesh = MESH[i];
        mesh.vertexCount = vc; mesh.triangleCount = vc / 3;
        mesh.vertices = (float*)MemAlloc(vc * 3 * sizeof(float));
        mesh.normals = (float*)MemAlloc(vc * 3 * sizeof(float));
        mesh.texcoords = (float*)MemAlloc(vc * 2 * sizeof(float));
        std::copy(pos[i].begin(), pos[i].end(), mesh.vertices);
        std::copy(nrm[i].begin(), nrm[i].end(), mesh.normals);
        std::copy(uv[i].begin(), uv[i].end(), mesh.texcoords);
        UploadMesh(&mesh, false);
    }
}

void mapDrawWorld() {
    setSpecular(0.1f);
    for (size_t i = 0; i < MESH.size() && i < MATS.size(); i++) if (MESH[i].vertexCount > 0) DrawMesh(MESH[i], MATS[i], MatrixIdentity());
    setSpecular(0.3f);
}

// ---------------------------------------------------------------- light and sky
static Color c3(const double* c) { return Color{(unsigned char)std::clamp(c[0] * 255, 0.0, 255.0), (unsigned char)std::clamp(c[1] * 255, 0.0, 255.0), (unsigned char)std::clamp(c[2] * 255, 0.0, 255.0), 255}; }
Color mapSkyHorizon(const gmap::Look& L) { return c3(L.skyHorizon); }

void mapApplyLook(const gmap::Look& L) {
    Lighting g;
    double yw = L.sunYaw * DEG2RAD, pt = L.sunPitch * DEG2RAD;
    g.dir[0] = (float)(std::cos(pt) * std::cos(yw)); g.dir[1] = (float)std::sin(pt); g.dir[2] = (float)(std::cos(pt) * std::sin(yw));
    g.fill[0] = -g.dir[0]; g.fill[1] = 0.35f; g.fill[2] = -g.dir[2];
    for (int i = 0; i < 3; i++) {
        g.col[i] = (float)L.sun[i] * 1.15f;
        g.fillCol[i] = (float)L.skyTop[i];
        g.ambSky[i] = (float)L.ambient[i] * 1.2f;
        g.ambGround[i] = (float)L.ambient[i] * 0.75f;
    }
    g.fog[0] = (float)L.fogStart; g.fog[1] = (float)std::max(1.0, L.fogEnd - L.fogStart); g.fog[2] = 1.0f;
    setLighting(g);
    setFog(c3(L.skyHorizon));
}
void mapRestoreLook() { setLighting(Lighting{}); setFog(Color{199, 219, 239, 255}); }

void mapDrawSky(const gmap::Look& L, float x, float y, float w, float h, float pitch, float fovY) {
    // the horizon sits where a level line of sight lands on screen
    float t = std::tan(pitch) / std::tan(fovY * DEG2RAD / 2);
    float hy = y + h / 2 + t * h / 2;
    Color top = c3(L.skyTop), hor = c3(L.skyHorizon);
    float span = h * 0.9f;
    float y0 = hy - span;
    auto lerpC = [&](float k) { k = std::clamp(k, 0.f, 1.f); return Color{(unsigned char)(top.r + (hor.r - top.r) * k), (unsigned char)(top.g + (hor.g - top.g) * k), (unsigned char)(top.b + (hor.b - top.b) * k), 255}; };
    float a = std::max(y, y0), b = std::min(y + h, hy);
    DrawRectangle((int)x, (int)y, (int)w, (int)h, hor);
    if (b > a) DrawRectangleGradientV((int)x, (int)a, (int)w, (int)std::ceil(b - a), lerpC((a - y0) / span), lerpC((b - y0) / span));
    if (y0 > y) DrawRectangle((int)x, (int)y, (int)w, (int)std::ceil(std::min(y0, y + h) - y), top);
}

// ---------------------------------------------------------------- viewport
void mapBeginViewport(const Camera3D& cam, float x, float y, float w, float h) {
    BeginMode3D(cam);
    float sx = (float)GetRenderWidth() / GetScreenWidth(), sy = (float)GetRenderHeight() / GetScreenHeight();
    rlViewport((int)(x * sx), (int)(GetRenderHeight() - (y + h) * sy), (int)(w * sx), (int)(h * sy));
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    double aspect = (double)w / std::max(1.f, h);
    if (cam.projection == CAMERA_ORTHOGRAPHIC) {
        double top = cam.fovy / 2.0, right = top * aspect;
        rlOrtho(-right, right, -top, top, -2000, 2000);
    } else {
        double top = 0.02 * std::tan(cam.fovy * 0.5 * DEG2RAD), right = top * aspect;
        rlFrustum(-right, right, -top, top, 0.02, 2000);
    }
    rlMatrixMode(RL_MODELVIEW);
    float vp[3] = {cam.position.x, cam.position.y, cam.position.z};
    SetShaderValue(G.lit, G.locView, vp, SHADER_UNIFORM_VEC3);
}
void mapEndViewport() { rlViewport(0, 0, GetRenderWidth(), GetRenderHeight()); }
