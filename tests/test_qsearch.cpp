// Roj chess engine — Phase 2, Step 4: quiescence-search tests.
//
// Maps to phase2.md Step 4 "done when":
//  1. HORIZON: fixed-depth search WITHOUT qsearch grabs a hanging piece at the
//     horizon; WITH qsearch it does not (score corrected).
//  2. CHECK IN QSEARCH: in check -> ALL evasions searched (not only captures);
//     checkmate in qsearch -> mate score; quiet with no noisy moves -> stand_pat.
//  3. DELTA PRUNING: active out of check (insufficient capture skipped), disabled
//     in check, and never changes a search score (no lost tactic).
//  4. TERMINATION: qsearch completes on capture-rich positions (bounded nodes).
//  5. REGRESSION: mate-in-1/2 and stalemate still correct with qsearch ON.
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_qsearch.cpp src/search.cpp src/eval.cpp src/movegen.cpp src/position.cpp src/fen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp -o test_qsearch

#include "../src/search.h"
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

using namespace roj;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

static SearchInfo qs_on() {
    SearchInfo s; s.use_qsearch = true; s.use_mvv_lva = true; s.use_delta_pruning = true; return s;
}

// ---- 1. Horizon / tactical stability -----------------------------------------
static void test_horizon() {
    // Each position: the side to move can grab a pawn that is defended, so the
    // capturing piece is recaptured one ply later. At a fixed depth without
    // qsearch the recapture is over the horizon, so the search "wins" the pawn and
    // plays the losing capture. With qsearch the recapture is seen.
    struct H { const char* name; const char* fen; const char* hanging; };
    const H cases[] = {
        {"BxP (defended)", "6k1/8/4p3/3p4/2B5/8/8/4K3 w - - 0 1", "c4d5"},  // Bxd5? exd5
        {"RxP (defended)", "6k1/8/4p3/3p4/3R4/8/8/4K3 w - - 0 1", "d4d5"}   // Rxd5? exd5
    };
    for (const H& h : cases) {
        Position po; parse_fen(po, h.fen);
        Position pn; parse_fen(pn, h.fen);
        SearchInfo off; off.use_qsearch = false; off.use_mvv_lva = true;
        SearchInfo on  = qs_on();
        const SearchResult ro = search_root(po, 1, off);
        const SearchResult rn = search_root(pn, 1, on);
        std::cout << "  [" << h.name << "] OFF best=" << move_to_uci(ro.best) << " score=" << ro.score
                  << "  |  ON best=" << move_to_uci(rn.best) << " score=" << rn.score << "\n";
        check(move_to_uci(ro.best) == h.hanging,
              std::string("no-qsearch grabs the hanging piece [") + h.name + "]");
        check(move_to_uci(rn.best) != h.hanging,
              std::string("qsearch avoids the refuted capture [") + h.name + "]");
        check(rn.score < ro.score,
              std::string("qsearch corrects the over-optimistic score [") + h.name + "]");
    }
}

// ---- 2. Check inside qsearch -------------------------------------------------
static void test_qsearch_check() {
    SearchInfo info = qs_on();

    // (a) In check with ONLY quiet evasions (Black Ra1 checks Ke1; king steps
    //     Kd2/Ke2/Kf2, no captures). qsearch must generate them (not just
    //     captures) and return a normal, finite, negative score (down a rook).
    {
        Position p; parse_fen(p, "6k1/8/8/8/8/8/8/r3K3 w - - 0 1");
        const int v = qsearch(p, -VALUE_INFINITE, VALUE_INFINITE, 0, info);
        check(v > VALUE_MATED_IN_MAX_PLY && v < VALUE_MATE_IN_MAX_PLY,
              "in-check qsearch (only quiet evasions) returns a normal score (got " + std::to_string(v) + ")");
        check(v < 0, "in-check qsearch score is negative (down a rook)");
    }
    // (b) Checkmate inside qsearch -> mate score. Black is back-rank mated; at
    //     ply 0 qsearch returns -VALUE_MATE.
    {
        Position p; parse_fen(p, "R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1");
        const int v = qsearch(p, -VALUE_INFINITE, VALUE_INFINITE, 0, info);
        check(v == -VALUE_MATE, "checkmate in qsearch returns -VALUE_MATE at ply 0 (got " + std::to_string(v) + ")");
    }
    // (c) Quiet, not in check, no noisy moves -> return stand_pat (== eval).
    {
        Position p; parse_fen(p, "7k/8/8/8/8/8/8/7K w - - 0 1");
        const int v = qsearch(p, -VALUE_INFINITE, VALUE_INFINITE, 0, info);
        check(v == evaluate(p), "quiet node with no noisy moves returns stand_pat (got " + std::to_string(v) + ")");
    }
}

