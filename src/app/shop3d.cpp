// Gunsmith3D - career mode on foot (see shop3d.hpp).
#include "app/shop3d.hpp"
#include "app/mapgfx.hpp"
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
    std::vector<Spot> out, waitOut;   // (waitOut: those waiting outside the door, left where they are)
    const gmap::Entity* counter = findType("shop_counter");
    const gmap::Entity* door = findType("shop_door");
    if (counter) {   // customers queue on the counter's far side (the door's side), facing it
        gmap::V3 f = frontOf(*counter), s = f * -1.0;
        if (door) {
            gmap::V3 to = door->pos - counter->pos;
            if (to.x * f.x + to.z * f.z > 0) s = f;
        }
        double yaw = std::atan2(-s.z, -s.x) * 180 / 3.14159265358979;
        // the queue: a line straight back from the counter, 0.8 m apart, each facing the one ahead; where it would run
        // into a wall, a fixture or out of the building it turns along it. Anyone past the room in the line looks at
        // the displays while they wait.
        auto freeAt = [&](gmap::V3 p) {
            if (ME.map.build.on() && !ME.map.build.has(p, -0.2)) return false;
            if (door && std::hypot(p.x - door->pos.x, p.z - door->pos.z) < 1.1) return false;   // (keep the way in clear)
            for (auto& e : ME.map.ents) {   // workstations, the counter and displays are drawn, not blocks: keep off them
                bool big = e.type == "shop_counter" || e.type.rfind("station_", 0) == 0 || gmap::isDisplay(e.type);
                if (!big && e.type != "shop_computer") continue;
                gmap::V3 ef = frontOf(e), d = p - e.pos;
                double across = std::fabs(d.x * ef.x + d.z * ef.z), along = std::fabs(-d.x * ef.z + d.z * ef.x);
                if (across < 0.65 && along < (e.type == "shop_counter" ? 1.3 : 0.9)) return false;
            }
            return gmap::boxFree(ME.map, {p.x, p.y + 0.95, p.z}, {0.22, 0.85, 0.22});
        };
        std::vector<gmap::V3> line;
        {
            gmap::V3 dir = s, at = counter->pos + s * 0.75;
            for (int k = 0; k < 10; k++) {
                if (k > 0) {
                    gmap::V3 nx = at + dir * 0.8;
                    if (!freeAt(nx)) {   // turn: whichever way along the obstacle has room
                        gmap::V3 l{-dir.z, 0, dir.x}, r{dir.z, 0, -dir.x};
                        auto room = [&](gmap::V3 d) { int n = 0; while (n < 6 && freeAt(at + d * (0.8 * (n + 1)))) n++; return n; };
                        int nl = room(l), nr = room(r);
                        if (nl == 0 && nr == 0) break;
                        // toward the way in if there's room (so nobody has to squeeze past the ones already waiting),
                        // else the side with the longer run
                        gmap::V3 in = door ? door->pos - at : gmap::V3{0, 0, 0};
                        double tl = in.x * l.x + in.z * l.z;
                        if (door && nl > 0 && tl > 0.1) dir = l;
                        else if (door && nr > 0 && tl < -0.1) dir = r;
                        else dir = nl >= nr ? l : r;
                        nx = at + dir * 0.8;
                    }
                    at = nx;
                }
                line.push_back(at);
            }
        }
        std::vector<const gmap::Entity*> shows;
        for (auto& e : ME.map.ents) if (gmap::isDisplay(e.type)) shows.push_back(&e);
        int k = 0, browsing = 0, outside = 0;
        for (auto& w : CAREER.walkins) {
            if (w.state != 0 || (!CAREER.props.empty() && w.prop != CAREER.here)) continue;
            if (k < (int)line.size()) {
                gmap::V3 ahead = k == 0 ? counter->pos : line[k - 1], p = line[k];
                double fy = std::atan2(ahead.z - p.z, ahead.x - p.x) * 180 / 3.14159265358979;
                out.push_back({p, k == 0 ? yaw : fy, w.id, w.name});
                k++;
            } else if (browsing < (int)shows.size()) {
                const gmap::Entity& d = *shows[browsing++];
                gmap::V3 f = frontOf(d);
                out.push_back({d.pos + f * 0.75, std::atan2(f.z, f.x) * 180 / 3.14159265358979 + 180, w.id, w.name});
            } else if (door) {   // no room left inside: they wait outside the door, one behind the other
                gmap::V3 o = door->pos - (ME.map.build.on() ? gmap::V3{(ME.map.build.x0 + ME.map.build.x1) / 2, 0, (ME.map.build.z0 + ME.map.build.z1) / 2} : counter->pos);
                o.y = 0;
                double ol = std::hypot(o.x, o.z);
                o = ol > 1e-6 ? o * (1 / ol) : gmap::V3{1, 0, 0};
                if (std::fabs(o.x) > std::fabs(o.z)) o = {o.x > 0 ? 1.0 : -1.0, 0, 0}; else o = {0, 0, o.z > 0 ? 1.0 : -1.0};   // (square to the wall)
                gmap::V3 p = door->pos + o * (1.3 + 0.8 * outside++);
                waitOut.push_back({p, std::atan2(-o.z, -o.x) * 180 / 3.14159265358979, w.id, w.name});
            }
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
    auto inside = [&](gmap::V3& p) {
        if (!ME.map.build.on()) return;
        const gmap::Zone& z = ME.map.build;
        p.x = std::clamp(p.x, z.x0 + 0.35, z.x1 - 0.35); p.z = std::clamp(p.z, z.z0 + 0.35, z.z1 - 0.35);
    };
    for (auto& p : out) inside(p.at);   // (everyone inside the building, but for those waiting outside)
    out.insert(out.end(), waitOut.begin(), waitOut.end());
    // nobody stands in anybody else: a spot squeezed onto another (a queue running into a wall) steps aside
    for (size_t i = 0; i < out.size(); i++) {
        const gmap::V3 base = out[i].at;
        for (int tries = 0; tries < 12; tries++) {
            bool clash = false;
            for (size_t j = 0; j < i && !clash; j++) clash = std::hypot(out[i].at.x - out[j].at.x, out[i].at.z - out[j].at.z) < 0.55;
            if (!clash) break;
            double a = tries * 2.39996;   // (round and out, a golden-angle spiral)
            double r = 0.6 * (1 + tries / 3);
            out[i].at = {base.x + std::cos(a) * r, base.y, base.z + std::sin(a) * r};
            inside(out[i].at);
        }
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
    std::map<int, int> rank;   // who gives way: the one further back in the queue gives way to the one ahead of them
    for (size_t i = 0; i < spots.size(); i++) rank[spots[i].person] = (int)i + 1;
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
        // walk along the path, minding the others: behind someone going your way (or standing), slow down and wait;
        // meeting someone coming the other way, both keep to their right and pass
        if (w.path.empty() && !w.leaving) {   // nudged off their spot: step back to it
            double gx = w.goal.x - w.pos.x, gz = w.goal.z - w.pos.z;
            if (gx * gx + gz * gz > 0.01) w.path.push_back(w.goal);
        }
        auto yieldsTo = [&](int me, const Walker& a, int other, const Walker& b) {
            auto rk = [&](int id, const Walker& x) { return x.leaving ? 0 : (rank.count(id) ? rank[id] : 1000); };
            int ra = rk(me, a), rb = rk(other, b);
            return ra != rb ? ra > rb : me > other;
        };
        double want2 = 1.15 * dt;
        auto headOf = [](const Walker& o, double& x, double& z) {
            x = z = 0;
            if (o.path.empty()) return false;
            double dx = o.path.front().x - o.pos.x, dz = o.path.front().z - o.pos.z, l = std::hypot(dx, dz);
            if (l < 1e-4) return false;
            x = dx / l; z = dz / l; return true;
        };
        double hx, hz;
        if (headOf(w, hx, hz)) {
            for (auto& [oid, o] : gWalkers) {
                if (oid == it->first || !yieldsTo(it->first, w, oid, o)) continue;
                double ox = o.pos.x - w.pos.x, oz = o.pos.z - w.pos.z, od = std::hypot(ox, oz);
                if (od < 1e-4 || od > 1.0) continue;
                double ahead = (ox * hx + oz * hz) / od, px, pz;
                bool moving = headOf(o, px, pz);
                bool oncoming = moving && px * hx + pz * hz < -0.3;
                double side = (ox * -hz + oz * hx);   // + : they're on our left
                if (ahead > 0.5 && !oncoming && od < 0.8 && std::fabs(side) < 0.4)
                    want2 *= std::clamp((od - 0.5) / 0.3, 0.0, 1.0);   // (queue up behind them)
                if (ahead > 0.3 && (oncoming || !moving) && od < 1.0 && std::fabs(side) < 0.45) {   // keep right of them
                    double k = std::min(1.0, (double)dt * 3) * (1.0 - od) * 0.6;
                    w.pos.x += hz * k; w.pos.z += -hx * k;
                }
            }
        }
        for (auto& [oid, o] : gWalkers) {   // never inside each other (the one giving way is the one moved)
            if (oid == it->first || !yieldsTo(it->first, w, oid, o)) continue;
            double ox = o.pos.x - w.pos.x, oz = o.pos.z - w.pos.z, od = std::hypot(ox, oz);
            if (od < 1e-4 || od > 0.5) continue;
            double push = (0.5 - od) * 0.5 * std::min(1.0, (double)dt * 8);
            w.pos.x -= ox / od * push; w.pos.z -= oz / od * push;
        }
        double moved = 0;
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

// the shop's clock: the business day opens at 8 and the clock runs while you're in the shop; once the day is over it
// stops at midnight until you close up (the owner's choice). A day takes the map's own day length, else 20 minutes.
static double gShopHour = 8;
double shopHour() { return gShopHour; }
void shopClockTick(float dt) {
    double mins = ME.map.look.dayMinutes > 0 ? ME.map.look.dayMinutes : 20;
    gShopHour = std::min(24.0, gShopHour + dt * 24.0 / (mins * 60));
}
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


// The customer at the counter lays their transfer form and then their ID on the counter top, slid across to you.
static void drawCounterPapers() {
    const gmap::Entity* counter = findType("shop_counter");
    if (!counter) return;
    const Walker* at = nullptr;
    for (auto& [id, w] : gWalkers) {
        if (w.staff || w.leaving || !w.path.empty()) continue;
        if (std::hypot(w.pos.x - counter->pos.x, w.pos.z - counter->pos.z) > 1.1) continue;
        if (!at || w.idle > at->idle) at = &w;
    }
    if (!at) return;
    gmap::V3 f = frontOf(*counter);
    float sg = (at->pos.x - counter->pos.x) * f.x + (at->pos.z - counter->pos.z) * f.z > 0 ? 1.0f : -1.0f;   // (their side)
    Matrix R = MatrixRotateY(-(float)counter->yaw * DEG2RAD);
    Vector3 base{(float)counter->pos.x, (float)counter->pos.y, (float)counter->pos.z};
    auto lay = [&](double delay, float x, float z, float turnDeg, float w, float len, std::function<void(std::function<void(float, float, float, float, float, Color)>)> marks) {
        float t = (float)std::clamp((at->idle - delay) / 0.6, 0.0, 1.0);
        if (t <= 0) return;
        float e = 1 - (1 - t) * (1 - t);                              // (eased: put down, slid over)
        float xx = sg * (x + (1 - e) * 0.3f), lift = 0.12f * std::sin(t * PI) * (1 - t);
        Matrix Rp = MatrixMultiply(MatrixRotateY(turnDeg * DEG2RAD), R);
        Vector3 c = Vector3Add(base, Vector3Transform({xx, 1.012f + lift, z}, R));
        // u runs toward the customer (the top of the page), v along the counter
        auto put = [&](float u, float v, float su, float sv, float h, Color col) {
            drawBox(Vector3Add(c, Vector3Transform({sg * u, h, v}, Rp)), {su, 0.0008f, sv}, col, Rp);
        };
        put(0, 0, len, w, 0, Color{247, 245, 238, 255});
        marks(put);
    };
    // the form: a black heading, ruled boxes, ink, a column of tick boxes, a signature
    lay(0.0, 0.08f, -0.28f, sg * 4.0f, 0.216f, 0.279f, [&](auto put) {
        const Color ink{40, 60, 160, 255}, blk{25, 25, 28, 255}, rule{150, 150, 155, 255}, fill{226, 232, 244, 255};
        put(0.118f, 0, 0.018f, 0.12f, 0.0006f, blk);                                // the title
        put(0.095f, 0, 0.008f, 0.19f, 0.0006f, blk);                                // Section A's bar
        for (int r = 0; r < 3; r++) { put(0.075f - r * 0.017f, 0, 0.014f, 0.19f, 0.0006f, fill); put(0.073f - r * 0.017f, -0.03f + 0.01f * r, 0.0025f, 0.08f, 0.0012f, ink); }
        put(0.02f, 0, 0.008f, 0.19f, 0.0006f, blk);                                 // Section B's bar
        for (int q = 0; q < 9; q++) {
            float u = 0.006f - q * 0.0095f;
            put(u, -0.03f, 0.0018f, 0.13f, 0.0006f, rule);
            put(u, 0.075f, 0.005f, 0.005f, 0.0009f, q == 0 ? blk : rule);
            put(u, 0.088f, 0.005f, 0.005f, 0.0009f, q == 0 ? rule : blk);
        }
        put(-0.092f, 0, 0.008f, 0.19f, 0.0006f, blk);                               // Section C
        put(-0.115f, -0.035f, 0.003f, 0.09f, 0.0012f, ink);                         // the signature
    });
    // the ID card, a moment later
    lay(0.7, 0.12f, 0.06f, -sg * 9.0f, 0.054f, 0.086f, [&](auto put) {
        put(0.035f, 0, 0.014f, 0.054f, 0.0006f, Color{50, 80, 150, 255});            // its coloured band
        put(-0.006f, -0.014f, 0.032f, 0.022f, 0.0008f, Color{150, 115, 90, 255});    // the photo
        for (int r = 0; r < 4; r++) put(0.012f - r * 0.008f, 0.012f, 0.002f, 0.022f, 0.0008f, Color{60, 60, 66, 255});
    });
}

void shopShadowCasters() {
    for (auto& [id, w] : gWalkers) shadowBody({(float)w.pos.x, (float)w.pos.y, (float)w.pos.z}, 1.72f);
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
    drawCounterPapers();
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
    const char* top = TextFormat("Day %d  %s%s    %s    %s    %s", C.day, gmap::clockText(gShopHour).c_str(), gShopHour >= 24 ? " (closed: end the day at the door)" : "", TextFormat("$%.0f", C.money), career::tierName(C.reputation), where.c_str());
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
    gShopHour = 8;
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
