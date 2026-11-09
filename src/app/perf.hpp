// Gunsmith3D - the performance overlay (F3, or Settings > Interface): frame rate and frame time over the last few
// seconds as graphs, where each frame's time goes (the gun's physics, NPCs, drawing the 3D view, the rest, waiting
// for the screen), and details of what's on screen, so a laggy design or map can be tracked down.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace perf {
enum Slot { Physics, Npcs, Scene, SlotCount };
void frameStart();                    // at the top of every frame
struct Scope {                        // times what runs while it exists, into a slot
    Slot s; double t0;
    explicit Scope(Slot s);
    ~Scope();
};
void substeps(int n);                 // physics steps run this frame (added up)
void toggle();                        // off -> frame rate -> full details -> off
void overlay();                       // draws it (inside the ImGui frame, last), when it's on
}  // namespace perf

// Lines of debug details from the firing range (what's alive, in flight, the map)
std::vector<std::pair<std::string, std::string>> rangeDebugLines();
