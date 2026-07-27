// Rogatia chess engine -- bitboard geometry tables.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bitboard.h"

#include <algorithm>

namespace rogatia {

namespace detail {
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];
std::uint8_t SquareDistance[SQUARE_NB][SQUARE_NB];
}  // namespace detail

namespace {

constexpr Direction AllDirections[8] = {
    NORTH, NORTH_EAST, EAST, SOUTH_EAST, SOUTH, SOUTH_WEST, WEST, NORTH_WEST
};

// One step from s in direction d, SQ_NONE if it falls off the board.
// A legal single step never changes the file by more than one.
Square step(Square s, Direction d) {
    const int t = int(s) + int(d);
    if (t < 0 || t > 63)
        return SQ_NONE;
    const Square to = Square(t);
    if (file_distance(s, to) > 1)
        return SQ_NONE;
    return to;
}

// All squares reachable from s by repeatedly stepping in direction d.
Bitboard ray(Square s, Direction d) {
    Bitboard b = 0;
    for (Square t = step(s, d); t != SQ_NONE; t = step(t, d))
        b |= square_bb(t);
    return b;
}

}  // namespace

Bitboard adjacent_files_bb(File f) {
    return (f > FILE_A ? file_bb(File(f - 1)) : 0) | (f < FILE_H ? file_bb(File(f + 1)) : 0);
}

Bitboard adjacent_files_bb(Square s) { return adjacent_files_bb(file_of(s)); }

void init_bitboards() {
    for (int a = 0; a < SQUARE_NB; ++a)
        for (int b = 0; b < SQUARE_NB; ++b) {
            detail::BetweenBB[a][b] = 0;
            detail::LineBB[a][b]    = 0;
            detail::SquareDistance[a][b] = std::uint8_t(
                std::max(file_distance(Square(a), Square(b)), rank_distance(Square(a), Square(b))));
        }

    for (int i = 0; i < SQUARE_NB; ++i) {
        const Square a = Square(i);

        // BetweenBB: walk out from a, accumulating the squares already passed.
        for (Direction d : AllDirections) {
            Bitboard acc = 0;
            for (Square t = step(a, d); t != SQ_NONE; t = step(t, d)) {
                detail::BetweenBB[a][t] = acc;
                acc |= square_bb(t);
            }
        }

        // LineBB: for each of the four axes, the full two-way line through a.
        for (Direction d : {NORTH, EAST, NORTH_EAST, NORTH_WEST}) {
            const Bitboard fwd  = ray(a, d);
            const Bitboard back = ray(a, -d);
            const Bitboard ln   = fwd | back | square_bb(a);
            Bitboard others = fwd | back;
            while (others) {
                const Square b = pop_lsb(others);
                detail::LineBB[a][b] = ln;
            }
        }
    }
}

std::string square_name(Square s) {
    if (s == SQ_NONE)
        return "-";
    return std::string{char('a' + int(file_of(s))), char('1' + int(rank_of(s)))};
}

std::string pretty(Bitboard b) {
    std::string out = "+---+---+---+---+---+---+---+---+\n";
    for (int r = RANK_8; r >= RANK_1; --r) {
        for (int f = FILE_A; f <= FILE_H; ++f)
            out += (b & square_bb(make_square(File(f), Rank(r)))) ? "| X " : "|   ";
        out += "| ";
        out += char('1' + r);
        out += "\n+---+---+---+---+---+---+---+---+\n";
    }
    out += "  a   b   c   d   e   f   g   h\n";
    return out;
}

}  // namespace rogatia