// ---- 3. Delta pruning --------------------------------------------------------
static void test_delta() {
    // (a) ACTIVE when not in check: with a high alpha window the small capture
    //     exd4 (PxP) cannot reach alpha, so delta ON skips it -> fewer nodes.
    {
        const char* fen = "4k3/8/8/8/3p4/4P3/8/4K3 w - - 0 1";
        Position pa; parse_fen(pa, fen);
        Position pb; parse_fen(pb, fen);
        const int sp = evaluate(pa);
        SearchInfo don  = qs_on();
        SearchInfo doff = qs_on(); doff.use_delta_pruning = false;
        const int vd_on  = qsearch(pa, sp + 500, sp + 1000, 0, don);
        const int vd_off = qsearch(pb, sp + 500, sp + 1000, 0, doff);
        std::cout << "  delta ACTIVE: nodes ON=" << don.nodes << " OFF=" << doff.nodes
                  << "  (val ON=" << vd_on << " OFF=" << vd_off << ")\n";
        check(don.nodes < doff.nodes, "delta pruning active: insufficient capture skipped (fewer nodes)");
    }
    // (b) DISABLED in check: identical nodes AND value with delta ON vs OFF.
    {
        const char* fen = "6k1/8/8/8/8/8/8/r3K3 w - - 0 1";
        Position pa; parse_fen(pa, fen);
        Position pb; parse_fen(pb, fen);
        SearchInfo don  = qs_on();
        SearchInfo doff = qs_on(); doff.use_delta_pruning = false;
        const int vd_on  = qsearch(pa, -VALUE_INFINITE, VALUE_INFINITE, 0, don);
        const int vd_off = qsearch(pb, -VALUE_INFINITE, VALUE_INFINITE, 0, doff);
        check(vd_on == vd_off && don.nodes == doff.nodes,
              "delta pruning disabled in check (identical nodes and value)");
    }
    // (c) SAFE: on the horizon positions the full search score is identical with
    //     delta ON vs OFF (delta must not drop a real tactic). Report both.
    {
        const char* fens[] = { "6k1/8/4p3/3p4/2B5/8/8/4K3 w - - 0 1",
                               "6k1/8/4p3/3p4/3R4/8/8/4K3 w - - 0 1" };
        for (const char* fen : fens) {
            Position pa; parse_fen(pa, fen);
            Position pb; parse_fen(pb, fen);
            SearchInfo don  = qs_on();
            SearchInfo doff = qs_on(); doff.use_delta_pruning = false;
            const int s_on  = search_root(pa, 3, don).score;
            const int s_off = search_root(pb, 3, doff).score;
            std::cout << "  delta SAFE [" << fen << "] d3: ON=" << s_on << " OFF=" << s_off << "\n";
            check(s_on == s_off, std::string("delta pruning does not change the score [") + fen + "]");
        }
    }
}

// ---- 4. Termination ----------------------------------------------------------
static void test_termination() {
    // Capture-rich positions: qsearch must terminate (captures strictly reduce
    // material). If it did not, this test would hang.
    const char* fens[] = {
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",  // Kiwipete
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"              // P5
    };
    for (const char* fen : fens) {
        Position p; parse_fen(p, fen);
        SearchInfo info = qs_on();
        const int v = qsearch(p, -VALUE_INFINITE, VALUE_INFINITE, 0, info);
        std::cout << "  termination [" << fen << "]: nodes=" << info.nodes << " value=" << v << "\n";
        check(info.nodes > 0 && info.nodes < 100000000ULL, "qsearch terminated with a bounded node count");
    }
}

// ---- 5. Mate/terminal regression with qsearch ON -----------------------------
static void test_mate_regression() {
    {
        Position p; parse_fen(p, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
        SearchInfo on = qs_on();
        const SearchResult r = search_root(p, 2, on);
        check(r.score == VALUE_MATE - 1, "qsearch ON: mate-in-1 score 31999 (got " + std::to_string(r.score) + ")");
        check(move_to_uci(r.best) == "a1a8", "qsearch ON: mate-in-1 move a1a8 (got " + move_to_uci(r.best) + ")");
    }
    {
        Position p; parse_fen(p, "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1");
        SearchInfo on = qs_on();
        const SearchResult r = search_root(p, 4, on);
        check(r.score == VALUE_MATE - 3, "qsearch ON: mate-in-2 score 31997 (got " + std::to_string(r.score) + ")");
    }
    {
        Position p; parse_fen(p, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        SearchInfo on = qs_on();
        const SearchResult r = search_root(p, 3, on);
        check(r.score == VALUE_DRAW, "qsearch ON: stalemate score 0 (got " + std::to_string(r.score) + ")");
    }
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    test_horizon();
    test_qsearch_check();
    test_delta();
    test_termination();
    test_mate_regression();

    if (g_failures == 0) {
        std::cout << "test_qsearch: ALL STEP 4 CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_qsearch: FAILURES = " << g_failures << "\n";
    return 1;
}
