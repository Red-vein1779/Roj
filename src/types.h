// Roj chess engine — core type definitions.
//
// This header is the foundation that every other Phase 1 component builds on:
// bitboards, attack tables, magic bitboards, Zobrist hashing, FEN parsing,
// move encoding, make/unmake and move generation. Keeping these definitions in
// one small, dependency-free header means a single source of truth for how the
// board, pieces and moves are represented across the whole engine.
//
// All code is original. Platform-independent C++17 only — no OS-specific APIs.

#ifndef ROJ_TYPES_H
#define ROJ_TYPES_H

#include <cstdint>

namespace roj {

// ----------------------------------------------------------------------------
// Bitboard
// ----------------------------------------------------------------------------
// A bitboard is a 64-bit integer where each bit maps to one square of the
// board (bit 0 = a1, bit 63 = h8). This is the central data structure of the
// engine: piece sets, attack masks and move targets are all expressed as
// bitboards so that set operations become single CPU instructions.
using Bitboard = std::uint64_t;

constexpr Bitboard EMPTY_BB = 0ULL;
constexpr Bitboard FULL_BB  = ~0ULL;

// ----------------------------------------------------------------------------
// Colors
// ----------------------------------------------------------------------------
enum Color : int {
    WHITE,
    BLACK,
    COLOR_NB = 2
};

// Side to move flips between WHITE and BLACK; defining the negation here keeps
// the intent explicit wherever we switch sides.
constexpr Color operator~(Color c) {
    return static_cast<Color>(c ^ BLACK);
}

// ----------------------------------------------------------------------------
// Piece types and pieces
// ----------------------------------------------------------------------------
// PieceType is color-agnostic; Piece carries the color as well. Encoding a
// Piece as (color << 3) | type gives a compact 0..11 index that is convenient
// for Zobrist tables and piece-square tables later on.
enum PieceType : int {
    NO_PIECE_TYPE,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    PIECE_TYPE_NB = 7
};

enum Piece : int {
    NO_PIECE,
    W_PAWN = PAWN,      W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = PAWN + 8,  B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};

constexpr Piece make_piece(Color c, PieceType pt) {
    return static_cast<Piece>((c << 3) | pt);
}

constexpr PieceType type_of(Piece pc) {
    return static_cast<PieceType>(pc & 7);
}

constexpr Color color_of(Piece pc) {
    return static_cast<Color>(pc >> 3);
}

// ----------------------------------------------------------------------------
// Squares, files and ranks
// ----------------------------------------------------------------------------
// Squares run a1=0 .. h1=7, a2=8 .. h8=63 (little-endian rank-file mapping).
// This layout makes file/rank extraction and bitboard shifts straightforward.
enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE,
    SQUARE_NB = 64
};

enum File : int {
    FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H,
    FILE_NB = 8
};

enum Rank : int {
    RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8,
    RANK_NB = 8
};

constexpr File file_of(Square s) { return static_cast<File>(s & 7); }
constexpr Rank rank_of(Square s) { return static_cast<Rank>(s >> 3); }

constexpr Square make_square(File f, Rank r) {
    return static_cast<Square>((r << 3) | f);
}

// Turn a square index into its single-bit bitboard.
constexpr Bitboard square_bb(Square s) {
    return 1ULL << s;
}

// ----------------------------------------------------------------------------
// Castling rights
// ----------------------------------------------------------------------------
// Stored as a 4-bit mask so the full castling state fits in one integer and can
// be folded into the Zobrist key cheaply.
enum CastlingRights : int {
    NO_CASTLING  = 0,
    WHITE_OO     = 1,
    WHITE_OOO    = 2,
    BLACK_OO     = 4,
    BLACK_OOO    = 8,
    ANY_CASTLING = WHITE_OO | WHITE_OOO | BLACK_OO | BLACK_OOO
};

// Number of distinct 4-bit castling-right combinations (0..15), e.g. for sizing
// the Zobrist castling-key table.
constexpr int CASTLING_RIGHTS_NB = 16;

