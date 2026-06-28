// Roj chess engine — Phase 1, step 8 verification: is_attacked / check detection.
//
// Each case sets up a position by FEN and asks is_attacked(sq, by). For evidence
// we also probe each of the five layers separately and print which ones fire.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_check.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_check

#include "../src/position.h"
#include "../src/fen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/bitboard.h"
#include "../src/zobrist.h"

#include <iostream>
#include <string>

using namespace roj;

static int g_failures = 0;

// Report which of the five layers see `sq` as attacked by `by` (diagnostics).
static void which_layers(Square sq, Color by, const Position& pos) {
    std::cout << "        layers firing:";
    bool any = false;
    if (PAWN_ATTACKS[~by][sq] & pos.pieces[by][PAWN])   { std::cout << " pawn";        any = true; }
    if (KNIGHT_ATTACKS[sq] & pos.pieces[by][KNIGHT])    { std::cout << " knight";      any = true; }
    if (bishop_attacks(sq, pos.occupied) & (pos.pieces[by][BISHOP] | pos.pieces[by][QUEEN]))
                                                        { std::cout << " bishop/queen"; any = true; }
    if (rook_attacks(sq, pos.occupied) & (pos.pieces[by][ROOK] | pos.pieces[by][QUEEN]))
                                                        { std::cout << " rook/queen";   any = true; }
    if (KING_ATTACKS[sq] & pos.pieces[by][KING])        { std::cout << " king";        any = true; }
    if (!any) std::cout << " (none)";
    std::cout << "\n";
}

static void check_attack(const char* label, const Position& pos,
                         Square sq, Color by, bool expected) {
    const bool got = is_attacked(sq, by, pos);
    const bool ok = (got == expected);
    if (!ok) ++g_failures;
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << label
              << "  -> " << (got ? "true" : "false")
              << " (expected " << (expected ? "true" : "false") << ")\n";
    which_layers(sq, by, pos);
}

static void test_case(const char* label, const char* fen,
                      Square sq, Color by, bool expected) {
    Position pos;
    parse_fen(pos, fen);
    check_attack(label, pos, sq, by, expected);
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    const char* startpos = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

    test_case("1. startpos: e2 attacked by WHITE", startpos, SQ_E2, WHITE, true);
    test_case("2. startpos: e4 attacked by WHITE", startpos, SQ_E4, WHITE, false);
    test_case("3. startpos: e2 attacked by BLACK", startpos, SQ_E2, BLACK, false);
    test_case("4a. kiwipete: e8 (black king) attacked by WHITE", kiwipete, SQ_E8, WHITE, false);
    test_case("4b. kiwipete: e1 (white king) attacked by BLACK", kiwipete, SQ_E1, BLACK, false);
    test_case("5. Re1 vs ke8, empty file: e8 by WHITE",
              "4k3/8/8/8/8/8/8/4R3 w - - 0 1", SQ_E8, WHITE, true);
    test_case("6. Re1 vs ke8, black pawn e4 blocks: e8 by WHITE",
              "4k3/8/8/8/4p3/8/8/4R3 w - - 0 1", SQ_E8, WHITE, false);

    // Pawn layer in isolation, both directions (confirms PAWN_ATTACKS[~by][sq]).
    std::cout << "\n=== pawn layer (isolated) ===\n";
    {
        Position a;
        a.clear_board();
        set_piece(a, WHITE, PAWN, SQ_E4);          // a lone white pawn on e4
        check_attack("A. white pawn e4: d5 by WHITE", a, SQ_D5, WHITE, true);
        check_attack("A. white pawn e4: f5 by WHITE", a, SQ_F5, WHITE, true);
        check_attack("A. white pawn e4: e5 by WHITE", a, SQ_E5, WHITE, false);
    }
    {
        Position b;
        b.clear_board();
        set_piece(b, BLACK, PAWN, SQ_E5);          // a lone black pawn on e5
        check_attack("B. black pawn e5: d4 by BLACK", b, SQ_D4, BLACK, true);
        check_attack("B. black pawn e5: f4 by BLACK", b, SQ_F4, BLACK, true);
        check_attack("B. black pawn e5: e4 by BLACK", b, SQ_E4, BLACK, false);
    }

    std::cout << "\n"
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
