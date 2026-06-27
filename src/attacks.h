// Roj chess engine — precomputed attack tables for the leaper pieces.
//
// Knights, kings and pawns are "leapers": their attacks depend only on the
// square they stand on, never on what else is on the board. So we compute every
// square's attack set once at startup and store it in a lookup table — a single
// array read at runtime instead of recomputation.
//
// Sliding pieces (bishop / rook / queen) DO depend on occupancy and are handled
// separately by magic bitboards (step 4); they are deliberately not here.
//
// Pawns are the only colour-dependent leaper: white pawns capture one rank up,
// black pawns one rank down — hence the [colour][square] table.

#ifndef ROJ_ATTACKS_H
#define ROJ_ATTACKS_H

#include "types.h"

namespace roj {

extern Bitboard KNIGHT_ATTACKS[SQUARE_NB];
extern Bitboard KING_ATTACKS[SQUARE_NB];
extern Bitboard PAWN_ATTACKS[COLOR_NB][SQUARE_NB];   // [WHITE] = up, [BLACK] = down

// Fill the three tables above. Must be called once before they are read (e.g.
// at program start). Idempotent: recomputing is harmless.
void init_attack_tables();

} // namespace roj

#endif // ROJ_ATTACKS_H
