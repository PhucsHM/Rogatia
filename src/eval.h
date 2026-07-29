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

// Tablebase scores sit in the band just below mate scores.  A table hit is
// certain, so it must outrank every positional score; but a mate the search
// actually found is shorter and more useful, so it must outrank the table.
// Deliberately below VALUE_MATE_IN_MAX_PLY, which keeps is_mate_score() false
// for them -- a WDL probe knows the result, not the distance, so reporting
// "mate in N" off one would be inventing a number.
constexpr Score VALUE_TB                = VALUE_MATE_IN_MAX_PLY - 1;
constexpr Score VALUE_TB_WIN_IN_MAX_PLY = VALUE_TB - MAX_PLY;

constexpr Score tb_win_in(int ply)  { return VALUE_TB - ply; }
constexpr Score tb_loss_in(int ply) { return -VALUE_TB + ply; }

// True for a mate score OR a tablebase score, because the tablebase band sits
// below the mate band and this test starts at the lower of the two.
//
// **Use this, not is_mate_score(), to guard anything that reasons about a
// score as a normal centipawn value.**  A tablebase score is deliberately
// invisible to is_mate_score(), so a guard written with that test alone lets
// the whole tablebase band through.  Every pruning margin, the aspiration
// window and the singular test need the union.  is_mate_score() is correct in
// exactly one place: reporting "mate in N" over UCI, where a table hit knows
// the result but not the distance.
constexpr bool is_decisive(Score s) {
    return s >= VALUE_TB_WIN_IN_MAX_PLY || s <= -VALUE_TB_WIN_IN_MAX_PLY;
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
