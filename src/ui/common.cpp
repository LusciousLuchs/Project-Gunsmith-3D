// Gunsmith3D - Shared widgets, panel header, hint overlay, theme.
#include "app/app.hpp"
#include "app/view.hpp"
#include "ui/ui.hpp"
#include "imgui.h"
#include "rlImGui.h"
#include "app/settings.hpp"
#include "app/units.hpp"
#include "app/usertex.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------- UI helpers
bool editedAny = false;
void markEdit() { if (ImGui::IsItemEdited()) editedAny = true; }
bool inputText(const char* label, std::string& s) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s", s.c_str());
    bool ch = ImGui::InputText(label, buf, sizeof buf);
    if (ch) s = buf;
    markEdit();
    return ch;
}
bool bodyCombo(const char* label, std::string& id, bool withWorld) {
    bool ch = false;
    if (ImGui::BeginCombo(label, bname(id).c_str())) {
        if (withWorld && ImGui::Selectable("Ground (fixed)", id == "world")) { id = "world"; ch = true; }
        for (auto& b : A.design.bodies)
            if (ImGui::Selectable((b.name + "##" + b.id).c_str(), id == b.id)) { id = b.id; ch = true; }
        ImGui::EndCombo();
    }
    if (ch) editedAny = true;
    return ch;
}
void numField(const char* label, double& v, double step, const char* fmt) {
    ImGui::InputDouble(label, &v, step, step * 10, fmt);
    markEdit();
}
bool numFieldU(const char* label, double& v, double step, Q q, int decimals, double lo) {
    double d = toDisp(q, v), st = niceStep(q, step), dlo = toDisp(q, lo), dhi = 1e9;
    char fmt[32];
    std::snprintf(fmt, sizeof fmt, "%%.%df %s", decimals >= 0 ? decimals : decimalsOf(q), unitOf(q));
    // drag sideways to scrub the value, or click it (without dragging) to type it
    // (sizes stop at their smallest sensible value, typed or dragged; offsets pass a large negative lo)
    bool ch = ImGui::DragScalar(label, ImGuiDataType_Double, &d, (float)(st * 0.2), &dlo, &dhi, fmt, ImGuiSliderFlags_AlwaysClamp);
    if (ch) { v = std::max(lo, fromDisp(q, d)); editedAny = true; }
    return ch;
}
// Sliders only switch to typing on Ctrl+click; let a double-click do it too. Call right after the slider.
void typeable() {
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ImGui::SetKeyboardFocusHere(-1);
}
bool texturePicker(std::string& file) {
    bool ch = false;
    const auto& files = userTextureFiles();
    std::string shown = textureName(file);
    if (ImGui::BeginCombo("##tex", file.empty() ? "Plain (just the colour)" : shown.c_str())) {
        if (ImGui::Selectable("Plain (just the colour)", file.empty())) { file.clear(); ch = true; }
        ImGui::SeparatorText("Gun finishes");
        for (auto& b : builtinTextures()) {
            Texture2D t = userTexture(b.file);
            ImGui::PushID(b.file.c_str());
            rlImGuiImageSize(&t, (int)(18 * CFG.uiScale), (int)(18 * CFG.uiScale));
            ImGui::SameLine();
            if (ImGui::Selectable(b.name.c_str(), b.file == file)) { file = b.file; ch = true; }
            ImGui::PopID();
        }
        if (!files.empty()) ImGui::SeparatorText("Your pictures");
        for (auto& f : files) {
            Texture2D t = userTexture(f);
            ImGui::PushID(f.c_str());
            rlImGuiImageSize(&t, (int)(18 * CFG.uiScale), (int)(18 * CFG.uiScale));
            ImGui::SameLine();
            if (ImGui::Selectable(f.c_str(), f == file)) { file = f; ch = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
#ifdef _WIN32
    if (ImGui::Button("Add a picture...", {w2, 0})) {
        std::string p;
        if (pickImageFile(p)) { std::string n = importTexture(p); if (!n.empty()) { file = n; ch = true; } else toast("That file isn't a picture the game can read."); }
    }
    ImGui::SameLine();
#endif
    if (ImGui::Button("Open textures folder", {w2, 0})) { scanUserTextures(); openTexturesFolder(); }
    if (tipHover()) ImGui::SetTooltip("Pictures in user/textures in the game folder show up here (.png, .jpg, .bmp, .tga).\nYou can also drag pictures onto the game window.");
    return ch;
}
bool tipHover() { return CFG.tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort); }
ImVec4 colGood() { return CFG.colorblind ? ImVec4(0.45f, 0.68f, 1.0f, 1) : ImVec4(0.47f, 0.84f, 0.65f, 1); }
ImVec4 colBad() { return CFG.colorblind ? ImVec4(1.0f, 0.62f, 0.2f, 1) : ImVec4(1.0f, 0.5f, 0.4f, 1); }
ImVec4 colWarn() { return CFG.colorblind ? ImVec4(0.95f, 0.85f, 0.4f, 1) : ImVec4(1.0f, 0.82f, 0.45f, 1); }
ImVec4 colAccent() { return ImVec4(0.91f, 0.63f, 0.23f, 1); }
void check(const char* label, bool& v) { ImGui::Checkbox(label, &v); markEdit(); }
bool accentButton(const char* label, bool on, ImVec2 size) {
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.94f, 0.64f, 0.23f, 1)), ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.07f, 0.02f, 1));
    bool r = ImGui::Button(label, size);
    if (on) ImGui::PopStyleColor(2);
    return r;
}


