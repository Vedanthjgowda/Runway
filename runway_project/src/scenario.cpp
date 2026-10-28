#include "scenario.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <random>
#include <algorithm>

static bool parseWake(char c, Wake& w) {
    switch (c) { case 'L': w = Wake::LIGHT; return true; case 'M': w = Wake::MEDIUM; return true;
                 case 'H': w = Wake::HEAVY; return true; } return false;
}
static bool parseTier(char c, Tier& t) {
    switch (c) { case 'E': t = Tier::EMERGENCY; return true; case 'F': t = Tier::LOW_FUEL; return true;
                 case 'N': t = Tier::NORMAL; return true; } return false;
}

bool loadScenario(const std::string& path, Scenario& out, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open file: " + path; return false; }
    out = Scenario{};
    std::string line; int ln = 0;
    while (std::getline(in, line)) {
        ++ln;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kw; ss >> kw;
        auto bad = [&](const std::string& m) { err = "line " + std::to_string(ln) + ": " + m; return false; };
        if (kw == "RUNWAYS") { if (!(ss >> out.cfg.numRunways) || out.cfg.numRunways < 1) return bad("invalid runway count"); }
        else if (kw == "GAP")   { if (!(ss >> out.cfg.crossRunwayGap)) return bad("invalid GAP"); }
        else if (kw == "AGING") { if (!(ss >> out.cfg.aging)) return bad("invalid AGING"); }
        else if (kw == "AIRCRAFT") {
            Event e; e.kind = EvType::REQUEST; char t, w, p;
            if (!(ss >> e.ac.id >> e.ac.flight >> t >> w >> p >> e.ac.request)) return bad("AIRCRAFT needs: id flight A|D L|M|H E|F|N time");
            if (t != 'A' && t != 'D') return bad("type must be A or D");
            e.ac.type = (t == 'A') ? OpType::ARRIVAL : OpType::DEPARTURE;
            if (!parseWake(w, e.ac.wake)) return bad("wake must be L, M or H");
            if (!parseTier(p, e.ac.tier)) return bad("priority must be E, F or N");
            if (e.ac.request < 0) return bad("time must be >= 0");
            e.time = e.ac.request; out.events.push_back(e);
        } else if (kw == "CLOSE") {
            int r, t, d; if (!(ss >> r >> t >> d) || d <= 0) return bad("CLOSE needs: runway time duration");
            Event c; c.kind = EvType::CLOSE; c.runway = r; c.time = t; out.events.push_back(c);
            Event o; o.kind = EvType::OPEN;  o.runway = r; o.time = t + d; out.events.push_back(o);
        } else if (kw == "WEATHER") {
            int t, d; double f; if (!(ss >> t >> d >> f) || d <= 0 || f < 1.0) return bad("WEATHER needs: time duration factor(>=1)");
            Event a; a.kind = EvType::WEATHER_ON; a.time = t; a.factor = f; out.events.push_back(a);
            Event b; b.kind = EvType::WEATHER_OFF; b.time = t + d; out.events.push_back(b);
        } else return bad("unknown keyword '" + kw + "'");
    }
    for (const auto& e : out.events)
        if ((e.kind == EvType::CLOSE || e.kind == EvType::OPEN) && e.runway >= out.cfg.numRunways) {
            err = "closure refers to runway " + std::to_string(e.runway) + " but only " +
                  std::to_string(out.cfg.numRunways) + " runways exist"; return false; }
    return true;
}

Scenario generateScenario(int n, unsigned seed, int numRunways) {
    Scenario sc; sc.cfg.numRunways = numRunways;
    std::mt19937 rng(seed);
    const int horizon = std::max(600, n * 90 / std::max(1, numRunways));
    auto uni = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto prob = [&]() { return std::uniform_real_distribution<double>(0, 1)(rng); };
    static const char* carriers[] = {"AI", "6E", "UK", "SG", "QP", "EK", "LH"};

    for (int i = 1; i <= n; ++i) {
        Event e; e.kind = EvType::REQUEST; Aircraft& a = e.ac;
        a.id = i;
        a.flight = std::string(carriers[uni(0, 6)]) + std::to_string(uni(100, 999));
        a.type = prob() < 0.5 ? OpType::ARRIVAL : OpType::DEPARTURE;
        double w = prob(); a.wake = w < 0.15 ? Wake::LIGHT : (w < 0.85 ? Wake::MEDIUM : Wake::HEAVY);
        double p = prob();
        a.tier = Tier::NORMAL;
        if (a.type == OpType::ARRIVAL) { if (p < 0.02) a.tier = Tier::EMERGENCY; else if (p < 0.08) a.tier = Tier::LOW_FUEL; }
        double pk = prob();                       // two rush hours at 25% and 70% of the horizon
        int centre = pk < 0.3 ? horizon / 4 : (pk < 0.6 ? horizon * 7 / 10 : -1);
        a.request = centre < 0 ? uni(0, horizon)
                               : std::clamp(centre + (int)std::normal_distribution<double>(0, horizon / 14.0)(rng), 0, horizon);
        e.time = a.request; sc.events.push_back(e);
    }
    // dynamic conditions: one runway closure and one weather window
    if (numRunways >= 2) {
        Event c; c.kind = EvType::CLOSE; c.runway = numRunways - 1; c.time = horizon * 3 / 10; sc.events.push_back(c);
        Event o; o.kind = EvType::OPEN;  o.runway = numRunways - 1; o.time = c.time + 400;    sc.events.push_back(o);
    }
    Event a; a.kind = EvType::WEATHER_ON; a.time = horizon * 55 / 100; a.factor = 1.5; sc.events.push_back(a);
    Event b; b.kind = EvType::WEATHER_OFF; b.time = a.time + 500; sc.events.push_back(b);
    return sc;
}

void describeScenario(const Scenario& sc) {
    int req = 0; for (auto& e : sc.events) if (e.kind == EvType::REQUEST) ++req;
    std::cout << "Scenario: " << sc.cfg.numRunways << " runway(s), " << req << " aircraft requests, "
              << sc.events.size() - req << " condition events\n";
    for (auto& e : sc.events) {
        if (e.kind == EvType::CLOSE)      std::cout << "  t=" << e.time << "s  runway " << e.runway << " CLOSES\n";
        if (e.kind == EvType::OPEN)       std::cout << "  t=" << e.time << "s  runway " << e.runway << " REOPENS\n";
        if (e.kind == EvType::WEATHER_ON) std::cout << "  t=" << e.time << "s  weather: separation x" << e.factor << "\n";
        if (e.kind == EvType::WEATHER_OFF)std::cout << "  t=" << e.time << "s  weather clears\n";
    }
}
