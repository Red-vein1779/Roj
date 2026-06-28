// Roj chess engine — FEN parsing and generation.

#include "fen.h"
#include "bitboard.h"

#include <cctype>
#include <sstream>
#include <string>

namespace roj {

namespace {

// FEN piece letter -> (colour, type). Uppercase = white, lowercase = black.
bool parse_piece_char(char ch, Color& c, PieceType& pt) {
    switch (ch) {
        case 'P': c = WHITE; pt = PAWN;   return true;
        case 'N': c = WHITE; pt = KNIGHT; return true;
        case 'B': c = WHITE; pt = BISHOP; return true;
        case 'R': c = WHITE; pt = ROOK;   return true;
        case 'Q': c = WHITE; pt = QUEEN;  return true;
        case 'K': c = WHITE; pt = KING;   return true;
        case 'p': c = BLACK; pt = PAWN;   return true;
        case 'n': c = BLACK; pt = KNIGHT; return true;
        case 'b': c = BLACK; pt = BISHOP; return true;
        case 'r': c = BLACK; pt = ROOK;   return true;
        case 'q': c = BLACK; pt = QUEEN;  return true;
        case 'k': c = BLACK; pt = KING;   return true;
        default:  return false;
    }
}

char piece_char(Color c, PieceType pt) {
    static const char letters[PIECE_TYPE_NB] = {'?', 'P', 'N', 'B', 'R', 'Q', 'K'};
    const char ch = letters[pt];
    return (c == WHITE)
               ? ch
               : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

} // namespace

bool parse_fen(Position& pos, const std::string& fen) {
    pos.clear_board();

    std::istringstream ss(fen);
    std::string placement, side, castling, ep;
    int halfmove = 0, fullmove = 1;
    if (!(ss >> placement >> side >> castling >> ep >> halfmove >> fullmove))
        return false;

    // 1) Piece placement: ranks 8 down to 1, files a to h within each rank.
    int rank = 7, file = 0;
    for (const char ch : placement) {
        if (ch == '/') {
            rank--;
            file = 0;
        } else if (std::isdigit(static_cast<unsigned char>(ch))) {
            file += ch - '0';
        } else {
            Color c;
            PieceType pt;
            if (!parse_piece_char(ch, c, pt)) return false;
            if (file < 0 || file > 7 || rank < 0 || rank > 7) return false;
            set_piece(pos, c, pt,
                      make_square(static_cast<File>(file), static_cast<Rank>(rank)));
            file++;
        }
    }

    // 2) Side to move.
    pos.side_to_move = (side == "b") ? BLACK : WHITE;

    // 3) Castling rights.
    CastlingRights cr = NO_CASTLING;
    if (castling != "-") {
        for (const char ch : castling) {
            switch (ch) {
                case 'K': cr = static_cast<CastlingRights>(cr | WHITE_OO);  break;
                case 'Q': cr = static_cast<CastlingRights>(cr | WHITE_OOO); break;
                case 'k': cr = static_cast<CastlingRights>(cr | BLACK_OO);  break;
                case 'q': cr = static_cast<CastlingRights>(cr | BLACK_OOO); break;
                default:  return false;
            }
        }
    }
    pos.castling_rights = cr;

    // 4) En-passant target square.
    if (ep != "-") {
        if (ep.size() < 2) return false;
        const int f = ep[0] - 'a';
        const int r = ep[1] - '1';
        if (f < 0 || f > 7 || r < 0 || r > 7) return false;
        pos.ep_square = make_square(static_cast<File>(f), static_cast<Rank>(r));
    } else {
        pos.ep_square = SQ_NONE;
    }

    // 5) Clocks.
    pos.halfmove_clock  = halfmove;
    pos.fullmove_number = fullmove;

    // Hash is recomputed from scratch once the whole board is in place (doing it
    // incrementally during parsing would be needless and error-prone).
    pos.hash = compute_hash_from_scratch(pos);
    return true;
}

std::string fen_string(const Position& pos) {
    std::string s;

    // Piece placement.
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Square sq =
                make_square(static_cast<File>(file), static_cast<Rank>(rank));

            Color c = WHITE;
            PieceType pt = NO_PIECE_TYPE;
            for (int cc = 0; cc < COLOR_NB; ++cc)
                for (int p = PAWN; p <= KING; ++p)
                    if (test_bit(pos.pieces[cc][p], sq)) {
                        c = static_cast<Color>(cc);
                        pt = static_cast<PieceType>(p);
                    }

            if (pt == NO_PIECE_TYPE) {
                empty++;
            } else {
                if (empty) {
                    s += static_cast<char>('0' + empty);
                    empty = 0;
                }
                s += piece_char(c, pt);
            }
        }
        if (empty) s += static_cast<char>('0' + empty);
        if (rank > 0) s += '/';
    }

    // Side to move.
    s += ' ';
    s += (pos.side_to_move == WHITE) ? 'w' : 'b';

    // Castling rights.
    s += ' ';
    if (pos.castling_rights == NO_CASTLING) {
        s += '-';
    } else {
        if (pos.castling_rights & WHITE_OO)  s += 'K';
        if (pos.castling_rights & WHITE_OOO) s += 'Q';
        if (pos.castling_rights & BLACK_OO)  s += 'k';
        if (pos.castling_rights & BLACK_OOO) s += 'q';
    }

    // En-passant target square.
    s += ' ';
    if (pos.ep_square == SQ_NONE) {
        s += '-';
    } else {
        s += static_cast<char>('a' + file_of(pos.ep_square));
        s += static_cast<char>('1' + rank_of(pos.ep_square));
    }

    // Clocks.
    s += ' ';
    s += std::to_string(pos.halfmove_clock);
    s += ' ';
    s += std::to_string(pos.fullmove_number);

    return s;
}

} // namespace roj
