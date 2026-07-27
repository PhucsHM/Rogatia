// Rogatia chess engine -- perft.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_PERFT_H
#define ROGATIA_PERFT_H

#include <cstdint>
#include <iosfwd>
#include <string>

#include "position.h"
#include "types.h"

namespace rogatia {

// Long algebraic (UCI) notation, e.g. "e2e4", "e7e8q".
std::string move_to_uci(Move m);

// Leaf-node count at `depth`.  Bulk counted: at depth 1 the legal moves are
// counted without recursing.
std::uint64_t perft(Position& pos, int depth);

// Same, but prints the node count under each root move.  This is the tool for
// bisecting a mismatch: diff its output against a reference engine, descend
// into the move that disagrees, repeat.
std::uint64_t perft_divide(Position& pos, int depth, std::ostream& os);
std::uint64_t perft_divide(Position& pos, int depth);

}  // namespace rogatia

#endif  // ROGATIA_PERFT_H
