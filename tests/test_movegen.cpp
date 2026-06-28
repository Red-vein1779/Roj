// Roj chess engine — Phase 1, step 10 verification: pseudo-legal NORMAL movegen.
//
// Hand-countable positions (pseudo-legal, so PINNED moves count too), plus the
// critical pseudo-legality case: an illegal move (leaving the king in check)
// must still be generated here — the legality filter (step 12) removes it.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_movegen.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp src/bitboard.cpp -o test_movegen

#include "../src/movegen.h"
#include "../src/position.h"
#include "../src/fen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"
#include "../src/bitboard.h"
#include "../src/types.h"

#include <iostream>
#include <string>

using namespace roj;

static int g_failures = 0;

static std::string mv(Move m) {
    std::string s;
    s += static_cast<char>('a' + file_of(from_sq(m)));
    s += static_cast<char>('1' + rank_of(from_sq(m)));
    s += static_cast<char>('a' + file_of(to_sq(m)));
    s += static_cast<char>('1' + rank_of(to_sq(m)));
    if (move_type(m) == DOUBLE_PUSH) s += "(dp)";
    return s;
}

static bool list_contains(const MoveList& l, Move m) {
    for (int i = 0; i < l.count; ++i)
        if (l.moves[i] == m) return true;
    return false;
}

