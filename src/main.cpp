// Roj chess engine — program entry point and UCI command loop.
//
// Phase 1, step 15: the UCI loop now plays LEGAL (not good) chess. It holds one
// Position, rebuilds it from "position [startpos|fen ...] [moves ...]", and
// answers "go" with the first legal move. Real search is Phase 2. "d" prints the
// board + FEN for hand-verification. Platform-independent C++17 only.

#include "types.h"
#include "attacks.h"
#include "magic.h"
#include "zobrist.h"
#include "position.h"
#include "movegen.h"
#include "fen.h"
#include "tt.h"
#include "search.h"
#include "bench.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace roj {

void uci_identify() {
    std::cout << "id name " << ENGINE_NAME << ' ' << ENGINE_VERSION << '\n'
              << "id author " << ENGINE_AUTHOR << '\n'
              << "option name Hash type spin default 16 min 1 max 1024\n"
              // Step 11: a test knob for A/B SPRT (default true = no behaviour change).
              // Lets the harness run "quiescence on vs off" as one binary, two options.
              << "option name Qsearch type check default true\n"
              << "uciok" << std::endl;
}

// Does a generated legal move match a UCI move string (e2e4 / e7e8q)? We match
// the from/to squares and, when the string carries a promotion letter, the
// promoted piece. The move's own flag stays authoritative, so castling (e1g1),
// en passant and promotion need no special string parsing.
bool uci_matches(Move m, const std::string& s) {
    if (s.size() < 4) return false;
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8' ||
        s[2] < 'a' || s[2] > 'h' || s[3] < '1' || s[3] > '8')
        return false;

    const Square from = make_square(static_cast<File>(s[0] - 'a'), static_cast<Rank>(s[1] - '1'));
    const Square to   = make_square(static_cast<File>(s[2] - 'a'), static_cast<Rank>(s[3] - '1'));
    if (from_sq(m) != from || to_sq(m) != to)
        return false;

    if (s.size() >= 5) {                        // promotion: the piece must match
        if (!is_promotion(m)) return false;
        char got = '?';
        switch (promotion_type(m)) {
            case KNIGHT: got = 'n'; break;
            case BISHOP: got = 'b'; break;
            case ROOK:   got = 'r'; break;
            case QUEEN:  got = 'q'; break;
            default: break;
        }
        return got == s[4];
    }
    return !is_promotion(m);                     // 4-char string: the non-promotion move
}

// "position [startpos | fen <6 fields>] [moves m1 m2 ...]". Rebuilt FRESH each
// time (GUIs resend the whole game): set the base, then replay moves through the
// legal generator so every flag is correct. Unknown/illegal input is ignored.
//
// Step 8: `gameKeys` records the Zobrist key of every position actually played —
// the base position followed by the position after each applied move — so the
// search can detect a repetition that began BEFORE the root (§9 "Repetition och
// pre-rot-historik"). The final entry is the current (root) position; the ones
// before it are the pre-root history.
void uci_position(Position& pos, std::istringstream& iss, std::vector<std::uint64_t>& gameKeys) {
    std::string token;
    if (!(iss >> token)) return;

    if (token == "startpos") {
        parse_fen(pos, START_FEN);
    } else if (token == "fen") {
        std::string fen, field;
        for (int i = 0; i < 6 && (iss >> field); ++i)
            fen += (i == 0 ? "" : " ") + field;
        if (!parse_fen(pos, fen)) return;
    } else {
        return;  // malformed
    }

    gameKeys.clear();
    gameKeys.push_back(pos.hash);   // base position

    if (iss >> token && token == "moves") {
        std::string mv;
        while (iss >> mv) {
            MoveList list;
            generate_legal_moves(pos, list);
            bool applied = false;
            for (int i = 0; i < list.count; ++i)
                if (uci_matches(list.moves[i], mv)) {
                    make_move(pos, list.moves[i]);
                    gameKeys.push_back(pos.hash);
                    applied = true;
                    break;
                }
            if (!applied) break;   // unknown/illegal move: stop replaying
        }
    }
}

