// Roj chess engine — Phase 1, step 6 verification: FEN round-trip.
//
// Criterion: parse_fen -> fen_string must reproduce the input byte for byte,
// and the parsed position's incremental hash must equal the from-scratch oracle.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_fen.cpp src/fen.cpp src/position.cpp src/zobrist.cpp -o test_fen

#include "../src/fen.h"
#include "../src/position.h"
#include "../src/zobrist.h"

#include <iostream>
#include <string>

using namespace roj;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        const bool check_ok = (cond);                                     \
        std::cout << (check_ok ? "[PASS] " : "[FAIL] ") << #cond << '\n'; \
        if (!check_ok) ++g_failures;                                      \
    } while (0)

int main() {
    init_zobrist();

    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2",
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 4 4",
        "8/8/8/8/8/8/8/8 w - - 0 1",
    };

    for (const char* f : fens) {
        Position pos;
        const bool ok = parse_fen(pos, f);
        const std::string out = fen_string(pos);

        std::cout << "in : " << f << "\n"
                  << "out: " << out << "\n";
        CHECK(ok);
        CHECK(out == std::string(f));                          // exact round-trip
        CHECK(pos.hash == compute_hash_from_scratch(pos));     // hash consistent
        std::cout << "\n";
    }

    std::cout << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
