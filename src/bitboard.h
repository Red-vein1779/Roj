// Roj chess engine — bitboard primitives.
//
// A bitboard is a 64-bit set of squares (bit i == square i, a1=0 .. h8=63).
// This header holds the small, hot building blocks every later component relies
// on: setting / clearing / testing individual squares, counting set bits, and
// extracting the least-significant set bit. Move generation and evaluation will
// call these millions of times, so they are inline.
//
// The bit-counting / bit-scan operations wrap the GCC/Clang builtins. Those
// builtins are *compiler*-specific but NOT *OS*-specific: they compile to the
// same instructions on Windows and Linux, so wrapping them here keeps the rest
// of the engine portable and free of #ifdefs. If we ever target a compiler
// without these builtins, only this one file changes.

#ifndef ROJ_BITBOARD_H
#define ROJ_BITBOARD_H

#include "types.h"
#include <cassert>

namespace roj {

// --- single-square access -------------------------------------------------
inline void set_bit(Bitboard& bb, Square s)   { bb |=  square_bb(s); }
inline void clear_bit(Bitboard& bb, Square s) { bb &= ~square_bb(s); }
inline bool test_bit(Bitboard bb, Square s)   { return (bb & square_bb(s)) != 0; }

// --- population count -----------------------------------------------------
inline int popcount(Bitboard bb) { return __builtin_popcountll(bb); }

// --- least-significant bit ------------------------------------------------
// PRECONDITION: bb != 0. __builtin_ctzll(0) is undefined behaviour, so callers
// must never pass an empty bitboard. The assert documents the contract and, in
// debug/sanitizer builds, traps any accidental violation early.
inline Square lsb(Bitboard bb) {
    assert(bb != 0 && "lsb: bitboard must be non-empty");
    return static_cast<Square>(__builtin_ctzll(bb));
}

// Return the least-significant set square AND clear it from bb.
// PRECONDITION: bb != 0 (same as lsb). `bb &= bb - 1` clears the lowest set bit.
inline Square pop_lsb(Bitboard& bb) {
    assert(bb != 0 && "pop_lsb: bitboard must be non-empty");
    const Square s = lsb(bb);
    bb &= bb - 1;
    return s;
}

// --- debugging ------------------------------------------------------------
// Draw bb as an 8x8 grid: rank 8 on top, rank 1 at the bottom, a-file on the
// left. Defined in bitboard.cpp. This is our visual debugger for every table we
// build later — everything downstream is inspected through this lens.
void print_bitboard(Bitboard bb);

} // namespace roj

#endif // ROJ_BITBOARD_H
