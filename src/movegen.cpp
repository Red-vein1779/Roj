// Roj chess engine — pseudo-legal move generation (NORMAL moves, step 10).

#include "movegen.h"
#include "bitboard.h"
#include "attacks.h"
#include "magic.h"

namespace roj {

namespace {

constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;
constexpr Bitboard RANK_2_BB = 0x000000000000FF00ULL;
constexpr Bitboard RANK_7_BB = 0x00FF000000000000ULL;
constexpr Bitboard RANK_8_BB = 0xFF00000000000000ULL;

// Emit a NORMAL move from `from` to every square in `targets`.
//
// QUIESCENCE PREP: every non-pawn piece below computes its full target set as
// (attacks & ~own) == (attacks & enemy) | (attacks & empty), i.e. captures OR
// quiets. Splitting generation later is then a one-line change: pass
// `attacks & enemy` for a captures-only generator and `attacks & ~occupied` for
// a quiets-only generator, calling add_moves with each subset instead of the
// union. The pawn section is likewise already split into pushes (quiet) and
// diagonal captures.
void add_moves(MoveList& list, Square from, Bitboard targets) {
    while (targets)
        list.add(make_move(from, pop_lsb(targets)));
}

} // namespace

void generate_moves(const Position& pos, MoveList& list) {
    const Color    us    = pos.side_to_move;
    const Color    them  = ~us;
    const Bitboard own   = pos.byColor[us];
    const Bitboard enemy = pos.byColor[them];
    const Bitboard occ   = pos.occupied;
    const Bitboard empty = ~occ;

    // --- Pawns (NORMAL only) -------------------------------------------------
    // Pushes and diagonal captures, but any move landing on the last rank is a
    // PROMOTION and is skipped here (generated in step 11).
    const int      up         = (us == WHITE) ? 8 : -8;
    const Bitboard start_rank = (us == WHITE) ? RANK_2_BB : RANK_7_BB;
    const Bitboard last_rank  = (us == WHITE) ? RANK_8_BB : RANK_1_BB;

    Bitboard pawns = pos.pieces[us][PAWN];
    while (pawns) {
        const Square from = pop_lsb(pawns);
        const Square one  = static_cast<Square>(from + up);

        // Single push (if the square ahead is empty and not a promotion).
        if (test_bit(empty, one) && !test_bit(last_rank, one)) {
            list.add(make_move(from, one));

            // Double push: only from the start rank, both squares ahead empty.
            if (test_bit(start_rank, from)) {
                const Square two = static_cast<Square>(from + 2 * up);
                if (test_bit(empty, two))
                    list.add(make_move(from, two, DOUBLE_PUSH));
            }
        }

        // Diagonal captures (excluding promotion captures, handled in step 11).
        add_moves(list, from, PAWN_ATTACKS[us][from] & enemy & ~last_rank);
    }

    // --- Knights -------------------------------------------------------------
    Bitboard knights = pos.pieces[us][KNIGHT];
    while (knights) {
        const Square from = pop_lsb(knights);
        add_moves(list, from, KNIGHT_ATTACKS[from] & ~own);
    }

    // --- Bishops + queens (diagonal sliders) ---------------------------------
    Bitboard diagonal = pos.pieces[us][BISHOP] | pos.pieces[us][QUEEN];
    while (diagonal) {
        const Square from = pop_lsb(diagonal);
        add_moves(list, from, bishop_attacks(from, occ) & ~own);
    }

    // --- Rooks + queens (orthogonal sliders) ---------------------------------
    Bitboard orthogonal = pos.pieces[us][ROOK] | pos.pieces[us][QUEEN];
    while (orthogonal) {
        const Square from = pop_lsb(orthogonal);
        add_moves(list, from, rook_attacks(from, occ) & ~own);
    }

    // --- King (no castling here; step 11) ------------------------------------
    Bitboard king = pos.pieces[us][KING];
    while (king) {
        const Square from = pop_lsb(king);
        add_moves(list, from, KING_ATTACKS[from] & ~own);
    }
}

} // namespace roj
