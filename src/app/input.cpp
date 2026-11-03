// Gunsmith3D - Camera controls, routing viewport input, keyboard shortcuts.
#include "app/pieces.hpp"
#include "app/pose.hpp"
#include "app/app.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "app/paths.hpp"
#include "app/bench.hpp"
#include "app/input.hpp"
#include "app/keys.hpp"
#include "app/settings.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>

static void cameraInput(bool overUI) {
    const int ORBIT = CFG.orbitMiddle ? MOUSE_BUTTON_MIDDLE : MOUSE_BUTTON_RIGHT;
    const int PAN = CFG.orbitMiddle ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_MIDDLE;
    Vector2 m = GetMousePosition();
    if (!overUI) {
        float wheel = GetMouseWheelMove() * (CFG.invertZoom ? -1.f : 1.f);
        if (wheel != 0) {
            float step = std::pow(1.12f, CFG.zoomSpeed), f = wheel > 0 ? 1 / step : step;
            if (A.ortho) A.orthoH = std::clamp(A.orthoH * f, 2.f, 400.f);
            else A.dist = std::clamp(A.dist * f, 3.f, 600.f);
        }
        if (IsMouseButtonPressed(ORBIT)) { A.dragKind = 4; A.pressPos = m; A.dragMoved = false; }
        if (IsMouseButtonPressed(PAN)) { A.dragKind = 5; A.pressPos = m; }
    }
    Vector2 md = GetMouseDelta();
    if (A.dragKind == 4 && IsMouseButtonDown(ORBIT)) {
        if (Vector2Distance(m, A.pressPos) > 3) A.dragMoved = true;
        if (A.dragMoved) {  // (orthographic stays orthographic: turn it to any angle)
            A.yaw -= md.x * 0.006f;
            A.pitch = std::clamp(A.pitch + md.y * 0.006f, -PI / 2, PI / 2);
        }
    }
    if (A.dragKind == 4 && IsMouseButtonReleased(ORBIT)) {
        if (!A.dragMoved && A.mode == Mode::Build && ORBIT == MOUSE_BUTTON_RIGHT) {  // right click: delete vertex or track point, split an edge
            if (auto* h = handleAt(m)) {
                if (h->kind == 0) { auto& d = A.design.bodies[h->i]; if (d.pts.size() > 3) { fixShape(d); d.pts.erase(d.pts.begin() + h->v); d.rad.erase(d.rad.begin() + h->v); d.bulge.erase(d.bulge.begin() + h->v); A.sel.v = -1; commit(); } }
                else if (h->kind == 7 && splitReady()) {  // edge diamond: split it in half
                    auto& d = A.design.bodies[h->i];
                    int nv = splitEdge(d, h->v);
                    if (nv < 0) cornerLimitToast();
                    else { A.sel.v = nv; A.selVs = {nv}; commit(); }
                }
                else if (h->kind == 8) { auto& d = A.design.bodies[h->i]; if (d.roundPath.size() > 2) { d.roundPath.erase(d.roundPath.begin() + h->v); commit(); } }
                else if (h->kind == 2) {
                    auto& j = A.design.joints[h->i];
                    if (j.type == "track" && j.path.size() > 2) { j.path.erase(j.path.begin() + h->v); commit(); }
                    else if (j.type == "magwell" && h->v > 0 && h->v + 1 < (int)j.path.size()) {   // a bend in a magazine's insertion path
                        j.path.erase(j.path.begin() + h->v);
                        if (h->v < (int)j.pathAng.size()) j.pathAng.erase(j.pathAng.begin() + h->v);
                        commit();
                    }
                }
            }
        }
        A.dragKind = 0;
    }
    if (A.dragKind == 5) {
        if (IsMouseButtonDown(PAN)) {
            float scale = A.ortho ? A.orthoH / A.vh : A.dist * 2 * std::tan(20 * DEG2RAD) / A.vh;
            Vector3 fwd = Vector3Normalize(Vector3Subtract(A.cam.target, A.cam.position));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, {0, 1, 0}));
            Vector3 up = Vector3CrossProduct(right, fwd);
            A.target = Vector3Add(A.target, Vector3Add(Vector3Scale(right, -md.x * scale), Vector3Scale(up, md.y * scale)));
        } else A.dragKind = 0;
    }
}


