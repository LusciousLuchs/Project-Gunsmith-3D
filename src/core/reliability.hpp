// Gunsmith3D - reliability test: shoot a design many times with the small differences real shooting has
// (ammunition that is a little hot or weak, a dirty or clean action, a firm or loose hold), clear each jam
// the way a shooter would, and count what went wrong.
#pragma once
#include "solver.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace sim {

struct ReliabilityOptions {
    int rounds = 50;              // rounds to fire
    unsigned seed = 1;            // the same seed gives the same run
    double powerSpread = 0.05;    // ammunition power, +- this fraction (1 standard deviation)
    double frictionSpread = 0.3;  // action friction from clean to dirty: up to this fraction more
    double limpMax = 0.25;        // the loosest hold, 0 firm .. 1 a gun held by nothing
};

struct ReliabilityEvent {
    int shot;                     // after this many rounds fired
    Malf malf;
    double power, limp;           // the round and the hold when it happened
};

struct ReliabilityReport {
    bool manual = false;          // the action is worked by hand (bolt, single shot)
    bool singleLoad = false;      // rounds go in one at a time by hand
    int fired = 0;                // rounds that went off
    int attempts = 0;             // trigger pulls
    int jams = 0;                 // malfunctions in all
    int byType[(int)Malf::Count] = {0};
    int reloads = 0;
    std::vector<ReliabilityEvent> events;
    bool gaveUp = false;          // a jam that could not be cleared: the test stopped
    std::string note;             // why it stopped, or anything else worth saying
    double jamRate() const { return fired + jams > 0 ? 100.0 * jams / (fired + jams) : 0; }  // % of rounds that jammed
    double mrbf() const { return jams > 0 ? (double)fired / jams : fired; }                  // mean rounds between failures
};

// Runs headless; can take a while for many rounds, so `done` reports progress (rounds fired) and `cancel` stops it.
ReliabilityReport runReliability(const Design& d, const ReliabilityOptions& o, std::atomic<int>* done = nullptr,
                                 const std::atomic<bool>* cancel = nullptr);

}  // namespace sim
