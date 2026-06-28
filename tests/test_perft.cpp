// Roj chess engine — Phase 1, step 13 verification: perft TOOL correctness.
//
// This checks the tool runs and divide works, using only the two positions whose
// values we independently verified earlier in this project (start, Kiwipete).
// It is NOT the gate — the gate (step 14) pulls all six positions' values live.
// The hash invariant is ON (verify_hash = true) for every run here.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_perft.cpp src/perft.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_perft

#include "../src/perft.h"
#include "../src/position.h"
#include "../src/fen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"
#include "../src/types.h"

#include <cstdint>
#include <iostream>

using namespace roj;

static int g_failures = 0;

static void check_perft(const char* fen, int depth, std::uint64_t expected) {
    Position p;
    parse_fen(p, fen);
    const std::uint64_t got = perft(p, depth, /*verify_hash=*/true);
    const bool ok = (got == expected);
    if (!ok) ++g_failures;
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << "perft(" << depth << ") = " << got
              << " (expected " << expected << ")\n";
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    const char* START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const char* KIWI  = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

    std::cout << "=== start position, perft to depth 5 (hash invariant ON) ===\n";
    check_perft(START, 1, 20);
    check_perft(START, 2, 400);
    check_perft(START, 3, 8902);
    check_perft(START, 4, 197281);
    check_perft(START, 5, 4865609);

    std::cout << "\n=== Kiwipete, perft to depth 4 (hash invariant ON) ===\n";
    check_perft(KIWI, 1, 48);
    check_perft(KIWI, 2, 2039);
    check_perft(KIWI, 3, 97862);
    check_perft(KIWI, 4, 4085603);

    std::cout << "\n=== divide: start position depth 2 (each root move -> 20, total 400) ===\n";
    { Position p; parse_fen(p, START); perft_divide(p, 2, /*verify_hash=*/true); }

    std::cout << "\n"
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
