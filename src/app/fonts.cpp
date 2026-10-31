// Gunsmith3D - The UI typeface (see fonts.hpp).
#include "app/fonts.hpp"
#include "app/settings.hpp"
#include "app/font_inter.inc"

#include <vector>

FontData interRegular() { return {INTER_REGULAR, (int)sizeof INTER_REGULAR}; }
FontData interSemiBold() { return {INTER_SEMIBOLD, (int)sizeof INTER_SEMIBOLD}; }

static Font gView{};
static bool gViewOk = false;
void initViewFont() {
    std::vector<int> cps;
    for (int c = 32; c < 256; c++) cps.push_back(c);
    gView = LoadFontFromMemory(".otf", INTER_SEMIBOLD, (int)sizeof INTER_SEMIBOLD, 40, cps.data(), (int)cps.size());
    gViewOk = gView.texture.id != 0 && gView.glyphCount > 0;
    if (gViewOk) SetTextureFilter(gView.texture, TEXTURE_FILTER_BILINEAR);
}
void viewText(const char* t, float x, float y, float size, Color c) {
    size *= CFG.uiScale;
    if (gViewOk) DrawTextEx(gView, t, {x, y}, size, 0.3f, c);
    else DrawText(t, (int)x, (int)y, (int)size, c);
}
float viewTextWidth(const char* t, float size) {
    size *= CFG.uiScale;
    return gViewOk ? MeasureTextEx(gView, t, size, 0.3f).x : (float)MeasureText(t, (int)size);
}
