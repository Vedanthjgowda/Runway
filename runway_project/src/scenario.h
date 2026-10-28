// scenario.h - build a Scenario from a text file or from a seeded random generator
#pragma once
#include "types.h"
#include <string>

bool   loadScenario(const std::string& path, Scenario& out, std::string& err);
Scenario generateScenario(int numAircraft, unsigned seed, int numRunways);
void   describeScenario(const Scenario& sc);
