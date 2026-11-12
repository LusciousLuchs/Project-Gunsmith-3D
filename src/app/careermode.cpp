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
#include <cstring>
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
std::string gSlot;   // the career being played: its folder under Career/
std::string careerRoot() { return userFolder() + "/Career"; }
std::string careerDir() { return careerRoot() + "/" + (gSlot.empty() ? std::string("My shop") : gSlot); }
std::string careerFile() { return careerDir() + "/career.json"; }
// before there were several careers, the one career lived straight in Career/: it becomes "My shop"
void migrateCareer() {
    std::error_code ec;
    std::string root = careerRoot();
    if (!fs::exists(root + "/career.json", ec)) return;
    std::string to = root + "/My shop";
    fs::create_directories(to, ec);
    if (!fs::exists(to + "/career.json", ec)) {
        fs::rename(root + "/career.json", to + "/career.json", ec);
        if (fs::exists(root + "/workshop.gsmap", ec)) fs::rename(root + "/workshop.gsmap", to + "/workshop.gsmap", ec);
    }
}
std::string lastSlot() {
    std::ifstream in(careerRoot() + "/last.txt");
    std::string name;
    std::getline(in, name);
    std::error_code ec;
    if (!name.empty() && fs::exists(careerRoot() + "/" + name + "/career.json", ec)) return name;
    auto all = careerSlots();
    return all.empty() ? std::string() : all.front().name;
}
void useSlot(const std::string& name) {
    gSlot = name;
    std::error_code ec;
    fs::create_directories(careerDir(), ec);
    std::ofstream(careerRoot() + "/last.txt") << name;
}
std::string slotName(const std::string& want) {   // a folder name from what the player typed (and not one in use)
    std::string n;
    for (char c : want) if (!std::strchr("/\\:*?\"<>|", c) && (unsigned char)c >= 32) n += c;
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    if (n.empty()) n = "My shop";
    std::string base = n;
    std::error_code ec;
    for (int k = 2; fs::exists(careerRoot() + "/" + n, ec); k++) n = base + " " + std::to_string(k);
    return n;
}

// the bench test in the background
struct Test {
    std::thread worker;
    std::atomic<int> done{0};
    std::atomic<bool> cancel{false}, finished{false};
    bool running = false;
    int target = 30, contract = -1;
    std::string gun;
    bool stalled = false;
    career::GunSheet sheet;
    std::mutex m;
} T;
const int TEST_ROUNDS = 30;
}  // namespace

std::vector<CareerSlot> careerSlots() {
    migrateCareer();
    std::vector<CareerSlot> out;
    std::error_code ec;
    if (!fs::exists(careerRoot(), ec)) return out;
    for (auto& e : fs::directory_iterator(careerRoot(), ec)) {
        if (!e.is_directory()) continue;
        fs::path f = e.path() / "career.json";
        if (!fs::exists(f, ec)) continue;
        CareerSlot c;
        c.name = e.path().filename().string();
        std::ifstream in(f); std::stringstream ss; ss << in.rdbuf();
        career::Career k;
        if (career::careerFromJson(ss.str(), k)) { c.day = k.day; c.money = k.money; c.reputation = k.reputation; }
        c.when = (long long)fs::last_write_time(f, ec).time_since_epoch().count();
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const CareerSlot& a, const CareerSlot& b) { return a.when > b.when; });
    return out;
}
std::string careerName() { return gSlot; }
bool careerExists() { return !careerSlots().empty(); }
void careerContinue(const std::string& name) {
    useSlot(name);
    enterCareer();
}
void careerBegin(const std::string& name, int city) {
    useSlot(slotName(name));
    careerStartNew();
    if (!CAREER.props.empty()) career::setLocation(CAREER, CAREER.props[0], city);
    careerSave();
    enterCareer();
}
bool careerDelete(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos || name == "." || name == "..") return false;
    std::error_code ec;
    fs::path p = fs::path(careerRoot()) / name;
    if (!fs::exists(p / "career.json", ec)) return false;
    fs::remove_all(p, ec);
    if (gSlot == name) gSlot.clear();
    return !ec;
}
std::vector<std::string> careerWorkshopCopies() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(careerDir() + "/Workshops", ec))
        if (e.path().extension() == ".gsmap") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}
