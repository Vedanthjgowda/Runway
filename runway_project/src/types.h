// types.h - core domain types shared by all modules (high cohesion: data only)
#pragma once
#include <string>
#include <vector>

enum class OpType  { ARRIVAL = 0, DEPARTURE = 1 };
enum class Wake    { LIGHT = 0, MEDIUM = 1, HEAVY = 2 };
enum class Tier    { EMERGENCY = 0, LOW_FUEL = 1, NORMAL = 2 };   // lower = more urgent
enum class Policy  { FCFS, OPTIMIZED };

inline const char* toStr(OpType t) { return t == OpType::ARRIVAL ? "ARR" : "DEP"; }
inline const char* toStr(Wake w)   { return w == Wake::LIGHT ? "L" : (w == Wake::MEDIUM ? "M" : "H"); }
inline const char* toStr(Tier t)   { return t == Tier::EMERGENCY ? "EMERG" : (t == Tier::LOW_FUEL ? "FUEL" : "NORMAL"); }

struct Aircraft {
    int         id = 0;
    std::string flight;
    OpType      type = OpType::ARRIVAL;
    Wake        wake = Wake::MEDIUM;
    Tier        tier = Tier::NORMAL;
    int         request = 0;          // seconds: time the request enters the system

    // results (filled by the scheduler)
    int   start = -1;
    int   runway = -1;
    int   sepApplied = 0;             // separation enforced vs previous op on the same runway
    double weatherFactor = 1.0;       // weather multiplier in force when scheduled
    int   waitTime() const { return start - request; }
};

enum class EvType { REQUEST, CLOSE, OPEN, WEATHER_ON, WEATHER_OFF };

struct Event {
    int    time = 0;
    EvType kind = EvType::REQUEST;
    int    runway = -1;               // CLOSE / OPEN
    double factor = 1.0;              // WEATHER_ON
    Aircraft ac;                      // REQUEST
};

struct ClosureWindow { int runway; int from; int to; };

struct Config {
    int    numRunways = 2;
    int    crossRunwayGap = 20;       // min seconds between starts on different runways (dependent parallels)
    double aging = 0.5;               // alpha in score = start - alpha * waited
    int    occupancyArr = 50;         // seconds a runway is occupied by an arrival
    int    occupancyDep = 40;         // ... by a departure
};

struct Scenario {
    Config             cfg;
    std::vector<Event> events;        // unsorted; scheduler heapifies them
};

struct Result {
    std::vector<Aircraft>      schedule;     // in order of commitment
    std::vector<ClosureWindow> closures;
    std::vector<int>           busy;         // busy seconds per runway
    int                        makespan = 0;
    int                        unserved = 0;
};
