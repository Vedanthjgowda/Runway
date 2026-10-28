#include "report.h"
#include "separation.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

Metrics computeMetrics(const Result& r) {
    Metrics m; m.served = (int)r.schedule.size(); m.unserved = r.unserved; m.makespan = r.makespan;
    std::vector<double> waits; double sum[3] = {0, 0, 0}; int cnt[3] = {0, 0, 0};
    for (auto& a : r.schedule) {
        double w = a.waitTime(); waits.push_back(w);
        m.avgWait += w; m.maxWait = std::max(m.maxWait, w);
        sum[(int)a.tier] += w; cnt[(int)a.tier]++;
    }
    if (!waits.empty()) {
        m.avgWait /= waits.size();
        std::sort(waits.begin(), waits.end());
        m.p95Wait = waits[std::min(waits.size() - 1, (size_t)std::ceil(0.95 * waits.size()) - 1)];
    }
    if (cnt[0]) m.avgWaitEmergency = sum[0] / cnt[0];
    if (cnt[1]) m.avgWaitFuel      = sum[1] / cnt[1];
    if (cnt[2]) m.avgWaitNormal    = sum[2] / cnt[2];
    if (r.makespan > 0) {
        m.throughputPerHour = 3600.0 * m.served / r.makespan;
        for (int b : r.busy) m.utilization.push_back(100.0 * b / r.makespan);
        for (double u : m.utilization) m.avgUtilization += u;
        if (!m.utilization.empty()) m.avgUtilization /= m.utilization.size();
    }
    return m;
}

// Independent re-check of the produced schedule (does NOT reuse scheduler internals).
std::vector<std::string> validate(const Result& r, const Config& cfg, int expected) {
    std::vector<std::string> v;
    if ((int)r.schedule.size() + r.unserved != expected)
        v.push_back("aircraft count mismatch: served+unserved != input");
    std::map<int, int> seen;
    for (auto& a : r.schedule) if (++seen[a.id] > 1) v.push_back("aircraft " + std::to_string(a.id) + " scheduled twice");

    for (auto& a : r.schedule) {
        if (a.start < a.request) v.push_back("aircraft " + std::to_string(a.id) + " starts before its request");
        for (auto& c : r.closures)
            if (c.runway == a.runway && a.start >= c.from && a.start < c.to)
                v.push_back("aircraft " + std::to_string(a.id) + " uses closed runway " + std::to_string(a.runway));
    }
    // per-runway separation
    for (int rw = 0; rw < cfg.numRunways; ++rw) {
        std::vector<const Aircraft*> ops;
        for (auto& a : r.schedule) if (a.runway == rw) ops.push_back(&a);
        std::sort(ops.begin(), ops.end(), [](auto* x, auto* y) { return x->start < y->start; });
        for (size_t i = 1; i < ops.size(); ++i) {
            int need = separation(ops[i-1]->type, ops[i-1]->wake, ops[i]->type, ops[i]->wake, ops[i]->weatherFactor);
            if (ops[i]->start - ops[i-1]->start < need)
                v.push_back("separation violated on runway " + std::to_string(rw) + " between " +
                            std::to_string(ops[i-1]->id) + " and " + std::to_string(ops[i]->id));
        }
    }
    // cross-runway stagger
    std::vector<const Aircraft*> all;
    for (auto& a : r.schedule) all.push_back(&a);
    std::sort(all.begin(), all.end(), [](auto* x, auto* y) { return x->start < y->start; });
    for (size_t i = 1; i < all.size(); ++i)
        if (all[i]->runway != all[i-1]->runway && all[i]->start - all[i-1]->start < cfg.crossRunwayGap)
            v.push_back("cross-runway gap violated between " + std::to_string(all[i-1]->id) + " and " + std::to_string(all[i]->id));
    return v;
}

