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

HandRecord HANDREC;
void handRecordStart() {
    int t = REL.rounds;
    HANDREC = HandRecord();
    HANDREC.on = true; HANDREC.target = std::max(5, std::min(t, 100));
    toast("Recording: shoot, reload and clear jams yourself. Every shot and jam counts.");
}
void handRecordStop(bool keep) {
    if (!HANDREC.on) return;
    HANDREC.on = false;
    if (!keep || HANDREC.fired == 0) return;
    std::lock_guard<std::mutex> lk(REL.m);
    sim::ReliabilityReport r;
    r.manual = true;
    r.fired = HANDREC.fired; r.jams = HANDREC.jams; r.attempts = HANDREC.fired + HANDREC.clicks;
    for (int i = 0; i < (int)sim::Malf::Count; i++) r.byType[i] = HANDREC.byType[i];
    r.events = HANDREC.events;
    r.note = HANDREC.fired < HANDREC.target ? "Recorded by hand: " + std::to_string(HANDREC.fired) + " of " + std::to_string(HANDREC.target) + " shots."
                                            : "Recorded by hand.";
    REL.report = r; REL.have = true; REL.designName = A.design.name;
    toast("Your shots are the test's result now.");
}
void handRecordEvent(const sim::Sim& S, sim::Ev kind) {
    if (!HANDREC.on) return;
    if (kind == sim::Ev::Bang) {
        HANDREC.fired++;
        if (HANDREC.fired >= HANDREC.target) handRecordStop(true);
    } else if (kind == sim::Ev::Click) HANDREC.clicks++;
    else if (kind == sim::Ev::Malfunction && S.malf != sim::Malf::None) {
        HANDREC.jams++; HANDREC.byType[(int)S.malf]++;
        HANDREC.events.push_back({HANDREC.fired, S.malf, 1.0, 0.0});
    }
}
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
