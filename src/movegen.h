// Rogatia chess engine -- pseudo-legal move generation.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_MOVEGEN_H
#define ROGATIA_MOVEGEN_H

#include "position.h"
#include "types.h"

namespace rogatia {

enum GenType { CAPTURES, QUIETS, ALL };

// CAPTURES and QUIETS partition ALL exactly: a move that takes an enemy piece
// (en passant included) is a capture, everything else -- quiet promotions and
// castling included -- is quiet.
//
// Output is PSEUDO-legal.  Run every move through Position::is_legal before
// playing it.
template<GenType T>
Move* generate(const Position& pos, Move* moveList);

template<GenType T>
class MoveList {
public:
    explicit MoveList(const Position& pos) : last_(generate<T>(pos, moves_)) {}

    const Move* begin() const { return moves_; }
    const Move* end() const { return last_; }
    std::size_t size() const { return std::size_t(last_ - moves_); }
    bool        contains(Move m) const {
        for (const Move* p = moves_; p != last_; ++p)
            if (*p == m)
                return true;
        return false;
    }

private:
    Move  moves_[MAX_MOVES];
    Move* last_;
};

}  // namespace rogatia

#endif  // ROGATIA_MOVEGEN_H
