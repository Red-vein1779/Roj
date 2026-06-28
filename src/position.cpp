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
    history.clear();

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

namespace {

// AND-mask of castling rights to KEEP when a piece leaves OR arrives on a given
// square. A king square clears that side's two rights; a rook's corner square
// clears one; a rook captured on its corner clears the opponent's right (via the
// move's `to` square). Most squares keep everything.
int castling_keep_mask(Square s) {
    int m = ANY_CASTLING;
    if (s == SQ_E1) m &= ~(WHITE_OO | WHITE_OOO);
    if (s == SQ_H1) m &= ~WHITE_OO;
    if (s == SQ_A1) m &= ~WHITE_OOO;
    if (s == SQ_E8) m &= ~(BLACK_OO | BLACK_OOO);
    if (s == SQ_H8) m &= ~BLACK_OO;
    if (s == SQ_A8) m &= ~BLACK_OOO;
    return m;
}

} // namespace

void make_move(Position& pos, Move m) {
    const Square    from  = from_sq(m);
    const Square    to    = to_sq(m);
    const MoveType  flag  = move_type(m);
    const Color     us    = pos.side_to_move;
    const Color     them  = ~us;
    const PieceType moved = piece_type_on(pos, from);

    // 1) Save everything the move will destroy / change non-reversibly.
    StateInfo st;
    st.captured_piece       = NO_PIECE;
    st.prev_castling_rights = pos.castling_rights;
    st.prev_ep_square       = pos.ep_square;
    st.prev_halfmove_clock  = pos.halfmove_clock;
    st.prev_hash            = pos.hash;

    // 8a) Always clear any existing en-passant square from the hash; a fresh one
    //     is set below only on a double push.
    if (pos.ep_square != SQ_NONE)
        pos.hash ^= ZOBRIST_EP[file_of(pos.ep_square)];
    pos.ep_square = SQ_NONE;

    // 3) Captures. En passant removes the pawn BEHIND `to`, not on it.
    if (flag == EN_PASSANT) {
        const Square cap_sq = static_cast<Square>(to + (us == WHITE ? -8 : 8));
        st.captured_piece = make_piece(them, PAWN);
        remove_piece(pos, them, PAWN, cap_sq);
    } else if (test_bit(pos.byColor[them], to)) {
        const PieceType captured = piece_type_on(pos, to);
        st.captured_piece = make_piece(them, captured);
        remove_piece(pos, them, captured, to);
    }

    // 4/5) Move our own piece; on promotion the destination gets the new piece.
    remove_piece(pos, us, moved, from);
    if (is_promotion(m))
        set_piece(pos, us, promotion_type(m), to);
    else
        set_piece(pos, us, moved, to);

    // 6) Castling also relocates the rook (side implied by the king's `to` file).
    if (flag == CASTLING) {
        Square rook_from, rook_to;
        if (to > from) {                                 // kingside: rook h -> f
            rook_from = static_cast<Square>(to + 1);
            rook_to   = static_cast<Square>(to - 1);
        } else {                                         // queenside: rook a -> d
            rook_from = static_cast<Square>(to - 2);
            rook_to   = static_cast<Square>(to + 1);
        }
        remove_piece(pos, us, ROOK, rook_from);
        set_piece(pos, us, ROOK, rook_to);
    }

    // 7) Castling rights: XOR the old key out, recompute, XOR the new key in.
    pos.hash ^= ZOBRIST_CASTLING[pos.castling_rights];
    pos.castling_rights = static_cast<CastlingRights>(
        pos.castling_rights & castling_keep_mask(from) & castling_keep_mask(to));
    pos.hash ^= ZOBRIST_CASTLING[pos.castling_rights];

    // 8b) A double push exposes a new en-passant square (behind the pawn).
    if (flag == DOUBLE_PUSH) {
        pos.ep_square = static_cast<Square>(to + (us == WHITE ? -8 : 8));
        pos.hash ^= ZOBRIST_EP[file_of(pos.ep_square)];
    }

    // 9) Halfmove clock: reset on a pawn move or a capture, else increment.
    if (moved == PAWN || st.captured_piece != NO_PIECE)
        pos.halfmove_clock = 0;
    else
        pos.halfmove_clock++;

    // 10) Fullmove number increments once Black has completed a move.
    if (us == BLACK)
        pos.fullmove_number++;

    // 11) Switch the side to move.
    pos.side_to_move = them;
    pos.hash ^= ZOBRIST_SIDE;

    pos.history.push_back(st);
}

void unmake_move(Position& pos, Move m) {
    const StateInfo st = pos.history.back();
    pos.history.pop_back();

    // 11') Flip the side back; `us` is again the side that made the move.
    pos.side_to_move = ~pos.side_to_move;
    const Color us   = pos.side_to_move;
    const Color them = ~us;

    const Square   from = from_sq(m);
    const Square   to   = to_sq(m);
    const MoveType flag = move_type(m);

    // 10') Undo the fullmove increment when taking back a Black move.
    if (us == BLACK)
        pos.fullmove_number--;

    // Move our piece back. On promotion, remove the promoted piece and put a pawn
    // back on `from`.
    if (is_promotion(m)) {
        remove_piece(pos, us, promotion_type(m), to);
        set_piece(pos, us, PAWN, from);
    } else {
        const PieceType moved = piece_type_on(pos, to);
        remove_piece(pos, us, moved, to);
        set_piece(pos, us, moved, from);
    }

    // Restore the captured piece, if any (en passant: behind `to`).
    if (flag == EN_PASSANT) {
        const Square cap_sq = static_cast<Square>(to + (us == WHITE ? -8 : 8));
        set_piece(pos, them, PAWN, cap_sq);
    } else if (st.captured_piece != NO_PIECE) {
        set_piece(pos, them, type_of(st.captured_piece), to);
    }

    // Castling: move the rook back.
    if (flag == CASTLING) {
        Square rook_from, rook_to;
        if (to > from) {
            rook_from = static_cast<Square>(to + 1);
            rook_to   = static_cast<Square>(to - 1);
        } else {
            rook_from = static_cast<Square>(to - 2);
            rook_to   = static_cast<Square>(to + 1);
        }
        remove_piece(pos, us, ROOK, rook_to);
        set_piece(pos, us, ROOK, rook_from);
    }

    // Restore scalar state and the hash directly from the saved StateInfo. The
    // hash churn from set/remove_piece above is intentionally discarded here.
    pos.castling_rights = st.prev_castling_rights;
    pos.ep_square       = st.prev_ep_square;
    pos.halfmove_clock  = st.prev_halfmove_clock;
    pos.hash            = st.prev_hash;
}

} // namespace roj
