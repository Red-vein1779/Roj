// Roj chess engine — Phase 2, Step 10: `bench` node signature.
//
// bench runs a FIXED set of positions each to a FIXED depth with a FIXED-size
// transposition table, using the exact search the `go` play path uses (fixed depth,
// single-threaded, no time checks -> fully deterministic). It sums the node count
// over all positions into a single signature number. That number is committed to the
// repo (see bench.cpp) Stockfish-style, so any accidental change to the search shows
// up immediately as a changed bench count. Platform-independent C++17 only.

#ifndef ROJ_BENCH_H
#define ROJ_BENCH_H

#include <cstdint>

namespace roj {

// Run the benchmark suite and return the total node count. When `verbose`, prints a
// per-position line plus a final summary (`Nodes searched: <N>`, time, nps). The
// returned count is deterministic across runs, rebuilds and platforms.
std::uint64_t run_bench(bool verbose);

} // namespace roj

#endif // ROJ_BENCH_H
