# Runway Operation Scheduler (C++17)

Discrete-event simulator that sequences arrivals and departures on one or more runways,
minimising waiting time while enforcing wake-turbulence separation, under dynamic conditions
(new requests, runway closures, weather). Compares an **FCFS baseline** with an **optimized
scheduler** and proves every schedule conflict-free with an independent validator.

## Build and run
    make                      # builds runway_sim and run_tests
    make test                 # 26 unit + system tests
    make demo                 # sample scenario (data/sample.txt)
    ./runway_sim --generate 200 --runways 2 --seed 7
    ./runway_sim --scenario data/sample.txt --csv schedule.csv
    ./runway_sim --interactive

## Scenario file format
    RUNWAYS 2 | GAP 20 | AGING 0.5
    AIRCRAFT <id> <flight> <A|D> <L|M|H> <E|F|N> <request seconds>
    CLOSE <runway> <start> <duration>
    WEATHER <start> <duration> <separation multiplier >= 1>

## Modules (coupling / cohesion)
| File | Responsibility | Depends on |
|---|---|---|
| types.h | Plain data: Aircraft, Event, Config, Result | nothing |
| separation.h | Pure function: wake / mixed-mode separation | types |
| scheduler.h/.cpp | Engine: event loop, waiting pool, runway state | types, separation |
| scenario.h/.cpp | Parse files, generate random traffic | types |
| report.h/.cpp | Metrics, validator, tables, CSV | types, separation |
| main.cpp | CLI and interactive menu | all (front end only) |

## Data structures
- Min-heap of events (closure, weather, request) ordered by time: O(log n)
- Six waiting heaps, one per (operation x wake category), ordered by (priority tier, request time).
  A decision compares only the 6 heap tops against R runways: O(6R + log n), never a scan of the pool.
- 2D separation matrices: O(1) lookup.

## Algorithm
1. Apply every event up to the current clock (requests join the pool, closures/weather change state).
2. Decision time D = max(clock, earliest moment an open runway is physically free).
3. Candidates = heap tops of the most urgent tier present (Emergency > Low-fuel > Normal).
4. For each (candidate, open runway) compute the earliest legal start:
   max(D, request, lastStart_on_runway + separation, lastStart_anywhere + cross-runway gap).
5. Pick the pair with the lowest score = start - aging x (D - request). The aging term prevents starvation.
6. Commit only if no unseen event occurs before that start; otherwise advance to the event and re-plan.

## Validator checks
No start before request, no duplicate aircraft, per-runway separation (with weather factor),
cross-runway stagger, no operation inside a closure window, served + unserved = input.

## Simplifications (state these in the report)
Illustrative separation values, not real ICAO tables; no taxiway/gate model; runway-mode is mixed
for all runways; a committed slot is not re-optimised (frozen-sequence assumption, as in real AMAN/DMAN).
