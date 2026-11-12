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
#include "app/render.hpp"
#include "app/doors.hpp"
#include "raymath.h"
#include "app/fonts.hpp"
#include "ui/ui.hpp"
#include "map.hpp"
#include "npc.hpp"
#include <map>
#include "npcai.hpp"
#include "nav.hpp"
#include "imgui.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {
int gOpen = -1;
bool gAskMissing = false;   // Walk was pressed with things missing: ask            // the screen open: 0 computer, 1 workstation, 2 counter, 3 front door; -1 none
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
    for (auto& w : CAREER.walkins) if (w.state == 0 && (CAREER.props.empty() || w.prop == CAREER.here)) waiting++;
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
struct Spot { gmap::V3 at; double yaw; int person; std::string name; bool staff = false; };
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
        // the displays they look at while they wait (each stands in front of one, facing it), then the queue
        std::vector<const gmap::Entity*> shows;
        for (auto& e : ME.map.ents) if (gmap::isDisplay(e.type)) shows.push_back(&e);
        int k = 0, browsing = 0;
        bool first = true;
        for (auto& w : CAREER.walkins) {
            if (w.state != 0 || (!CAREER.props.empty() && w.prop != CAREER.here)) continue;
            if (!first && browsing < (int)shows.size()) {   // (the first waits at the counter; the others look round)
                const gmap::Entity& d = *shows[browsing++];
                gmap::V3 f = frontOf(d);
                out.push_back({d.pos + f * 0.75, std::atan2(f.z, f.x) * 180 / 3.14159265358979 + 180, w.id, w.name});
                continue;
            }
            first = false;
            gmap::V3 side{-s.z, 0, s.x};
            out.push_back({counter->pos + s * (0.75 + 0.75 * k) + side * (k % 2 ? 0.25 : -0.1 * k), yaw, w.id, w.name});
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
        out.push_back({got->pos + f * 0.7, std::atan2(-f.z, -f.x) * 180 / 3.14159265358979, (int)(std::hash<std::string>()(emp.name) & 0x7fffffff), emp.name, true});
    }
    if (ME.map.build.on()) {   // (everyone inside the building)
        const gmap::Zone& z = ME.map.build;
        for (auto& p : out) { p.at.x = std::clamp(p.at.x, z.x0 + 0.35, z.x1 - 0.35); p.at.z = std::clamp(p.at.z, z.z0 + 0.35, z.z1 - 0.35); }
    }
    return out;
}
// People walking: each goes from where they are to their spot (a display, the counter's queue, a workstation) along a
// path round the furniture; customers come in at the front door and walk back out once served.
struct Walker {
    int person = 0; std::string name;
    gmap::V3 pos; double yaw = 0, phase = 0, speed = 0;
    std::vector<gmap::V3> path;
    gmap::V3 goal; double goalYaw = 0;
    bool leaving = false, staff = false;
    double idle = 0;   // seconds standing at their spot (they glance about)
};
std::map<int, Walker> gWalkers;
nav::Cache gNav;
unsigned gNavVersion = ~0u;
std::string gWalkFile;
gmap::V3 doorSpot() {
    if (const gmap::Entity* d = findType("shop_door")) return d->pos;
    if (const gmap::Entity* s = findType("player_start")) return s->pos;
    return {0, 0, 0};
}
void plan(Walker& w, gmap::V3 to) {
    w.goal = to;
    w.path.clear();
    nav::Agent ag; ag.halfWidth = 0.22; ag.height = 1.7;
    const nav::Grid& g = gNav.get(ME.map, ag);
    std::vector<gmap::V3> route;
    if (nav::findPath(g, w.pos, to, route) && !route.empty()) w.path = route;
    else w.path = {to};   // (off the grid: straight there)
}
void stepWalkers(float dt) {
    if (gNavVersion != (unsigned)ME.version || gWalkFile != ME.careerFile) {   // the shop changed: plan again on the new floor
        gNav.clear(); gNavVersion = (unsigned)ME.version;
        if (gWalkFile != ME.careerFile) { gWalkers.clear(); gWalkFile = ME.careerFile; }
        for (auto& [id, w] : gWalkers) plan(w, w.goal);
    }
    auto spots = people();
    std::map<int, const Spot*> want;
    for (auto& sp : spots) want[sp.person] = &sp;
    for (auto& sp : spots) {
        auto it = gWalkers.find(sp.person);
        if (it == gWalkers.end()) {   // someone new: customers come in at the door, staff are already at work
            Walker w; w.person = sp.person; w.name = sp.name; w.staff = sp.staff;
            w.pos = sp.staff ? sp.at : doorSpot();
            w.yaw = sp.yaw; w.goalYaw = sp.yaw;
            if (sp.staff) { w.goal = sp.at; } else plan(w, sp.at);
            gWalkers[sp.person] = w;
            continue;
        }
        Walker& w = it->second;
        w.goalYaw = sp.yaw; w.leaving = false;
        double dx = sp.at.x - w.goal.x, dz = sp.at.z - w.goal.z;
        if (dx * dx + dz * dz > 0.09) plan(w, sp.at);   // (their spot moved: a new place in the queue)
    }
    for (auto it = gWalkers.begin(); it != gWalkers.end();) {
        Walker& w = it->second;
        if (!want.count(it->first) && !w.leaving) {   // served, sent away or gone home: out the door
            w.leaving = true;
            if (w.staff) { it = gWalkers.erase(it); continue; }
            plan(w, doorSpot());
        }
        // walk along the path
        double moved = 0, want2 = 1.15 * dt;
        while (!w.path.empty() && moved < want2) {
            gmap::V3 t = w.path.front();
            double dx = t.x - w.pos.x, dz = t.z - w.pos.z, d = std::hypot(dx, dz);
            if (d < 1e-4) { w.path.erase(w.path.begin()); continue; }
            double stp = std::min(d, want2 - moved);
            w.pos.x += dx / d * stp; w.pos.z += dz / d * stp; w.pos.y = t.y;
            moved += stp;
            double ty = std::atan2(dz, dx) * 180 / 3.14159265358979, dy = std::remainder(ty - w.yaw, 360.0);
            w.yaw += std::clamp(dy, -400.0 * dt, 400.0 * dt);
            if (stp >= d - 1e-6) w.path.erase(w.path.begin());
        }
        w.speed = dt > 0 ? moved / dt : 0;
        if (moved > 0) {   // a shut door in their way: they open it
            gmap::V3 probe = w.pos; int door = -1;
            if (doors::pushOut(ME.map, probe, 0.45, 1.7, &door) && !doors::isOpen(ME.map, door)) doors::toggle(ME.map, door, w.pos);
        }
        w.phase += moved * 2 * 3.14159265358979 / 0.8;
        if (w.path.empty()) {
            if (w.leaving) { it = gWalkers.erase(it); continue; }
            double dy = std::remainder(w.goalYaw - w.yaw, 360.0);
            w.yaw += std::clamp(dy, -200.0 * dt, 200.0 * dt);   // turn to face what they came to
            w.idle += dt;
        } else w.idle = 0;
        ++it;
    }
}
}  // namespace

