// Gunsmith3D - Career mode, stage 1 (see career.hpp).
#include "career.hpp"
#include "solver.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using json = nlohmann::json;

namespace career {

namespace {
double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
unsigned nextRand(unsigned& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
double rnd(Career& c) { return (nextRand(c.rng) & 0xFFFFFF) / double(0x1000000); }   // 0 .. 1
int rndInt(Career& c, int lo, int hi) { return lo + (int)(rnd(c) * (hi - lo + 1) * 0.99999); }
template <class T> const T& pick(Career& c, const std::vector<T>& v) { return v[std::min(v.size() - 1, (size_t)(rnd(c) * v.size()))]; }
std::string money(double v) { char b[32]; std::snprintf(b, sizeof b, "$%.0f", v); return b; }
// what a gun of a round this strong sells for, before its quality (pistols a few hundred, rifles about a thousand)
double baseValue(double energy, bool twoHands, int capacity) {
    return 250 + 120 * std::log2(1 + std::max(0.0, energy) / 100) + (twoHands ? 250 : 0) + 4 * std::min(capacity, 30);
}
}  // namespace

double GunSheet::quality() const {
    double rel = rated ? clamp01(1 - jamRate / 8) : 0.5;                    // 8% jams and up: worthless as a working gun
    double acc = moa > 0 ? clamp01((8 - moa) / 7) : 0.7;                   // (a shotgun's pattern: fair)
    double kick = twoHands ? clamp01(1 - (recoil - 2.5) / 6) : clamp01(1 - (recoil - 3) / 8);
    // (weights as drawn: each part's material, steel unless chosen, so older long guns come out heavy)
    double heft = twoHands ? clamp01(1 - (massKg - 7) / 5) : clamp01(1 - (massKg - 1.1) / 1.2);
    return 0.55 * rel + 0.2 * acc + 0.15 * kick + 0.1 * heft;
}
double GunSheet::value() const { return baseValue(energy, twoHands, capacity) * (0.35 + 0.9 * quality()); }

GunSheet sheetOf(const Design& d) {
    GunSheet s;
    s.name = d.name;
    const auto& br = d.breech;
    const Cartridge& cart = br.cart;
    s.caliber = cart.name;
    s.energy = cart.energy();
    s.velocity = cart.velocity;
    double grams = 0, carves = 0, turned = 0, rifled = 0, cost = 0, shaping = 0;
    for (auto& b : d.bodies) {
        if (b.magazine && !b.magFixed) { s.capacity = std::max(s.capacity, b.magCap); continue; }   // (magazines come with it, cheaply)
        if (b.magazine) s.capacity = std::max(s.capacity, b.magCap);
        const MaterialInfo& mt = materialOf(b.material);
        double g = b.mass > 0 ? b.mass : std::fabs(sim::polyArea(b.pts)) * b.thick * mt.density * 1e-3;   // (its material, unless its weight is set)
        grams += g;
        cost += g / 1000 * mt.costPerKg;
        shaping += 0.2 * mt.hoursK;
        s.parts++;
        carves += b.carves.size();
        if (b.round) turned++;
        if (b.barrel.on && b.barrel.rifled) rifled++;
    }
    int springs = 0;
    for (auto& j : d.joints) if (j.type == "spring") springs++;
    s.massKg = grams / 1000;
    s.recoil = cart.impulse() / std::max(0.2, s.massKg);
    std::string held = heldWith(d);
    s.posed = !held.empty();
    s.twoHands = held == "two hands";
    if (br.on && br.hasMuzzle) s.barrelMm = std::hypot(br.muzzle.x - br.face.x, br.muzzle.y - br.face.y);
    // groups: a longer barrel holds tighter (a sight radius, a steadier bullet); a bullet spun too slowly opens them up
    if (cart.shot()) s.moa = 0;
    else {
        s.moa = 1.0 + 300 / std::max(50.0, s.barrelMm);
        for (auto& b : d.bodies)
            if (b.barrel.on) {
                double sg = spinStability(cart, b.barrel.twist, b.barrel.rifled);
                if (sg < 1.0) s.moa *= 5; else if (sg < 1.4) s.moa *= 1.6;
                break;
            }
    }
    // to make one: every part shaped and fitted, cuts and turned parts take longer, springs wound, a barrel rifled
    s.buildHours = 0.5 + shaping + 0.3 * carves + 0.3 * turned + 0.15 * springs + 0.5 * rifled;   // (shaping: 0.2 h a steel part)
    s.materialCost = 3 * cost + 2 * s.parts + 10;   // (stock bought in, with waste)
    return s;
}

std::vector<std::string> shortfalls(const Contract& c, const GunSheet& s) {
    std::vector<std::string> out;
    char b[160];
    if (c.minEnergy > 0 && s.energy < c.minEnergy) { std::snprintf(b, sizeof b, "its round is too weak (%.0f J, they want %.0f J or more)", s.energy, c.minEnergy); out.push_back(b); }
    if (c.maxEnergy > 0 && s.energy > c.maxEnergy) { std::snprintf(b, sizeof b, "its round is too strong (%.0f J, they want %.0f J at most)", s.energy, c.maxEnergy); out.push_back(b); }
    if (c.hands && !s.posed) out.push_back("how it's held isn't set (Test bench > Pose: place the firing hand)");
    else if (c.hands == 1 && s.twoHands) out.push_back("it's held in two hands; they want a one-handed gun");
    if (c.hands == 2 && s.posed && !s.twoHands) out.push_back("it's one-handed; they want a long gun");
    if (c.maxKg > 0 && s.massKg > c.maxKg) { std::snprintf(b, sizeof b, "too heavy (%.2f kg, at most %.2f)", s.massKg, c.maxKg); out.push_back(b); }
    if (c.minCapacity > 0 && s.capacity < c.minCapacity) { std::snprintf(b, sizeof b, "holds too few (%d, they want %d)", s.capacity, c.minCapacity); out.push_back(b); }
    if (c.maxMoa > 0 && (s.moa <= 0 || s.moa > c.maxMoa)) { std::snprintf(b, sizeof b, "not accurate enough (%.1f MOA, they want %.1f)", s.moa, c.maxMoa); out.push_back(b); }
    if (c.maxJam > 0 && (!s.rated || s.jamRate > c.maxJam)) { std::snprintf(b, sizeof b, "not reliable enough (%.1f%% jams, at most %.1f%%)", s.jamRate, c.maxJam); out.push_back(b); }
    return out;
}

bool startBuild(Career& c, const std::string& gun, int qty, std::string* why) {
    auto it = c.sheets.find(gun);
    if (it == c.sheets.end()) { if (why) *why = "rate it first"; return false; }
    if (qty < 1) { if (why) *why = "nothing to make"; return false; }
    double cost = it->second.materialCost * qty;
    if (cost > c.money) { if (why) *why = "not enough money for the material (" + money(cost) + ")"; return false; }
    c.money -= cost;
    if (!c.builds.empty() && c.builds.back().gun == gun) c.builds.back().qty += qty;
    else c.builds.push_back({gun, qty, 0});
    return true;
}

namespace {
const std::vector<std::string> FIRST = {"Dale", "Marisol", "Hank", "Priya", "Jo", "Wendell", "Tamsin", "Earl", "Bea", "Luis", "Corinne", "Rusty",
                                        "Aiyana", "Gus", "Nadia", "Floyd", "Yvette", "Otis", "Kim", "Delbert"};
const std::vector<std::string> PLACE = {"Amarillo, TX", "Boise, ID", "Dothan, AL", "Bangor, ME", "Casper, WY", "Flagstaff, AZ", "Duluth, MN",
                                        "Tulsa, OK", "Macon, GA", "Billings, MT", "Erie, PA", "Spokane, WA", "Paducah, KY", "Ely, NV"};
std::string buyer(Career& c) { return pick(c, FIRST) + " " + std::string(1, (char)('A' + rndInt(c, 0, 25))) + ". (" + pick(c, PLACE) + ")"; }

std::string reviewText(Career& c, const GunSheet& s, int stars, double price) {
    std::vector<std::string> bits;
    // what it did in their hands: the bench's jam rate, as a few hundred rounds would show it
    int rounds = 150 + rndInt(c, 0, 350), jams = 0;
    for (int i = 0; i < rounds; i++) if (rnd(c) * 100 < s.jamRate) jams++;
    char b[200];
    if (s.manual) std::snprintf(b, sizeof b, "Worked the action through %d rounds%s.", rounds, jams == 0 ? " and it never missed a beat" : jams < 3 ? ", a couple of hiccups" : " and it kept choking");
    else if (jams == 0) {
        static const char* clean[] = {"Ran %d rounds without a single stoppage.", "%d rounds of cheap range ammo, zero malfunctions.", "Put %d through it this weekend. Not one hiccup.",
                                      "Fed everything I tried, %d rounds and counting.", "%d rounds in and it hasn't missed once."};
        std::snprintf(b, sizeof b, clean[rndInt(c, 0, 4)], rounds);
    }
    else if (jams < 3) std::snprintf(b, sizeof b, "%d stoppage%s in %d rounds. Not bad.", jams, jams == 1 ? "" : "s", rounds);
    else if (jams < 10) std::snprintf(b, sizeof b, "Jammed %d times in %d rounds. Needs work.", jams, rounds);
    else std::snprintf(b, sizeof b, "Couldn't get through a magazine without a jam (%d in %d rounds).", jams, rounds);
    bits.push_back(b);
    if (rnd(c) < 0.4) bits.push_back(pick(c, std::vector<std::string>{"Fit and finish are clean.", "Trigger breaks nice and crisp.", "Took it to the range with my dad, he wants one now.",
                                                                       "Shipped fast, well packed.", "Sights were right on out of the box.", "Feels good in the hand.", "Took a while to break in."}));
    if (s.moa > 0 && s.moa < 2) bits.push_back(pick(c, std::vector<std::string>{"Groups like a laser.", "Tight little groups at 100.", "More accurate than I am."}));
    else if (s.moa > 5) bits.push_back(pick(c, std::vector<std::string>{"Accuracy is minute-of-barn.", "Couldn't keep it on paper past 15 yards.", "Spreads them all over."}));
    double kickLimit = s.twoHands ? 5 : 6;
    if (s.recoil > kickLimit) bits.push_back(pick(c, std::vector<std::string>{"Kicks like a mule.", "My wrist is still sore.", "Recoil is brutal."}));
    else if (s.recoil < kickLimit * 0.5) bits.push_back(pick(c, std::vector<std::string>{"Soft shooter.", "Barely moves when it fires.", "Gentle on the hands."}));
    if (!s.twoHands && s.massKg > 1.4) bits.push_back("Heavy to carry all day.");
    if (s.twoHands && s.massKg > 10) bits.push_back("A boat anchor.");
    double vfm = s.value() / std::max(1.0, price);
    if (vfm > 1.25) bits.push_back("A steal at this price.");
    else if (vfm < 0.8) bits.push_back("Overpriced for what it is.");
    std::string open = stars >= 5 ? pick(c, std::vector<std::string>{"Love it.", "Outstanding work.", "Buying another."})
                     : stars == 4 ? pick(c, std::vector<std::string>{"Solid gun.", "Happy with it.", "Good piece."})
                     : stars == 3 ? pick(c, std::vector<std::string>{"It's okay.", "Mixed feelings.", "Does the job, mostly."})
                     : stars == 2 ? pick(c, std::vector<std::string>{"Disappointed.", "Not great.", "Expected better."})
                     : pick(c, std::vector<std::string>{"Avoid.", "Sent it back.", "What a mess."});
    std::string t = open;
    for (auto& x : bits) t += " " + x;
    return t;
}

// a customer's brief, by what kind of order it is; bigger orders once you're better known
Contract offer(Career& c) {
    Contract k;
    k.id = c.nextId++;
    struct Tpl { const char* title; const char* brief; double minE, maxE, maxJam, maxKg; int minCap, hands; double maxMoa; int qMin, qMax; double rep; bool demo = false; };
    static const std::vector<Tpl> T = {
        {"A pocket pistol", "Something small and light I can carry every day. It has to work every time.", 150, 700, 3, 1.0, 6, 1, 0, 1, 1, 0},
        {"A deer rifle", "A rifle for whitetail out to 200 yards. Accuracy matters more than anything.", 1800, 0, 6, 0, 0, 2, 2.5, 1, 1, 0},
        {"A ranch carbine", "Something handy for coyotes and the truck. Light, holds a few.", 500, 2500, 5, 6.5, 5, 2, 0, 1, 1, 0},
        {"A competition pistol", "For steel matches: accurate, soft-shooting, and it must not jam.", 300, 800, 1.5, 0, 10, 1, 4, 1, 1, 0},
        {"Something cursed", "I run a YouTube channel. Build me the weirdest thing you can that still goes bang now and then.", 0, 0, 30, 0, 0, 0, 0, 1, 1, 0},
        {"A big-bore revolver substitute", "I want a handgun that hits like a truck. Weight and kick don't scare me.", 1000, 0, 8, 0, 0, 1, 0, 1, 1, 0},
        {"Guns for a security firm", "Sidearms for our guards: reliable above all, enough rounds.", 300, 800, 2, 1.3, 10, 1, 0, 3, 5, 15},
        {"Rifles for a hunting lodge", "Loaner rifles for our guests. Accurate and dependable.", 1800, 0, 4, 9, 3, 2, 3, 3, 6, 25},
        // big orders: once you're known. They test it on the bench, then want to see it shot on the range.
        {"Sidearms for the county sheriff", "Duty pistols for our deputies. Dependable, enough rounds, not too heavy. We'll want to see it shot.", 300, 800, 1.5, 1.3, 12, 1, 0, 10, 20, 35, true},
        {"Patrol rifles for the state police", "Rifles for our troopers' cars: accurate and reliable. A range demonstration first.", 1200, 3500, 2, 0, 20, 2, 3, 8, 15, 45, true},
        {"Service pistols for the Army", "A sidearm for the troops. It has to run, every time. Bench test and a demonstration.", 300, 800, 1, 1.2, 15, 1, 0, 40, 80, 60, true},
        {"Service rifles for the Marines", "A rifle for the infantry: accurate, tough, holds plenty. Bench test and a demonstration.", 1200, 3500, 1, 0, 20, 2, 2.5, 50, 120, 75, true},
    };
    std::vector<const Tpl*> open;
    for (auto& t : T) if (c.reputation >= t.rep) open.push_back(&t);
    const Tpl& t = *open[std::min(open.size() - 1, (size_t)(rnd(c) * open.size()))];
    k.title = t.title; k.brief = t.brief;
    k.minEnergy = t.minE; k.maxEnergy = t.maxE; k.maxJam = t.maxJam; k.maxKg = t.maxKg; k.minCapacity = t.minCap; k.hands = t.hands; k.maxMoa = t.maxMoa;
    k.qty = rndInt(c, t.qMin, t.qMax);
    k.demo = t.demo;
    k.client = t.qMax > 1 ? pick(c, std::vector<std::string>{"Ironwood Security", "Coldwater Lodge", "Redline Protection", "Big Sky Outfitters"}) : buyer(c);
    double e = t.maxE > 0 ? (t.minE + t.maxE) / 2 : std::max(t.minE, 600.0);
    double each = k.qty >= 10 ? baseValue(e, t.hands == 2, t.minCap) * (1.05 + 0.25 * rnd(c))   // (volume: a little over the shelf price)
                              : baseValue(e, t.hands == 2, t.minCap) * (1.25 + 0.4 * rnd(c)) + 150;   // bespoke work pays better than the shelf
    k.pay = std::round(each * k.qty / 10) * 10;
    k.dueDay = c.day + rndInt(c, 6, 12) + (k.qty >= 10 ? k.qty / 2 + 10 : 2 * k.qty);
    return k;
}
}  // namespace

// ---------------------------------------------------------------- stage 2: your shop and your people
int shopStations(int tier) { static const int n[] = {3, 6, 10, 16}; return n[std::max(0, std::min(3, tier))]; }
double shopUpgradeCost(int tier) { static const double m[] = {12000, 35000, 90000, 0}; return m[std::max(0, std::min(3, tier))]; }
const char* shopName(int tier) { static const char* n[] = {"A garage", "A small shop", "A workshop", "A factory floor"}; return n[std::max(0, std::min(3, tier))]; }
const char* tierName(double r) {
    return r >= 80 ? "Defense supplier" : r >= 60 ? "National brand" : r >= 35 ? "Regional maker" : r >= 15 ? "Local gunsmith" : "Garage gunsmith";
}
namespace {
// who works where today: each employee at a station that suits them (or a bench, slower), as far as your building allows
struct Crew { double hours = 0, flaw = 0, qc = 0; std::vector<int> idle; };
Crew crewToday(const Career& c) {
    Crew w;
    auto get = [&](const char* k) { auto it = c.stations.find(k); return it == c.stations.end() ? 0 : it->second; };
    int bench = get("bench"), lathe = get("lathe"), mill = get("mill"), qc = get("qc");
    int room = shopStations(c.shopTier);   // (stations past what your building holds don't count)
    auto cut = [&](int& n) { int k = std::min(n, room); room -= k; n = k; };
    cut(bench); cut(lathe); cut(mill); cut(qc);
    double flawHours = 0;
    // you: your own day, at a bench if there is one
    if (bench > 0) bench--;
    w.hours = c.hoursPerDay; flawHours += c.hoursPerDay * 0.03;
    int machines = lathe + mill;
    for (size_t i = 0; i < c.staff.size(); i++) {
        const Employee& e = c.staff[i];
        double eff = 0;
        if (e.specialty == "machinist") { if (machines > 0) { machines--; eff = 1.15; } else if (bench > 0) { bench--; eff = 0.8; } }
        else if (e.specialty == "finisher") {
            if (qc > 0) { qc--; eff = 0.6; w.qc = std::max(w.qc, 0.5 + 0.45 * e.skill); }   // (checking copies takes most of their day)
            else if (bench > 0) { bench--; eff = 0.7; }
        } else { if (bench > 0) { bench--; eff = 1.1; } else if (machines > 0) { machines--; eff = 0.8; } }
        if (eff <= 0) { w.idle.push_back((int)i); continue; }
        double h = 8 * (0.55 + 0.6 * e.skill) * eff;
        w.hours += h; flawHours += h * (1 - e.skill) * 0.2;
    }
    w.flaw = w.hours > 0 ? flawHours / w.hours : 0;
    return w;
}
}  // namespace
double workHours(const Career& c) { return crewToday(c).hours; }
void refreshCandidates(Career& c) {
    static const std::vector<std::string> LAST = {"Hensley", "Okafor", "Brandt", "Castillo", "Mbeki", "Larsen", "Duval", "Petrov", "Nakamura", "Quinlan", "Ruiz", "Abernathy"};
    static const std::vector<std::string> SPEC = {"machinist", "assembler", "finisher"};
    c.candidates.clear();
    int first = rndInt(c, 0, 2);
    for (int k = 0; k < 3; k++) {   // (one of each kind, so there's always a choice)
        Employee e;
        e.name = pick(c, FIRST) + " " + pick(c, LAST);
        e.specialty = SPEC[(first + k) % 3];
        e.skill = std::min(0.95, 0.2 + 0.5 * rnd(c) + c.reputation / 400);
        e.wage = std::round((90 + 260 * e.skill) / 5) * 5;
        c.candidates.push_back(e);
    }
    c.candidatesDay = c.day;
}
bool hire(Career& c, int i, std::string* why) {
    if (i < 0 || i >= (int)c.candidates.size()) return false;
    int stations = 0;
    for (auto& [k, n] : c.stations) if (k != "counter") stations += n;
    stations = std::min(stations, shopStations(c.shopTier));
    if ((int)c.staff.size() + 1 >= stations + 0) {   // (you take a bench too)
        if (why) *why = "no free workstation: place another in your workshop (a bench, a lathe, a mill or a test bench)";
        return false;
    }
    c.staff.push_back(c.candidates[i]);
    c.candidates.erase(c.candidates.begin() + i);
    return true;
}
void letGo(Career& c, int i) { if (i >= 0 && i < (int)c.staff.size()) c.staff.erase(c.staff.begin() + i); }

namespace { Walkin walkin(Career& c); }
Career newCareer(unsigned seed) {
    Career c;
    c.on = true;
    c.rng = seed ? seed : 12345;
    for (int k = 0; k < 2; k++) c.contracts.push_back(offer(c));   // (a couple of customers waiting on day 1)
    c.stations = {{"bench", 1}, {"counter", 1}};   // (your workshop: a bench and a counter, until you build it up)
    refreshCandidates(c);
    for (int i = 0; i < 2; i++) c.walkins.push_back(walkin(c));
    c.news = {"Day 1. You've opened your shop. Design a gun, rate it on the bench, build some copies and list them in your online store.",
              "Customers' orders are under Contracts."};
    return c;
}

namespace {
// someone at your counter: what they want, their form and their ID. One in a hundred can't buy: the check denies them,
// or their ID doesn't match the form, or it's run out
Walkin walkin(Career& c) {
    static const std::vector<std::string> LAST = {"Hensley", "Okafor", "Brandt", "Castillo", "Larsen", "Duval", "Petrov", "Quinlan", "Ruiz", "Abernathy", "Whitlock", "Sato"};
    struct Want { const char* text; int hands; double minE, maxE; };
    static const std::vector<Want> W = {
        {"My first handgun. Something easy to shoot.", 1, 150, 700}, {"Something for home defense.", 0, 400, 0}, {"A deer rifle for the season.", 2, 1800, 0},
        {"Just something fun to plink with.", 0, 0, 500}, {"A carry gun. Small, if you've got it.", 1, 150, 700}, {"Anything with some real punch.", 0, 1500, 0},
        {"A truck gun. Nothing fancy.", 2, 400, 2500}, {"Show me what you've got.", 0, 0, 0}};
    Walkin w;
    w.id = c.nextId++;
    std::string first = pick(c, FIRST), last = pick(c, LAST);
    w.name = first + " " + std::string(1, (char)('A' + rndInt(c, 0, 25))) + ". " + last;
    w.idName = w.name;
    const Want& want = pick(c, W);
    w.request = want.text; w.hands = want.hands; w.minEnergy = want.minE; w.maxEnergy = want.maxE;
    double e = want.maxE > 0 ? (want.minE + want.maxE) / 2 : std::max(want.minE, 500.0);
    w.budget = std::round(baseValue(e, want.hands == 2, 10) * (1.0 + 0.6 * rnd(c)) / 10) * 10;
    int y = 1950 + rndInt(c, 0, 55), m = rndInt(c, 1, 12), d = rndInt(c, 1, 28);
    char b[32]; std::snprintf(b, sizeof b, "%02d/%02d/%d", m, d, y);
    w.dob = w.idDob = b;
    std::snprintf(b, sizeof b, "%02d/%d", rndInt(c, 1, 12), 2032 + rndInt(c, 0, 6));
    w.idExpires = b;
    if (rnd(c) < 0.01) {   // the one in a hundred
        int kind = rndInt(c, 0, 3);
        if (kind <= 1) w.trouble = "denied";
        else if (kind == 2) { w.trouble = "mismatch"; w.idName = pick(c, FIRST) + " " + w.name.substr(w.name.find(' ') + 1); if (w.idName == w.name) w.idName = "J. " + last + "son"; }
        else { w.trouble = "expired"; std::snprintf(b, sizeof b, "%02d/%d", rndInt(c, 1, 12), 2024 + rndInt(c, 0, 5)); w.idExpires = b; }
    }
    return w;
}
}  // namespace

void endDay(Career& c) {
    c.news.clear();
    char b[256];
    // the day's work: copies in order, by you and your staff; some come out flawed, and a finisher catches most
    Crew crew = crewToday(c);
    double hours = crew.hours;
    int caught = 0, slipped = 0;
    for (auto& bd : c.builds) {
        auto it = c.sheets.find(bd.gun);
        double per = it != c.sheets.end() ? std::max(0.5, it->second.buildHours) : 4;
        while (bd.qty > 0 && hours > 0) {
            double need = per - bd.hoursDone, use = std::min(need, hours);
            bd.hoursDone += use; hours -= use;
            if (bd.hoursDone >= per - 1e-9) {
                bd.qty--; bd.hoursDone = 0; c.stock[bd.gun]++;
                std::snprintf(b, sizeof b, "Finished a %s.", bd.gun.c_str()); c.news.push_back(b);
                if (rnd(c) < crew.flaw) {
                    if (rnd(c) < crew.qc) caught++;            // (put right before it left the bench)
                    else { c.flawed[bd.gun]++; slipped++; }
                }
            }
        }
    }
    c.builds.erase(std::remove_if(c.builds.begin(), c.builds.end(), [](const Build& x) { return x.qty <= 0; }), c.builds.end());
    if (caught) { std::snprintf(b, sizeof b, "Your finisher caught %d flawed cop%s and put %s right.", caught, caught == 1 ? "y" : "ies", caught == 1 ? "it" : "them"); c.news.push_back(b); }
    (void)slipped;   // (nobody knows yet: the buyers find out)
    for (int i : crew.idle) { std::snprintf(b, sizeof b, "%s had no workstation to work at today.", c.staff[i].name.c_str()); c.news.push_back(b); }
    // wages, and your people get better at it
    double wages = 0;
    for (auto& e : c.staff) { wages += e.wage; e.days++; e.skill = std::min(0.95, e.skill + 0.003); }
    if (wages > 0) { c.money -= wages; std::snprintf(b, sizeof b, "Paid %s in wages.", money(wages).c_str()); c.news.push_back(b); }
    if (c.candidates.empty() || c.day - c.candidatesDay >= 7) refreshCandidates(c);
    // the online store: buyers look at each listing; the better it is for its price, the likelier they buy
    double income = 0;
    for (auto& L : c.listings) {
        if (!L.active || L.price <= 0) continue;
        auto it = c.sheets.find(L.gun);
        if (it == c.sheets.end()) continue;
        const GunSheet& s = it->second;
        int lookers = 1 + (int)(c.reputation / 20) + (rnd(c) < 0.5 ? 1 : 0);
        double p = std::min(0.85, 0.3 * std::pow(s.value() / L.price, 3.0));
        for (int k = 0; k < lookers; k++) {
            if (c.stock[L.gun] <= 0) break;
            if (rnd(c) >= p) continue;
            bool bad = c.flawed[L.gun] > 0 && rnd(c) * c.stock[L.gun] < c.flawed[L.gun];   // (one of the flawed ones?)
            if (bad) c.flawed[L.gun]--;
            c.stock[L.gun]--; L.sold++; c.money += L.price; income += L.price;
            std::string who = buyer(c);
            std::snprintf(b, sizeof b, "Sold a %s online for %s to %s.", L.gun.c_str(), money(L.price).c_str(), who.c_str()); c.news.push_back(b);
            if (rnd(c) < 0.5) {   // and maybe tells everyone what they think
                double vfm = clamp01(s.value() / L.price * 0.8);
                GunSheet got = s;
                if (bad) got.jamRate += 10;   // a flawed copy: it jams a lot more than the design should
                double score = 1 + 4 * (0.6 * (got.rated ? clamp01(1 - got.jamRate / 8) : 0.5) + 0.2 * (s.moa > 0 ? clamp01((8 - s.moa) / 7) : 0.7) + 0.2 * vfm) + (rnd(c) - 0.5) * 1.2;
                int stars = std::max(1, std::min(5, (int)std::lround(score)));
                Review r{c.day, L.gun, who, reviewText(c, got, stars, L.price) + (bad ? " Mine must be a lemon." : ""), stars};
                c.reviews.push_back(r);
                c.reputation = std::max(0.0, std::min(100.0, c.reputation + (stars - 3) * 0.8));
                std::snprintf(b, sizeof b, "New %d-star review of the %s.", stars, L.gun.c_str()); c.news.push_back(b);
            }
        }
    }
    if (c.reviews.size() > 200) c.reviews.erase(c.reviews.begin(), c.reviews.begin() + (c.reviews.size() - 200));
    // contracts: accepted ones past their day lapse; old offers go elsewhere; new ones come in
    for (auto& k : c.contracts) {
        if ((k.state == 1 || k.state == 4) && c.day >= k.dueDay) {
            k.state = 3; k.verdict = "Not delivered in time.";
            c.reputation = std::max(0.0, c.reputation - 3);
            std::snprintf(b, sizeof b, "Missed the deadline: %s for %s. Your reputation took a knock.", k.title.c_str(), k.client.c_str()); c.news.push_back(b);
        }
    }
    c.contracts.erase(std::remove_if(c.contracts.begin(), c.contracts.end(), [&](const Contract& k) { return k.state == 0 && c.day >= k.dueDay - 4; }), c.contracts.end());
    int offered = 0;
    for (auto& k : c.contracts) if (k.state == 0) offered++;
    if (offered < 4 && (offered < 2 || rnd(c) < 0.5)) {
        Contract k = offer(c);
        std::snprintf(b, sizeof b, "New contract offered: %s (%s, %s).", k.title.c_str(), k.client.c_str(), money(k.pay).c_str()); c.news.push_back(b);
        c.contracts.push_back(k);
    }
    if (c.contracts.size() > 60) c.contracts.erase(c.contracts.begin(), c.contracts.begin() + (c.contracts.size() - 60));
    // tomorrow's customers at your counter (if you have one): yesterday's who didn't buy have gone
    c.walkins.clear();
    if (c.stations.count("counter") && c.stations["counter"] > 0) {
        int n = std::min(5, 1 + (int)(c.reputation / 25) + (rnd(c) < 0.5 ? 1 : 0));
        for (int i = 0; i < n; i++) c.walkins.push_back(walkin(c));
    }
    c.day++;
    std::snprintf(b, sizeof b, "Day %d.%s", c.day, income > 0 ? (" Online sales yesterday: " + money(income) + ".").c_str() : "");
    c.news.insert(c.news.begin(), b);
}

namespace {
// the copies go, the money comes; flawed copies among them come back as complaints
void handOver(Career& c, Contract& k, const std::string& gun) {
    int bad = 0;
    for (int i = 0; i < k.qty; i++) if (c.flawed[gun] > 0 && rnd(c) * c.stock[gun] < c.flawed[gun]) { c.flawed[gun]--; bad++; }
    c.stock[gun] -= k.qty;
    c.money += k.pay;
    k.state = 2; k.delivered = gun; k.verdict = "Delivered and paid " + money(k.pay) + ".";
    c.reputation = std::min(100.0, c.reputation + 3 + std::min(10.0, k.qty * 0.5));
    if (bad) {
        c.reputation = std::max(0.0, c.reputation - bad);
        char b[200];
        std::snprintf(b, sizeof b, "%s found %d flawed gun%s in the order: they're not pleased.", k.client.c_str(), bad, bad == 1 ? "" : "s");
        c.news.push_back(b);
        k.verdict += " " + std::string(b);
    }
}
}  // namespace
bool deliver(Career& c, int id, const std::string& gun, const GunSheet& tested) {
    for (auto& k : c.contracts) {
        if (k.id != id || k.state != 1) continue;
        if (c.stock[gun] < k.qty) { k.verdict = "You need " + std::to_string(k.qty) + " built and in stock."; return false; }
        auto miss = shortfalls(k, tested);
        if (!miss.empty()) {
            k.verdict = "Turned down: ";
            for (size_t i = 0; i < miss.size(); i++) k.verdict += (i ? "; " : "") + miss[i];
            k.verdict += ".";
            return false;
        }
        c.sheets[gun] = tested;
        if (k.demo) {   // a big order: now they want to see it shot
            k.state = 4; k.delivered = gun;
            char b[200];
            std::snprintf(b, sizeof b, "Passed their bench test. Now the range demonstration: hit the paper %d times in %d rounds.", DEMO_HITS, DEMO_SHOTS);
            k.verdict = b;
            return false;
        }
        handOver(c, k, gun);
        return true;
    }
    return false;
}

void soldToProhibited(Career& c) {
    c.money -= 2500;
    c.reputation = std::max(0.0, c.reputation - 8);
    c.news.push_back("You sold to someone who failed the check: a $2,500 fine, and word gets around.");
}

// ---------------------------------------------------------------- saving
std::string careerToJson(const Career& c) {
    json o;
    o["gunsmithCareer"] = 1;
    o["money"] = c.money; o["day"] = c.day; o["reputation"] = c.reputation; o["hoursPerDay"] = c.hoursPerDay;
    o["rng"] = c.rng; o["nextId"] = c.nextId;
    json S = json::object();
    for (auto& [k, s] : c.sheets)
        S[k] = {{"rated", s.rated}, {"jamRate", s.jamRate}, {"fired", s.fired}, {"manual", s.manual}, {"caliber", s.caliber}, {"energy", s.energy},
                {"velocity", s.velocity}, {"massKg", s.massKg}, {"recoil", s.recoil}, {"barrelMm", s.barrelMm}, {"moa", s.moa}, {"capacity", s.capacity},
                {"twoHands", s.twoHands}, {"posed", s.posed}, {"parts", s.parts}, {"buildHours", s.buildHours}, {"materialCost", s.materialCost}};
    o["sheets"] = S;
    json st = json::object();
    for (auto& [k, n] : c.stock) if (n > 0) st[k] = n;
    o["stock"] = st;
    json L = json::array();
    for (auto& l : c.listings) L.push_back({{"gun", l.gun}, {"price", l.price}, {"active", l.active}, {"sold", l.sold}});
    o["listings"] = L;
    json B = json::array();
    for (auto& b : c.builds) B.push_back({{"gun", b.gun}, {"qty", b.qty}, {"hoursDone", b.hoursDone}});
    o["builds"] = B;
    json R = json::array();
    for (auto& r : c.reviews) R.push_back({{"day", r.day}, {"gun", r.gun}, {"who", r.who}, {"text", r.text}, {"stars", r.stars}});
    o["reviews"] = R;
    json K = json::array();
    for (auto& k : c.contracts)
        K.push_back({{"id", k.id}, {"client", k.client}, {"title", k.title}, {"brief", k.brief}, {"minEnergy", k.minEnergy}, {"maxEnergy", k.maxEnergy},
                     {"maxJam", k.maxJam}, {"maxKg", k.maxKg}, {"minCapacity", k.minCapacity}, {"hands", k.hands}, {"maxMoa", k.maxMoa}, {"qty", k.qty},
                     {"pay", k.pay}, {"dueDay", k.dueDay}, {"state", k.state}, {"delivered", k.delivered}, {"verdict", k.verdict}, {"demo", k.demo}});
    o["contracts"] = K;
    auto staffJ = [](const std::vector<Employee>& v) { json a = json::array(); for (auto& e : v) a.push_back({{"name", e.name}, {"specialty", e.specialty}, {"skill", e.skill}, {"wage", e.wage}, {"days", e.days}}); return a; };
    o["staff"] = staffJ(c.staff); o["candidates"] = staffJ(c.candidates); o["candidatesDay"] = c.candidatesDay; o["shopTier"] = c.shopTier;
    json st2 = json::object(); for (auto& [k, n] : c.stations) st2[k] = n; o["stations"] = st2;
    json fl = json::object(); for (auto& [k, n] : c.flawed) if (n > 0) fl[k] = n; o["flawed"] = fl;
    json W = json::array();
    for (auto& w : c.walkins)
        W.push_back({{"id", w.id}, {"name", w.name}, {"idName", w.idName}, {"request", w.request}, {"hands", w.hands}, {"minEnergy", w.minEnergy}, {"maxEnergy", w.maxEnergy},
                     {"budget", w.budget}, {"dob", w.dob}, {"idDob", w.idDob}, {"idExpires", w.idExpires}, {"trouble", w.trouble}, {"checked", w.checked}, {"result", w.result}, {"state", w.state}});
    o["walkins"] = W;
    o["news"] = c.news;
    return o.dump(1);
}

bool careerFromJson(const std::string& text, Career& out) {
    json o = json::parse(text, nullptr, false);
    if (o.is_discarded() || !o.is_object() || !o.contains("gunsmithCareer")) return false;
    Career c;
    c.on = true;
    c.money = o.value("money", 6000.0); c.day = o.value("day", 1); c.reputation = o.value("reputation", 0.0); c.hoursPerDay = o.value("hoursPerDay", 8.0);
    c.rng = o.value("rng", 12345u); c.nextId = o.value("nextId", 1);
    if (o.contains("sheets") && o["sheets"].is_object())
        for (auto& [k, j] : o["sheets"].items()) {
            GunSheet s; s.name = k;
            s.rated = j.value("rated", false); s.jamRate = j.value("jamRate", 0.0); s.fired = j.value("fired", 0); s.manual = j.value("manual", false);
            s.caliber = j.value("caliber", ""); s.energy = j.value("energy", 0.0); s.velocity = j.value("velocity", 0.0); s.massKg = j.value("massKg", 0.0);
            s.recoil = j.value("recoil", 0.0); s.barrelMm = j.value("barrelMm", 0.0); s.moa = j.value("moa", 0.0); s.capacity = j.value("capacity", 0);
            s.twoHands = j.value("twoHands", false); s.posed = j.value("posed", true); s.parts = j.value("parts", 0); s.buildHours = j.value("buildHours", 1.0); s.materialCost = j.value("materialCost", 0.0);
            c.sheets[k] = s;
        }
    if (o.contains("stock") && o["stock"].is_object()) for (auto& [k, n] : o["stock"].items()) c.stock[k] = n.get<int>();
    for (auto& j : o.value("listings", json::array())) c.listings.push_back({j.value("gun", ""), j.value("price", 0.0), j.value("active", true), j.value("sold", 0)});
    for (auto& j : o.value("builds", json::array())) c.builds.push_back({j.value("gun", ""), j.value("qty", 0), j.value("hoursDone", 0.0)});
    for (auto& j : o.value("reviews", json::array())) c.reviews.push_back({j.value("day", 0), j.value("gun", ""), j.value("who", ""), j.value("text", ""), j.value("stars", 3)});
    for (auto& j : o.value("contracts", json::array())) {
        Contract k;
        k.id = j.value("id", 0); k.client = j.value("client", ""); k.title = j.value("title", ""); k.brief = j.value("brief", "");
        k.minEnergy = j.value("minEnergy", 0.0); k.maxEnergy = j.value("maxEnergy", 0.0); k.maxJam = j.value("maxJam", 0.0); k.maxKg = j.value("maxKg", 0.0);
        k.minCapacity = j.value("minCapacity", 0); k.hands = j.value("hands", 0); k.maxMoa = j.value("maxMoa", 0.0); k.qty = j.value("qty", 1);
        k.pay = j.value("pay", 0.0); k.dueDay = j.value("dueDay", 0); k.state = j.value("state", 0); k.delivered = j.value("delivered", ""); k.verdict = j.value("verdict", "");
        k.demo = j.value("demo", false);
        c.contracts.push_back(k);
    }
    auto staffR = [](const json& a, std::vector<Employee>& v) {
        if (!a.is_array()) return;
        for (auto& j : a) { Employee e; e.name = j.value("name", ""); e.specialty = j.value("specialty", "assembler"); e.skill = j.value("skill", 0.5); e.wage = j.value("wage", 150.0); e.days = j.value("days", 0); v.push_back(e); }
    };
    if (o.contains("staff")) staffR(o["staff"], c.staff);
    if (o.contains("candidates")) staffR(o["candidates"], c.candidates);
    c.candidatesDay = o.value("candidatesDay", 0); c.shopTier = o.value("shopTier", 0);
    if (o.contains("stations") && o["stations"].is_object()) for (auto& [k, n] : o["stations"].items()) c.stations[k] = n.get<int>();
    if (o.contains("flawed") && o["flawed"].is_object()) for (auto& [k, n] : o["flawed"].items()) c.flawed[k] = n.get<int>();
    for (auto& j : o.value("walkins", json::array())) {
        Walkin w;
        w.id = j.value("id", 0); w.name = j.value("name", ""); w.idName = j.value("idName", ""); w.request = j.value("request", ""); w.hands = j.value("hands", 0);
        w.minEnergy = j.value("minEnergy", 0.0); w.maxEnergy = j.value("maxEnergy", 0.0); w.budget = j.value("budget", 0.0); w.dob = j.value("dob", ""); w.idDob = j.value("idDob", "");
        w.idExpires = j.value("idExpires", ""); w.trouble = j.value("trouble", ""); w.checked = j.value("checked", false); w.result = j.value("result", ""); w.state = j.value("state", 0);
        c.walkins.push_back(w);
    }
    if (o.contains("news") && o["news"].is_array()) for (auto& n : o["news"]) if (n.is_string()) c.news.push_back(n.get<std::string>());
    out = c;
    return true;
}


// ---------------------------------------------------------------- stage 3: the counter
namespace {
Walkin* findWalkin(Career& c, int id) { for (auto& w : c.walkins) if (w.id == id) return &w; return nullptr; }
}
void runCheck(Career& c, int id) {
    Walkin* w = findWalkin(c, id);
    if (!w || w->checked) return;
    w->checked = true;
    w->result = w->trouble == "denied" ? "DENIED" : "PROCEED";   // (an ID that doesn't match, or has run out: the check
                                                                  // doesn't see that. You do, if you look.)
}
bool sellToWalkin(Career& c, int id, const std::string& gun, double price, std::string* why) {
    Walkin* w = findWalkin(c, id);
    auto no = [&](const std::string& t) { if (why) *why = t; return false; };
    if (!w || w->state != 0) return no("they've gone");
    if (!w->checked) return no("run the background check first");
    if (c.stock[gun] <= 0) return no("none in stock");
    auto it = c.sheets.find(gun);
    if (it == c.sheets.end()) return no("rate it first");
    const GunSheet& s = it->second;
    if (w->hands == 1 && (!s.posed || s.twoHands)) return no("\"I'm after a handgun.\"");
    if (w->hands == 2 && (!s.posed || !s.twoHands)) return no("\"I wanted a long gun.\"");
    if (w->minEnergy > 0 && s.energy < w->minEnergy) return no("\"Bit light for what I need.\"");
    if (w->maxEnergy > 0 && s.energy > w->maxEnergy) return no("\"That's more gun than I want.\"");
    if (price > w->budget) return no("\"That's more than I can spend.\"");
    bool bad = c.flawed[gun] > 0 && rnd(c) * c.stock[gun] < c.flawed[gun];
    if (bad) c.flawed[gun]--;
    c.stock[gun]--;
    c.money += price;
    w->state = 1;
    c.reputation = std::min(100.0, c.reputation + 0.3);
    char b[200];
    std::snprintf(b, sizeof b, "Sold a %s over the counter to %s for %s.", gun.c_str(), w->name.c_str(), money(price).c_str());
    c.news.push_back(b);
    if (!w->trouble.empty()) soldToProhibited(c);   // (they shouldn't have had it)
    return true;
}
void refuseWalkin(Career& c, int id, bool police) {
    Walkin* w = findWalkin(c, id);
    if (!w || w->state != 0) return;
    w->state = police ? 3 : 2;
    if (!w->trouble.empty() && police) { c.reputation = std::min(100.0, c.reputation + 2); c.news.push_back("The deputies came for " + w->name + ". The sheriff thanks you for the call."); }
    else if (w->trouble.empty() && police) { c.reputation = std::max(0.0, c.reputation - 2); c.news.push_back("You called the police on " + w->name + ", who'd done nothing wrong. Awkward."); }
    else if (w->trouble.empty()) c.reputation = std::max(0.0, c.reputation - 0.3);   // (a customer turned away for nothing)
}

// ---------------------------------------------------------------- stage 4: the range demonstration
bool demoResult(Career& c, int id, const std::string& gun, int hits, int shots) {
    for (auto& k : c.contracts) {
        if (k.id != id || k.state != 4) continue;
        char b[240];
        if (c.stock[gun] < k.qty) { k.verdict = "You need " + std::to_string(k.qty) + " built and in stock."; return false; }
        if (hits >= DEMO_HITS) {
            handOver(c, k, gun);
            std::snprintf(b, sizeof b, " The demonstration went well: %d hits in %d rounds.", hits, shots);
            k.verdict += b;
            c.reputation = std::min(100.0, c.reputation + 2);
            return true;
        }
        k.state = 4;
        std::snprintf(b, sizeof b, "The demonstration fell short: %d hits in %d rounds (they want %d). You can try again before the deadline.", hits, shots, DEMO_HITS);
        k.verdict = b;
        c.reputation = std::max(0.0, c.reputation - 1);
        return false;
    }
    return false;
}
}  // namespace career
