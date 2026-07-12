// Roj chess engine — Phase 3, Step 4: null-move make/unmake invariants.
//
// Maps to phase3.md Step 4 "done when" / §8 "NMP och hash":
//  1. HASH INVARIANT: after make_null_move, the incremental hash equals a
//     from-scratch hash of the resulting position (Phase 1's oracle reused),
//     over positions WITH and WITHOUT an en-passant square set — the EP-file
//     key must be XORed out exactly per Phase 1's always-if-exists convention.
//  2. ROUND-TRIP: unmake_null_move restores side to move, castling rights,
//     en-passant square, both clocks, the hash and every bitboard bit for bit.
//  3. NESTING: null moves interleaved with real moves (make m, null, unmake
//     null, unmake m) keep the history stack balanced and the state exact.
//
// Build (one line):
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_nullmove.cpp src/position.cpp src/fen.cpp src/movegen.cpp src/attacks.cpp src/magic.cpp src/zobrist.cpp src/bitboard.cpp -o test_nullmove

#include "../src/position.h"
#include "../src/fen.h"
#include "../src/movegen.h"
#include "../src/attacks.h"
#include "../src/magic.h"
#include "../src/zobrist.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

using namespace roj;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { ++g_failures; std::cout << "  FAIL: " << what << "\n"; }
}

// Bitwise-comparable snapshot of everything a null move may NOT corrupt.
struct Snapshot {
    Bitboard pieces[COLOR_NB][PIECE_TYPE_NB];
    Bitboard byColor[COLOR_NB];
    Bitboard occupied;
    Color side; CastlingRights cr; Square ep; int hm, fm;
    std::uint64_t hash;
    std::size_t hist;
};
static Snapshot snap(const Position& p) {
    Snapshot s{};
    std::memcpy(s.pieces, p.pieces, sizeof(s.pieces));
    std::memcpy(s.byColor, p.byColor, sizeof(s.byColor));
    s.occupied = p.occupied;
    s.side = p.side_to_move; s.cr = p.castling_rights; s.ep = p.ep_square;
    s.hm = p.halfmove_clock; s.fm = p.fullmove_number; s.hash = p.hash;
    s.hist = p.history.size();
    return s;
}
static bool same(const Snapshot& a, const Snapshot& b) {
    return std::memcmp(a.pieces, b.pieces, sizeof(a.pieces)) == 0
        && std::memcmp(a.byColor, b.byColor, sizeof(a.byColor)) == 0
        && a.occupied == b.occupied && a.side == b.side && a.cr == b.cr
        && a.ep == b.ep && a.hm == b.hm && a.fm == b.fm && a.hash == b.hash
        && a.hist == b.hist;
}

int main() {
    init_attack_tables();
    init_magics();
    init_zobrist();

    // WITH and WITHOUT an EP square; castling rights present and absent.
    const char* FENS[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",   // EP set
        "rnbqkbnr/pppp1ppp/8/8/3Pp3/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 2",   // EP set, black to move
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1",                        // zugzwang (Fine 70)
    };

    for (const char* fen : FENS) {
        Position p; parse_fen(p, fen);
        const Snapshot before = snap(p);

        // 1. Hash invariant after make_null_move.
        make_null_move(p);
        check(p.hash == compute_hash_from_scratch(p),
              std::string("null-make hash == from-scratch [") + fen + "]");
        check(p.side_to_move == ~before.side, "side toggled");
        check(p.ep_square == SQ_NONE, "EP cleared");
        check(p.castling_rights == before.cr, "castling untouched");
        check(p.halfmove_clock == before.hm && p.fullmove_number == before.fm,
              "clocks untouched");

        // 2. Round-trip restores everything bit for bit.
        unmake_null_move(p);
        check(same(snap(p), before), std::string("null round-trip bit-for-bit [") + fen + "]");

        // 3. Nesting with a real move: m, null, unnull, unmake m.
        MoveList ml; generate_legal_moves(p, ml);
        if (ml.count > 0) {
            make_move(p, ml.moves[0]);
            const Snapshot mid = snap(p);
            make_null_move(p);
            check(p.hash == compute_hash_from_scratch(p),
                  std::string("nested null hash == from-scratch [") + fen + "]");
            unmake_null_move(p);
            check(same(snap(p), mid), "nested null round-trip");
            unmake_move(p, ml.moves[0]);
            check(same(snap(p), before), "outer real-move round-trip after nesting");
        }
    }

    if (g_failures == 0) {
        std::cout << "test_nullmove: ALL STEP 4 NULL-MAKE/UNMAKE CHECKS PASS\n";
        return 0;
    }
    std::cout << "test_nullmove: FAILURES = " << g_failures << "\n";
    return 1;
}
