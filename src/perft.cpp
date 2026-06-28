// Roj chess engine — perft and divide.

#include "perft.h"
#include "movegen.h"

#include <cassert>
#include <iostream>
#include <string>

namespace roj {

namespace {

// Long-algebraic / UCI move string: from-square, to-square, and a lowercase
// promotion letter for promotions (e.g. "e2e4", "e7e8q").
std::string move_to_uci(Move m) {
    const Square from = from_sq(m);
    const Square to   = to_sq(m);

    std::string s;
    s += static_cast<char>('a' + file_of(from));
    s += static_cast<char>('1' + rank_of(from));
    s += static_cast<char>('a' + file_of(to));
    s += static_cast<char>('1' + rank_of(to));

    if (is_promotion(m)) {
        static const char letters[PIECE_TYPE_NB] = {'?', 'p', 'n', 'b', 'r', 'q', 'k'};
        s += letters[promotion_type(m)];
    }
    return s;
}

} // namespace

std::uint64_t perft(Position& pos, int depth, bool verify_hash) {
    // DoD item 2: the incremental hash must equal the from-scratch oracle at
    // EVERY node visited. Full recursion (no leaf shortcut) keeps this running
    // even at the last ply.
    if (verify_hash)
        assert(pos.hash == compute_hash_from_scratch(pos));

    if (depth == 0)
        return 1;

    MoveList list;
    generate_legal_moves(pos, list);

    const std::uint64_t entry_hash = pos.hash;
    std::uint64_t nodes = 0;

    for (int i = 0; i < list.count; ++i) {
        make_move(pos, list.moves[i]);
        nodes += perft(pos, depth - 1, verify_hash);
        unmake_move(pos, list.moves[i]);
    }

    // Cheap loop-integrity proxy: the position must be unchanged after the loop
    // (the hash is a lightweight stand-in for a full board comparison).
    if (verify_hash)
        assert(pos.hash == entry_hash);

    return nodes;
}

void perft_divide(Position& pos, int depth, bool verify_hash) {
    MoveList list;
    generate_legal_moves(pos, list);

    std::uint64_t total = 0;
    for (int i = 0; i < list.count; ++i) {
        make_move(pos, list.moves[i]);
        const std::uint64_t n = perft(pos, depth - 1, verify_hash);
        unmake_move(pos, list.moves[i]);

        std::cout << move_to_uci(list.moves[i]) << ": " << n << "\n";
        total += n;
    }
    std::cout << "total: " << total << "\n";
}

} // namespace roj