void printSchedule(const Result& r, std::ostream& os, int maxRows) {
    std::vector<const Aircraft*> o;
    for (auto& a : r.schedule) o.push_back(&a);
    std::sort(o.begin(), o.end(), [](auto* x, auto* y) { return x->start < y->start; });
    os << std::left << std::setw(5) << "ID" << std::setw(9) << "Flight" << std::setw(5) << "Op" << std::setw(5) << "Wk"
       << std::setw(8) << "Prio" << std::setw(9) << "Request" << std::setw(8) << "Start" << std::setw(8) << "Wait"
       << std::setw(7) << "RWY" << "Sep\n" << std::string(70, '-') << "\n";
    int n = 0;
    for (auto* a : o) {
        if (maxRows >= 0 && n++ >= maxRows) { os << "... (" << o.size() - maxRows << " more rows)\n"; break; }
        os << std::left << std::setw(5) << a->id << std::setw(9) << a->flight << std::setw(5) << toStr(a->type)
           << std::setw(5) << toStr(a->wake) << std::setw(8) << toStr(a->tier) << std::setw(9) << a->request
           << std::setw(8) << a->start << std::setw(8) << a->waitTime() << std::setw(7) << a->runway << a->sepApplied << "\n";
    }
}

static std::string f1(double v) { std::ostringstream s; s << std::fixed << std::setprecision(1) << v; return s.str(); }

void printMetrics(const std::string& title, const Metrics& m, std::ostream& os) {
    os << "\n--- " << title << " ---\n"
       << "Served: " << m.served << "   Unserved: " << m.unserved << "   Makespan: " << m.makespan << " s\n"
       << "Wait (s): avg " << f1(m.avgWait) << ", p95 " << f1(m.p95Wait) << ", max " << f1(m.maxWait) << "\n"
       << "Avg wait by priority (s): emergency " << (m.avgWaitEmergency < 0 ? "n/a" : f1(m.avgWaitEmergency))
       << ", low-fuel " << (m.avgWaitFuel < 0 ? "n/a" : f1(m.avgWaitFuel))
       << ", normal " << (m.avgWaitNormal < 0 ? "n/a" : f1(m.avgWaitNormal)) << "\n"
       << "Throughput: " << f1(m.throughputPerHour) << " ops/hour   Avg runway utilisation: " << f1(m.avgUtilization) << "%\n";
    for (size_t i = 0; i < m.utilization.size(); ++i) os << "  Runway " << i << " utilisation: " << f1(m.utilization[i]) << "%\n";
}

void printComparison(const Metrics& a, const Metrics& b, std::ostream& os) {
    auto row = [&](const std::string& name, double x, double y, bool lowerBetter) {
        double d = x == 0 ? 0 : 100.0 * (y - x) / x;
        os << std::left << std::setw(30) << name << std::right << std::setw(10) << f1(x) << std::setw(12) << f1(y)
           << std::setw(10) << (std::abs(d) < 0.05 ? "same" : ((lowerBetter ? d < 0 : d > 0) ? "better" : "worse")) << std::setw(9) << (f1(d) + "%") << "\n";
    };
    os << "\n=========== FCFS baseline vs OPTIMIZED scheduler ===========\n"
       << std::left << std::setw(30) << "Metric" << std::right << std::setw(10) << "FCFS" << std::setw(12) << "Optimized"
       << std::setw(10) << "Verdict" << std::setw(9) << "Change" << "\n" << std::string(71, '-') << "\n";
    row("Average wait (s)", a.avgWait, b.avgWait, true);
    row("95th percentile wait (s)", a.p95Wait, b.p95Wait, true);
    row("Maximum wait (s)", a.maxWait, b.maxWait, true);
    if (a.avgWaitEmergency >= 0 && b.avgWaitEmergency >= 0) row("Emergency avg wait (s)", a.avgWaitEmergency, b.avgWaitEmergency, true);
    if (a.avgWaitFuel >= 0 && b.avgWaitFuel >= 0) row("Low-fuel avg wait (s)", a.avgWaitFuel, b.avgWaitFuel, true);
    row("Makespan (s)", a.makespan, b.makespan, true);
    row("Throughput (ops/hour)", a.throughputPerHour, b.throughputPerHour, false);
    row("Avg runway utilisation (%)", a.avgUtilization, b.avgUtilization, false);
}

bool writeCsv(const Result& r, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "id,flight,operation,wake,priority,request_s,start_s,wait_s,runway,separation_s,weather_factor\n";
    std::vector<const Aircraft*> o;
    for (auto& a : r.schedule) o.push_back(&a);
    std::sort(o.begin(), o.end(), [](auto* x, auto* y) { return x->start < y->start; });
    for (auto* a : o)
        f << a->id << ',' << a->flight << ',' << toStr(a->type) << ',' << toStr(a->wake) << ',' << toStr(a->tier) << ','
          << a->request << ',' << a->start << ',' << a->waitTime() << ',' << a->runway << ',' << a->sepApplied << ',' << a->weatherFactor << '\n';
    return true;
}
