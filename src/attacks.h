// Rogatia chess engine -- attack generation (black magic bitboards / PEXT).
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_ATTACKS_H
#define ROGATIA_ATTACKS_H

#include "bitboard.h"
#include "types.h"

#ifdef __BMI2__
#include <immintrin.h>
#endif

namespace rogatia {

// Sizes of the per-square, non-overlapping slider tables.
// sum over squares of 2^popcount(relevant_mask).
constexpr int ROOK_TABLE_SIZE   = 102400;
constexpr int BISHOP_TABLE_SIZE = 5248;

// One slider lookup descriptor.
//
// Black magics: the relevant occupancy is *negated in*, i.e. every irrelevant
// bit is forced to one before multiplying.  That is the whole trick -- it lets
// a single multiply-shift serve as the index without masking first.
//
// Under -mbmi2 the same struct indexes with PEXT instead.  The tables are laid
// out identically in both modes (per square, size 2^popcount(mask), no
// sharing), so the two builds can be perft-diffed against each other with no
// other change.
struct Magic {
    Bitboard  mask;     // relevant occupancy squares (edges trimmed)
    Bitboard  magic;    // unused in the PEXT build
    Bitboard* attacks;  // -> table slice of size 2^popcount(mask)
    unsigned  shift;    // 64 - popcount(mask)

    unsigned index(Bitboard occ) const {
#ifdef __BMI2__
        return unsigned(_pext_u64(occ, mask));
#else
        return unsigned(((occ | ~mask) * magic) >> shift);
#endif
    }

    Bitboard operator[](Bitboard occ) const { return attacks[index(occ)]; }
};

namespace detail {
extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];  // empty-board attacks
extern Magic    RookMagics[SQUARE_NB];
extern Magic    BishopMagics[SQUARE_NB];
}  // namespace detail

void init_attacks();

// Reference sliding attacks by ray walking.  Slow, obviously correct; used to
// build the magic tables and available as a cross-check.
Bitboard sliding_attacks_ref(PieceType pt, Square s, Bitboard occ);

// ------------------------------------------------------------ lookups -----

inline Bitboard pawn_attacks_bb(Color c, Square s) {
    assert(is_ok(s));
    return detail::PawnAttacks[c][s];
}

// All squares attacked by a set of pawns of colour c.
template<Color C>
constexpr Bitboard pawn_attacks_bb(Bitboard pawns) {
    return C == WHITE ? shift<NORTH_WEST>(pawns) | shift<NORTH_EAST>(pawns)
                      : shift<SOUTH_WEST>(pawns) | shift<SOUTH_EAST>(pawns);
}

inline Bitboard pawn_attacks_bb(Color c, Bitboard pawns) {
    return c == WHITE ? pawn_attacks_bb<WHITE>(pawns) : pawn_attacks_bb<BLACK>(pawns);
}

template<PieceType Pt>
inline Bitboard attacks(Square s, Bitboard occ = 0) {
    static_assert(Pt != PAWN, "use pawn_attacks_bb for pawns");
    assert(is_ok(s));
    if constexpr (Pt == KNIGHT || Pt == KING)
        return detail::PseudoAttacks[Pt][s];
    else if constexpr (Pt == BISHOP)
        return detail::BishopMagics[s][occ];
    else if constexpr (Pt == ROOK)
        return detail::RookMagics[s][occ];
    else
        return detail::BishopMagics[s][occ] | detail::RookMagics[s][occ];
}

inline Bitboard attacks(PieceType pt, Square s, Bitboard occ = 0) {
    switch (pt) {
    case KNIGHT: return attacks<KNIGHT>(s);
    case KING:   return attacks<KING>(s);
    case BISHOP: return attacks<BISHOP>(s, occ);
    case ROOK:   return attacks<ROOK>(s, occ);
    case QUEEN:  return attacks<QUEEN>(s, occ);
    default:     assert(false); return 0;
    }
}

// Empty-board attacks, useful for cheap "could this ever reach" tests.
inline Bitboard pseudo_attacks(PieceType pt, Square s) { return detail::PseudoAttacks[pt][s]; }

// True when the black-magic and PEXT indexers were both available and agree.
// (Only meaningful in a BMI2 build; see attacks.cpp.)
bool verify_slider_tables();

}  // namespace rogatia

#endif  // ROGATIA_ATTACKS_H
