// Roj chess engine — Phase 2: fail-soft negamax alpha-beta + move ordering + quiescence.
//
// Step 2: fixed-depth fail-soft negamax core (section 3 decision 1), ply-relative
// mate scoring (section 4). Step 3: MVV-LVA capture ordering. Step 4: quiescence
// search at the leaves (captures + queen promotions + check evasions; stand-pat +
// delta pruning; section 3 decision 3 / section 9). Still no transposition table,
// no killers/history, no iterative deepening, no time management, no SEE.
// Platform-independent C++17 only.

#ifndef ROJ_SEARCH_H
#define ROJ_SEARCH_H

#include "types.h"
#include "position.h"
#include "movegen.h"

#include <cstdint>

namespace roj {

// A root search result: the exact (full-window) score and the root move.
struct SearchResult {
    int  score;
    Move best;
};

// Per-search state threaded through the recursion: a node counter (deterministic;
// also seeds the Step 10 `bench`) plus feature toggles, so the recursion signature
// stays stable as later steps add state. Defaults reproduce the Step 3 behaviour
// (ordering on, quiescence OFF, static eval at depth 0); the Step 2 wrappers below
// additionally turn ordering off.
struct SearchInfo {
    std::uint64_t nodes             = 0;
    bool          use_mvv_lva       = true;
    bool          use_qsearch       = false;  // false: static eval at depth 0 (Steps 2/3)
    bool          use_delta_pruning = true;   // delta pruning inside qsearch (ignored if !use_qsearch)
};

// Fail-soft negamax alpha-beta at fixed `depth`, with the search context.
// PLY: root is 0, +1 per recursion. No legal moves -> (-VALUE_MATE + ply) in check
// else VALUE_DRAW, returned BEFORE the depth check. At depth 0: qsearch() when
// info.use_qsearch, else the static eval. Returns the true best value (fail-soft).
int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info);

// Root wrapper at the FULL window (exact minimax value), tracking the best move.
SearchResult search_root(Position& pos, int depth, SearchInfo& info);

// Quiescence search (Step 4). At a leaf, keep searching "noisy" moves until the
// position is quiet, so a leaf value is never read mid-capture (the horizon
// effect). Fail-soft negamax. IN CHECK: no stand-pat; search ALL legal evasions;
// no legal moves -> (-VALUE_MATE + ply). NOT in check: stand_pat = evaluate(pos)
// as a lower bound (>= beta returns it; else raise alpha), then search only
// captures + queen promotions (MVV-LVA ordered), with delta pruning.
int qsearch(Position& pos, int alpha, int beta, int ply, SearchInfo& info);

// Step 2/3 convenience overloads: natural move order, quiescence OFF, node count
// discarded. Kept so the Step 2/3 tests/callers are unaffected.
int search(Position& pos, int depth, int alpha, int beta, int ply);
SearchResult search_root(Position& pos, int depth);

// Plain no-pruning negamax MINIMAX oracle (same eval/terminal/ply rules; static
// eval at depth 0). The full-window equivalence gate uses this on the ordering-ON
// but quiescence-OFF path.
int minimax(Position& pos, int depth, int ply);

// --- Move-ordering layer (Step 3: MVV-LVA captures) -------------------------
void order_moves(const Position& pos, MoveList& ml);
int capture_score(const Position& pos, Move m);

} // namespace roj

#endif // ROJ_SEARCH_H
