// Roj chess engine — Phase 1, step 7 verification: move encode/decode round-trip.
//
// Every logical move kind is encoded then decoded; from / to / flag (and the
// promoted piece, for promotions) must come back identical. Plus the NULL_MOVE
// sentinel. Move is a header-only encoding, so this test needs no .cpp.
//
// Build:
//   g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic tests/test_move.cpp -o test_move

#include "../src/types.h"

#include <iostream>
#include <string>

using namespace roj;

static int g_failures = 0;

static std::string sqname(Square s) {
    if (s == SQ_NONE) return "--";
    std::string r;
    r += static_cast<char>('a' + file_of(s));
    r += static_cast<char>('1' + rank_of(s));
    return r;
}

static const char* flagname(MoveType t) {
    switch (t) {
        case NORMAL:           return "NORMAL";
        case DOUBLE_PUSH:      return "DOUBLE_PUSH";
        case CASTLING:         return "CASTLING";
        case EN_PASSANT:       return "EN_PASSANT";
        case PROMO_KNIGHT:     return "PROMO_KNIGHT";
        case PROMO_BISHOP:     return "PROMO_BISHOP";
        case PROMO_ROOK:       return "PROMO_ROOK";
        case PROMO_QUEEN:      return "PROMO_QUEEN";
        case PROMO_KNIGHT_CAP: return "PROMO_KNIGHT_CAP";
        case PROMO_BISHOP_CAP: return "PROMO_BISHOP_CAP";
        case PROMO_ROOK_CAP:   return "PROMO_ROOK_CAP";
        case PROMO_QUEEN_CAP:  return "PROMO_QUEEN_CAP";
    }
    return "?";
}

static const char* piecechar(PieceType pt) {
    switch (pt) {
        case KNIGHT: return "N";
        case BISHOP: return "B";
        case ROOK:   return "R";
        case QUEEN:  return "Q";
        default:     return "?";
    }
}

static void roundtrip(const char* label, Square from, Square to, MoveType flag) {
    const Move m       = make_move(from, to, flag);
    const Square df    = from_sq(m);
    const Square dt    = to_sq(m);
    const MoveType dfl = move_type(m);
    const bool ok      = (df == from) && (dt == to) && (dfl == flag);
    if (!ok) ++g_failures;

    std::cout << (ok ? "[PASS] " : "[FAIL] ")
              << label << ": " << sqname(from) << sqname(to) << ' ' << flagname(flag)
              << "  enc=0x" << std::hex << m << std::dec
              << "  ->  " << sqname(df) << sqname(dt) << ' ' << flagname(dfl);
    if (is_promotion(m))
        std::cout << " (=" << piecechar(promotion_type(m)) << ')';
    std::cout << '\n';
}

int main() {
    std::cout << "=== move round-trip ===\n";
    roundtrip("normal quiet    ", SQ_E2, SQ_E4, NORMAL);
    roundtrip("normal capture  ", SQ_D4, SQ_E5, NORMAL);
    roundtrip("double push     ", SQ_E2, SQ_E4, DOUBLE_PUSH);
    roundtrip("castle O-O white", SQ_E1, SQ_G1, CASTLING);
    roundtrip("en passant      ", SQ_D5, SQ_E6, EN_PASSANT);

    roundtrip("promo Q         ", SQ_E7, SQ_E8, promo_flag(QUEEN,  false));
    roundtrip("promo R         ", SQ_E7, SQ_E8, promo_flag(ROOK,   false));
    roundtrip("promo B         ", SQ_E7, SQ_E8, promo_flag(BISHOP, false));
    roundtrip("promo N         ", SQ_E7, SQ_E8, promo_flag(KNIGHT, false));

    roundtrip("promo Q capture ", SQ_E7, SQ_F8, promo_flag(QUEEN,  true));
    roundtrip("promo R capture ", SQ_E7, SQ_F8, promo_flag(ROOK,   true));
    roundtrip("promo B capture ", SQ_E7, SQ_F8, promo_flag(BISHOP, true));
    roundtrip("promo N capture ", SQ_E7, SQ_F8, promo_flag(KNIGHT, true));

    std::cout << "\n=== sentinels ===\n";
    const bool null_ok = (from_sq(NULL_MOVE) == to_sq(NULL_MOVE))
                         && (move_type(NULL_MOVE) == NORMAL)
                         && (NULL_MOVE != MOVE_NONE);
    if (!null_ok) ++g_failures;
    std::cout << (null_ok ? "[PASS] " : "[FAIL] ")
              << "NULL_MOVE: from==to (" << sqname(from_sq(NULL_MOVE))
              << "), no flags, distinct from MOVE_NONE\n";

    std::cout << '\n'
              << (g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << " (failures: " << g_failures << ")\n";
    return g_failures == 0 ? 0 : 1;
}
