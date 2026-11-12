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
    int state = 0;                         // 0 offered, 1 accepted, 2 delivered, 3 lapsed (missed or turned down)
    std::string delivered, verdict;
};
// What the gun misses of the brief (empty: it meets it all)
std::vector<std::string> shortfalls(const Contract& c, const GunSheet& s);

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
};
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
