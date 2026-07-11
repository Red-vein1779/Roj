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

// Step 8 draw detection: is the position at this node a draw for search purposes?
// Covers the 50-move rule, insufficient material, and twofold repetition (the
// current key already appears earlier along the path — pre-root history or an
// ancestor in the tree). The no-legal-moves terminal test is done by the CALLER
// BEFORE this, so checkmate takes precedence over the 50-move rule (§9 "50-drag mot
// matt"). GHI (§9): repetition is a PATH-dependent property while the TT is
// path-independent, so this check must run BEFORE the TT is probed for a score; we
// accept residual GHI imperfection in Phase 2 (not fully solved here). The
// repetition key is pos.hash — Phase 1's incremental Zobrist — so the en-passant
// convention matches Phase 1 exactly (position.cpp), avoiding a false-alarming key.
bool is_draw(const Position& pos, const SearchInfo& info) {
    if (pos.halfmove_clock >= 100)     // 50-move rule (100 plies)
        return true;
    if (insufficient_material(pos))
        return true;
    // Twofold in tree / pre-root repetition: any single earlier occurrence of the
    // current key is treated as a draw (a search optimisation, not the threefold
    // game rule).
    for (std::size_t i = 0; i < info.rep.size(); ++i)
        if (info.rep[i] == pos.hash)
            return true;
    return false;
}

// Step 9: milliseconds elapsed since the search started (search_id stamped start_time).
long long elapsed_ms(const SearchInfo& info) {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now() - info.start_time).count();
}

// Step 9: poll the abort conditions. Called once every CHECK_NODES nodes (a power of
// two so the test is a cheap mask). It never fires until depth 1 has completed
// (abort_armed), which guarantees the search always returns a legal move. Sources:
// an external `stop`, the node limit, or the hard wall-clock limit.
constexpr std::uint64_t CHECK_NODES = 2048;
void check_abort(SearchInfo& info) {
    if (info.aborted || !info.abort_armed) return;
    if (info.stop != nullptr && *info.stop)                    { info.aborted = true; return; }
    if (info.max_nodes != 0 && info.nodes >= info.max_nodes)   { info.aborted = true; return; }
    if (info.use_time_management && elapsed_ms(info) >= info.hard_ms) { info.aborted = true; }
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

bool insufficient_material(const Position& pos) {
    // Pawns, rooks and queens are always mating material.
    if (pos.pieces[WHITE][PAWN]  | pos.pieces[BLACK][PAWN])  return false;
    if (pos.pieces[WHITE][ROOK]  | pos.pieces[BLACK][ROOK])  return false;
    if (pos.pieces[WHITE][QUEEN] | pos.pieces[BLACK][QUEEN]) return false;

    const int wB = popcount(pos.pieces[WHITE][BISHOP]);
    const int bB = popcount(pos.pieces[BLACK][BISHOP]);
    const int minors = popcount(pos.pieces[WHITE][KNIGHT]) + popcount(pos.pieces[BLACK][KNIGHT])
                     + wB + bB;

    if (minors == 0) return true;   // K vs K
    if (minors == 1) return true;   // K + single minor (KN or KB) vs K
    // KB vs KB with both bishops on the same colour complex is a dead draw. Any
    // other two-minor material (KNN, KBN, KB vs KN, KB vs KB opposite colours) can
    // in principle mate, so we deliberately keep it out of the draw set (§ Step 8).
    if (minors == 2 && wB == 1 && bB == 1) {
        const Square ws = lsb(pos.pieces[WHITE][BISHOP]);
        const Square bs = lsb(pos.pieces[BLACK][BISHOP]);
        const int wColor = (file_of(ws) + rank_of(ws)) & 1;
        const int bColor = (file_of(bs) + rank_of(bs)) & 1;
        if (wColor == bColor) return true;
    }
    return false;
}

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

    if (info.check_time) {                       // Step 9: abort polling (no-op when off)
        if ((info.nodes & (CHECK_NODES - 1)) == 0) check_abort(info);
        if (info.aborted) return 0;              // sentinel — discarded by the caller
    }

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
        if (info.check_time && info.aborted) return best;   // bail; make/unmake balanced

        if (score > best) best = score;
        if (best > alpha)  alpha = best;
        if (alpha >= beta) break;
    }
    return best;
}

