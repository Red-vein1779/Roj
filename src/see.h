// Roj chess engine — Phase 3, Step 6: Static Exchange Evaluation (SEE).
//
// SEE(position, move) estimates the material outcome (centipawn-like units,
// Phase 2's material scale) of playing out the full capture sequence on the
// move's target square, both sides always capturing with their least valuable
// attacker and stopping when continuing is no longer profitable. It is a PURE
// function over the attack tables and a local occupancy copy — no make/unmake,
// no game-state mutation — and it changes NO search behaviour by itself:
// phase3.md §2.2 gates it against a brute-force oracle (tests/test_see.cpp);
// its two USES (quiescence pruning, move ordering) are Steps 7 and 8.
//
// Documented simplifications (phase3.md §8 "SEE:s definierade förenklingar"):
//  - Pinned pieces participate as if unpinned (accepted imperfection — do NOT
//    "fix" without a plan amendment).
//  - The king participates only as the LAST capturer: it never captures while
//    the opponent still has any attacker of the square left.
//  - Only the INITIATING move can be an en-passant capture; in-sequence
//    captures all land on the target square itself.
//  - An in-sequence pawn capture onto its promotion rank is assumed to promote
//    to a QUEEN (material-dominant choice).

#ifndef ROJ_SEE_H
#define ROJ_SEE_H

#include "types.h"
#include "position.h"

namespace roj {

// The material scale SEE resolves in. Matches eval.cpp's piece values; the king
// entry is a sentinel large enough that "the king got captured" dominates any
// material total (it is never cashed in a legal sequence — the king-last rule —
// but keeps illegal-initiator calls well-defined and oracle-comparable).
constexpr int SEE_VALUE[PIECE_TYPE_NB] = { 0, 100, 320, 330, 500, 900, 20000 };

// Expected material outcome of `m` (a capture, promotion or any move — a quiet
// move scores the exchange the opponent may start on its target square) for
// the side to move. Positive = profitable.
int see(const Position& pos, Move m);

} // namespace roj

#endif // ROJ_SEE_H
