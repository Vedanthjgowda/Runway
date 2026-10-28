#include "scheduler.h"
#include "separation.h"
#include <queue>
#include <climits>
#include <algorithm>
#include <tuple>

namespace {

// ---- Event queue: min-heap on time (non-request events before requests at equal time)
struct EventCmp {
    bool operator()(const Event& a, const Event& b) const {
        if (a.time != b.time) return a.time > b.time;
        int ra = a.kind == EvType::REQUEST, rb = b.kind == EvType::REQUEST;
        if (ra != rb) return ra > rb;
        return a.ac.id > b.ac.id;
    }
};
using EventQueue = std::priority_queue<Event, std::vector<Event>, EventCmp>;

// ---- Waiting pool: one heap per (operation type, wake category) = 6 heaps.
// Inside a heap: OPTIMIZED orders by (tier, request); FCFS orders by request only.
struct PoolCmp {
    bool fcfs;
    bool operator()(const Aircraft& a, const Aircraft& b) const {
        if (!fcfs && a.tier != b.tier) return (int)a.tier > (int)b.tier;
        if (a.request != b.request) return a.request > b.request;
        return a.id > b.id;
    }
};
using PoolHeap = std::priority_queue<Aircraft, std::vector<Aircraft>, PoolCmp>;

struct RunwayState {
    bool   open = true;
    bool   used = false;
    int    lastStart = 0;
    OpType lastType = OpType::DEPARTURE;
    Wake   lastWake = Wake::MEDIUM;
    int    freeAt = 0;
    int    busy = 0;
    int    closedSince = 0;
};

inline int poolIndex(const Aircraft& a) { return (int)a.type * 3 + (int)a.wake; }

} // namespace

Result Scheduler::run(const std::vector<Event>& evIn) const {
    const bool fcfs = (policy_ == Policy::FCFS);
    const int  R = cfg_.numRunways;

    Result res;
    res.busy.assign(R, 0);

    EventQueue events;
    for (const auto& e : evIn) events.push(e);

    std::vector<PoolHeap> pool(6, PoolHeap(PoolCmp{fcfs}));
    int pooled = 0;
    std::vector<RunwayState> rw(R);

    double weather = 1.0;
    int  clock = 0;
    int  lastAnyStart = 0;
    bool anyStarted = false;

    auto applyDue = [&](int upTo) {
        while (!events.empty() && events.top().time <= upTo) {
            Event e = events.top(); events.pop();
            switch (e.kind) {
                case EvType::REQUEST:
                    pool[poolIndex(e.ac)].push(e.ac); ++pooled; break;
                case EvType::CLOSE:
                    if (e.runway >= 0 && e.runway < R && rw[e.runway].open) {
                        rw[e.runway].open = false;
                        rw[e.runway].closedSince = e.time;
                    }
                    break;
                case EvType::OPEN:
                    if (e.runway >= 0 && e.runway < R && !rw[e.runway].open) {
                        rw[e.runway].open = true;
                        res.closures.push_back({e.runway, rw[e.runway].closedSince, e.time});
                    }
                    break;
                case EvType::WEATHER_ON:  weather = e.factor; break;
                case EvType::WEATHER_OFF: weather = 1.0; break;
            }
        }
    };

    while (true) {
        applyDue(clock);                                     // invariant: all events <= clock applied

        if (pooled == 0) {                                   // idle: jump to next event
            if (events.empty()) break;
            clock = std::max(clock, events.top().time);
            continue;
        }

        int minFree = INT_MAX; bool anyOpen = false;
        for (int r = 0; r < R; ++r)
            if (rw[r].open) { anyOpen = true; minFree = std::min(minFree, rw[r].freeAt); }
        if (!anyOpen) {                                      // every runway closed: wait for a reopen
            if (events.empty()) break;
            clock = std::max(clock, events.top().time);
            continue;
        }
        const int D = std::max(clock, minFree);              // decision instant

        // ---- gather candidates: the top of each non-empty heap
        std::vector<const Aircraft*> cands;
        for (auto& h : pool) if (!h.empty()) cands.push_back(&h.top());
        if (!fcfs) {                                         // strict priority between tiers
            int bestTier = 99;
            for (auto* c : cands) bestTier = std::min(bestTier, (int)c->tier);
            cands.erase(std::remove_if(cands.begin(), cands.end(),
                        [&](const Aircraft* c) { return (int)c->tier != bestTier; }), cands.end());
        } else {                                             // FCFS: only the oldest request
            const Aircraft* oldest = cands[0];
            for (auto* c : cands)
                if (std::tie(c->request, c->id) < std::tie(oldest->request, oldest->id)) oldest = c;
            cands.assign(1, oldest);
        }

        // ---- evaluate every (candidate, open runway) pair
        const Aircraft* bestC = nullptr; int bestR = -1, bestStart = 0, bestSep = 0;
        double bestScore = 0;
        for (auto* c : cands) {
            for (int r = 0; r < R; ++r) {
                if (!rw[r].open) continue;
                int s = std::max(D, c->request), sep = 0;
                if (rw[r].used) {
                    sep = separation(rw[r].lastType, rw[r].lastWake, c->type, c->wake, weather);
                    s = std::max(s, rw[r].lastStart + sep);
                }
                if (anyStarted) s = std::max(s, lastAnyStart + cfg_.crossRunwayGap);

                double score = fcfs ? (double)s : s - cfg_.aging * (double)(D - c->request);
                bool better = !bestC || score < bestScore - 1e-9 ||
                    (std::abs(score - bestScore) <= 1e-9 &&
                     std::tie(c->tier, c->request, c->id, r) <
                     std::tie(bestC->tier, bestC->request, bestC->id, bestR));
                if (better) { bestC = c; bestR = r; bestStart = s; bestSep = sep; bestScore = score; }
            }
        }

        // ---- commit only if no unseen event happens before the chosen start time
        if (!events.empty() && events.top().time <= bestStart) {
            clock = std::max(clock, events.top().time);
            continue;                                        // re-plan with the new information
        }

        Aircraft a = *bestC;
        pool[poolIndex(a)].pop(); --pooled;

        a.start = bestStart; a.runway = bestR; a.sepApplied = bestSep; a.weatherFactor = weather;
        RunwayState& st = rw[bestR];
        int occ = (a.type == OpType::ARRIVAL) ? cfg_.occupancyArr : cfg_.occupancyDep;
        st.used = true; st.lastStart = bestStart; st.lastType = a.type; st.lastWake = a.wake;
        st.freeAt = bestStart + occ; st.busy += occ;
        lastAnyStart = bestStart; anyStarted = true;
        res.makespan = std::max(res.makespan, bestStart + occ);
        res.schedule.push_back(a);
        clock = D;
    }

    for (int r = 0; r < R; ++r) {
        res.busy[r] = rw[r].busy;
        if (!rw[r].open) res.closures.push_back({r, rw[r].closedSince, INT_MAX});
    }
    res.unserved = pooled;
    return res;
}
