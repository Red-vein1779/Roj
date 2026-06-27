// Roj chess engine — Phase 1, step 5 verification: Position + Zobrist.
//
// The whole point of the from-scratch hash is to be an independent oracle for
// the incremental hash. So every check here confirms that an incrementally
// XOR-updated hash equals compute_hash_from_scratch() of the same position.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_position.cpp src/position.cpp src/zobrist.cpp src/bitboard.cpp -o test_position

#include "../src/position.h"
#include "../src/zobrist.h"
#include "../src/bitboard.h"

#include <cstdint>
#include <iostream>

using namespace roj;

static int g_failures = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        const bool ok = (cond);                                     \
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << #cond << '\n'; \
        if (!ok) ++g_failures;                                      \
    } while (0)

int main() {
    init_zobrist();

    Position pos;
    pos.clear_board();

    // ---------- empty board after clear_board() ----------
    std::cout << "=== clear_board(): empty, White to move, no rights/EP ===\n";
    bool all_pieces_zero = true;
    for (int c = 0; c < COLOR_NB; ++c)
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
            if (pos.pieces[c][pt] != EMPTY_BB) all_pieces_zero = false;

    CHECK(all_pieces_zero);
    CHECK(pos.byColor[WHITE] == EMPTY_BB);
    CHECK(pos.byColor[BLACK] == EMPTY_BB);
    CHECK(pos.occupied == EMPTY_BB);
    CHECK(pos.side_to_move == WHITE);
    CHECK(pos.castling_rights == NO_CASTLING);
    CHECK(pos.ep_square == SQ_NONE);
    CHECK(pos.halfmove_clock == 0);
    CHECK(pos.fullmove_number == 1);
    CHECK(pos.hash == 0);                                  // neutral empty -> 0
    CHECK(compute_hash_from_scratch(pos) == 0);
    CHECK(pos.hash == compute_hash_from_scratch(pos));

    // ---------- place pieces manually, updating hash incrementally ----------
    // Helper: set the bit in every relevant board and XOR the piece key in.
    auto place = [&](Color c, PieceType pt, Square s) {
        set_bit(pos.pieces[c][pt], s);
        set_bit(pos.byColor[c], s);
        set_bit(pos.occupied, s);
        pos.hash ^= ZOBRIST_PIECE[make_piece(c, pt)][s];
    };

    place(WHITE, KING, SQ_E1);
    place(BLACK, KING, SQ_E8);
    place(WHITE, PAWN, SQ_E2);

    std::cout << "\n=== after placing Ke1, Ke8, Pe2 (occupied) ===";
    print_bitboard(pos.occupied);
    std::cout << "incremental hash    = 0x" << std::hex << pos.hash << "\n"
              << "from-scratch oracle = 0x" << compute_hash_from_scratch(pos)
              << std::dec << "\n\n";

    CHECK(popcount(pos.occupied) == 3);
    CHECK(pos.hash == compute_hash_from_scratch(pos));

    // ---------- mutate state incrementally, re-checking against the oracle ----------
    // side to move
    pos.side_to_move = BLACK;
    pos.hash ^= ZOBRIST_SIDE;
    CHECK(pos.hash == compute_hash_from_scratch(pos));

    // castling rights (XOR out the old key, XOR in the new)
    pos.hash ^= ZOBRIST_CASTLING[pos.castling_rights];
    pos.castling_rights = static_cast<CastlingRights>(WHITE_OO | BLACK_OOO);
    pos.hash ^= ZOBRIST_CASTLING[pos.castling_rights];
    CHECK(pos.hash == compute_hash_from_scratch(pos));

    // en-passant square
    pos.ep_square = SQ_E3;
    pos.hash ^= ZOBRIST_EP[file_of(SQ_E3)];
    CHECK(pos.hash == compute_hash_from_scratch(pos));

    std::cout << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