const char* JT_LABEL(const std::string& t) {
    if (t == "pin") return "Pin"; if (t == "weld") return "Weld"; if (t == "track") return "Track / cam slot"; if (t == "spring") return "Spring";
    if (t == "cam") return "Helical cam"; if (t == "lug") return "Locking lugs"; if (t == "latch") return "Catch"; if (t == "magwell") return "Magazine well"; return "?";
}
std::string jointDesc(const JointDef& j) {
    if (j.type == "track") return bname(j.a) + " rides on " + bname(j.b);
    if (j.type == "cam") return bname(j.a) + " turned by " + bname(j.b);
    if (j.type == "lug") return bname(j.a) + " locks into " + bname(j.b);
    if (j.type == "latch") return bname(j.a) + " held, released by " + bname(j.rel);
    if (j.type == "magwell") return bname(j.a) + " seats in " + bname(j.b);
    return bname(j.a) + " + " + bname(j.b);
}

void panelHeader() {
    auto& D = A.design;
    ImGui::SetNextWindowPos({(float)GetScreenWidth() - PANEL_W, TOOLBAR_H});
    ImGui::SetNextWindowSize({PANEL_W, (float)GetScreenHeight() - TOOLBAR_H});
    ImGui::Begin("##panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushItemWidth(-1);
    ImGui::PushFont(gFontBold, 17.0f);
    if (A.mode == Mode::Build) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::SetNextItemWidth(-1);
        inputText("##name", D.name);
        ImGui::PopStyleColor();
        if (tipHover()) ImGui::SetTooltip("The design's name. Click to rename.");
    } else {
        ImGui::TextUnformatted(D.name.c_str());
    }
    ImGui::PopFont();
    if (A.mode == Mode::Test) ImGui::TextDisabled(A.benchOnly ? "On the test bench." : "Testing never changes your design.");
}

