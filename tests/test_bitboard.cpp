// Roj chess engine — Phase 1, step 2 verification: bitboard primitives.
//
// Standalone test (NOT part of the engine binary). It does two things:
//   1. Renders single corner squares so we can visually confirm board
//      orientation — the classic bitboard bug.
//   2. Asserts popcount / lsb / pop_lsb / set / clear / test against
//      hand-counted bitboards.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_bitboard.cpp src/bitboard.cpp -o test_bitboard

#include "../src/bitboard.h"

#include <iostream>

using namespace roj;

static int g_failures = 0;

// A reporting assertion: prints PASS/FAIL with the checked expression so a
// successful run still shows evidence, and records failures for the exit code.
#define CHECK(cond)                                                  \
    do {                                                             \
        const bool ok = (cond);                                     \
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << #cond << '\n'; \
        if (!ok) ++g_failures;                                      \
    } while (0)

int main() {
    // --- orientation: a single corner bit must land in the right corner ---
    std::cout << "=== Orientation: a8 set (expect TOP-LEFT '1') ===";
    Bitboard bb = EMPTY_BB;
    set_bit(bb, SQ_A8);
    print_bitboard(bb);

    std::cout << "=== Orientation: h1 set (expect BOTTOM-RIGHT '1') ===";
    bb = EMPTY_BB;
    set_bit(bb, SQ_H1);
    print_bitboard(bb);

    // --- assertions on hand-counted bitboards ---
    std::cout << "=== Assertions ===\n";

    // Three squares: a1 (idx 0), c1 (idx 2), h8 (idx 63).
    bb = EMPTY_BB;
    set_bit(bb, SQ_A1);
    set_bit(bb, SQ_C1);
    set_bit(bb, SQ_H8);
    CHECK(popcount(bb) == 3);
    CHECK(lsb(bb) == SQ_A1);

    const Square first = pop_lsb(bb);     // removes a1
    CHECK(first == SQ_A1);
    CHECK(popcount(bb) == 2);
    CHECK(lsb(bb) == SQ_C1);              // next lowest is now c1

    // set / clear / test round-trip on a single square (e4, idx 28).
    Bitboard one = EMPTY_BB;
    set_bit(one, SQ_E4);
    CHECK(test_bit(one, SQ_E4));
    CHECK(popcount(one) == 1);
    clear_bit(one, SQ_E4);
    CHECK(!test_bit(one, SQ_E4));
    CHECK(one == EMPTY_BB);

    // a couple of fixed masks counted by hand.
    CHECK(popcount(0xFFULL) == 8);        // rank 1 = bits 0..7
    CHECK(lsb(0xFFULL) == SQ_A1);
    CHECK(popcount(FULL_BB) == 64);       // every square set

    std::cout << '\n'
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
