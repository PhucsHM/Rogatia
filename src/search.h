// Rogatia chess engine -- search.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_SEARCH_H
#define ROGATIA_SEARCH_H

#include <cstdint>
#include <vector>

#include "eval.h"
#include "position.h"
#include "types.h"

namespace rogatia::search {

// Everything a `go` command can ask for.  Zero means "not specified".
struct Limits {
    int           time[COLOR_NB] = {0, 0};
    int           inc[COLOR_NB]  = {0, 0};
    int           movestogo      = 0;
    int           depth          = 0;
    int           movetime       = 0;
    std::uint64_t nodes          = 0;
    bool          infinite       = false;
    int           moveOverhead   = 10;
};

// Forget everything learned about the previous game: TT, killers, history.
void clear();

// Runs iterative deepening to completion, printing `info` lines as it goes and
// a single `bestmove` at the end.  Blocking -- UCI runs it on its own thread.
// Clear the stop flag.  Call this on the UCI thread BEFORE starting the search
// thread, never inside go().  Clearing it inside go() loses a `stop` that
// arrives between the spawn and the new thread reaching the store -- and a lost
// stop makes `go infinite` run to depth 244 while the UCI thread blocks on
// join(), which is a hang.
void prepare();

void go(Position& pos, const Limits& limits);

// Asks the running search to abort at its next check.  Safe from any thread.
void stop();

// What a completed search found, for bench and for tests.
struct Result {
    std::uint64_t     nodes = 0;
    Move              best  = MOVE_NONE;
    Score             score = VALUE_NONE;
    std::vector<Move> pv;
};

// Fixed depth, no output, no time checks, exact node count.
Result search_fixed_depth(Position& pos, int depth);

// Soft node limit, no output, no time checks.  What datagen calls per move:
// the node budget is the knob that trades label quality against throughput.
Result search_fixed_nodes(Position& pos, std::uint64_t nodes);

// Quiescence score of `pos` through a full window.  Datagen compares it with
// the static eval to decide whether a position is quiet enough to label.
Score qsearch_eval(Position& pos);

// True when the side to move ends up at least `threshold` ahead after the
// capture sequence on the destination square plays itself out.
bool see_ge(const Position& pos, Move m, int threshold);

}  // namespace rogatia::search

#endif  // ROGATIA_SEARCH_H
