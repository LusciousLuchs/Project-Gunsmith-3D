// Gunsmith3D - career mode on foot (see shop3d.hpp).
#include "app/shop3d.hpp"
#include "app/app.hpp"
#include "app/careermode.hpp"
#include "app/mapedit.hpp"
#include "app/npcedit.hpp"
#include "app/keys.hpp"
#include "app/settings.hpp"
#include "app/view.hpp"
#include "app/gfx.hpp"
#include "app/fonts.hpp"
#include "ui/ui.hpp"
#include "map.hpp"
#include "npc.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {
int gOpen = -1;            // the screen open: 0 computer, 1 workstation, 2 counter, 3 front door; -1 none
gmap::Player gStood;       // where you stood when you went to build
bool gHaveStood = false;

int thingOf(const std::string& type) {
    if (type == "shop_computer") return 0;
    if (type.rfind("station_", 0) == 0) return 1;
    if (type == "shop_counter") return 2;
    if (type == "shop_door") return 3;
    return -1;
}
gmap::V3 frontOf(const gmap::Entity& e) { double a = e.yaw * 3.14159265358979 / 180; return {std::cos(a), 0, std::sin(a)}; }
const gmap::Entity* findType(const char* t) { for (auto& e : ME.map.ents) if (e.type == t) return &e; return nullptr; }

// what you're looking at, close enough to use: its entity index, or -1
int facing() {
    gmap::V3 feet = ME.pl.feet, f = gmap::playerForward(ME.pl);
    double fl = std::hypot(f.x, f.z);
    if (fl < 1e-6) return -1;
    int best = -1; double bd = 1e9;
    for (int i = 0; i < (int)ME.map.ents.size(); i++) {
        const auto& e = ME.map.ents[i];
        if (thingOf(e.type) < 0) continue;
        double dx = e.pos.x - feet.x, dz = e.pos.z - feet.z, d = std::hypot(dx, dz);
        if (d > 2.0) continue;
        double look = d < 1e-6 ? 1 : (dx * f.x + dz * f.z) / (d * fl);
        if (d > 0.9 && look < 0.45) continue;   // (right beside it counts; further off you have to face it)
        double score = d - look;
        if (score < bd) { bd = score; best = i; }
    }
    return best;
}
const char* promptFor(const gmap::Entity& e) {
    static char buf[160];
    int waiting = 0;
    for (auto& w : CAREER.walkins) if (w.state == 0) waiting++;
    switch (thingOf(e.type)) {
        case 0: return "F  Use the computer: online store, contracts, staff";
        case 1: return e.type == "station_bench" ? "F  Your guns: rate them, build copies" : "F  Your guns (at the workstation)";
        case 2: std::snprintf(buf, sizeof buf, waiting ? "F  Serve at the counter (%d waiting)" : "F  The counter (nobody waiting)", waiting); return buf;
        case 3: return "F  Close up for the day";
    }
    return "";
}
void openThing(int thing) { gOpen = thing; EnableCursor(); }

// where the people stand
struct Spot { gmap::V3 at; double yaw; int person; };
std::vector<Spot> people() {
    std::vector<Spot> out;
    const gmap::Entity* counter = findType("shop_counter");
    const gmap::Entity* door = findType("shop_door");
    if (counter) {   // customers queue on the counter's far side (the door's side), facing it
        gmap::V3 f = frontOf(*counter), s = f * -1.0;
        if (door) {
            gmap::V3 to = door->pos - counter->pos;
            if (to.x * f.x + to.z * f.z > 0) s = f;
        }
        double yaw = std::atan2(-s.z, -s.x) * 180 / 3.14159265358979;
        int k = 0;
        for (auto& w : CAREER.walkins) {
            if (w.state != 0) continue;
            gmap::V3 side{-s.z, 0, s.x};
            out.push_back({counter->pos + s * (0.75 + 0.75 * k) + side * (k % 2 ? 0.25 : -0.1 * k), yaw, w.id});
            if (++k >= 6) break;
        }
    }
    // staff: each at a workstation that suits them (you keep the first bench)
    std::vector<const gmap::Entity*> free;
    bool mine = false;
    for (auto& e : ME.map.ents)
        if (e.type.rfind("station_", 0) == 0) {
            if (e.type == "station_bench" && !mine) { mine = true; continue; }
            free.push_back(&e);
        }
    for (auto& emp : CAREER.staff) {
        std::vector<std::string> want;
        if (emp.specialty == "machinist") want = {"station_lathe", "station_mill", "station_bench"};
        else if (emp.specialty == "finisher") want = {"station_qc", "station_bench"};
        else want = {"station_bench", "station_qc"};
        const gmap::Entity* got = nullptr;
        for (auto& w : want) {
            for (auto it = free.begin(); it != free.end(); ++it) if ((*it)->type == w) { got = *it; free.erase(it); break; }
            if (got) break;
        }
        if (!got && !free.empty()) { got = free.front(); free.erase(free.begin()); }
        if (!got) break;
        gmap::V3 f = frontOf(*got);
        out.push_back({got->pos + f * 0.7, std::atan2(-f.z, -f.x) * 180 / 3.14159265358979, (int)(std::hash<std::string>()(emp.name) & 0x7fffffff)});
    }
    return out;
}
}  // namespace

