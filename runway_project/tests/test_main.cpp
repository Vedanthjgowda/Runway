// tests/test_main.cpp - unit tests (separation, parser) and system tests (scheduler + validator)
#include "scenario.h"
#include "scheduler.h"
#include "report.h"
#include "separation.h"
#include <iostream>
#include <fstream>
#include <functional>

static int passed = 0, failed = 0;
#define CHECK(name, cond) do { if (cond) { ++passed; std::cout << "  [PASS] " << name << "\n"; } \
                               else { ++failed; std::cout << "  [FAIL] " << name << "  (line " << __LINE__ << ")\n"; } } while (0)

static Event req(int id, OpType t, Wake w, Tier p, int time) {
    Event e; e.kind = EvType::REQUEST; e.time = time; e.ac.id = id; e.ac.flight = "T" + std::to_string(id);
    e.ac.type = t; e.ac.wake = w; e.ac.tier = p; e.ac.request = time; return e;
}
static const Aircraft* find(const Result& r, int id) { for (auto& a : r.schedule) if (a.id == id) return &a; return nullptr; }

static void testSeparation() {
    std::cout << "Unit: separation rules\n";
    CHECK("heavy->light arrival needs 150 s", baseSeparation(OpType::ARRIVAL, Wake::HEAVY, OpType::ARRIVAL, Wake::LIGHT) == 150);
    CHECK("light->heavy arrival needs 60 s",  baseSeparation(OpType::ARRIVAL, Wake::LIGHT, OpType::ARRIVAL, Wake::HEAVY) == 60);
    CHECK("mixed arrival->departure 60 s",    baseSeparation(OpType::ARRIVAL, Wake::HEAVY, OpType::DEPARTURE, Wake::LIGHT) == 60);
    CHECK("weather x1.5 scales 60 -> 90",     separation(OpType::ARRIVAL, Wake::MEDIUM, OpType::ARRIVAL, Wake::MEDIUM, 1.5) == 90);
}

static void testParser() {
    std::cout << "Unit: scenario parser\n";
    std::ofstream("/tmp/ok.txt") << "RUNWAYS 2\nAIRCRAFT 1 AI1 A M N 0\nCLOSE 1 100 50\nWEATHER 10 20 1.5\n";
    std::ofstream("/tmp/bad1.txt") << "AIRCRAFT 1 AI1 X M N 0\n";
    std::ofstream("/tmp/bad2.txt") << "RUNWAYS 1\nCLOSE 3 10 10\n";
    Scenario sc; std::string err;
    CHECK("valid file loads", loadScenario("/tmp/ok.txt", sc, err) && sc.events.size() == 5);
    CHECK("bad operation type rejected", !loadScenario("/tmp/bad1.txt", sc, err));
    CHECK("closure of non-existent runway rejected", !loadScenario("/tmp/bad2.txt", sc, err));
    CHECK("missing file reported", !loadScenario("/tmp/nope.txt", sc, err));
}

static void testSingleRunwaySeparation() {
    std::cout << "System: single runway, separation respected\n";
    Config cfg; cfg.numRunways = 1;
    std::vector<Event> ev = { req(1, OpType::ARRIVAL, Wake::HEAVY, Tier::NORMAL, 0), req(2, OpType::ARRIVAL, Wake::LIGHT, Tier::NORMAL, 0) };
    Result r = Scheduler(cfg, Policy::FCFS).run(ev);
    CHECK("both served", r.schedule.size() == 2);
    CHECK("heavy first starts at 0", find(r, 1)->start == 0);
    CHECK("light after heavy waits 150 s", find(r, 2)->start == 150);
    CHECK("validator passes", validate(r, cfg, 2).empty());
}

static void testEmergencyPriority() {
    std::cout << "System: emergency overtakes queue\n";
    Config cfg; cfg.numRunways = 1;
    std::vector<Event> ev;
    for (int i = 1; i <= 5; ++i) ev.push_back(req(i, OpType::ARRIVAL, Wake::MEDIUM, Tier::NORMAL, 0));
    ev.push_back(req(99, OpType::ARRIVAL, Wake::MEDIUM, Tier::EMERGENCY, 30));
    Result fc = Scheduler(cfg, Policy::FCFS).run(ev), op = Scheduler(cfg, Policy::OPTIMIZED).run(ev);
    CHECK("optimized: emergency served right after the one already committed", find(op, 99)->start <= 60 + 60);
    CHECK("optimized: emergency waits less than under FCFS", find(op, 99)->waitTime() < find(fc, 99)->waitTime());
    CHECK("both validate", validate(fc, cfg, 6).empty() && validate(op, cfg, 6).empty());
}

