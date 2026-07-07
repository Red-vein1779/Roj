// Roj chess engine — Phase 2, Step 5: killer moves + history heuristic tests.
//
// Maps to phase2.md Step 5 "done when":
//  1. RESULT-INVARIANCE GATE (quiescence OFF): full-window search with killers+
//     history ON == minimax, depths 1..4, best move stable.
//  2. NODE-COUNT REDUCTION (quiescence ON): killers+history reduce total nodes
//     across the suite, identical score per position. (Phase 3 Step 1 amendment:
//     with PVS unconditional, a killer-induced reordering can occasionally COST
//     nodes on a single position. Traced on P6 (d4, ON=17886 vs OFF=17253):
//     the excess sits in the null-window PROBES themselves — a reordered,
//     locally worse first move weakens the probe bound for the remaining moves
//     — NOT in the re-search cascade (ON re-searched LESS: 12 vs 14). So the
//     Phase 2 per-position "ON <= OFF" claim is too strong under PVS; the
//     benefit is asserted in AGGREGATE, and the Elo value is SPRT-measured.)
//  3. UNIT: killer store/shift + no-dup; quiet cutoff stores a killer, capture
//     cutoff does not; history increment + bounded aging; between-search hygiene.
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_killers.cpp src/search.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_killers

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

// ---- 1. Result-invariance gate (quiescence OFF) ------------------------------
static int root_value_unordered(Position& pos, Move m, int depth) {
    make_move(pos, m);
    SearchInfo info; info.use_mvv_lva = false; info.use_qsearch = false; info.use_killers_history = false;
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
            SearchInfo on;  on.use_mvv_lva = true;  on.use_killers_history = true;  on.use_qsearch = false;
            SearchInfo off; off.use_mvv_lva = true; off.use_killers_history = false; off.use_qsearch = false;
            const SearchResult ro = search_root(pa, depth, on);
            const SearchResult ru = search_root(pb, depth, off);
            const int mm = minimax(pc, depth, 0);

            if (ro.score != mm || ru.score != mm) {
                ++mismatches; ++g_failures;
                std::cout << "  FAIL invariance [" << fen << "] d" << depth
                          << ": kh=" << ro.score << " noKh=" << ru.score << " minimax=" << mm << "\n";
            }
            if (ro.best != ru.best) {
                Position pd; parse_fen(pd, fen);
                const int vo = root_value_unordered(pd, ro.best, depth);
                const int vu = root_value_unordered(pd, ru.best, depth);
                check(vo == mm && vu == mm,
                      std::string("differing best moves are both optimal [") + fen + "]");
            }
        }
    if (mismatches == 0)
        std::cout << "  invariance: killers+history ON score == minimax == OFF for depths 1..4 on all positions\n";
}

