// Roj chess engine — Phase 2: fail-soft negamax alpha-beta search + move ordering.
//
// Step 2 built the fixed-depth fail-soft negamax core (phase2.md section 3
// decision 1) with ply-relative mate scoring (section 4). Step 3 adds the move-
// ordering layer: MVV-LVA ordering of captures (section 3 decision 4 lists the
// full order TT -> MVV-LVA -> killers -> history; only MVV-LVA is added here).
// Still no transposition table, no quiescence, no iterative deepening, no time
// management. Platform-independent C++17 only.

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
// also seeds the Step 10 `bench`) and the MVV-LVA ordering toggle. Bundling it in
// one struct keeps the recursion signature stable as later steps add state.
struct SearchInfo {
    std::uint64_t nodes       = 0;
    bool          use_mvv_lva = true;   // false reproduces the Step 2 natural order
};

// Fail-soft negamax alpha-beta at fixed `depth`, with the search context.
// PLY: root is 0, +1 per recursion. Terminal scoring is node-relative (section 4):
// no legal moves -> (-VALUE_MATE + ply) in check, else VALUE_DRAW, returned BEFORE
// the depth check. With info.use_mvv_lva, captures are tried in MVV-LVA order
// before quiet moves; otherwise moves keep natural generation order. Returns the
// TRUE best value found (fail-soft, never clamped to [alpha, beta]).
int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info);

// Root wrapper at the FULL window (exact minimax value), tracking the best move.
SearchResult search_root(Position& pos, int depth, SearchInfo& info);

// Step 2 convenience overloads: natural move order (ordering OFF), node count
// discarded. Kept so Step 2 callers/tests are unaffected.
int search(Position& pos, int depth, int alpha, int beta, int ply);
SearchResult search_root(Position& pos, int depth);

// Plain no-pruning negamax MINIMAX oracle (same eval/terminal/ply rules). At a
// full window, search() must equal this exactly — ordering changes only speed.
int minimax(Position& pos, int depth, int ply);

// --- Move-ordering layer (Step 3: MVV-LVA captures) -------------------------
// Reorder `ml` in place for the search: capture moves first, ordered by MVV-LVA
// (most valuable victim first; among equal victims, least valuable aggressor
// first); quiet moves after, in their natural generation order. En passant is a
// pawn taking a pawn; capture-promotions count as captures. This layer extends
// later (Step 5 adds killer/history scores for quiet moves; Step 6 puts the TT
// move first). Ordering must never change the search RESULT, only the node count.
void order_moves(const Position& pos, MoveList& ml);

// The ordering score of one move: > 0 for captures (MVV-LVA), 0 for quiet moves,
// so captures sort ahead of quiets. Exposed for the ordering unit test.
int capture_score(const Position& pos, Move m);

} // namespace roj

#endif // ROJ_SEARCH_H
