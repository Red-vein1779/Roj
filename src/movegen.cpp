// Roj chess engine — pseudo-legal move generation (NORMAL moves, step 10).

#include "movegen.h"
#include "bitboard.h"
#include "attacks.h"
#include "magic.h"

#include <string>

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

// Emit the four promotion moves (Q, R, B, N) for a pawn reaching the last rank;
// capturing or quiet depending on `capture`.
void add_promotions(MoveList& list, Square from, Square to, bool capture) {
    list.add(make_move(from, to, promo_flag(QUEEN,  capture)));
    list.add(make_move(from, to, promo_flag(ROOK,   capture)));
    list.add(make_move(from, to, promo_flag(BISHOP, capture)));
    list.add(make_move(from, to, promo_flag(KNIGHT, capture)));
}

} // namespace

void generate_moves(const Position& pos, MoveList& list) {
    const Color    us    = pos.side_to_move;
    const Color    them  = ~us;
    const Bitboard own   = pos.byColor[us];
    const Bitboard enemy = pos.byColor[them];
    const Bitboard occ   = pos.occupied;
    const Bitboard empty = ~occ;

    // --- Pawns (pushes, double push, captures, and promotions) ---------------
    const int      up         = (us == WHITE) ? 8 : -8;
    const Bitboard start_rank = (us == WHITE) ? RANK_2_BB : RANK_7_BB;
    const Bitboard last_rank  = (us == WHITE) ? RANK_8_BB : RANK_1_BB;

    Bitboard pawns = pos.pieces[us][PAWN];
    while (pawns) {
        const Square from = pop_lsb(pawns);
        const Square one  = static_cast<Square>(from + up);

        // Forward push.
        if (test_bit(empty, one)) {
            if (test_bit(last_rank, one)) {
                add_promotions(list, from, one, false);      // four quiet promotions
            } else {
                list.add(make_move(from, one));
                if (test_bit(start_rank, from)) {            // double push
                    const Square two = static_cast<Square>(from + 2 * up);
                    if (test_bit(empty, two))
                        list.add(make_move(from, two, DOUBLE_PUSH));
                }
            }
        }

        // Diagonal captures (a capture onto the last rank is a promotion capture).
        Bitboard caps = PAWN_ATTACKS[us][from] & enemy;
        while (caps) {
            const Square to = pop_lsb(caps);
            if (test_bit(last_rank, to))
                add_promotions(list, from, to, true);        // four capture promotions
            else
                list.add(make_move(from, to));
        }
    }

    // --- En passant (pseudo-legal) -------------------------------------------
    // Own pawns able to capture onto ep_square are exactly those standing on the
    // squares a `them` pawn placed on ep_square would attack. The rare "EP
    // exposes the king along the rank" case is left to step 12 (make-then-test).
    if (pos.ep_square != SQ_NONE) {
        Bitboard ep_from = PAWN_ATTACKS[them][pos.ep_square] & pos.pieces[us][PAWN];
        while (ep_from) {
            const Square from = pop_lsb(ep_from);
            list.add(make_move(from, pos.ep_square, EN_PASSANT));
        }
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

    // --- King ----------------------------------------------------------------
    Bitboard king = pos.pieces[us][KING];
    while (king) {
        const Square from = pop_lsb(king);
        add_moves(list, from, KING_ATTACKS[from] & ~own);
    }

    // --- Castling (PSEUDO-LEGAL) ---------------------------------------------
    // Generated on (a) the right being present and (b) the squares between king
    // and rook being EMPTY. The three attack checks — king not currently in
    // check, the transit square not attacked, the destination not attacked —
    // are deliberately NOT done here; they need is_attacked and belong to the
    // legality filter (step 12), so castles out of / through check ARE generated.
    // Note the queenside trap: the b-file square must be empty (the rook's path)
    // but is never attack-checked, because the king never crosses it.
    if (us == WHITE) {
        if ((pos.castling_rights & WHITE_OO)
            && test_bit(empty, SQ_F1) && test_bit(empty, SQ_G1))
            list.add(make_move(SQ_E1, SQ_G1, CASTLING));
        if ((pos.castling_rights & WHITE_OOO)
            && test_bit(empty, SQ_B1) && test_bit(empty, SQ_C1) && test_bit(empty, SQ_D1))
            list.add(make_move(SQ_E1, SQ_C1, CASTLING));
    } else {
        if ((pos.castling_rights & BLACK_OO)
            && test_bit(empty, SQ_F8) && test_bit(empty, SQ_G8))
            list.add(make_move(SQ_E8, SQ_G8, CASTLING));
        if ((pos.castling_rights & BLACK_OOO)
            && test_bit(empty, SQ_B8) && test_bit(empty, SQ_C8) && test_bit(empty, SQ_D8))
            list.add(make_move(SQ_E8, SQ_C8, CASTLING));
    }
}

void generate_legal_moves(Position& pos, MoveList& list) {
    MoveList pseudo;
    generate_moves(pos, pseudo);

    // The attacker for castling checks is the opponent in the CURRENT position.
    // pos stays in this current state throughout the loop: every make_move below
    // is matched by an unmake_move, and castling moves don't touch pos at all.
    const Color them = ~pos.side_to_move;

    for (int i = 0; i < pseudo.count; ++i) {
        const Move m = pseudo.moves[i];

        if (move_type(m) == CASTLING) {
            // PART 2 — castling predicate (make-then-test cannot see the transit
            // square, which is empty in the FINAL position). Evaluated in the
            // current position: king_from, the transit square (midpoint of the
            // king's two-square move) and the destination must all be unattacked.
            // The b-file square is deliberately NOT checked — the king never
            // crosses it, only the rook does, and rooks may pass attacked squares.
            const Square kfrom   = from_sq(m);
            const Square kto     = to_sq(m);
            const Square transit = static_cast<Square>((kfrom + kto) / 2);
            if (!is_attacked(kfrom,   them, pos)
                && !is_attacked(transit, them, pos)
                && !is_attacked(kto,     them, pos))
                list.add(m);
        } else {
            // PART 1 — general make-then-test. After make_move the side has
            // flipped, so the mover's king is pos.pieces[~side_to_move][KING] and
            // the would-be attacker is the new side_to_move. This one mechanism
            // covers pins, double check, the king fleeing along a ray, and the
            // en-passant discovered check (make_move clears both pawns first).
            make_move(pos, m);
            const Square ksq = lsb(pos.pieces[~pos.side_to_move][KING]);
            const bool illegal = is_attacked(ksq, pos.side_to_move, pos);
            unmake_move(pos, m);
            if (!illegal)
                list.add(m);
        }
    }
}

std::string move_to_uci(Move m) {
    const Square from = from_sq(m);
    const Square to   = to_sq(m);

    std::string s;
    s += static_cast<char>('a' + file_of(from));
    s += static_cast<char>('1' + rank_of(from));
    s += static_cast<char>('a' + file_of(to));
    s += static_cast<char>('1' + rank_of(to));

    if (is_promotion(m)) {
        static const char letters[PIECE_TYPE_NB] = {'?', 'p', 'n', 'b', 'r', 'q', 'k'};
        s += letters[promotion_type(m)];
    }
    return s;
}

} // namespace roj
