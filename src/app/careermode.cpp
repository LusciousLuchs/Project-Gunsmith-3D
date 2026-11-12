// Gunsmith3D - Career mode in the app (see careermode.hpp).
#include "app/careermode.hpp"
#include <cstdlib>
#include "app/app.hpp"
#include "app/paths.hpp"
#include "app/mapedit.hpp"
#include "app/shop3d.hpp"
#include "app/range.hpp"
#include "app/mapgfx.hpp"
#include "map.hpp"
#include "reliability.hpp"
#include "npc.hpp"
#include "app/npcedit.hpp"
#include "app/gfx.hpp"
#include "rlgl.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
career::Career CAREER;

namespace {
bool gIn = false;   // in career mode
std::string careerFile() { return userFolder() + "/Career/career.json"; }

// the bench test in the background
struct Test {
    std::thread worker;
    std::atomic<int> done{0};
    std::atomic<bool> cancel{false}, finished{false};
    bool running = false;
    int target = 30, contract = -1;
    std::string gun;
    career::GunSheet sheet;
    std::mutex m;
} T;
const int TEST_ROUNDS = 30;
}  // namespace

bool careerExists() { std::error_code ec; return fs::exists(careerFile(), ec); }
void careerSave() {
    std::error_code ec;
    fs::create_directories(fs::path(careerFile()).parent_path(), ec);
    std::ofstream(careerFile()) << career::careerToJson(CAREER);
}
bool careerLoad() {
    std::ifstream in(careerFile());
    if (!in) return false;
    std::stringstream ss; ss << in.rdbuf();
    return career::careerFromJson(ss.str(), CAREER);
}
void careerStartNew() {
    CAREER = career::newCareer((unsigned)(GetTime() * 1000) ^ 0x5bd1e995u);
    careerSave();
}
std::string careerWorkshopFile() { return userFolder() + "/Career/workshop.gsmap"; }
void careerCountStations() {
    std::ifstream in(careerWorkshopFile());
    if (!in) return;   // (not built yet: the career's own start, a bench and a counter)
    std::stringstream ss; ss << in.rdbuf();
    gmap::Map m;
    if (gmap::mapFromJson(ss.str(), m)) CAREER.stations = gmap::workshopStations(m);
}
void careerOpenWorkshop(bool walk) {
    enterCareerWorkshop(careerWorkshopFile(), walk);
    gIn = true;
    shopEnter();
}
void careerWorkshopToOffice() {
    leaveCareerWorkshop();
    mapRestoreLook();
    careerCountStations();
    careerSave();
    gIn = true;
    A.screen = Screen::Career;
    EnableCursor();
}
namespace { bool gBackToOffice = false; }
void careerDemoStart(int id) {
    for (auto& k : CAREER.contracts) {
        if (k.id != id || k.state != 4) continue;
        const CareerGun* g = careerGun(k.delivered);
        if (!g) { toast("That gun's blueprint is gone."); return; }
        std::string gun = k.delivered, client = k.client;
        enterRange(g->d, gmap::builtinRange());
        rangeDemoStart(client, career::DEMO_HITS, career::DEMO_SHOTS, [id, gun](int hits, int shots) {
            career::demoResult(CAREER, id, gun, hits, shots);
            for (auto& c : CAREER.contracts) if (c.id == id) toast(c.verdict);
            careerSave();
            gBackToOffice = true;
        });
        gIn = true;
        return;
    }
}
void careerTick() {
    if (A.screen == Screen::Range && rangeDemoOver()) backToMenu();   // (the course is shot: the result goes in as you leave)
    if (gBackToOffice && A.screen == Screen::Menu) { gBackToOffice = false; careerOpenWorkshop(true); }   // (back in your shop)
}
void enterCareer() {
    if (!careerLoad()) careerStartNew();
    careerCountStations();
    gIn = true;
    A.showPause = A.showSettings = A.showBenchPicker = A.showRangePicker = false;
    if (std::getenv("GUNSMITH_CAREERTAB")) { A.screen = Screen::Career; EnableCursor(); return; }   // (screenshots: the screens on their own)
    careerOpenWorkshop(true);   // you start in your shop, on foot
}
bool inCareer() { return gIn; }
void careerLeave() { gIn = false; }
void careerBackToOffice() {
    backToMenu();          // (saves the design, leaves the bench)
    careerOpenWorkshop(true);   // back in your shop
}

