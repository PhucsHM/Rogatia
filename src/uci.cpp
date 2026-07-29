// Rogatia chess engine -- UCI protocol.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "uci.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "bench.h"
#include "datagen.h"
#include "movegen.h"
#include "perft.h"  // move_to_uci
#include "position.h"
#include "search.h"
#include "tbprobe.h"
#include "tt.h"
#include "tunable.h"

namespace rogatia {

namespace {

constexpr const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

constexpr int HASH_DEFAULT = 16;
constexpr int MOVE_OVERHEAD_DEFAULT = 10;

struct Engine {
    Position    pos;
    std::thread searching;

    int hashMb       = HASH_DEFAULT;
    int moveOverhead = MOVE_OVERHEAD_DEFAULT;

    void join() {
        if (searching.joinable())
            searching.join();
    }
};

int parse_int(const std::string& s, int fallback) {
    try {
        return std::stoi(s);
    } catch (...) {
        return fallback;
    }
}

// Never trust the string: a move is whatever generation says it is, matched by
// its text.  That keeps a typo or a hostile GUI from putting a piece on a
// square the rules do not allow.
Move move_from_uci(const Position& pos, const std::string& text) {
    for (Move m : MoveList<ALL>(pos))
        if (pos.is_legal(m) && move_to_uci(m) == text)
            return m;
    return MOVE_NONE;
}

void cmd_position(Engine& e, std::istringstream& is) {
    std::string token, fen;
    is >> token;

    if (token == "startpos") {
        fen = StartFEN;
        is >> token;  // consume "moves" if present
    } else if (token == "fen") {
        while (is >> token && token != "moves")
            fen += token + ' ';
    } else {
        return;
    }

    Position parsed;
    if (!parsed.set(fen)) {
        std::cout << "info string ignoring malformed FEN\n" << std::flush;
        return;
    }

    // Replay the moves rather than jumping to the final FEN: repetition
    // detection reads the history stack, and a position set directly has none.
    e.pos = parsed;
    if (token == "moves")
        while (is >> token) {
            const Move m = move_from_uci(e.pos, token);
            if (m == MOVE_NONE) {
                std::cout << "info string ignoring illegal move " << token << '\n' << std::flush;
                break;
            }
            e.pos.make_move(m);
        }
}

void cmd_go(Engine& e, std::istringstream& is) {
    search::Limits limits;
    limits.moveOverhead = e.moveOverhead;

    // Read each value as a token and parse it separately.  Extracting straight
    // into an int puts the stream in a fail state on the first bad token and
    // every later limit then silently reads as zero -- which is how a typo
    // turns into a search nothing can interrupt.
    bool       malformed = false;
    const auto value     = [&is, &malformed]() -> long long {
        std::string tok;
        if (!(is >> tok)) {
            malformed = true;
            return 0;
        }
        try {
            return std::stoll(tok);
        } catch (...) {
            malformed = true;
            return 0;
        }
    };

    std::string token;
    while (is >> token) {
        if (token == "wtime")          limits.time[WHITE] = int(value());
        else if (token == "btime")     limits.time[BLACK] = int(value());
        else if (token == "winc")      limits.inc[WHITE]  = int(value());
        else if (token == "binc")      limits.inc[BLACK]  = int(value());
        else if (token == "movestogo") limits.movestogo   = int(value());
        else if (token == "depth")     limits.depth       = int(value());
        else if (token == "nodes")     limits.nodes       = std::uint64_t(std::max(value(), 0LL));
        else if (token == "movetime")  limits.movetime    = int(value());
        else if (token == "infinite")  limits.infinite    = true;
    }

    if (malformed || limits.depth < 0) {
        std::cout << "info string ignoring malformed go\n" << std::flush;
        return;
    }

    e.join();
    // Clear the stop flag HERE, on this thread, before the search thread
    // starts.  Doing it inside go() loses a `stop` that lands between the spawn
    // and the new thread reaching the store.
    search::prepare();
    e.searching = std::thread([&e, limits] { search::go(e.pos, limits); });
}

void cmd_setoption(Engine& e, std::istringstream& is) {
    std::string token, name, value;
    is >> token;  // "name"

    while (is >> token && token != "value")
        name += (name.empty() ? "" : " ") + token;
    while (is >> token)
        value += (value.empty() ? "" : " ") + token;

    if (name == "Hash") {
        e.hashMb = std::clamp(parse_int(value, HASH_DEFAULT), 1, 65536);
        TT.resize(std::size_t(e.hashMb));
    } else if (name == "Move Overhead") {
        e.moveOverhead = std::clamp(parse_int(value, MOVE_OVERHEAD_DEFAULT), 0, 5000);
    } else if (name == "SyzygyPath") {
        // Until this arrives TB_LARGEST is zero and the search never probes,
        // which is what keeps `bench` identical on a machine with tablebases
        // and one without.  Never set it before a bench.
        tb_free();
        if (!value.empty() && value != "<empty>" && tb_init(value.c_str()) && TB_LARGEST > 0)
            std::cout << "info string syzygy: " << TB_LARGEST
                      << "-piece tablebases from " << value << '\n' << std::flush;
        else
            std::cout << "info string syzygy: no tablebases at " << value
                      << ", probing stays off\n" << std::flush;
    } else {
        // Every search constant lives in tunable.h so SPSA can drive it without
        // a recompile.  Unknown names still fall through silently, as UCI wants.
        tunable::set_option(name, parse_int(value, 0));
    }
    // "Threads" is accepted and ignored: OpenBench requires the option to
    // exist, and Phase 7 owns SMP.
}

}  // namespace

void uci_loop() {
    Engine e;
    TT.resize(HASH_DEFAULT);
    e.pos.set(StartFEN);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string        token;
        is >> token;

        if (token == "uci") {
            std::cout << "id name Rogatia\n"
                      << "id author PhucsHM\n"
                      << "option name Hash type spin default " << HASH_DEFAULT
                      << " min 1 max 65536\n"
                      << "option name Threads type spin default 1 min 1 max 1\n"
                      << "option name Move Overhead type spin default " << MOVE_OVERHEAD_DEFAULT
                      << " min 0 max 5000\n"
                      << "option name SyzygyPath type string default <empty>\n"
                      << std::flush;
            tunable::print_options();
            std::cout << "uciok\n" << std::flush;
        } else if (token == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (token == "ucinewgame") {
            e.join();
            search::new_game();
            e.pos.set(StartFEN);
        } else if (token == "setoption") {
            e.join();
            cmd_setoption(e, is);
        } else if (token == "position") {
            e.join();
            cmd_position(e, is);
        } else if (token == "go") {
            cmd_go(e, is);
        } else if (token == "stop") {
            search::stop();
            e.join();
        } else if (token == "quit") {
            search::stop();
            e.join();
            break;
        } else if (token == "bench") {
            e.join();
            int depth = bench_depth_default();
            if (is >> token)
                depth = parse_int(token, depth);
            run_bench(depth);
            TT.resize(std::size_t(e.hashMb));  // bench forces its own size back
        } else if (token == "datagen") {
            // datagen <output> <positions> [seed] [nodes]
            e.join();
            datagen::Config cfg;
            if (!(is >> cfg.output)) {
                std::cout << "usage: datagen <output> <positions> [seed] [nodes]\n" << std::flush;
                continue;
            }
            if (is >> token)
                cfg.positions = std::uint64_t(std::max(parse_int(token, 0), 0));
            if (is >> token)
                cfg.seed = std::uint64_t(std::max(parse_int(token, 1), 1));
            if (is >> token)
                cfg.nodes = std::uint64_t(std::max(parse_int(token, 5000), 1));
            datagen::run(cfg);
            TT.resize(std::size_t(e.hashMb));  // datagen forces its own size back
        } else if (token == "d") {
            std::cout << e.pos.to_string() << std::flush;
        } else if (token == "eval") {
            std::cout << "eval " << evaluate(e.pos) << '\n' << std::flush;
        }
    }

    search::stop();
    e.join();
}

}  // namespace rogatia
