// Roj chess engine — Phase 3, Step 6: SEE implementation (see.h for contract).
//
// Our own iterative swap-off algorithm, written from scratch on top of the
// Phase 1 attack infrastructure:
//   1. Seed a gain array with the initiating move's victim value (plus the
//      promotion delta when the initiator promotes).
//   2. Repeatedly let the side to move recapture on the target square with its
//      LEAST VALUABLE attacker, appending the speculative gain
//      gain[d] = occupant_value - gain[d-1] each time, where `occupant` is the
//      piece currently standing on the square.
//   3. Resolve backward: gain[k-1] = -max(-gain[k-1], gain[k]) — each side may
//      always STOP instead of recapturing, so the final gain[0] is the minimax
//      value of the exchange.
// X-rays: after every capture the capturer's square is cleared from the local
// occupancy copy and the sliding attacks onto the target square are recomputed
// with the magic lookups — a bishop/rook/queen that stood BEHIND the capturer
// on the same ray then appears in the fresh attack set automatically. That is
// the whole x-ray mechanism: no ray-walking code, just re-probing the magics
// against the updated occupancy.

#include "see.h"
#include "attacks.h"
#include "magic.h"
#include "bitboard.h"

#include <algorithm>

namespace roj {

namespace {

// Every piece of EITHER colour attacking `s` under occupancy `occ`. Built from
// the leaper tables and magic lookups; pawn attackers of `s` are found by
// projecting the OPPOSITE colour's pawn-attack pattern from `s`.
Bitboard attackers_to(const Position& pos, Square s, Bitboard occ) {
    return (PAWN_ATTACKS[BLACK][s] & pos.pieces[WHITE][PAWN])
         | (PAWN_ATTACKS[WHITE][s] & pos.pieces[BLACK][PAWN])
         | (KNIGHT_ATTACKS[s] & (pos.pieces[WHITE][KNIGHT] | pos.pieces[BLACK][KNIGHT]))
         | (KING_ATTACKS[s]   & (pos.pieces[WHITE][KING]   | pos.pieces[BLACK][KING]))
         | (bishop_attacks(s, occ)
            & (pos.pieces[WHITE][BISHOP] | pos.pieces[BLACK][BISHOP]
             | pos.pieces[WHITE][QUEEN]  | pos.pieces[BLACK][QUEEN]))
         | (rook_attacks(s, occ)
            & (pos.pieces[WHITE][ROOK] | pos.pieces[BLACK][ROOK]
             | pos.pieces[WHITE][QUEEN] | pos.pieces[BLACK][QUEEN]));
}

} // namespace

int see(const Position& pos, Move m) {
    const Square from = from_sq(m);
    const Square to   = to_sq(m);
    const Color  us   = pos.side_to_move;

    // The exchange can involve at most all 32 pieces; 40 slots is safely above.
    int gain[40];
    int d = 0;

    // Local occupancy copy — SEE never touches the real position.
    Bitboard occ = pos.occupied;
    clear_bit(occ, from);

    // Victim of the initiating move. EN PASSANT (documented convention): only
    // the INITIATING move can be an EP capture; the captured pawn sits behind
    // the target square and is removed from the local occupancy here.
    int victim;
    if (move_type(m) == EN_PASSANT) {
        const Square cap = static_cast<Square>(us == WHITE ? to - 8 : to + 8);
        clear_bit(occ, cap);
        victim = SEE_VALUE[PAWN];
    } else {
        const PieceType v = piece_type_on(pos, to);
        victim = (v == NO_PIECE_TYPE) ? 0 : SEE_VALUE[v];   // quiet move: 0
    }

    // The piece now standing on `to`. PROMOTION (documented convention): the
    // initiating promotion adds (promo - pawn) to the gain and the promoted
    // piece — not the pawn — is what the opponent may recapture.
    int occupant = SEE_VALUE[piece_type_on(pos, from)];
    gain[0] = victim;
    if (is_promotion(m)) {
        gain[0]  += SEE_VALUE[promotion_type(m)] - SEE_VALUE[PAWN];
        occupant  = SEE_VALUE[promotion_type(m)];
    }

    Bitboard attackers = attackers_to(pos, to, occ) & occ;
    Color side = ~us;

    while (true) {
        const Bitboard mine = attackers & pos.byColor[side] & occ;
        if (!mine)
            break;

        // Least valuable attacker of `side`. PINNED PIECES (phase3.md §8,
        // accepted imperfection — not a bug, no "fix" without a plan
        // amendment): pins are ignored; every attacker participates as if
        // unpinned.
        PieceType pt = NO_PIECE_TYPE;
        Bitboard  bb = 0;
        for (int p = PAWN; p <= KING; ++p) {
            bb = mine & pos.pieces[side][p];
            if (bb) { pt = static_cast<PieceType>(p); break; }
        }

        // King-last rule: the king may only capture when the opponent has NO
        // attacker left to recapture with (capturing into a defended square
        // would be capturing into check). Enforced here — the king is never
        // fed into the swap list like an ordinary attacker.
        if (pt == KING && (attackers & pos.byColor[~side] & occ))
            break;

        const Square sq = lsb(bb);
        ++d;
        gain[d] = occupant - gain[d - 1];
        occupant = SEE_VALUE[pt];

        // In-sequence pawn promotion (documented convention): a pawn
        // recapturing onto its promotion rank promotes to a queen — the gain
        // swings by (queen - pawn) and the queen becomes the new occupant.
        if (pt == PAWN && rank_of(to) == (side == WHITE ? RANK_8 : RANK_1)) {
            gain[d]  += SEE_VALUE[QUEEN] - SEE_VALUE[PAWN];
            occupant  = SEE_VALUE[QUEEN];
        }

        clear_bit(occ, sq);
        // X-ray refresh: re-probe the sliding attacks with the capturer's
        // square vacated; any slider that was hiding behind it is now in.
        attackers |= (bishop_attacks(to, occ)
                      & (pos.pieces[WHITE][BISHOP] | pos.pieces[BLACK][BISHOP]
                       | pos.pieces[WHITE][QUEEN]  | pos.pieces[BLACK][QUEEN]))
                   | (rook_attacks(to, occ)
                      & (pos.pieces[WHITE][ROOK] | pos.pieces[BLACK][ROOK]
                       | pos.pieces[WHITE][QUEEN] | pos.pieces[BLACK][QUEEN]));
        attackers &= occ;

        side = ~side;
        if (d >= 38)   // structural safety cap; unreachable in legal chess
            break;
    }

    // Backward resolution: either side may stop instead of recapturing.
    while (d > 0) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        --d;
    }
    return gain[0];
}

} // namespace roj
