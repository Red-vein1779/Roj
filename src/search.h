// Roj chess engine — Phase 2: negamax alpha-beta + move ordering + quiescence.
//
// Step 2: fixed-depth fail-soft negamax (section 3 decision 1), ply-relative mate
// scoring (section 4). Step 3: MVV-LVA capture ordering. Step 4: quiescence
// (captures + queen promotions + evasions; stand-pat + delta pruning). Step 5:
// quiet-move ordering with killer moves + a history heuristic (section 3 decision
// 4: TT -> MVV-LVA -> killers -> history; the TT move is Step 6). Still no
// transposition table, no iterative deepening, no time management, no SEE.
// Platform-independent C++17 only.

#ifndef ROJ_SEARCH_H
#define ROJ_SEARCH_H

#include "types.h"
#include "position.h"
#include "movegen.h"
#include "value.h"

#include <cstdint>

namespace roj {

// A root search result: the exact (full-window) score and the root move.
struct SearchResult {
    int  score;
    Move best;
};

// Per-search state threaded through the recursion: a node counter, feature toggles
// and the quiet-move ordering tables. Defaults reproduce the Step 3 behaviour
// (ordering on, quiescence OFF, killers/history OFF); the Step 2 wrappers below
// additionally turn ordering off. The tables are cleared at the start of each root
// search (hygiene); a fresh SearchInfo also starts zeroed.
struct SearchInfo {
    std::uint64_t nodes               = 0;
    bool          use_mvv_lva         = true;
    bool          use_qsearch         = false;  // false: static eval at depth 0 (Steps 2/3)
    bool          use_delta_pruning   = true;   // delta pruning inside qsearch
    bool          use_killers_history = false;  // Step 5 quiet-move ordering

    Move killers[MAX_PLY][2] = {};                          // two killer moves per ply
    int  history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};      // [side][from][to] history
};

// Fail-soft negamax alpha-beta at fixed `depth`, with the search context.
int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info);

// Root wrapper at the FULL window (exact minimax value), tracking the best move.
// Clears the killer/history tables at entry when use_killers_history is set.
SearchResult search_root(Position& pos, int depth, SearchInfo& info);

// Quiescence search (Step 4). At a leaf, extend over noisy moves until quiet.
int qsearch(Position& pos, int alpha, int beta, int ply, SearchInfo& info);

// Step 2/3 convenience overloads: natural move order, quiescence OFF, killers/
// history OFF, node count discarded. Kept so the earlier tests/callers are intact.
int search(Position& pos, int depth, int alpha, int beta, int ply);
SearchResult search_root(Position& pos, int depth);

// Plain no-pruning negamax MINIMAX oracle (static eval at depth 0). The invariance
// gate compares against this on the ordering-ON but quiescence-OFF path.
int minimax(Position& pos, int depth, int ply);

// --- Move-ordering layer ----------------------------------------------------
// Step 3 capture ordering (MVV-LVA), used by qsearch and exposed for tests.
void order_moves(const Position& pos, MoveList& ml);
int capture_score(const Position& pos, Move m);

// Step 5 quiet-move mechanics, exposed for unit tests. store_killer shifts killer
// 0 into slot 1 and installs m in slot 0 (no duplicate). update_history adds
// depth*depth to [side][from][to] and halves the whole table if an entry exceeds
// the cap (aging / overflow protection).
void store_killer(SearchInfo& info, int ply, Move m);
void update_history(SearchInfo& info, Color side, Move m, int depth);

} // namespace roj

#endif // ROJ_SEARCH_H
