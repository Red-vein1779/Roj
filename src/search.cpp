// Roj chess engine — Phase 2: negamax alpha-beta + MVV-LVA move ordering (search.h).

#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "bitboard.h"
#include "value.h"

#include <algorithm>

namespace roj {
namespace {

// Is the side to move in check? Its king's square attacked by the opponent.
bool in_check(const Position& pos) {
    const Square ksq = lsb(pos.pieces[pos.side_to_move][KING]);
    return is_attacked(ksq, ~pos.side_to_move, pos);
}

// Score when the side to move has no legal moves: mated (in check) or stalemated
// (not). Node-relative mate distance per phase2.md section 4.
int terminal_score(const Position& pos, int ply) {
    return in_check(pos) ? (-VALUE_MATE + ply) : VALUE_DRAW;
}

// Does move m capture? A normal move captures iff an enemy piece stands on `to`;
// en passant always captures; a promotion captures iff it carries the capture bit.
bool is_capture(const Position& pos, Move m) {
    const MoveType mt = move_type(m);
    if (mt == EN_PASSANT) return true;
    if (is_promotion(m))  return (mt & 4) != 0;   // PROMO_*_CAP have bit 2 set
    return test_bit(pos.byColor[~pos.side_to_move], to_sq(m));
}

} // namespace

int capture_score(const Position& pos, Move m) {
    if (!is_capture(pos, m))
        return 0;                                 // quiet moves sort after captures
    // Victim: the captured piece type (a pawn for en passant). Aggressor: the
    // mover. Piece-type ordinals are PAWN=1 < N=2 < B=3 < R=4 < Q=5 < K=6, so
    // "victim * 16 - aggressor" ranks a more valuable victim first and, among
    // equal victims, the least valuable aggressor first (PxQ before RxQ).
    const int victim = (move_type(m) == EN_PASSANT)
                         ? static_cast<int>(PAWN)
                         : static_cast<int>(piece_type_on(pos, to_sq(m)));
    const int aggressor = static_cast<int>(piece_type_on(pos, from_sq(m)));
    return victim * 16 - aggressor;
}

void order_moves(const Position& pos, MoveList& ml) {
    // Stable sort by descending ordering score: captures (positive, MVV-LVA) come
    // before quiet moves (score 0), and stability preserves the natural order
    // among equal scores — so quiet moves keep their generation order and equal
    // MVV-LVA captures keep theirs.
    std::stable_sort(ml.moves, ml.moves + ml.count,
        [&pos](Move a, Move b) { return capture_score(pos, a) > capture_score(pos, b); });
}

int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info) {
    ++info.nodes;

    MoveList ml;
    generate_legal_moves(pos, ml);
    if (ml.count == 0)               // checkmate / stalemate, scored BEFORE depth
        return terminal_score(pos, ply);
    if (depth == 0)                  // horizon: static eval, side-to-move relative
        return evaluate(pos);

    if (info.use_mvv_lva)
        order_moves(pos, ml);

    int best = -VALUE_INFINITE;      // fail-soft floor
    for (int i = 0; i < ml.count; ++i) {
        make_move(pos, ml.moves[i]);
        const int score = -search(pos, depth - 1, -beta, -alpha, ply + 1, info);
        unmake_move(pos, ml.moves[i]);

        if (score > best) best = score;
        if (best > alpha)  alpha = best;
        if (alpha >= beta) break;    // beta cutoff
    }
    return best;                     // FAIL-SOFT: the true best, never clamped
}

SearchResult search_root(Position& pos, int depth, SearchInfo& info) {
    ++info.nodes;

    MoveList ml;
    generate_legal_moves(pos, ml);
    if (ml.count == 0)
        return { terminal_score(pos, 0), MOVE_NONE };
    if (depth == 0)
        return { evaluate(pos), MOVE_NONE };

    if (info.use_mvv_lva)
        order_moves(pos, ml);

    int  best     = -VALUE_INFINITE;
    Move bestMove = MOVE_NONE;
    int  alpha    = -VALUE_INFINITE;
    const int beta = VALUE_INFINITE;

    for (int i = 0; i < ml.count; ++i) {
        make_move(pos, ml.moves[i]);
        const int score = -search(pos, depth - 1, -beta, -alpha, 1, info);
        unmake_move(pos, ml.moves[i]);

        if (score > best) { best = score; bestMove = ml.moves[i]; }
        if (best > alpha) alpha = best;
        // Full window (beta = +VALUE_INFINITE): no root cutoff; best is exact.
    }
    return { best, bestMove };
}

// --- Step 2 convenience overloads (ordering OFF, node count discarded) -------
int search(Position& pos, int depth, int alpha, int beta, int ply) {
    SearchInfo info;
    info.use_mvv_lva = false;
    return search(pos, depth, alpha, beta, ply, info);
}

SearchResult search_root(Position& pos, int depth) {
    SearchInfo info;
    info.use_mvv_lva = false;
    return search_root(pos, depth, info);
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