// Empty board with a single white piece on `sq`; report move count + a picture
// of the destination squares (popcount of that picture must equal the count).
static void single_piece_case(const char* label, PieceType pt, Square sq, int expected) {
    Position p;
    p.clear_board();
    set_piece(p, WHITE, pt, sq);

    MoveList l;
    generate_moves(p, l);

    Bitboard dest = EMPTY_BB;
    for (int i = 0; i < l.count; ++i)
        set_bit(dest, to_sq(l.moves[i]));

    const bool ok = (l.count == expected);
    if (!ok) ++g_failures;
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << label << ": "
              << l.count << " moves (expected " << expected << ")";
    print_bitboard(dest);
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    // 1. Start position: must equal perft(1) = 20 (no special moves exist here).
    std::cout << "=== 1. start position (expect 20) ===\n";
    {
        Position p;
        parse_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        MoveList l;
        generate_moves(p, l);
        const bool ok = (l.count == 20);
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << l.count << " moves (expected 20):\n  ";
        for (int i = 0; i < l.count; ++i) std::cout << mv(l.moves[i]) << ' ';
        std::cout << "\n";
    }

    // 2-7. Single sliders/leapers on an empty board.
    std::cout << "\n=== 2-7. single piece on an empty board ===\n";
    single_piece_case("knight e4", KNIGHT, SQ_E4, 8);
    single_piece_case("knight a1", KNIGHT, SQ_A1, 2);
    single_piece_case("rook e4",   ROOK,   SQ_E4, 14);
    single_piece_case("bishop e4", BISHOP, SQ_E4, 13);
    single_piece_case("queen e4",  QUEEN,  SQ_E4, 27);
    single_piece_case("king e4",   KING,   SQ_E4, 8);
    single_piece_case("king a1",   KING,   SQ_A1, 3);

    // 8. Pseudo-legality: a pinned rook's illegal move must still be generated.
    std::cout << "\n=== 8. pseudo-legality (pinned move must be generated) ===\n";
    {
        // White Re2 is pinned to Ke1 by black re7. e2d2 leaves the king in check
        // (illegal) but MUST appear in pseudo-legal generation.
        Position p;
        parse_fen(p, "4k3/4r3/8/8/8/8/4R3/4K3 w - - 0 1");
        MoveList l;
        generate_moves(p, l);
        const Move pinned = make_move(SQ_E2, SQ_D2, NORMAL);
        const bool present = list_contains(l, pinned);
        if (!present) ++g_failures;
        std::cout << (present ? "[PASS] " : "[FAIL] ")
                  << "e2d2 (illegal: leaves K in check) is generated: "
                  << (present ? "yes" : "NO")
                  << "   [" << l.count << " pseudo-legal moves total]\n";
    }

    // 9. Pawn captures (forward PAWN_ATTACKS[us][sq] & enemy) + push blocking.
    std::cout << "\n=== 9. pawn captures + blocked push ===\n";
    {
        // White Pe4; black pawns d5, e5, f5. e4 can take d5 and f5 but the push
        // to e5 is blocked -> exactly two pawn moves, both captures.
        Position p;
        parse_fen(p, "4k3/8/8/3ppp2/4P3/8/8/4K3 w - - 0 1");
        MoveList l;
        generate_moves(p, l);

        const bool capL   = list_contains(l, make_move(SQ_E4, SQ_D5, NORMAL));
        const bool capR   = list_contains(l, make_move(SQ_E4, SQ_F5, NORMAL));
        const bool noPush = !list_contains(l, make_move(SQ_E4, SQ_E5, NORMAL));
        int pawn_moves = 0;
        for (int i = 0; i < l.count; ++i)
            if (from_sq(l.moves[i]) == SQ_E4) ++pawn_moves;
        const bool exactly2 = (pawn_moves == 2);

        if (!(capL && capR && noPush && exactly2)) ++g_failures;
        std::cout << (capL    ? "[PASS] " : "[FAIL] ") << "e4xd5 present\n";
        std::cout << (capR    ? "[PASS] " : "[FAIL] ") << "e4xf5 present\n";
        std::cout << (noPush  ? "[PASS] " : "[FAIL] ") << "e4e5 NOT present (push blocked by e5)\n";
        std::cout << (exactly2 ? "[PASS] " : "[FAIL] ")
                  << "pawn e4 makes exactly 2 moves, both captures: got " << pawn_moves << "\n";
    }

    // ===================== SPECIAL MOVES (step 11) =========================
    std::cout << "\n=== 10. promotion ===\n";

    // NOTE: the brief's FENs a/b put the black king on e8, which BLOCKS the
    // e7-e8 push. We test the intended scenario with the king moved off e8, and
    // keep the original (king on e8) as a "push blocked" case below.
    std::cout << "-- quiet promotion (e8 empty): e7 -> e8 = Q/R/B/N (4) --\n";
    {
        Position p; parse_fen(p, "7k/4P3/8/8/8/8/8/4K3 w - - 0 1");
        MoveList l; generate_moves(p, l);
        int pn = 0; for (int i = 0; i < l.count; ++i) if (from_sq(l.moves[i]) == SQ_E7) ++pn;
        const bool ok = pn == 4
            && list_contains(l, make_move(SQ_E7, SQ_E8, promo_flag(QUEEN,  false)))
            && list_contains(l, make_move(SQ_E7, SQ_E8, promo_flag(ROOK,   false)))
            && list_contains(l, make_move(SQ_E7, SQ_E8, promo_flag(BISHOP, false)))
            && list_contains(l, make_move(SQ_E7, SQ_E8, promo_flag(KNIGHT, false)));
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << "4 quiet promotions, all flags present: got " << pn << " pawn moves\n";
    }

    std::cout << "-- promotion with capture (Nd8, e8 empty): e8=4 + exd8=4 = 8 --\n";
    {
        Position p; parse_fen(p, "3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1");
        MoveList l; generate_moves(p, l);
        int pn = 0; for (int i = 0; i < l.count; ++i) if (from_sq(l.moves[i]) == SQ_E7) ++pn;
        const bool ok = pn == 8
            && list_contains(l, make_move(SQ_E7, SQ_E8, promo_flag(QUEEN,  false)))
            && list_contains(l, make_move(SQ_E7, SQ_D8, promo_flag(QUEEN,  true)))
            && list_contains(l, make_move(SQ_E7, SQ_D8, promo_flag(KNIGHT, true)));
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << "8 promotions (4 push + 4 capture): got " << pn << " pawn moves\n";
    }

    std::cout << "-- push BLOCKED (brief's original FEN, Ke8 on the promotion square): 0 --\n";
    {
        Position p; parse_fen(p, "4k3/4P3/8/8/8/8/8/4K3 w - - 0 1");
        MoveList l; generate_moves(p, l);
        int pn = 0; for (int i = 0; i < l.count; ++i) if (from_sq(l.moves[i]) == SQ_E7) ++pn;
        const bool ok = pn == 0;
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << "e7 pawn has 0 moves (push blocked by Ke8, no captures): got " << pn << "\n";
    }

    std::cout << "\n=== 11. en passant ===\n";
    {
        Position p; parse_fen(p, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
        MoveList l; generate_moves(p, l);
        const bool ok = list_contains(l, make_move(SQ_E5, SQ_D6, EN_PASSANT))
                     && list_contains(l, make_move(SQ_E5, SQ_E6, NORMAL));
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << "ep set: e5xd6(EP) present and e5e6 push present\n";
    }
    {
        Position p; parse_fen(p, "4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1");
        MoveList l; generate_moves(p, l);
        const bool ep = list_contains(l, make_move(SQ_E5, SQ_D6, EN_PASSANT));
        if (ep) ++g_failures;
        std::cout << (!ep ? "[PASS] " : "[FAIL] ") << "no ep: e5xd6(EP) NOT generated\n";
    }

    std::cout << "\n=== 12. castling (pseudo-legal) ===\n";
    {
        Position p; parse_fen(p, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        MoveList l; generate_moves(p, l);
        const bool ok = list_contains(l, make_move(SQ_E1, SQ_G1, CASTLING))
                     && list_contains(l, make_move(SQ_E1, SQ_C1, CASTLING));
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << "both empty: e1g1 AND e1c1 generated\n";
    }
    {
        Position p; parse_fen(p, "r3k2r/8/8/8/8/8/8/R2BK2R w KQkq - 0 1");
        MoveList l; generate_moves(p, l);
        const bool oo  = list_contains(l, make_move(SQ_E1, SQ_G1, CASTLING));
        const bool ooo = list_contains(l, make_move(SQ_E1, SQ_C1, CASTLING));
        const bool ok = oo && !ooo;
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << "Bd1: e1g1 present, e1c1 absent (d1 occupied)\n";
    }
    {
        // HEADLINE pseudo-legality: black re2 checks Ke1; castling generated anyway.
        Position p; parse_fen(p, "r3k3/8/8/8/8/8/4r3/R3K2R w KQ - 0 1");
        MoveList l; generate_moves(p, l);
        const bool oo  = list_contains(l, make_move(SQ_E1, SQ_G1, CASTLING));
        const bool ooo = list_contains(l, make_move(SQ_E1, SQ_C1, CASTLING));
        const bool ok = oo && ooo;
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ")
                  << "HEADLINE: Ke1 in check, e1g1 AND e1c1 STILL generated (step 12 removes them): "
                  << (ok ? "yes" : "NO") << "\n";
    }
    {
        // Long-castle trap: b1 must be empty (rook's path).
        Position p; parse_fen(p, "1r2k3/8/8/8/8/8/8/RN2K2R w KQ - 0 1");
        MoveList l; generate_moves(p, l);
        const bool oo  = list_contains(l, make_move(SQ_E1, SQ_G1, CASTLING));
        const bool ooo = list_contains(l, make_move(SQ_E1, SQ_C1, CASTLING));
        const bool ok = oo && !ooo;
        if (!ok) ++g_failures;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << "Nb1: e1g1 present, e1c1 absent (b1 not empty)\n";
    }

    std::cout << "\n"
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
