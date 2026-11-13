// Gunsmith3D - Gunsmith3D entry point: command line, window, main loop.
#include "app/pose.hpp"
#include "app/app.hpp"
#include "app/fitbench.hpp"
#include "app/npcedit.hpp"
#include "app/careermode.hpp"
#include "app/sysinfo.hpp"
#include "app/perf.hpp"
#include "app/keys.hpp"
#include "app/gfx.hpp"
#include "app/audio.hpp"
#include "app/view.hpp"
#include "app/editor.hpp"
#include "app/meshtool.hpp"
#include "app/render.hpp"
#include "app/overlay.hpp"
#include "app/bench.hpp"
#include "app/input.hpp"
#include "app/reltest.hpp"
#include "app/settings.hpp"
#include "app/updater.hpp"
#include "app/shop3d.hpp"
#include "app/doors.hpp"
#include "app/paths.hpp"
#include "app/usertex.hpp"
#include "app/fonts.hpp"
#include "app/scene.hpp"
#include "app/mapedit.hpp"
#include "ui/ui.hpp"
#include "app/range.hpp"
#include "version.hpp"
#include "imgui.h"
#include "rlImGui.h"
#include "rlgl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    installCrashHandler();
    // command-line options for automated screenshots
    std::string presetArg, shotPath, selectArg, demo;
    bool meshDemo = false;
    bool perfArg = false, charArg = false;
    int frames = 0;
    bool trackDraft = false, carveTool = false;
    float camYaw = -100, camPitch = 0, camDist = -1;
    int selCarveArg = -1;
    bool fireArg = false, sideArg = false, testArg = false, designerArg = false;
    int poseArg = -1;
    int relArg = 0;
    bool menuArg = false, settingsArg = false, pauseArg = false, benchArg = false;
    int introArg = 0;
    bool solidArg = false;
    std::string rangeArg, fittingArg, rangeLoadout; bool rangeAim = false, careerArg = false;
    double rangeYaw = 0, rangePitch = 0, rangeArmor = 0; bool rangeShot = false;
    int settingsPage = 0;
    bool useSettings = false;
    std::string mapArg; bool walkArg = false, npcArg = false;
    bool tutListArg = false; int tutArg = 0;
    int magCapArg = 0; double magCurveArg = -999; std::string cartArg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--version" || a == "-v") { std::printf("Gunsmith3D %s\n", version::full()); return 0; }
        if (a == "--help" || a == "-h") {
            std::printf("Gunsmith3D %s\n\nUsage: gunsmith3d [options]\n"
                        "  --preset NAME   go straight to the designer with an example (blowback, tilting, boltaction, single, longrecoil, blank)\n"
                        "  --test          go straight to the test bench\n"
                        "  --side          start in side view\n"
                        "  --version       print the version and exit\n", version::full());
            return 0;
        }
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--preset") presetArg = next();
        else if (a == "--shot") shotPath = next();
        else if (a == "--frames") frames = std::atoi(next().c_str());
        else if (a == "--fire") fireArg = true;
        else if (a == "--test") testArg = true;
        else if (a == "--pose") { testArg = true; poseArg = std::atoi(next().c_str()); }  // (screenshots) the pose editor at hold N
        else if (a == "--side") sideArg = true;
        else if (a == "--select") selectArg = next();
        else if (a == "--trackdraft") trackDraft = true;
        else if (a == "--cutpick") A.cutPick = true;
        else if (a == "--fitting") fittingArg = next();
        else if (a == "--rangeloadout") rangeLoadout = next();   // (screenshots) this loadout on the range gun
        else if (a == "--rangeaim") rangeAim = true;
        else if (a == "--career") careerArg = true;   // (screenshots) career mode's office; GUNSMITH_CAREERTAB, GUNSMITH_CAREERRATE, GUNSMITH_CAREERDAYS             // (screenshots) aiming   // (screenshots) the fitting bench, with this loadout ("-" for none)   // (screenshots) "Pick cuts in the view" on
        else if (a == "--measure") {   // (screenshots) a measuring line from x1 y1 to x2 y2 (mm), kept shown
            double v[4]; for (double& k : v) k = std::atof(next().c_str());
            A.measureA = {v[0], v[1]}; A.measureB = {v[2], v[3]}; A.measured = A.measureKeep = true;
        }
        else if (a == "--carvetool") carveTool = true;
        else if (a == "--meshdemo") meshDemo = true;   // (screenshots) the selected part made a 3D mesh, its sides caved in
        else if (a == "--rotate") A.rotateMode = true;   // (screenshots) the top bar's Rotate
        else if (a == "--cam") { camYaw = std::atof(next().c_str()); camPitch = std::atof(next().c_str()); }  // (screenshots) camera angles, radians        // (screenshots) pick the Carve tool
        else if (a == "--selcarve") selCarveArg = std::atoi(next().c_str());  // (screenshots) select carve N of the selected part   // (screenshots) start a track on the selected part
        else if (a == "--demo") demo = next();
        else if (a == "--designer") designerArg = true;
        else if (a == "--magcap") magCapArg = std::atoi(next().c_str());
        else if (a == "--magcurve") magCurveArg = std::atof(next().c_str());
        else if (a == "--cart") cartArg = next();
        else if (a == "--reltest") relArg = std::atoi(next().c_str());
        else if (a == "--menu") menuArg = true;
        else if (a == "--perf") perfArg = true;   // (screenshots) the performance overlay with full details
        else if (a == "--about") { menuArg = true; A.showAbout = true; }   // (screenshots of the About window)
        else if (a == "--careers") { menuArg = true; A.showCareerPicker = true; }   // (screenshots of the career list)
        else if (a == "--solid") solidArg = true;
        else if (a == "--dist") camDist = (float)std::atof(next().c_str());   // (screenshots) how far the camera is
        else if (a == "--range") rangeArg = next();
        else if (a == "--rangeshot") { rangeYaw = std::atof(next().c_str()); rangePitch = std::atof(next().c_str()); rangeArmor = std::atof(next().c_str()); rangeShot = true; }   // (screenshots) look, armour          // (screenshots) the firing range with an example gun            // (screenshots) every part solid (the x-ray view off)
        else if (a == "--intro") introArg = 1;              // (screenshots) the launch splash
        else if (a == "--eula") introArg = 2;               // (screenshots) the agreement screen
        else if (a == "--usesettings") useSettings = true;  // (screenshots) honour the settings file            // (screenshots) stay on the main menu
        else if (a == "--settings") settingsArg = true;    // (screenshots) open the settings
        else if (a == "--settingspage") { settingsArg = true; settingsPage = std::atoi(next().c_str()); }
        else if (a == "--pause") pauseArg = true;          // (screenshots) open the pause menu
        else if (a == "--benchpick") benchArg = true;      // (screenshots) open the test-bench picker
        else if (a == "--npcedit") npcArg = true;
        else if (a == "--character") charArg = true;   // (screenshots) the character editor
        else if (a == "--mapedit") mapArg = next();         // (screenshots) open the map editor: builtin, new, or a file
        else if (a == "--walk") walkArg = true;            // (screenshots) walk the map
        else if (a == "--tutorials") tutListArg = true;    // (screenshots) open the list of tutorials
        else if (a == "--tutorial") tutArg = std::atoi(next().c_str());  // (screenshots) start tutorial N (1 = the first)
    }

