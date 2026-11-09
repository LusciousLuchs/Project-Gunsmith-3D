// Gunsmith3D - the performance overlay (see perf.hpp).
#include "app/perf.hpp"
#include "app/app.hpp"
#include "app/mapedit.hpp"
#include "app/npcedit.hpp"
#include "app/settings.hpp"
#include "app/sysinfo.hpp"
#include "carve.hpp"
#include "raylib.h"
#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace perf {
namespace {
constexpr int N = 300;   // frames kept (about 5 s at 60 FPS)
double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Frame { float total = 0, work = 0, slot[SlotCount] = {0, 0, 0}; int steps = 0; };
Frame hist[N];
int head = 0, filled = 0;
Frame cur;                   // the frame being measured
double frameT0 = -1, workEnd = -1;
float plotFrame[N], plotFps[N];
}  // namespace

void frameStart() {
    double t = now();
    if (frameT0 >= 0) {   // the frame that just ended
        cur.total = (float)(t - frameT0);
        cur.work = (float)((workEnd > frameT0 ? workEnd : t) - frameT0);
        hist[head] = cur;
        head = (head + 1) % N;
        filled = std::min(filled + 1, N);
    }
    cur = Frame();
    frameT0 = t;
    workEnd = -1;
}
Scope::Scope(Slot s_) : s(s_), t0(now()) {}
Scope::~Scope() { cur.slot[s] += (float)(now() - t0); }
void substeps(int n) { cur.steps += n; }
void toggle() { CFG.perfOverlay = (CFG.perfOverlay + 1) % 3; saveSettings(); }

namespace {
const Frame& back(int k) { return hist[(head - 1 - k + N * 2) % N]; }   // k frames ago (0: the last one)
ImVec4 fpsColour(double fps) { return fps >= 55 ? ImVec4{0.45f, 0.85f, 0.45f, 1} : fps >= 30 ? ImVec4{0.95f, 0.8f, 0.3f, 1} : ImVec4{0.95f, 0.4f, 0.35f, 1}; }
void line(const char* k, const std::string& v) {
    ImGui::TextDisabled("%s", k);
    ImGui::SameLine(150 * CFG.uiScale);
    ImGui::TextUnformatted(v.c_str());
}
// What makes a part costly to simulate: its corners, checked in every slice it's cut into through its depth
int sliceGuess(const BodyDef& b) {
    if (!b.carves.empty() || b.round || carve::tilted(b) || !b.depthOff.empty()) return 16;
    return 1;
}
// The parts of a gun that cost the physics most: many corners, cut into many slices
void heaviestParts(const Design& D) {
    std::vector<std::pair<double, std::string>> heavy;
    for (auto& b : D.bodies) heavy.push_back({(double)b.pts.size() * sliceGuess(b), b.name.empty() ? b.id : b.name});
    std::sort(heavy.rbegin(), heavy.rend());
    ImGui::TextDisabled("Heaviest parts for the physics (corners x slices):");
    char v[160];
    for (size_t k = 0; k < heavy.size() && k < 3; k++) { std::snprintf(v, sizeof v, "  %s: %.0f", heavy[k].second.c_str(), heavy[k].first); ImGui::TextUnformatted(v); }
    ImGui::TextDisabled("Fewer corners, or fewer cuts, make a part lighter.");
}
}  // namespace