// ---------------------------------------------------------------- property rows: label on the left, control on the right
static float labelW() { return 118 * CFG.uiScale; }
void prop(const char* label, const char* tip) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.69f, 0.74f, 1));
    ImGui::TextUnformatted(label, std::strstr(label, "##"));  // "Depth##cut": shows "Depth", kept apart by the rest
    ImGui::PopStyleColor();
    if (tip && tipHover()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(labelW());
    ImGui::SetNextItemWidth(-1);
}
static std::string pid(const char* label) { return std::string("##") + label; }
bool pNumU(const char* label, double& v, double step, Q q, int dec, const char* tip, double lo) {
    prop(label, tip);
    bool r = numFieldU(pid(label).c_str(), v, step, q, dec, lo);
    if (tip && tipHover()) ImGui::SetTooltip("%s", tip);
    return r;
}
bool pNum(const char* label, double& v, double step, const char* fmt, const char* tip) {
    prop(label, tip);
    bool r = ImGui::DragScalar(pid(label).c_str(), ImGuiDataType_Double, &v, (float)(step * 0.2), nullptr, nullptr, fmt);
    if (r) editedAny = true;
    return r;
}
bool pCheck(const char* label, bool& v, const char* tip) {
    prop(label, tip);
    bool r = ImGui::Checkbox(pid(label).c_str(), &v);
    if (r) editedAny = true;
    if (tip && tipHover()) ImGui::SetTooltip("%s", tip);
    return r;
}
bool pText(const char* label, std::string& s) { prop(label); return inputText(pid(label).c_str(), s); }
bool pBody(const char* label, std::string& id, bool withWorld) { prop(label); return bodyCombo(pid(label).c_str(), id, withWorld); }
bool section(const char* name, bool open) {
    ImGui::Dummy({0, 2 * CFG.uiScale});
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.04f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.07f));
    ImGui::PushFont(gFontBold, 15.0f);
    bool r = ImGui::CollapsingHeader(name, open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    return r;
}
void heading(const char* t) {
    ImGui::Dummy({0, 6 * CFG.uiScale});
    ImGui::PushFont(gFontBold, 15.0f);
    ImGui::TextUnformatted(t);
    ImGui::PopFont();
}
bool dangerButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Text, colBad());
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    return r;
}

