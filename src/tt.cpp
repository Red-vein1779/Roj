// Roj chess engine — Phase 2: transposition table (see tt.h).

#include "tt.h"

#include <algorithm>

namespace roj {

namespace {
// Largest power of two <= x (x >= 1).
std::size_t floor_pow2(std::size_t x) {
    std::size_t p = 1;
    while ((p << 1) != 0 && (p << 1) <= x) p <<= 1;
    return p;
}
} // namespace

void TranspositionTable::resize(std::size_t mb) {
    const std::size_t bytes        = mb * 1024u * 1024u;
    const std::size_t clusterBytes = 2u * sizeof(TTEntry);
    std::size_t clusters = bytes / clusterBytes;
    if (clusters < 1) clusters = 1;
    clusterCount_ = floor_pow2(clusters);
    mask_         = clusterCount_ - 1;
    entries_.assign(clusterCount_ * 2u, TTEntry{});
}

void TranspositionTable::clear() {
    std::fill(entries_.begin(), entries_.end(), TTEntry{});
}

bool TranspositionTable::probe(std::uint64_t key, TTEntry& out) const {
    if (clusterCount_ == 0) return false;
    const std::uint32_t k32 = static_cast<std::uint32_t>(key >> 32);
    const std::size_t   base = (static_cast<std::size_t>(key) & mask_) * 2u;
    for (std::size_t i = 0; i < 2; ++i) {
        const TTEntry& e = entries_[base + i];
        if (e.bound != BOUND_NONE && e.key32 == k32) {
            out = e;
            return true;
        }
    }
    return false;
}

void TranspositionTable::store(std::uint64_t key, Move move, std::int16_t score,
                               std::int16_t depth, Bound bound) {
    if (clusterCount_ == 0) return;
    const std::uint32_t k32  = static_cast<std::uint32_t>(key >> 32);
    const std::size_t   base = (static_cast<std::size_t>(key) & mask_) * 2u;

    TTEntry& depthPref = entries_[base];       // bucket 0: depth-preferred
    TTEntry& always    = entries_[base + 1];   // bucket 1: always-replace

    const TTEntry ne{ k32, move, score, depth, static_cast<std::uint8_t>(bound) };

    // Depth-preferred bucket: overwrite it when it is empty, holds the SAME
    // position, or the newcomer was searched at least as deep. Otherwise the
    // newcomer displaces the always-replace bucket, keeping the deeper entry.
    if (depthPref.bound == BOUND_NONE || depthPref.key32 == k32 || depth >= depthPref.depth)
        depthPref = ne;
    else
        always = ne;
}

} // namespace roj
