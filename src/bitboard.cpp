// Roj chess engine — bitboard debug rendering.
//
// Kept out of the header because it is not performance-critical and pulls in
// <iostream>; the hot primitives stay inline in bitboard.h.

#include "bitboard.h"

#include <iostream>

namespace roj {

void print_bitboard(Bitboard bb) {
    std::cout << '\n';
    for (int r = 7; r >= 0; --r) {          // rank 8 (top) down to rank 1
        std::cout << (r + 1) << "  ";        // left-hand rank label (8..1)
        for (int f = 0; f < 8; ++f) {        // file a (left) to file h (right)
            const Square s = make_square(static_cast<File>(f),
                                         static_cast<Rank>(r));
            std::cout << (test_bit(bb, s) ? '1' : '.') << ' ';
        }
        std::cout << '\n';
    }
    std::cout << "\n   a b c d e f g h\n";
    std::cout << "   bitboard = 0x" << std::hex << std::uppercase << bb
              << std::dec << "\n\n";
}

} // namespace roj
