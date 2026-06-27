// Roj chess engine — sliding-piece attacks via magic bitboards.
//
// Sliding pieces (bishop, rook, queen) differ from leapers: their attacks
// depend on what blocks them. Magic bitboards turn "which squares does a rook
// on s attack, given this occupancy?" into a single table lookup:
//
//     index = ((occupancy & MASK[s]) * MAGIC[s]) >> SHIFT[s];
//     attacks = ATTACKS[s][index];
//
// The MAGIC[s] number is a perfect hash, found by our own search at startup
// (no published constants). The MASK strips irrelevant edge squares so the
// index is small. Queen attacks are simply rook | bishop.

#ifndef ROJ_MAGIC_H
#define ROJ_MAGIC_H

#include "types.h"

namespace roj {

// (A) Ray-tracing oracle — slow but provably correct. Walks square by square in
// each direction, stops at the first blocker (included, it can be captured) or
// the board edge. This is the source of truth the magic tables are tested
// against, and is used internally to build them.
Bitboard rook_attacks_slow(Square s, Bitboard occ);
Bitboard bishop_attacks_slow(Square s, Bitboard occ);

// Maximum relevant-occupancy bits: 12 for a rook (corner files+ranks), 9 for a
// bishop (long central diagonals). These size the per-square table rows.
constexpr int ROOK_INDEX_BITS   = 12;
constexpr int BISHOP_INDEX_BITS = 9;

extern Bitboard ROOK_MASK[SQUARE_NB];
extern Bitboard BISHOP_MASK[SQUARE_NB];
extern Bitboard ROOK_MAGICS[SQUARE_NB];
extern Bitboard BISHOP_MAGICS[SQUARE_NB];
extern int      ROOK_SHIFTS[SQUARE_NB];
extern int      BISHOP_SHIFTS[SQUARE_NB];
extern Bitboard ROOK_ATTACKS[SQUARE_NB][1 << ROOK_INDEX_BITS];
extern Bitboard BISHOP_ATTACKS[SQUARE_NB][1 << BISHOP_INDEX_BITS];

// Build masks, search the magics with our own code, and fill the attack tables.
// Must be called once at startup, before the lookups below.
void init_magics();

// (D) Fast lookups.
inline Bitboard rook_attacks(Square s, Bitboard occ) {
    occ &= ROOK_MASK[s];
    occ *= ROOK_MAGICS[s];
    occ >>= ROOK_SHIFTS[s];
    return ROOK_ATTACKS[s][occ];
}
inline Bitboard bishop_attacks(Square s, Bitboard occ) {
    occ &= BISHOP_MASK[s];
    occ *= BISHOP_MAGICS[s];
    occ >>= BISHOP_SHIFTS[s];
    return BISHOP_ATTACKS[s][occ];
}
inline Bitboard queen_attacks(Square s, Bitboard occ) {
    return rook_attacks(s, occ) | bishop_attacks(s, occ);
}

} // namespace roj

#endif // ROJ_MAGIC_H
