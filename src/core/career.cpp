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
    struct Tpl { const char* title; const char* brief; double minE, maxE, maxJam, maxKg; int minCap, hands; double maxMoa; int qMin, qMax; double rep; };
    static const std::vector<Tpl> T = {
        {"A pocket pistol", "Something small and light I can carry every day. It has to work every time.", 150, 700, 3, 1.0, 6, 1, 0, 1, 1, 0},
        {"A deer rifle", "A rifle for whitetail out to 200 yards. Accuracy matters more than anything.", 1800, 0, 6, 0, 0, 2, 2.5, 1, 1, 0},
        {"A ranch carbine", "Something handy for coyotes and the truck. Light, holds a few.", 500, 2500, 5, 6.5, 5, 2, 0, 1, 1, 0},
        {"A competition pistol", "For steel matches: accurate, soft-shooting, and it must not jam.", 300, 800, 1.5, 0, 10, 1, 4, 1, 1, 0},
        {"Something cursed", "I run a YouTube channel. Build me the weirdest thing you can that still goes bang now and then.", 0, 0, 30, 0, 0, 0, 0, 1, 1, 0},
        {"A big-bore revolver substitute", "I want a handgun that hits like a truck. Weight and kick don't scare me.", 1000, 0, 8, 0, 0, 1, 0, 1, 1, 0},
        {"Guns for a security firm", "Sidearms for our guards: reliable above all, enough rounds.", 300, 800, 2, 1.3, 10, 1, 0, 3, 5, 15},
        {"Rifles for a hunting lodge", "Loaner rifles for our guests. Accurate and dependable.", 1800, 0, 4, 9, 3, 2, 3, 3, 6, 25},
    };
    std::vector<const Tpl*> open;
    for (auto& t : T) if (c.reputation >= t.rep) open.push_back(&t);
    const Tpl& t = *open[std::min(open.size() - 1, (size_t)(rnd(c) * open.size()))];
    k.title = t.title; k.brief = t.brief;
    k.minEnergy = t.minE; k.maxEnergy = t.maxE; k.maxJam = t.maxJam; k.maxKg = t.maxKg; k.minCapacity = t.minCap; k.hands = t.hands; k.maxMoa = t.maxMoa;
    k.qty = rndInt(c, t.qMin, t.qMax);
    k.client = t.qMax > 1 ? pick(c, std::vector<std::string>{"Ironwood Security", "Coldwater Lodge", "Redline Protection", "Big Sky Outfitters"}) : buyer(c);
    double e = t.maxE > 0 ? (t.minE + t.maxE) / 2 : std::max(t.minE, 600.0);
    double each = baseValue(e, t.hands == 2, t.minCap) * (1.25 + 0.4 * rnd(c)) + 150;   // bespoke work pays better than the shelf
    k.pay = std::round(each * k.qty / 10) * 10;
    k.dueDay = c.day + rndInt(c, 6, 12) + 2 * k.qty;
    return k;
}
}  // namespace

Career newCareer(unsigned seed) {
    Career c;
    c.on = true;
    c.rng = seed ? seed : 12345;
    for (int k = 0; k < 2; k++) c.contracts.push_back(offer(c));   // (a couple of customers waiting on day 1)
    c.news = {"Day 1. You've opened your shop. Design a gun, rate it on the bench, build some copies and list them in your online store.",
              "Customers' orders are under Contracts."};
    return c;
}

void endDay(Career& c) {
    c.news.clear();
    char b[256];
    // the day's work: copies in order
    double hours = c.hoursPerDay;
    for (auto& bd : c.builds) {
        auto it = c.sheets.find(bd.gun);
        double per = it != c.sheets.end() ? std::max(0.5, it->second.buildHours) : 4;
        while (bd.qty > 0 && hours > 0) {
            double need = per - bd.hoursDone, use = std::min(need, hours);
            bd.hoursDone += use; hours -= use;
            if (bd.hoursDone >= per - 1e-9) {
                bd.qty--; bd.hoursDone = 0; c.stock[bd.gun]++;
                std::snprintf(b, sizeof b, "Finished a %s.", bd.gun.c_str()); c.news.push_back(b);
            }
        }
    }
    c.builds.erase(std::remove_if(c.builds.begin(), c.builds.end(), [](const Build& x) { return x.qty <= 0; }), c.builds.end());
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
            c.stock[L.gun]--; L.sold++; c.money += L.price; income += L.price;
            std::string who = buyer(c);
            std::snprintf(b, sizeof b, "Sold a %s online for %s to %s.", L.gun.c_str(), money(L.price).c_str(), who.c_str()); c.news.push_back(b);
            if (rnd(c) < 0.5) {   // and maybe tells everyone what they think
                double vfm = clamp01(s.value() / L.price * 0.8);
                double score = 1 + 4 * (0.6 * (s.rated ? clamp01(1 - s.jamRate / 8) : 0.5) + 0.2 * (s.moa > 0 ? clamp01((8 - s.moa) / 7) : 0.7) + 0.2 * vfm) + (rnd(c) - 0.5) * 1.2;
                int stars = std::max(1, std::min(5, (int)std::lround(score)));
                Review r{c.day, L.gun, who, reviewText(c, s, stars, L.price), stars};
                c.reviews.push_back(r);
                c.reputation = std::max(0.0, std::min(100.0, c.reputation + (stars - 3) * 0.8));
                std::snprintf(b, sizeof b, "New %d-star review of the %s.", stars, L.gun.c_str()); c.news.push_back(b);
            }
        }
    }
    if (c.reviews.size() > 200) c.reviews.erase(c.reviews.begin(), c.reviews.begin() + (c.reviews.size() - 200));
    // contracts: accepted ones past their day lapse; old offers go elsewhere; new ones come in
    for (auto& k : c.contracts) {
        if (k.state == 1 && c.day >= k.dueDay) {
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
    c.day++;
    std::snprintf(b, sizeof b, "Day %d.%s", c.day, income > 0 ? (" Online sales yesterday: " + money(income) + ".").c_str() : "");
    c.news.insert(c.news.begin(), b);
}

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
        c.stock[gun] -= k.qty;
        c.money += k.pay;
        k.state = 2; k.delivered = gun; k.verdict = "Delivered and paid " + money(k.pay) + ".";
        c.reputation = std::min(100.0, c.reputation + 3 + k.qty * 0.5);
        c.sheets[gun] = tested;
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
                     {"pay", k.pay}, {"dueDay", k.dueDay}, {"state", k.state}, {"delivered", k.delivered}, {"verdict", k.verdict}});
    o["contracts"] = K;
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
        c.contracts.push_back(k);
    }
    if (o.contains("news") && o["news"].is_array()) for (auto& n : o["news"]) if (n.is_string()) c.news.push_back(n.get<std::string>());
    out = c;
    return true;
}

}  // namespace career
