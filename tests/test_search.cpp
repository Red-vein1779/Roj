// Roj chess engine — Phase 2, Step 2: negamax alpha-beta tests.
//
// Maps to phase2.md Step 2 "done when":
//  1. ORACLE GATE: at a full window, search() == minimax() EXACTLY, depths 1..4,
//     on the six perft positions plus varied FENs (both sides to move).
//  2. Mate-in-1 / mate-in-2 scores AND moves; stalemate == VALUE_DRAW.
//  3. Hand-counted depth-1 and depth-2 root scores on a minimal position.
//  4. Fail-soft return: fail-low returns < alpha (not clamped); fail-high returns
//     >= beta and may strictly exceed it (not clamped).
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_search.cpp src/search.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_search

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

#include <iostream>
#include <string>

using namespace roj;

static int g_failures = 0;

static void check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

// ---- 1. Oracle gate: full-window alpha-beta == minimax, depths 1..4 ----------
static void test_oracle() {
    const char* fens[] = {
        // The six standard perft positions.
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        // Varied, both sides to move.
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
        "rnbqkb1r/pppp1ppp/5n2/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 2 3",
        "8/8/4k3/8/8/2K5/5R2/3b4 b - - 0 1",
        "8/5k2/8/8/8/8/2K5/8 w - - 0 1"
    };
    int mismatches = 0;
    for (const char* fen : fens)
        for (int depth = 1; depth <= 4; ++depth) {
            Position a; parse_fen(a, fen);
            Position b; parse_fen(b, fen);
            const int ab = search(a, depth, -VALUE_INFINITE, VALUE_INFINITE, 0);
            const int mm = minimax(b, depth, 0);
            if (ab != mm) {
                ++mismatches; ++g_failures;
                std::cout << "  FAIL oracle [" << fen << "] depth " << depth
                          << ": ab=" << ab << " minimax=" << mm << "\n";
            }
        }
    if (mismatches == 0)
        std::cout << "  oracle: full-window alpha-beta == minimax for depths 1..4 on all positions\n";
}