// ---- 2. Node-count reduction (quiescence ON) --------------------------------
static void test_node_reduction() {
    struct P { const char* name; const char* fen; };
    const P ps[] = {
        {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
        {"Italian",  "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"},
        {"P6",       "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"}
    };
    const int depth = 4;
    std::uint64_t total_on = 0, total_off = 0;
    for (const P& p : ps) {
        Position pa; parse_fen(pa, p.fen);
        Position pb; parse_fen(pb, p.fen);
        SearchInfo on;  on.use_mvv_lva = true;  on.use_qsearch = true; on.use_delta_pruning = true; on.use_killers_history = true;
        SearchInfo off; off.use_mvv_lva = true; off.use_qsearch = true; off.use_delta_pruning = true; off.use_killers_history = false;
        const SearchResult ro = search_root(pa, depth, on);
        const SearchResult ru = search_root(pb, depth, off);
        std::cout << "  " << p.name << " d" << depth << ": nodes ON=" << on.nodes
                  << " OFF=" << off.nodes << "  score=" << ro.score << "\n";
        total_on  += on.nodes;
        total_off += off.nodes;
        check(ro.score == ru.score,  std::string("identical score ON/OFF [") + p.name + "]");
    }
    std::cout << "  aggregate d" << depth << ": nodes ON=" << total_on
              << " OFF=" << total_off << "\n";
    check(total_on < total_off, "aggregate nodes ON < OFF across the suite");
}

// ---- 3a. Killer store / shift ------------------------------------------------
static void test_killer_unit() {
    {   // mechanism: shift + no-duplicate
        SearchInfo info;
        const Move mA = make_move(SQ_E2, SQ_E4);
        const Move mB = make_move(SQ_D2, SQ_D4);
        const Move mC = make_move(SQ_G1, SQ_F3);
        store_killer(info, 5, mA);
        check(info.killers[5][0] == mA && info.killers[5][1] == MOVE_NONE, "killer 1: slot0=mA");
        store_killer(info, 5, mB);
        check(info.killers[5][0] == mB && info.killers[5][1] == mA, "killer 2: mA->slot1, mB->slot0");
        store_killer(info, 5, mB);
        check(info.killers[5][0] == mB && info.killers[5][1] == mA, "killer dup: unchanged");
        store_killer(info, 5, mC);
        check(info.killers[5][0] == mC && info.killers[5][1] == mB, "killer 3: mB->slot1, mC->slot0");
    }
    {   // search: a QUIET beta cutoff stores a killer (kings only -> no captures)
        SearchInfo info; info.use_mvv_lva = true; info.use_killers_history = true; info.use_qsearch = false;
        Position p; parse_fen(p, "7k/8/8/8/8/8/8/7K w - - 0 1");
        (void)search(p, 1, -20000, -19000, 0, info);    // low window forces a cutoff on move 0
        check(info.killers[0][0] != MOVE_NONE, "quiet beta cutoff stores a killer");
    }
    {   // search: a CAPTURE beta cutoff does NOT store a killer (Bxd5 ordered first)
        SearchInfo info; info.use_mvv_lva = true; info.use_killers_history = true; info.use_qsearch = false;
        Position p; parse_fen(p, "4k3/8/8/3p4/4B3/8/8/4K3 w - - 0 1");
        (void)search(p, 1, -20000, -19000, 0, info);
        check(info.killers[0][0] == MOVE_NONE, "capture beta cutoff does not store a killer");
    }
}

// ---- 3b. History increment + bounded aging -----------------------------------
static void test_history_unit() {
    SearchInfo info;
    const Move m = make_move(SQ_E2, SQ_E4);
    check(info.history[WHITE][SQ_E2][SQ_E4] == 0, "history starts at 0");
    update_history(info, WHITE, m, 4);
    check(info.history[WHITE][SQ_E2][SQ_E4] == 16, "history += depth*depth (4*4=16)");
    for (int i = 0; i < 500000; ++i)
        update_history(info, WHITE, m, 30);              // +900 each; aging must bound it
    const int h = info.history[WHITE][SQ_E2][SQ_E4];
    check(h > 0 && h < (1 << 20), "history bounded under aging, no overflow (got " + std::to_string(h) + ")");
}

// ---- 3c. Between-search hygiene ----------------------------------------------
static void test_hygiene() {
    const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"; // Kiwipete
    const int depth = 4;

    SearchInfo reused; reused.use_mvv_lva = true; reused.use_killers_history = true; reused.use_qsearch = false;
    { Position p; parse_fen(p, fen); search_root(p, depth, reused); }   // populate killers/history
    reused.nodes = 0;
    { Position p; parse_fen(p, fen); search_root(p, depth, reused); }   // must clear at entry
    const std::uint64_t nodes_reused = reused.nodes;

    SearchInfo fresh; fresh.use_mvv_lva = true; fresh.use_killers_history = true; fresh.use_qsearch = false;
    { Position p; parse_fen(p, fen); search_root(p, depth, fresh); }
    const std::uint64_t nodes_fresh = fresh.nodes;

    std::cout << "  hygiene: nodes reused=" << nodes_reused << " fresh=" << nodes_fresh << "\n";
    check(nodes_reused == nodes_fresh,
          "killers/history cleared at search start (reused == fresh node count)");
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    test_invariance();
    test_node_reduction();
    test_killer_unit();
    test_history_unit();
    test_hygiene();

    if (g_failures == 0) {
        std::cout << "test_killers: ALL STEP 5 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_killers: FAILURES = " << g_failures << "\n";
    return 1;
}