void toastOverlay() {  // just the messages, for screens without a 3D workshop
    if (A.toastT <= 0) return;
    ImGui::SetNextWindowPos({GetScreenWidth() / 2.f, GetScreenHeight() - 60.f * CFG.uiScale}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(colWarn(), "%s", A.toast.c_str());
    ImGui::End();
}
// One quiet line at the bottom of the 3D view: what the current tool does, plus any message.
void hintOverlay() {
    std::string h;
    if (CFG.hints) {
        if (A.mode == Mode::Test) h = A.running ? "Drag any moving part, bolt handle or round to work it by hand." : "Paused.";
        else if (A.tool == Tool::Track) h = trackHint();
        else h = TOOL_HINT[(int)A.tool];
    }
    bool click = A.clickT > 0, msg = A.toastT > 0;
    if (h.empty() && !click && !msg) return;
    float s = CFG.uiScale;
    ImGui::SetNextWindowPos({12 * s, TOOLBAR_H + A.vh - 12 * s}, ImGuiCond_Always, {0, 1});
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::SetNextWindowSizeConstraints({0, 0}, {std::max(200.f, A.vw - 24 * s), 400});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10 * s, 6 * s});
    ImGui::Begin("##hint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize);
    if (msg) ImGui::TextColored(colWarn(), "%s", A.toast.c_str());
    if (click) ImGui::TextColored(colBad(), "Click. The chamber is empty.");
    if (!h.empty()) ImGui::TextDisabled("%s", h.c_str());
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------- fonts and theme
#include "app/fonts.hpp"
ImFont* gFontBold = nullptr;
static void loadFonts() {  // Inter for text, with Font Awesome icons merged in by rlImGui
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    FontData f = interRegular();
    io.Fonts->AddFontFromMemoryTTF((void*)f.data, f.size, 15.0f, &cfg);
}
void setupImGui() {
    rlImGuiSetLoadFontsCallback(loadFonts);
    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigDragClickToInputText = true;  // click a number without dragging to type it
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    FontData fb = interSemiBold();
    gFontBold = io.Fonts->AddFontFromMemoryTTF((void*)fb.data, fb.size, 15.0f, &cfg);
    if (CFG.uiScaleAuto) CFG.uiScale = autoUiScale();
    applyUiScale();
}
float autoUiScale() {
    // from the game window itself (not the monitor): the interface is laid out for 1600 x 1080 at 100%, so it grows and
    // shrinks with the window and always fits. 1080p 100%, 1440p about 133%, 4K 200%, a 1440 x 900 window about 85%.
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight();
    if (w <= 0 || h <= 0) return 1.0f;
    float s = std::min(h / 1080.f, w / 1600.f);
    return std::clamp(std::round(s * 20) / 20, 0.75f, 2.0f);
}
void setUiScale(float s, bool chosen) {
    CFG.uiScale = std::clamp(std::round(s * 20) / 20, 0.75f, 2.0f);
    if (chosen) CFG.uiScaleAuto = false;
    applyUiScale();
    saveSettings();
}
void applyUiScale() {
    float s = CFG.uiScale;
    ImGui::GetStyle() = ImGuiStyle();
    styleUI();
    ImGui::GetStyle().ScaleAllSizes(s);
    ImGui::GetStyle().FontScaleMain = s;
    PANEL_W = PANEL_W_BASE * s;
    TOOLBAR_H = TOOLBAR_H_BASE * s;
}

// A quiet dark theme: charcoal surfaces, one amber accent used sparingly.
void styleUI() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.ChildRounding = 4; s.FrameRounding = 4; s.PopupRounding = 4; s.GrabRounding = 4; s.ScrollbarRounding = 6; s.TabRounding = 4;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 0; s.ChildBorderSize = 0;
    s.FramePadding = {9, 5}; s.ItemSpacing = {8, 7}; s.ItemInnerSpacing = {6, 5}; s.WindowPadding = {14, 12};
    s.IndentSpacing = 14; s.ScrollbarSize = 10; s.GrabMinSize = 10; s.SeparatorTextBorderSize = 1; s.SeparatorTextPadding = {0, 4};
    ImVec4* c = s.Colors;
    auto rgb = [](int r, int g, int b, float a = 1) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); };
    ImVec4 acc = rgb(232, 161, 58);
    c[ImGuiCol_Text] = rgb(228, 230, 234);
    c[ImGuiCol_TextDisabled] = rgb(128, 134, 145);
    c[ImGuiCol_WindowBg] = rgb(22, 24, 28, 0.97f);
    c[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgb(28, 31, 36, 0.99f);
    c[ImGuiCol_Border] = rgb(255, 255, 255, 0.06f);
    c[ImGuiCol_FrameBg] = rgb(36, 40, 47);
    c[ImGuiCol_FrameBgHovered] = rgb(44, 49, 57);
    c[ImGuiCol_FrameBgActive] = rgb(52, 58, 68);
    c[ImGuiCol_Button] = rgb(38, 42, 49);
    c[ImGuiCol_ButtonHovered] = rgb(50, 55, 64);
    c[ImGuiCol_ButtonActive] = rgb(62, 68, 79);
    c[ImGuiCol_Header] = rgb(40, 44, 51);
    c[ImGuiCol_HeaderHovered] = rgb(50, 55, 64);
    c[ImGuiCol_HeaderActive] = rgb(58, 64, 74);
    c[ImGuiCol_CheckMark] = acc;
    c[ImGuiCol_CheckboxSelectedBg] = rgb(48, 53, 61);
    c[ImGuiCol_TextSelectedBg] = rgb(232, 161, 58, 0.30f);
    c[ImGuiCol_TextLink] = acc;
    c[ImGuiCol_InputTextCursor] = rgb(228, 230, 234);
    c[ImGuiCol_TabSelectedOverline] = acc;
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = rgb(232, 161, 58, 0.4f);
    c[ImGuiCol_ResizeGripActive] = acc;
    c[ImGuiCol_DragDropTarget] = acc;
    c[ImGuiCol_SeparatorActive] = acc;
    c[ImGuiCol_PlotLines] = rgb(150, 156, 166);
    c[ImGuiCol_TreeLines] = rgb(255, 255, 255, 0.08f);
    c[ImGuiCol_SliderGrab] = rgb(150, 156, 166);
    c[ImGuiCol_SliderGrabActive] = acc;
    c[ImGuiCol_TitleBg] = rgb(18, 20, 23);
    c[ImGuiCol_TitleBgActive] = rgb(24, 27, 31);
    c[ImGuiCol_Separator] = rgb(255, 255, 255, 0.08f);
    c[ImGuiCol_SeparatorHovered] = rgb(232, 161, 58, 0.5f);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(60, 65, 74);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(75, 81, 92);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(90, 97, 110);
    c[ImGuiCol_Tab] = rgb(30, 33, 38);
    c[ImGuiCol_TabHovered] = rgb(50, 55, 64);
    c[ImGuiCol_TabSelected] = rgb(44, 49, 57);
    c[ImGuiCol_PlotHistogram] = acc;
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);
    c[ImGuiCol_NavCursor] = acc;
}
