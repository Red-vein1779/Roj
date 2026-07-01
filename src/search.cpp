// Roj chess engine — Phase 2: negamax + MVV-LVA + quiescence + killers/history (search.h).

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

// Score when the side to move has no legal moves: mated (in check) or stalemated.
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

// A "noisy" move for quiescence: any capture or a quiet queen promotion.
bool is_noisy(const Position& pos, Move m) {
    if (is_capture(pos, m)) return true;
    return is_promotion(m) && promotion_type(m) == QUEEN;
}

// Piece values for delta pruning (centipawns, matching the eval material scale).
constexpr int PIECE_VALUE[PIECE_TYPE_NB] = { 0, 100, 320, 330, 500, 900, 0 };
constexpr int DELTA_MARGIN = 200;   // ~a minor piece; see phase2.md section 9

// Ordering score bands (descending priority): captures on top, then the two
// killers, then history for the remaining quiet moves. History is kept well below
// the killer band by aging, and quiet moves with no history score 0.
constexpr int CAPTURE_BONUS  = 1 << 20;
constexpr int KILLER_0_SCORE = 1 << 19;
constexpr int KILLER_1_SCORE = 1 << 18;
constexpr int HISTORY_MAX    = 1 << 16;   // halve the table when an entry exceeds this

// Ordering key for the MAIN search (captures via MVV-LVA, then killers, then
// history). qsearch uses order_moves() below (MVV-LVA only).
int move_order_score(const Position& pos, Move m, int ply, const SearchInfo& info) {
    if (info.use_mvv_lva) {
        const int cap = capture_score(pos, m);
        if (cap > 0) return CAPTURE_BONUS + cap;   // captures always ahead of quiets
    }
    if (info.use_killers_history) {
        if (ply < MAX_PLY) {
            if (m == info.killers[ply][0]) return KILLER_0_SCORE;
            if (m == info.killers[ply][1]) return KILLER_1_SCORE;
        }
        return info.history[pos.side_to_move][from_sq(m)][to_sq(m)];
    }
    return 0;
}

void order_search_moves(const Position& pos, MoveList& ml, int ply, const SearchInfo& info) {
    if (!info.use_mvv_lva && !info.use_killers_history)
        return;                                    // natural order (Step 2)
    std::stable_sort(ml.moves, ml.moves + ml.count,
        [&](Move a, Move b) {
            return move_order_score(pos, a, ply, info) > move_order_score(pos, b, ply, info);
        });
}

void clear_killers_history(SearchInfo& info) {
    for (int p = 0; p < MAX_PLY; ++p) {
        info.killers[p][0] = MOVE_NONE;
        info.killers[p][1] = MOVE_NONE;
    }
    for (int c = 0; c < COLOR_NB; ++c)
        for (int f = 0; f < SQUARE_NB; ++f)
            for (int t = 0; t < SQUARE_NB; ++t)
                info.history[c][f][t] = 0;
}

} // namespace

int capture_score(const Position& pos, Move m) {
    if (!is_capture(pos, m))
        return 0;
    const int victim = (move_type(m) == EN_PASSANT)
                         ? static_cast<int>(PAWN)
                         : static_cast<int>(piece_type_on(pos, to_sq(m)));
    const int aggressor = static_cast<int>(piece_type_on(pos, from_sq(m)));
    return victim * 16 - aggressor;
}

void order_moves(const Position& pos, MoveList& ml) {
    std::stable_sort(ml.moves, ml.moves + ml.count,
        [&pos](Move a, Move b) { return capture_score(pos, a) > capture_score(pos, b); });
}

void store_killer(SearchInfo& info, int ply, Move m) {
    if (ply < 0 || ply >= MAX_PLY) return;
    if (info.killers[ply][0] == m) return;         // already killer 0: no duplicate
    info.killers[ply][1] = info.killers[ply][0];   // shift slot 0 -> slot 1
    info.killers[ply][0] = m;                      // install new killer 0
}

void update_history(SearchInfo& info, Color side, Move m, int depth) {
    int& h = info.history[side][from_sq(m)][to_sq(m)];
    h += depth * depth;                            // depth-weighted bonus
    if (h > HISTORY_MAX) {                          // aging: halve the whole table
        for (int c = 0; c < COLOR_NB; ++c)
            for (int f = 0; f < SQUARE_NB; ++f)
                for (int t = 0; t < SQUARE_NB; ++t)
                    info.history[c][f][t] >>= 1;
    }
}

