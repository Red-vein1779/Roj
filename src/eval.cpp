// Roj chess engine — Phase 2: disposable evaluation stub (see eval.h).
//
// score = sum over white pieces of (material + pst[type][square])
//       - sum over black pieces of (material + pst[type][square ^ 56])
// then returned from the side-to-move's perspective. The `^ 56` flips the rank,
// so Black reads the vertical mirror of White's tables — which makes the eval
// symmetric by construction (verified in tests/test_eval.cpp). Squares are
// a1=0 .. h8=63 exactly as in Phase 1.

#include "eval.h"
#include "bitboard.h"

namespace roj {
namespace {

// Material values (our own, centipawn-like). The king has NO material value: a
// lost king is expressed by mate scoring (value.h), not by material. Indexed by
// PieceType (NO_PIECE_TYPE..KING).
constexpr int MATERIAL[PIECE_TYPE_NB] = {
    0,    // NO_PIECE_TYPE (unused)
    100,  // PAWN
    320,  // KNIGHT
    330,  // BISHOP
    500,  // ROOK
    900,  // QUEEN
    0     // KING
};

// Piece-square tables — our own, deliberately crude midgame values, magnitudes
// within about +/-50 cp, NO game-phase tapering (that is Phase 4). Written from
// WHITE's perspective, indexed by square a1=0 .. h8=63: the FIRST row below is
// rank 1 (a1..h1), the LAST row is rank 8 (a8..h8). Black reads square ^ 56.
// These are disposable (phase2.md section 2): they only nudge pieces toward
// sensible squares so the search has something non-trivial to optimise.
constexpr int PST[PIECE_TYPE_NB][SQUARE_NB] = {
    { 0 },  // NO_PIECE_TYPE (unused)

    {   // PAWN
          0,   0,   0,   0,   0,   0,   0,   0,
          5,   5,   0, -10, -10,   0,   5,   5,
          2,   2,   5,  10,  10,   5,   2,   2,
          4,   4,  10,  20,  20,  10,   4,   4,
          6,   6,  12,  24,  24,  12,   6,   6,
         10,  10,  15,  25,  25,  15,  10,  10,
         20,  20,  20,  30,  30,  20,  20,  20,
          0,   0,   0,   0,   0,   0,   0,   0
    },

    {   // KNIGHT
        -40, -25, -15, -10, -10, -15, -25, -40,
        -25, -10,   0,   5,   5,   0, -10, -25,
        -15,   0,  12,  15,  15,  12,   0, -15,
        -10,   5,  15,  20,  20,  15,   5, -10,
        -10,   5,  15,  20,  20,  15,   5, -10,
        -15,   0,  12,  15,  15,  12,   0, -15,
        -25, -10,   0,   5,   5,   0, -10, -25,
        -40, -25, -15, -10, -10, -15, -25, -40
    },

    {   // BISHOP
        -15,  -8,  -8,  -8,  -8,  -8,  -8, -15,
         -8,   6,   0,   0,   0,   0,   6,  -8,
         -8,   6,   8,   8,   8,   8,   6,  -8,
         -8,   0,   8,  12,  12,   8,   0,  -8,
         -8,   0,   8,  12,  12,   8,   0,  -8,
         -8,   6,   8,   8,   8,   8,   6,  -8,
         -8,   6,   0,   0,   0,   0,   6,  -8,
        -15,  -8,  -8,  -8,  -8,  -8,  -8, -15
    },

    {   // ROOK
          0,   0,   4,   8,   8,   4,   0,   0,
         -4,   0,   0,   4,   4,   0,   0,  -4,
         -4,   0,   0,   0,   0,   0,   0,  -4,
         -4,   0,   0,   0,   0,   0,   0,  -4,
         -4,   0,   0,   0,   0,   0,   0,  -4,
         -4,   0,   0,   0,   0,   0,   0,  -4,
         12,  15,  15,  15,  15,  15,  15,  12,
          0,   0,   4,   8,   8,   4,   0,   0
    },

    {   // QUEEN
        -15,  -8,  -8,  -4,  -4,  -8,  -8, -15,
         -8,   0,   0,   0,   0,   0,   0,  -8,
         -8,   0,   4,   4,   4,   4,   0,  -8,
         -4,   0,   4,   6,   6,   4,   0,  -4,
         -4,   0,   4,   6,   6,   4,   0,  -4,
         -8,   0,   4,   4,   4,   4,   0,  -8,
         -8,   0,   0,   0,   0,   0,   0,  -8,
        -15,  -8,  -8,  -4,  -4,  -8,  -8, -15
    },

    {   // KING (midgame: stay home / castled, avoid the centre)
         25,  30,  12,   0,   0,  12,  30,  25,
         18,  20,   5,  -5,  -5,   5,  20,  18,
          0,   0, -10, -15, -15, -10,   0,   0,
        -10, -15, -20, -25, -25, -20, -15, -10,
        -20, -25, -30, -35, -35, -30, -25, -20,
        -30, -35, -40, -45, -45, -40, -35, -30,
        -35, -40, -45, -50, -50, -45, -40, -35,
        -40, -45, -50, -50, -50, -50, -45, -40
    }
};

} // namespace

int evaluate(const Position& pos) {
    int score = 0;  // white-relative, centipawn-like

    for (int pt = PAWN; pt <= KING; ++pt) {
        Bitboard w = pos.pieces[WHITE][pt];
        while (w) {
            const Square s = pop_lsb(w);
            score += MATERIAL[pt] + PST[pt][s];
        }
        Bitboard b = pos.pieces[BLACK][pt];
        while (b) {
            const Square s = pop_lsb(b);
            score -= MATERIAL[pt] + PST[pt][s ^ 56];
        }
    }

    return (pos.side_to_move == WHITE) ? score : -score;
}

} // namespace roj
