// Roj chess engine — leaper attack-table generation (knight, king, pawn).

#include "attacks.h"
#include "bitboard.h"

namespace roj {

Bitboard KNIGHT_ATTACKS[SQUARE_NB];
Bitboard KING_ATTACKS[SQUARE_NB];
Bitboard PAWN_ATTACKS[COLOR_NB][SQUARE_NB];

namespace {

// Is the (file, rank) pair a real square? Checking the FILE bound is exactly
// what prevents the classic "knight on the a-file wraps round to the h-file"
// bug: we work in file/rank space and simply reject any target that steps off
// an edge, so no off-board square is ever set.
bool on_board(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

} // namespace

void init_attack_tables() {
    // The 8 knight jumps and 8 king steps as (file, rank) deltas.
    static const int KN_DF[8] = {-2, -2, -1, -1,  1,  1,  2,  2};
    static const int KN_DR[8] = {-1,  1, -2,  2, -2,  2, -1,  1};
    static const int KG_DF[8] = {-1, -1, -1,  0,  0,  1,  1,  1};
    static const int KG_DR[8] = {-1,  0,  1, -1,  1, -1,  0,  1};

    for (int s = 0; s < SQUARE_NB; ++s) {
        const int f = s & 7;      // file of square s
        const int r = s >> 3;     // rank of square s

        Bitboard knight = EMPTY_BB, king = EMPTY_BB;
        Bitboard white_pawn = EMPTY_BB, black_pawn = EMPTY_BB;

        for (int i = 0; i < 8; ++i) {
            const int knf = f + KN_DF[i], knr = r + KN_DR[i];
            if (on_board(knf, knr))
                set_bit(knight, make_square(static_cast<File>(knf),
                                            static_cast<Rank>(knr)));

            const int gnf = f + KG_DF[i], gnr = r + KG_DR[i];
            if (on_board(gnf, gnr))
                set_bit(king, make_square(static_cast<File>(gnf),
                                          static_cast<Rank>(gnr)));
        }

        // Pawns capture diagonally forward only (never straight ahead).
        if (on_board(f - 1, r + 1))
            set_bit(white_pawn, make_square(static_cast<File>(f - 1), static_cast<Rank>(r + 1)));
        if (on_board(f + 1, r + 1))
            set_bit(white_pawn, make_square(static_cast<File>(f + 1), static_cast<Rank>(r + 1)));
        if (on_board(f - 1, r - 1))
            set_bit(black_pawn, make_square(static_cast<File>(f - 1), static_cast<Rank>(r - 1)));
        if (on_board(f + 1, r - 1))
            set_bit(black_pawn, make_square(static_cast<File>(f + 1), static_cast<Rank>(r - 1)));

        KNIGHT_ATTACKS[s]      = knight;
        KING_ATTACKS[s]        = king;
        PAWN_ATTACKS[WHITE][s] = white_pawn;
        PAWN_ATTACKS[BLACK][s] = black_pawn;
    }
}

} // namespace roj
