// report.h - metrics, independent validator, printing and CSV export (no scheduling logic)
#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <ostream>

struct Metrics {
    int    served = 0, unserved = 0;
    double avgWait = 0, maxWait = 0, p95Wait = 0;
    double avgWaitEmergency = -1, avgWaitFuel = -1, avgWaitNormal = -1;
    double throughputPerHour = 0;
    std::vector<double> utilization;      // percent per runway
    double avgUtilization = 0;
    int    makespan = 0;
};

Metrics computeMetrics(const Result& r);
std::vector<std::string> validate(const Result& r, const Config& cfg, int expectedAircraft);

void printSchedule(const Result& r, std::ostream& os, int maxRows = -1);
void printMetrics(const std::string& title, const Metrics& m, std::ostream& os);
void printComparison(const Metrics& fcfs, const Metrics& opt, std::ostream& os);
bool writeCsv(const Result& r, const std::string& path);