// ---- 2. Mate / terminal tests ------------------------------------------------
static void test_mates() {
    // Mate in 1: 1.Ra8#. Black king g8 is shut in by its own f7/g7/h7 pawns; the
    // rook check along the 8th rank cannot be answered. Score = VALUE_MATE - 1.
    {
        Position p; parse_fen(p, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
        const SearchResult r = search_root(p, 2);
        check(r.score == VALUE_MATE - 1,
              "mate-in-1 score == 31999 (got " + std::to_string(r.score) + ")");
        check(move_to_uci(r.best) == "a1a8",
              "mate-in-1 best move == a1a8 (got " + move_to_uci(r.best) + ")");
    }
    // Mate in 2 (forced): 1.Qf8+ Kh7 2.Qg7#. The white king on f6 covers g7, so
    // the queen mates with king support. With the ply convention the root score
    // is VALUE_MATE - 3 = 31997 (a mate scored 3 plies from the root).
    {
        // A forced mate in 2 with several mating keys, so instead of hard-coding
        // one move we (a) check the mate SCORE and (b) verify the returned move is
        // A correct key: after it, the side to move is mated in 2 plies, scoring
        // -(VALUE_MATE - 2) = -31998. One line is 1.Qg2! (threat 2.Qg7#); both
        // 1...Kg8 and 1...Kh7 allow 2.Qg7# (the f6 king guards g7).
        Position p; parse_fen(p, "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1");
        const SearchResult r = search_root(p, 4);
        std::cout << "  mate-in-2 score=" << r.score
                  << " move=" << move_to_uci(r.best) << "\n";
        check(r.score >= VALUE_MATE_IN_MAX_PLY,
              "mate-in-2 is a winning mate score (>= 31754)");
        check(r.score == VALUE_MATE - 3,
              "mate-in-2 score == 31997 (got " + std::to_string(r.score) + ")");
        check(r.best != MOVE_NONE, "mate-in-2 returns a move");
        make_move(p, r.best);
        const SearchResult r2 = search_root(p, 3);
        unmake_move(p, r.best);
        check(r2.score == -(VALUE_MATE - 2),
              "after the key the defender is mated in 2 plies (-31998), got "
              + std::to_string(r2.score));
    }
    // Stalemate: black to move, no legal moves, NOT in check -> VALUE_DRAW.
    // Black Kh8; white Qf7 covers g8/g7/h7, white Kg6 supports — h8 not attacked.
    {
        Position p; parse_fen(p, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        const SearchResult r = search_root(p, 3);
        check(r.score == VALUE_DRAW,
              "stalemate score == 0 (got " + std::to_string(r.score) + ")");
    }
}

// ---- 3. Hand-counted shallow tree --------------------------------------------
// Minimal position: White Kh1, Black Kh8 (kings only). Material is 0 on both
// sides, so only the king PST matters. From the Step 1 KING table (square a1=0):
//   g1=30, h1=25, g2=20, h2=18 ; black reads square ^ 56.
// eval(start, White to move) = PST_K[h1] - PST_K[h8^56=h1] = 25 - 25 = 0.
//
// DEPTH 1: White king moves h1 -> {g1,g2,h2}. The child (Black to move) has
// white-relative value PST_K[wk] - PST_K[h1] = PST_K[wk] - 25, and the move's
// root value is -eval(child) = PST_K[wk] - 25:
//   g1 -> +5,  g2 -> -5,  h2 -> -7.   depth-1 root score = max = +5 (move h1g1).
//
// DEPTH 2: after White's wk, Black (from h8) replies bk2 in {g8,g7,h7}; the
// grandchild (White to move) is PST_K[wk] - PST_K[bk2^56], with bk2^56 in
// {g1,g2,h2} = {30,20,18}. Black minimises White's value, i.e. picks the reply
// maximising PST_K[bk2^56] = 30 (bk2=g8). So the move's value = PST_K[wk] - 30:
//   g1 -> 0,  g2 -> -10,  h2 -> -12.  depth-2 root score = max = 0 (move h1g1).
static void test_hand_counted() {
    const char* fen = "7k/8/8/8/8/8/8/7K w - - 0 1";
    {
        Position p; parse_fen(p, fen);
        const SearchResult r = search_root(p, 1);
        check(r.score == 5,
              "hand depth-1 score == 5 (got " + std::to_string(r.score) + ")");
        check(move_to_uci(r.best) == "h1g1",
              "hand depth-1 best == h1g1 (got " + move_to_uci(r.best) + ")");
    }
    {
        Position p; parse_fen(p, fen);
        const SearchResult r = search_root(p, 2);
        check(r.score == 0,
              "hand depth-2 score == 0 (got " + std::to_string(r.score) + ")");
        check(move_to_uci(r.best) == "h1g1",
              "hand depth-2 best == h1g1 (got " + move_to_uci(r.best) + ")");
    }
}

// ---- 4. Fail-soft return -----------------------------------------------------
// Same kings-only position; its depth-1 minimax value is +5 (above). We call
// search() directly with windows that do not contain 5.
static void test_fail_soft() {
    const char* fen = "7k/8/8/8/8/8/8/7K w - - 0 1";

    // FAIL-LOW: window entirely ABOVE the true value. Fail-soft returns the true
    // best (5), strictly BELOW alpha; fail-hard would clamp it up to alpha (100).
    {
        Position p; parse_fen(p, fen);
        const int v = search(p, 1, /*alpha=*/100, /*beta=*/200, /*ply=*/0);
        check(v == 5,  "fail-low returns true value 5 (got " + std::to_string(v) + ")");
        check(v < 100, "fail-low return < alpha (not clamped to alpha)");
    }

    // FAIL-HIGH: window entirely BELOW the true value. The search takes a beta
    // cutoff and returns a value that is >= beta and STRICTLY exceeds it (5 > -100);
    // fail-hard would clamp the return down to beta (-100).
    {
        Position p; parse_fen(p, fen);
        const int v = search(p, 1, /*alpha=*/-200, /*beta=*/-100, /*ply=*/0);
        check(v >= -100, "fail-high return >= beta");
        check(v >  -100, "fail-high return strictly exceeds beta (not clamped to beta)");
        check(v == 5,    "fail-high returns true best 5 here (got " + std::to_string(v) + ")");
    }
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    test_oracle();
    test_mates();
    test_hand_counted();
    test_fail_soft();

    if (g_failures == 0) {
        std::cout << "test_search: ALL STEP 2 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_search: FAILURES = " << g_failures << "\n";
    return 1;
}
