// Roj chess engine — program entry point and UCI command loop.
//
// Phase 1 scaffold: this is a *basic* UCI loop. It answers the protocol
// handshake (uci / isready / ucinewgame / quit) so that any chess GUI or
// Lichess bot bridge can connect to the engine. The "position" and "go"
// handlers are intentionally stubs marked with TODO — board representation,
// move generation and search arrive as those Phase 1 / Phase 2 components are
// implemented. Nothing here pretends to play chess yet.
//
// Platform-independent C++17 only — no OS-specific APIs.

#include "types.h"
#include "attacks.h"
#include "magic.h"
#include "zobrist.h"

#include <iostream>
#include <sstream>
#include <string>

namespace roj {

// Respond to the "uci" handshake: identify the engine and report that all
// option parsing is finished. Real engine options will be added here later.
void uci_identify() {
    std::cout << "id name " << ENGINE_NAME << ' ' << ENGINE_VERSION << '\n'
              << "id author " << ENGINE_AUTHOR << '\n'
              << "uciok" << std::endl;
}

// TODO (Phase 1): set up the board from "startpos" or a FEN, then apply the
// optional list of moves. Requires the Position type, FEN parsing and
// make/unmake, which are not built yet.
void uci_position(std::istringstream& /*args*/) {
    // Intentionally empty until the board representation exists.
}

// TODO (Phase 2): launch the search and report the best move. For now we reply
// with the UCI "none" move so a connected GUI does not hang waiting on "go".
void uci_go(std::istringstream& /*args*/) {
    std::cout << "bestmove 0000" << std::endl;
}

// The main UCI loop. It reads one command per line from standard input and
// dispatches on the first token. Unknown commands are ignored, as the UCI
// specification requires.
void uci_loop() {
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
            // TODO (Phase 2): clear the transposition table and search state.
        } else if (token == "position") {
            uci_position(iss);
        } else if (token == "go") {
            uci_go(iss);
        } else if (token == "quit" || token == "stop") {
            break;
        }
        // Any other input is silently ignored per the UCI protocol.
    }
}

} // namespace roj

int main() {
    // Unbuffered, line-by-line communication is what UCI GUIs expect; flushing
    // after each response (handled at the call sites via std::endl) keeps the
    // exchange responsive.
    // Attack tables, sliding-piece magics and Zobrist keys are computed once,
    // before any command is handled.
    roj::init_attack_tables();
    roj::init_magics();
    roj::init_zobrist();

    roj::uci_loop();
    return 0;
}
