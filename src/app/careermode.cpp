// Gunsmith3D - Career mode in the app (see careermode.hpp).
#include "app/careermode.hpp"
#include "app/app.hpp"
#include "app/paths.hpp"
#include "reliability.hpp"

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
void enterCareer() {
    if (!careerLoad()) careerStartNew();
    gIn = true;
    A.screen = Screen::Career;
    A.showPause = A.showSettings = A.showBenchPicker = A.showRangePicker = false;
    EnableCursor();
}
bool inCareer() { return gIn; }
void careerLeave() { gIn = false; }
void careerBackToOffice() {
    backToMenu();          // (saves the design, leaves the bench)
    gIn = true;
    A.screen = Screen::Career;
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
