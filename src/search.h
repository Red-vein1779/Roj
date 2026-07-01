// Roj chess engine — Phase 2: negamax alpha-beta + ordering + quiescence + TT.
//
// Step 2: fail-soft negamax core. Step 3: MVV-LVA. Step 4: quiescence. Step 5:
// killers + history. Step 6: transposition table (section 3 decision 5, section 8
// invariant): probe/store keyed by the Phase 1 Zobrist hash, mate-adjusted score
// (value_to_tt/value_from_tt), bound types, and the TT move ordered first. Still
// no iterative deepening, no time management, single-threaded. C++17 only.

#ifndef ROJ_SEARCH_H
#define ROJ_SEARCH_H

#include "types.h"
#include "position.h"
#include "movegen.h"
#include "value.h"

#include <cstdint>

namespace roj {

class TranspositionTable;   // forward declaration; SearchInfo holds a pointer

// A root search result: the exact (full-window) score and the root move.
struct SearchResult {
    int  score;
    Move best;
};

// Per-search state threaded through the recursion.
struct SearchInfo {
    std::uint64_t nodes               = 0;
    bool          use_mvv_lva         = true;
    bool          use_qsearch         = false;
    bool          use_delta_pruning   = true;
    bool          use_killers_history = false;

    Move killers[MAX_PLY][2] = {};
    int  history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    // Transposition table (Step 6). nullptr = no TT (Steps 2-5 behaviour). The TT
    // is owned elsewhere and persists across searches (cleared via ucinewgame).
    TranspositionTable* tt = nullptr;
    bool tt_tripwire = false;   // TEST ONLY: corrupt the stored score to prove the invariant alarms
};

// Fail-soft negamax alpha-beta at fixed `depth`, with the search context.
int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info);

// Root wrapper at the FULL window (exact minimax value), tracking the best move.
// Clears killers/history at entry when use_killers_history is set.
SearchResult search_root(Position& pos, int depth, SearchInfo& info);

// Quiescence search (Step 4). At a leaf, extend over noisy moves until quiet.
int qsearch(Position& pos, int alpha, int beta, int ply, SearchInfo& info);

// Step 2/3 convenience overloads: natural order, quiescence/killers/TT OFF.
int search(Position& pos, int depth, int alpha, int beta, int ply);
SearchResult search_root(Position& pos, int depth);

// Plain no-pruning negamax MINIMAX oracle (static eval at depth 0).
int minimax(Position& pos, int depth, int ply);

// --- Move-ordering layer ----------------------------------------------------
void order_moves(const Position& pos, MoveList& ml);         // MVV-LVA (used by qsearch)
int capture_score(const Position& pos, Move m);
void store_killer(SearchInfo& info, int ply, Move m);
void update_history(SearchInfo& info, Color side, Move m, int depth);

} // namespace roj

#endif // ROJ_SEARCH_H
