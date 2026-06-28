// Roj chess engine — FEN parsing and generation.
//
// FEN (Forsyth-Edwards Notation) is the standard text encoding of a position.
// parse_fen fills a Position from a FEN string; fen_string serialises one back.
// They are inverses: a valid FEN parsed and re-serialised must come out byte for
// byte identical (the round-trip test in step 6).

#ifndef ROJ_FEN_H
#define ROJ_FEN_H

#include "position.h"

#include <string>

namespace roj {

// Fill `pos` completely from `fen` (pieces, side, castling, ep, clocks) and set
// its hash from scratch. Returns false on malformed input.
bool parse_fen(Position& pos, const std::string& fen);

// Serialise `pos` back to a FEN string.
std::string fen_string(const Position& pos);

} // namespace roj

#endif // ROJ_FEN_H
