// Roj chess engine — Phase 2: search core (negamax/ordering/qsearch/TT + ID/PV).
//
// Steps 2-6 built fail-soft negamax, MVV-LVA, quiescence, killers/history and the
// transposition table. Step 7 adds iterative deepening, a triangular principal-
// variation table (section 3 decision 6), and full UCI `info` (section 6), wiring
// the search into `go`. No aspiration windows, no draw detection, no time
// management, single-threaded. C++17 only.

#ifndef ROJ_SEARCH_H
#define ROJ_SEARCH_H

#include "types.h"
#include "position.h"
#include "movegen.h"
#include "value.h"

#include <cstdint>
#include <string>

namespace roj {

class TranspositionTable;   // forward declaration

struct SearchResult {
    int  score;
    Move best;
};

// Triangular principal-variation table (section 3 decision 6): pv[ply] holds the
// PV from that ply; length[ply] its length. Collected during the search by copying
// the child's PV in behind the current best move — NOT extracted from the TT.
struct PvTable {
    Move pv[MAX_PLY][MAX_PLY] = {};
    int  length[MAX_PLY] = {};
};

// Per-search state threaded through the recursion.
struct SearchInfo {
    std::uint64_t nodes               = 0;
    int           seldepth            = 0;   // max ply reached this iteration (incl. qsearch)
    bool          use_mvv_lva         = true;
    bool          use_qsearch         = false;
    bool          use_delta_pruning   = true;
    bool          use_killers_history = false;

    Move killers[MAX_PLY][2] = {};
    int  history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    TranspositionTable* tt = nullptr;
    bool tt_tripwire = false;

    // Step 7: when set, the search collects the PV here AND uses the TT for move
    // ordering ONLY (no TT cutoffs), so the PV is complete and the score is
    // order-invariant (ID(N) == a direct depth-N search). When null, Steps 2-6
    // behaviour is unchanged (full TT cutoffs, no PV).
    PvTable* pv = nullptr;
};

// Fail-soft negamax alpha-beta at fixed `depth`.
int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info);

// Root wrapper at the FULL window (exact minimax value), tracking the best move.
SearchResult search_root(Position& pos, int depth, SearchInfo& info);

// Iterative deepening (Step 7): search depths 1..maxDepth reusing the TT, keeping
// the best move + PV of the last completed iteration. When printInfo, emit a UCI
// `info` line after each iteration. Returns the final iteration's result.
SearchResult search_id(Position& pos, int maxDepth, SearchInfo& info, bool printInfo);

// Quiescence search (Step 4).
int qsearch(Position& pos, int alpha, int beta, int ply, SearchInfo& info);

// Step 2/3 convenience overloads.
int search(Position& pos, int depth, int alpha, int beta, int ply);
SearchResult search_root(Position& pos, int depth);

// Plain no-pruning negamax MINIMAX oracle.
int minimax(Position& pos, int depth, int ply);

// --- Move-ordering layer ----------------------------------------------------
void order_moves(const Position& pos, MoveList& ml);
int capture_score(const Position& pos, Move m);
void store_killer(SearchInfo& info, int ply, Move m);
void update_history(SearchInfo& info, Color side, Move m, int depth);

// UCI score field: "mate N" (section 4 mate convention) or "cp X". Exposed for tests.
std::string score_to_uci(int score);

} // namespace roj

#endif // ROJ_SEARCH_H
