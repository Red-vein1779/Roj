// Roj chess engine — Phase 2, Step 1: evaluation symmetry test.
//
// This is Step 1's central "done when" (phase2.md section 2 / section 7): the
// eval must be symmetric. We check it two complementary ways per position and
// also that the start position evaluates to exactly 0.
//
//   (a) NEGATION (the locked section 2/7 form): mirror = swap piece colours and
//       flip the board vertically (square ^ 56), KEEPING the side to move. With a
//       side-to-move-relative eval this gives  eval(pos) == -eval(mirror).
//   (b) EQUALITY (the full physical mirror): additionally swap the side to move.
//       Then the two sign flips cancel and the relation is eval(pos) ==
//       eval(mirror). This is the stronger check — it also exercises the
//       side-to-move sign, which (a) alone does not.
//
// NOTE on the spec: the Step 1 brief described the mirror as "swap colours, flip
// vertically, AND swap the side to move" while asserting eval == -eval(mirror).
// Those two are inconsistent for a side-relative eval (swapping the side to move
// turns the relation into equality). We therefore implement BOTH forms above so
// the locked negation form is verified literally and the full-mirror form is
// verified with its correct (equality) sign.
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_eval.cpp src/eval.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_eval

#include "../src/eval.h"
#include "../src/position.h"
#include "../src/fen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"
#include "../src/bitboard.h"
#include "../src/types.h"

#include <iostream>

using namespace roj;

static int g_failures = 0;

// Build the mirror of `pos`: swap piece colours and flip the board vertically
// (square ^ 56). The eval reads only piece placement and side to move, so this
// is sufficient; castling/EP are irrelevant to it. `swap_stm` chooses the form:
//   false -> colours + vertical flip, SAME side to move   (negation form)
//   true  -> colours + vertical flip + swap side to move  (full physical mirror)
static Position mirror(const Position& pos, bool swap_stm) {
    Position m;
    m.clear_board();
    for (int pt = PAWN; pt <= KING; ++pt) {
        Bitboard w = pos.pieces[WHITE][pt];
        while (w) {
            const Square s = pop_lsb(w);
            set_piece(m, BLACK, static_cast<PieceType>(pt), static_cast<Square>(s ^ 56));
        }
        Bitboard b = pos.pieces[BLACK][pt];
        while (b) {
            const Square s = pop_lsb(b);
            set_piece(m, WHITE, static_cast<PieceType>(pt), static_cast<Square>(s ^ 56));
        }
    }
    m.side_to_move = swap_stm ? ~pos.side_to_move : pos.side_to_move;
    return m;
}

static void test_position(const char* name, const char* fen) {
    Position pos;
    if (!parse_fen(pos, fen)) {
        ++g_failures;
        std::cout << "  FAIL: could not parse FEN for " << name << "\n";
        return;
    }
    const int e = evaluate(pos);

    // (a) negation form (locked section 2/7): same side to move.
    const int e_neg = evaluate(mirror(pos, /*swap_stm=*/false));
    if (e != -e_neg) {
        ++g_failures;
        std::cout << "  FAIL [" << name << "] negation: eval=" << e
                  << "  -eval(colour+flip)=" << -e_neg << "\n";
    }

    // (b) equality form (full physical mirror): also swap the side to move.
    const int e_eq = evaluate(mirror(pos, /*swap_stm=*/true));
    if (e != e_eq) {
        ++g_failures;
        std::cout << "  FAIL [" << name << "] equality: eval=" << e
                  << "  eval(full mirror)=" << e_eq << "\n";
    }
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    struct FenCase { const char* name; const char* fen; };
    const FenCase cases[] = {
        // The six standard perft positions.
        {"P1 Start",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"P2 Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
        {"P3",          "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
        {"P4",          "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
        {"P5",          "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"},
        {"P6",          "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
        // A handful of varied extra positions (both sides to move, lopsided
        // material, bare endgames).
        {"Italian",     "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"},
        {"KQ vs k",     "8/8/8/4k3/8/8/8/3QK3 w - - 0 1"},
        {"Kings only",  "8/5k2/8/8/8/8/2K5/8 w - - 0 1"},
        {"Lopsided",    "rnbqkbnr/pppppppp/8/8/8/8/8/4K3 w - - 0 1"},
        {"Endgame b2m", "8/8/4k3/8/8/2K5/5R2/3b4 b - - 0 1"}
    };

    for (const FenCase& c : cases)
        test_position(c.name, c.fen);

    // The start position is its own mirror (up to side to move) and must score 0.
    {
        Position start;
        parse_fen(start, START_FEN);
        const int e = evaluate(start);
        if (e != 0) {
            ++g_failures;
            std::cout << "  FAIL: start position eval != 0 (got " << e << ")\n";
        }
    }

    if (g_failures == 0) {
        std::cout << "test_eval: SYMMETRY HOLDS ON ALL POSITIONS; start == 0 -- PASS\n";
        return 0;
    }
    std::cout << "test_eval: FAILURES = " << g_failures << "\n";
    return 1;
}
