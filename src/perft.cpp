// Roj chess engine — perft and divide.

#include "perft.h"
#include "movegen.h"

#include <cassert>
#include <iostream>
#include <string>

namespace roj {

// move_to_uci now lives in movegen.cpp (shared with the UCI loop).

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
