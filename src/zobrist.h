// Rogatia chess engine -- Zobrist hashing.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_ZOBRIST_H
#define ROGATIA_ZOBRIST_H

#include "types.h"

namespace rogatia {

// Five independent key sets are generated even though only KEY_MAIN is read
// today.  The other four feed correction history later, and retrofitting extra
// keys through make/unmake after the fact is far more painful than carrying
// them from the start.
enum KeySet : int {
    KEY_MAIN     = 0,  // every piece + side + castling + ep file
    KEY_PAWN     = 1,  // pawns only
    KEY_NONPAWN  = 2,  // everything except pawns, accumulated per colour
    KEY_MAJOR    = 3,  // rooks, queens, kings
    KEY_MINOR    = 4,  // knights, bishops, kings
    KEY_SET_NB   = 5
};

namespace zobrist {

// PIECE-MAJOR, not set-major.  update_keys() reads four or five of these for one
// (piece, square) on every piece event, and make_move fires it 4-6 times.  With
// the set index outermost the five keys sat 8 KB apart -- five cache lines and
// five pages for one piece landing on one square.  Here they are 40 contiguous
// bytes, so the first read brings the rest in with it.
extern Key Psq[PIECE_NB][SQUARE_NB][KEY_SET_NB];
extern Key Castling[CASTLING_RIGHT_NB];
extern Key EnPassant[FILE_NB];
extern Key Side;

}  // namespace zobrist

// Deterministic: fixed seed, fixed generation order.  Hashes must be identical
// across builds and platforms (OpenBench requirement).
void init_zobrist();

}  // namespace rogatia

#endif  // ROGATIA_ZOBRIST_H
