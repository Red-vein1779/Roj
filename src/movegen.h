// Roj chess engine — pseudo-legal move generation.
//
// LOCKED DECISION 3: we generate PSEUDO-LEGAL moves. A move that leaves the
// side-to-move's own king in check IS generated here; removing such moves is the
// legality filter's job (step 12). There is deliberately no pin logic and no
// check-evasion logic in this file — filtering early would pull step 12 forward.
//
// Scope: ALL pseudo-legal moves — normal moves (step 10) plus castling, en
// passant and promotion (step 11).

#ifndef ROJ_MOVEGEN_H
#define ROJ_MOVEGEN_H

#include "types.h"
#include "position.h"

namespace roj {

// Fixed-capacity move list — no heap allocation in the hot path. 256 comfortably
// exceeds the most moves any legal position can have (~218).
struct MoveList {
    Move moves[256];
    int  count = 0;

    void add(Move m) { moves[count++] = m; }
};

// Generate ALL pseudo-legal moves for pos.side_to_move (normal moves plus
// castling, en passant and promotion). See the file header for the pseudo-legal
// contract (moves that leave the own king in check ARE included).
void generate_moves(const Position& pos, MoveList& list);

} // namespace roj

#endif // ROJ_MOVEGEN_H
