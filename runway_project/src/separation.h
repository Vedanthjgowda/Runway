// separation.h - wake-turbulence / mixed-mode separation rules (stateless, zero coupling)
#pragma once
#include "types.h"
#include <cmath>

// Minimum seconds between the START of the leader and the START of the follower.
// Illustrative values inspired by ICAO wake categories, simplified for a teaching model.
inline int baseSeparation(OpType lt, Wake lw, OpType ft, Wake fw) {
    if (lt == OpType::ARRIVAL && ft == OpType::ARRIVAL) {
        static const int AA[3][3] = {   // leader row, follower col : L, M, H
            { 60, 60, 60 },
            { 90, 60, 60 },
            {150, 120, 90 } };
        return AA[(int)lw][(int)fw];
    }
    if (lt == OpType::DEPARTURE && ft == OpType::DEPARTURE) {
        static const int DD[3][3] = {
            { 60, 60, 60 },
            { 90, 60, 60 },
            {120, 90, 60 } };
        return DD[(int)lw][(int)fw];
    }
    return 60;                                     // arrival<->departure mixed operations
}

inline int separation(OpType lt, Wake lw, OpType ft, Wake fw, double weatherFactor) {
    return (int)std::ceil(baseSeparation(lt, lw, ft, fw) * weatherFactor);
}