// ---------------------------------------------------------------- your guns
const std::vector<CareerGun>& careerGuns() {
    static std::vector<CareerGun> guns;
    static std::map<std::string, fs::file_time_type> seen;
    static double checked = -10;
    if (GetTime() - checked < 1.5) return guns;
    checked = GetTime();
    std::error_code ec;
    std::vector<CareerGun> now;
    std::map<std::string, fs::file_time_type> times;
    for (auto& e : fs::directory_iterator(GUNS_DIR, ec)) {
        if (e.path().extension() != ".json") continue;
        std::string name = e.path().stem().string();
        auto t = fs::last_write_time(e.path(), ec);
        times[name] = t;
        const CareerGun* old = nullptr;
        for (auto& g : guns) if (g.name == name) old = &g;
        if (old && seen.count(name) && seen[name] == t) { now.push_back(*old); continue; }
        std::ifstream in(e.path());
        std::stringstream ss; ss << in.rdbuf();
        CareerGun g; g.name = name;
        if (!designFromJson(ss.str(), g.d) || g.d.attach.on || g.d.npcGun.on || !g.d.breech.on) continue;   // (guns only: not attachments, not NPC guns)
        g.fresh = career::sheetOf(g.d);
        g.fresh.name = name;
        now.push_back(std::move(g));
    }
    std::sort(now.begin(), now.end(), [](const CareerGun& a, const CareerGun& b) { return a.name < b.name; });
    guns = std::move(now); seen = std::move(times);
    return guns;
}
const CareerGun* careerGun(const std::string& name) {
    for (auto& g : careerGuns()) if (g.name == name) return &g;
    return nullptr;
}

// ---------------------------------------------------------------- the bench test
void careerTestStart(const std::string& gun, int contractId) {
    if (T.running) return;
    const CareerGun* g = careerGun(gun);
    if (!g) return;
    if (T.worker.joinable()) T.worker.join();
    T.cancel = false; T.finished = false; T.done = 0; T.running = true;
    T.gun = gun; T.contract = contractId; T.target = TEST_ROUNDS;
    Design d = g->d;
    career::GunSheet base = g->fresh;
    sim::ReliabilityOptions o;
    o.rounds = TEST_ROUNDS;
    o.seed = (unsigned)(CAREER.day * 7919 + contractId * 31 + 1);   // (each test day its own luck)
    T.worker = std::thread([d, o, base]() {
        sim::ReliabilityReport r = sim::runReliability(d, o, &T.done, &T.cancel);
        std::lock_guard<std::mutex> lk(T.m);
        T.sheet = base;
        T.sheet.rated = !r.gaveUp || r.fired > 0;
        T.sheet.jamRate = r.gaveUp && r.fired == 0 ? 100 : r.jamRate();
        if (r.gaveUp && r.fired < o.rounds) T.sheet.jamRate = std::max(T.sheet.jamRate, 100.0 * (o.rounds - r.fired) / o.rounds);   // (stuck for good)
        T.sheet.fired = r.fired;
        T.sheet.manual = r.manual;
        T.finished = true;
    });
}
bool careerTestRunning(float* progress, std::string* gun) {
    if (progress) *progress = T.target > 0 ? (float)T.done / T.target : 0;
    if (gun) *gun = T.gun;
    return T.running;
}
void careerTestPoll() {
    if (!T.running || !T.finished) return;
    if (T.worker.joinable()) T.worker.join();
    T.running = false;
    career::GunSheet s;
    { std::lock_guard<std::mutex> lk(T.m); s = T.sheet; }
    if (T.contract < 0) {
        CAREER.sheets[T.gun] = s;
        char b[160];
        std::snprintf(b, sizeof b, "Rated the %s: %.1f%% jams over %d rounds, worth about $%.0f.", T.gun.c_str(), s.jamRate, s.fired, s.value());
        toast(b);
    } else {
        bool paid = career::deliver(CAREER, T.contract, T.gun, s);
        for (auto& k : CAREER.contracts) if (k.id == T.contract) toast(k.verdict);
        (void)paid;
    }
    careerSave();
}
void careerShutdown() {
    T.cancel = true;
    if (T.worker.joinable()) T.worker.join();
}

