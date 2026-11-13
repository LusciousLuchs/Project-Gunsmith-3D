// Gunsmith3D - runs the reliability test on a worker thread so the app keeps drawing while it shoots.
#pragma once
#include "reliability.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct RelTest {
    int rounds = 50;
    unsigned seed = 1;
    std::atomic<bool> running{false}, cancel{false}, finished{false};
    std::atomic<int> done{0};
    int target = 0;
    std::thread worker;
    std::mutex m;
    sim::ReliabilityReport report;   // valid when `have`
    bool have = false;
    std::string designName;          // what was tested
};
extern RelTest REL;

// Recorded by hand: you shoot and work the gun on the bench yourself; every shot, dry click and jam counts, and after
// `target` shots the result stands as the design's reliability test (for actions the automatic test can't work)
struct HandRecord {
    bool on = false;
    int target = 25, fired = 0, clicks = 0, jams = 0;
    int byType[(int)sim::Malf::Count] = {0};
    std::vector<sim::ReliabilityEvent> events;
};
extern HandRecord HANDREC;
void handRecordStart();
void handRecordStop(bool keep);   // keep: what was shot so far becomes the result
void handRecordEvent(const sim::Sim& S, sim::Ev kind);   // the bench's events, as they come

void relStart();   // tests the current design
void relStop();    // asks the worker to stop; poll picks up the partial result
void relPoll();    // call every frame: collects a finished run
void relShutdown();
