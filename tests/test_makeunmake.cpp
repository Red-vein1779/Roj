// Roj chess engine — Phase 1, step 9 verification: make/unmake round-trip.
//
// The strongest pre-perft test: for each move, snapshot the position, make_move,
// assert the incremental hash equals the from-scratch oracle (DoD item 2 in
// miniature), unmake_move, and require the position to be restored BIT FOR BIT.
// Also a multi-move sequence: push several, check the hash after each, unmake all
// in reverse, and confirm we are back to the start.
//
// HONEST CAVEAT: round-trip + hash-invariant catch make/unmake asymmetry and
// incremental-hash errors, but full proof comes only when perft (steps 13-14)
// runs thousands of real move sequences.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_makeunmake.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_makeunmake

#include "../src/position.h"
#include "../src/fen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"
#include "../src/types.h"

#include <iostream>

using namespace roj;

static int g_failures = 0;

// Full board-state equality (everything except the internal history vector).
static bool same_position(const Position& a, const Position& b) {
    for (int c = 0; c < COLOR_NB; ++c) {
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
            if (a.pieces[c][pt] != b.pieces[c][pt]) return false;
        if (a.byColor[c] != b.byColor[c]) return false;
    }
    return a.occupied        == b.occupied
        && a.side_to_move    == b.side_to_move
        && a.castling_rights == b.castling_rights
        && a.ep_square       == b.ep_square
        && a.halfmove_clock  == b.halfmove_clock
        && a.fullmove_number == b.fullmove_number
        && a.hash            == b.hash;
}

// make -> check hash invariant -> unmake -> check bit-for-bit restore.
static void roundtrip(Position& pos, Move mv, const char* label) {
    const Position snap = pos;
    make_move(pos, mv);
    const bool hash_ok = (pos.hash == compute_hash_from_scratch(pos));
    unmake_move(pos, mv);
    const bool restore_ok =
        same_position(pos, snap) && (pos.history.size() == snap.history.size());
    if (!hash_ok || !restore_ok) ++g_failures;
    std::cout << ((hash_ok && restore_ok) ? "[PASS] " : "[FAIL] ") << label
              << "  | hash-after-make: " << (hash_ok ? "ok" : "BAD")
              << "  | restore: " << (restore_ok ? "ok" : "BAD") << "\n";
}

// Resulting-board assertions: round-trip proves a move is reversible, not that
// the destination board is correct. These confirm the actual squares after make.
#define CHECK(cond)                                                            \
    do {                                                                       \
        const bool check_ok_ = (cond);                                         \
        std::cout << (check_ok_ ? "  [PASS] " : "  [FAIL] ") << #cond << '\n'; \
        if (!check_ok_) ++g_failures;                                          \
    } while (0)

