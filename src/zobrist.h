// Roj chess engine — Zobrist hashing keys.
//
// Zobrist hashing represents a position as the XOR of one random 64-bit key per
// independent feature (each piece on each square, side to move, castling rights,
// en-passant file). Because XOR is its own inverse, a move can update the hash
// incrementally by XOR-ing out what changed and XOR-ing in what replaced it —
// the basis for the transposition table and repetition detection later.
//
// Keys are generated once at startup by our own PRNG (no published tables).

#ifndef ROJ_ZOBRIST_H
#define ROJ_ZOBRIST_H

#include "types.h"

#include <cstdint>

namespace roj {

extern std::uint64_t ZOBRIST_PIECE[PIECE_NB][SQUARE_NB];   // [piece][square]
extern std::uint64_t ZOBRIST_SIDE;                         // XOR-ed in when BLACK to move
extern std::uint64_t ZOBRIST_CASTLING[CASTLING_RIGHTS_NB]; // one per 4-bit combo
extern std::uint64_t ZOBRIST_EP[FILE_NB];                  // one per en-passant file

// Generate all Zobrist keys. Must be called once at startup.
void init_zobrist();

} // namespace roj

#endif // ROJ_ZOBRIST_H
