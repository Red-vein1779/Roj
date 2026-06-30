// Roj chess engine — Phase 2, Step 2: fail-soft negamax alpha-beta (see search.h).

#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "bitboard.h"
#include "value.h"

namespace roj {
namespace {

// Is the side to move in check? Its king's square attacked by the opponent.
// Every legal position has exactly one king of each colour, so lsb() is safe.
bool in_check(const Position& pos) {
    const Square ksq = lsb(pos.pieces[pos.side_to_move][KING]);
    return is_attacked(ksq, ~pos.side_to_move, pos);
}

// Score when the side to move has no legal moves: mated (in check) or stalemated
// (not). Node-relative mate distance per phase2.md section 4.
int terminal_score(const Position& pos, int ply) {
    return in_check(pos) ? (-VALUE_MATE + ply) : VALUE_DRAW;
}

} // namespace

int search(Position& pos, int depth, int alpha, int beta, int ply) {
    MoveList ml;
    generate_legal_moves(pos, ml);

    if (ml.count == 0)               // checkmate / stalemate, scored BEFORE depth
        return terminal_score(pos, ply);
    if (depth == 0)                  // horizon: static eval, side-to-move relative
        return evaluate(pos);

    int best = -VALUE_INFINITE;      // fail-soft floor
    for (int i = 0; i < ml.count; ++i) {
        make_move(pos, ml.moves[i]);
        const int score = -search(pos, depth - 1, -beta, -alpha, ply + 1);
        unmake_move(pos, ml.moves[i]);

        if (score > best) best = score;
        if (best > alpha)  alpha = best;
        if (alpha >= beta) break;    // beta cutoff
    }
    return best;                     // FAIL-SOFT: the true best, never clamped
}

SearchResult search_root(Position& pos, int depth) {
    MoveList ml;
    generate_legal_moves(pos, ml);

    if (ml.count == 0)
        return { terminal_score(pos, 0), MOVE_NONE };
    if (depth == 0)
        return { evaluate(pos), MOVE_NONE };

    int  best     = -VALUE_INFINITE;
    Move bestMove = MOVE_NONE;
    int  alpha    = -VALUE_INFINITE;
    const int beta = VALUE_INFINITE;

    for (int i = 0; i < ml.count; ++i) {
        make_move(pos, ml.moves[i]);
        const int score = -search(pos, depth - 1, -beta, -alpha, 1);
        unmake_move(pos, ml.moves[i]);

        if (score > best) { best = score; bestMove = ml.moves[i]; }
        if (best > alpha) alpha = best;
        // Full window (beta = +VALUE_INFINITE): alpha never reaches beta, so the
        // root never cuts off and `best` is the exact minimax value.
    }
    return { best, bestMove };
}

int minimax(Position& pos, int depth, int ply) {
    MoveList ml;
    generate_legal_moves(pos, ml);

    if (ml.count == 0)
        return terminal_score(pos, ply);
    if (depth == 0)
        return evaluate(pos);

    int best = -VALUE_INFINITE;
    for (int i = 0; i < ml.count; ++i) {
        make_move(pos, ml.moves[i]);
        const int score = -minimax(pos, depth - 1, ply + 1);
        unmake_move(pos, ml.moves[i]);
        if (score > best) best = score;
    }
    return best;
}

} // namespace roj
