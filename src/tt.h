// Roj chess engine — Phase 2: transposition table (phase2.md section 3 decision 5).
//
// Keyed by the Phase 1 incremental Zobrist hash. Two entries (buckets) per index:
// bucket 0 is depth-preferred, bucket 1 is always-replace. Each entry stores a
// check portion of the key, the best move, remaining depth, a bound type, and a
// score that the SEARCH has already mate-adjusted with value_to_tt / value_from_tt
// (section 4) — the table itself is dumb storage. Platform-independent C++17 only.

#ifndef ROJ_TT_H
#define ROJ_TT_H

#include "types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace roj {

// Bound type of a stored score relative to the alpha/beta window it was found in.
enum Bound : std::uint8_t {
    BOUND_NONE  = 0,   // empty slot
    BOUND_UPPER = 1,   // fail-low: the true value is <= score
    BOUND_LOWER = 2,   // fail-high: the true value is >= score
    BOUND_EXACT = 3    // exact value
};

struct TTEntry {
    std::uint32_t key32 = 0;          // upper 32 bits of the Zobrist key (collision check)
    Move          move  = MOVE_NONE;  // best move found at this node
    std::int16_t  score = 0;          // value_to_tt-adjusted score
    std::int16_t  depth = 0;          // remaining search depth this entry was searched to
    std::uint8_t  bound = BOUND_NONE;
};

class TranspositionTable {
public:
    // (Re)allocate the table to hold about `mb` megabytes, rounded DOWN to a
    // power-of-two number of clusters, and clear it. `Hash` UCI option calls this.
    void resize(std::size_t mb);

    // Zero every entry. Called between independent searches (ucinewgame) so a
    // fixed-depth search is reproducible.
    void clear();

    // Probe by key: if a matching, non-empty entry is found in the cluster, copy
    // it into `out` and return true.
    bool probe(std::uint64_t key, TTEntry& out) const;

    // Store using depth-preferred + always-replace over the two buckets.
    void store(std::uint64_t key, Move move, std::int16_t score, std::int16_t depth, Bound bound);

    std::size_t clusters() const { return clusterCount_; }

private:
    std::vector<TTEntry> entries_;      // clusterCount_ * 2 entries
    std::size_t          clusterCount_ = 0;   // power of two (0 = unallocated)
    std::size_t          mask_ = 0;           // clusterCount_ - 1
};

} // namespace roj

#endif // ROJ_TT_H