static bool has(const Position& pos, Color c, PieceType pt, Square s) {
    return test_bit(pos.pieces[c][pt], s);
}
static bool empty_sq(const Position& pos, Square s) {
    return !test_bit(pos.occupied, s);
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    const char* startpos = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    const char* eppos    = "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3";
    const char* promopos = "5b1k/4P3/8/8/8/8/8/4K3 w - - 0 1";

    std::cout << "=== 1. startpos ===\n";
    { Position p; parse_fen(p, startpos);
      roundtrip(p, make_move(SQ_E2, SQ_E4, DOUBLE_PUSH), "e2e4  double push (EP set, then cleared)");
      roundtrip(p, make_move(SQ_G1, SQ_F3, NORMAL),      "g1f3  knight (quiet)");
      roundtrip(p, make_move(SQ_B1, SQ_C3, NORMAL),      "b1c3  knight (quiet)");
    }

    std::cout << "\n=== 2. kiwipete ===\n";
    { Position p; parse_fen(p, kiwipete);
      roundtrip(p, make_move(SQ_D5, SQ_E6, NORMAL),   "d5xe6 pawn capture");
      roundtrip(p, make_move(SQ_E1, SQ_G1, CASTLING), "e1g1  castle kingside");
      roundtrip(p, make_move(SQ_E1, SQ_C1, CASTLING), "e1c1  castle queenside");
      roundtrip(p, make_move(SQ_A1, SQ_B1, NORMAL),   "a1b1  rook move (loses O-O-O right)");
    }

    std::cout << "\n=== 3. en passant ===\n";
    { Position p; parse_fen(p, eppos);
      roundtrip(p, make_move(SQ_E5, SQ_D6, EN_PASSANT), "e5xd6 en passant (removes d5 pawn)");
    }

    std::cout << "\n=== 4. promotion (with and without capture) ===\n";
    { Position p; parse_fen(p, promopos);
      roundtrip(p, make_move(SQ_E7, SQ_E8, promo_flag(QUEEN,  false)), "e7e8=Q");
      roundtrip(p, make_move(SQ_E7, SQ_E8, promo_flag(ROOK,   false)), "e7e8=R");
      roundtrip(p, make_move(SQ_E7, SQ_E8, promo_flag(BISHOP, false)), "e7e8=B");
      roundtrip(p, make_move(SQ_E7, SQ_E8, promo_flag(KNIGHT, false)), "e7e8=N");
      roundtrip(p, make_move(SQ_E7, SQ_F8, promo_flag(QUEEN,  true)),  "e7xf8=Q (captures bishop)");
      roundtrip(p, make_move(SQ_E7, SQ_F8, promo_flag(ROOK,   true)),  "e7xf8=R");
      roundtrip(p, make_move(SQ_E7, SQ_F8, promo_flag(BISHOP, true)),  "e7xf8=B");
      roundtrip(p, make_move(SQ_E7, SQ_F8, promo_flag(KNIGHT, true)),  "e7xf8=N");
    }

    std::cout << "\n=== 5. sequence: 5 moves, hash after each, then unmake all ===\n";
    {
        Position p; parse_fen(p, startpos);
        const Position start = p;

        struct Step { Move m; const char* name; };
        const Step seq[] = {
            { make_move(SQ_E2, SQ_E4, DOUBLE_PUSH), "e2e4" },
            { make_move(SQ_E7, SQ_E5, DOUBLE_PUSH), "e7e5" },
            { make_move(SQ_G1, SQ_F3, NORMAL),      "g1f3" },
            { make_move(SQ_B8, SQ_C6, NORMAL),      "b8c6" },
            { make_move(SQ_F1, SQ_B5, NORMAL),      "f1b5" },
        };
        const int n = 5;

        for (int i = 0; i < n; ++i) {
            make_move(p, seq[i].m);
            const bool ok = (p.hash == compute_hash_from_scratch(p));
            if (!ok) ++g_failures;
            std::cout << "  after " << seq[i].name << ": hash " << (ok ? "ok" : "BAD")
                      << "  (stack depth " << p.history.size() << ")\n";
        }
        for (int i = n - 1; i >= 0; --i)
            unmake_move(p, seq[i].m);

        const bool back = same_position(p, start) && (p.history.empty());
        if (!back) ++g_failures;
        std::cout << (back ? "[PASS] " : "[FAIL] ")
                  << "unmade all 5 -> bit-for-bit identical to startpos (stack depth "
                  << p.history.size() << ")\n";
    }

    std::cout << "\n=== 6. resulting-state assertions (tricky moves) ===\n";

    std::cout << "-- castle kingside (Kiwipete): K g1, R f1, e1/h1 empty, K+Q rights gone --\n";
    { Position p; parse_fen(p, kiwipete);
      make_move(p, make_move(SQ_E1, SQ_G1, CASTLING));
      CHECK(has(p, WHITE, KING, SQ_G1));
      CHECK(has(p, WHITE, ROOK, SQ_F1));
      CHECK(empty_sq(p, SQ_E1));
      CHECK(empty_sq(p, SQ_H1));
      CHECK((p.castling_rights & (WHITE_OO | WHITE_OOO)) == 0); }

    std::cout << "-- castle queenside (Kiwipete): K c1, R d1, e1/a1 empty --\n";
    { Position p; parse_fen(p, kiwipete);
      make_move(p, make_move(SQ_E1, SQ_C1, CASTLING));
      CHECK(has(p, WHITE, KING, SQ_C1));
      CHECK(has(p, WHITE, ROOK, SQ_D1));
      CHECK(empty_sq(p, SQ_E1));
      CHECK(empty_sq(p, SQ_A1)); }

    std::cout << "-- en passant: white pawn to d6, d5 (captured pawn) and e5 emptied --\n";
    { Position p; parse_fen(p, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
      make_move(p, make_move(SQ_E5, SQ_D6, EN_PASSANT));
      CHECK(has(p, WHITE, PAWN, SQ_D6));
      CHECK(empty_sq(p, SQ_D5));
      CHECK(empty_sq(p, SQ_E5)); }

    std::cout << "-- promotion, no capture: queen on e8, NO pawn on e8, e7 empty --\n";
    { Position p; parse_fen(p, "4k3/4P3/8/8/8/8/8/4K3 w - - 0 1");
      make_move(p, make_move(SQ_E7, SQ_E8, promo_flag(QUEEN, false)));
      CHECK(has(p, WHITE, QUEEN, SQ_E8));
      CHECK(!has(p, WHITE, PAWN, SQ_E8));
      CHECK(empty_sq(p, SQ_E7)); }

    std::cout << "-- promotion, with capture: knight on f8, black rook gone, e7 empty --\n";
    { Position p; parse_fen(p, "5r2/4P3/8/8/8/8/8/k3K3 w - - 0 1");
      make_move(p, make_move(SQ_E7, SQ_F8, promo_flag(KNIGHT, true)));
      CHECK(has(p, WHITE, KNIGHT, SQ_F8));
      CHECK(!has(p, BLACK, ROOK, SQ_F8));
      CHECK(empty_sq(p, SQ_E7)); }

    std::cout << "-- HEADLINE: a1xa8 clears BOTH white & black O-O-O (from + to mask) --\n";
    { Position p; parse_fen(p, "r3k3/8/8/8/8/8/8/R3K3 w Qq - 0 1");
      make_move(p, make_move(SQ_A1, SQ_A8, NORMAL));
      CHECK(has(p, WHITE, ROOK, SQ_A8));
      CHECK(empty_sq(p, SQ_A1));
      CHECK(p.castling_rights == NO_CASTLING);
      CHECK(p.hash == compute_hash_from_scratch(p)); }

    std::cout << "-- symmetry: h8xh1 clears BOTH white & black O-O --\n";
    { Position p; parse_fen(p, "4k2r/8/8/8/8/8/8/4K2R b Kk - 0 1");
      make_move(p, make_move(SQ_H8, SQ_H1, NORMAL));
      CHECK(has(p, BLACK, ROOK, SQ_H1));
      CHECK(empty_sq(p, SQ_H8));
      CHECK(p.castling_rights == NO_CASTLING);
      CHECK(p.hash == compute_hash_from_scratch(p)); }

    std::cout << "\n"
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