void handleViewportInput() {
    ImGuiIO& io = ImGui::GetIO();
    bool overUI = io.WantCaptureMouse || !mouseInViewport();
    cameraInput(overUI);
    if (A.dragKind == 4 || A.dragKind == 5) return;
    if (A.mode == Mode::Test && A.posing) poseInput(overUI);
    else if (A.mode == Mode::Test) testInput(overUI);
    else {
        buildInput(overUI);
        // a live mirror follows the selected part as it's edited, not only when you let go
        if (A.sel.kind == 1 && A.sel.idx < (int)A.design.bodies.size() && mirrorPartner(A.design, A.sel.idx) >= 0)
            syncMirrors(A.design, nullptr, A.sel.idx);
    }
}

void handleKeys() {
    ImGuiIO& io = ImGui::GetIO();
    bool kb = !io.WantCaptureKeyboard && !A.showPause && !A.showSettings && !A.modalAtFrameStart;
    if (kb && actPressed(Act::Fire)) fireDown();
    if (A.S) {
        bool fire = (kb && actDown(Act::Fire)) || A.fireBtnHeld, mrel = (kb && actDown(Act::MagRelease)) || A.releaseBtnHeld;
        A.S->trigger = fire;
        sim::setKey(*A.S, "fire", fire);
        // a control with a recorded animation: its key plays the animation (the hand works it) instead of pushing it
        bool held[5] = {mrel, kb && actDown(Act::Control1), kb && actDown(Act::Control2), kb && actDown(Act::Control3), kb && actDown(Act::Control4)};
        bool pressed[5] = {kb && actPressed(Act::MagRelease), kb && actPressed(Act::Control1), kb && actPressed(Act::Control2), kb && actPressed(Act::Control3), kb && actPressed(Act::Control4)};
        const char* keyName[5] = {"magrelease", "k1", "k2", "k3", "k4"};
        for (int k = 0; k < 5; k++)
            for (auto& b : A.design.bodies) {
                if (b.ctrl == "none" || b.key != keyName[k]) continue;
                auto it = A.design.anims.find("control:" + b.id);
                if (it == A.design.anims.end() || it->second.keys.empty()) continue;
                if (pressed[k] && !sim::animPlaying(*A.S)) { A.S->design.anims = A.design.anims; sim::playAnim(*A.S, "control:" + b.id); }
                held[k] = false;   // the hand does it
            }
        // with a real catch button in the design, the key presses the button rather than releasing by magic
        if (sim::hasControl(*A.S, "magrelease")) { sim::setKey(*A.S, "magrelease", held[0]); A.S->magRelease = false; }
        else A.S->magRelease = held[0];
        sim::setKey(*A.S, "k1", held[1]);
        sim::setKey(*A.S, "k2", held[2]);
        sim::setKey(*A.S, "k3", held[3]);
        sim::setKey(*A.S, "k4", held[4]);
        A.S->handStrength = A.handN;
    }
    if (!kb) return;
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_S)) { saveBlueprint(); return; }  // in the designer or on the bench
    if (actPressed(Act::Screenshot)) {  // (raylib itself already saves one on F12)
        if (keyOf(Act::Screenshot) != KEY_F12) TakeScreenshot(TextFormat("gunsmith3d_%d.png", (int)GetTime()));
        toast("Screenshot saved.");
    }
    if (actPressed(Act::ToggleMode) && !A.benchOnly) setMode(A.mode == Mode::Build ? Mode::Test : Mode::Build);
    if (actPressed(Act::FitView)) fitView();
    if (actPressed(Act::SideView)) { if (A.ortho && std::fabs(A.yaw) < 1e-3f && std::fabs(A.pitch) < 1e-3f) setOrtho(false); else sideView(); }
    {   // number pad, as in most 3D programs: 5 orthographic on/off; 1 front, 3 side, 7 top (with Ctrl: the opposite side)
        bool c = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (IsKeyPressed(KEY_KP_5)) setOrtho(!A.ortho);
        if (IsKeyPressed(KEY_KP_1)) snapView(c ? View::Back : View::Front);
        if (IsKeyPressed(KEY_KP_3)) snapView(c ? View::Left : View::Right);
        if (IsKeyPressed(KEY_KP_7)) snapView(c ? View::Bottom : View::Top);
    }
    bool dragging = (A.dragKind == 1 || A.dragKind == 2) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);  // (X, Y, Z lock the drag then)
    if (actPressed(Act::XRay) && !dragging) A.xray = !A.xray;
    if (actPressed(Act::Cutaway)) A.cutaway = !A.cutaway;
    if (A.mode == Mode::Test) {
        if (actPressed(Act::Reload) && A.S && !sim::animPlaying(*A.S)) {   // your reload animation, or the plain swap
            sim::Body* m = sim::seatedMag(*A.S);
            bool empty = !A.S->chambered && (!m || m->rounds == 0);
            A.S->design.anims = A.design.anims;
            if ((empty && sim::playAnim(*A.S, "reload_empty")) || sim::playAnim(*A.S, "reload")) toast("Reloading (your animation).");
            else { sim::quickReload(*A.S); toast("Reloaded."); }
        }
        if (actPressed(Act::NewMag)) spawnSpareMag();
        if (actPressed(Act::LooseRound)) spawnBenchRound();
        if (actPressed(Act::FillMags)) topUpMagazines();
        if (actPressed(Act::Pause)) A.running = !A.running;
        if (actPressed(Act::Restart)) restartTest();
        if (IsKeyPressed(KEY_ESCAPE)) A.showPause = true;
        return;
    }
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shiftK = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (actPressed(Act::Redo) || (actPressed(Act::Undo) && shiftK)) redo();
    else if (actPressed(Act::Undo)) undo();
    if (actPressed(Act::Delete) || (!ctrl && IsKeyPressed(KEY_BACKSPACE))) deleteSel();
    if (actPressed(Act::Duplicate) && A.sel.kind == 1) duplicateSel();
    if (ctrl && IsKeyPressed(KEY_C)) copySelection();
    if (ctrl && IsKeyPressed(KEY_V)) pasteClipboard();
    if (actPressed(Act::Knife)) setTool(Tool::Knife);
    if (A.sel.kind == 1 && A.sel.idx < (int)A.design.bodies.size()) {  // arrow keys nudge vertices or the whole part
        double st = std::max(0.1, gSnap > 0 ? gSnap : 0.5) * ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? 10 : 1);
        double dx = 0, dy = 0;
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) dx = -st;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) dx = st;
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) dy = -st;
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) dy = st;
        if (dx != 0 || dy != 0) {
            auto& d = A.design.bodies[A.sel.idx];
            if (!A.selVs.empty()) { for (int v : A.selVs) if (v < (int)d.pts.size()) { d.pts[v].x += dx; d.pts[v].y += dy; } }
            else for (int k : weldedWith(A.design, A.sel.idx)) moveBody(k, dx, dy);  // a welded piece moves as one
            commit();
        }
    }
    if (IsKeyPressed(KEY_ESCAPE)) {  // first cancel what you're doing, then the menu
        if (A.tool != Tool::Select || A.drafting) { A.tool = Tool::Select; A.drafting = false; A.draft.clear(); A.draftHasF = false; }
        else A.showPause = true;
    }
    if (actPressed(Act::Finish)) {
        if (A.tool == Tool::Body) finishBody();
        else if (A.tool == Tool::Track) finishTrack();
        else if (A.tool == Tool::RoundPath) finishRoundPath();
    }
}
