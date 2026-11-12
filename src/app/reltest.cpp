// Gunsmith3D - reliability test runner (see reltest.hpp).
#include "app/reltest.hpp"
#include "app/app.hpp"

RelTest REL;

void relStart() {
    if (REL.running) return;
    if (REL.worker.joinable()) REL.worker.join();
    REL.cancel = false; REL.finished = false; REL.done = 0;
    REL.target = REL.rounds;
    REL.running = true;
    REL.designName = A.design.name;
    Design d = A.design;  // the worker gets its own copy: editing carries on meanwhile
    sim::ReliabilityOptions o;
    o.rounds = REL.rounds;
    o.seed = REL.seed++;
    o.stallSeconds = 10;   // (a gun that can't fire and reload in 10 s is given up on, not tested forever)
    REL.worker = std::thread([d, o]() {
        sim::ReliabilityReport r = sim::runReliability(d, o, &REL.done, &REL.cancel);
        std::lock_guard<std::mutex> lk(REL.m);
        REL.report = r;
        REL.finished = true;
    });
}
void relStop() { REL.cancel = true; }
void relPoll() {
    if (!REL.running || !REL.finished) return;
    if (REL.worker.joinable()) REL.worker.join();
    REL.running = false;
    {
        std::lock_guard<std::mutex> lk(REL.m);
        REL.have = true;
    }
    toast(REL.report.jams ? "Reliability test done: " + std::to_string(REL.report.jams) + " jam(s)." : "Reliability test done: no jams.");
}
void relShutdown() {
    REL.cancel = true;
    if (REL.worker.joinable()) REL.worker.join();
}