bool shopActive() { return inCareer() && A.screen == Screen::MapEditor && !ME.careerFile.empty(); }
bool shopWalking() { return shopActive() && ME.walking; }

void shopEnter() {
    gmap::addShopBasics(ME.map);
    ME.version++;
    gOpen = -1;
    CAREER.stations = gmap::workshopStations(ME.map);
}

bool shopWalkUpdate(float dt) {
    (void)dt;
    if (gOpen >= 0) {
        if (IsKeyPressed(KEY_ESCAPE) || (actPressed(Act::Use) && !ImGui::GetIO().WantCaptureKeyboard)) shopCloseThing();
        return true;
    }
    if (!IsCursorHidden()) DisableCursor();   // (back from the pause menu)
    if (IsKeyPressed(KEY_ESCAPE)) { A.showPause = true; EnableCursor(); return true; }
    if (IsKeyPressed(KEY_B)) { shopBuildMode(); return true; }
    if (actPressed(Act::Use)) {
        int i = facing();
        if (i >= 0) { openThing(thingOf(ME.map.ents[i].type)); return true; }
    }
    return false;
}

void shopDrawPeople() {
    if (ME.walking) {   // a ceiling over the walls while you're inside on foot (the workshop is built without a roof, so build mode sees in)
        double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9, top = 0;
        for (auto& b : ME.map.brushes) {
            gmap::V3 mn, mx;
            gmap::brushBounds(b, mn, mx);
            if (mx.y < 1.5 || mn.y > 1.0) continue;   // walls: standing on the floor, taller than a person
            x0 = std::min(x0, mn.x); x1 = std::max(x1, mx.x); z0 = std::min(z0, mn.z); z1 = std::max(z1, mx.z); top = std::max(top, mx.y);
        }
        if (x1 > x0 && z1 > z0 && top > 0)
            drawBox({(float)((x0 + x1) / 2), (float)top + 0.05f, (float)((z0 + z1) / 2)}, {(float)(x1 - x0), 0.1f, (float)(z1 - z0)}, Color{150, 146, 138, 255});
    }
    for (auto& p : people()) drawNpcAt(careerPerson(p.person), {(float)p.at.x, (float)p.at.y, (float)p.at.z}, (float)p.yaw, 0.001f);
}

void shopHud() {
    auto& C = CAREER;
    float s = CFG.uiScale;
    // the day, money and how well known you are, top left
    const char* top = TextFormat("Day %d    %s    %s", C.day, TextFormat("$%.0f", C.money), career::tierName(C.reputation));
    float w = viewTextWidth(top, 15);
    DrawRectangleRounded({12 * s, 12 * s, w + 24 * s, 30 * s}, 0.4f, 6, Color{20, 23, 28, 170});
    viewText(top, 24 * s, 19 * s, 15, Color{235, 238, 242, 240});
    if (gOpen >= 0) return;
    float cx = GetScreenWidth() / 2.0f, cy = GetScreenHeight() / 2.0f;
    DrawCircleV({cx, cy}, 2.5f * s, Color{240, 240, 240, 200});   // where you're looking
    int i = facing();
    if (i >= 0) {
        const char* p = promptFor(ME.map.ents[i]);
        float pw = viewTextWidth(p, 15);
        DrawRectangleRounded({cx - pw / 2 - 14 * s, cy + 40 * s, pw + 28 * s, 30 * s}, 0.4f, 6, Color{20, 23, 28, 190});
        viewText(p, cx - pw / 2, cy + 47 * s, 15, Color{255, 214, 120, 255});
    }
    const char* help = "WASD walk    F use    B build mode    Esc menu";
    float hw = viewTextWidth(help, 12);
    viewText(help, cx - hw / 2, GetScreenHeight() - 30 * s, 12, Color{200, 204, 212, 170});
}

void shopWindows() {
    careerTestPoll();   // (a bench test finishing while you walk)
    if (gOpen < 0) return;
    if (!careerThingWindow(gOpen)) shopCloseThing();
}

void shopOpen(int thing) { if (shopWalking()) openThing(thing); }
void shopCloseThing() {
    gOpen = -1;
    if (shopWalking() && !A.showPause) DisableCursor();
}

void shopBuildMode() {
    if (!shopActive()) return;
    gOpen = -1;
    gStood = ME.pl; gHaveStood = true;
    meStopWalking();
    ME.cam = {ME.pl.feet.x - 4, ME.pl.feet.y + 5, ME.pl.feet.z + 4}; ME.yaw = -0.8; ME.pitch = -0.6;
    toast("Build mode: the level editor's tools on your shop. Workstation tool for benches, lathes, the counter, the computer and the door. B to walk again.");
}

void shopWalk() {
    if (!shopActive()) return;
    gmap::addShopBasics(ME.map);
    CAREER.stations = gmap::workshopStations(ME.map);
    careerSave();
    meWalk(false);
    if (gHaveStood) { ME.pl = gStood; }
    gOpen = -1;
}

void shopNewDay() {
    int waiting = 0;
    for (auto& w : CAREER.walkins) if (w.state == 0) waiting++;
    toast(TextFormat("Day %d. %s", CAREER.day, waiting ? TextFormat("%d customer%s at the counter.", waiting, waiting == 1 ? "" : "s") : "Nobody at the counter yet."));
}
