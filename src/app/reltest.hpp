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

void relStart();   // tests the current design
void relStop();    // asks the worker to stop; poll picks up the partial result
void relPoll();    // call every frame: collects a finished run
void relShutdown();
