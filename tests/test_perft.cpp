// Roj chess engine — Phase 1, step 14: THE PERFT GATE (non-negotiable gate to Phase 2).
//
// Six standard positions, two passes:
//   PASS 1 — hash invariant ON (verify_hash=true), depths 1-4, all six: proves
//            DoD requirement 2 (pos.hash == compute_hash_from_scratch at every
//            node) across the whole suite. Completing without the assertion
//            firing == the invariant held on every node of every position.
//   PASS 2 — headline depths, hash invariant OFF for speed (already proven by
//            Pass 1 and the step-13 tripwire): the counts ARE the gate.
//
// Node counts are the published Chess Programming Wiki values (cross-checked live
// against the wiki). They are the source of truth — never adjusted to the engine.
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

#include <chrono>
#include <cstdint>
#include <iostream>

using namespace roj;

static int g_failures = 0;

struct PerftPos {
    const char*   name;
    const char*   fen;
    int           target_depth;
    std::uint64_t expected[7];   // indexed by depth 1..6 (index 0 unused)
};

static const PerftPos POSITIONS[6] = {
    {"P1 Start",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     6, {0, 20, 400, 8902, 197281, 4865609, 119060324}},
    {"P2 Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
     5, {0, 48, 2039, 97862, 4085603, 193690690}},
    {"P3",          "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
     6, {0, 14, 191, 2812, 43238, 674624, 11030083}},
    {"P4",          "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
     5, {0, 6, 264, 9467, 422333, 15833292}},
    {"P5",          "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
     5, {0, 44, 1486, 62379, 2103487, 89941194}},
    {"P6",          "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
     5, {0, 46, 2079, 89890, 3894594, 164075551}},
};

static void run_depth(Position& p, const PerftPos& P, int depth, bool verify) {
    const std::uint64_t got = perft(p, depth, verify);
    const bool ok = (got == P.expected[depth]);
    if (!ok) ++g_failures;
    std::cout << "    d" << depth << " = " << got
              << "  (expected " << P.expected[depth] << ")  "
              << (ok ? "PASS" : "FAIL") << "\n";
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    std::cout << "########## PASS 1 — hash invariant ON, depths 1-4, all six ##########\n";
    for (const PerftPos& P : POSITIONS) {
        std::cout << "-- " << P.name << " --\n";
        Position p;
        parse_fen(p, P.fen);
        for (int d = 1; d <= 4; ++d)
            run_depth(p, P, d, /*verify=*/true);
    }
    std::cout << "[Pass 1 completed without the hash-invariant assertion firing on any node.]\n";

    std::cout << "\n########## PASS 2 — headline depths, hash invariant OFF (timed) ##########\n";
    for (const PerftPos& P : POSITIONS) {
        std::cout << "-- " << P.name << " (target depth " << P.target_depth << ") --\n";
        Position p;
        parse_fen(p, P.fen);
        const auto t0 = std::chrono::steady_clock::now();
        for (int d = 1; d <= P.target_depth; ++d)
            run_depth(p, P, d, /*verify=*/false);
        const auto t1 = std::chrono::steady_clock::now();
        const long long ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "    [" << P.name << " elapsed: " << ms << " ms]\n";
    }

    std::cout << "\n########## divide demo: start position, depth 2 ##########\n";
    { Position p; parse_fen(p, POSITIONS[0].fen); perft_divide(p, 2, /*verify=*/false); }

    std::cout << "\n";
    if (g_failures == 0)
        std::cout << "ALL SIX POSITIONS MATCH — PERFT GATE: PASS\n";
    else
        std::cout << "SOME COUNTS WRONG — PERFT GATE: FAIL (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
