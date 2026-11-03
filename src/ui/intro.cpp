// Gunsmith3D - what the game shows at launch, before the main menu: the studio's splash (OAB Foundry), then the End User License
// Agreement, until the player has agreed to it once.
#include "ui/ui.hpp"
#include "app/settings.hpp"
#include "raylib.h"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {
const char* EULA_TEXT =
#include "eula.inc"
    ;
enum class Phase { Off, Splash, Eula };
Phase phase = Phase::Off;
float t = 0;
bool ticked = false;
constexpr float FADE_IN = 0.8f, HOLD = 1.6f, FADE_OUT = 0.8f;
float S() { return CFG.uiScale; }

std::string fingerprint() {   // which agreement was agreed to: a hash of its text (FNV-1a, 64 bits)
    uint64_t h = 1469598103934665603ull;
    for (const char* c = EULA_TEXT; *c; c++) { h ^= (unsigned char)*c; h *= 1099511628211ull; }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
    return b;
}
bool agreed() { return !CFG.eulaAgreed.empty(); }   // agreed once: never asked again (the text agreed to is kept on record)

void spaced(ImDrawList* dl, ImFont* f, float size, float cx, float y, const char* s, float gap, ImU32 col) {   // letter-spaced, centred
    float w = 0;
    for (const char* c = s; *c; c++) w += f->CalcTextSizeA(size, 1e9f, 0, c, c + 1).x + (c[1] ? gap : 0);
    float x = cx - w / 2;
    for (const char* c = s; *c; c++) {
        dl->AddText(f, size, {x, y}, col, c, c + 1);
        x += f->CalcTextSizeA(size, 1e9f, 0, c, c + 1).x + gap;
    }
}

void splash(float dt) {
    t += dt;
    bool skip = GetKeyPressed() != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    if (skip && t < FADE_IN + HOLD) t = FADE_IN + HOLD;   // a key or a click: straight to the fade out
    float a = t < FADE_IN ? t / FADE_IN : t < FADE_IN + HOLD ? 1.f : 1.f - (t - FADE_IN - HOLD) / FADE_OUT;
    a = std::fmax(0.f, std::fmin(1.f, a));
    a = a * a * (3 - 2 * a);   // eased
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight(), cx = W / 2, cy = H / 2;
    float big = std::fmin(64.f * S(), W / 14.f), small = big * 0.42f;
    ImU32 white = IM_COL32(236, 238, 242, (int)(255 * a)), gold = IM_COL32(232, 161, 58, (int)(255 * a)), dim = IM_COL32(150, 156, 168, (int)(255 * a));
    // the studio: OAB Foundry
    big *= 1.5f; small = big * 0.3f;
    spaced(dl, gFontBold, big, cx, cy - big * 0.95f, "OAB", big * 0.18f, white);
    float lw = big * 3.0f;
    dl->AddRectFilled({cx - lw / 2, cy + big * 0.28f}, {cx + lw / 2, cy + big * 0.28f + std::fmax(2.f, 2 * S())}, gold);
    spaced(dl, gFontBold, small, cx, cy + big * 0.45f, "FOUNDRY", small * 0.9f, dim);
    if (t >= FADE_IN + HOLD + FADE_OUT) { t = 0; phase = agreed() ? Phase::Off : Phase::Eula; }
}

bool heading(const std::string& l, bool first) {   // the title, and short numbered lines with no full stop ("4.2 Sharing")
    if (first) return true;
    if (l.empty() || l.size() > 70 || l.back() == '.' || l.back() == ':') return false;
    size_t i = 0;
    while (i < l.size() && (std::isdigit((unsigned char)l[i]) || l[i] == '.')) i++;
    return (i > 0 && i < l.size() && l[i] == ' ') || l == "Acknowledgment";
}
void agreementText() {   // the agreement, laid out: headings in bold, bullets, and "⸻" lines as dividers
    const std::string text = EULA_TEXT;
    size_t at = 0;
    bool first = true;
    while (at <= text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string l = text.substr(at, nl - at);
        at = nl + 1;
        if (l.empty()) { ImGui::Dummy({0, 4 * S()}); continue; }
        if (l == "\u2E3B") { ImGui::Dummy({0, 2 * S()}); ImGui::Separator(); ImGui::Dummy({0, 2 * S()}); continue; }
        if (l.size() > 2 && l[0] == '*' && l[1] == ' ') { ImGui::Bullet(); ImGui::TextWrapped("%s", l.c_str() + 2); continue; }
        if (heading(l, first)) {
            ImGui::PushFont(gFontBold, first ? 20.0f : 16.0f);
            ImGui::TextWrapped("%s", l.c_str());
            ImGui::PopFont();
        } else ImGui::TextWrapped("%s", l.c_str());
        first = false;
    }
}

void eula() {
    static bool tick = false;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float pw = std::fmin(780 * S(), W - 64 * S()), ph = std::fmin(640 * S(), H - 64 * S());
    ImGui::SetNextWindowPos({(W - pw) / 2, (H - ph) / 2});
    ImGui::SetNextWindowSize({pw, ph});
    ImGui::Begin("##eula", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(gFontBold, 26.0f);
    ImGui::TextUnformatted("End User License Agreement");
    ImGui::PopFont();
    ImGui::TextDisabled("Please read it, then agree to continue to Gunsmith3D.");
    ImGui::Dummy({0, 6 * S()});
    float foot = ImGui::GetFrameHeightWithSpacing() * 2 + 18 * S();
    ImGui::BeginChild("##text", {0, -foot}, ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0);
    agreementText();
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::Dummy({0, 4 * S()});
    check("I have read and agree to this agreement", tick);
    ImGui::Dummy({0, 4 * S()});
    float bw = 200 * S();
    ImGui::BeginDisabled(!tick);
    if (accentButton("I agree", true, {bw, 0})) { CFG.eulaAgreed = fingerprint(); saveSettings(); phase = Phase::Off; }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Decline", {bw * 0.7f, 0})) A.quit = true;   // not agreed: the game closes
    ImGui::End();
}
}  // namespace

void introStart(bool eulaOnly) { t = 0; phase = eulaOnly ? (agreed() ? Phase::Off : Phase::Eula) : Phase::Splash; }
void introForceEula() { t = 0; phase = Phase::Eula; }
bool introActive() { return phase != Phase::Off; }
void introFrame(float dt) {
    if (phase == Phase::Splash) splash(dt);
    else if (phase == Phase::Eula) eula();
}
