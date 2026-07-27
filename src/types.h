// Rogatia chess engine -- core types.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_TYPES_H
#define ROGATIA_TYPES_H

#include <cstdint>

namespace rogatia {

using Bitboard = std::uint64_t;
using Key      = std::uint64_t;
using Score    = std::int32_t;

constexpr int MAX_MOVES = 256;
constexpr int MAX_PLY   = 246;

// ---------------------------------------------------------------- squares --

enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64,
    SQUARE_NB = 64
};

enum File : int { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, FILE_NB };
enum Rank : int { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_NB };

// Board directions as square deltas (A1 = 0, H8 = 63, rank-major).
enum Direction : int {
    NORTH =  8,
    EAST  =  1,
    SOUTH = -8,
    WEST  = -1,

    NORTH_EAST = NORTH + EAST,
    NORTH_WEST = NORTH + WEST,
    SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST
};

constexpr Square operator+(Square s, Direction d) { return Square(int(s) + int(d)); }
constexpr Square operator-(Square s, Direction d) { return Square(int(s) - int(d)); }
constexpr Square& operator+=(Square& s, Direction d) { return s = s + d; }
constexpr Square& operator-=(Square& s, Direction d) { return s = s - d; }
constexpr Direction operator-(Square a, Square b) { return Direction(int(a) - int(b)); }
constexpr Direction operator-(Direction d) { return Direction(-int(d)); }

constexpr Square make_square(File f, Rank r) { return Square((int(r) << 3) + int(f)); }
constexpr File   file_of(Square s) { return File(int(s) & 7); }
constexpr Rank   rank_of(Square s) { return Rank(int(s) >> 3); }
constexpr bool   is_ok(Square s) { return s >= SQ_A1 && s <= SQ_H8; }

// ----------------------------------------------------------------- colors --

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };

constexpr Color operator~(Color c) { return Color(int(c) ^ 1); }

// Mirror a square vertically for BLACK (SQ_A1 -> SQ_A8).
constexpr Square relative_square(Color c, Square s) { return Square(int(s) ^ (c == WHITE ? 0 : 56)); }
constexpr Rank   relative_rank(Color c, Rank r) { return Rank(int(r) ^ (c == WHITE ? 0 : 7)); }
constexpr Rank   relative_rank(Color c, Square s) { return relative_rank(c, rank_of(s)); }

constexpr Direction pawn_push(Color c) { return c == WHITE ? NORTH : SOUTH; }

// ----------------------------------------------------------------- pieces --

enum PieceType : int {
    NO_PIECE_TYPE = 0,
    PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    PIECE_TYPE_NB = 7
};

// Piece = (color << 3) | type.  Index 0 and 7/8/15 are unused holes.
enum Piece : int {
    NO_PIECE = 0,
    W_PAWN = 1, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 9, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};

constexpr Piece     make_piece(Color c, PieceType pt) { return Piece((int(c) << 3) + int(pt)); }
constexpr PieceType type_of(Piece p) { return PieceType(int(p) & 7); }
constexpr Color     color_of(Piece p) { return Color(int(p) >> 3); }

// ------------------------------------------------------------- castling ----

enum CastlingRights : int {
    NO_CASTLING = 0,
    WHITE_OO    = 1,
    WHITE_OOO   = 2,
    BLACK_OO    = 4,
    BLACK_OOO   = 8,

    WHITE_CASTLING = WHITE_OO | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO | BLACK_OOO,
    ANY_CASTLING   = WHITE_CASTLING | BLACK_CASTLING,
    CASTLING_RIGHT_NB = 16
};

constexpr int castling_rights(Color c) { return c == WHITE ? WHITE_CASTLING : BLACK_CASTLING; }
constexpr int king_side_right(Color c) { return c == WHITE ? WHITE_OO : BLACK_OO; }
constexpr int queen_side_right(Color c) { return c == WHITE ? WHITE_OOO : BLACK_OOO; }

// ------------------------------------------------------------------ moves --

// 16-bit packed move:
//   bits  0-5   : from square
//   bits  6-11  : to square
//   bits 12-13  : move type (NORMAL / PROMOTION / EN_PASSANT / CASTLING)
//   bits 14-15  : promotion piece, encoded as (PieceType - KNIGHT)
//
// Move(0) is reserved as MOVE_NONE: from == to == SQ_A1 is never a real move.
enum MoveType : int {
    NORMAL     = 0,
    PROMOTION  = 1,
    EN_PASSANT = 2,
    CASTLING   = 3
};

enum Move : std::uint16_t { MOVE_NONE = 0, MOVE_NULL = 65 };

constexpr Square   from_sq(Move m)  { return Square(int(m) & 0x3F); }
constexpr Square   to_sq(Move m)    { return Square((int(m) >> 6) & 0x3F); }
constexpr MoveType type_of(Move m)  { return MoveType((int(m) >> 12) & 0x3); }
// Only meaningful when type_of(m) == PROMOTION.
constexpr PieceType promotion_type(Move m) { return PieceType(((int(m) >> 14) & 0x3) + KNIGHT); }
// from and to together, i.e. the move with type/promo stripped.
constexpr int from_to(Move m) { return int(m) & 0xFFF; }

constexpr Move make_move(Square from, Square to) {
    return Move(std::uint16_t(int(from) | (int(to) << 6)));
}

template<MoveType T>
constexpr Move make_move(Square from, Square to, PieceType promo = KNIGHT) {
    return Move(std::uint16_t(int(from) | (int(to) << 6) | (int(T) << 12)
                             | ((int(promo) - int(KNIGHT)) << 14)));
}

constexpr bool is_ok(Move m) { return m != MOVE_NONE && m != MOVE_NULL; }

}  // namespace rogatia

#endif  // ROGATIA_TYPES_H
