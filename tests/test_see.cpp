// Roj chess engine — Phase 3, Step 6: SEE vs brute-force oracle (phase3.md §2.2).
//
// The ORACLE is written independently of see.cpp by construction: instead of a
// swap array over attack bitboards it plays the capture sequence out for real —
// make/unmake on a scratch position, minimaxing the material outcome over the
// PSEUDO-LEGAL capture continuations on the target square (each side may also
// stop). Pseudo-legal deliberately models phase3.md §8's accepted
// simplification (pinned pieces participate as if unpinned); the king-last rule
// falls out naturally because a king recapture is skipped iff the landing
// square is attacked afterwards (a real is_attacked test after the real make).
//
// Suite: hand-built categories (a)-(f) + a large seeded-random batch (g):
//  a. simple captures, b. x-ray stacks, c. king-participates / king-declines,
//  d. promotion captures, e. en-passant initiators, f. pinned-piece divergence
//  (SEE's documented simplification vs a pin-aware playout — demonstrated, not
//  just asserted), g. 1500+ random legal (position, capture) pairs from seeded
//  random walks. Fully deterministic — this is a CI gate.
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_see.cpp src/see.cpp src/position.cpp src/fen.cpp src/movegen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp src/bitboard.cpp -o test_see

#include "../src/see.h"
#include "../src/position.h"
#include "../src/fen.h"
#include "../src/movegen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"
#include "../src/bitboard.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

using namespace roj;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

static bool is_capture_like(const Position& pos, Move m) {
    if (move_type(m) == EN_PASSANT) return true;
    return test_bit(pos.byColor[~pos.side_to_move], to_sq(m));
}

// ---- Brute-force oracle ------------------------------------------------------
// Best material outcome for the side to move, who may either STOP (0) or play
// any pseudo-legal capture landing on `to` and hand the turn over.
static int oracle_exchange(Position& pos, Square to) {
    int best = 0;
    MoveList ml;
    generate_moves(pos, ml);                       // PSEUDO-legal: pins ignored (§8)
    for (int i = 0; i < ml.count; ++i) {
        const Move m = ml.moves[i];
        if (to_sq(m) != to || !is_capture_like(pos, m))
            continue;
        if (move_type(m) == EN_PASSANT)
            continue;                              // in-sequence EP never re-targets `to`
        const PieceType victim = piece_type_on(pos, to);
        const PieceType mover  = piece_type_on(pos, from_sq(m));
        int v = SEE_VALUE[victim];
        if (is_promotion(m))
            v += SEE_VALUE[promotion_type(m)] - SEE_VALUE[PAWN];
        make_move(pos, m);
        // King-last, for real: a king recapture is legal only if the square is
        // not attacked afterwards (side_to_move is now the opponent).
        if (mover == KING && is_attacked(to, pos.side_to_move, pos)) {
            unmake_move(pos, m);
            continue;
        }
        v -= oracle_exchange(pos, to);
        unmake_move(pos, m);
        best = std::max(best, v);
    }
    return best;
}

static int oracle_see(Position& pos, Move m) {
    const Square to = to_sq(m);
    int v;
    if (move_type(m) == EN_PASSANT)
        v = SEE_VALUE[PAWN];
    else {
        const PieceType pt = piece_type_on(pos, to);
        v = (pt == NO_PIECE_TYPE) ? 0 : SEE_VALUE[pt];
    }
    if (is_promotion(m))
        v += SEE_VALUE[promotion_type(m)] - SEE_VALUE[PAWN];
    make_move(pos, m);
    v -= oracle_exchange(pos, to);
    unmake_move(pos, m);
    return v;
}

// Pin-AWARE variant (LEGAL continuations) — used only to DEMONSTRATE the §8
// divergence in category (f); it is not the gate.
static int pin_aware_exchange(Position& pos, Square to) {
    int best = 0;
    MoveList ml;
    generate_legal_moves(pos, ml);
    for (int i = 0; i < ml.count; ++i) {
        const Move m = ml.moves[i];
        if (to_sq(m) != to || !is_capture_like(pos, m) || move_type(m) == EN_PASSANT)
            continue;
        int v = SEE_VALUE[piece_type_on(pos, to)];
        if (is_promotion(m))
            v += SEE_VALUE[promotion_type(m)] - SEE_VALUE[PAWN];
        make_move(pos, m);
        v -= pin_aware_exchange(pos, to);
        unmake_move(pos, m);
        best = std::max(best, v);
    }
    return best;
}

// Find the unique legal/pseudo-legal move matching a UCI-ish from-to (+promo).
static Move find_move(const Position& pos, Square from, Square to,
                      PieceType promo = NO_PIECE_TYPE) {
    Position p = pos;
    MoveList ml;
    generate_moves(p, ml);
    for (int i = 0; i < ml.count; ++i) {
        const Move m = ml.moves[i];
        if (from_sq(m) != from || to_sq(m) != to) continue;
        if (promo != NO_PIECE_TYPE && (!is_promotion(m) || promotion_type(m) != promo)) continue;
        if (promo == NO_PIECE_TYPE && is_promotion(m)) continue;
        return m;
    }
    return MOVE_NONE;
}

