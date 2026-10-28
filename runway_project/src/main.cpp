// main.cpp - command-line front end: batch mode (file / random) and interactive menu
#include "scenario.h"
#include "scheduler.h"
#include "report.h"
#include <iostream>
#include <limits>
#include <string>
#include <cstdlib>

static void usage() {
    std::cout <<
R"(Runway Operation Scheduler  (Data Structures micro-project)

Usage:
  runway_sim --scenario <file>            run a scenario file
  runway_sim --generate <N> [options]     run N randomly generated aircraft
  runway_sim --interactive                menu-driven mode
Options:
  --runways <R>      number of runways for --generate (default 2)
  --seed <S>         random seed for --generate (default 42)
  --policy fcfs|opt|both   which strategy to run (default both)
  --rows <K>         rows of the schedule table to print (default 25, -1 = all)
  --csv <file>       export the OPTIMIZED schedule (or FCFS if --policy fcfs) to CSV
)";
}

static int runAndReport(const Scenario& sc, const std::string& policy, int rows, const std::string& csv) {
    describeScenario(sc);
    int expected = 0; for (auto& e : sc.events) if (e.kind == EvType::REQUEST) ++expected;
    Result rf, ro; Metrics mf, mo; bool bad = false;

    auto check = [&](const char* name, const Result& r) {
        auto v = validate(r, sc.cfg, expected);
        std::cout << "Validator (" << name << "): " << (v.empty() ? "PASS - no separation, closure or timing violations" : "FAIL") << "\n";
        for (auto& s : v) std::cout << "   ! " << s << "\n";
        if (!v.empty()) bad = true;
    };

    if (policy != "opt") { rf = Scheduler(sc.cfg, Policy::FCFS).run(sc.events); mf = computeMetrics(rf); }
    if (policy != "fcfs") { ro = Scheduler(sc.cfg, Policy::OPTIMIZED).run(sc.events); mo = computeMetrics(ro); }

    if (policy != "fcfs") {
        std::cout << "\n=============== OPTIMIZED SCHEDULE ===============\n";
        printSchedule(ro, std::cout, rows); printMetrics("Optimized metrics", mo, std::cout); check("optimized", ro);
    }
    if (policy != "opt") {
        if (policy == "fcfs") { std::cout << "\n=============== FCFS SCHEDULE ===============\n"; printSchedule(rf, std::cout, rows); }
        printMetrics("FCFS metrics", mf, std::cout); check("FCFS", rf);
    }
    if (policy == "both") printComparison(mf, mo, std::cout);

    if (!csv.empty()) {
        const Result& out = (policy == "fcfs") ? rf : ro;
        std::cout << (writeCsv(out, csv) ? "\nSchedule exported to " + csv + "\n" : "\nCould not write " + csv + "\n");
    }
    return bad ? 2 : 0;
}

// ---------------- interactive mode ----------------
static int readInt(const std::string& prompt, int lo, int hi) {
    while (true) {
        std::cout << prompt; int v;
        if (std::cin >> v && v >= lo && v <= hi) return v;
        if (std::cin.eof()) return lo;
        std::cin.clear(); std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "  please enter a number between " << lo << " and " << hi << "\n";
    }
}
static char readChar(const std::string& prompt, const std::string& allowed) {
    while (true) {
        std::cout << prompt; std::string s; std::cin >> s;
        if (std::cin.eof()) return allowed[0];
        if (s.size() == 1 && allowed.find((char)toupper(s[0])) != std::string::npos) return (char)toupper(s[0]);
        std::cout << "  allowed: " << allowed << "\n";
    }
}

