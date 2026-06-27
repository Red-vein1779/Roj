// Roj chess engine — Zobrist key generation.

#include "zobrist.h"
#include "rng.h"

namespace roj {

std::uint64_t ZOBRIST_PIECE[PIECE_NB][SQUARE_NB];
std::uint64_t ZOBRIST_SIDE;
std::uint64_t ZOBRIST_CASTLING[CASTLING_RIGHTS_NB];
std::uint64_t ZOBRIST_EP[FILE_NB];

void init_zobrist() {
    // Fixed seed, DIFFERENT from the magic search's seed, so the two key
    // streams are reproducible yet independent.
    Rng rng(0xD1B54A32D192ED03ULL);

    for (int p = 0; p < PIECE_NB; ++p)
        for (int s = 0; s < SQUARE_NB; ++s)
            ZOBRIST_PIECE[p][s] = rng.next();

    ZOBRIST_SIDE = rng.next();

    // NO_CASTLING (index 0) contributes nothing, so the neutral/empty position
    // hashes to 0; the remaining 15 combinations get distinct random keys.
    ZOBRIST_CASTLING[0] = 0;
    for (int c = 1; c < CASTLING_RIGHTS_NB; ++c)
        ZOBRIST_CASTLING[c] = rng.next();

    for (int f = 0; f < FILE_NB; ++f)
        ZOBRIST_EP[f] = rng.next();
}

} // namespace roj
