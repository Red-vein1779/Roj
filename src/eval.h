// Roj chess engine — Phase 2: disposable evaluation stub.
//
// phase2.md section 2: a deliberately minimal evaluation (material + a small
// midgame piece-square table, NO game-phase tapering) whose only job is to make
// the search testable and measurable. It is symmetric by construction and is
// explicitly DISPOSABLE — it will be replaced or built out in Phase 4 and no
// chess knowledge should be layered on top of it.
//
// Platform-independent C++17 only.

#ifndef ROJ_EVAL_H
#define ROJ_EVAL_H

#include "position.h"

namespace roj {

// Static evaluation in centipawn-like units, FROM THE SIDE-TO-MOVE's
// perspective: positive means the side to move is better off. Computed in `int`.
// Depends only on piece placement and side to move (no search, no game state).
int evaluate(const Position& pos);

} // namespace roj

#endif // ROJ_EVAL_H
