// Roj chess engine — Phase 1, step 3 verification: leaper attack tables.
//
// Standalone test. Builds the knight / king / pawn tables via
// init_attack_tables(), checks the hand-verifiable cases, and prints a few
// boards so the attack sets can be seen with the naked eye.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_attacks.cpp src/attacks.cpp src/bitboard.cpp -o test_attacks

#include "../src/attacks.h"
#include "../src/bitboard.h"

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
    init_attack_tables();

    // --- visual evidence ---
    std::cout << "=== KNIGHT on e4 (expect 8 targets) ===";
    print_bitboard(KNIGHT_ATTACKS[SQ_E4]);
    std::cout << "=== KING on a1 (corner, expect 3 targets) ===";
    print_bitboard(KING_ATTACKS[SQ_A1]);
    std::cout << "=== WHITE PAWN on e2 (expect d3 & f3, NOT e3) ===";
    print_bitboard(PAWN_ATTACKS[WHITE][SQ_E2]);

    std::cout << "=== Assertions ===\n";

    // knight
    CHECK(popcount(KNIGHT_ATTACKS[SQ_E4]) == 8);
    CHECK(popcount(KNIGHT_ATTACKS[SQ_A1]) == 2);
    CHECK(test_bit(KNIGHT_ATTACKS[SQ_A1], SQ_B3));
    CHECK(test_bit(KNIGHT_ATTACKS[SQ_A1], SQ_C2));

    // king
    CHECK(popcount(KING_ATTACKS[SQ_E4]) == 8);
    CHECK(popcount(KING_ATTACKS[SQ_A1]) == 3);
    CHECK(test_bit(KING_ATTACKS[SQ_A1], SQ_A2));
    CHECK(test_bit(KING_ATTACKS[SQ_A1], SQ_B1));
    CHECK(test_bit(KING_ATTACKS[SQ_A1], SQ_B2));

    // pawns — diagonal only, colour-dependent direction
    CHECK(popcount(PAWN_ATTACKS[WHITE][SQ_E2]) == 2);
    CHECK(test_bit(PAWN_ATTACKS[WHITE][SQ_E2], SQ_D3));
    CHECK(test_bit(PAWN_ATTACKS[WHITE][SQ_E2], SQ_F3));
    CHECK(!test_bit(PAWN_ATTACKS[WHITE][SQ_E2], SQ_E3));   // never straight ahead

    CHECK(popcount(PAWN_ATTACKS[BLACK][SQ_E7]) == 2);
    CHECK(test_bit(PAWN_ATTACKS[BLACK][SQ_E7], SQ_D6));
    CHECK(test_bit(PAWN_ATTACKS[BLACK][SQ_E7], SQ_F6));

    std::cout << '\n'
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
