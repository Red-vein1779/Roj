// Roj chess engine — perft (performance test) and divide.
//
// perft counts the leaf nodes of the legal move tree to a fixed depth — the only
// way to verify move generation. divide breaks the count down per root move so a
// discrepancy can be localised to a single move. This is the TOOL; judging the
// counts against published values is the Phase 1 gate (step 14), not here.

#ifndef ROJ_PERFT_H
#define ROJ_PERFT_H

#include "position.h"

#include <cstdint>

namespace roj {

// Count legal leaf nodes at `depth`. With verify_hash == true, every node
// visited asserts pos.hash == compute_hash_from_scratch(pos) (Definition of Done
// item 2), catching symmetric incremental-hash bugs that round-trip tests miss.
// Pass false to skip the check for speed at high depth. The check uses assert(),
// so the build must NOT define NDEBUG for it to be active.
std::uint64_t perft(Position& pos, int depth, bool verify_hash = true);

// Print "<uci move>: <perft(depth-1)>" for each legal root move, then the total.
// Move format is long algebraic / UCI (e2e4, e7e8q), for move-by-move comparison
// against a reference divide. Intended for depth >= 1.
void perft_divide(Position& pos, int depth, bool verify_hash = true);

} // namespace roj

#endif // ROJ_PERFT_H
