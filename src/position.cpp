// Roj chess engine — Position helpers: board reset and from-scratch hashing.

#include "position.h"
#include "zobrist.h"
#include "bitboard.h"

namespace roj {

std::uint64_t compute_hash_from_scratch(const Position& pos) {
    std::uint64_t h = 0;

    for (int c = WHITE; c < COLOR_NB; ++c)
        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard b = pos.pieces[c][pt];
            const Piece pc = make_piece(static_cast<Color>(c),
                                        static_cast<PieceType>(pt));
            while (b) {
                const Square s = pop_lsb(b);
                h ^= ZOBRIST_PIECE[pc][s];
            }
        }

    if (pos.side_to_move == BLACK)
        h ^= ZOBRIST_SIDE;

    h ^= ZOBRIST_CASTLING[pos.castling_rights];

    // Phase 1 en-passant convention (KNOWN SIMPLIFICATION): we hash the EP file
    // whenever an ep_square exists, WITHOUT checking that an en-passant capture
    // is actually available. The "only if capturable" refinement is deferred to
    // Phase 2. The incremental update and this oracle MUST share this exact
    // convention, or the per-node hash invariant in perft would false-alarm.
    if (pos.ep_square != SQ_NONE)
        h ^= ZOBRIST_EP[file_of(pos.ep_square)];

    return h;
}

void Position::clear_board() {
    for (int c = 0; c < COLOR_NB; ++c) {
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
            pieces[c][pt] = EMPTY_BB;
        byColor[c] = EMPTY_BB;
    }
    occupied = EMPTY_BB;

    side_to_move    = WHITE;
    castling_rights = NO_CASTLING;
    ep_square       = SQ_NONE;
    halfmove_clock  = 0;
    fullmove_number = 1;

    // Recompute from scratch: empty board + White to move + no rights + no EP,
    // which evaluates to 0.
    hash = compute_hash_from_scratch(*this);
}

} // namespace roj