static void testClosure() {
    std::cout << "System: runway closure\n";
    Config cfg; cfg.numRunways = 2;
    std::vector<Event> ev;
    for (int i = 1; i <= 6; ++i) ev.push_back(req(i, OpType::DEPARTURE, Wake::MEDIUM, Tier::NORMAL, i * 5));
    Event c; c.kind = EvType::CLOSE; c.runway = 1; c.time = 0; ev.push_back(c);
    Event o; o.kind = EvType::OPEN;  o.runway = 1; o.time = 600; ev.push_back(o);
    Result r = Scheduler(cfg, Policy::OPTIMIZED).run(ev);
    bool onClosed = false; for (auto& a : r.schedule) if (a.runway == 1 && a.start < 600) onClosed = true;
    CHECK("no operation on closed runway", !onClosed);
    CHECK("all aircraft still served via runway 0", r.schedule.size() == 6 && r.unserved == 0);
    CHECK("closure window recorded", r.closures.size() == 1 && r.closures[0].from == 0 && r.closures[0].to == 600);
    CHECK("validator passes", validate(r, cfg, 6).empty());
}

static void testAllClosed() {
    std::cout << "System: all runways closed, then reopened\n";
    Config cfg; cfg.numRunways = 1;
    std::vector<Event> ev = { req(1, OpType::ARRIVAL, Wake::MEDIUM, Tier::NORMAL, 10) };
    Event c; c.kind = EvType::CLOSE; c.runway = 0; c.time = 0; ev.push_back(c);
    Event o; o.kind = EvType::OPEN;  o.runway = 0; o.time = 300; ev.push_back(o);
    Result r = Scheduler(cfg, Policy::OPTIMIZED).run(ev);
    CHECK("aircraft held until reopen", r.schedule.size() == 1 && r.schedule[0].start == 300);
    std::vector<Event> ev2 = { req(1, OpType::ARRIVAL, Wake::MEDIUM, Tier::NORMAL, 10) };
    Event c2; c2.kind = EvType::CLOSE; c2.runway = 0; c2.time = 0; ev2.push_back(c2);
    Result r2 = Scheduler(cfg, Policy::OPTIMIZED).run(ev2);
    CHECK("never reopened -> reported unserved, no crash", r2.unserved == 1 && r2.schedule.empty());
}

static void testWeather() {
    std::cout << "System: weather stretches separation\n";
    Config cfg; cfg.numRunways = 1;
    std::vector<Event> ev = { req(1, OpType::ARRIVAL, Wake::MEDIUM, Tier::NORMAL, 0), req(2, OpType::ARRIVAL, Wake::MEDIUM, Tier::NORMAL, 0) };
    Event w; w.kind = EvType::WEATHER_ON; w.time = 0; w.factor = 2.0; ev.push_back(w);
    Result r = Scheduler(cfg, Policy::OPTIMIZED).run(ev);
    CHECK("second arrival delayed by 2 x 60 = 120 s", find(r, 2)->start - find(r, 1)->start == 120);
    CHECK("validator passes", validate(r, cfg, 2).empty());
}

static void testDynamicArrival() {
    std::cout << "System: late request is not scheduled before it exists\n";
    Config cfg; cfg.numRunways = 1;
    std::vector<Event> ev = { req(1, OpType::ARRIVAL, Wake::MEDIUM, Tier::NORMAL, 0), req(2, OpType::DEPARTURE, Wake::MEDIUM, Tier::NORMAL, 500) };
    Result r = Scheduler(cfg, Policy::OPTIMIZED).run(ev);
    CHECK("aircraft 2 starts at its request time 500", find(r, 2)->start == 500);
}

static void testRandomisedValidity() {
    std::cout << "System: 60 random scenarios, both policies must validate\n";
    int bad = 0;
    for (unsigned seed = 1; seed <= 30; ++seed) {
        for (int runways : {1, 3}) {
            Scenario sc = generateScenario(80, seed, runways);
            int n = 0; for (auto& e : sc.events) if (e.kind == EvType::REQUEST) ++n;
            for (Policy p : {Policy::FCFS, Policy::OPTIMIZED}) {
                Result r = Scheduler(sc.cfg, p).run(sc.events);
                if (!validate(r, sc.cfg, n).empty() || r.unserved != 0) ++bad;
            }
        }
    }
    CHECK("zero violations across 120 runs", bad == 0);
}

static void testOptimizationHelps() {
    std::cout << "System: optimized beats FCFS on average\n";
    double sf = 0, so = 0; int better = 0, total = 0;
    for (unsigned seed = 1; seed <= 20; ++seed) {
        Scenario sc = generateScenario(120, seed, 2);
        Metrics f = computeMetrics(Scheduler(sc.cfg, Policy::FCFS).run(sc.events));
        Metrics o = computeMetrics(Scheduler(sc.cfg, Policy::OPTIMIZED).run(sc.events));
        sf += f.avgWait; so += o.avgWait; ++total; if (o.avgWait <= f.avgWait) ++better;
    }
    std::cout << "    mean avg-wait FCFS " << sf / total << " s vs optimized " << so / total << " s; optimized better in " << better << "/" << total << " runs\n";
    CHECK("optimized mean wait <= FCFS mean wait", so <= sf);
}

int main() {
    testSeparation(); testParser(); testSingleRunwaySeparation(); testEmergencyPriority();
    testClosure(); testAllClosed(); testWeather(); testDynamicArrival(); testRandomisedValidity(); testOptimizationHelps();
    std::cout << "\nResult: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
