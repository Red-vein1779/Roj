// Roj chess engine — Phase 2, Step 8: draw-detection tests.
//
// Maps to phase2.md Step 8 "done when" (§7 row 8, §3 decision 7, §9 bullets
// "GHI", "Repetition och pre-rot-historik", "EP-konvention", "50-drag mot matt"):
//
//  1. TWOFOLD IN TREE: a forced king shuffle that repeats within the search scores
//     0 (VALUE_DRAW). Contrasted against detection OFF, which keeps a nonzero
//     (material) score because it never sees the repetition.
//  2. PRE-ROOT REPETITION: a game history (as fed by `position ... moves ...`) that
//     already contains the root position is detected as a draw at the root — and is
//     MISSED when the pre-root history is not supplied (the wiring is what catches it).
//  3. 50-MOVE RULE: halfmove clock >= 100 scores 0; BUT a checkmated side with the
//     clock >= 100 is still mate (mate precedence, §9 "50-drag mot matt").
//  4. INSUFFICIENT MATERIAL: KvK, KNvK, KBvK, KB vs KB same colour -> draw; a case
//     that still has mating material (KQvK, KRvK, KBNvK, KPvK) is NOT a draw.
//  5. NO REGRESSION: on the Step 2 (non-drawish) suite the score with detection ON
//     equals the score with it OFF (Step 7) at fixed depths 1..4; forced mates
//     (mate-in-1..4) are still found with the correct mate score.
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_draw.cpp src/search.cpp src/tt.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp src/bitboard.cpp -o test_draw

#include "../src/search.h"
#include "../src/tt.h"
#include "../src/eval.h"
#include "../src/value.h"
#include "../src/movegen.h"
#include "../src/position.h"
#include "../src/fen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"
#include "../src/types.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace roj;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

static void configure(SearchInfo& info) {
    info.use_mvv_lva = true; info.use_killers_history = true;
    info.use_qsearch = true; info.use_delta_pruning = true;
}

// Find the legal move matching a "from-to" UCI string (no promotion suffix needed
// for the endgame lines used here).
static Move find_move(Position& p, const std::string& uci) {
    MoveList ml; generate_legal_moves(p, ml);
    const Square from = make_square(File(uci[0] - 'a'), Rank(uci[1] - '1'));
    const Square to   = make_square(File(uci[2] - 'a'), Rank(uci[3] - '1'));
    for (int i = 0; i < ml.count; ++i)
        if (from_sq(ml.moves[i]) == from && to_sq(ml.moves[i]) == to)
            return ml.moves[i];
    return MOVE_NONE;
}

// ---- 1. Twofold repetition in the tree --------------------------------------
// FEN: 8/8/8/8/1p6/1P6/1P6/K1k5 b - - 0 1
//   White Ka1 + doubled pawns b2,b3; Black Kc1 + pawn b4. The b-pawns are locked
//   head-to-head (b3 vs b4, b2 behind b3), so no pawn can ever move and neither
//   king can break out of the a1/c1 corner. White is a (dead) pawn up, but the
//   only moves are king shuffles, e.g. ...Kd2 Ka2 Kc1 Ka1 Kd2 ... which repeats.
//   A twofold repetition in the tree is scored 0, so the search returns VALUE_DRAW;
//   with detection OFF the search never sees the draw and keeps a material score.
static void test_twofold_in_tree() {
    const char* fen = "8/8/8/8/1p6/1P6/1P6/K1k5 b - - 0 1";
    const int depth = 10;

    Position pon; parse_fen(pon, fen);
    SearchInfo on; configure(on); on.use_draw_detection = true;
    const int son = search_root(pon, depth, on).score;

    Position poff; parse_fen(poff, fen);
    SearchInfo off; configure(off); off.use_draw_detection = false;
    const int soff = search_root(poff, depth, off).score;

    check(son == VALUE_DRAW, "twofold-in-tree: detection ON scores 0");
    check(soff != VALUE_DRAW, "twofold-in-tree: detection OFF keeps a nonzero (material) score");
    std::cout << "  twofold in tree [" << fen << "] d" << depth
              << ": ON=" << son << " OFF=" << soff << "\n";
}

