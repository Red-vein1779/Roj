// Roj chess engine — Phase 2, Step 1: score-convention unit test.
//
// Verifies the mate-distance rebasing of src/value.h: value_from_tt(value_to_tt(
// v, ply), ply) == v for ordinary AND mate scores at every ply, that a mate-in-N
// round-trips to the exact mate distance, and that every produced stored value
// fits in int16_t (the TT will store int16_t in Step 6). No TT is built or used.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_value.cpp -o test_value

#include "../src/value.h"

#include <cstdint>
#include <iostream>
#include <limits>

using namespace roj;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

static bool fits_int16(int v) {
    return v >= static_cast<int>(std::numeric_limits<std::int16_t>::min())
        && v <= static_cast<int>(std::numeric_limits<std::int16_t>::max());
}

int main() {
    // 1) Ordinary (non-mate) scores: to_tt leaves them unchanged, the round-trip
    //    is the identity, and they fit int16_t — at every ply.
    const int normals[] = {
        0, 1, -1, 50, -50, 100, -100, 900, -900, 5000, -5000,
        VALUE_MATE_IN_MAX_PLY - 1, VALUE_MATED_IN_MAX_PLY + 1
    };
    for (int v : normals)
        for (int ply = 0; ply <= MAX_PLY; ++ply) {
            const int stored = value_to_tt(v, ply);
            check(stored == v,                          "ordinary score unchanged by to_tt");
            check(value_from_tt(stored, ply) == v,      "ordinary round-trip");
            check(fits_int16(stored),                   "ordinary stored fits int16");
        }

    // 2) Mate scores: a mate FOR us in n plies is VALUE_MATE - n; a mate AGAINST
    //    us in n plies is -VALUE_MATE + n. Each must round-trip to itself (the
    //    correct distance) at every ply, and every stored form fits int16_t.
    for (int n = 0; n <= MAX_PLY; ++n) {
        const int mate_for     =  VALUE_MATE - n;
        const int mate_against = -VALUE_MATE + n;
        for (int ply = 0; ply <= MAX_PLY; ++ply) {
            const int sf = value_to_tt(mate_for, ply);
            const int sa = value_to_tt(mate_against, ply);
            check(value_from_tt(sf, ply) == mate_for,     "mate-for round-trip");
            check(value_from_tt(sa, ply) == mate_against, "mate-against round-trip");
            check(fits_int16(sf),                         "mate-for stored fits int16");
            check(fits_int16(sa),                         "mate-against stored fits int16");
        }
    }

    // 3) Concrete distance check: a mate-in-3 (root score VALUE_MATE - 3) stored
    //    at ply 5 and probed back at ply 5 reads as VALUE_MATE - 3 — a recovered
    //    mate distance of exactly 3.
    {
        const int mate_in_3 = VALUE_MATE - 3;
        const int stored    = value_to_tt(mate_in_3, 5);
        const int recovered = value_from_tt(stored, 5);
        check(recovered == mate_in_3,        "mate-in-3 round-trips exactly");
        check(VALUE_MATE - recovered == 3,   "recovered mate distance == 3");
    }

    if (g_failures == 0) {
        std::cout << "test_value: ALL VALUE-CONVENTION CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_value: FAILURES = " << g_failures << "\n";
    return 1;
}
