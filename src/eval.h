// Rogatia chess engine -- evaluation.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_EVAL_H
#define ROGATIA_EVAL_H

#include "position.h"
#include "types.h"

namespace rogatia {

// Score scale: one pawn is roughly 100.  Everything here is integer -- a float
// anywhere on this path would make the bench node count depend on -march.
constexpr Score VALUE_DRAW     = 0;
constexpr Score VALUE_MATE     = 32000;
constexpr Score VALUE_INFINITE = 32001;
constexpr Score VALUE_NONE     = 32002;

// Anything at least this large is a forced mate rather than a positional score.
constexpr Score VALUE_MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY;

constexpr Score mate_in(int ply)  { return VALUE_MATE - ply; }
constexpr Score mated_in(int ply) { return -VALUE_MATE + ply; }

constexpr bool is_mate_score(Score s) {
    return s >= VALUE_MATE_IN_MAX_PLY || s <= -VALUE_MATE_IN_MAX_PLY;
}

// Rough material values, used by SEE and move ordering.  Deliberately round
// numbers rather than the tapered ones below: SEE wants a stable ranking, not
// an accurate score.
constexpr Score PieceValue[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 0};

// Side-to-move relative.  Tapered piece-square tables only.
// ponytail: no mobility, king safety, pawn structure or any other hand-crafted
// term -- Phase 6 replaces this file with NNUE, so anything built here is work
// that gets deleted.  Upgrade path is eval.cpp -> nnue.cpp, same signature.
Score evaluate(const Position& pos);

}  // namespace rogatia

#endif  // ROGATIA_EVAL_H
