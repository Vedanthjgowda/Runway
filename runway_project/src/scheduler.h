// scheduler.h - discrete-event runway scheduling engine
#pragma once
#include "types.h"

// The ONLY module that talks to every other part: it consumes events, owns the
// waiting pool + runway state, and produces a Result. Everything else is data or
// pure functions (data coupling only).
class Scheduler {
public:
    Scheduler(const Config& cfg, Policy policy) : cfg_(cfg), policy_(policy) {}
    Result run(const std::vector<Event>& events) const;

private:
    Config cfg_;
    Policy policy_;
};
