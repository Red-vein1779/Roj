// Roj chess engine — Phase 2: search core (negamax/ordering/qsearch/TT + ID/PV).
//
// Steps 2-6 built fail-soft negamax, MVV-LVA, quiescence, killers/history and the
// transposition table. Step 7 adds iterative deepening, a triangular principal-
// variation table (section 3 decision 6), and full UCI `info` (section 6), wiring
// the search into `go`. No aspiration windows, no draw detection, no time
// management, single-threaded. C++17 only.

#ifndef ROJ_SEARCH_H
#define ROJ_SEARCH_H

#include "types.h"
#include "position.h"
#include "movegen.h"
#include "value.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace roj {

class TranspositionTable;   // forward declaration

struct SearchResult {
    int  score;
    Move best;
};

// Triangular principal-variation table (section 3 decision 6): pv[ply] holds the
// PV from that ply; length[ply] its length. Collected during the search by copying
// the child's PV in behind the current best move — NOT extracted from the TT.
struct PvTable {
    Move pv[MAX_PLY][MAX_PLY] = {};
    int  length[MAX_PLY] = {};
};

// Per-search state threaded through the recursion.
struct SearchInfo {
    std::uint64_t nodes               = 0;
    int           seldepth            = 0;   // max ply reached this iteration (incl. qsearch)
    bool          use_mvv_lva         = true;
    bool          use_qsearch         = false;
    bool          use_delta_pruning   = true;
    bool          use_killers_history = false;

    Move killers[MAX_PLY][2] = {};
    int  history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    TranspositionTable* tt = nullptr;
    bool tt_tripwire = false;

    // Step 8: draw detection. When set, the search returns VALUE_DRAW on twofold
    // repetition (along the search path AND the pre-root game history), the 50-move
    // rule, and insufficient material. Default OFF so Steps 2-7 behaviour (and the
    // minimax oracle equivalence) is byte-for-byte unchanged when it is not asked
    // for; the `go` play path turns it on.
    bool use_draw_detection = false;

    // Repetition path for draw detection. Seed `rep` with the PRE-ROOT game keys
    // (the positions actually played before the root, via `position ... moves ...`)
    // BEFORE searching; the search then pushes each node's key as it descends and
    // pops on the way back up. A node whose key already appears in `rep` is a
    // repetition (twofold in the tree, or a repeat of a pre-root position). The key
    // stored here is `pos.hash` — Phase 1's incremental Zobrist — so the en-passant
    // convention is exactly Phase 1's (position.cpp), as phase2.md §9 requires.
    std::vector<std::uint64_t> rep;

    // Step 7: when set, the search collects the PV here. Phase 2 rule (use_pvs
    // false): the whole PV-collecting path uses the TT for move ordering ONLY (no
    // TT cutoffs), so the PV is complete and the score is order-invariant
    // (ID(N) == a direct depth-N search). Phase 3 rule (use_pvs true): only PV
    // NODES forgo TT value cutoffs; null-window (non-PV) nodes take full cutoffs.
    PvTable* pv = nullptr;

    // Phase 3 Step 1: PVS (phase3.md §3 decisions 2/4, §8). ON: the first move at
    // each node is searched with the full window at full depth; every later move
    // is probed with a null window [alpha, alpha+1] as a NON-PV child (non-PV
    // nodes take full TT value cutoffs), with a full-window full-depth re-search
    // when the probe fails high and a wider window exists (§8 re-search cascade).
    // PV nodes keep the Phase 2 lock: TT for move ordering only, never value
    // cutoffs, so the triangular PV stays complete. OFF: exactly the Phase 2
    // search — every move full window, cutoffs governed by info.pv == nullptr.
    // Default false so every Phase 2 test keeps byte-for-byte behaviour; the play
    // path (`go`, temporary UCI option "PVS") and bench turn it on.
    bool use_pvs = false;

    // Step 9: time management + abortable search. `check_time` is the master switch
    // for ALL of this: when it is false (fixed-depth `go depth N`, the minimax
    // oracle, and every test that searches a fixed depth) none of the fields below
    // are consulted, so those searches stay byte-for-byte deterministic. search_id()
    // owns the clock: it stamps `start_time`, enforces the soft limit between
    // iterations, and ARMS the hard abort only after depth 1 has completed so a
    // legal move always exists.
    bool          check_time          = false;  // master switch for abort logic
    bool          use_time_management  = false;  // soft/hard wall-clock limits active
    long long     soft_ms             = 0;       // don't START a new iteration past this
    long long     hard_ms             = 0;       // abort the RUNNING iteration past this
    std::uint64_t max_nodes           = 0;       // node limit (0 = none; `go nodes N`)
    const bool*   stop                = nullptr; // external stop request (UCI `stop`)
    std::chrono::steady_clock::time_point start_time;  // stamped by search_id()
    bool          aborted             = false;   // set once a limit/stop has fired
    bool          abort_armed         = false;   // true only after depth 1 completes
    int           completed_depth     = 0;       // deepest fully-completed iteration
};

// Fail-soft negamax alpha-beta at fixed `depth`. `pv_node` is the node type for
// the PVS / TT-cutoff logic (Phase 3 Step 1): PV nodes never take TT value
// cutoffs; null-window children are searched as non-PV. The 6-argument overload
// derives the Phase 2 rule (every node on the PV-collecting path is PV).
int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info, bool pv_node);
int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info);

// Root wrapper at the FULL window (exact minimax value), tracking the best move.
SearchResult search_root(Position& pos, int depth, SearchInfo& info);

// Iterative deepening (Step 7): search depths 1..maxDepth reusing the TT, keeping
// the best move + PV of the last completed iteration. When printInfo, emit a UCI
// `info` line after each iteration. Returns the final iteration's result.
SearchResult search_id(Position& pos, int maxDepth, SearchInfo& info, bool printInfo);

// Step 9: a simple soft/hard time budget in milliseconds from the side-to-move
// clock. `movetime >= 0` dominates (soft = hard = movetime - overhead). Otherwise
// the budget derives from the remaining time, the increment and movestogo (a fixed
// divisor is assumed when movestogo <= 0). The hard cap is at most half the usable
// time, so repeatedly spending it can never make the clock flag. Deliberately
// simple (the sophisticated manager is Phase 7). Shared by `go` and the tests.
struct TimeBudget { long long soft_ms; long long hard_ms; };
TimeBudget compute_time_budget(long long remaining, long long inc, int movestogo, long long movetime);

// Quiescence search (Step 4).
int qsearch(Position& pos, int alpha, int beta, int ply, SearchInfo& info);

// Step 2/3 convenience overloads.
int search(Position& pos, int depth, int alpha, int beta, int ply);
SearchResult search_root(Position& pos, int depth);

// Plain no-pruning negamax MINIMAX oracle.
int minimax(Position& pos, int depth, int ply);

// --- Move-ordering layer ----------------------------------------------------
void order_moves(const Position& pos, MoveList& ml);
int capture_score(const Position& pos, Move m);
void store_killer(SearchInfo& info, int ply, Move m);
void update_history(SearchInfo& info, Color side, Move m, int depth);

// UCI score field: "mate N" (section 4 mate convention) or "cp X". Exposed for tests.
std::string score_to_uci(int score);

// Step 8: true iff neither side has enough material to force mate — the standard
// dead-draw cases K vs K, K+single minor (KN or KB) vs K, and KB vs KB with both
// bishops on the same colour complex. Path-independent, so safe to test at any
// node. Exposed for tests.
bool insufficient_material(const Position& pos);

} // namespace roj

#endif // ROJ_SEARCH_H
