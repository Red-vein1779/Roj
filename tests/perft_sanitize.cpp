// Roj chess engine — dedicated perft driver for the SANITIZER build.
//
// This is NOT the perft gate. The gate is tests/test_perft.cpp (run at -O3),
// which proves the deep published leaf counts (start d6, Kiwipete d5, ...).
//
// This driver is the target for the AddressSanitizer + UBSan build on Linux, and
// the UBSan trap-mode build on Windows. It cannot be linked with src/main.cpp
// (two main() functions), so the sanitizer compiles this file plus the engine
// modules perft depends on. It runs the six standard positions to depth 4 — the
// start position also to depth 5 — with the from-scratch Zobrist hash invariant
// ON at every node (perft(..., verify_hash=true)). That exercises make/unmake,
// legal move generation, the magics and the hash path under the sanitizers while
// staying shallow enough (DoD section 8: "depth 4-5") to finish in minutes under
// ASan. Any wrong count, or any failed hash assertion, yields a non-zero exit;
// combined with -fno-sanitize-recover=all the exit code is the whole signal.
//
// Build (Linux, full ASan + UBSan) -- one line:
//   g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer tests/perft_sanitize.cpp src/perft.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o /tmp/perft_sanitize
// Run:
//   UBSAN_OPTIONS=print_stacktrace=1 /tmp/perft_sanitize ; echo "EXIT=$?"

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

namespace {

struct Check {
    const char*   name;
    const char*   fen;
    int           depth;
    std::uint64_t expected;   // published Chess Programming Wiki count
};

// Same source-of-truth counts as tests/test_perft.cpp (the gate). Depths are
// capped at 4-5 so the sanitizer run is fast; the deep counts live in the gate.
const Check CHECKS[] = {
    {"P1 Start",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",                 4, 197281},
    {"P1 Start",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",                 5, 4865609},
    {"P2 Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",     4, 4085603},
    {"P3",          "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",                                4, 43238},
    {"P4",          "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",         4, 422333},
    {"P5",          "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",                4, 2103487},
    {"P6",          "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
};

} // namespace

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    int failures = 0;
    std::cout << "Sanitizer perft driver -- hash invariant ON, depths 4-5:\n";
    for (const Check& c : CHECKS) {
        Position p;
        parse_fen(p, c.fen);
        const std::uint64_t got = perft(p, c.depth, /*verify_hash=*/true);
        const bool ok = (got == c.expected);
        if (!ok) ++failures;
        std::cout << "  " << c.name << " d" << c.depth << " = " << got
                  << " (expected " << c.expected << ")  "
                  << (ok ? "PASS" : "FAIL") << "\n";
    }

    if (failures == 0) {
        std::cout << "ALL CHECKS PASS\n";
        return 0;
    }
    std::cout << "CHECKS FAILED (" << failures << ")\n";
    return 1;
}
