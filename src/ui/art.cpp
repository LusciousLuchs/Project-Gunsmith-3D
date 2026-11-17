// Gunsmith3D - the owner's artwork, built into the game (tools/embed_logo.py): the title splash, the main menu's
// workshop picture and logo, and the window's icon. Loaded the first time they're asked for.
#include "ui/ui.hpp"
#include "raylib.h"

namespace {
#include "art.inc"
Texture2D load(const char* ext, const unsigned char* data, int n) {
    Texture2D t{};
    Image img = LoadImageFromMemory(ext, data, n);
    if (!img.data) return t;
    t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}
}  // namespace

Texture2D artTexture(Art a) {
    static Texture2D tex[3]{};
    static bool tried[3]{};
    int i = (int)a;
    if (!tried[i]) {
        tried[i] = true;
        if (a == Art::TitleSplash) tex[i] = load(".jpg", TITLE_SPLASH_JPG, (int)sizeof(TITLE_SPLASH_JPG));
        else if (a == Art::MenuBackground) tex[i] = load(".jpg", MENU_BG_JPG, (int)sizeof(MENU_BG_JPG));
        else tex[i] = load(".png", MENU_LOGO_PNG, (int)sizeof(MENU_LOGO_PNG));
    }
    return tex[i];
}
void setGameIcon() {   // the window's own icon (on Windows the .exe's icon does it as well)
    Image img = LoadImageFromMemory(".png", ICON_PNG, (int)sizeof(ICON_PNG));
    if (!img.data) return;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageResize(&img, 128, 128);
    SetWindowIcon(img);
    UnloadImage(img);
}
// Draws a picture covering the rectangle (cropped, never stretched), on the ImGui draw list given
void drawCover(ImDrawList* dl, Texture2D t, ImVec2 a, ImVec2 b, float alignX, ImU32 tint) {
    if (!t.id) return;
    float w = b.x - a.x, h = b.y - a.y, ta = (float)t.width / t.height, ra = w / h;
    float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
    if (ra > ta) { float vis = ta / ra; v0 = (1 - vis) * 0.5f; v1 = v0 + vis; }
    else { float vis = ra / ta; u0 = (1 - vis) * alignX; u1 = u0 + vis; }
    dl->AddImage((ImTextureID)(intptr_t)t.id, a, b, {u0, v0}, {u1, v1}, tint);
}