int qsearch(Position& pos, int alpha, int beta, int ply, SearchInfo& info) {
    ++info.nodes;

    if (ply >= MAX_PLY)              // safety guard; natural termination is material decrease
        return evaluate(pos);

    const bool inCheck = in_check(pos);

    MoveList ml;
    int best;
    int stand_pat = 0;

    if (inCheck) {
        generate_legal_moves(pos, ml);
        if (ml.count == 0)
            return -VALUE_MATE + ply;              // checkmate at a qsearch node
        best = -VALUE_INFINITE;
    } else {
        stand_pat = evaluate(pos);
        if (stand_pat >= beta)
            return stand_pat;
        best = stand_pat;
        if (stand_pat > alpha)
            alpha = stand_pat;

        MoveList legal;
        generate_legal_moves(pos, legal);
        for (int i = 0; i < legal.count; ++i)
            if (is_noisy(pos, legal.moves[i]))
                ml.add(legal.moves[i]);
    }

    if (info.use_mvv_lva)
        order_moves(pos, ml);

    for (int i = 0; i < ml.count; ++i) {
        const Move m = ml.moves[i];

        if (!inCheck && info.use_delta_pruning && is_capture(pos, m) && !is_promotion(m)) {
            const int victim = (move_type(m) == EN_PASSANT)
                                 ? PIECE_VALUE[PAWN]
                                 : PIECE_VALUE[piece_type_on(pos, to_sq(m))];
            if (stand_pat + victim + DELTA_MARGIN < alpha)
                continue;
        }

        make_move(pos, m);
        const int score = -qsearch(pos, -beta, -alpha, ply + 1, info);
        unmake_move(pos, m);

        if (score > best) best = score;
        if (best > alpha)  alpha = best;
        if (alpha >= beta) break;
    }
    return best;
}

int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info) {
    ++info.nodes;

    MoveList ml;
    generate_legal_moves(pos, ml);
    if (ml.count == 0)               // checkmate / stalemate, scored BEFORE depth
        return terminal_score(pos, ply);
    if (depth == 0)                  // horizon: quiescence or the static eval stub
        return info.use_qsearch ? qsearch(pos, alpha, beta, ply, info) : evaluate(pos);

    order_search_moves(pos, ml, ply, info);

    int best = -VALUE_INFINITE;      // fail-soft floor
    for (int i = 0; i < ml.count; ++i) {
        const Move m = ml.moves[i];
        make_move(pos, m);
        const int score = -search(pos, depth - 1, -beta, -alpha, ply + 1, info);
        unmake_move(pos, m);

        if (score > best) best = score;
        if (best > alpha)  alpha = best;
        if (alpha >= beta) {
            // Beta cutoff: reward a QUIET cutoff move for ordering (killers +
            // history). Captures are already ordered by MVV-LVA, so they are not
            // stored as killers.
            if (info.use_killers_history && !is_capture(pos, m)) {
                store_killer(info, ply, m);
                update_history(info, pos.side_to_move, m, depth);
            }
            break;
        }
    }
    return best;                     // FAIL-SOFT: the true best, never clamped
}

SearchResult search_root(Position& pos, int depth, SearchInfo& info) {
    if (info.use_killers_history)
        clear_killers_history(info);              // hygiene: start each search clean

    ++info.nodes;

    MoveList ml;
    generate_legal_moves(pos, ml);
    if (ml.count == 0)
        return { terminal_score(pos, 0), MOVE_NONE };
    if (depth == 0)
        return { info.use_qsearch ? qsearch(pos, -VALUE_INFINITE, VALUE_INFINITE, 0, info)
                                  : evaluate(pos), MOVE_NONE };

    order_search_moves(pos, ml, 0, info);

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

// --- Step 2/3 convenience overloads (ordering OFF, quiescence OFF) -----------
int search(Position& pos, int depth, int alpha, int beta, int ply) {
    SearchInfo info;
    info.use_mvv_lva = false;
    info.use_qsearch = false;
    return search(pos, depth, alpha, beta, ply, info);
}

SearchResult search_root(Position& pos, int depth) {
    SearchInfo info;
    info.use_mvv_lva = false;
    info.use_qsearch = false;
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