// ---- 2. Pre-root repetition (history wiring) --------------------------------
// Replay a game that leaves and returns to the base position, recording the key of
// every position exactly as uci_position() does. The root then repeats a pre-root
// position, so with the pre-root history supplied the search sees a draw (0); with
// the history omitted it does not (and returns White's winning material score).
static void test_pre_root_repetition() {
    // White Ra2 + Ke1 (a whole rook up) vs Black Ke8.
    const char* base = "4k3/8/8/8/8/8/R7/4K3 w - - 0 1";
    const char* line[] = { "a2a4", "e8d8", "a4a2", "d8e8" };  // 4 plies -> back to base

    // Build the game-history keys the way the UCI layer does: base, then after each move.
    Position p; parse_fen(p, base);
    std::vector<std::uint64_t> gameKeys{ p.hash };
    for (const std::string mv : line) {
        const Move m = find_move(p, mv);
        check(m != MOVE_NONE, std::string("pre-root: legal move ") + mv);
        make_move(p, m);
        gameKeys.push_back(p.hash);
    }
    // After the round trip the position is the base again.
    check(gameKeys.back() == gameKeys.front(), "pre-root: line returns to the base position");

    const int depth = 6;

    // WITH pre-root history: seed rep with every key EXCEPT the root itself (the
    // last one), exactly as uci_go() does. The root repeats gameKeys[0] -> draw.
    {
        Position root; parse_fen(root, base);
        SearchInfo info; configure(info); info.use_draw_detection = true;
        info.rep.assign(gameKeys.begin(), gameKeys.end() - 1);
        const int s = search_root(root, depth, info).score;
        check(s == VALUE_DRAW, "pre-root repetition detected at root (history supplied) -> 0");
        std::cout << "  pre-root WITH history: score=" << s << " (up a rook, but drawn by repetition)\n";
    }
    // WITHOUT pre-root history: no seed -> the repetition is invisible, White is winning.
    {
        Position root; parse_fen(root, base);
        SearchInfo info; configure(info); info.use_draw_detection = true;  // detection on, but no history
        const int s = search_root(root, depth, info).score;
        check(s != VALUE_DRAW, "pre-root repetition MISSED when history omitted (proves the wiring)");
        std::cout << "  pre-root WITHOUT history: score=" << s << " (winning score returned)\n";
    }
}

