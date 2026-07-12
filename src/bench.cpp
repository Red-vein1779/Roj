// Roj chess engine — Phase 2, Step 10: `bench` node signature (see bench.h).

#include "bench.h"

#include "search.h"
#include "tt.h"
#include "position.h"
#include "fen.h"
#include "movegen.h"
#include "value.h"
#include "types.h"

#include <chrono>
#include <iostream>

namespace roj {

namespace {

// Fixed parameters. Changing any of these changes the signature, so they are pinned
// here and never derived from UCI options or the environment. Depth 6 in the `go`
// play configuration (Phase 3 Step 1: PVS, TT value cutoffs at non-PV nodes) runs
// the suite in ~2 s. The depth raise is DEFERRED to after Block B's LTC regression
// (phase3.md §3 decision 3 as amended): PVS alone cuts nodes, but the effective
// branching factor is set by the Block B pruners, so deeper bench is not yet cheap.
constexpr int BENCH_DEPTH   = 6;    // fixed search depth per position
constexpr int BENCH_HASH_MB = 16;   // fixed TT size (independent of the `Hash` option)

// REFERENCE SIGNATURE (commit this, Stockfish-style): with the positions, depth, TT
// size and search configuration below, run_bench() returns exactly:
//
//     Nodes searched: 1617309
//
// This number is deterministic across runs, rebuilds and platforms (fixed depth,
// fixed positions, integer-only search, TT cluster count derived from a fixed byte
// budget and the fixed sizeof(TTEntry)). Any accidental change to move generation,
// ordering, evaluation, qsearch, the TT or draw detection will change it.

// A fixed, varied set of positions: the opening, tactical middlegames and a couple
// of endgames. Chosen to exercise many code paths; pure insufficient-material
// positions are avoided (they would resolve to a root draw and add ~1 node).
const char* const BENCH_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",       // start
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", // Kiwipete
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",                     // rook/pawn ending
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "rnbqkb1r/pppp1ppp/5n2/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 2 3",
    "2rq1rk1/pp1bppbp/2np1np1/8/2BNP3/2N1BP2/PPPQ2PP/2KR3R w - - 0 1",
    "r1bq1rk1/pp2bppp/2n2n2/2pp4/3P4/2NBPN2/PP3PPP/R1BQ1RK1 w - - 0 1",
    "8/pp3p1k/2p2q1p/3r1P2/5R2/7P/P1P1QP2/7K b - - 0 1",
    "8/8/4k3/8/8/2K5/5R2/3b4 b - - 0 1",                             // KR vs KB ending
    "r2q1rk1/1b1nbppp/p2ppn2/1p6/3NPP2/2NBB3/PPPQ2PP/2KR3R w - - 0 1",
    "r1b2rk1/2q1b1pp/p2ppn2/1p6/3QP3/1BN1B3/PPP3PP/R4RK1 w - - 0 1",
};
constexpr int BENCH_COUNT = static_cast<int>(sizeof(BENCH_FENS) / sizeof(BENCH_FENS[0]));

} // namespace

std::uint64_t run_bench(bool verbose) {
    TranspositionTable tt;
    tt.resize(BENCH_HASH_MB);
    static PvTable pv;   // static: the triangular table is large; keep it off the stack

    std::uint64_t total = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < BENCH_COUNT; ++i) {
        Position pos;
        parse_fen(pos, BENCH_FENS[i]);

        // Clear the TT before EACH position so the count is independent of position
        // order and of any carry-over — fully reproducible.
        tt.clear();

        // Exactly the `go` play-path configuration, but fixed depth and NO time
        // checks (check_time stays false), which makes the search deterministic.
        // Draw detection is on with an empty pre-root history, as for a fresh `go`.
        SearchInfo info;
        info.use_mvv_lva = true;
        info.use_killers_history = true;
        info.use_qsearch = true;
        info.use_delta_pruning = true;
        info.use_draw_detection = true;
        info.use_nullmove = true;        // Step 4 sign-off: NMP unconditional on the play path
        info.use_lmr = true;             // Step 5 sign-off: LMR unconditional on the play path
        info.tt = &tt;
        info.pv = &pv;

        const SearchResult r = search_id(pos, BENCH_DEPTH, info, /*printInfo=*/false);
        total += info.nodes;

        if (verbose)
            std::cout << "position " << (i + 1) << '/' << BENCH_COUNT
                      << "  nodes " << info.nodes
                      << "  score " << score_to_uci(r.score)
                      << "  bestmove " << move_to_uci(r.best)
                      << std::endl;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const long long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    if (verbose) {
        const long long nps = (ms > 0)
            ? static_cast<long long>(total * 1000ULL / static_cast<std::uint64_t>(ms))
            : 0;
        std::cout << "===========================\n"
                  << "Nodes searched: " << total << '\n'
                  << "Time: " << ms << " ms   nps: " << nps << std::endl;
    }
    return total;
}

} // namespace roj
