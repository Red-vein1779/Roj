// Roj chess engine — Phase 1, step 12 verification: the legality filter.
//
// generate_legal_moves = generate_moves filtered by make-then-test (general) +
// an explicit castling predicate (king_from / transit / destination unattacked,
// and crucially NOT the b-file square). Two counts are checked against PUBLISHED
// perft(1) values (start = 20, Kiwipete = 48).
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_legal.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_legal

#include "../src/movegen.h"
#include "../src/position.h"
#include "../src/fen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"
#include "../src/types.h"

#include <iostream>

using namespace roj;

static int g_failures = 0;

static void report(const char* label, bool ok) {
    if (!ok) ++g_failures;
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << label << "\n";
}

static bool contains(const MoveList& l, Move m) {
    for (int i = 0; i < l.count; ++i)
        if (l.moves[i] == m) return true;
    return false;
}

// Print which of the three castling conditions hold in the current position.
static void castle_diag(const char* which, const Position& pos,
                        Square kfrom, Square transit, Square dest) {
    const Color them = ~pos.side_to_move;
    std::cout << "        " << which
              << ": (a) king "    << (!is_attacked(kfrom,   them, pos) ? "ok" : "ATTACKED")
              << "  (b) transit " << (!is_attacked(transit, them, pos) ? "ok" : "ATTACKED")
              << "  (c) dest "    << (!is_attacked(dest,    them, pos) ? "ok" : "ATTACKED") << "\n";
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    // 1. Start position — published perft(1) = 20.
    std::cout << "=== 1. start position: legal == 20 (PUBLISHED perft(1)) ===\n";
    { Position p; parse_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
      MoveList l; generate_legal_moves(p, l);
      std::cout << "  legal moves: " << l.count << "\n";
      report("start position legal count == 20", l.count == 20); }

    // 2. Kiwipete — published perft(1) = 48 (exercises castling both sides, pins).
    std::cout << "\n=== 2. Kiwipete: legal == 48 (PUBLISHED perft(1)) ===\n";
    { Position p; parse_fen(p, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
      MoveList l; generate_legal_moves(p, l);
      std::cout << "  legal moves: " << l.count << "\n";
      report("Kiwipete legal count == 48", l.count == 48); }

    // 3. Pin: make-then-test removes the rook's off-pin moves.
    std::cout << "\n=== 3. pin (Re2 pinned by re7): pseudo 16 -> legal 9 ===\n";
    { Position p; parse_fen(p, "4k3/4r3/8/8/8/8/4R3/4K3 w - - 0 1");
      MoveList ps; generate_moves(p, ps);
      MoveList lg; generate_legal_moves(p, lg);
      const Move e2d2 = make_move(SQ_E2, SQ_D2, NORMAL);
      std::cout << "  pseudo: " << ps.count << "   legal: " << lg.count << "\n";
      report("pseudo count == 16", ps.count == 16);
      report("legal count == 9 (5 e-file rook + 4 king)", lg.count == 9);
      report("e2d2 PRESENT in pseudo", contains(ps, e2d2));
      report("e2d2 ABSENT in legal (leaves king in check)", !contains(lg, e2d2)); }

    // 4. En passant discovers check along the rank.
    std::cout << "\n=== 4. en passant discovers check: dxe6 dropped ===\n";
    { Position p; parse_fen(p, "4k3/8/8/r2PpK2/8/8/8/8 w - e6 0 1");
      MoveList ps; generate_moves(p, ps);
      MoveList lg; generate_legal_moves(p, lg);
      const Move dxe6 = make_move(SQ_D5, SQ_E6, EN_PASSANT);
      const Move d5d6 = make_move(SQ_D5, SQ_D6, NORMAL);
      report("dxe6(EP) PRESENT in pseudo", contains(ps, dxe6));
      report("dxe6(EP) ABSENT in legal (exposes Kf5 to ra5)", !contains(lg, dxe6));
      report("d5d6 PRESENT in legal (legal push)", contains(lg, d5d6)); }

    // 5. Castling while in check — condition (a).
    std::cout << "\n=== 5. castling, king in check now -> both rejected (cond a) ===\n";
    { Position p; parse_fen(p, "r3k3/8/8/8/8/8/4r3/R3K2R w KQ - 0 1");
      castle_diag("O-O  ", p, SQ_E1, SQ_F1, SQ_G1);
      castle_diag("O-O-O", p, SQ_E1, SQ_D1, SQ_C1);
      MoveList lg; generate_legal_moves(p, lg);
      report("e1g1 ABSENT", !contains(lg, make_move(SQ_E1, SQ_G1, CASTLING)));
      report("e1c1 ABSENT", !contains(lg, make_move(SQ_E1, SQ_C1, CASTLING))); }

    // 6. Kingside transit attacked — condition (b).
    std::cout << "\n=== 6. kingside transit f1 attacked -> e1g1 rejected (cond b) ===\n";
    { Position p; parse_fen(p, "r3k2r/8/8/8/8/8/5r2/R3K2R w KQkq - 0 1");
      castle_diag("O-O  ", p, SQ_E1, SQ_F1, SQ_G1);
      MoveList lg; generate_legal_moves(p, lg);
      report("e1g1 ABSENT (transit f1 attacked)", !contains(lg, make_move(SQ_E1, SQ_G1, CASTLING)));
      report("e1c1 PRESENT", contains(lg, make_move(SQ_E1, SQ_C1, CASTLING))); }

    // 7. Kingside destination attacked — condition (c).
    std::cout << "\n=== 7. kingside destination g1 attacked -> e1g1 rejected (cond c) ===\n";
    { Position p; parse_fen(p, "r3k1r1/8/8/8/8/8/8/R3K2R w KQ - 0 1");
      castle_diag("O-O  ", p, SQ_E1, SQ_F1, SQ_G1);
      MoveList lg; generate_legal_moves(p, lg);
      report("e1g1 ABSENT (dest g1 attacked)", !contains(lg, make_move(SQ_E1, SQ_G1, CASTLING)));
      report("e1c1 PRESENT", contains(lg, make_move(SQ_E1, SQ_C1, CASTLING))); }

    // 8. HEADLINE: b-file trap. b1 attacked but b1 is NOT a checked square.
    std::cout << "\n=== 8. b-file trap (rb8 attacks empty b1) -> e1c1 STILL legal ===\n";
    { Position p; parse_fen(p, "1r2k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
      castle_diag("O-O-O", p, SQ_E1, SQ_D1, SQ_C1);
      std::cout << "        (b1 is attacked by rb8 but is NEVER checked — king does not cross it)\n";
      MoveList lg; generate_legal_moves(p, lg);
      report("e1c1 PRESENT (b1 attack irrelevant)", contains(lg, make_move(SQ_E1, SQ_C1, CASTLING)));
      report("e1g1 PRESENT", contains(lg, make_move(SQ_E1, SQ_G1, CASTLING))); }

    // 9. Fully legal castling — all three conditions pass on both sides.
    std::cout << "\n=== 9. fully legal: both castles present ===\n";
    { Position p; parse_fen(p, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
      MoveList lg; generate_legal_moves(p, lg);
      report("e1g1 PRESENT", contains(lg, make_move(SQ_E1, SQ_G1, CASTLING)));
      report("e1c1 PRESENT", contains(lg, make_move(SQ_E1, SQ_C1, CASTLING))); }

    std::cout << "\n"
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
