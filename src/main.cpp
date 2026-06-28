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

#include <iostream>
#include <sstream>
#include <string>

namespace roj {

void uci_identify() {
    std::cout << "id name " << ENGINE_NAME << ' ' << ENGINE_VERSION << '\n'
              << "id author " << ENGINE_AUTHOR << '\n'
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
void uci_position(Position& pos, std::istringstream& iss) {
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

    if (iss >> token && token == "moves") {
        std::string mv;
        while (iss >> mv) {
            MoveList list;
            generate_legal_moves(pos, list);
            bool applied = false;
            for (int i = 0; i < list.count; ++i)
                if (uci_matches(list.moves[i], mv)) {
                    make_move(pos, list.moves[i]);
                    applied = true;
                    break;
                }
            if (!applied) break;   // unknown/illegal move: stop replaying
        }
    }
}

// "go": play the FIRST legal move (deterministic; real search is Phase 2). Any
// arguments are ignored. No legal moves (mate/stalemate) -> "bestmove 0000".
void uci_go(Position& pos) {
    MoveList list;
    generate_legal_moves(pos, list);
    if (list.count == 0) {
        std::cout << "bestmove 0000" << std::endl;
        return;
    }
    std::cout << "bestmove " << move_to_uci(list.moves[0]) << std::endl;
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
        } else if (token == "position") {
            uci_position(pos, iss);
        } else if (token == "go") {
            uci_go(pos);
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
