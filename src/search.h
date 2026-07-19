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

    // Step 7 / Phase 3 Step 1: when set, the search collects the PV here. The
    // search is PVS (unconditional since the Step 1 sign-off): only PV NODES
    // forgo TT value cutoffs (TT for move ordering only, so the triangular PV
    // stays complete); null-window (non-PV) children take full cutoffs. NOTE
    // (phase3.md §2.1): this retires the Phase 2 identities "root score is
    // Hash-size independent" and "ID(N) == direct depth-N" — a TT cutoff may
    // legally short-circuit with a fail-soft value from another window/depth.
    PvTable* pv = nullptr;

    // Phase 3 Step 4: null move pruning — SPRT-signed-off and unconditional on
    // the play path (`go` and bench set this true; the UCI toggle is removed).
    // Kept as an INTERNAL test-layering flag (same pattern as use_qsearch and
    // the other Phase 2 layer flags, default false) because NMP — unlike PVS
    // and the check extension — is deliberately unsound pruning: it cannot be
    // mirrored in the minimax oracle, so the oracle/regression tests need the
    // sound-alpha-beta core with this off to stay meaningful (phase3.md §2).
    // ON: at non-PV, not-in-check nodes deep enough, where the side to move
    // has non-pawn material and the previous ply was not itself a null move,
    // the turn is passed and searched at reduced depth with a null window just
    // above beta; a fail-high prunes (BOUND_LOWER TT store; mate-zone scores
    // clipped to beta first — phase3.md §8).
    bool use_nullmove = false;

    // Phase 3 Step 5: late move reductions — SPRT-signed-off and unconditional
    // on the play path (`go` and bench set this true; the UCI toggle is
    // removed). Kept as an INTERNAL test-scoping flag (default false) per the
    // §8 pattern established in Step 4: LMR, like NMP, is deliberately unsound
    // pruning with no meaningful oracle off-mode, so the oracle/regression
    // tests exercise the sound core with this off. ON: late (4th+), quiet,
    // non-TT, non-killer, non-checking moves at calm nodes are probed at a
    // reduced depth from our own startup-generated log table; fail-highs climb
    // the §8 cascade (reduced null-window -> full-depth null-window -> full
    // window).
    bool use_lmr = false;

    // Ply at which the search most recently made a null move on the CURRENT
    // path (set before the null-search recursion, restored after). A node at
    // last_null_ply + 1 is the null move's direct child and may not null again
    // (§8 "aldrig två null i rad"). -2 = no null move on the path.
    int last_null_ply = -2;

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

// Root wrapper tracking the best move. The 5-argument form searches the root
// with the given window (Phase 3 Step 2: aspiration); a root result outside
// (alpha, beta) is a bound, and the TT store bound reflects that. The
// 3-argument form is the FULL window (exact minimax value), as before.
SearchResult search_root(Position& pos, int depth, SearchInfo& info, int alpha, int beta);
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
