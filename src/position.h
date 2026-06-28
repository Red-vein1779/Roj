// Roj chess engine — board representation and state.
//
// Position is the engine's board. Pieces are stored as bitboards (one per
// colour x piece type). The composite occupancy boards (byColor, occupied) are
// fully derivable from the piece boards but are cached because move generation
// reads them constantly. A running Zobrist hash is kept up to date so the
// transposition table and repetition detection can use it directly later.

#ifndef ROJ_POSITION_H
#define ROJ_POSITION_H

#include "types.h"
#include "bitboard.h"
#include "zobrist.h"

#include <cstdint>
#include <vector>

namespace roj {

// Saved per move so unmake_move can restore what a move destroyed and what
// cannot be recomputed from the resulting position.
struct StateInfo {
    Piece          captured_piece;        // NO_PIECE if the move was not a capture
    CastlingRights prev_castling_rights;
    Square         prev_ep_square;
    int            prev_halfmove_clock;
    std::uint64_t  prev_hash;
};

struct Position {
    // One bitboard per (colour, piece type). Indexed by PieceType PAWN..KING;
    // the [*][NO_PIECE_TYPE] slot is unused.
    Bitboard pieces[COLOR_NB][PIECE_TYPE_NB];

    // Cached composites (derived from `pieces`, kept for speed).
    Bitboard byColor[COLOR_NB];   // all white pieces / all black pieces
    Bitboard occupied;            // all pieces of either colour

    Color          side_to_move;
    CastlingRights castling_rights;
    Square         ep_square;       // SQ_NONE when there is no en-passant target
    int            halfmove_clock;  // plies since last capture or pawn move
    int            fullmove_number; // starts at 1, increments after Black moves
    std::uint64_t  hash;            // incremental Zobrist hash

    // History stack: make_move pushes one StateInfo, unmake_move pops it.
    std::vector<StateInfo> history;

    // Reset to a completely empty board: White to move, no castling rights, no
    // en-passant square, clocks at their start values, and the hash recomputed
    // from scratch (which is 0 for this neutral empty position).
    void clear_board();
};

// Building blocks for make/unmake (step 9): place or remove a single piece on a
// square, keeping the piece bitboard, the byColor/occupied composites AND the
// incremental Zobrist hash consistent. XOR is its own inverse, so both helpers
// XOR the same piece key (set XORs it in, remove XORs it out).
inline void set_piece(Position& pos, Color c, PieceType pt, Square s) {
    set_bit(pos.pieces[c][pt], s);
    set_bit(pos.byColor[c], s);
    set_bit(pos.occupied, s);
    pos.hash ^= ZOBRIST_PIECE[make_piece(c, pt)][s];
}
inline void remove_piece(Position& pos, Color c, PieceType pt, Square s) {
    clear_bit(pos.pieces[c][pt], s);
    clear_bit(pos.byColor[c], s);
    clear_bit(pos.occupied, s);
    pos.hash ^= ZOBRIST_PIECE[make_piece(c, pt)][s];
}

// The piece type on square s (NO_PIECE_TYPE if empty), regardless of colour.
inline PieceType piece_type_on(const Position& pos, Square s) {
    for (int pt = PAWN; pt <= KING; ++pt)
        if (test_bit(pos.pieces[WHITE][pt] | pos.pieces[BLACK][pt], s))
            return static_cast<PieceType>(pt);
    return NO_PIECE_TYPE;
}

// From-scratch Zobrist hash: XOR every present piece's key, the side key (if
// Black to move), the castling-rights key, and the en-passant file key. This is
// the independent oracle used to validate the incremental hash in perft
// (step 13). It walks the whole board, so it must NOT be used in hot paths —
// only in tests and assertions.
std::uint64_t compute_hash_from_scratch(const Position& pos);

// True iff square `sq` is attacked by any piece of colour `by`, given the
// occupancy in `pos`. This is the foundation of check detection, castling
// legality and the make-then-test legality filter. It is evaluated one piece
// type at a time, reusing the leaper tables and the magic sliding-attack lookups.
bool is_attacked(Square sq, Color by, const Position& pos);

// Apply / undo a (pseudo-legal) move, maintaining the bitboards, caches, the
// incremental hash and the history stack. unmake_move restores the position bit
// for bit (it resets the hash directly from the saved StateInfo, which avoids
// any XOR double-counting).
void make_move(Position& pos, Move m);
void unmake_move(Position& pos, Move m);

} // namespace roj

#endif // ROJ_POSITION_H
