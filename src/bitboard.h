// Rogatia chess engine -- bitboard primitives.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_BITBOARD_H
#define ROGATIA_BITBOARD_H

#include <bit>
#include <cassert>
#include <string>

#include "types.h"

namespace rogatia {

constexpr Bitboard EMPTY_BB = 0ULL;
constexpr Bitboard ALL_BB   = ~0ULL;

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileBBB = FileABB << 1;
constexpr Bitboard FileCBB = FileABB << 2;
constexpr Bitboard FileDBB = FileABB << 3;
constexpr Bitboard FileEBB = FileABB << 4;
constexpr Bitboard FileFBB = FileABB << 5;
constexpr Bitboard FileGBB = FileABB << 6;
constexpr Bitboard FileHBB = FileABB << 7;

constexpr Bitboard Rank1BB = 0xFFULL;
constexpr Bitboard Rank2BB = Rank1BB << (8 * 1);
constexpr Bitboard Rank3BB = Rank1BB << (8 * 2);
constexpr Bitboard Rank4BB = Rank1BB << (8 * 3);
constexpr Bitboard Rank5BB = Rank1BB << (8 * 4);
constexpr Bitboard Rank6BB = Rank1BB << (8 * 5);
constexpr Bitboard Rank7BB = Rank1BB << (8 * 6);
constexpr Bitboard Rank8BB = Rank1BB << (8 * 7);

constexpr Bitboard square_bb(Square s) { return 1ULL << int(s); }
constexpr Bitboard file_bb(File f)     { return FileABB << int(f); }
constexpr Bitboard file_bb(Square s)   { return file_bb(file_of(s)); }
constexpr Bitboard rank_bb(Rank r)     { return Rank1BB << (8 * int(r)); }
constexpr Bitboard rank_bb(Square s)   { return rank_bb(rank_of(s)); }

// Rank a colour's pawns stand on after a double push has cleared rank 3 / 6.
constexpr Bitboard relative_rank_bb(Color c, Rank r) { return rank_bb(relative_rank(c, r)); }

// ------------------------------------------------------------ bit fiddling --

inline int  popcount(Bitboard b) { return std::popcount(b); }
inline bool more_than_one(Bitboard b) { return (b & (b - 1)) != 0; }

// Index of the least / most significant set bit.  b must be non-zero.
inline Square lsb(Bitboard b) { assert(b); return Square(std::countr_zero(b)); }
inline Square msb(Bitboard b) { assert(b); return Square(63 - std::countl_zero(b)); }

// Pops and returns the least significant set bit.
inline Square pop_lsb(Bitboard& b) {
    assert(b);
    const Square s = lsb(b);
    b &= b - 1;
    return s;
}

// ---------------------------------------------------------------- shifting --

template<Direction D>
constexpr Bitboard shift(Bitboard b) {
    return D == NORTH      ?  b << 8
         : D == SOUTH      ?  b >> 8
         : D == EAST       ? (b & ~FileHBB) << 1
         : D == WEST       ? (b & ~FileABB) >> 1
         : D == NORTH_EAST ? (b & ~FileHBB) << 9
         : D == NORTH_WEST ? (b & ~FileABB) << 7
         : D == SOUTH_EAST ? (b & ~FileHBB) >> 7
         : D == SOUTH_WEST ? (b & ~FileABB) >> 9
         : 0;
}

inline Bitboard shift(Bitboard b, Direction d) {
    switch (d) {
    case NORTH:      return shift<NORTH>(b);
    case SOUTH:      return shift<SOUTH>(b);
    case EAST:       return shift<EAST>(b);
    case WEST:       return shift<WEST>(b);
    case NORTH_EAST: return shift<NORTH_EAST>(b);
    case NORTH_WEST: return shift<NORTH_WEST>(b);
    case SOUTH_EAST: return shift<SOUTH_EAST>(b);
    case SOUTH_WEST: return shift<SOUTH_WEST>(b);
    default:         return 0;
    }
}

// --------------------------------------------------------- geometry tables --

namespace detail {
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];
extern std::uint8_t SquareDistance[SQUARE_NB][SQUARE_NB];
}  // namespace detail

// Squares strictly between a and b along a rank / file / diagonal.
// EXCLUSIVE of both endpoints.  Zero if a and b are not aligned or a == b.
// Check-evasion masks are therefore  between_bb(ksq, checker) | square_bb(checker).
inline Bitboard between_bb(Square a, Square b) { return detail::BetweenBB[a][b]; }

// Every square of the infinite line through a and b (endpoints included).
// Zero if a and b are not aligned or a == b.
inline Bitboard line_bb(Square a, Square b) { return detail::LineBB[a][b]; }

// True when the three squares lie on one rank, file or diagonal.
inline bool aligned(Square a, Square b, Square c) { return line_bb(a, b) & square_bb(c); }

// Chebyshev distance.
inline int distance(Square a, Square b) { return detail::SquareDistance[a][b]; }
inline int file_distance(Square a, Square b) {
    return file_of(a) > file_of(b) ? file_of(a) - file_of(b) : file_of(b) - file_of(a);
}
inline int rank_distance(Square a, Square b) {
    return rank_of(a) > rank_of(b) ? rank_of(a) - rank_of(b) : rank_of(b) - rank_of(a);
}

// Files immediately left and right of s (0, 1 or 2 files).
Bitboard adjacent_files_bb(Square s);
Bitboard adjacent_files_bb(File f);

// ------------------------------------------------------------------ debug --

std::string pretty(Bitboard b);
std::string square_name(Square s);

void init_bitboards();

}  // namespace rogatia

#endif  // ROGATIA_BITBOARD_H