// "go [wtime .. btime .. winc .. binc .. movestogo ..] | movetime T | depth N |
//  nodes N | infinite": run iterative deepening under the chosen limit and print
// the best move plus a full info trail. `go depth N` stays a fixed-depth,
// deterministic search (no time checks); a clock/movetime/nodes limit drives the
// soft/hard abort (Step 9). `infinite` and a bare `go` keep the pre-Step-9 default
// (fixed depth 6): a single-threaded engine cannot receive `stop` mid-search, so a
// truly unbounded search is deferred to the async input of a later phase.
void uci_go(Position& pos, TranspositionTable& tt, std::istringstream& iss,
            const std::vector<std::uint64_t>& gameKeys, bool optQsearch) {
    long long wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1, nodes = -1;
    int movestogo = 0, depth = -1;
    bool infinite = false;
    std::string token;
    while (iss >> token) {
        if      (token == "wtime")     iss >> wtime;
        else if (token == "btime")     iss >> btime;
        else if (token == "winc")      iss >> winc;
        else if (token == "binc")      iss >> binc;
        else if (token == "movestogo") iss >> movestogo;
        else if (token == "movetime")  iss >> movetime;
        else if (token == "depth")     iss >> depth;
        else if (token == "nodes")     iss >> nodes;
        else if (token == "infinite")  infinite = true;
    }

    SearchInfo info;
    info.use_mvv_lva = true;
    info.use_killers_history = true;
    info.use_qsearch = optQsearch;   // Step 11: A/B knob (UCI option `Qsearch`)
    info.use_delta_pruning = true;
    info.tt = &tt;
    PvTable pv;
    info.pv = &pv;

    // Step 8: enable draw detection and seed the repetition history with the
    // PRE-ROOT game positions (every recorded key except the last, which is the
    // root itself). Including the root would make it repeat itself and false-alarm.
    info.use_draw_detection = true;
    if (gameKeys.size() > 1)
        info.rep.assign(gameKeys.begin(), gameKeys.end() - 1);
    info.rep.reserve(info.rep.size() + MAX_PLY);

    // Choose the limit. Priority: fixed depth > node limit > movetime > clock >
    // infinite/default. Only the first branch is deterministic (no time checks).
    int maxDepth = MAX_PLY - 1;
    if (depth >= 1) {
        maxDepth = std::min(depth, MAX_PLY - 1);          // fixed depth: check_time stays false
    } else if (nodes >= 0) {
        info.check_time = true;
        info.max_nodes  = static_cast<std::uint64_t>(nodes);
    } else if (movetime >= 0) {
        info.check_time = true;
        info.use_time_management = true;
        const TimeBudget b = compute_time_budget(0, 0, 0, movetime);
        info.soft_ms = b.soft_ms;
        info.hard_ms = b.hard_ms;
    } else if (wtime >= 0 || btime >= 0) {
        info.check_time = true;
        info.use_time_management = true;
        const long long remaining = (pos.side_to_move == WHITE) ? wtime : btime;
        const long long inc       = (pos.side_to_move == WHITE) ? winc  : binc;
        const TimeBudget b = compute_time_budget(remaining, inc, movestogo, /*movetime=*/-1);
        info.soft_ms = b.soft_ms;
        info.hard_ms = b.hard_ms;
    } else if (infinite) {
        maxDepth = 6;   // see header comment: no async `stop` in single-threaded Phase 2
    } else {
        maxDepth = 6;   // bare `go`: pre-Step-9 default
    }

    const SearchResult r = search_id(pos, maxDepth, info, /*printInfo=*/true);
    std::cout << "bestmove " << (r.best != MOVE_NONE ? move_to_uci(r.best) : "0000") << std::endl;
}

// "d": piece grid (rank 8 on top, a-file left) + FEN, to eyeball a position.
void print_board(const Position& pos) {
    static const char LETTER[PIECE_TYPE_NB] = {'.', 'P', 'N', 'B', 'R', 'Q', 'K'};
    std::cout << '\n';
    for (int r = 7; r >= 0; --r) {
        std::cout << (r + 1) << "  ";
        for (int f = 0; f < 8; ++f) {
            const Square s = make_square(static_cast<File>(f), static_cast<Rank>(r));
            const PieceType pt = piece_type_on(pos, s);
            char c = '.';
            if (pt != NO_PIECE_TYPE)
                c = test_bit(pos.byColor[WHITE], s)
                        ? LETTER[pt]
                        : static_cast<char>(LETTER[pt] + 32);  // +32: to lowercase (black)
            std::cout << c << ' ';
        }
        std::cout << '\n';
    }
    std::cout << "\n   a b c d e f g h\n";
}

// Read one command per line and dispatch on the first token.
void uci_loop() {
    Position pos;
    parse_fen(pos, START_FEN);   // a sensible default before any "position"

    // Step 8: pre-root game history (Zobrist keys of positions actually played).
    // Kept in sync with `pos`: reset to the current position on a fresh base
    // (startup / ucinewgame), rebuilt by `position`, and read by `go`.
    std::vector<std::uint64_t> gameKeys{ pos.hash };

    TranspositionTable tt;
    tt.resize(16);   // default Hash size (Step 6); real search through `go` is Step 7

    bool optQsearch = true;   // Step 11: `Qsearch` UCI option (default = normal search)

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "uci") {
            uci_identify();
        } else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (token == "ucinewgame") {
            parse_fen(pos, START_FEN);
            gameKeys.assign(1, pos.hash);
            tt.clear();
        } else if (token == "setoption") {
            // "setoption name Hash value <MB>": (re)size the transposition table.
            std::string w, name, value;
            iss >> w >> name >> w >> value;   // name <Hash> value <MB>
            if (name == "Hash") {
                try {
                    const int mb = std::stoi(value);
                    if (mb >= 1) tt.resize(static_cast<std::size_t>(mb));
                } catch (...) { /* ignore malformed value */ }
            } else if (name == "Qsearch") {
                optQsearch = (value == "true" || value == "1");
            }
        } else if (token == "position") {
            uci_position(pos, iss, gameKeys);
        } else if (token == "go") {
            uci_go(pos, tt, iss, gameKeys, optQsearch);
        } else if (token == "bench") {
            // Step 10: deterministic node signature (fixed positions/depth/TT).
            run_bench(/*verbose=*/true);
        } else if (token == "d") {
            print_board(pos);
            std::cout << "FEN: " << fen_string(pos) << std::endl;
        } else if (token == "quit") {
            break;
        }
        // "stop" and any unknown command are ignored (there is no search to stop).
    }
}

} // namespace roj

int main() {
    // Attack tables, sliding-piece magics and Zobrist keys are computed once,
    // before any command is handled.
    roj::init_attack_tables();
    roj::init_magics();
    roj::init_zobrist();

    roj::uci_loop();
    return 0;
}
