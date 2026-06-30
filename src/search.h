// Roj chess engine — Phase 2, Step 2: fixed-depth fail-soft negamax alpha-beta.
//
// This is the search core in its simplest honest form (phase2.md section 3
// decision 1): no transposition table, no quiescence, no move ordering, no
// iterative deepening, no time management — those are later steps. Mate scoring
// uses the section 4 conventions. Platform-independent C++17 only.

#ifndef ROJ_SEARCH_H
#define ROJ_SEARCH_H

#include "types.h"
#include "position.h"

namespace roj {

// A root search result: the exact (full-window) score and the root move that
// achieves it.
struct SearchResult {
    int  score;
    Move best;
};

// Fail-soft negamax alpha-beta at a fixed `depth`.
//
// PLY: the root is ply 0; each recursion adds exactly 1. Terminal scoring is
// node-relative (section 4): a side to move with NO legal moves returns
//   -VALUE_MATE + ply   when in check (it is being mated at this ply), or
//    VALUE_DRAW         when not in check (stalemate),
// and this is returned BEFORE the depth check so a mate at the horizon is scored
// as mate, not as a static eval. Moves are tried in natural generation order
// (NO ordering in this step). Returns the TRUE best value found — fail-soft, NOT
// clamped to the [alpha, beta] window.
int search(Position& pos, int depth, int alpha, int beta, int ply);

// Thin root wrapper: searches at the FULL window (alpha = -VALUE_INFINITE,
// beta = +VALUE_INFINITE, ply = 0), so the score is the exact minimax value, and
// tracks which root move produced it.
SearchResult search_root(Position& pos, int depth);

// Plain negamax MINIMAX with NO alpha-beta pruning — identical eval, terminal and
// ply rules to search(). This is Step 2's objective correctness oracle (the
// analogue of perft): at a full window, search() must equal minimax() EXACTLY;
// pruning may change speed, never the result.
int minimax(Position& pos, int depth, int ply);

} // namespace roj

#endif // ROJ_SEARCH_H
