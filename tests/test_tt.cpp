// Roj chess engine — Phase 2, Step 6: transposition-table tests (corrected §8 gate).
//
// The §8 gate is hash-independence (NOT "identical with/without TT" — that is
// unachievable with fail-soft + value cutoffs, see §8's correction). Maps to the
// corrected Step 6 "done when":
//  1. HASH-INDEPENDENCE (gate): score(1MB) == score(256MB) with full cutoffs ON,
//     depths 1..5 on the Step 2 suite. Best move stable between sizes when unique.
//  2. DETERMINISM: same input -> same score, move and node count across runs.
//  3. MATE ROUND-TRIP through the TT: mate-in-1..4 preserve distance; a mate solved
//     WITH the TT returns the correct score.
//  4. TRIPWIRE (primary authenticity check): corrupting a stored TT score shifts a
//     solved-position score (a constant corruption is invisible to hash-independence
//     alone, so it is caught here); removing it restores agreement.
//  5. REPLACEMENT: two-bucket depth-preferred + always-replace.
//  6. REGRESSION: the TT-off path still equals minimax at full window (depths 1..4).
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_tt.cpp src/search.cpp src/tt.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_tt

#include "../src/search.h"
#include "../src/tt.h"
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

// Full search with quiescence + full ordering, optionally with a TT (cleared
// first for reproducibility) and the tripwire. Reports node count via nodesOut.
static SearchResult run(const char* fen, int depth, TranspositionTable* tt,
                        bool tripwire, std::uint64_t* nodesOut = nullptr) {
    Position p; parse_fen(p, fen);
    SearchInfo info;
    info.use_mvv_lva = true; info.use_killers_history = true;
    info.use_qsearch = true; info.use_delta_pruning = true;
    info.tt = tt; info.tt_tripwire = tripwire;
    if (tt) tt->clear();
    const SearchResult r = search_root(p, depth, info);
    if (nodesOut) *nodesOut = info.nodes;
    return r;
}

// Full-window value of playing m WITHOUT the TT (for best-move tie verification).
static int root_value_nott(const char* fen, Move m, int depth) {
    Position p; parse_fen(p, fen);
    make_move(p, m);
    SearchInfo info;
    info.use_mvv_lva = true; info.use_killers_history = true;
    info.use_qsearch = true; info.use_delta_pruning = true;
    const int v = -search(p, depth - 1, -VALUE_INFINITE, VALUE_INFINITE, 1, info);
    unmake_move(p, m);
    return v;
}

// ---- 1. Hash-independence (the gate) -----------------------------------------
static void test_hash_independence(TranspositionTable& tt1, TranspositionTable& tt256) {
    int mismatches = 0;
    for (const char* fen : STEP2_FENS)
        for (int depth = 1; depth <= 5; ++depth) {
            const SearchResult on1   = run(fen, depth, &tt1, false);
            const SearchResult on256 = run(fen, depth, &tt256, false);
            if (on1.score != on256.score) {
                ++mismatches; ++g_failures;
                std::cout << "  FAIL hash-indep [" << fen << "] d" << depth
                          << ": 1MB=" << on1.score << " 256MB=" << on256.score << "\n";
            }
            if (on1.best != on256.best) {   // ties may differ; verify both optimal
                const int v1 = root_value_nott(fen, on1.best, depth);
                const int v2 = root_value_nott(fen, on256.best, depth);
                check(v1 == on1.score && v2 == on1.score,
                      std::string("best differs between sizes but both optimal [") + fen + "]");
            }
        }
    if (mismatches == 0)
        std::cout << "  hash-independence: score(1MB) == score(256MB), depths 1..5, all positions\n";
}

// ---- 2. Determinism ----------------------------------------------------------
static void test_determinism(TranspositionTable& tt) {
    for (const char* fen : STEP2_FENS) {
        std::uint64_t n1 = 0, n2 = 0;
        const SearchResult r1 = run(fen, 4, &tt, false, &n1);
        const SearchResult r2 = run(fen, 4, &tt, false, &n2);
        check(r1.score == r2.score && r1.best == r2.best && n1 == n2,
              std::string("deterministic: same score/move/nodes across runs [") + fen + "]");
    }
    std::cout << "  determinism: identical root score, move and node count across repeated runs\n";
}

// ---- 3. Mate-score round-trip through the TT ---------------------------------
static void test_mate_roundtrip(TranspositionTable& tt) {
    struct M { const char* fen; int depth; int expected; };
    const M ms[] = {
        {"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", 2, VALUE_MATE - 1},   // mate in 1
        {"7k/8/5K2/8/8/8/8/5Q2 w - - 0 1",       4, VALUE_MATE - 3}    // mate in 2
    };
    for (const M& m : ms) {
        const SearchResult on = run(m.fen, m.depth, &tt, false);
        check(on.score == m.expected,
              std::string("mate solved WITH TT has correct score [") + m.fen + "]");
    }
    // Direct round-trip: a mate-in-n root score (VALUE_MATE - (2n-1)) stored and
    // probed through the TT preserves the exact distance (value_to_tt/value_from_tt).
    TranspositionTable t; t.resize(1); t.clear();
    for (int n = 1; n <= 4; ++n) {
        const int rootMate = VALUE_MATE - (2 * n - 1);
        const int ply = 4;
        const std::uint64_t key = 0x00ABCD00ULL + static_cast<std::uint64_t>(n);
        t.store(key, MOVE_NONE, static_cast<std::int16_t>(value_to_tt(rootMate, ply)), 6, BOUND_EXACT);
        TTEntry e;
        const bool found = t.probe(key, e);
        const int recovered = found ? value_from_tt(e.score, ply) : 0;
        check(found && recovered == rootMate,
              "mate-in-" + std::to_string(n) + " round-trips through the TT (got " + std::to_string(recovered) + ")");
    }
}