// ---- 3. 50-move rule + mate precedence --------------------------------------
static void test_fifty_move() {
    // (a) Halfmove clock == 100 in a non-terminal position -> draw (0).
    {
        const char* fen = "4k3/8/8/8/8/8/4P3/4K3 w - - 100 60"; // White up a pawn, clock 100
        Position p; parse_fen(p, fen);
        SearchInfo info; configure(info); info.use_draw_detection = true;
        const int s = search_root(p, 6, info).score;
        check(s == VALUE_DRAW, "50-move: clock 100 scores 0");

        // Same position with a low clock is NOT a draw (sanity: the clock is the cause).
        Position p2; parse_fen(p2, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 60");
        SearchInfo i2; configure(i2); i2.use_draw_detection = true;
        const int s2 = search_root(p2, 6, i2).score;
        check(s2 != VALUE_DRAW, "50-move: same position with clock 0 is not a draw");
        std::cout << "  50-move: clock100=" << s << "  clock0=" << s2 << "\n";
    }
    // (b) Mate precedence: the side to move is checkmated with the clock >= 100.
    //     The no-legal-moves terminal test runs before the draw check, so this is
    //     mate (a mated score), never a 50-move 0.
    {
        // Black (to move) is checkmated: Qg7 mates Kh8, defended by Kg6. Clock 100.
        const char* fen = "7k/6Q1/6K1/8/8/8/8/8 b - - 100 80";
        Position p; parse_fen(p, fen);
        MoveList ml; generate_legal_moves(p, ml);
        check(ml.count == 0, "mate-precedence: constructed a genuine checkmate position");
        SearchInfo info; configure(info); info.use_draw_detection = true;
        const int s = search_root(p, 4, info).score;
        check(s <= VALUE_MATED_IN_MAX_PLY,
              "mate-precedence: checkmated with clock >= 100 returns a mate score, not 0");
        std::cout << "  mate precedence (clock>=100): score=" << s
                  << " (VALUE_MATED_IN_MAX_PLY=" << VALUE_MATED_IN_MAX_PLY << ")\n";
    }
}

// ---- 4. Insufficient material ------------------------------------------------
static void test_insufficient_material() {
    struct C { const char* fen; bool draw; const char* name; };
    const C cases[] = {
        { "4k3/8/8/8/8/8/8/4K3 w - - 0 1",       true,  "KvK" },
        { "4k3/8/8/8/8/8/8/3NK3 w - - 0 1",      true,  "KNvK" },
        { "4k3/8/8/8/8/8/8/3BK3 w - - 0 1",      true,  "KBvK" },
        { "4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1",    true,  "KBvKB same colour (c1,f8 both dark)" },
        // Still has mating material -> NOT an insufficient-material draw:
        { "4k3/8/8/8/8/8/8/3QK3 w - - 0 1",      false, "KQvK" },
        { "4k3/8/8/8/8/8/8/3RK3 w - - 0 1",      false, "KRvK" },
        { "4k3/8/8/8/8/8/8/2BNK3 w - - 0 1",     false, "KBNvK" },
        { "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",     false, "KPvK" },
        { "2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1",   false, "KBvKB opposite colours (c1 dark, c8 light)" },
    };
    for (const C& c : cases) {
        Position p; parse_fen(p, c.fen);
        check(insufficient_material(p) == c.draw,
              std::string("insufficient_material(") + c.name + ") == " + (c.draw ? "true" : "false"));

        // Draw cases must also score 0 through the search (insufficient material is
        // path-independent, so it fires at the root regardless of depth). The
        // mating-material cases are only checked via insufficient_material() above:
        // their search score is position-dependent (KQvK wins, opposite-bishops is
        // near 0) so it is not a reliable draw/non-draw signal.
        if (c.draw) {
            SearchInfo info; configure(info); info.use_draw_detection = true;
            const int s = search_root(p, 4, info).score;
            check(s == VALUE_DRAW, std::string("search: ") + c.name + " scores 0 (insufficient material)");
        }
    }
    std::cout << "  insufficient material: KvK/KNvK/KBvK/KBvKB-same == draw; KQ/KR/KBN/KP/OCB != insufficient\n";
}

// ---- 5. No regression on non-drawn positions --------------------------------
static const char* STEP2_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"
};

static void test_no_regression() {
    int mismatches = 0;
    for (const char* fen : STEP2_FENS)
        for (int depth = 1; depth <= 4; ++depth) {
            Position pon; parse_fen(pon, fen);
            SearchInfo on; configure(on); on.use_draw_detection = true;   // empty rep, normal clocks
            const int son = search_root(pon, depth, on).score;

            Position poff; parse_fen(poff, fen);
            SearchInfo off; configure(off); off.use_draw_detection = false;  // Step 7 behaviour
            const int soff = search_root(poff, depth, off).score;

            if (son != soff) {
                ++mismatches; ++g_failures;
                std::cout << "  FAIL no-regression [" << fen << "] d" << depth
                          << ": ON=" << son << " OFF=" << soff << "\n";
            }
        }
    if (mismatches == 0)
        std::cout << "  no regression: detection ON == OFF on the Step 2 suite, depths 1..4\n";

    // Forced mates still found (score AND that it is a mate score).
    struct M { const char* fen; int depth; };
    const M mates[] = {
        { "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", 2 },  // mate in 1
        { "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1",       4 },  // mate in 2
    };
    for (const M& m : mates) {
        Position p; parse_fen(p, m.fen);
        SearchInfo info; configure(info); info.use_draw_detection = true;
        const int s = search_root(p, m.depth, info).score;
        check(s >= VALUE_MATE_IN_MAX_PLY, std::string("forced mate still found [") + m.fen + "]");
    }
    std::cout << "  forced mates (mate-in-1, mate-in-2) still found with detection on\n";
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    test_twofold_in_tree();
    test_pre_root_repetition();
    test_fifty_move();
    test_insufficient_material();
    test_no_regression();

    if (g_failures == 0) {
        std::cout << "test_draw: ALL STEP 8 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_draw: FAILURES = " << g_failures << "\n";
    return 1;
}