std::string careerWorkshopCopyFile(const std::string& name) {
    std::string n;
    for (char c : name) if (!std::strchr("/\\:*?\"<>|", c) && (unsigned char)c >= 32) n += c;
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    if (n.empty()) n = "Copy";
    std::error_code ec;
    fs::create_directories(careerDir() + "/Workshops", ec);
    return careerDir() + "/Workshops/" + n + ".gsmap";
}
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
// each property's building is its own map: the first in workshop.gsmap (as before properties), the others property_<id>
std::string careerPropertyFile(int id) { return careerDir() + (id <= 1 ? std::string("/workshop.gsmap") : "/property_" + std::to_string(id) + ".gsmap"); }
std::string careerWorkshopFile() { return careerPropertyFile(CAREER.here); }
static void ensurePropertyMap(int id) {   // a property bought (or a new career's garage): its building, as it comes
    std::error_code ec;
    std::string f = careerPropertyFile(id);
    if (fs::exists(f, ec)) return;
    const career::Property* p = career::propertyById(CAREER, id);
    fs::create_directories(fs::path(f).parent_path(), ec);
    std::ofstream(f) << gmap::mapToJson(gmap::propertyShell(p ? p->kind : "garage"));
}
void careerNoteMap(const gmap::Map& m, int id) {
    career::Property* p = career::propertyById(CAREER, id);
    if (!p) return;
    p->stations = gmap::workshopStations(m);
    p->rating = gmap::storeRating(m);
    p->shown.clear();
    for (auto& e : m.ents) if ((e.type == "display_case" || e.type == "display_rack") && !e.ref.empty() && m.store.has(e.pos, 0.3)) p->shown.push_back(e.ref);
    career::sumStations(CAREER);
}
void careerCountStations() {
    career::ensureProperties(CAREER);
    for (auto& p : CAREER.props) {
        std::ifstream in(careerPropertyFile(p.id));
        if (!in) continue;   // (not built yet: as it comes)
        std::stringstream ss; ss << in.rdbuf();
        gmap::Map m;
        if (gmap::mapFromJson(ss.str(), m)) careerNoteMap(m, p.id);
    }
    career::sumStations(CAREER);
}
void careerGoTo(int id) {
    if (!career::propertyById(CAREER, id) || id == CAREER.here) return;
    if (shopActive()) { meSaveQuiet(); careerNoteMap(ME.map, CAREER.here); }
    CAREER.here = id;
    careerSave();
    careerOpenWorkshop(true);
}
void careerOpenWorkshop(bool walk) {
    career::ensureProperties(CAREER);
    ensurePropertyMap(CAREER.here);
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
    migrateCareer();
    if (gSlot.empty()) { std::string l = lastSlot(); useSlot(l.empty() ? slotName("My shop") : l); }
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
    o.stallSeconds = 10;   // (a gun that can't fire and reload in 10 s fails the test instead of testing forever)
    T.worker = std::thread([d, o, base]() {
        sim::ReliabilityReport r = sim::runReliability(d, o, &T.done, &T.cancel);
        std::lock_guard<std::mutex> lk(T.m);
        T.sheet = base;
        T.sheet.rated = !r.gaveUp || r.fired > 0;
        T.sheet.jamRate = r.gaveUp && r.fired == 0 ? 100 : r.jamRate();
        if (r.gaveUp && r.fired < o.rounds) T.sheet.jamRate = std::max(T.sheet.jamRate, 100.0 * (o.rounds - r.fired) / o.rounds);   // (stuck for good)
        T.sheet.fired = r.fired;
        T.sheet.manual = r.manual;
        T.stalled = r.gaveUp && r.note.rfind("Gave up", 0) == 0;
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
        if (T.stalled) std::snprintf(b, sizeof b, "The %s couldn't fire and reload within 10 seconds on the bench: rated on the %d round%s it fired.", T.gun.c_str(), s.fired, s.fired == 1 ? "" : "s");
        else std::snprintf(b, sizeof b, "Rated the %s: %.1f%% jams over %d rounds, worth about $%.0f.", T.gun.c_str(), s.jamRate, s.fired, s.value());
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
CharLook lookFor(int id, const std::string& name) {
    // every draw from a well-mixed hash of the number (neighbouring numbers used to come out alike: long runs of one
    // body, skin tones that hardly changed from one customer to the next)
    unsigned long long st = (unsigned long long)(unsigned)id * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    auto next = [&](int n) {
        unsigned long long z = (st += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; z ^= z >> 31;
        return (int)(z % (unsigned long long)n);
    };
    // skin, light to deep, evenly likely
    static const char* skins[] = {"#f3dccb", "#ebc9ae", "#e2b896", "#d9a982", "#c99470", "#b8835e", "#a06c48", "#865636", "#6b4229", "#4e301e"};
    static const char* hairDark[] = {"#121212", "#1e1612", "#2a1d14", "#3b2a1e"};
    static const char* hairAny[] = {"#2a1d14", "#3b2a1e", "#6b4a2b", "#a8834a", "#d2b57a", "#7a2f1a", "#121212"};
    static const char* grey = "#c9c2b5";
    static const char* eyes[] = {"#3b2a1e", "#4a3424", "#5a4a2a", "#4a6a8a", "#5a7a5a"};
    static const char* cloth[] = {"#4f5d52", "#3d4a63", "#6b2f2f", "#8a7a5a", "#2f3033", "#5e6b75", "#a0563a", "#c9b994", "#2e4a3a", "#56406a"};
    static const char* tops[] = {"tshirt", "longsleeve", "tank"};
    CharLook L; L.on = true;
    std::string b = career::bodyForName(name);
    L.body = !b.empty() ? b : (next(2) ? "male" : "female");
    bool man = L.body == "male";
    L.height = 0.9 + next(25) / 100.0; L.build = 0.85 + next(40) / 100.0;
    int tone = next(10);
    L.skin = skins[tone];
    L.eyes = tone >= 5 ? eyes[next(3)] : eyes[next(5)];
    static const char* menHair[] = {"none", "buzz", "buzz", "short", "short", "short", "long", "ponytail"};
    static const char* womenHair[] = {"short", "long", "long", "ponytail", "ponytail", "bun", "bun", "buzz"};
    L.hair = man ? menHair[next(8)] : womenHair[next(8)];
    L.hairColor = next(9) == 0 ? grey : tone >= 5 ? hairDark[next(4)] : hairAny[next(7)];
    L.top = tops[next(3)]; L.topColor = cloth[next(10)]; L.pants = next(5) ? "long" : "shorts"; L.pantsColor = cloth[next(10)];
    L.belt = next(2); L.hat = next(4) == 0; L.headphones = false; L.glasses = next(4) == 0;
    L.hatColor = cloth[next(10)];
    return L;
}
}  // namespace
void careerRenderPortraits() {
    for (auto& w : CAREER.walkins) {
        if (gPortraits.count(w.id)) continue;
        RenderTexture2D rt = LoadRenderTexture(256, 320);
        NpcDef n = characterFromLook(lookFor(w.id, w.name));
        npcRelaxedStance(n, (unsigned)w.id);
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
const NpcDef& careerPerson(int id, const std::string& name) {
    static std::map<int, NpcDef> people;
    auto it = people.find(id);
    if (it != people.end()) return it->second;
    if (people.size() > 200) people.clear();
    std::string nm = name;
    if (nm.empty()) for (auto& w : CAREER.walkins) if (w.id == id) nm = w.name;
    NpcDef p = characterFromLook(lookFor(id, nm));
    npcRelaxedStance(p, (unsigned)id);
    return people[id] = p;
}
Texture2D careerPortrait(int id) { auto it = gPortraits.find(id); return it == gPortraits.end() ? Texture2D{} : it->second.texture; }

