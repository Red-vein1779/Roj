// Roj chess engine — magic bitboard generation (own search, no published numbers).

#include "magic.h"
#include "bitboard.h"

#include <cstdint>

namespace roj {

Bitboard ROOK_MASK[SQUARE_NB];
Bitboard BISHOP_MASK[SQUARE_NB];
Bitboard ROOK_MAGICS[SQUARE_NB];
Bitboard BISHOP_MAGICS[SQUARE_NB];
int      ROOK_SHIFTS[SQUARE_NB];
int      BISHOP_SHIFTS[SQUARE_NB];
Bitboard ROOK_ATTACKS[SQUARE_NB][1 << ROOK_INDEX_BITS];
Bitboard BISHOP_ATTACKS[SQUARE_NB][1 << BISHOP_INDEX_BITS];

namespace {

constexpr Bitboard RANK1 = 0x00000000000000FFULL;
constexpr Bitboard RANK8 = 0xFF00000000000000ULL;
constexpr Bitboard FILEA = 0x0101010101010101ULL;
constexpr Bitboard FILEH = 0x8080808080808080ULL;

const int ROOK_DIRS[4][2]   = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
const int BISHOP_DIRS[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

// (A) Generic ray walk in file/rank space. Working in (file, rank) and checking
// both bounds is what makes off-board wraparound impossible.
Bitboard ray_attacks(Square s, Bitboard occ, const int dirs[][2]) {
    Bitboard attacks = EMPTY_BB;
    const int f0 = file_of(s), r0 = rank_of(s);
    for (int d = 0; d < 4; ++d) {
        int f = f0 + dirs[d][0];
        int r = r0 + dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            const Square sq = make_square(static_cast<File>(f), static_cast<Rank>(r));
            set_bit(attacks, sq);
            if (test_bit(occ, sq)) break;   // include blocker, then stop
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return attacks;
}

// (B) Relevant-occupancy masks. Edge squares are stripped: a blocker on the far
// edge never changes the attack set (the ray reaches that square either way),
// so excluding edges keeps the index as small as possible.
Bitboard rook_mask(Square s) {
    const Bitboard att      = rook_attacks_slow(s, EMPTY_BB);
    const Bitboard own_rank = RANK1 << (8 * rank_of(s));
    const Bitboard own_file = FILEA << file_of(s);
    const Bitboard edges    = ((RANK1 | RANK8) & ~own_rank) | ((FILEA | FILEH) & ~own_file);
    return att & ~edges;
}
Bitboard bishop_mask(Square s) {
    return bishop_attacks_slow(s, EMPTY_BB) & ~(RANK1 | RANK8 | FILEA | FILEH);
}

// Map an integer 0..2^bits-1 to a distinct occupancy subset of `mask` by
// scattering the integer's bits across the mask's set squares.
Bitboard index_to_occupancy(int index, Bitboard mask) {
    Bitboard occ = EMPTY_BB;
    int i = 0;
    while (mask) {
        const Square sq = pop_lsb(mask);
        if (index & (1 << i)) set_bit(occ, sq);
        ++i;
    }
    return occ;
}

// Tiny xorshift64* PRNG — our own generator, fixed seed for reproducibility.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    // ANDing three draws clears most bits; sparse candidates make better magics.
    std::uint64_t sparse() { return next() & next() & next(); }
};

// Search scratch, reused per square (init is single-threaded and sequential).
Bitboard g_occ[1 << ROOK_INDEX_BITS];
Bitboard g_ref[1 << ROOK_INDEX_BITS];
Bitboard g_seen[1 << ROOK_INDEX_BITS];
int      g_epoch[1 << ROOK_INDEX_BITS];
int      g_cur_epoch = 0;

// (C + D) Find a magic for one square and fill its attack-table row.
Bitboard find_magic(Square s, Bitboard mask, bool rook,
                    Bitboard* table_row, int& out_shift, Rng& rng) {
    const int bits  = popcount(mask);
    const int shift = 64 - bits;
    const int n     = 1 << bits;

    // Precompute every occupancy subset and its correct attacks once (oracle).
    for (int i = 0; i < n; ++i) {
        g_occ[i] = index_to_occupancy(i, mask);
        g_ref[i] = rook ? rook_attacks_slow(s, g_occ[i])
                        : bishop_attacks_slow(s, g_occ[i]);
    }

    for (;;) {
        const Bitboard magic = rng.sparse();
        // Quick reject: a usable magic spreads bits into the high byte.
        if (popcount((mask * magic) >> 56) < 6) continue;

        ++g_cur_epoch;
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            const unsigned idx = static_cast<unsigned>((g_occ[i] * magic) >> shift);
            if (g_epoch[idx] != g_cur_epoch) {           // slot free this attempt
                g_epoch[idx] = g_cur_epoch;
                g_seen[idx]  = g_ref[i];
            } else if (g_seen[idx] != g_ref[i]) {        // destructive collision
                ok = false;
                break;
            }
            // else: constructive collision (same attacks) — allowed.
        }
        if (ok) {
            for (int i = 0; i < n; ++i) {
                const unsigned idx = static_cast<unsigned>((g_occ[i] * magic) >> shift);
                table_row[idx] = g_ref[i];
            }
            out_shift = shift;
            return magic;
        }
    }
}

} // namespace

// (A) Public oracle.
Bitboard rook_attacks_slow(Square s, Bitboard occ)   { return ray_attacks(s, occ, ROOK_DIRS); }
Bitboard bishop_attacks_slow(Square s, Bitboard occ) { return ray_attacks(s, occ, BISHOP_DIRS); }

void init_magics() {
    Rng rng(0x0123456789ABCDEFULL);   // fixed seed -> reproducible magics
    for (int s = 0; s < SQUARE_NB; ++s) {
        const Square sq = static_cast<Square>(s);
        ROOK_MASK[s]   = rook_mask(sq);
        BISHOP_MASK[s] = bishop_mask(sq);
        ROOK_MAGICS[s]   = find_magic(sq, ROOK_MASK[s],   true,
                                      ROOK_ATTACKS[s],   ROOK_SHIFTS[s],   rng);
        BISHOP_MAGICS[s] = find_magic(sq, BISHOP_MASK[s], false,
                                      BISHOP_ATTACKS[s], BISHOP_SHIFTS[s], rng);
    }
}

} // namespace roj
