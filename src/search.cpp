// Roj chess engine — Phase 2: search core (search.h).

#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "bitboard.h"
#include "value.h"
#include "tt.h"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace roj {
namespace {

bool in_check(const Position& pos) {
    const Square ksq = lsb(pos.pieces[pos.side_to_move][KING]);
    return is_attacked(ksq, ~pos.side_to_move, pos);
}

int terminal_score(const Position& pos, int ply) {
    return in_check(pos) ? (-VALUE_MATE + ply) : VALUE_DRAW;
}

bool is_capture(const Position& pos, Move m) {
    const MoveType mt = move_type(m);
    if (mt == EN_PASSANT) return true;
    if (is_promotion(m))  return (mt & 4) != 0;
    return test_bit(pos.byColor[~pos.side_to_move], to_sq(m));
}

bool is_noisy(const Position& pos, Move m) {
    if (is_capture(pos, m)) return true;
    return is_promotion(m) && promotion_type(m) == QUEEN;
}

constexpr int PIECE_VALUE[PIECE_TYPE_NB] = { 0, 100, 320, 330, 500, 900, 0 };
constexpr int DELTA_MARGIN = 200;

constexpr int TT_MOVE_SCORE  = 1 << 24;
constexpr int CAPTURE_BONUS  = 1 << 20;
constexpr int KILLER_0_SCORE = 1 << 19;
constexpr int KILLER_1_SCORE = 1 << 18;
constexpr int HISTORY_MAX    = 1 << 16;

int move_order_score(const Position& pos, Move m, int ply, const SearchInfo& info, Move ttMove) {
    if (ttMove != MOVE_NONE && m == ttMove)
        return TT_MOVE_SCORE;
    if (info.use_mvv_lva) {
        const int cap = capture_score(pos, m);
        if (cap > 0) return CAPTURE_BONUS + cap;
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

void order_search_moves(const Position& pos, MoveList& ml, int ply, const SearchInfo& info, Move ttMove) {
    if (ttMove == MOVE_NONE && !info.use_mvv_lva && !info.use_killers_history)
        return;
    std::stable_sort(ml.moves, ml.moves + ml.count,
        [&](Move a, Move b) {
            return move_order_score(pos, a, ply, info, ttMove) > move_order_score(pos, b, ply, info, ttMove);
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

// Record "move m + child's PV" as this node's PV (triangular copy).
void pv_update(SearchInfo& info, int ply, Move m) {
    if (info.pv == nullptr || ply + 1 >= MAX_PLY) return;
    info.pv->pv[ply][0] = m;
    const int childLen = info.pv->length[ply + 1];
    for (int k = 0; k < childLen && ply + 1 + k < MAX_PLY; ++k)
        info.pv->pv[ply][k + 1] = info.pv->pv[ply + 1][k];
    info.pv->length[ply] = childLen + 1;
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
    if (info.killers[ply][0] == m) return;
    info.killers[ply][1] = info.killers[ply][0];
    info.killers[ply][0] = m;
}

void update_history(SearchInfo& info, Color side, Move m, int depth) {
    int& h = info.history[side][from_sq(m)][to_sq(m)];
    h += depth * depth;
    if (h > HISTORY_MAX) {
        for (int c = 0; c < COLOR_NB; ++c)
            for (int f = 0; f < SQUARE_NB; ++f)
                for (int t = 0; t < SQUARE_NB; ++t)
                    info.history[c][f][t] >>= 1;
    }
}

int qsearch(Position& pos, int alpha, int beta, int ply, SearchInfo& info) {
    ++info.nodes;
    if (ply > info.seldepth) info.seldepth = ply;

    if (ply >= MAX_PLY)
        return evaluate(pos);

    const bool inCheck = in_check(pos);

    MoveList ml;
    int best;
    int stand_pat = 0;

    if (inCheck) {
        generate_legal_moves(pos, ml);
        if (ml.count == 0)
            return -VALUE_MATE + ply;
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
    if (ply > info.seldepth) info.seldepth = ply;
    if (info.pv) info.pv->length[ply] = 0;

    MoveList ml;
    generate_legal_moves(pos, ml);
    if (ml.count == 0)
        return terminal_score(pos, ply);
    if (depth == 0)
        return info.use_qsearch ? qsearch(pos, alpha, beta, ply, info) : evaluate(pos);

    const int alpha_orig = alpha;

    // TT probe (before moving). GHI (section 9): no draw scores stored yet
    // (Step 8); repetition must be detected on the path before trusting a TT draw.
    // TT CUTOFFS are taken only when NOT collecting a PV (info.pv == nullptr); in
    // PV mode the TT is used for move ordering only, so the PV is complete and the
    // score is order-invariant.
    Move ttMove = MOVE_NONE;
    if (info.tt != nullptr) {
        TTEntry e;
        if (info.tt->probe(pos.hash, e)) {
            ttMove = e.move;
            if (info.pv == nullptr && e.depth >= depth) {
                const int s = value_from_tt(e.score, ply);
                if (e.bound == BOUND_EXACT) return s;
                if (e.bound == BOUND_LOWER && s >= beta) return s;
                if (e.bound == BOUND_UPPER && s <= alpha) return s;
            }
        }
    }

    order_search_moves(pos, ml, ply, info, ttMove);

    int  best     = -VALUE_INFINITE;
    Move bestMove = MOVE_NONE;
    for (int i = 0; i < ml.count; ++i) {
        const Move m = ml.moves[i];
        make_move(pos, m);
        const int score = -search(pos, depth - 1, -beta, -alpha, ply + 1, info);
        unmake_move(pos, m);

        if (score > best) { best = score; bestMove = m; pv_update(info, ply, m); }
        if (best > alpha)  alpha = best;
        if (alpha >= beta) {
            if (info.use_killers_history && !is_capture(pos, m)) {
                store_killer(info, ply, m);
                update_history(info, pos.side_to_move, m, depth);
            }
            break;
        }
    }

    if (info.tt != nullptr) {
        const Bound b = (best <= alpha_orig) ? BOUND_UPPER
                      : (best >= beta)       ? BOUND_LOWER
                                             : BOUND_EXACT;
        int storeScore = value_to_tt(best, ply);
        if (info.tt_tripwire) storeScore += 7;
        info.tt->store(pos.hash, bestMove,
                       static_cast<std::int16_t>(storeScore),
                       static_cast<std::int16_t>(depth), b);
    }
    return best;
}

SearchResult search_root(Position& pos, int depth, SearchInfo& info) {
    if (info.use_killers_history)
        clear_killers_history(info);

    ++info.nodes;
    if (info.pv) info.pv->length[0] = 0;

    MoveList ml;
    generate_legal_moves(pos, ml);
    if (ml.count == 0)
        return { terminal_score(pos, 0), MOVE_NONE };
    if (depth == 0)
        return { info.use_qsearch ? qsearch(pos, -VALUE_INFINITE, VALUE_INFINITE, 0, info)
                                  : evaluate(pos), MOVE_NONE };

    // Root probes the TT for the ordering move (the previous iteration's best);
    // no cutoff at the root (full window). Stores the root result so the next ID
    // iteration finds the best move first.
    Move ttMove = MOVE_NONE;
    if (info.tt != nullptr) {
        TTEntry e;
        if (info.tt->probe(pos.hash, e)) ttMove = e.move;
    }

    order_search_moves(pos, ml, 0, info, ttMove);

    int  best     = -VALUE_INFINITE;
    Move bestMove = MOVE_NONE;
    int  alpha    = -VALUE_INFINITE;
    const int beta = VALUE_INFINITE;

    for (int i = 0; i < ml.count; ++i) {
        make_move(pos, ml.moves[i]);
        const int score = -search(pos, depth - 1, -beta, -alpha, 1, info);
        unmake_move(pos, ml.moves[i]);

        if (score > best) { best = score; bestMove = ml.moves[i]; pv_update(info, 0, ml.moves[i]); }
        if (best > alpha) alpha = best;
    }

    if (info.tt != nullptr)
        info.tt->store(pos.hash, bestMove, static_cast<std::int16_t>(value_to_tt(best, 0)),
                       static_cast<std::int16_t>(depth), BOUND_EXACT);
    return { best, bestMove };
}

std::string score_to_uci(int score) {
    if (score >= VALUE_MATE_IN_MAX_PLY) {
        const int matePlies = VALUE_MATE - score;
        return "mate " + std::to_string((matePlies + 1) / 2);
    }
    if (score <= VALUE_MATED_IN_MAX_PLY) {
        const int matePlies = VALUE_MATE + score;   // distance for the mated side
        return "mate -" + std::to_string((matePlies + 1) / 2);
    }
    return "cp " + std::to_string(score);
}

namespace {
std::string pv_to_uci(const SearchInfo& info) {
    std::string s;
    if (info.pv == nullptr) return s;
    for (int k = 0; k < info.pv->length[0]; ++k)
        s += ' ' + move_to_uci(info.pv->pv[0][k]);
    return s;
}
} // namespace

SearchResult search_id(Position& pos, int maxDepth, SearchInfo& info, bool printInfo) {
    SearchResult best{ 0, MOVE_NONE };
    const auto t0 = std::chrono::steady_clock::now();

    for (int d = 1; d <= maxDepth; ++d) {
        info.seldepth = 0;
        best = search_root(pos, d, info);

        if (printInfo) {
            const auto t1 = std::chrono::steady_clock::now();
            const long long ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            const long long nps = (ms > 0)
                ? static_cast<long long>(info.nodes * 1000ULL / static_cast<std::uint64_t>(ms))
                : 0;
            std::cout << "info depth " << d
                      << " seldepth " << info.seldepth
                      << " score " << score_to_uci(best.score)
                      << " nodes " << info.nodes
                      << " nps " << nps
                      << " time " << ms
                      << " pv" << pv_to_uci(info)
                      << std::endl;
        }
    }
    return best;
}

// --- Step 2/3 convenience overloads (ordering OFF, quiescence OFF, no TT) -----
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