void overlay() {
    workEnd = now();   // (the game's own work for this frame is done; what follows is drawing the overlay and the wait)
    if (CFG.perfOverlay <= 0) return;
    int n = filled;
    if (n < 2) return;
    // the last N frames, oldest first, for the graphs; averages, worst, and the "1% low" (the slowest 1% of frames)
    double sum = 0, worst = 0, work = 0, slot[SlotCount] = {0, 0, 0}; int steps = 0;
    std::vector<float> sorted;
    for (int k = 0; k < n; k++) {
        const Frame& f = back(n - 1 - k);
        plotFrame[k] = f.total;
        plotFps[k] = f.total > 0 ? 1000.f / f.total : 0;
        sum += f.total; worst = std::max(worst, (double)f.total); sorted.push_back(f.total);
    }
    int recent = std::min(n, 60);   // breakdown over the last second or so
    for (int k = 0; k < recent; k++) { const Frame& f = back(k); work += f.work; steps += f.steps; for (int s = 0; s < SlotCount; s++) slot[s] += f.slot[s]; }
    work /= recent; for (double& s : slot) s /= recent;
    std::sort(sorted.begin(), sorted.end());
    double avg = sum / n, low1 = sorted[(size_t)std::max(0, (int)std::floor(n * 0.99) - 1)];
    double fpsNow = back(0).total > 0 ? 1000.0 / back(0).total : 0, fpsAvg = 1000.0 / std::max(0.001, avg);

    float sc = CFG.uiScale, x = 10 * sc, y = TOOLBAR_H + 8 * sc;
    if (A.screen == Screen::Workshop) x = 96 * sc;   // (clear of the designer's tool rail)
    ImGui::SetNextWindowPos({x, y});
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::Begin("##perf", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(fpsColour(fpsAvg), "%.0f FPS", fpsAvg);
    ImGui::SameLine();
    ImGui::Text("%.1f ms a frame", avg);
    ImGui::SameLine();
    ImGui::TextDisabled("(now %.0f, slowest 1%%: %.0f FPS, worst %.0f ms)", fpsNow, 1000.0 / std::max(0.001, low1), worst);
    float gw = 330 * sc, gh = 54 * sc;
    double top = std::max(34.0, worst * 1.1);
    char lab[64];
    std::snprintf(lab, sizeof lab, "frame time (top %.0f ms; 16.7 = 60 FPS)", top);
    ImGui::PlotLines("##ft", plotFrame, n, 0, lab, 0, (float)top, {gw, gh});
    if (CFG.perfOverlay >= 2) {
        float fmax = 0; for (int k = 0; k < n; k++) fmax = std::max(fmax, plotFps[k]);
        std::snprintf(lab, sizeof lab, "frame rate (top %.0f FPS)", fmax);
        ImGui::PlotLines("##fps", plotFps, n, 0, lab, 0, std::max(60.f, fmax * 1.1f), {gw, gh * 0.8f});
        // where the time goes (averaged over the last second)
        ImGui::SeparatorText("Where each frame's time goes");
        double other = std::max(0.0, work - slot[Physics] - slot[Npcs] - slot[Scene]);
        double wait = std::max(0.0, avg - work);
        char v[96];
        std::snprintf(v, sizeof v, "%.2f ms  (%d steps a frame)", slot[Physics], steps / std::max(1, recent)); line("Gun physics", v);
        std::snprintf(v, sizeof v, "%.2f ms", slot[Npcs]); line("NPCs", v);
        std::snprintf(v, sizeof v, "%.2f ms", slot[Scene]); line("Drawing the 3D view", v);
        std::snprintf(v, sizeof v, "%.2f ms", other); line("Panels and the rest", v);
        std::snprintf(v, sizeof v, "%.2f ms", wait); line("Graphics card, screen", v);
        if (wait > avg * 0.45) {   // what the rest of the frame is: held back on purpose, or the graphics card busy
            int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
            int limit = CFG.vsync ? (refresh > 0 ? refresh : 60) : CFG.fpsCap;
            ImGui::PushTextWrapPos(gw + 20 * sc);
            if (limit > 0 && fpsAvg >= limit * 0.93) ImGui::TextDisabled("At the frame rate limit (%d FPS: V-Sync or the cap). The game has time to spare.", limit);
            else ImGui::TextDisabled("Most of the frame is the graphics card drawing the picture: a smaller window, less anti-aliasing or fewer things on screen help.");
            ImGui::PopTextWrapPos();
        } else if (slot[Physics] > avg * 0.4) {
            ImGui::PushTextWrapPos(gw + 20 * sc);
            ImGui::TextDisabled("The gun's physics takes most of the frame: see its heaviest parts below.");
            ImGui::PopTextWrapPos();
        }
        // what's on screen
        ImGui::SeparatorText("On screen");
        if (A.screen == Screen::Range) {
            for (auto& [k, val] : rangeDebugLines()) line(k.c_str(), val);
            if (A.S) heaviestParts(A.design);
        } else if (A.screen == Screen::MapEditor) {
            line("Map", ME.map.name);
            line("Blocks", std::to_string(ME.map.brushes.size()));
            line("Objects", std::to_string(ME.map.ents.size()));
        } else if (A.screen == Screen::NpcEditor) {
            line("NPC", NE.npc.name);
            line("Bones", std::to_string(NE.npc.bones.size()));
        } else if (A.screen != Screen::Menu) {
            const Design& D = A.design;
            int corners = 0, carved = 0;
            for (auto& b : D.bodies) { corners += (int)b.pts.size(); if (!b.carves.empty()) carved++; }
            line("Design", D.name);
            std::snprintf(v, sizeof v, "%d parts, %d joints, %d corners", (int)D.bodies.size(), (int)D.joints.size(), corners); line("Parts", v);
            std::snprintf(v, sizeof v, "%d", carved); line("Carved parts", v);
            if (A.S) {
                std::snprintf(v, sizeof v, "%d bodies, %d joints, %.2f s simulated", (int)A.S->bodies.size(), (int)A.S->joints.size(), A.S->t); line("Simulation", v);
            }
            heaviestParts(D);
        }
        ImGui::SeparatorText("This PC");
        std::snprintf(v, sizeof v, "%d x %d, %s", GetScreenWidth(), GetScreenHeight(), CFG.vsync ? "V-Sync on" : CFG.fpsCap > 0 ? TextFormat("capped at %d FPS", CFG.fpsCap) : "uncapped");
        line("Window", v);
        line("Graphics", gpuInfo().renderer);
    }
    ImGui::TextDisabled(CFG.perfOverlay >= 2 ? "F3: hide" : "F3: more details");
    ImGui::End();
}
}  // namespace perf
