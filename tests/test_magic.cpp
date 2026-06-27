// Roj chess engine — Phase 1, step 4 verification: magic bitboards.
//
// Strategy: the magic lookups must agree with the ray-tracing oracle for the
// named spot cases AND across a broad random sweep. Zero mismatches is the gate.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_magic.cpp src/magic.cpp src/bitboard.cpp -o test_magic

#include "../src/magic.h"
#include "../src/bitboard.h"

#include <cstdint>
#include <iostream>

using namespace roj;

static int g_failures = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        const bool ok = (cond);                                     \
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << #cond << '\n'; \
        if (!ok) ++g_failures;                                      \
    } while (0)

int main() {
    init_magics();

    // ---------- named spot cases (magic == oracle), with pictures ----------
    std::cout << "=== ROOK e4, empty board (whole rank 4 + e-file, minus e4) ===";
    print_bitboard(rook_attacks(SQ_E4, EMPTY_BB));
    CHECK(rook_attacks(SQ_E4, EMPTY_BB) == rook_attacks_slow(SQ_E4, EMPTY_BB));

    std::cout << "=== ROOK a1, empty board ===";
    print_bitboard(rook_attacks(SQ_A1, EMPTY_BB));
    CHECK(rook_attacks(SQ_A1, EMPTY_BB) == rook_attacks_slow(SQ_A1, EMPTY_BB));

    Bitboard occR = EMPTY_BB;
    set_bit(occR, SQ_E6);
    set_bit(occR, SQ_C4);
    std::cout << "=== ROOK e4, blockers e6 & c4 (stops AT e6 and c4) ===";
    print_bitboard(rook_attacks(SQ_E4, occR));
    CHECK(rook_attacks(SQ_E4, occR) == rook_attacks_slow(SQ_E4, occR));
    CHECK(test_bit(rook_attacks(SQ_E4, occR), SQ_E6));     // sees the blocker
    CHECK(test_bit(rook_attacks(SQ_E4, occR), SQ_C4));
    CHECK(!test_bit(rook_attacks(SQ_E4, occR), SQ_E7));    // not beyond it
    CHECK(!test_bit(rook_attacks(SQ_E4, occR), SQ_B4));

    std::cout << "=== BISHOP e4, empty board ===";
    print_bitboard(bishop_attacks(SQ_E4, EMPTY_BB));
    CHECK(bishop_attacks(SQ_E4, EMPTY_BB) == bishop_attacks_slow(SQ_E4, EMPTY_BB));

    Bitboard occB = EMPTY_BB;
    set_bit(occB, SQ_C6);
    set_bit(occB, SQ_G6);
    std::cout << "=== BISHOP e4, blockers c6 & g6 ===";
    print_bitboard(bishop_attacks(SQ_E4, occB));
    CHECK(bishop_attacks(SQ_E4, occB) == bishop_attacks_slow(SQ_E4, occB));
    CHECK(test_bit(bishop_attacks(SQ_E4, occB), SQ_C6));
    CHECK(test_bit(bishop_attacks(SQ_E4, occB), SQ_G6));
    CHECK(!test_bit(bishop_attacks(SQ_E4, occB), SQ_B7));  // beyond c6
    CHECK(!test_bit(bishop_attacks(SQ_E4, occB), SQ_H7));  // beyond g6

    // ---------- broad random sweep: magic == oracle ----------
    std::uint64_t rs = 0x9E3779B97F4A7C15ULL;
    auto rnd = [&rs]() -> std::uint64_t {
        rs ^= rs >> 12;
        rs ^= rs << 25;
        rs ^= rs >> 27;
        return rs * 0x2545F4914F6CDD1DULL;
    };

    const int N = 100000;     // requirement is 1000; we run far more
    int errors = 0;
    for (int i = 0; i < N; ++i) {
        const Square s    = static_cast<Square>(rnd() % 64);
        const Bitboard occ = rnd() & rnd();   // mixed-density occupancy
        if (rook_attacks(s, occ)   != rook_attacks_slow(s, occ))   ++errors;
        if (bishop_attacks(s, occ) != bishop_attacks_slow(s, occ)) ++errors;
    }
    std::cout << "\nRandom sweep: " << N << " (square, occupancy) pairs, rook+bishop each = "
              << (2 * N) << " comparisons -> " << errors << " errors\n";
    CHECK(errors == 0);

    std::cout << '\n'
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