bool shopActive() { return inCareer() && A.screen == Screen::MapEditor && !ME.careerFile.empty(); }
bool shopWalking() { return shopActive() && ME.walking; }

void shopEnter() {
    size_t had = ME.map.ents.size();
    gmap::addShopBasics(ME.map);
    gmap::shopRepair(ME.map);
    if (int n = (int)(ME.map.ents.size() - had); n > 0) toast(TextFormat("Your shop was missing %d thing%s it needs: put back near where you start.", n, n == 1 ? "" : "s"));
    ME.version++;
    gOpen = -1;
    careerNoteMap(ME.map, CAREER.here);
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
        int d = doors::facing(ME.map, gmap::playerEye(ME.pl, ME.map), gmap::playerForward(ME.pl));
        if (d >= 0) { doors::toggle(ME.map, d, ME.pl.feet); return true; }
    }
    return false;
}

void shopDrawPeople() {
    if (!ME.walking && ME.map.build.on()) {   // build mode: the property's floor (white) and its storefront (gold), outlined
        auto outline = [](const gmap::Zone& z, Color c) {
            float y = 0.03f;
            Vector3 a{(float)z.x0, y, (float)z.z0}, b{(float)z.x1, y, (float)z.z0}, cc{(float)z.x1, y, (float)z.z1}, d{(float)z.x0, y, (float)z.z1};
            DrawLine3D(a, b, c); DrawLine3D(b, cc, c); DrawLine3D(cc, d, c); DrawLine3D(d, a, c);
            for (float t = 0.02f; t < 0.06f; t += 0.02f) { DrawLine3D({a.x, t, a.z}, {b.x, t, b.z}, c); DrawLine3D({b.x, t, b.z}, {cc.x, t, cc.z}, c); DrawLine3D({cc.x, t, cc.z}, {d.x, t, d.z}, c); DrawLine3D({d.x, t, d.z}, {a.x, t, a.z}, c); }
        };
        outline(ME.map.build, Color{230, 235, 240, 255});
        if (ME.map.store.on()) outline(ME.map.store, Color{255, 196, 70, 255});
    }
    if (ME.walking && ME.map.build.on() && ME.map.roofY > 0) {   // a property: its ceiling over its floor
        const gmap::Zone& z = ME.map.build;
        drawBox({(float)((z.x0 + z.x1) / 2), (float)ME.map.roofY + 0.05f, (float)((z.z0 + z.z1) / 2)}, {(float)(z.x1 - z.x0) + 0.6f, 0.1f, (float)(z.z1 - z.z0) + 0.6f}, Color{150, 146, 138, 255});
    } else if (ME.walking) {   // a ceiling over the walls while you're inside on foot (the workshop is built without a roof, so build mode sees in)
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
    if (std::getenv("GUNSMITH_PEOPLESHEET"))   // (screenshots) the first dozen people the shop would make, in a row
        for (int i = 0; i < 12; i++) drawNpcAt(careerPerson(10000 + i, ""), {(float)(-4.4 + 0.8 * i), 0, -1.5f}, 90, 0.001f);
    // your guns on the displays
    for (auto& e : ME.map.ents) {
        if ((e.type != "display_case" && e.type != "display_rack") || e.ref.empty()) continue;
        const CareerGun* g = careerGun(e.ref);
        if (!g || g->d.bodies.empty()) continue;
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (auto& b : g->d.bodies) for (auto& q : b.pts) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
        double len = std::max(1.0, x1 - x0);
        float k = (float)(0.001 / U) * (float)std::min(1.0, 1250.0 / len);   // (metres; a long gun shrunk to fit the display)
        Matrix M = MatrixMultiply(MatrixTranslate(-(float)((x0 + x1) / 2) * U, (float)((y0 + y1) / 2) * U, 0), MatrixScale(k, k, k));
        bool lying = e.type == "display_case";
        if (lying) M = MatrixMultiply(M, MatrixRotateX(-PI / 2));   // (on its side, under the glass)
        M = MatrixMultiply(M, MatrixRotateY(PI / 2));
        M = MatrixMultiply(M, MatrixRotateY(-(float)e.yaw * DEG2RAD));
        Vector3 off = Vector3Transform(lying ? Vector3{0, 0.9f, 0} : Vector3{0.06f, 1.45f, 0}, MatrixRotateY(-(float)e.yaw * DEG2RAD));
        M = MatrixMultiply(M, MatrixTranslate((float)e.pos.x + off.x, (float)e.pos.y + off.y, (float)e.pos.z + off.z));
        drawDesignModel("display:" + e.ref, g->d, M);
    }
    stepWalkers(std::min(GetFrameTime(), 0.1f));
    for (auto& [id, w] : gWalkers) {
        const NpcDef& n = careerPerson(w.person, w.name);
        double look = w.path.empty() && !w.staff ? 18 * std::sin(w.idle * 0.6 + id) : 0;   // (standing about: they look round)
        drawNpcAt(w.speed > 0.05 ? npcai::walking(n, w.phase, w.speed) : n, {(float)w.pos.x, (float)w.pos.y, (float)w.pos.z}, (float)(w.yaw + look * 0.25), 0.001f);
    }
}

void shopHud() {
    auto& C = CAREER;
    float s = CFG.uiScale;
    // the day, money and how well known you are, top left
    std::string where;
    if (const career::Property* hp = career::propertyById(C, C.here)) {
        where = hp->name.empty() ? career::propertyKind(hp->kind).name : hp->name;
        if (hp->sells()) where += TextFormat("   store %.1f / 5", hp->rating);
    }
    const char* top = TextFormat("Day %d    %s    %s    %s", C.day, TextFormat("$%.0f", C.money), career::tierName(C.reputation), where.c_str());
    float w = viewTextWidth(top, 15);
    DrawRectangleRounded({12 * s, 12 * s, w + 24 * s, 30 * s}, 0.4f, 6, Color{20, 23, 28, 170});
    viewText(top, 24 * s, 19 * s, 15, Color{235, 238, 242, 240});
    if (gOpen >= 0) return;
    float cx = GetScreenWidth() / 2.0f, cy = GetScreenHeight() / 2.0f;
    DrawCircleV({cx, cy}, 2.5f * s, Color{240, 240, 240, 200});   // where you're looking
    int i = facing();
    int dr = i < 0 ? doors::facing(ME.map, gmap::playerEye(ME.pl, ME.map), gmap::playerForward(ME.pl)) : -1;
    if (dr >= 0) {
        const char* p = TextFormat("F  %s the door", doors::isOpen(ME.map, dr) ? "Shut" : "Open");
        float pw = viewTextWidth(p, 15);
        DrawRectangleRounded({cx - pw / 2 - 14 * s, cy + 40 * s, pw + 28 * s, 30 * s}, 0.4f, 6, Color{20, 23, 28, 190});
        viewText(p, cx - pw / 2, cy + 47 * s, 15, Color{255, 214, 120, 255});
    }
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
    if (gAskMissing && shopActive() && !ME.walking) {
        auto miss = gmap::shopMissing(ME.map);
        if (miss.empty()) { gAskMissing = false; shopWalk(); return; }
        ImGui::OpenPopup("Your shop isn't ready");
        ImGui::SetNextWindowPos({GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f}, ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({440 * CFG.uiScale, 0});
        if (ImGui::BeginPopupModal("Your shop isn't ready", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::TextWrapped("Your shop needs these to work:");
            for (auto& m : miss) { ImGui::BulletText("%s", m.what.c_str()); ImGui::Indent(); ImGui::TextDisabled("%s", m.tool.c_str()); ImGui::Unindent(); }
            ImGui::Dummy({0, 6 * CFG.uiScale});
            float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
            if (ImGui::Button("Put them in for me", {w, 30 * CFG.uiScale})) {
                meSnapshot();
                gmap::shopRepair(ME.map);
                meChanged();
                gAskMissing = false;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                shopWalk();
                return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep building", {w, 30 * CFG.uiScale}) || IsKeyPressed(KEY_ESCAPE)) { gAskMissing = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
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
    if (!gmap::shopMissing(ME.map).empty()) { gAskMissing = true; return; }   // (the window in shopWindows asks what to do)
    careerNoteMap(ME.map, CAREER.here);
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

void cursorGuard() {
    // the cursor is hidden only while you look around on foot (a map walked, the range); anywhere else it shows. In your
    // shop it also shows while one of its screens, the pause menu or a question is open
    bool walkingMap = A.screen == Screen::MapEditor && ME.walking;
    if (A.screen == Screen::Range) return;   // (the range keeps its own: Alt frees it)
    bool want = walkingMap;
    if (walkingMap && shopActive()) want = gOpen < 0 && !gAskMissing && !A.showPause && !A.showSettings && !A.showTutorials;
    else if (walkingMap) want = !A.showPause && !A.showSettings && !A.showTutorials;
    if (want != IsCursorHidden()) { if (want) DisableCursor(); else EnableCursor(); }
}
