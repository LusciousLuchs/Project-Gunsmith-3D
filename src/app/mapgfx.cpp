// Gunsmith3D - Drawing maps (see mapgfx.hpp).
// The textures are generated here rather than loaded, so there are no image files to license or ship:
// tileable noise plus simple patterns, in the spirit of early-2000s shooters.
#include "app/mapgfx.hpp"
#include "app/usertex.hpp"
#include "app/gfx.hpp"
#include "rlgl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <vector>
#include <atomic>
#include <thread>

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

static constexpr int N = 512;   // texture size (the patterns below were laid out on a 256 grid: B)
static constexpr int B = 256;
static Color texelNew(int m, int I, int J, float u, float v);

static Color texel(int m, int I, int J) {
    float u = (I + 0.5f) / N, v = (J + 0.5f) / N;
    if (m >= 18) return texelNew(m, I, J, u, v);
    int i = I * B / N, j = J * B / N;   // (the older patterns' lines and bricks, at the same size as before)
    switch (m) {
    case 0: {  // dev grid: orange with lines every quarter metre, a brighter border every metre
        float r = 214, g = 128, b = 60;
        int ci = i % (B / 4), cj = j % (B / 4);
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
        int rowH = B / 16, row = j / rowH, colW = B / 4;
        int x = (i + (row % 2) * colW / 2) % B, col = x / colW;
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
        const int C = 16, CN = B / C;
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
        int board = j / (B / 5);
        float g = fbm(u + rnd(board, 0, 24), v, 4, 4, 25);
        float grain = std::sin((v * 60 + g * 8) * 3.14159f) * 0.5f + 0.5f;
        float t = rnd(board, 1, 26) * 24 - 12;
        float k = (j % (B / 5)) < 2 ? 0.55f : 0.85f + grain * 0.15f;
        return rgbf((150 + t) * k, (108 + t * 0.7f) * k, (70 + t * 0.4f) * k);
    }
    case 13: {  // asphalt
        float n = (fbm(u, v, 8, 5, 27) - 0.5f) * 18;
        float s = rnd(i, j, 28) > 0.93f ? 30 : 0;
        return rgbf(60 + n + s, 60 + n + s, 63 + n + s);
    }
    case 14: {  // tiles: 4 x 4 per metre
        int t = B / 4;
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

// The newer materials (18 on), drawn at full size. Several lean on the look of mid-2000s shooters' city buildings:
// two-tone institutional paint, formwork concrete, riveted panels, old brick.
static Color texelNew(int m, int I, int J, float u, float v) {
    auto grain = [&](float k, int s) { return (fbm(u, v, 8, 5, s) - 0.5f) * k + (fbm(u, v, 64, 3, s + 1) - 0.5f) * k * 0.5f; };
    switch (m) {
    case 18: {  // institutional wall: green-grey below a dark stripe, cream above, scuffed
        float n = grain(12, 101), scuff = std::max(0.f, fbm(u, v, 16, 4, 102) - 0.62f) * 120;
        if (v > 0.56f) return rgbf(86 + n - scuff * 0.3f, 104 + n - scuff * 0.3f, 88 + n - scuff * 0.3f);   // (v grows downward: the lower part)
        if (v > 0.52f) return rgbf(52 + n, 58 + n, 50 + n);
        return rgbf(214 + n - scuff * 0.2f, 206 + n - scuff * 0.2f, 176 + n - scuff * 0.25f);
    }
    case 19: {  // poured concrete: formwork panels with tie holes and stains
        float n = grain(30, 103);
        int pw = N / 2, ph = N / 4;
        int x = I % pw, y = J % ph;
        if (x < 3 || y < 3) n -= 22;
        float hx = (x - pw / 2.f), hy = (y - ph / 2.f);
        for (float ox : {-pw * 0.3f, pw * 0.3f}) if (std::hypot(hx - ox, hy) < 6) n -= 50;
        float stain = std::max(0.f, fbm(u * 0.5f, v * 2, 4, 4, 104) - 0.58f) * 110;
        return rgbf(150 + n - stain, 150 + n - stain, 146 + n - stain);
    }
    case 20: {  // cinder block, painted
        int bw = N / 2, bh = N / 8, row = J / bh;
        int x = (I + (row % 2) * bw / 2) % N;
        bool mortar = (J % bh) < 5 || (x % bw) < 5;
        float n = grain(16, 105) + (rnd(I / 2, J / 2, 106) - 0.5f) * 14;
        return mortar ? rgbf(160 + n, 158 + n, 150 + n) : rgbf(196 + n, 194 + n, 184 + n);
    }
    case 21: {  // old brick: darker, uneven, sooty
        int rowH = N / 16, row = J / rowH, colW = N / 4;
        int x = (I + (row % 2) * colW / 2) % N, col = x / colW;
        bool mortar = (J % rowH) < 4 || (x % colW) < 5;
        float soot = fbm(u, v, 4, 4, 107) * 50;
        if (mortar) { float n = grain(18, 108); return rgbf(120 + n - soot * 0.5f, 114 + n - soot * 0.5f, 104 + n - soot * 0.5f); }
        float t = rnd(col, row, 109) * 40 - 20, n = grain(28, 110);
        return rgbf(118 + t + n - soot, 58 + t * 0.4f + n * 0.6f - soot * 0.6f, 44 + n * 0.5f - soot * 0.5f);
    }
    case 22: {  // riveted metal panels
        int p = N / 2, x = I % p, y = J % p;
        float n = grain(18, 111);
        float edge = (x < 4 || y < 4) ? -30 : (x < 8 || y < 8) ? 14 : 0;
        float rivet = 0;
        for (int k = 1; k < 4; k++) for (auto [rx, ry] : {std::pair<int, int>{k * p / 4, 14}, {14, k * p / 4}}) {
            float d = std::hypot((float)(x - rx), (float)(y - ry));
            if (d < 5) rivet = 40 - d * 6;
        }
        return rgbf(112 + n + edge + rivet, 118 + n + edge + rivet, 120 + n + edge + rivet);
    }
    case 23: {  // rusted metal
        float r = fbm(u, v, 8, 6, 112), n = grain(20, 113);
        float t = std::clamp((r - 0.35f) * 2.2f, 0.f, 1.f);
        return rgbf(96 + n + t * 70, 92 + n + t * 18, 90 + n - t * 20);
    }
    case 24: {  // wallpaper: faded stripes and a small pattern
        int sw = N / 8;
        bool stripe = (I / sw) % 2 == 0;
        float n = grain(10, 114), dot = 0;
        int cx = I % (sw), cy = J % (sw);
        if (std::hypot(cx - sw / 2.f, cy - sw / 2.f) < sw * 0.12f) dot = -18;
        float fade = fbm(u, v, 4, 3, 115) * 20;
        return stripe ? rgbf(178 + n + dot + fade, 160 + n + dot + fade, 126 + n + dot + fade) : rgbf(196 + n + fade, 182 + n + fade, 150 + n + fade);
    }
    case 25: {  // parquet: herringbone of short boards
        int b = N / 8;
        int a = (I + J) / b, c = (I - J + N) / b;
        bool dir = ((a + c) & 1) == 0;
        float along = dir ? u : v, g = fbm(dir ? u : v, dir ? v : u, 4, 4, 116 + (a * 7 + c) % 5);
        float gr = std::sin((along * 90 + g * 10) * 3.14159f) * 0.5f + 0.5f;
        float t = rnd(a, c, 117) * 26 - 13;
        float seam = ((I + J) % b < 2 || (I - J + N) % b < 2) ? 0.6f : 1;
        float k = (0.85f + gr * 0.15f) * seam;
        return rgbf((158 + t) * k, (112 + t * 0.7f) * k, (70 + t * 0.4f) * k);
    }
    case 26: {  // carpet: fine fibres, a little pattern
        float n = (rnd(I, J, 118) - 0.5f) * 30 + grain(14, 119);
        float pat = std::sin(u * 3.14159f * 16) * std::sin(v * 3.14159f * 16) > 0.6f ? 12 : 0;
        return rgbf(70 + n + pat, 64 + n + pat, 92 + n + pat);
    }
    case 27: {  // checker tile
        int t = N / 4;
        bool grout = (I % t) < 4 || (J % t) < 4, dark = ((I / t) + (J / t)) % 2;
        float n = grain(10, 120);
        if (grout) return rgbf(120 + n, 118 + n, 112 + n);
        return dark ? rgbf(48 + n, 50 + n, 54 + n) : rgbf(222 + n, 220 + n, 212 + n);
    }
    case 28: {  // stone wall: cells of rough stones
        const int C = 64, CN = N / C;
        float best = 9, second = 9; float val = 0;
        int ci = I / C, cj = J / C;
        for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++) {
            int x = ci + a, y = cj + b, wx = ((x % CN) + CN) % CN, wy = ((y % CN) + CN) % CN;
            float px = (x + 0.15f + 0.7f * rnd(wx, wy, 121)) * C, py = (y + 0.15f + 0.7f * rnd(wx, wy, 122)) * C;
            float d = std::hypot(px - I, py - J);
            if (d < best) { second = best; best = d; val = rnd(wx, wy, 123); } else if (d < second) second = d;
        }
        float n = grain(26, 124);
        if (second - best < 6) return rgbf(82 + n * 0.5f, 80 + n * 0.5f, 76 + n * 0.5f);   // the mortar between
        float g = 128 + val * 50;
        return rgbf(g + n, g * 0.96f + n, g * 0.9f + n);
    }
    case 29: {  // roof shingles
        int rh = N / 8, row = J / rh, w = N / 6;
        int x = (I + (row % 2) * w / 2) % N;
        float n = grain(18, 125), shade = (float)(J % rh) / rh * 26;
        bool gap = (x % w) < 3 || (J % rh) < 3;
        float t = rnd(x / w, row, 126) * 20 - 10;
        return gap ? rgbf(30, 30, 32) : rgbf(76 + n + t - shade, 70 + n + t - shade, 70 + n + t - shade);
    }
    case 30: {  // marble: soft veins
        float w = fbm(u, v, 4, 6, 127);
        float vein = std::pow(std::fabs(std::sin((u * 6 + v * 3 + w * 6) * 3.14159f)), 0.18f);
        float n = grain(6, 128);
        float k = 160 + vein * 74;
        return rgbf(k + n, k + n - 2, k + n - 6);
    }
    case 31: {  // wood door face (tiled as a wall: planks with a frame)
        float g = fbm(u, v * 0.3f, 4, 5, 129), gr = std::sin((v * 40 + g * 8) * 3.14159f) * 0.5f + 0.5f;
        int x = I % (N / 2), y = J % (N / 2);
        bool groove = x < 4 || y < 4 || std::abs(x - N / 4) < 2;
        float k = (0.78f + gr * 0.22f) * (groove ? 0.6f : 1);
        return rgbf(122 * k, 82 * k, 50 * k);
    }
    case 32: default: {  // painted metal door face, tiled: worn green paint, dents
        float n = grain(16, 130), wear = std::max(0.f, fbm(u, v, 16, 4, 131) - 0.66f) * 200;
        int x = I % (N / 2), y = J % (N / 2);
        float rim = (x < 6 || y < 6) ? -24 : 0;
        return rgbf(62 + n + rim + wear * 0.5f, 84 + n + rim + wear * 0.45f, 74 + n + rim + wear * 0.4f);
    }
    }
}

static std::vector<Texture2D> TEX;
static std::vector<Material> MATS;

void mapGfxInit() {
    if (!TEX.empty()) return;
    int count = gmap::builtInMaterials();  // (the player's pictures are added as they're needed)
    // the pictures are worked out on every core at once (a second or two on one), then handed to the graphics card
    std::vector<std::vector<Color>> px(count);
    std::atomic<int> next{0};
    auto work = [&]() { for (int m; (m = next++) < count;) { px[m].resize(N * N); for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) px[m][j * N + i] = texel(m, i, j); } };
    double t0 = GetTime();
    unsigned nt = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned k = 1; k < nt; k++) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
    if (std::getenv("GUNSMITH_TEXTIME")) { std::printf("TEXTIME all %d in %.0f ms on %u threads\n", count, (GetTime() - t0) * 1000, nt); std::fflush(stdout); }
    for (int m = 0; m < count; m++) {
        Image img{px[m].data(), N, N, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
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
// The player's own pictures become materials as they turn up (user/textures, or named in a map that was opened).
static void ensureUserMaterials() {
    if (TEX.empty()) mapGfxInit();
    int count = (int)gmap::materialNames().size();
    while ((int)TEX.size() < count) {
        std::string name = gmap::materialNames()[TEX.size()];
        Texture2D t = userTexture(name.substr(5));  // after "user:"
        TEX.push_back(t);
        Material mat = LoadMaterialDefault();
        mat.shader = G.lit;
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = t;
        mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        MATS.push_back(mat);
    }
}
Texture2D mapTexture(int m) {
    ensureUserMaterials();
    return TEX.empty() ? Texture2D{} : TEX[std::clamp(m, 0, (int)TEX.size() - 1)];
}

// ---------------------------------------------------------------- meshes
static std::vector<Mesh> MESH;  // one per material; vertexCount 0 when unused

void mapBuildMeshes(const gmap::Map& m) {
    ensureUserMaterials();
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
    ensureUserMaterials();
    setSpecular(0.1f);
    for (size_t i = 0; i < MESH.size() && i < MATS.size(); i++) if (MESH[i].vertexCount > 0) DrawMesh(MESH[i], MATS[i], MatrixIdentity());
    setSpecular(0.3f);
}

// ---------------------------------------------------------------- light and sky
static Color c3(const double* c, double k = 1) { return Color{(unsigned char)std::clamp(c[0] * k * 255, 0.0, 255.0), (unsigned char)std::clamp(c[1] * k * 255, 0.0, 255.0), (unsigned char)std::clamp(c[2] * k * 255, 0.0, 255.0), 255}; }
static double skyK(const gmap::Look& L) { double d = std::clamp(L.daylight, 0.0, 1.0); return 0.05 + 0.95 * std::pow(d, 0.8); }   // the sky darkens with the day
Color mapSkyHorizon(const gmap::Look& L) { return c3(L.skyHorizon, skyK(L)); }

static int gWorkLight = 0;
void mapWorkLight(int mode) { gWorkLight = mode; }
void mapApplyLook(const gmap::Look& L) {
    Lighting g;
    const double day = std::clamp(L.daylight, 0.0, 1.0);
    double yw = L.sunYaw * DEG2RAD, pt = L.sunPitch * DEG2RAD;
    g.dir[0] = (float)(std::cos(pt) * std::cos(yw)); g.dir[1] = (float)std::sin(pt); g.dir[2] = (float)(std::cos(pt) * std::sin(yw));
    g.fill[0] = -g.dir[0]; g.fill[1] = 0.35f; g.fill[2] = -g.dir[2];
    for (int i = 0; i < 3; i++) {   // (never pitch black: a little light so you can just make things out)
        g.col[i] = (float)(L.sun[i] * 1.15 * day);
        g.fillCol[i] = (float)(L.skyTop[i] * day);
        g.ambSky[i] = (float)std::max(L.ambient[i] * 1.2 * day, 0.05);
        g.ambGround[i] = (float)std::max(L.ambient[i] * 0.75 * day, 0.05);
        if (gWorkLight) { g.ambSky[i] = std::max(g.ambSky[i], 0.62f); g.ambGround[i] = std::max(g.ambGround[i], 0.45f); }   // (the editor's work light)
    }
    g.fog[0] = (float)L.fogStart; g.fog[1] = (float)std::max(1.0, L.fogEnd - L.fogStart); g.fog[2] = 1.0f;
    if (gWorkLight == 2)   // fullbright: every face its own colour, no sun, no shade, no fog
        for (int i = 0; i < 3; i++) { g.col[i] = g.fillCol[i] = 0; g.ambSky[i] = g.ambGround[i] = 1.43f; g.fog[2] = 0; }
    setLighting(g);
    setFog(c3(L.skyHorizon, skyK(L)));
}
void mapRestoreLook() { setLighting(Lighting{}); setFog(Color{199, 219, 239, 255}); setLamps({}); }

void mapSetLamps(const gmap::Map& m, gmap::V3 eye, const std::vector<Lamp>& extra) {
    if (gWorkLight == 2) { setLamps({}); return; }   // (fullbright: lamps would only wash it out)
    std::vector<Lamp> out(extra.begin(), extra.begin() + std::min((int)extra.size(), MAX_LAMPS));
    struct C { double score; Lamp L; };
    std::vector<C> cand;
    for (auto& e : m.ents) {
        if (e.type != "light" || e.power <= 0) continue;
        gmap::LampAt a = gmap::lampOf(e);
        Lamp L;
        L.pos[0] = (float)a.pos.x; L.pos[1] = (float)a.pos.y; L.pos[2] = (float)a.pos.z;
        L.dir[0] = (float)a.dir.x; L.dir[1] = (float)a.dir.y; L.dir[2] = (float)a.dir.z;
        for (int i = 0; i < 3; i++) L.col[i] = (float)a.color[i];
        L.power = (float)a.power; L.range = (float)a.range; L.cosCone = (float)a.cosCone;
        cand.push_back({gmap::len(a.pos - eye) - a.range, L});   // the nearest edges of light first
    }
    std::sort(cand.begin(), cand.end(), [](const C& a, const C& b) { return a.score < b.score; });
    for (auto& c : cand) { if ((int)out.size() >= MAX_LAMPS) break; out.push_back(c.L); }
    setLamps(out);
}

void mapDrawSky(const gmap::Look& L, float x, float y, float w, float h, float pitch, float fovY) {
    // the horizon sits where a level line of sight lands on screen
    float t = std::tan(pitch) / std::tan(fovY * DEG2RAD / 2);
    float hy = y + h / 2 + t * h / 2;
    Color top = c3(L.skyTop, skyK(L)), hor = c3(L.skyHorizon, skyK(L));
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
