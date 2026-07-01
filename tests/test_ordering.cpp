// Roj chess engine — Phase 2, Step 3: MVV-LVA capture-ordering tests.
//
// Maps to phase2.md Step 3 "done when":
//  1. ORDERING UNIT: on a position with several captures of differing victim /
//     aggressor values, order_moves() puts captures first in MVV-LVA order and
//     all captures before every quiet move.
//  2. RESULT-INVARIANCE GATE: at a full window, depths 1..4, on the Step 2
//     positions, ordered score == minimax == unordered score; the best move is
//     stable (identical, or a genuine tie where both moves are optimal).
//  3. NODE-COUNT REDUCTION: on tactical positions at depth 4, nodes(ON) <=
//     nodes(OFF), reported per position.
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_ordering.cpp src/search.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_ordering

#include "../src/search.h"
#include "../src/eval.h"
#include "../src/value.h"
#include "../src/movegen.h"
#include "../src/position.h"
#include "../src/fen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"
#include "../src/types.h"

#include <cstdint>
#include <iostream>
#include <string>

using namespace roj;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

// The Step 2 test positions (six perft + varied, both sides to move).
static const char* STEP2_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "rnbqkb1r/pppp1ppp/5n2/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 2 3",
    "8/8/4k3/8/8/2K5/5R2/3b4 b - - 0 1",
    "8/5k2/8/8/8/8/2K5/8 w - - 0 1"
};

// ---- 1. Ordering unit test ---------------------------------------------------
// FEN "4k3/8/8/1r1q4/2P5/8/8/3RK3 w - - 0 1":
//   Black Qd5, Rb5. White Pc4, Rd1, Ke1. Legal captures:
//     c4xd5 (PxQ), d1xd5 (RxQ), c4xb5 (PxR).
//   MVV-LVA scores (victim*16 - aggressor, ordinals P=1 N=2 B=3 R=4 Q=5 K=6):
//     c4xd5 = 5*16-1 = 79   (PxQ)
//     d1xd5 = 5*16-4 = 76   (RxQ  — same victim, heavier aggressor -> after PxQ)
//     c4xb5 = 4*16-1 = 63   (PxR  — lower victim -> after both queen captures)
//   Expected order: c4d5, d1d5, c4b5, then every quiet move.
static void test_ordering_unit() {
    Position pos; parse_fen(pos, "4k3/8/8/1r1q4/2P5/8/8/3RK3 w - - 0 1");
    MoveList ml; generate_legal_moves(pos, ml);
    order_moves(pos, ml);

    check(ml.count >= 4, "position has both captures and quiet moves");
    check(move_to_uci(ml.moves[0]) == "c4d5" && capture_score(pos, ml.moves[0]) == 79,
          "1st capture = c4d5 PxQ (79), got " + move_to_uci(ml.moves[0]));
    check(move_to_uci(ml.moves[1]) == "d1d5" && capture_score(pos, ml.moves[1]) == 76,
          "2nd capture = d1d5 RxQ (76), got " + move_to_uci(ml.moves[1]));
    check(move_to_uci(ml.moves[2]) == "c4b5" && capture_score(pos, ml.moves[2]) == 63,
          "3rd capture = c4b5 PxR (63), got " + move_to_uci(ml.moves[2]));
    check(capture_score(pos, ml.moves[3]) == 0, "4th move is a quiet move (score 0)");

    // Scores must be non-increasing across the whole list, and no capture may
    // appear after a quiet move (captures strictly precede quiets).
    bool nonincreasing = true, seen_quiet = false, capture_after_quiet = false;
    int prev = capture_score(pos, ml.moves[0]);
    for (int i = 0; i < ml.count; ++i) {
        const int s = capture_score(pos, ml.moves[i]);
        if (s > prev) nonincreasing = false;
        if (s == 0) seen_quiet = true;
        else if (seen_quiet) capture_after_quiet = true;
        prev = s;
    }
    check(nonincreasing, "ordering scores are non-increasing");
    check(!capture_after_quiet, "no capture appears after a quiet move");
}

// ---- 2. Result-invariance gate ----------------------------------------------
// Full-window value of playing m in the unordered search (for tie verification).
static int root_value_of(Position& pos, Move m, int depth) {
    make_move(pos, m);
    SearchInfo info; info.use_mvv_lva = false;
    const int v = -search(pos, depth - 1, -VALUE_INFINITE, VALUE_INFINITE, 1, info);
    unmake_move(pos, m);
    return v;
}

static void test_invariance() {
    int mismatches = 0;
    for (const char* fen : STEP2_FENS)
        for (int depth = 1; depth <= 4; ++depth) {
            Position pa; parse_fen(pa, fen);
            Position pb; parse_fen(pb, fen);
            Position pc; parse_fen(pc, fen);
            SearchInfo on;  on.use_mvv_lva  = true;
            SearchInfo off; off.use_mvv_lva = false;
            const SearchResult ro = search_root(pa, depth, on);
            const SearchResult ru = search_root(pb, depth, off);
            const int mm = minimax(pc, depth, 0);

            if (ro.score != mm || ru.score != mm) {
                ++mismatches; ++g_failures;
                std::cout << "  FAIL invariance [" << fen << "] d" << depth
                          << ": ordered=" << ro.score << " unordered=" << ru.score
                          << " minimax=" << mm << "\n";
            }
            // Best move: identical, or a genuine tie (both moves are optimal).
            if (ro.best != ru.best) {
                Position pd; parse_fen(pd, fen);
                const int vo = root_value_of(pd, ro.best, depth);
                const int vu = root_value_of(pd, ru.best, depth);
                check(vo == mm && vu == mm,
                      std::string("differing best moves are both optimal [") + fen + "]");
            }
        }
    if (mismatches == 0)
        std::cout << "  invariance: ordered == minimax == unordered for depths 1..4 on all positions\n";
}

// ---- 3. Node-count reduction -------------------------------------------------
static void test_node_reduction() {
    struct Tac { const char* name; const char* fen; };
    const Tac tac[] = {
        {"Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
        {"P4",       "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
        {"P5",       "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"},
        {"P6",       "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"}
    };
    const int depth = 4;
    for (const Tac& t : tac) {
        Position pa; parse_fen(pa, t.fen);
        Position pb; parse_fen(pb, t.fen);
        SearchInfo on;  on.use_mvv_lva  = true;
        SearchInfo off; off.use_mvv_lva = false;
        const SearchResult ro = search_root(pa, depth, on);
        const SearchResult ru = search_root(pb, depth, off);
        const double pct = off.nodes
            ? 100.0 * static_cast<double>(off.nodes - on.nodes) / static_cast<double>(off.nodes)
            : 0.0;
        std::cout << "  " << t.name << " d" << depth
                  << ": nodes ON=" << on.nodes << " OFF=" << off.nodes
                  << "  (-" << pct << "%)  score=" << ro.score << "\n";
        check(on.nodes <= off.nodes, std::string("nodes ON <= OFF [") + t.name + "]");
        check(ro.score == ru.score, std::string("score unchanged by ordering [") + t.name + "]");
    }
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    test_ordering_unit();
    test_invariance();
    test_node_reduction();

    if (g_failures == 0) {
        std::cout << "test_ordering: ALL STEP 3 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_ordering: FAILURES = " << g_failures << "\n";
    return 1;
}
