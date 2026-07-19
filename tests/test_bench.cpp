// Roj chess engine — Phase 2, Step 10: `bench` node-signature tests.
//
// Maps to phase2.md Step 10 "done when" (§7 row 10, §6 bench, §12):
//
//  1. DETERMINISTIC: run_bench() returns the identical total node count on repeated
//     calls in one build.
//  2. REFERENCE SIGNATURE: that count equals the value committed in bench.cpp
//     (Stockfish-style), so an accidental search change is caught as a changed count.
//
// (Rebuild stability and the sensitivity check — perturb the search, confirm the
// number moves, revert — are demonstrated at the command level in the Step 10
// report; the reference number is the anchor asserted here.)
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_bench.cpp src/bench.cpp src/search.cpp src/tt.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp src/bitboard.cpp -o test_bench

#include "../src/bench.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"

#include <cstdint>
#include <iostream>

using namespace roj;

// The reference signature committed in src/bench.cpp. Keep the two in lock-step.
// Phase 3 Step 1: re-baselined for PVS (depth unchanged at 6, phase3.md §3.3 as
// amended); the Phase 2 signature at this depth was 7948336, Step 1's 4643314.
// Phase 3 Step 2: aspiration windows on the play path — same-depth widening
// re-searches change the fixed-depth node count (+2.2%, 4746412).
// Phase 3 Step 3: check extension — in-check nodes searched one ply deeper
// changes the fixed-nominal-depth node count (+31.8%, 6254236).
// Phase 3 Step 4: null move pruning on the play path (-45.0%, 3437418).
// Phase 3 Step 5: LMR on the play path (-52.9%, 1617309 at depth 6).
// Post-Block-B re-baseline (phase3.md §3.3 as amended): depth raised 6 -> 7.
// (Step 7 SEE-qsearch pruning was measured at 2911321 / 2890141 and PARKED —
// phase3.md §9; the signature returns to the Block B baseline.)
// Phase 3 Step 8: SEE capture ordering on the play path (+0.5% — LMR/order
// interplay at fixed depth; 3403889 at the base variant). §3.7 retune (soft
// demotion, losing captures between killers and quiets): -5.9%.
static constexpr std::uint64_t BENCH_REFERENCE = 3184757ULL;

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    const std::uint64_t a = run_bench(/*verbose=*/false);
    const std::uint64_t b = run_bench(/*verbose=*/false);

    int failures = 0;
    if (a != b) {
        ++failures;
        std::cout << "  FAIL: bench not deterministic across runs: " << a << " vs " << b << "\n";
    }
    if (a != BENCH_REFERENCE) {
        ++failures;
        std::cout << "  FAIL: bench count " << a << " != reference " << BENCH_REFERENCE
                  << " (search changed? update the reference if intentional)\n";
    }

    std::cout << "  bench nodes = " << a << " (reference " << BENCH_REFERENCE
              << "), identical on repeat\n";

    if (failures == 0) {
        std::cout << "test_bench: ALL STEP 10 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_bench: FAILURES = " << failures << "\n";
    return 1;
}