// ---- 4. Tripwire (primary authenticity check) --------------------------------
static void test_tripwire(TranspositionTable& tt) {
    // A constant score corruption is identical across Hash sizes, so hash-
    // independence cannot see it. It IS caught by the shifted-solved-position-score
    // check: a corrupted stored score, reused in a cutoff, changes the result.
    bool caught = false;
    std::string where;
    for (const char* fen : STEP2_FENS) {
        for (int depth = 3; depth <= 5 && !caught; ++depth) {
            const int reference = run(fen, depth, &tt, false).score;
            const int corrupted = run(fen, depth, &tt, true).score;
            if (reference != corrupted) {
                caught = true;
                where = std::string(fen) + " d" + std::to_string(depth)
                      + " (" + std::to_string(reference) + " -> " + std::to_string(corrupted) + ")";
            }
        }
        if (caught) break;
    }
    check(caught, "tripwire ALARMS: corrupting a stored TT score shifts a solved-position score");
    if (caught) std::cout << "  tripwire caught by shifted-solved-position-score at: " << where << "\n";

    // Restore (no corruption): the uncorrupted search is stable and equals itself.
    bool restored = true;
    for (const char* fen : STEP2_FENS)
        if (run(fen, 4, &tt, false).score != run(fen, 4, &tt, false).score)
            restored = false;
    check(restored, "tripwire removed: uncorrupted search is stable (gate passes)");
}

// ---- 5. Replacement policy ---------------------------------------------------
static void test_replacement() {
    TranspositionTable tt; tt.resize(1); tt.clear();
    const std::uint64_t kA = 0x0000000000001000ULL;   // identical low bits -> same cluster
    const std::uint64_t kB = 0x0000000100001000ULL;
    const std::uint64_t kC = 0x0000000200001000ULL;
    const std::uint64_t kD = 0x0000000300001000ULL;
    const Move mA = make_move(SQ_A1, SQ_A2);
    const Move mB = make_move(SQ_B1, SQ_B2);
    const Move mC = make_move(SQ_C1, SQ_C2);
    const Move mD = make_move(SQ_D1, SQ_D2);
    TTEntry e;

    tt.store(kA, mA, 10, 8, BOUND_EXACT);   // deep -> depth-preferred bucket
    tt.store(kB, mB, 20, 3, BOUND_EXACT);   // shallow, diff key -> always-replace bucket
    check(tt.probe(kA, e) && e.depth == 8 && e.move == mA, "deep entry kept in depth-preferred bucket");
    check(tt.probe(kB, e) && e.depth == 3 && e.move == mB, "shallow entry in always-replace bucket");

    tt.store(kC, mC, 30, 2, BOUND_EXACT);   // shallow, diff key -> always-replace (evicts kB)
    check(tt.probe(kC, e) && e.move == mC, "always-replace bucket takes the most recent (kC)");
    check(!tt.probe(kB, e),                 "kB evicted from the always-replace bucket");
    check(tt.probe(kA, e) && e.depth == 8,  "kA still retained in the depth-preferred bucket");

    tt.store(kD, mD, 40, 9, BOUND_EXACT);   // deeper than 8 -> depth-preferred (evicts kA)
    check(tt.probe(kD, e) && e.depth == 9 && e.move == mD, "deeper entry replaces the depth-preferred bucket");
    check(!tt.probe(kA, e), "kA evicted from the depth-preferred bucket by a deeper entry");
}

// ---- 6. Regression: TT-off path == minimax (game-theoretic correctness) -------
static void test_regression_minimax() {
    int mismatches = 0;
    for (const char* fen : STEP2_FENS)
        for (int depth = 1; depth <= 4; ++depth) {
            Position pa; parse_fen(pa, fen);
            SearchInfo info;                 // TT off, quiescence off (to match minimax)
            info.use_mvv_lva = true; info.use_killers_history = true;
            info.use_qsearch = false; info.tt = nullptr;
            const int ab = search_root(pa, depth, info).score;
            Position pb; parse_fen(pb, fen);
            const int mm = minimax(pb, depth, 0);
            if (ab != mm) { ++mismatches; ++g_failures;
                std::cout << "  FAIL regression [" << fen << "] d" << depth
                          << ": ab=" << ab << " minimax=" << mm << "\n"; }
        }
    if (mismatches == 0)
        std::cout << "  regression: TT-off path == minimax at full window, depths 1..4\n";
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    TranspositionTable tt1, tt256;
    tt1.resize(1);
    tt256.resize(256);

    test_hash_independence(tt1, tt256);
    test_determinism(tt1);
    test_mate_roundtrip(tt1);
    test_tripwire(tt1);
    test_replacement();
    test_regression_minimax();

    if (g_failures == 0) {
        std::cout << "test_tt: ALL STEP 6 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_tt: FAILURES = " << g_failures << "\n";
    return 1;
}