// ---------------------------------------------------------------- customers' faces
namespace {
std::map<int, RenderTexture2D> gPortraits;
CharLook lookFor(int id) {
    unsigned r = (unsigned)id * 2654435761u + 12345u;
    auto next = [&](int n) { r = r * 1664525u + 1013904223u; return (int)((r >> 8) % (unsigned)n); };
    static const char* skins[] = {"#f1d3bd", "#e0b394", "#c8a088", "#a5785a", "#7a5238", "#4e3424"};
    static const char* hairC[] = {"#2a1d14", "#3b2a1e", "#6b4a2b", "#a8834a", "#c9c2b5", "#121212", "#7a2f1a"};
    static const char* hairs[] = {"none", "buzz", "short", "short", "long", "ponytail", "bun"};
    static const char* cloth[] = {"#4f5d52", "#3d4a63", "#6b2f2f", "#8a7a5a", "#2f3033", "#5e6b75", "#a0563a", "#c9b994"};
    static const char* tops[] = {"tshirt", "longsleeve", "tank"};
    CharLook L; L.on = true;
    L.body = next(2) ? "male" : "female";
    L.height = 0.9 + next(25) / 100.0; L.build = 0.85 + next(40) / 100.0;
    L.skin = skins[next(6)]; L.hair = hairs[next(7)]; L.hairColor = hairC[next(7)];
    L.top = tops[next(3)]; L.topColor = cloth[next(8)]; L.pants = next(5) ? "long" : "shorts"; L.pantsColor = cloth[next(8)];
    L.belt = next(2); L.hat = next(4) == 0; L.headphones = false; L.glasses = next(4) == 0;
    L.hatColor = cloth[next(8)];
    return L;
}
}  // namespace
void careerRenderPortraits() {
    for (auto& w : CAREER.walkins) {
        if (gPortraits.count(w.id)) continue;
        RenderTexture2D rt = LoadRenderTexture(256, 320);
        NpcDef n = characterFromLook(lookFor(w.id));
        double h = npcHeight(n) / 1000;
        Camera3D cam{};
        cam.position = {0, (float)(h * 0.86), 1.25f}; cam.target = {0, (float)(h * 0.8), 0}; cam.up = {0, 1, 0}; cam.fovy = 34; cam.projection = CAMERA_PERSPECTIVE;
        mapRestoreLook();   // (the shop's own light, not a dark map's)
        BeginTextureMode(rt);
        ClearBackground(Color{58, 50, 44, 255});   // (the shop's back wall)
        BeginMode3D(cam);
        {   // a light at the counter, in front of them and a little to one side, so their face shows
            Lamp key; key.pos[0] = 0.5f; key.pos[1] = (float)(h * 0.95); key.pos[2] = 1.1f; key.col[0] = 1; key.col[1] = 0.93f; key.col[2] = 0.82f;
            key.power = 1.6f; key.range = 4;
            setLamps({key});
        }
        { float vp[3] = {cam.position.x, cam.position.y, cam.position.z}; SetShaderValue(G.lit, G.locView, vp, SHADER_UNIFORM_VEC3); }   // (lit from where it's seen)
        drawNpcAt(n, {0, 0, 0}, 90, 0.001f);
        EndMode3D();
        EndTextureMode();
        gPortraits[w.id] = rt;
    }
    if (gPortraits.size() > 24)   // (forget faces nobody's showing)
        for (auto it = gPortraits.begin(); it != gPortraits.end();) {
            bool here = false; for (auto& w : CAREER.walkins) if (w.id == it->first) here = true;
            if (!here) { UnloadRenderTexture(it->second); it = gPortraits.erase(it); } else ++it;
        }
}
const NpcDef& careerPerson(int id) {
    static std::map<int, NpcDef> people;
    auto it = people.find(id);
    if (it != people.end()) return it->second;
    if (people.size() > 200) people.clear();
    return people[id] = characterFromLook(lookFor(id));
}
Texture2D careerPortrait(int id) { auto it = gPortraits.find(id); return it == gPortraits.end() ? Texture2D{} : it->second.texture; }

