// Roj chess engine — small deterministic PRNG (xorshift64*).
//
// Shared, reusable generator. Each user seeds it with its OWN fixed constant so
// the streams are deterministic and reproducible but independent (the magic
// search and the Zobrist keys must not share a stream). This is the same
// xorshift64* algorithm used by the magic search in step 4.

#ifndef ROJ_RNG_H
#define ROJ_RNG_H

#include <cstdint>

namespace roj {

struct Rng {
    std::uint64_t state;

    explicit Rng(std::uint64_t seed) : state(seed) {}

    std::uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
};

} // namespace roj

#endif // ROJ_RNG_H
