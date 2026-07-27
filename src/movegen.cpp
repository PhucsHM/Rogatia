// Rogatia chess engine -- pseudo-legal move generation.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "movegen.h"

namespace rogatia {

namespace {

template<Color Us, GenType T>
Move* generate_pawn_moves(const Position& pos, Move* ml) {
    constexpr Color     Them    = ~Us;
    constexpr Direction Up      = (Us == WHITE) ? NORTH : SOUTH;
    constexpr Direction UpRight = (Us == WHITE) ? NORTH_EAST : SOUTH_EAST;
    constexpr Direction UpLeft  = (Us == WHITE) ? NORTH_WEST : SOUTH_WEST;
    constexpr Bitboard  Rank7   = (Us == WHITE) ? Rank7BB : Rank2BB;
    constexpr Bitboard  Rank3   = (Us == WHITE) ? Rank3BB : Rank6BB;

    const Bitboard empty   = ~pos.pieces();
    const Bitboard them    = pos.pieces(Them);
    const Bitboard pawns   = pos.pieces(Us, PAWN);
    const Bitboard onRank7 = pawns & Rank7;
    const Bitboard rest    = pawns & ~Rank7;

    if constexpr (T != CAPTURES) {
        Bitboard single = shift<Up>(rest) & empty;
        Bitboard dbl    = shift<Up>(single & Rank3) & empty;

        while (single) {
            const Square to = pop_lsb(single);
            *ml++ = make_move(to - Up, to);
        }
        while (dbl) {
            const Square to = pop_lsb(dbl);
            *ml++ = make_move(to - Up - Up, to);
        }

        Bitboard promo = shift<Up>(onRank7) & empty;
        while (promo) {
            const Square to   = pop_lsb(promo);
            const Square from = to - Up;
            *ml++ = make_move<PROMOTION>(from, to, QUEEN);
            *ml++ = make_move<PROMOTION>(from, to, ROOK);
            *ml++ = make_move<PROMOTION>(from, to, BISHOP);
            *ml++ = make_move<PROMOTION>(from, to, KNIGHT);
        }
    }

    if constexpr (T != QUIETS) {
        Bitboard capR = shift<UpRight>(rest) & them;
        Bitboard capL = shift<UpLeft>(rest) & them;

        while (capR) {
            const Square to = pop_lsb(capR);
            *ml++ = make_move(to - UpRight, to);
        }
        while (capL) {
            const Square to = pop_lsb(capL);
            *ml++ = make_move(to - UpLeft, to);
        }

        Bitboard promoR = shift<UpRight>(onRank7) & them;
        Bitboard promoL = shift<UpLeft>(onRank7) & them;

        while (promoR) {
            const Square to   = pop_lsb(promoR);
            const Square from = to - UpRight;
            *ml++ = make_move<PROMOTION>(from, to, QUEEN);
            *ml++ = make_move<PROMOTION>(from, to, ROOK);
            *ml++ = make_move<PROMOTION>(from, to, BISHOP);
            *ml++ = make_move<PROMOTION>(from, to, KNIGHT);
        }
        while (promoL) {
            const Square to   = pop_lsb(promoL);
            const Square from = to - UpLeft;
            *ml++ = make_move<PROMOTION>(from, to, QUEEN);
            *ml++ = make_move<PROMOTION>(from, to, ROOK);
            *ml++ = make_move<PROMOTION>(from, to, BISHOP);
            *ml++ = make_move<PROMOTION>(from, to, KNIGHT);
        }

        if (pos.ep_square() != SQ_NONE) {
            const Square ep = pos.ep_square();
            // Squares a pawn of ours must stand on to reach ep = squares an
            // enemy pawn on ep would attack.
            Bitboard b = pawn_attacks_bb(Them, ep) & rest;
            while (b)
                *ml++ = make_move<EN_PASSANT>(pop_lsb(b), ep);
        }
    }

    return ml;
}

template<Color Us, PieceType Pt>
Move* generate_piece_moves(const Position& pos, Move* ml, Bitboard target) {
    static_assert(Pt != PAWN && Pt != KING, "pawns and kings are generated separately");

    const Bitboard occ = pos.pieces();
    Bitboard       b   = pos.pieces(Us, Pt);

    while (b) {
        const Square from = pop_lsb(b);
        Bitboard     att  = attacks<Pt>(from, occ) & target;
        while (att)
            *ml++ = make_move(from, pop_lsb(att));
    }
    return ml;
}

template<Color Us, GenType T>
Move* generate_king_moves(const Position& pos, Move* ml, Bitboard target) {
    const Square ksq = pos.king_square(Us);

    Bitboard b = attacks<KING>(ksq) & target;
    while (b)
        *ml++ = make_move(ksq, pop_lsb(b));

    if constexpr (T != CAPTURES) {
        constexpr Square KFrom = relative_square(Us, SQ_E1);

        if (ksq == KFrom && !pos.checkers()) {
            // Rights already imply the rook is home: they are revoked whenever
            // the rook moves or is captured.  is_legal() checks the king's walk.
            if (pos.can_castle(king_side_right(Us))) {
                constexpr Bitboard Path = square_bb(relative_square(Us, SQ_F1))
                                        | square_bb(relative_square(Us, SQ_G1));
                if (!(pos.pieces() & Path))
                    *ml++ = make_move<CASTLING>(KFrom, relative_square(Us, SQ_G1));
            }
            if (pos.can_castle(queen_side_right(Us))) {
                // b1/b8 must be empty too, though the king never steps on it.
                constexpr Bitboard Path = square_bb(relative_square(Us, SQ_B1))
                                        | square_bb(relative_square(Us, SQ_C1))
                                        | square_bb(relative_square(Us, SQ_D1));
                if (!(pos.pieces() & Path))
                    *ml++ = make_move<CASTLING>(KFrom, relative_square(Us, SQ_C1));
            }
        }
    }
    return ml;
}

template<Color Us, GenType T>
Move* generate_all(const Position& pos, Move* ml) {
    const Bitboard target = (T == CAPTURES) ? pos.pieces(~Us)
                          : (T == QUIETS)   ? ~pos.pieces()
                                            : ~pos.pieces(Us);

    ml = generate_pawn_moves<Us, T>(pos, ml);
    ml = generate_piece_moves<Us, KNIGHT>(pos, ml, target);
    ml = generate_piece_moves<Us, BISHOP>(pos, ml, target);
    ml = generate_piece_moves<Us, ROOK>(pos, ml, target);
    ml = generate_piece_moves<Us, QUEEN>(pos, ml, target);
    ml = generate_king_moves<Us, T>(pos, ml, target);
    return ml;
}

}  // namespace

template<GenType T>
Move* generate(const Position& pos, Move* moveList) {
    return pos.side_to_move() == WHITE ? generate_all<WHITE, T>(pos, moveList)
                                       : generate_all<BLACK, T>(pos, moveList);
}

template Move* generate<CAPTURES>(const Position&, Move*);
template Move* generate<QUIETS>(const Position&, Move*);
template Move* generate<ALL>(const Position&, Move*);

}  // namespace rogatia
