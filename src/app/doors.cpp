// Gunsmith3D - doors on maps (see doors.hpp).
#include "app/doors.hpp"
#include "app/gfx.hpp"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace doors {
namespace {
using gmap::V3;
constexpr double PI_D = 3.14159265358979;
V3 facingDir(const gmap::Entity& e) { double a = e.yaw * PI_D / 180; return {std::cos(a), 0, std::sin(a)}; }
V3 across(const gmap::Entity& e) { V3 f = facingDir(e); return {-f.z, 0, f.x}; }

// ---- the doors' own pictures (a whole door face, not a repeating tile)
float hashf(int x, int y, int s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
    return (h & 0xffff) / 65535.f;
}
float noise(float x, float y, int s) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi; fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hashf(xi, yi, s), b = hashf(xi + 1, yi, s), c = hashf(xi, yi + 1, s), d = hashf(xi + 1, yi + 1, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
float fbm(float x, float y, int s) { float r = 0, amp = 0.5f, tot = 0; for (int o = 0; o < 5; o++) { r += amp * noise(x, y, s + o * 17); tot += amp; x *= 2; y *= 2; amp *= 0.5f; } return r / tot; }
unsigned char cl(float v) { return (unsigned char)std::clamp(v, 0.f, 255.f); }
Texture2D gTex[3] = {};
Texture2D doorTexture(int style) {   // 0 wood, 1 metal, 2 glass (its frame)
    if (gTex[style].id) return gTex[style];
    const int W = 256, H = 512;
    std::vector<Color> px(W * H);
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            float u = (i + 0.5f) / W, v = 1 - (j + 0.5f) / H;   // (v from the top of the door: the cube's faces run the other way)
            float r, g, b;
            if (style == 0) {   // a panelled wood door: dark stain, six raised panels in a frame, worn round the handle
                float gr = std::sin((v * 70 + fbm(u * 4, v * 16, 3) * 6) * 3.14159f) * 0.5f + 0.5f;
                float k = 0.78f + gr * 0.2f;
                int col = u < 0.5f ? 0 : 1, row = v < 0.33f ? 0 : v < 0.62f ? 1 : 2;
                float px0 = col ? 0.56f : 0.14f, px1 = col ? 0.86f : 0.44f;
                float py0 = row == 0 ? 0.07f : row == 1 ? 0.37f : 0.67f, py1 = row == 0 ? 0.30f : row == 1 ? 0.58f : 0.93f;
                bool inPanel = u > px0 && u < px1 && v > py0 && v < py1;
                float edge = std::min(std::min(u - px0, px1 - u) * W, std::min(v - py0, py1 - v) * H);
                if (inPanel && edge < 7) k *= 0.62f + edge / 7 * 0.25f;   // the bevel round each panel
                float wear = std::max(0.f, fbm(u * 6, v * 6, 9) - 0.6f) * 0.6f;
                if (u > 0.78f && v > 0.45f && v < 0.6f) wear += 0.15f;
                r = (98 + wear * 80) * k; g = (62 + wear * 50) * k; b = (36 + wear * 30) * k;
            } else if (style == 1) {   // a steel door: worn green-grey paint, a small wired window, a kick plate, scuffs
                float n = (fbm(u * 8, v * 16, 21) - 0.5f) * 22;
                float wear = std::max(0.f, fbm(u * 10, v * 20, 22) - 0.64f) * 220;
                r = 70 + n + wear * 0.55f; g = 88 + n + wear * 0.5f; b = 80 + n + wear * 0.45f;
                if (u > 0.3f && u < 0.7f && v > 0.12f && v < 0.3f) {   // the window: dark glass with a wire mesh
                    bool wire = std::fmod(u * W, 16.f) < 1.5f || std::fmod(v * H, 16.f) < 1.5f;
                    r = wire ? 150 : 40; g = wire ? 150 : 52; b = wire ? 150 : 58;
                    if (u < 0.31f || u > 0.69f || v < 0.13f || v > 0.29f) { r = g = b = 30; }
                }
                if (v > 0.86f) { float kp = (fbm(u * 30, v * 4, 23) - 0.5f) * 30; r = 120 + kp; g = 122 + kp; b = 126 + kp; }   // kick plate
                if (std::fabs(u - 0.5f) > 0.47f || v < 0.015f) { r *= 0.6f; g *= 0.6f; b *= 0.6f; }
            } else {   // a glass door's aluminium frame (the glass itself is drawn see-through)
                float n = (fbm(u * 8, v * 16, 31) - 0.5f) * 14;
                r = 168 + n; g = 172 + n; b = 178 + n;
            }
            px[j * W + i] = Color{cl(r), cl(g), cl(b), 255};
        }
    Image img{px.data(), W, H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    gTex[style] = LoadTextureFromImage(img);
    GenTextureMipmaps(&gTex[style]);
    SetTextureFilter(gTex[style], TEXTURE_FILTER_TRILINEAR);
    return gTex[style];
}
int styleOf(const gmap::Entity& e) { return e.ref == "metal" ? 1 : e.ref == "glass" ? 2 : 0; }
}  // namespace

void draw(const gmap::Map& m, int selected) {
    for (int i = 0; i < (int)m.ents.size(); i++) {
        const auto& e = m.ents[i];
        if (e.type != "door") continue;
        V3 f = facingDir(e), t = across(e), h, q;
        slab(m, i, h, q);
        Color frame{58, 52, 46, 255};
        if (i == selected) frame = Color{255, 205, 60, 255};
        // the frame: two jambs and a lintel
        Matrix R = MatrixRotateY(-(float)e.yaw * DEG2RAD);
        for (double sd : {-1.0, 1.0}) {
            V3 c = e.pos + t * (sd * (e.width / 2 + 0.04));
            drawBox({(float)c.x, (float)(e.pos.y + HEIGHT / 2 + 0.03), (float)c.z}, {0.14f, (float)HEIGHT + 0.06f, 0.08f}, frame, R);
        }
        drawBox({(float)e.pos.x, (float)(e.pos.y + HEIGHT + 0.07), (float)e.pos.z}, {0.14f, 0.08f, (float)e.width + 0.16f}, frame, R);
        // the slab, turned about its hinge
        V3 mid = (h + q) * 0.5;
        double ang = std::atan2(q.z - h.z, q.x - h.x);
        Matrix M = MatrixMultiply(MatrixScale((float)e.width, (float)HEIGHT, (float)THICK), MatrixRotateY((float)(-ang)));
        M = MatrixMultiply(M, MatrixTranslate((float)mid.x, (float)(e.pos.y + HEIGHT / 2), (float)mid.z));
        int st = styleOf(e);
        if (st == 2) {   // glass: a light frame round a see-through pane
            Matrix Mg = MatrixMultiply(MatrixScale((float)e.width * 0.84f, (float)HEIGHT * 0.88f, (float)THICK * 0.5f), MatrixRotateY((float)(-ang)));
            Mg = MatrixMultiply(Mg, MatrixTranslate((float)mid.x, (float)(e.pos.y + HEIGHT / 2), (float)mid.z));
            V3 dir{(q.x - h.x) / e.width, 0, (q.z - h.z) / e.width};
            for (double sd : {-1.0, 1.0}) {   // the stiles
                V3 c = mid + dir * (sd * e.width * 0.46);
                drawBox({(float)c.x, (float)(e.pos.y + HEIGHT / 2), (float)c.z}, {(float)(e.width * 0.08), (float)HEIGHT, (float)THICK}, Color{170, 174, 180, 255}, MatrixRotateY((float)(-ang)));
            }
            for (double y : {0.04, HEIGHT - 0.06}) drawBox({(float)mid.x, (float)(e.pos.y + y), (float)mid.z}, {(float)e.width, 0.12f, (float)THICK}, Color{170, 174, 180, 255}, MatrixRotateY((float)(-ang)));
            rlDisableDepthMask();
            drawMesh(G.cube, Mg, Color{180, 215, 230, 255}, 0.28f);
            rlEnableDepthMask();
        } else drawMeshTex(G.cube, M, WHITE, doorTexture(st));
        // the handle, on the free end's side, both faces
        V3 dir{(q.x - h.x) / e.width, 0, (q.z - h.z) / e.width}, nrm{-dir.z, 0, dir.x};
        V3 hp = h + dir * (e.width * 0.88);
        for (double sd : {-1.0, 1.0}) {
            V3 c = hp + nrm * (sd * 0.05);
            drawBox({(float)c.x, (float)(e.pos.y + 1.0), (float)c.z}, {0.12f, 0.025f, 0.025f}, Color{190, 170, 110, 255}, MatrixRotateY((float)(-ang)));
        }
        (void)f;
    }
}

}  // namespace doors
