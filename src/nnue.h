// Rogatia chess engine -- NNUE evaluation.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_NNUE_H
#define ROGATIA_NNUE_H

#include <cstdint>

#include "position.h"
#include "types.h"

namespace rogatia::nnue {

// (768 -> 256)x2 -> 1.  768 inputs = 2 colours x 6 piece types x 64 squares,
// seen twice: once from each side's point of view.
constexpr int INPUTS = 768;
constexpr int HIDDEN = 256;

// Quantisation, fixed by how the net was trained -- see trainer/rogatia.rs.
constexpr int QA    = 255;
constexpr int QB    = 64;
constexpr int SCALE = 400;

// One side's hidden layer before activation.  Kept per perspective: the same
// position produces two different accumulators, and the output layer reads the
// side to move's first.
struct alignas(64) Accumulator {
    std::int16_t v[COLOR_NB][HIDDEN];
};

// Loads the network.  Returns false if the file is missing or the wrong size,
// in which case the caller must fall back to the hand-crafted evaluation.
bool load(const char* path);

// True once a network is loaded and nnue::evaluate may be called.
//
// Inline over an extern flag, not an out-of-line function.  This is read before
// EVERY make_move (search.cpp) and the Makefile falls back to no LTO when the
// toolchain lacks it -- in that build an out-of-line definition is a real call
// on the hottest path in the engine, for a byte load.
extern bool NetLoaded;
inline bool loaded() { return NetLoaded; }

// Builds an accumulator from scratch.  O(pieces * HIDDEN).
void refresh(const Position& pos, Accumulator& acc);

// Incrementally derives the accumulator after `m` from the one before it.
// `pos` must be the position *before* the move.  O(HIDDEN) per changed piece
// instead of a full refresh -- this is the whole point of the accumulator.
void apply_move(const Position& pos, Move m, const Accumulator& src, Accumulator& dst);

// Side-to-move relative score in centipawns.
Score evaluate(const Position& pos, const Accumulator& acc);

// Convenience: refresh and evaluate in one call.  Correct but slow -- the
// search is expected to keep an accumulator instead.
Score evaluate(const Position& pos);

}  // namespace rogatia::nnue

#endif  // ROGATIA_NNUE_H
