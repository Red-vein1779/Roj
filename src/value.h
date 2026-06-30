// Roj chess engine — Phase 2: score values and the mate-distance TT convention.
//
// This header locks the score conventions of phase2.md section 4. They are the
// Phase 2 counterpart of Phase 1's spiked en-passant convention: a detail that
// must be fixed BEFORE the search and transposition table are written, otherwise
// the TT invariant misfires or `info score mate N` comes out wrong.
//
// NOTE: this defines the convention only. The transposition table that USES it is
// Phase 2 Step 6 — no TT is built here. Platform-independent C++17 only.

#ifndef ROJ_VALUE_H
#define ROJ_VALUE_H

namespace roj {

// Scores are computed as `int` (32-bit) throughout the search. The transposition
// table (Step 6) will store them as int16_t to save space, so every sentinel
// below fits in int16_t (|value| <= 32767) with margin.
constexpr int VALUE_DRAW     = 0;
constexpr int VALUE_MATE     = 32000;
constexpr int VALUE_INFINITE = 32001;   // strictly above any real score; used as
                                         // the initial alpha/beta window in Step 2.
constexpr int MAX_PLY        = 246;      // hard ceiling on search ply.

// Mate-distance convention:
//   A mate WE deliver in N plies scores   (VALUE_MATE - N).
//   A mate AGAINST us in N plies scores   (-VALUE_MATE + N).
// Closer mates are more extreme (a faster mate for us scores higher; a faster
// mate against us scores lower). Any score at or beyond these thresholds is a
// "mate score"; everything strictly between them is an ordinary material/
// positional score.
constexpr int VALUE_MATE_IN_MAX_PLY  =  VALUE_MATE - MAX_PLY;   // 31754
constexpr int VALUE_MATED_IN_MAX_PLY = -VALUE_MATE + MAX_PLY;   // -31754

// Mate scores are found relative to the NODE that proves them, but the rest of
// the search reasons about them relative to the ROOT. value_to_tt rebases a
// score from root-relative to node-relative for storage; value_from_tt rebases a
// stored node-relative score back to root-relative on probe. Ordinary scores
// pass through unchanged. These are pure functions (no state); the TT that calls
// them is Step 6. They are unit-tested now so Step 6 is mechanical.
constexpr int value_to_tt(int v, int ply) {
    if (v >= VALUE_MATE_IN_MAX_PLY)  return v + ply;
    if (v <= VALUE_MATED_IN_MAX_PLY) return v - ply;
    return v;
}

constexpr int value_from_tt(int v, int ply) {
    if (v >= VALUE_MATE_IN_MAX_PLY)  return v - ply;
    if (v <= VALUE_MATED_IN_MAX_PLY) return v + ply;
    return v;
}

} // namespace roj

#endif // ROJ_VALUE_H
