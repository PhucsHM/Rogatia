// Rogatia chess engine -- attack tables.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "attacks.h"

#include <cassert>
#include <cstdio>

namespace rogatia {

namespace detail {
Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];
Magic    RookMagics[SQUARE_NB];
Magic    BishopMagics[SQUARE_NB];
}  // namespace detail

namespace {

Bitboard RookTable[ROOK_TABLE_SIZE];
Bitboard BishopTable[BISHOP_TABLE_SIZE];

constexpr Direction RookDirs[4]   = {NORTH, EAST, SOUTH, WEST};
constexpr Direction BishopDirs[4] = {NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST};
constexpr Direction KingDirs[8]   = {NORTH, NORTH_EAST, EAST, SOUTH_EAST,
                                     SOUTH, SOUTH_WEST, WEST, NORTH_WEST};
// Knight jumps as (file, rank) offsets -- kept as offsets rather than square
// deltas so the wrap check is trivial.
constexpr int KnightOffsets[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2},
                                     {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};

Square step(Square s, Direction d) {
    const int t = int(s) + int(d);
    if (t < 0 || t > 63)
        return SQ_NONE;
    const Square to = Square(t);
    if (file_distance(s, to) > 1)
        return SQ_NONE;
    return to;
}

// Deterministic xorshift64.  Fixed seed: magic tables must be identical on
// every build and every machine.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed) {}
    std::uint64_t next() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 0x2545F4914F6CDD1DULL;
    }
    // Magic candidates want few set bits.
    std::uint64_t sparse() { return next() & next() & next(); }

private:
    std::uint64_t s_;
};

void build_magics(PieceType pt, Bitboard* table, Magic magics[]);

}  // namespace

Bitboard sliding_attacks_ref(PieceType pt, Square s, Bitboard occ) {
    Bitboard result = 0;
    const Direction* dirs = (pt == BISHOP) ? BishopDirs : RookDirs;
    const int nDirs = (pt == QUEEN) ? 8 : 4;

    for (int i = 0; i < nDirs; ++i) {
        const Direction d = (pt == QUEEN) ? KingDirs[i] : dirs[i];
        for (Square t = step(s, d); t != SQ_NONE; t = step(t, d)) {
            result |= square_bb(t);
            if (occ & square_bb(t))
                break;
        }
    }
    return result;
}

namespace {

// Builds mask, magic and the attack slice for every square of one slider.
void build_magics(PieceType pt, Bitboard* table, Magic magics[]) {
    static Bitboard occs[4096];
    static Bitboard refs[4096];
#ifndef ROGATIA_PEXT
    // Collision detection scratch for the magic search; `cnt` is the epoch, so
    // the array never needs clearing between candidates.
    static int epoch[4096];
    static int cnt = 0;

    Rng rng(pt == ROOK ? 0x9E3779B97F4A7C15ULL : 0xD1B54A32D192ED03ULL);
#endif

    int offset = 0;
    for (int i = 0; i < SQUARE_NB; ++i) {
        const Square s = Square(i);
        Magic&       m = magics[s];

        // Trim the board edges: a blocker on the last square of a ray cannot
        // change what is attacked, so it need not be part of the index.
        const Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb(s))
                             | ((FileABB | FileHBB) & ~file_bb(s));

        m.mask    = sliding_attacks_ref(pt, s, 0) & ~edges;
        m.shift   = unsigned(64 - popcount(m.mask));
        m.attacks = table + offset;

        // Carry-rippler enumeration of every subset of the mask.
        int      n = 0;
        Bitboard b = 0;
        do {
            occs[n] = b;
            refs[n] = sliding_attacks_ref(pt, s, b);
            ++n;
            b = (b - m.mask) & m.mask;
        } while (b);

        assert(n == (1 << popcount(m.mask)));
        offset += n;

#ifdef ROGATIA_PEXT
        m.magic = 0;
        for (int k = 0; k < n; ++k)
            m.attacks[m.index(occs[k])] = refs[k];
#else
        for (;;) {
            // A usable magic must at least scatter the high bits.
            do {
                m.magic = rng.sparse();
            } while (popcount((m.magic * ~m.mask) >> 56) < 6);

            ++cnt;
            bool ok = true;
            for (int k = 0; k < n; ++k) {
                const unsigned idx = m.index(occs[k]);
                assert(idx < unsigned(n));
                if (epoch[idx] != cnt) {
                    epoch[idx]    = cnt;
                    m.attacks[idx] = refs[k];
                } else if (m.attacks[idx] != refs[k]) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                break;
        }
#endif
    }
    assert(offset == (pt == ROOK ? ROOK_TABLE_SIZE : BISHOP_TABLE_SIZE));
    (void) offset;
}

}  // namespace

void init_attacks() {
    for (int i = 0; i < SQUARE_NB; ++i) {
        const Square   s  = Square(i);
        const Bitboard bb = square_bb(s);

        detail::PawnAttacks[WHITE][s] = pawn_attacks_bb<WHITE>(bb);
        detail::PawnAttacks[BLACK][s] = pawn_attacks_bb<BLACK>(bb);

        Bitboard n = 0;
        for (auto& off : KnightOffsets) {
            const int f = int(file_of(s)) + off[0];
            const int r = int(rank_of(s)) + off[1];
            if (f >= 0 && f < 8 && r >= 0 && r < 8)
                n |= square_bb(make_square(File(f), Rank(r)));
        }
        detail::PseudoAttacks[KNIGHT][s] = n;

        Bitboard k = 0;
        for (Direction d : KingDirs)
            if (step(s, d) != SQ_NONE)
                k |= square_bb(step(s, d));
        detail::PseudoAttacks[KING][s] = k;
    }

    build_magics(ROOK, RookTable, detail::RookMagics);
    build_magics(BISHOP, BishopTable, detail::BishopMagics);

    for (int i = 0; i < SQUARE_NB; ++i) {
        const Square s = Square(i);
        detail::PseudoAttacks[BISHOP][s] = attacks<BISHOP>(s, 0);
        detail::PseudoAttacks[ROOK][s]   = attacks<ROOK>(s, 0);
        detail::PseudoAttacks[QUEEN][s]  = detail::PseudoAttacks[BISHOP][s]
                                         | detail::PseudoAttacks[ROOK][s];
    }

    // The black-magic and PEXT paths must agree for every square and occupancy.
    // verify_slider_tables() was written to check that and was never called, so
    // the two builds have only ever been compared by hand with perft.  Free in
    // release, and `make debug` now proves it on every run.
    assert(verify_slider_tables());
}

bool verify_slider_tables() {
    for (int i = 0; i < SQUARE_NB; ++i) {
        const Square s = Square(i);
        for (PieceType pt : {BISHOP, ROOK}) {
            const Magic& m = (pt == ROOK) ? detail::RookMagics[s] : detail::BishopMagics[s];
            Bitboard b = 0;
            do {
                // Sprinkle irrelevant bits in too: lookups get full occupancy,
                // not a pre-masked one.
                const Bitboard occ = b | (~m.mask & 0xA5A5A5A5A5A5A5A5ULL);
                if (attacks(pt, s, occ) != sliding_attacks_ref(pt, s, occ)) {
                    std::fprintf(stderr, "slider table mismatch: pt=%d sq=%d\n", int(pt), i);
                    return false;
                }
                b = (b - m.mask) & m.mask;
            } while (b);
        }
    }
    return true;
}

}  // namespace rogatia