struct HandCase { const char* name; const char* fen; Square from, to; PieceType promo; int expect; };

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    // ---- (a)-(e): hand-built cases, expected values derived by hand and
    // cross-checked against the oracle below.
    const HandCase cases[] = {
        // a. simple
        { "a1 PxP undefended",  "4k3/8/8/4p3/3P4/8/8/4K3 w - - 0 1", SQ_D4, SQ_E5, NO_PIECE_TYPE,  100 },
        { "a2 PxP defended",    "4k3/8/5p2/4p3/3P4/8/8/4K3 w - - 0 1", SQ_D4, SQ_E5, NO_PIECE_TYPE,   0 },
        { "a3 NxP defended",    "4k3/8/5p2/4p3/8/5N2/8/4K3 w - - 0 1", SQ_F3, SQ_E5, NO_PIECE_TYPE, -220 },
        // b. x-ray (doubled rooks both sides on the d-file)
        { "b1 RxP xray stacks", "3rk3/3r4/8/3p4/8/3R4/3R4/3K4 w - - 0 1", SQ_D3, SQ_D5, NO_PIECE_TYPE, -400 },
        // c. king participation
        { "c1 king last, participates", "4k3/6b1/8/4p3/3P1K2/8/8/8 w - - 0 1", SQ_D4, SQ_E5, NO_PIECE_TYPE, 100 },
        { "c2 king declines (hidden Ra5)", "4k3/6b1/8/r3p3/3P1K2/8/8/8 w - - 0 1", SQ_D4, SQ_E5, NO_PIECE_TYPE, 0 },
        // d. promotion captures
        { "d1 cxb8=Q undefended", "1n2k3/2P5/8/8/8/8/8/4K3 w - - 0 1", SQ_C7, SQ_B8, QUEEN, 1120 },
        { "d2 cxb8=Q, Ra8 defends", "rn2k3/2P5/8/8/8/8/8/4K3 w - - 0 1", SQ_C7, SQ_B8, QUEEN, 220 },
        // e. en passant initiators
        { "e1 exd6 ep undefended", "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2", SQ_E5, SQ_D6, NO_PIECE_TYPE, 100 },
        { "e2 exd6 ep, Ke7 recaptures", "8/4k3/8/3pP3/8/8/8/4K3 w - d6 0 2", SQ_E5, SQ_D6, NO_PIECE_TYPE, 0 },
    };

    int handCount = 0;
    for (const HandCase& c : cases) {
        Position p; parse_fen(p, c.fen);
        const Move m = find_move(p, c.from, c.to, c.promo);
        check(m != MOVE_NONE, std::string(c.name) + ": move exists");
        if (m == MOVE_NONE) continue;
        const int s = see(p, m);
        const int o = oracle_see(p, m);
        check(s == c.expect, std::string(c.name) + ": SEE == hand value (" +
              std::to_string(s) + " vs " + std::to_string(c.expect) + ")");
        check(s == o, std::string(c.name) + ": SEE == oracle (" +
              std::to_string(s) + " vs " + std::to_string(o) + ")");
        ++handCount;
    }
    std::cout << "  [a-e] hand cases: " << handCount << " positions, SEE == hand value == oracle\n";

    // ---- (f) pinned-piece divergence, DEMONSTRATED --------------------------------
    // Black Bd6 is the only defender of the e5 pawn but is pinned to Kd8 by Rd1.
    // SEE (pins ignored, §8): Nxe5 loses the knight for a pawn -> -220.
    // A pin-aware playout: the bishop may NOT recapture -> +100.
    {
        Position p; parse_fen(p, "3k4/8/3b4/4p3/8/5N2/8/3RK3 w - - 0 1");
        const Move m = find_move(p, SQ_F3, SQ_E5);
        check(m != MOVE_NONE, "f: move exists");
        const int s = see(p, m);
        const int o = oracle_see(p, m);           // oracle models the same simplification
        make_move(p, m);
        const int aware = 100 - pin_aware_exchange(p, SQ_E5) + 0; // victim P captured
        unmake_move(p, m);
        check(s == -220, "f: SEE treats pinned Bd6 as a live defender (-220), got " + std::to_string(s));
        check(o == -220, "f: oracle models the same simplification (-220), got " + std::to_string(o));
        check(aware == 100, "f: pin-aware playout says +100 (bishop cannot recapture), got " + std::to_string(aware));
        std::cout << "  [f] pin divergence demonstrated: SEE/oracle = " << s
                  << ", pin-aware = " << aware << " (documented §8 imperfection)\n";
    }

    // ---- (g) seeded random walks ---------------------------------------------------
    const char* bases[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    };
    std::uint64_t rng = 0x5EEC0FFEE1234567ULL;     // fixed seed: deterministic gate
    auto next = [&rng]() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };

    int pairs = 0, mismatches = 0;
    for (int walk = 0; pairs < 1500; ++walk) {
        Position p; parse_fen(p, bases[walk % 4]);
        for (int step = 0; step < 40; ++step) {
            MoveList legal;
            generate_legal_moves(p, legal);
            if (legal.count == 0) break;
            for (int i = 0; i < legal.count; ++i) {
                const Move m = legal.moves[i];
                if (!is_capture_like(p, m) && !is_promotion(m)) continue;
                const int s = see(p, m);
                const int o = oracle_see(p, m);
                ++pairs;
                if (s != o) {
                    ++mismatches; ++g_failures;
                    if (mismatches <= 5)
                        std::cout << "  FAIL g: SEE=" << s << " oracle=" << o
                                  << " move " << move_to_uci(m) << " [" << fen_string(p) << "]\n";
                }
            }
            make_move(p, legal.moves[next() % static_cast<std::uint64_t>(legal.count)]);
        }
        if (walk > 4000) break;                     // structural safety, never hit
    }
    std::cout << "  [g] random batch: " << pairs << " (position, capture/promo) pairs, "
              << mismatches << " mismatches\n";
    check(pairs >= 1500, "random batch reached 1500+ pairs");

    if (g_failures == 0) {
        std::cout << "test_see: ALL STEP 6 SEE-ORACLE CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_see: FAILURES = " << g_failures << "\n";
    return 1;
}
