// Gunsmith3D - the Reference pictures window (designer): a picture behind the side view and one under the top view,
// set to the real gun's size and levelled, to trace over. See app/refpics.hpp.
#include "ui/ui.hpp"
#include "app/app.hpp"
#include "app/refpics.hpp"
#include "app/usertex.hpp"
#include "app/settings.hpp"
#include "app/units.hpp"
#include "app/view.hpp"
#include "imgui.h"
#include "extras/IconsFontAwesome6.h"
#include <string>
#include <cstdlib>

bool gShowRefs = false;
static int gRefTab = 0;
static double gRealLen = 0;

static void refViewPanel(int view) {
    RefPicture& r = refOf(view);
    float S = CFG.uiScale;
    if (ImGui::Button(r.on ? ICON_FA_FOLDER_OPEN "  Change the picture..." : ICON_FA_IMAGE "  Load a picture...", {-1, 0})) {
        std::string path;
        if (pickImageFile(path)) { if (refLoad(view, path)) commit(); else toast("That picture couldn't be read."); }
    }
    if (!r.on) {
        ImGui::TextWrapped(view == 0 ? "A side-on photo or drawing of the real gun (muzzle either way: you can flip it). You can also drop a picture on the window while this is open."
                                     : "A photo or drawing of the real gun from above. You can also drop a picture on the window while this is open.");
        return;
    }
    if (refPictureMissing(view)) ImGui::TextColored({1, 0.6f, 0.3f, 1}, "The picture %s isn't in your References folder.", r.file.c_str());
    else ImGui::TextDisabled("%s", r.file.c_str());
    bool ch = false;
    ch |= ImGui::Checkbox("Show it", &r.shown);
    ImGui::SameLine(); ch |= ImGui::Checkbox("In front of the parts", &r.front);
    ImGui::SameLine(); ch |= ImGui::Checkbox("Flip", &r.flip);
    float op = (float)r.opacity;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##op", &op, 0.05f, 1.f, "See-through %.2f")) { r.opacity = op; }
    if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
    ImGui::Separator();
    // the three steps, in order
    int mode = refView() == view ? refMode() : 0;
    auto step = [&](const char* label, int m, const char* help) {
        bool on = mode == m;
        if (accentButton(label, on, {-1, 0})) refSetMode(view, on ? 0 : m);
        ImGui::TextDisabled("%s", help);
    };
    step("1  Set the scale: mark a length you know", 2, "Click both ends of something you know the size of\n(the barrel, the whole gun, the magazine), then type it.");
    if (refView() == view && refMarked()) {
        ImGui::Text("That's %s on the picture now. Really:", fmtQ(Q::Length, refMarkedMm()).c_str());
        if (gRealLen <= 0) gRealLen = refMarkedMm();
        ImGui::SetNextItemWidth(140 * S);
        numFieldU("##real", gRealLen, 1, Q::Length);
        ImGui::SameLine();
        if (accentButton("Set", true)) { refApplyLength(gRealLen); gRealLen = 0; }
    }
    step("2  Level it: mark a line that runs straight", 3, view == 0 ? "Click two points along the bore or the top of the slide." : "Click two points along the barrel.");
    step("3  Move it: drag it into place", 1, view == 0 ? "Drag it so the bore sits on your barrel, the breech\nface on your breech. Right-click or Esc when done." : "Drag it so the barrel lines up. Right-click or Esc when done.");
    ImGui::Separator();
    if (ImGui::Button(view == 0 ? ICON_FA_EYE "  Look from the side" : ICON_FA_EYE "  Look from above")) snapView(view == 0 ? View::Right : View::Top);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH "  Remove the picture")) { r = RefPicture{}; refSetMode(view, 0); ch = true; }
    if (ch) commit();
}

void refWindow() {
    static bool envDone = false;
    if (!envDone) { envDone = true; if (getenv("GUNSMITH_REFWIN")) gShowRefs = true; }
    if (!gShowRefs) return;
    if (A.mode != Mode::Build) return;
    if (IsFileDropped()) {   // a picture dropped while this is open: it's the picture for the view being set up
        FilePathList list = LoadDroppedFiles();
        if (list.count > 0 && isImageFile(list.paths[0])) { if (refLoad(gRefTab, list.paths[0])) { commit(); toast("Picture loaded."); } }
        UnloadDroppedFiles(list);
    }
    float S = CFG.uiScale;
    ImGui::SetNextWindowSize({380 * S, 0}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({70 * S, 90 * S}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_IMAGE "  Reference pictures", &gShowRefs, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    ImGui::PushTextWrapPos(360 * S);
    ImGui::TextWrapped("Put a picture of the real gun behind your work, set it to its real size, then trace over it.");
    ImGui::PopTextWrapPos();
    if (ImGui::BeginTabBar("##refviews")) {
        if (ImGui::BeginTabItem("Side view")) { if (gRefTab != 0) refSetMode(0, 0); gRefTab = 0; refViewPanel(0); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Top view")) { if (gRefTab != 1) refSetMode(1, 0); gRefTab = 1; refViewPanel(1); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (!gShowRefs) refSetMode(gRefTab, 0);
}