int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info, bool pv_node) {
    ++info.nodes;
    if (ply > info.seldepth) info.seldepth = ply;
    if (info.pv) info.pv->length[ply] = 0;

    if (info.check_time) {                       // Step 9: abort polling (no-op when off)
        if ((info.nodes & (CHECK_NODES - 1)) == 0) check_abort(info);
        if (info.aborted) return 0;              // sentinel — discarded by the caller
    }

    MoveList ml;
    generate_legal_moves(pos, ml);
    if (ml.count == 0)
        return terminal_score(pos, ply);   // mate/stalemate — precedes the 50-move draw

    // Step 8: draw detection runs AFTER the terminal test (so checkmate beats the
    // 50-move rule) and BEFORE the TT probe (GHI: a path-dependent draw must be
    // recognised on the path before a path-independent TT score can hide it). Drawn
    // nodes return VALUE_DRAW and are NOT stored in the TT.
    if (info.use_draw_detection && is_draw(pos, info))
        return VALUE_DRAW;

    // Phase 3 Step 3: check extension — when the side to move is in check this
    // node is searched one ply deeper (the depth budget is not decremented for
    // this level; the node's children see the un-decremented depth). Budget
    // guard (phase3.md §8): granted only while ply + depth < MAX_PLY - 1.
    // Down any path ply+depth is otherwise non-increasing (child = ply+1,
    // depth-1), so each extension raises it by exactly 1 toward a hard cap —
    // total extensions per path are bounded and ply can NEVER pass MAX_PLY,
    // even in a pathological all-check (cross-check) sequence. One extension
    // per node (§8 "ingen stapling i Fas 3"): if a second extension REASON is
    // ever added (Step 13, singular extensions), reasons must not stack — a
    // node gets at most +1 ply from all reasons combined. The ROOT is never
    // extended (ply > 0): on the play path search() is only entered at
    // ply >= 1 (search_root owns ply 0 and does not extend — the root depth IS
    // the iteration definition); direct ply-0 calls (test harnesses) follow
    // the same rule so the minimax-oracle identity compares the same tree.
    if (ply > 0 && ply + depth < MAX_PLY - 1 && in_check(pos))
        ++depth;

    if (depth == 0)
        return info.use_qsearch ? qsearch(pos, alpha, beta, ply, info) : evaluate(pos);

    const int alpha_orig = alpha;

    // TT probe (before moving). GHI (section 9): no draw scores stored yet
    // (Step 8); repetition must be detected on the path before trusting a TT draw.
    // TT VALUE CUTOFFS (Phase 3 Step 1, phase3.md §3 decision 2): non-PV nodes
    // take full cutoffs; PV nodes keep the Phase 2 lock — TT for move ordering
    // ONLY, never value cutoffs — so the triangular PV stays complete and legal.
    // The TT move is used for ordering in BOTH node types; legality is enforced
    // structurally because it is only matched against the freshly generated
    // legal move list (a colliding key can bias ordering, never inject a move).
    Move ttMove = MOVE_NONE;
    if (info.tt != nullptr) {
        TTEntry e;
        if (info.tt->probe(pos.hash, e)) {
            ttMove = e.move;
            if (!pv_node && e.depth >= depth) {
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
        // Push THIS node's key so the child sees it as an ancestor for repetition
        // detection; pop after unmaking so `rep` is always balanced (returns to the
        // seeded pre-root history). Guarded so Step 7 behaviour is untouched when
        // draw detection is off.
        if (info.use_draw_detection) info.rep.push_back(pos.hash);
        make_move(pos, m);
        int score;
        if (i == 0) {
            // First move: full window, full depth, inheriting this node's type —
            // it is the presumed-best line.
            score = -search(pos, depth - 1, -beta, -alpha, ply + 1, info, pv_node);
        } else {
            // PVS probe (phase3.md §8 "re-search-kaskaden"): every later move is
            // searched with a null window [alpha, alpha+1] as a NON-PV child
            // (full TT value cutoffs). Step 5 (LMR) will slot a reduced-depth
            // stage in FRONT of this probe:
            //   reduced null-window -> fail-high => unreduced null-window
            //   -> fail-high => full window (the re-search below).
            score = -search(pos, depth - 1, -alpha - 1, -alpha, ply + 1, info, false);
            // Re-search condition: the probe failed high (score > alpha) AND a
            // wider window exists to re-search into (beta - alpha > 1 — at a
            // null-window node the probe window IS the full window, so nothing
            // can be widened). Full window, full depth, this node's type, so a
            // PV re-search collects the complete triangular PV. Skipped when the
            // probe was aborted (its score is a discarded sentinel).
            if (score > alpha && beta - alpha > 1 && !(info.check_time && info.aborted))
                score = -search(pos, depth - 1, -beta, -alpha, ply + 1, info, pv_node);
        }
        unmake_move(pos, m);
        if (info.use_draw_detection) info.rep.pop_back();
        if (info.check_time && info.aborted) return best;   // discard; do NOT store to TT

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

SearchResult search_root(Position& pos, int depth, SearchInfo& info, int alphaIn, int betaIn) {
    if (info.use_killers_history)
        clear_killers_history(info);

    ++info.nodes;
    if (info.pv) info.pv->length[0] = 0;

    MoveList ml;
    generate_legal_moves(pos, ml);
    if (ml.count == 0)
        return { terminal_score(pos, 0), MOVE_NONE };

    // Step 8: if the root position is itself already a draw — its key repeats a
    // pre-root game position (seeded in info.rep), the 50-move clock is full, or
    // material is insufficient — report VALUE_DRAW with a legal move. Mate
    // precedence is preserved because the no-legal-moves test above ran first.
    if (info.use_draw_detection && is_draw(pos, info))
        return { VALUE_DRAW, ml.moves[0] };

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
    int  alpha    = alphaIn;
    const int beta = betaIn;
    const int alpha_orig = alphaIn;

    const bool root_pv = (info.pv != nullptr);   // the root inherits the path's type
    for (int i = 0; i < ml.count; ++i) {
        if (info.use_draw_detection) info.rep.push_back(pos.hash);   // root key is an ancestor
        make_move(pos, ml.moves[i]);
        int score;
        if (i == 0) {
            score = -search(pos, depth - 1, -beta, -alpha, 1, info, root_pv);
        } else {
            // Root PVS: the same probe/re-search cascade as in search() (§8).
            score = -search(pos, depth - 1, -alpha - 1, -alpha, 1, info, false);
            if (score > alpha && beta - alpha > 1 && !(info.check_time && info.aborted))
                score = -search(pos, depth - 1, -beta, -alpha, 1, info, root_pv);
        }
        unmake_move(pos, ml.moves[i]);
        if (info.use_draw_detection) info.rep.pop_back();
        // Step 9: on abort, return whatever we have — search_id discards this whole
        // (incomplete) iteration and keeps the last completed one.
        if (info.check_time && info.aborted) return { best, bestMove };

        if (score > best) { best = score; bestMove = ml.moves[i]; pv_update(info, 0, ml.moves[i]); }
        if (best > alpha) alpha = best;
    }

    // Phase 3 Step 2: with an aspiration window the root result can be a BOUND,
    // not an exact value — store the bound type accordingly (a fail-low/high
    // stored as EXACT would poison non-PV cutoffs at transposed nodes). At the
    // full window this is always EXACT, exactly as before.
    if (info.tt != nullptr) {
        const Bound b = (best <= alpha_orig) ? BOUND_UPPER
                      : (best >= beta)       ? BOUND_LOWER
                                             : BOUND_EXACT;
        info.tt->store(pos.hash, bestMove, static_cast<std::int16_t>(value_to_tt(best, 0)),
                       static_cast<std::int16_t>(depth), b);
    }
    return { best, bestMove };
}

SearchResult search_root(Position& pos, int depth, SearchInfo& info) {
    return search_root(pos, depth, info, -VALUE_INFINITE, VALUE_INFINITE);
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

// Step 9 time-budget constants. MOVE_OVERHEAD is a safety margin (ms) covering the
// gap between deciding to stop and the GUI receiving the bestmove; TIME_DIVISOR is
// the "assume this many moves remain" fallback when movestogo is not given.
namespace {
constexpr long long MOVE_OVERHEAD = 10;   // ms
constexpr long long TIME_DIVISOR  = 20;   // moves assumed remaining without movestogo
}

TimeBudget compute_time_budget(long long remaining, long long inc, int movestogo, long long movetime) {
    if (movetime >= 0) {
        long long t = movetime - MOVE_OVERHEAD;
        if (t < 1) t = 1;
        return { t, t };
    }
    long long rem = (remaining > 0 ? remaining : 0) - MOVE_OVERHEAD;
    if (rem < 1) rem = 1;
    long long base = (movestogo > 0 ? rem / movestogo : rem / TIME_DIVISOR)
                   + (inc > 0 ? (inc * 3) / 4 : 0);
    if (base < 1) base = 1;
    const long long soft = base;
    // The hard cap never exceeds half the usable time, so a game can never flag:
    // repeatedly halving a positive bank never reaches zero.
    long long hard = base * 3;
    if (hard > rem / 2) hard = rem / 2;
    if (hard < soft)    hard = soft;
    return { soft, hard };
}

// Phase 3 Step 2: aspiration-window parameters (our own values, tuned by SPRT).
// From ASPIRATION_MIN_DEPTH on, an iteration opens with the window
// [prev - DELTA, prev + DELTA] around the previous iteration's score. A
// fail-low/high widens ONLY the failed bound, re-centred on the failing score,
// with DELTA doubling each attempt (exponential widening); once DELTA exceeds
// ASPIRATION_DELTA_CAP — or a mate score turns up mid-widening — the iteration
// falls back to the FULL window. Mate-zone guard (phase3.md §8): if the
// PREVIOUS score is already in the mate zone, the iteration opens full-window
// directly (aspirating around a mate distance is pointless and bug-prone).
namespace {
constexpr int ASPIRATION_MIN_DEPTH = 4;    // aspire from this iteration depth on
constexpr int ASPIRATION_DELTA     = 25;   // initial half-width (cp)
constexpr int ASPIRATION_DELTA_CAP = 400;  // widen past this -> full window

bool in_mate_zone(int score) {
    return score >= VALUE_MATE_IN_MAX_PLY || score <= VALUE_MATED_IN_MAX_PLY;
}
} // namespace

SearchResult search_id(Position& pos, int maxDepth, SearchInfo& info, bool printInfo) {
    SearchResult best{ 0, MOVE_NONE };
    info.start_time      = std::chrono::steady_clock::now();
    info.aborted         = false;
    info.abort_armed     = false;   // depth 1 must always complete (guarantees a move)
    info.completed_depth = 0;

    for (int d = 1; d <= maxDepth; ++d) {
        // Step 9 soft limit: once we already hold a completed iteration, don't START
        // one we almost certainly can't finish inside the budget. (Fixed-depth and
        // node-limited searches leave use_time_management false and skip this.)
        if (info.use_time_management && info.abort_armed && elapsed_ms(info) >= info.soft_ms)
            break;

        info.seldepth = 0;
        SearchResult r{ 0, MOVE_NONE };
        // Aspiration (unconditional since the Step 2 sign-off): deep-enough
        // iterations open with a narrow window around the previous score.
        if (d >= ASPIRATION_MIN_DEPTH
            && info.completed_depth == d - 1 && !in_mate_zone(best.score)) {
            int delta = ASPIRATION_DELTA;
            int alpha = best.score - delta;
            int beta  = best.score + delta;
            for (;;) {
                r = search_root(pos, d, info, alpha, beta);
                if (info.aborted) break;                    // discarded below, as before
                const bool failLow  = r.score <= alpha;
                const bool failHigh = r.score >= beta;
                if (!failLow && !failHigh) break;           // bracketed: exact result
                delta *= 2;
                if (delta > ASPIRATION_DELTA_CAP || in_mate_zone(r.score)) {
                    r = search_root(pos, d, info, -VALUE_INFINITE, VALUE_INFINITE);
                    break;                                  // full-window fallback
                }
                if (failLow)  alpha = r.score - delta;      // widen/re-centre the failed bound
                else          beta  = r.score + delta;
            }
        } else {
            r = search_root(pos, d, info, -VALUE_INFINITE, VALUE_INFINITE);
        }
        // NOTE (`info` correctness): widening re-searches happen INSIDE this
        // iteration; the single `info` line below is printed only after the
        // iteration has converged, so no duplicate or bound-crossed lines reach
        // the GUI mid-widening.

        // Step 9 clean abort (§9): a hard-limit/stop abort discards the INCOMPLETE
        // iteration entirely; we keep the best move from the last COMPLETED one.
        if (info.aborted)
            break;

        best = r;
        info.completed_depth = d;
        info.abort_armed = true;   // depth 1 is in hand -> the hard abort may now fire

        if (printInfo) {
            const long long ms = elapsed_ms(info);
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

// Node type when the caller does not say (Phase 2 rule, and every existing test):
// the whole PV-collecting path is PV, everything else is not.
int search(Position& pos, int depth, int alpha, int beta, int ply, SearchInfo& info) {
    return search(pos, depth, alpha, beta, ply, info, info.pv != nullptr);
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
    // Step 3 sign-off: the oracle mirrors the search's now-unconditional check
    // extension (same rule, same MAX_PLY guard), so the regression identity
    // "alpha-beta == minimax" keeps comparing the SAME tree definition. Like
    // the engine (search_root never extends — the root depth IS the iteration
    // definition), the ROOT is not extended: ply > 0 only.
    if (ply > 0 && ply + depth < MAX_PLY - 1 && in_check(pos))
        ++depth;
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
