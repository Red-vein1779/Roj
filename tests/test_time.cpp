// Roj chess engine — Phase 2, Step 9: time-management + abortable-search tests.
//
// Maps to phase2.md Step 9 "done when" (§7 row 9, §3 decision 8, §9 "Ren abort"):
//
//  1. NEVER LOSES ON TIME: a simulated self-play game over 40+ plies at short and
//     very short controls (incl. 1000+10, 100+0 and repeated 50ms movetime) — the
//     clock never goes negative and every bestmove is returned within its budget.
//     Reports the tightest margin observed.
//  2. CLEAN ABORT: a search interrupted mid-iteration (deterministically, via a
//     node limit, and via a `stop` flag) returns the best move of the last
//     COMPLETED iteration — never a partial-iteration result.
//  3. SOFT vs HARD: the soft limit stops the ID loop BETWEEN iterations (no abort);
//     the hard limit aborts an iteration mid-flight. Both are shown firing.
//  4. FIXED-DEPTH DETERMINISM: `go depth N` (check_time off) is bit-for-bit
//     repeatable — identical score, move AND node count across runs — so time
//     management does not touch it. (Bench, Step 10, will therefore be fixed depth.)
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_time.cpp src/search.cpp src/tt.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp src/bitboard.cpp -o test_time

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

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace roj;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

static long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Configure a SearchInfo exactly like the `go` play path (Steps 3-8 features on).
static void configure_play(SearchInfo& info, TranspositionTable* tt, PvTable* pv) {
    info.use_mvv_lva = true; info.use_killers_history = true;
    info.use_qsearch = true; info.use_delta_pruning = true;
    info.tt = tt; info.pv = pv;
    info.use_draw_detection = true;
}

// ---- 1. Never loses on time -------------------------------------------------
// Self-play from startpos, managing both clocks the way a GUI + engine would. Each
// ply: compute the budget from the side-to-move clock (the same compute_time_budget
// the UCI layer uses), search, measure wall time, deduct it and add the increment,
// and assert the clock never flags. Returns the tightest (remaining_before-elapsed)
// margin seen.
static long long simulate_game(const std::string& label,
                               long long wtime0, long long btime0,
                               long long winc, long long binc,
                               long long movetime,   // >=0 => fixed per-move; clocks ignored
                               int maxPlies) {
    Position pos; parse_fen(pos, START_FEN);
    TranspositionTable tt; tt.resize(16);
    static PvTable pv;

    std::vector<std::uint64_t> gameKeys{ pos.hash };
    long long clock[2] = { wtime0, btime0 };
    const long long inc[2] = { winc, binc };
    long long tightest = (1LL << 62);
    bool everNegative = false;

    for (int ply = 0; ply < maxPlies; ++ply) {
        MoveList ml; generate_legal_moves(pos, ml);
        if (ml.count == 0) break;                 // game over (mate/stalemate)

        const Color stm = pos.side_to_move;
        const long long remainingBefore = (movetime >= 0) ? movetime : clock[stm];

        TimeBudget b = (movetime >= 0)
            ? compute_time_budget(0, 0, 0, movetime)
            : compute_time_budget(clock[stm], inc[stm], 0, -1);

        SearchInfo info; configure_play(info, &tt, &pv);
        info.check_time = true;
        info.use_time_management = true;
        info.soft_ms = b.soft_ms;
        info.hard_ms = b.hard_ms;
        if (gameKeys.size() > 1) info.rep.assign(gameKeys.begin(), gameKeys.end() - 1);

        const long long t0 = now_ms();
        const SearchResult r = search_id(pos, MAX_PLY - 1, info, /*printInfo=*/false);
        const long long elapsed = now_ms() - t0;

        check(r.best != MOVE_NONE, label + ": a legal move was returned");

        const long long margin = remainingBefore - elapsed;   // >0 means we did not flag
        if (margin < tightest) tightest = margin;

        if (movetime < 0) {
            clock[stm] -= elapsed;
            if (clock[stm] < 0) everNegative = true;
            clock[stm] += inc[stm];
        } else {
            if (elapsed > movetime) everNegative = true;       // "flag" == overran movetime
        }

        make_move(pos, r.best);
        gameKeys.push_back(pos.hash);
    }

    check(!everNegative, label + ": clock never went negative / never overran the budget");
    std::cout << "  [" << label << "] tightest margin = " << tightest << " ms"
              << (movetime >= 0 ? "  (movetime)" : "  (clock)") << "\n";
    return tightest;
}

static void test_never_loses_on_time() {
    long long m1 = simulate_game("1000+10", 1000, 1000, 10, 10, /*movetime=*/-1, 40);
    long long m2 = simulate_game("100+0",    100,  100,  0,  0,  /*movetime=*/-1, 40);
    long long m3 = simulate_game("movetime 50", 0, 0,    0,  0,  /*movetime=*/50, 12);
    check(m1 > 0 && m2 > 0 && m3 > 0, "every control kept a positive margin (never flagged)");
}

