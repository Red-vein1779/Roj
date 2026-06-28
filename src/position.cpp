// Roj chess engine — Position helpers: board reset and from-scratch hashing.

#include "position.h"
#include "zobrist.h"
#include "bitboard.h"
#include "attacks.h"
#include "magic.h"

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

bool is_attacked(Square sq, Color by, const Position& pos) {
    // 1) Pawns. A pawn of colour `by` attacks `sq` iff one stands on a square it
    //    could capture from. Those squares are exactly the ones the OPPOSITE
    //    colour's pawn placed on `sq` would attack — hence PAWN_ATTACKS[~by].
    if (PAWN_ATTACKS[~by][sq] & pos.pieces[by][PAWN])
        return true;

    // 2) Knights.
    if (KNIGHT_ATTACKS[sq] & pos.pieces[by][KNIGHT])
        return true;

    // 3) Bishops and queens, along diagonals (occupancy-aware via magics).
    if (bishop_attacks(sq, pos.occupied) & (pos.pieces[by][BISHOP] | pos.pieces[by][QUEEN]))
        return true;

    // 4) Rooks and queens, along ranks/files (occupancy-aware via magics).
    if (rook_attacks(sq, pos.occupied) & (pos.pieces[by][ROOK] | pos.pieces[by][QUEEN]))
        return true;

    // 5) King.
    if (KING_ATTACKS[sq] & pos.pieces[by][KING])
        return true;

    return false;
}

} // namespace roj
