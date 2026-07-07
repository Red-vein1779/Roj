// Roj chess engine — Phase 2, Step 7: iterative deepening + PV + info tests.
//
// Maps to phase2.md Step 7 "done when", AS AMENDED by Phase 3 Step 1 sign-off:
//  1. ID DETERMINISM (phase3.md §2.1 point 1): same binary + Hash + position +
//     fixed depth => identical score, best move and node count, run after run,
//     depths 1..5 on the Step 2 suite. Depth 1 keeps the exact "ID == direct"
//     anchor (a single iteration from a cleared TT has no history to differ by).
//     The Phase 2 identity "final ID iteration == direct depth-N" was a
//     documented Phase 2 CONFIGURATION (phase2.md §9: the PV path took no TT
//     value cutoffs) and died by design when PVS restored TT cutoffs at non-PV
//     nodes — a warm-TT iteration may legally short-circuit with fail-soft
//     values stored by earlier iterations.
//  2. PV VALIDITY: the reported PV is fully legal from the root, its first move is
//     the best move, and a forced-mate PV ends in checkmate.
//  3. MATE REPORTING: score_to_uci maps mate scores to "mate N" with correct sign;
//     a solved mate-in-1 reports "mate 1".
//  4. info FIELDS: seldepth >= depth; nodes counted.
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_id.cpp src/search.cpp src/tt.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_id

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

static void configure(SearchInfo& info, TranspositionTable* tt, PvTable* pv) {
    info.use_mvv_lva = true; info.use_killers_history = true;
    info.use_qsearch = true; info.use_delta_pruning = true;
    info.tt = tt; info.pv = pv;
}

// ---- 1. ID determinism (phase3.md §2.1 point 1) --------------------------------
static void test_id_determinism(TranspositionTable& ttA, TranspositionTable& ttB,
                                PvTable& pvA, PvTable& pvB) {
    int mismatches = 0;
    for (const char* fen : STEP2_FENS)
        for (int depth = 1; depth <= 5; ++depth) {
            Position pa; parse_fen(pa, fen);
            ttA.clear();
            SearchInfo ia; configure(ia, &ttA, &pvA);
            const SearchResult ra = search_id(pa, depth, ia, false);

            Position pb; parse_fen(pb, fen);
            ttB.clear();
            SearchInfo ib; configure(ib, &ttB, &pvB);
            const SearchResult rb = search_id(pb, depth, ib, false);

            if (ra.score != rb.score || ra.best != rb.best || ia.nodes != ib.nodes) {
                ++mismatches; ++g_failures;
                std::cout << "  FAIL id-determinism [" << fen << "] d" << depth
                          << ": score " << ra.score << "/" << rb.score
                          << " move " << move_to_uci(ra.best) << "/" << move_to_uci(rb.best)
                          << " nodes " << ia.nodes << "/" << ib.nodes << "\n";
            }
            // Depth 1: ID collapses to a single iteration from a cleared TT, so
            // the old exact identity with a direct search still must hold.
            if (depth == 1) {
                Position pc; parse_fen(pc, fen);
                ttB.clear();
                SearchInfo ic; configure(ic, &ttB, &pvB);
                const SearchResult rd = search_root(pc, 1, ic);
                if (ra.score != rd.score) {
                    ++mismatches; ++g_failures;
                    std::cout << "  FAIL id d1 == direct d1 [" << fen << "]: "
                              << ra.score << " vs " << rd.score << "\n";
                }
            }
        }
    if (mismatches == 0)
        std::cout << "  ID determinism: identical score/move/nodes across repeated runs, depths 1..5"
                     " (d1 == direct anchor)\n";
}

// ---- 2. PV validity ----------------------------------------------------------
static void test_pv_validity(TranspositionTable& tt, PvTable& pv) {
    struct C { const char* fen; int depth; bool mate; };
    const C cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, false},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, false},
        {"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", 3, true},   // mate in 1
        {"7k/8/5K2/8/8/8/8/5Q2 w - - 0 1",       5, true}    // mate in 2
    };
    for (const C& c : cases) {
        Position p; parse_fen(p, c.fen);
        tt.clear();
        SearchInfo info; configure(info, &tt, &pv);
        const SearchResult r = search_id(p, c.depth, info, false);

        const int len = pv.length[0];
        check(len >= 1, std::string("PV non-empty [") + c.fen + "]");
        check(len >= 1 && pv.pv[0][0] == r.best, "first PV move == reported best move");

        // Replay the PV from the root; every move must be legal in turn.
        Position rp; parse_fen(rp, c.fen);
        bool legal = true;
        for (int k = 0; k < len; ++k) {
            MoveList ml; generate_legal_moves(rp, ml);
            bool found = false;
            for (int j = 0; j < ml.count; ++j)
                if (ml.moves[j] == pv.pv[0][k]) { found = true; break; }
            if (!found) { legal = false; break; }
            make_move(rp, pv.pv[0][k]);
        }
        check(legal, std::string("PV fully legal from root [") + c.fen + "]");

        if (c.mate) {
            check(r.score >= VALUE_MATE_IN_MAX_PLY, "mate position scores a mate");
            if (legal) {
                MoveList ml; generate_legal_moves(rp, ml);
                check(ml.count == 0, std::string("mate PV ends in a terminal (checkmate) position [") + c.fen + "]");
            }
        }
    }
}

// ---- 3. Mate reporting -------------------------------------------------------
static void test_mate_reporting(TranspositionTable& tt, PvTable& pv) {
    check(score_to_uci(VALUE_MATE - 1) == "mate 1",  "score mate 1");
    check(score_to_uci(VALUE_MATE - 3) == "mate 2",  "score mate 2");
    check(score_to_uci(VALUE_MATE - 5) == "mate 3",  "score mate 3");
    check(score_to_uci(VALUE_MATE - 7) == "mate 4",  "score mate 4");
    check(score_to_uci(-VALUE_MATE + 1) == "mate -1", "score mate -1 (mated in 1)");
    check(score_to_uci(50) == "cp 50",               "score cp");

    Position p; parse_fen(p, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    tt.clear();
    SearchInfo info; configure(info, &tt, &pv);
    const SearchResult r = search_id(p, 2, info, false);
    check(score_to_uci(r.score) == "mate 1", "solved mate-in-1 reports 'mate 1'");
}

// ---- 4. info fields ----------------------------------------------------------
static void test_info_fields(TranspositionTable& tt, PvTable& pv) {
    Position p; parse_fen(p, STEP2_FENS[0]);
    tt.clear();
    SearchInfo info; configure(info, &tt, &pv);
    search_id(p, 5, info, false);
    check(info.seldepth >= 5, "seldepth >= depth (qsearch reaches deeper)");
    check(info.nodes > 0, "nodes counted");
    std::cout << "  info fields: seldepth=" << info.seldepth << " nodes=" << info.nodes << " (depth 5)\n";
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    TranspositionTable ttA, ttB;
    ttA.resize(8);
    ttB.resize(8);
    static PvTable pvA, pvB;   // static to keep these large tables off the main stack

    test_id_determinism(ttA, ttB, pvA, pvB);
    test_pv_validity(ttA, pvA);
    test_mate_reporting(ttA, pvA);
    test_info_fields(ttA, pvA);

    if (g_failures == 0) {
        std::cout << "test_id: ALL STEP 7 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_id: FAILURES = " << g_failures << "\n";
    return 1;
}
