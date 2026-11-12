// Gunsmith3D - Career mode (stage 1): a small gun business in a post-NFA America (every gun is legal to own).
// You design guns as always; here they're rated from the bench, built copy by copy (that takes hours), sold through an
// online store where buyers leave reviews, and built to order for customers' contracts. Days pass when you end them.
// No graphics here: the app's career screen (app/career, ui/panel_career) shows it and runs the slow bench tests.
#pragma once
#include "design.hpp"

#include <map>
#include <string>
#include <vector>

namespace career {

// A gun's rating sheet, worked out from the gun and a bench test
struct GunSheet {
    std::string name;            // the blueprint's name (its file in Blueprints)
    bool rated = false;          // the bench test has been run
    double jamRate = 0;          // % of rounds that jammed in the test
    int fired = 0;               // rounds fired in it
    bool manual = false;         // worked by hand (bolt, pump, single shot)
    std::string caliber;         // the cartridge's name
    double energy = 0, velocity = 0;   // J, m/s (muzzle)
    double massKg = 0;           // empty
    double recoil = 0;           // how hard it kicks: the gun's backward speed per shot, m/s
    double barrelMm = 0;
    double moa = 0;              // estimated group size, minutes of angle (smaller is better)
    int capacity = 0;            // rounds in a magazine (0: loaded one at a time)
    bool twoHands = false;
    bool posed = true;           // its hold is set in Pose (else nobody knows how it's held: one or two hands)
    int parts = 0;               // parts to make (not counting magazines' rounds)
    double buildHours = 0;       // to make one copy
    double materialCost = 0;     // $ of material in one copy
    double value() const;        // what a buyer thinks it's worth, $
    double quality() const;      // 0 .. 1: reliability, accuracy, handling together
};
// Everything but the bench test (instant). `jamRate`/`fired` come from a reliability test, run by the app.
GunSheet sheetOf(const Design& d);

struct Review { int day = 0; std::string gun, who, text; int stars = 3; };
struct Listing { std::string gun; double price = 0; bool active = true; int sold = 0; };
struct Build { std::string gun; int qty = 0; double hoursDone = 0; };   // copies being made, in order

// A customer's order to a brief. Paid on delivery, once the delivered gun passes the bench test.
struct Contract {
    int id = 0;
    std::string client, title, brief;
    // what it must be (0 / empty: anything)
    double minEnergy = 0, maxEnergy = 0;   // J at the muzzle: the class of round
    double maxJam = 0;                     // % jams in the test, at most
    double maxKg = 0;
    int minCapacity = 0;
    int hands = 0;                         // 1 one-handed, 2 two-handed, 0 either
    double maxMoa = 0;
    int qty = 1;
    double pay = 0;                        // $ for all of them
    int dueDay = 0;
    int state = 0;                         // 0 offered, 1 accepted, 2 delivered, 3 lapsed (missed or turned down),
                                           // 4 passed the bench test, waiting for its range demonstration
    bool demo = false;                     // a big order: after the bench test, you show it on the range
    std::string delivered, verdict;
};
// What the gun misses of the brief (empty: it meets it all)
std::vector<std::string> shortfalls(const Contract& c, const GunSheet& s);

// Stage 2: the people who build your guns, and the workstations they work at (placed in your workshop map)
struct Employee {
    std::string name, specialty;           // machinist (turned and cut parts), assembler (many parts), finisher (checks copies)
    double skill = 0.5;                    // 0 .. 1: how fast, and how rarely a copy comes out flawed; grows with the days
    double wage = 150;                     // $ a day
    int days = 0;
};
// Stage 3: someone at your counter
struct Walkin {
    int id = 0;
    std::string name, idName, request;     // the name on the form, the name on their ID, what they're after
    int hands = 0;                         // 1 a handgun, 2 a long gun, 0 anything
    double minEnergy = 0, maxEnergy = 0, budget = 0;
    std::string dob, idDob, idExpires;     // date of birth on the form and on the ID, and when the ID runs out
    std::string trouble;                   // "" a clean sale; else why they can't buy (the check denies them, or their
                                           // ID doesn't match the form, or it's expired): refuse, and call the police
    bool checked = false;
    std::string result;                    // the background check's answer: PROCEED or DENIED
    int state = 0;                         // 0 waiting, 1 sold, 2 sent away, 3 reported to the police
    int prop = 1;                          // the property they came into
    std::string interest;                  // a gun they saw on display there and want to see ("" none)
    // the paperwork (0.8.2): the transfer form they fill in, their ID, and the check you run
    std::string address, idAddress;        // what they wrote; what's on their ID
    std::string idNumber, idState;         // their licence (a made-up format)
    std::string sex, height, weight, birthPlace;
    std::string answers;                   // the form's questions (FORM_QUESTIONS), one letter each: Y, N, or ' ' left blank
    bool signedForm = true;
    std::vector<std::string> flaws;        // mistakes on the form they'd fix if handed it back: blank, unsigned, address, dob
    bool scanned = false;
    std::string scan;                      // the ID scanner's answer: VALID, EXPIRED, NOT VALID
    std::string txn;                       // the check's transaction number
    bool badPaper = false;                 // sold to with mistakes still on the form
};
// The transfer form's questions (a made-up form, in plain words). The first must be answered Yes; the rest No: any
// other answer means they can't buy.
const std::vector<std::string>& formQuestions();

// Properties (0.8, the owner's plan): you start selling out of your home garage and move up to a shack, a strip-mall unit
// and a warehouse, and can own several at once. Each is a fixed building (its walls and storefront come with it) that
// you fill as you like, up to its size; each is set to sell (a store: walk-ins, displays), make (a workshop: its
// workstations and staff count) or both.
struct PropertyKind { const char* id; const char* name; const char* blurb; double price, upkeep; int stations, staff; };
const std::vector<PropertyKind>& propertyKinds();   // garage, shack, retail, warehouse
const PropertyKind& propertyKind(const std::string& id);
struct Property {
    int id = 1;
    std::string kind = "garage";
    std::string role = "both";               // store, workshop, both
    std::string name;                         // "" = the kind's name
    std::map<std::string, int> stations;      // what's placed in it (from its map): bench, lathe, mill, qc, counter
    double rating = 0;                        // the store's rating, 0..5 stars (from its map: displays, decoration, light)
    std::vector<std::string> shown;           // the guns on its displays
    int city = -1;                            // where it is (places::cities()), -1 not chosen yet
    std::string address;                      // its made-up street address there
    bool sells() const { return role != "workshop"; }
    bool makes() const { return role != "store"; }
};

struct Career {
    bool on = false;
    double money = 6000;
    int day = 1;
    double reputation = 0;                 // 0 unknown .. 100 famous: more buyers, better contracts
    double hoursPerDay = 8;                // your own working day (employees add to this, later)
    std::map<std::string, GunSheet> sheets;
    std::map<std::string, int> stock;      // copies made and not yet sold
    std::vector<Listing> listings;
    std::vector<Build> builds;
    std::vector<Review> reviews;
    std::vector<Contract> contracts;
    std::vector<std::string> news;         // what happened at the end of the last day
    unsigned rng = 12345;
    int nextId = 1;
    double hoursToday = 0;                 // (hours already worked today by hand: none yet)
    // stage 2
    std::vector<Employee> staff, candidates;
    int candidatesDay = 0;
    int shopTier = 0;                      // your building: how many workstations fit (shopStations)
    std::map<std::string, int> stations;   // workstations placed in your workshop: bench, lathe, mill, qc (and counter)
    std::map<std::string, int> flawed;     // of the stock, copies with a flaw nobody caught (they jam more)
    // stage 3
    std::vector<Walkin> walkins;
    int badForms = 0;                       // sales made on forms with mistakes, since the last inspection
    // properties (0.8)
    std::vector<Property> props;
    int here = 1;                           // the property you're in
    // taxes: profit since the last tax day, paid each week (federal and your main property's state)
    double taxable = 0, taxesPaid = 0;
    int taxDay = 1;
    // accessories: plain boxes (magazines, ammunition, holsters, slings, cleaning kits...) sold in your stores
    int accessories = 0;                    // boxes in stock
    double accPrice = 45;                   // what you sell one for
};
Property* propertyById(Career& c, int id);
const Property* propertyById(const Career& c, int id);
void ensureProperties(Career& c);           // an older career: its one shop becomes its first property
bool buyProperty(Career& c, const std::string& kind, const std::string& role, std::string* why = nullptr);   // (the new one's id is the last in props)
bool sellProperty(Career& c, int id, std::string* why = nullptr);   // 60% back; never the last one
int stationRoom(const Career& c);           // workstations that count today: each making property's, up to its size
int staffRoom(const Career& c);             // employees you have room for
double dailyUpkeep(const Career& c);
void sumStations(Career& c);
void setLocation(Career& c, Property& p, int city);   // a property's city, and a made-up street address there
std::string propertyAddress(const Property& p);       // "" if it has no place yet
constexpr double ACC_COST = 18;             // a box of accessories, wholesale
bool buyAccessories(Career& c, int boxes, std::string* why = nullptr);
double taxRate(const Career& c);            // percent of profit, this week
double accessoryChance(const Property& p);  // the chance a customer in this store buys a box (accessory stands help)                // Career::stations from the making properties' own (call after a property's map changes)
// a person's body from their first name: "male", "female", or "" (a name either could have)
std::string bodyForName(const std::string& name);
int shopStations(int tier);                // workstations your building has room for
double shopUpgradeCost(int tier);          // to move up from this tier ($; 0: the biggest)
const char* shopName(int tier);
const char* tierName(double reputation);   // how well known you are
// Your working day plus your staff's, as today's workstations allow (per employee: their hours, and whether their
// station suits them)
double workHours(const Career& c);
void refreshCandidates(Career& c);         // a new few people looking for work (each week, or when asked)
bool hire(Career& c, int candidate, std::string* why = nullptr);
void letGo(Career& c, int employee);
// Stage 3: the counter. Run the check, then sell (true if they bought it), send them away, or call the police.
void runCheck(Career& c, int walkinId);   // (as if their ID's details were typed in right)
bool fixForm(Career& c, int walkinId);     // hand their form back: they correct its mistakes (true if there were any)
std::string scanId(Career& c, int walkinId);   // VALID, EXPIRED or NOT VALID
// the check system: what you typed in. NO MATCH (a typo, or details that aren't on their ID: try again), or PROCEED,
// DELAYED (not today) or DENIED for the person on the ID
std::string submitCheck(Career& c, int walkinId, const std::string& last, const std::string& first, const std::string& dob, const std::string& idNumber);
bool sellToWalkin(Career& c, int walkinId, const std::string& gun, double price, std::string* why = nullptr);
void refuseWalkin(Career& c, int walkinId, bool police);
// Stage 4: a big order's range demonstration: hits on paper out of the rounds fired. Paid if it passes.
bool demoResult(Career& c, int contractId, const std::string& gun, int hits, int shots);
constexpr int DEMO_SHOTS = 20, DEMO_HITS = 12;
Career newCareer(unsigned seed);
std::string careerToJson(const Career& c);
bool careerFromJson(const std::string& text, Career& c);

// Start making copies: pays for their material now. False (and why) if you can't.
bool startBuild(Career& c, const std::string& gun, int qty, std::string* why = nullptr);
// The end of the day: the day's hours go into the builds in order; online buyers order (from stock) and some leave
// reviews; new contracts come in, overdue ones lapse. Reported in c.news.
void endDay(Career& c);
// Hand a contract the copies (from stock) of a gun whose sheet was just tested. Paid if it meets the brief; if not, the
// copies come back and the verdict says why. True if paid.
bool deliver(Career& c, int contractId, const std::string& gun, const GunSheet& tested);
// A sale refused at the counter is fine; a sale to someone who failed the check is a fine and a hit to reputation
// (the owner's choice: no harsher consequence in a cozy game). (Used by the storefront, stage 3.)
void soldToProhibited(Career& c);

}  // namespace career