static int interactive() {
    Scenario sc; int nextId = 1;
    sc.cfg.numRunways = readInt("Number of runways (1-6): ", 1, 6);
    while (true) {
        std::cout << "\n1) Add aircraft  2) Schedule runway closure  3) Add weather window\n"
                     "4) Add random traffic  5) Show scenario  6) RUN scheduler  0) Exit\n";
        int c = readInt("Choice: ", 0, 6);
        if (c == 0) return 0;
        if (c == 1) {
            Event e; e.kind = EvType::REQUEST; Aircraft& a = e.ac; a.id = nextId++;
            std::cout << "Flight number: "; std::cin >> a.flight;
            a.type = readChar("Operation (A=arrival, D=departure): ", "AD") == 'A' ? OpType::ARRIVAL : OpType::DEPARTURE;
            char w = readChar("Wake category (L/M/H): ", "LMH"); a.wake = w == 'L' ? Wake::LIGHT : (w == 'M' ? Wake::MEDIUM : Wake::HEAVY);
            char p = readChar("Priority (E=emergency, F=low fuel, N=normal): ", "EFN"); a.tier = p == 'E' ? Tier::EMERGENCY : (p == 'F' ? Tier::LOW_FUEL : Tier::NORMAL);
            a.request = readInt("Request time in seconds: ", 0, 1000000); e.time = a.request;
            sc.events.push_back(e);
        } else if (c == 2) {
            int r = readInt("Runway number: ", 0, sc.cfg.numRunways - 1), t = readInt("Closure starts at (s): ", 0, 1000000), d = readInt("Duration (s): ", 1, 1000000);
            Event a; a.kind = EvType::CLOSE; a.runway = r; a.time = t; sc.events.push_back(a);
            Event b; b.kind = EvType::OPEN; b.runway = r; b.time = t + d; sc.events.push_back(b);
        } else if (c == 3) {
            int t = readInt("Weather starts at (s): ", 0, 1000000), d = readInt("Duration (s): ", 1, 1000000), f = readInt("Separation multiplier in percent (100-300): ", 100, 300);
            Event a; a.kind = EvType::WEATHER_ON; a.time = t; a.factor = f / 100.0; sc.events.push_back(a);
            Event b; b.kind = EvType::WEATHER_OFF; b.time = t + d; sc.events.push_back(b);
        } else if (c == 4) {
            int n = readInt("How many aircraft (1-500): ", 1, 500);
            Scenario g = generateScenario(n, (unsigned)std::rand(), sc.cfg.numRunways);
            for (auto e : g.events) { if (e.kind == EvType::REQUEST) { e.ac.id += nextId - 1; } sc.events.push_back(e); }
            nextId += n;
        } else if (c == 5) describeScenario(sc);
        else if (c == 6) runAndReport(sc, "both", 25, "");
    }
}

int main(int argc, char** argv) {
    std::string scenarioFile, policy = "both", csv; int gen = 0, runways = 2, rows = 25; unsigned seed = 42; bool inter = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { usage(); std::exit(1); } return argv[++i]; };
        if (a == "--scenario") scenarioFile = next();
        else if (a == "--generate") gen = std::atoi(next().c_str());
        else if (a == "--runways") runways = std::atoi(next().c_str());
        else if (a == "--seed") seed = (unsigned)std::atoi(next().c_str());
        else if (a == "--policy") policy = next();
        else if (a == "--rows") rows = std::atoi(next().c_str());
        else if (a == "--csv") csv = next();
        else if (a == "--interactive") inter = true;
        else { usage(); return 1; }
    }
    if (policy != "fcfs" && policy != "opt" && policy != "both") { usage(); return 1; }
    if (inter) return interactive();

    Scenario sc;
    if (!scenarioFile.empty()) {
        std::string err;
        if (!loadScenario(scenarioFile, sc, err)) { std::cerr << "Error: " << err << "\n"; return 1; }
    } else if (gen > 0) {
        if (runways < 1) { std::cerr << "Error: --runways must be >= 1\n"; return 1; }
        sc = generateScenario(gen, seed, runways);
    } else { usage(); return 1; }
    return runAndReport(sc, policy, rows, csv);
}