#if defined(__linux__)
    if (!std::getenv("WAYLAND_DISPLAY") && !std::getenv("DISPLAY")) {  // raylib would crash opening a window with no display
        std::fprintf(stderr, "Gunsmith3D needs a desktop session to run (no Wayland or X11 display found).\n");
        return 1;
    }
#endif
    initAppFolder();     // Documents/OAB Foundry/Gunsmith on Windows: the settings and autosave (moved over from the game folder)
    loadSettings();
    update::cleanup();   // the program file the last update moved aside
    if (CFG.autoUpdate && ((frames == 0 && shotPath.empty()) || std::getenv("GUNSMITH_UPDATE_API"))) update::start();   // (not for screenshot runs, unless testing it)
    setUserFolder(CFG.userDir.empty() ? defaultUserFolder() : CFG.userDir);   // your files: Blueprints, Levels, Rounds, ...
    setupUserFolders();  // (and files moved over from where older versions kept them)
    scanUserTextures();
    if (frames && !useSettings) CFG = Settings();  // automated screenshots use the defaults unless told otherwise
    unsigned flags = FLAG_WINDOW_RESIZABLE | (CFG.msaa ? FLAG_MSAA_4X_HINT : 0) | (CFG.vsync ? FLAG_VSYNC_HINT : 0);
    SetConfigFlags(flags);
    if (perfArg) CFG.perfOverlay = 2;
    installLogCapture();   // (keeps the graphics driver's report for the About window)
    InitWindow(CFG.resW, CFG.resH, (std::string("Gunsmith3D ") + version::text()).c_str());
    if (!IsWindowReady()) {  // no display (e.g. started over SSH): say so instead of crashing
        std::fprintf(stderr, "Gunsmith3D could not open a window. Start it from a desktop session (Wayland or X11).\n");
        return 1;
    }
    SetWindowMinSize(960, 600);
    SetExitKey(KEY_NULL);
    if (!frames && (CFG.windowMode != WindowMode::Windowed || CFG.monitor != 0)) applyDisplay();
    SetTargetFPS(CFG.fpsCap);
    initGfx();
    initScene();
    initViewFont();
    initSfx();
    setupImGui();
    gSnap = CFG.snap; A.snapOn = CFG.snapOn;

    A.userCarts = loadUserCartridges();
    std::ifstream in(autosavePath());
    if (!in) in.open(OLD_AUTOSAVE);  // carry work over from Action Lab 3D
    bool loaded = false;
    if (in && presetArg.empty()) { std::stringstream ss; ss << in.rdbuf(); loaded = designFromJson(ss.str(), A.design); }
    if (!loaded) A.design = loadPreset(presetArg.empty() ? "tilting" : presetArg);
    A.lastSnap = designToJson(A.design);
    A.vw = GetScreenWidth() - (int)PANEL_W;
    A.vh = GetScreenHeight() - (int)TOOLBAR_H;
    fitView();
    if (sideArg) sideView();
    if (!selectArg.empty()) { int i = A.design.bodyIndex(selectArg); if (i >= 0) A.sel = {1, i, -1}; }
    if (!cartArg.empty()) for (auto& c : cartridgePresets()) if (c.name == cartArg) { A.design.breech.cart = c; A.design.breech.cartImpulse = true; applyCartridge(A.design.breech); }
    if (magCapArg > 0 || magCurveArg > -999 || !cartArg.empty()) {
        for (size_t i = 0; i < A.design.bodies.size(); i++) if (A.design.bodies[i].magazine && A.design.bodies[i].gen.on) {
            if (magCapArg > 0) A.design.bodies[i].magCap = magCapArg;
            if (magCurveArg > -999) A.design.bodies[i].gen.curve = magCurveArg;
            generateMagazine(A.design, (int)i);
        }
        fitView();
    }
    // the game opens on the main menu; the screenshot options go straight to the workshop
    bool direct = !menuArg && (!presetArg.empty() || !fittingArg.empty() || testArg || sideArg || fireArg || !demo.empty() || designerArg || !selectArg.empty() || relArg > 0 || pauseArg);
    if (direct) enterWorkshop(false);
    if (sideArg) sideView();
    if (!selectArg.empty()) { int i = A.design.bodyIndex(selectArg); if (i >= 0) A.sel = {1, i, -1}; }
    if (const char* c = std::getenv("GUNSMITH_PARTRING")) { int ax = 0; double dg = 0; A.rotateMode = true; if (std::sscanf(c, "%d,%lf", &ax, &dg) == 2) partRingTest(ax, dg); }   // (tests) the selected part's rings
    if (carveTool) A.tool = Tool::Carve;
    if (meshDemo && A.sel.kind != 1) {   // a plain block beside the gun, to show the tool on
        BodyDef blk; blk.id = "block"; blk.name = "Grip block"; blk.pts = {{200, 0}, {236, 0}, {236, 90}, {200, 90}}; blk.thick = 30; blk.z = 0; blk.color = "#8a6a4a";
        fixShape(blk);
        A.design.bodies.push_back(blk);
        A.sel = {1, (int)A.design.bodies.size() - 1, -1};
    }
    if (meshDemo && A.sel.kind == 1) {
        A.tool = Tool::Mesh;
        meshMakeFromSelected();
        const BodyDef& b = A.design.bodies[A.sel.idx];
        meshAddRing(2, b.z);
        meshScalePicked(0.75, 0.75, 1);
        if (std::getenv("GUNSMITH_MESHFACE")) { meshFace() = 0; meshExtrudeBy() = 8; }
    }
    if (solidArg) A.xray = false;
    if (camYaw > -50) { A.yaw = camYaw; A.pitch = camPitch; }
    if (camDist > 0) A.dist = camDist;
    if (const char* z = std::getenv("GUNSMITH_ZOOM")) {  // (screenshots) "x y dist": look at a design point (mm) from close by
        double zx = 0, zy = 0, zd = 300; std::sscanf(z, "%lf %lf %lf", &zx, &zy, &zd);
        A.target = W3({zx, zy}, 0); A.dist = (float)zd * U;
    }
    if (selCarveArg >= 0) A.selCarve = selCarveArg;
    if (trackDraft && A.sel.kind == 1) {
        auto& b = A.design.bodies[A.sel.idx];
        A.tool = Tool::Track; A.draftHasF = true; A.drafting = true; A.draftA = b.id;
        A.draftF = sim::polyCentroid(b.pts); A.draft = {A.draftF};
    }
    if (designerArg) { A.editCart = A.design.breech.cart; A.showCartDesigner = true; }
    if (testArg || !demo.empty() || !fittingArg.empty()) setMode(Mode::Test);
    if (!fittingArg.empty()) { enterFitting(); if (fittingArg != "-") loadLoadout(fittingArg); }
    if (poseArg >= 0 && std::getenv("GUNSMITH_ANIM")) {   // (screenshots) "slot seconds": the animation editor at that moment
        char slot[64] = {0}; double t = 0;
        if (std::sscanf(std::getenv("GUNSMITH_ANIM"), "%63s %lf", slot, &t) >= 1) { A.animEdit = true; A.animSlot = slot; A.animT = t; }
    }
    if (poseArg >= 0) { A.poseHold = std::min(poseArg, 2); enterPose(); if (std::getenv("GUNSMITH_POSEHAND")) poseFocusHand(std::atoi(std::getenv("GUNSMITH_POSEHAND")));
        if (const char* c = std::getenv("GUNSMITH_POSECAM")) std::sscanf(c, "%f %f %f", &A.yaw, &A.pitch, &A.dist);
        if (const char* c = std::getenv("GUNSMITH_RINGTEST")) { int id = 0, ax = 0; double dg = 0; if (std::sscanf(c, "%d,%d,%lf", &id, &ax, &dg) == 3) poseRingTest(id, ax, dg); } }   // (screenshots) "yaw pitch dist" round the hand
    A.showSettings = settingsArg; A.showPause = pauseArg; A.showBenchPicker = benchArg;
    settingsPageIndex(settingsPage);
    if (fireArg) fireDown();
    if (charArg) enterCharacterEditor();
    if (careerArg) {
        enterCareer();
        if (const char* e = std::getenv("GUNSMITH_CAREERTAB")) careerSetTab(std::atoi(e));
        if (const char* e = std::getenv("GUNSMITH_CAREERRATE")) careerTestStart(e);
        if (const char* e = std::getenv("GUNSMITH_CAREERDELIVER")) { int id = std::atoi(e); const char* g = strchr(e, ':'); if (g) careerTestStart(g + 1, id); }   // "id:gun"
        if (std::getenv("GUNSMITH_CAREERDESIGN")) enterWorkshop(false);   // (the designer, opened from the office)
        if (const char* e = std::getenv("GUNSMITH_CAREERDEMO")) careerDemoStart(std::atoi(e));   // (a contract's range demonstration)
        if (const char* e = std::getenv("GUNSMITH_CAREERSHOP")) careerOpenWorkshop(std::atoi(e) == 2);   // (your workshop: 1 to build, 2 on foot)
        if (const char* e = std::getenv("GUNSMITH_SHOPREMOVE")) {   // (screenshots) take things out of your shop after it's opened: types, comma separated
            std::string t = std::string(",") + e + ",";
            auto& v = ME.map.ents;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const gmap::Entity& x) { return t.find("," + x.type + ",") != std::string::npos; }), v.end());
        }
        if (std::getenv("GUNSMITH_DOOROPEN"))   // (screenshots) open every door on the map, as if from where you stand
            for (int i = 0; i < (int)ME.map.ents.size(); i++) if (ME.map.ents[i].type == "door") doors::toggle(ME.map, i, ME.pl.feet);
        if (const char* e = std::getenv("GUNSMITH_SHOPWALK")) { (void)e; shopWalk(); }   // (screenshots) press Walk
        if (const char* e = std::getenv("GUNSMITH_SHOPOPEN")) shopOpen(std::atoi(e));   // (screenshots) a thing in your shop, used
        if (const char* e = std::getenv("GUNSMITH_CAREERDAYS")) for (int k = std::atoi(e); k > 0; k--) career::endDay(CAREER);
    }
    if (const char* c = std::getenv("GUNSMITH_NPCCAM")) std::sscanf(c, "%f,%f,%f,%f", &NE.dist, &NE.yaw, &NE.pitch, &NE.targetY);   // (screenshots) the NPC editor's camera
    if (const char* c = std::getenv("GUNSMITH_LOOK")) {   // (screenshots) the character maker's choices: body,hair,top,pants,extras
        char body[16] = "", hair[16] = "", top[16] = "", pants[16] = "", ex[16] = "";
        std::sscanf(c, "%15[^,],%15[^,],%15[^,],%15[^,],%15s", body, hair, top, pants, ex);
        CharLook L; L.on = true; L.body = body; L.hair = hair; L.top = top; L.pants = pants;
        std::string e = ex; L.belt = e.find('b') != std::string::npos; L.hat = e.find('c') != std::string::npos; L.headphones = e.find('h') != std::string::npos; L.glasses = e.find('g') != std::string::npos;
        NE.npc = characterFromLook(L, &NE.npc);
        npcCommit();
    }
    if (npcArg) { if (const char* k = std::getenv("GUNSMITH_NPC")) NE.npc = npcPreset(k); enterNpcEditor(); if (const char* k = std::getenv("GUNSMITH_NPC")) { NE.npc = npcPreset(k); NE.sel = 0; npcFrame(); } }
    if (!mapArg.empty()) {
        enterMapEditor();
        if (mapArg == "builtin") meLoadBuiltin(); else if (mapArg == "new") meNew(); else meOpen(mapArg);
        ME.undo.clear();
        if (const char* e = std::getenv("GUNSMITH_MAPSEL")) { int i = std::atoi(e); if (i >= 0 && i < (int)ME.map.brushes.size()) ME.sel = {i}; }
        if (const char* e = std::getenv("GUNSMITH_MAPFACE")) ME.face = std::atoi(e);
        if (std::getenv("GUNSMITH_MAPTOP")) { ME.top = true; ME.topC = {40, 0, 0}; ME.topH = 70; }
        if (const char* e = std::getenv("GUNSMITH_MAPTOOL")) ME.tool = (MTool)std::atoi(e);
        if (const char* e = std::getenv("GUNSMITH_MAPLIGHT")) ME.lightMode = std::atoi(e);   // (screenshots) 0 as played, 1 work light, 2 fullbright
        if (const char* e = std::getenv("GUNSMITH_MAPENT")) {   // (screenshots) pick the nth light
            int k = std::atoi(e), seen = 0;
            for (size_t i = 0; i < ME.map.ents.size(); i++) if (ME.map.ents[i].type == "light" && seen++ == k) { ME.ent = (int)i; ME.sel.clear(); }
        }
        if (const char* e = std::getenv("GUNSMITH_MAPCAM")) std::sscanf(e, "%lf,%lf,%lf,%lf,%lf", &ME.cam.x, &ME.cam.y, &ME.cam.z, &ME.yaw, &ME.pitch);
        if (walkArg) meWalk(false);
    }
    if (tutListArg) A.showTutorials = true;
    if (!rangeArg.empty()) {   // an example's key, or a blueprint file
        Design rd = loadPreset(rangeArg);
        std::ifstream rin(rangeArg);
        if (rin) { std::stringstream ss; ss << rin.rdbuf(); designFromJson(ss.str(), rd); }
        gmap::Map rm = gmap::builtinRange();
        if (const char* mf = std::getenv("GUNSMITH_RANGEMAP")) {   // (screenshots) a map file instead of the practice range
            std::ifstream min(mf); std::stringstream ms; ms << min.rdbuf();
            gmap::Map m2; if (gmap::mapFromJson(ms.str(), m2)) rm = m2;
        }
        enterRange(rd, rm);
        if (rangeShot) rangeShotSetup(rangeYaw, rangePitch, rangeArmor);
        rangeShotExtras(rangeLoadout, rangeAim);
    }
    // a normal launch: the studio's splash, then the agreement until it's agreed to (screenshot runs skip them)
    if (introArg == 2) introForceEula();
    else if (introArg == 1 || (!frames && !direct && mapArg.empty() && !npcArg && !tutListArg && tutArg <= 0 && !settingsArg && rangeArg.empty())) introStart(false);
    if (tutArg > 0) startTutorial(tutArg - 1);

    int frame = 0;
    while (!WindowShouldClose() && !A.quit) {
        perf::frameStart();
        cursorGuard();
        if (actPressed(Act::PerfOverlay)) perf::toggle();
        if (actPressed(Act::Fullscreen)) toggleFullscreenKey();
        if (CFG.uiScaleAuto && IsWindowResized()) setUiScale(autoUiScale(), false);   // (auto size follows the window)   // F3: the frame rate and what's slowing the game down
        handleDroppedFiles();  // pictures dropped on the window become textures
        relPoll();
        applyAudio();
        autosaveTick();
        A.modalAtFrameStart = A.showPause || A.showSettings || A.showBenchPicker || A.showTutorials || A.showOpenWindow || A.showRangePicker || A.showAbout || A.showCareerPicker;
        bool menu = A.screen == Screen::Menu;
        TOOLBAR_H = menu ? 0.f : TOOLBAR_H_BASE * CFG.uiScale;
        if (relArg > 0 && frame == 2) { REL.rounds = relArg; relStart(); }
        float dt = std::min(GetFrameTime(), 0.05f);
        if (frames) dt = 1.f / 60;
        A.showcase = menu;
        A.viewX = menu ? GetScreenWidth() * 0.28f : 0.f;
        A.vw = menu ? GetScreenWidth() - (int)A.viewX : GetScreenWidth() - (int)PANEL_W;
        A.vh = GetScreenHeight() - (int)TOOLBAR_H;
        if (menu) { A.ortho = false; A.yaw += dt * 0.10f; A.pitch = 0.28f; }  // the last design turns slowly behind the menu
        float keepDist = A.dist;
        if (menu) A.dist *= 1.3f;
        updateCamera();
        A.dist = keepDist;

        careerTick();   // (career mode: back to the office after a range demonstration)
        if (A.screen == Screen::Range) {
            bool modal = A.showPause || A.showSettings || A.showTutorials;
            rangeUpdate(dt, modal);
            BeginDrawing();
            ClearBackground(Color{30, 34, 41, 255});
            { perf::Scope t(perf::Scene); rangeRender(); }
            rangeHud();
            rlImGuiBegin();
            if (A.showTutorials) tutorialsWindow();
            else if (modal) pauseMenu();
            else { rangeSpawnerWindow(); rangeGadgetWindow(); }
            toastOverlay();
            perf::overlay();
            rlImGuiEnd();
            EndDrawing();
            frame++;
            if (frames && frame >= frames) { if (!shotPath.empty()) TakeScreenshot(shotPath.c_str()); break; }
            continue;
        }
        if (A.screen == Screen::MapEditor) {
            bool modal = A.showPause || A.showSettings || A.showTutorials;
            mapEditorUpdate(dt, modal);
            BeginDrawing();
            ClearBackground(Color{30, 34, 41, 255});
            { perf::Scope t(perf::Scene); mapEditorRender(); }
            if (shopActive()) careerRenderPortraits();   // (career: customers' faces for the counter's screen)
            rlImGuiBegin();
            if (!ME.walking) { mapToolbar(); mapPanel(); }
            if (shopActive()) shopWindows();   // (on foot: what you're using; building: the shop's checks)
            tutorialPanel();
            if (A.showTutorials) tutorialsWindow();
            else if (modal) pauseMenu();
            toastOverlay();
            perf::overlay();
            rlImGuiEnd();
            EndDrawing();
            frame++;
            if (frames && frame >= frames) { if (!shotPath.empty()) TakeScreenshot(shotPath.c_str()); break; }
            continue;
        }
        if (A.screen == Screen::NpcEditor) {
            bool modal = A.showPause || A.showSettings || A.showTutorials;
            npcEditorUpdate(dt, modal);
            BeginDrawing();
            ClearBackground(Color{30, 34, 41, 255});
            { perf::Scope t(perf::Scene); npcEditorRender(); }
            rlImGuiBegin();
            npcToolbar(); npcPanel();
            if (A.showTutorials) tutorialsWindow();
            else if (modal) pauseMenu();
            toastOverlay();
            perf::overlay();
            rlImGuiEnd();
            EndDrawing();
            frame++;
            if (frames && frame >= frames) { if (!shotPath.empty()) TakeScreenshot(shotPath.c_str()); break; }
            continue;
        }
        if (A.screen == Screen::Career) {
            bool modal = A.showPause || A.showSettings || A.showTutorials;
            BeginDrawing();
            ClearBackground(Color{30, 34, 41, 255});
            careerRenderPortraits();   // (customers' faces, drawn before the panels)
            rlImGuiBegin();
            careerScreen();
            if (A.showTutorials) tutorialsWindow();
            else if (modal) pauseMenu();
            toastOverlay();
            perf::overlay();
            rlImGuiEnd();
            EndDrawing();
            frame++;
            if (frames && frame >= frames) { if (!shotPath.empty()) TakeScreenshot(shotPath.c_str()); break; }
            continue;
        }
        if (introActive()) {
            BeginDrawing();
            ClearBackground(Color{8, 9, 11, 255});
            rlImGuiBegin();
            introFrame(dt);
            perf::overlay();
            rlImGuiEnd();
            EndDrawing();
            frame++;
            if (frames && frame >= frames) { if (!shotPath.empty()) TakeScreenshot(shotPath.c_str()); break; }
            continue;
        }
        if (menu) {
            BeginDrawing();
            ClearBackground(Color{14, 16, 19, 255});
            DrawRectangleGradientV(0, 0, GetScreenWidth(), A.vh, Color{40, 46, 56, 255}, Color{16, 18, 22, 255});
            { perf::Scope t(perf::Scene); beginViewport3D(); drawScene(); endViewport3D(); }
            // darken the left, where the menu sits, more than the right
            DrawRectangleGradientH(0, 0, GetScreenWidth(), A.vh, Color{10, 11, 13, 240}, Color{10, 11, 13, 40});
            rlImGuiBegin();
            mainMenu();
            toastOverlay();
            perf::overlay();
            rlImGuiEnd();
            EndDrawing();
            frame++;
            if (frames && frame >= frames) { if (!shotPath.empty()) TakeScreenshot(shotPath.c_str()); break; }
            continue;
        }

        if (!demo.empty() && A.S) {  // scripted demos for automated screenshots
            auto& S = *A.S;
            if (demo == "reload") {
                if (frame == 5) { S.magRelease = true; }
                if (frame == 40) { S.magRelease = false; spawnSpareMag(); }
                if (frame == 70) {  // grab the new magazine and lift it towards the well
                    for (auto& b : S.bodies) if (b.isMag && !b.tmpl.empty()) {
                        A.grabTmpl = A.design.bodyIndex(b.tmpl);
                        S.hand.on = true; S.hand.b = &b; S.hand.l = {0, -0.02};
                        auto* J = sim::magwell(S);
                        V2 ex = sim::wpt(*J->B, J->path.back());
                        S.hand.t = {ex.x + 0.03, ex.y + 0.01};
                    }
                }
            }
            if (demo == "rack") {
                auto* sl = S.find("slide");
                if (sl && frame == 10) { A.grabTmpl = A.design.bodyIndex("slide"); S.hand.on = true; S.hand.b = sl; S.hand.l = {-0.06, -0.006}; S.hand.t = {sl->x0 - 0.06 - 0.03, sl->y0 - 0.006}; }
            }
        }
        if (!demo.empty() && A.S && demo == "reload") A.S->magRelease = frame >= 5 && frame < 40;
        bool modal = A.showPause || A.showSettings || A.showTutorials;
        if (!modal) stepSim(dt);  // the bench stands still while a menu is open
        stepEffects(dt);

        BeginDrawing();
        ClearBackground(Color{30, 34, 41, 255});
        drawBackdrop(0, TOOLBAR_H, (float)A.vw, (float)A.vh);
        {
        perf::Scope sceneT(perf::Scene);   // (drawing the 3D view)
        beginViewport3D();
        if (A.posing || (A.S && sim::animPlaying(*A.S))) {  // the gun at this hold's tilt, and the mannequin holding it
            rlPushMatrix(); rlMultMatrixf(MatrixToFloat(poseGunMatrix())); drawScene(); rlPopMatrix();
            drawPoseBody();
        } else drawScene();
        endViewport3D();
        }
        if (A.posing) drawPoseOverlay(); else { drawOverlay(); drawFitOverlay(); }
        if (A.mode == Mode::Test) DrawRectangleLinesEx({0, TOOLBAR_H, (float)A.vw, (float)A.vh}, 2, Color{232, 161, 58, 90});

        rlImGuiBegin();
        editedAny = false;
        A.releaseBtnHeld = false;
        if (demo.empty() && !modal && !A.modalAtFrameStart) handleViewportInput();
        toolbar();
        toolRail();
        if (A.mode == Mode::Test) testPanel(); else { buildPanel(); cartridgeDesigner(); }
        tutorialPanel();
        if (A.showTutorials) tutorialsWindow();
        else if (A.showPause || A.showSettings) pauseMenu();
        if (fireArg && frame < 30) A.fireBtnHeld = true;
        handleKeys();
        if (!demo.empty() && A.S && demo == "reload") A.S->magRelease = frame >= 5 && frame < 40;
        hintOverlay();
        if (A.mode == Mode::Test) traceWindow();
        if (editedAny) A.uiDirty = true;
        if (A.uiDirty && !ImGui::IsAnyItemActive()) { commit(); A.uiDirty = false; }
        perf::overlay();
            rlImGuiEnd();
        EndDrawing();

        frame++;
        if (frames && frame == frames - 1)
            if (const char* e = std::getenv("GUNSMITH_PICKAT")) {   // (tests) which parts are under this design point (mm, x,y,z)
                float x = 0, y = 0, z = 0; std::sscanf(e, "%f,%f,%f", &x, &y, &z);
                Vector2 sp = toScreen(W3({x, y}, z));
                SetMousePosition((int)sp.x, (int)sp.y);
            }
        if (frames && frame == frames && std::getenv("GUNSMITH_PICKAT")) {
            std::printf("PICK at %.0f,%.0f:", GetMousePosition().x, GetMousePosition().y);
            for (auto& h : bodiesUnderMouse()) std::printf(" [%s]", A.design.bodies[h.body].name.c_str());
            std::printf("\n"); std::fflush(stdout);
        }
        if (frames && frame >= frames) {
            if (!shotPath.empty()) TakeScreenshot(shotPath.c_str());
            break;
        }
    }
    relShutdown();
    careerShutdown();
    if (A.screen == Screen::MapEditor) leaveMapEditor();
    if (A.screen == Screen::NpcEditor) leaveNpcEditor();
    if (A.saveDue && CFG.autosave != AutosaveEvery::Off) save();
    saveSettings();
    rlImGuiShutdown();
    if (SFX.ok) { UnloadSound(SFX.bang); UnloadSound(SFX.clack); UnloadSound(SFX.click); CloseAudioDevice(); }
    CloseWindow();
    return 0;
}