// ---- 2. Clean abort: last completed iteration is returned -------------------
static void test_clean_abort() {
    const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    TranspositionTable tt; tt.resize(16);
    static PvTable pv;

    // (a) Deterministic abort via a NODE limit set BETWEEN "finish depth k" and
    //     "finish depth k+1". The interrupted depth k+1 must be discarded and the
    //     move from the completed depth k returned.
    const int k = 5;

    Position pk; parse_fen(pk, fen);
    tt.clear();
    SearchInfo ik; configure_play(ik, &tt, &pv);
    const SearchResult moveK = search_id(pk, k, ik, false);   // completes depth k
    const std::uint64_t nodesK = ik.nodes;

    Position pk1; parse_fen(pk1, fen);
    tt.clear();
    SearchInfo ik1; configure_play(ik1, &tt, &pv);
    search_id(pk1, k + 1, ik1, false);                        // completes depth k+1
    const std::uint64_t nodesK1 = ik1.nodes;

    check(nodesK1 > nodesK, "clean-abort setup: depth k+1 uses more nodes than depth k");

    Position pa; parse_fen(pa, fen);
    tt.clear();
    SearchInfo ia; configure_play(ia, &tt, &pv);
    ia.check_time = true;
    ia.max_nodes = nodesK + (nodesK1 - nodesK) / 2;            // stop midway through k+1
    const SearchResult aborted = search_id(pa, k + 5, ia, false);

    check(ia.aborted, "node-limit abort actually fired mid-iteration");
    check(ia.completed_depth == k, "aborted search's last COMPLETED depth == k");
    check(aborted.best == moveK.best,
          "aborted search returns the last completed iteration's move (not a partial)");
    std::cout << "  node-abort: completed depth=" << ia.completed_depth
              << " move=" << move_to_uci(aborted.best)
              << " (== depth-" << k << " move " << move_to_uci(moveK.best) << ")\n";

    // (b) `stop` flag: depth 1 still completes (a move is guaranteed), then the very
    //     next iteration aborts -> the depth-1 move is returned.
    Position p1; parse_fen(p1, fen);
    tt.clear();
    SearchInfo i1; configure_play(i1, &tt, &pv);
    const SearchResult move1 = search_id(p1, 1, i1, false);   // depth-1 reference

    Position ps; parse_fen(ps, fen);
    tt.clear();
    bool stopFlag = true;
    SearchInfo is; configure_play(is, &tt, &pv);
    is.check_time = true;
    is.stop = &stopFlag;                                       // stop already requested
    const SearchResult stopped = search_id(ps, 20, is, false);

    check(is.aborted, "stop-flag abort fired");
    check(is.completed_depth == 1, "stop: depth 1 completed before honoring stop (a move exists)");
    check(stopped.best == move1.best, "stop: returns the last completed (depth-1) move");
    std::cout << "  stop-abort: completed depth=" << is.completed_depth
              << " move=" << move_to_uci(stopped.best) << "\n";
}

// ---- 3. Soft vs hard both fire ----------------------------------------------
static void test_soft_and_hard() {
    const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    TranspositionTable tt; tt.resize(16);
    static PvTable pv;

    // SOFT: tiny soft, huge hard. The loop must STOP BETWEEN iterations (no abort),
    // short of maxDepth.
    {
        Position p; parse_fen(p, fen);
        tt.clear();
        SearchInfo info; configure_play(info, &tt, &pv);
        info.check_time = true; info.use_time_management = true;
        info.soft_ms = 15; info.hard_ms = 100000;
        const SearchResult r = search_id(p, MAX_PLY - 1, info, false);
        check(!info.aborted, "soft: the search was NOT aborted (soft stops between iterations)");
        check(info.completed_depth >= 1 && info.completed_depth < MAX_PLY - 1,
              "soft: the ID loop stopped early (before maxDepth)");
        check(r.best != MOVE_NONE, "soft: a move was returned");
        std::cout << "  soft fired: completed depth=" << info.completed_depth
                  << " (soft=15ms, hard=100000ms), not aborted\n";
    }

    // HARD: huge soft (never stops between iterations), tiny hard. An iteration is
    // aborted mid-flight.
    {
        Position p; parse_fen(p, fen);
        tt.clear();
        SearchInfo info; configure_play(info, &tt, &pv);
        info.check_time = true; info.use_time_management = true;
        info.soft_ms = 10000000; info.hard_ms = 20;
        const SearchResult r = search_id(p, MAX_PLY - 1, info, false);
        check(info.aborted, "hard: the running iteration was aborted mid-flight");
        check(r.best != MOVE_NONE, "hard: last completed iteration's move returned");
        std::cout << "  hard fired: aborted=true, last completed depth="
                  << info.completed_depth << " (soft huge, hard=20ms)\n";
    }
}

// ---- 4. Fixed-depth determinism (time management must not touch it) ----------
static void test_fixed_depth_deterministic() {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"
    };
    for (const char* fen : fens) {
        TranspositionTable tt; tt.resize(16);
        static PvTable pvA, pvB;

        Position pa; parse_fen(pa, fen);
        tt.clear();
        SearchInfo ia; configure_play(ia, &tt, &pvA);   // check_time stays false: fixed depth
        const SearchResult ra = search_id(pa, 6, ia, false);
        const std::uint64_t na = ia.nodes;

        Position pb; parse_fen(pb, fen);
        tt.clear();
        SearchInfo ib; configure_play(ib, &tt, &pvB);
        const SearchResult rb = search_id(pb, 6, ib, false);
        const std::uint64_t nb = ib.nodes;

        check(!ia.aborted && !ib.aborted, std::string("fixed-depth: no abort [") + fen + "]");
        check(ra.score == rb.score && ra.best == rb.best && na == nb,
              std::string("fixed-depth deterministic (score/move/nodes identical) [") + fen + "]");
    }
    std::cout << "  fixed depth 6: identical score, move AND node count across runs (deterministic)\n";
    std::cout << "  (note: time-based search reaches a non-deterministic depth, so bench is fixed-depth)\n";
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    test_never_loses_on_time();
    test_clean_abort();
    test_soft_and_hard();
    test_fixed_depth_deterministic();

    if (g_failures == 0) {
        std::cout << "test_time: ALL STEP 9 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_time: FAILURES = " << g_failures << "\n";
    return 1;
}