// ----------------------------------------------------------------------------
// Move encoding
// ----------------------------------------------------------------------------
// A move is packed into 16 bits, which keeps move lists and the transposition
// table compact:
//
//   bits  0- 5 : from square (0..63)
//   bits  6-11 : to square   (0..63)
//   bits 12-15 : 4-bit flag  (see MoveType)
//
// The 4-bit flag names every special kind of move and, for promotions, encodes
// both the promoted piece AND whether the promotion is also a capture:
//
//   flag bit 3 (value 8) set => promotion
//   flag bit 2 (value 4) set => capture       (only distinguished for promotions)
//   flag bits 1-0            => promoted piece (0=N,1=B,2=R,3=Q) when promoting
//
// A normal quiet move and a normal capture share the NORMAL flag; for
// non-promotions, whether a move captures is read from the board, not the move.
// Castling is the king's two-square move (e1g1 / e1c1 / ...) with the CASTLING
// flag; the side is implied by the destination file.
enum MoveType : int {
    NORMAL           = 0,
    DOUBLE_PUSH      = 1,   // make/unmake sets the en-passant square
    CASTLING         = 2,   // king's two-square move; side implied by `to`
    EN_PASSANT       = 3,   // make/unmake clears the captured pawn's square
    PROMO_KNIGHT     = 8,
    PROMO_BISHOP     = 9,
    PROMO_ROOK       = 10,
    PROMO_QUEEN      = 11,
    PROMO_KNIGHT_CAP = 12,
    PROMO_BISHOP_CAP = 13,
    PROMO_ROOK_CAP   = 14,
    PROMO_QUEEN_CAP  = 15
};

// A plain 16-bit integer (not a class wrapper) so moves stay trivially copyable
// and cheap to pass around in hot loops.
using Move = std::uint16_t;

// Two reserved sentinels, both with from == to (which no legal move ever has).
// SQ_NONE (64) does not fit the 6-bit square fields, so the canonical
// "from equals to" trick marks a non-move / null move instead.
constexpr Move MOVE_NONE = 0;   // a1->a1 : "no move"
constexpr Move NULL_MOVE = 65;  // b1->b1 : the null move (side to move passes)

// Build a move. The default flag covers normal quiet and capture moves; pass a
// specific flag for special moves (DOUBLE_PUSH, CASTLING, EN_PASSANT, or one of
// the promotion flags).
constexpr Move make_move(Square from, Square to, MoveType flag = NORMAL) {
    return static_cast<Move>((flag << 12) | (to << 6) | from);
}

// Convenience: the promotion flag for a promoted piece, capturing or not.
constexpr MoveType promo_flag(PieceType promoted, bool capture) {
    return static_cast<MoveType>(8 | (capture ? 4 : 0) | (promoted - KNIGHT));
}

constexpr Square from_sq(Move m) { return static_cast<Square>(m & 0x3F); }
constexpr Square to_sq(Move m)   { return static_cast<Square>((m >> 6) & 0x3F); }
constexpr MoveType move_type(Move m) { return static_cast<MoveType>((m >> 12) & 0xF); }

// True for any of the eight promotion flags.
constexpr bool is_promotion(Move m) { return ((m >> 12) & 0x8) != 0; }

// The promoted piece. Valid only when is_promotion(m).
constexpr PieceType promotion_type(Move m) {
    return static_cast<PieceType>(KNIGHT + ((m >> 12) & 0x3));
}

// ----------------------------------------------------------------------------
// Engine identity and standard positions
// ----------------------------------------------------------------------------
constexpr const char* ENGINE_NAME    = "Roj";
constexpr const char* ENGINE_AUTHOR  = "Roj Project";
constexpr const char* ENGINE_VERSION = "0.1.0-dev";

// The standard chess starting position in Forsyth-Edwards Notation, used by the
// UCI "position startpos" command and by the perft test suite.
constexpr const char* START_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

} // namespace roj

#endif // ROJ_TYPES_H
