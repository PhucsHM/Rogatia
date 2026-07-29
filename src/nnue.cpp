// Rogatia chess engine -- NNUE evaluation.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inference for the `(768 -> 256)x2 -> 1` network trained by bullet.  The
// weight file is bullet's `quantised.bin`; its layout is documented in
// docs/ROADMAP.md and asserted below, because a silently misread net produces
// plausible-looking numbers rather than an obvious failure.
//
// ponytail: scalar loops, no hand-written AVX2.  The compiler vectorises both
// of them at -O3 (they are contiguous int16 accumulate and multiply-accumulate,
// the easy case), and hand-written intrinsics are exactly where bench
// determinism across -march levels goes to die.  Upgrade path if a profile
// justifies it: explicit intrinsics with a fixed accumulation order.
#include "nnue.h"

#include <cstdio>
#include <cstring>

namespace rogatia::nnue {

namespace {

// alignas(64), because nothing else requires it.  The natural alignment is
// alignof(int16_t) = 2; linkers usually give a large .bss object 16 or 32 bytes
// as a side effect, but that can change with the compiler, with LTO, or with a
// -march level.  Every feature column is 512 bytes apart, so they all share the
// base's alignment residue -- if the base is under-aligned, EVERY weight load
// in the accumulator update is unaligned.
//
// The offsets work out so one attribute covers all three arrays: l0w is
// 768*256*2 = 393,216 = 6144*64, l0b is 512, so l1w starts at 6152*64.
struct alignas(64) Network {
    // Column-major HIDDEN x INPUTS: a feature's HIDDEN weights are contiguous,
    // which is the whole reason an accumulator update is cheap.
    std::int16_t l0w[INPUTS * HIDDEN];
    std::int16_t l0b[HIDDEN];
    std::int16_t l1w[2 * HIDDEN];
    std::int16_t l1b;
};

Network Net;
bool    Loaded = false;

// Index of (colour, pieceType, square) in the 768 inputs, seen from `pov`.
// Both the piece colour and the square are mirrored, so "our pawn on the second
// rank" lands on the same input whichever side is to move -- that shared
// structure is what the net is able to learn.
int feature_index(Color pov, Color pieceColor, PieceType pt, Square s) {
    const int relSquare = (pov == WHITE) ? int(s) : int(s) ^ 56;
    const int relColour = (pieceColor == pov) ? 0 : 1;
    return relColour * 384 + 64 * (int(pt) - 1) + relSquare;
}

// Squared clipped ReLU.  The square is what makes this worth using over plain
// clipped ReLU, and it is why the output needs an extra division by QA below.
int screlu(std::int16_t x) {
    const int v = (x < 0) ? 0 : (x > QA ? QA : int(x));
    return v * v;
}

}  // namespace

bool load(const char* path) {
    Loaded = false;
    if (!path || !*path)
        return false;

    std::FILE* f = std::fopen(path, "rb");
    if (!f)
        return false;

    // Check the size FIRST.  The reads below only prove the file is at least
    // big enough, so a net for a different architecture -- a Phase 8
    // (768 -> 1024) file, say -- loads "successfully" and then evaluates as
    // plausible garbage.  That is the silent-failure class this file exists to
    // avoid, and the header comment promised an assert that was never written.
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    // Weights, then up to 63 bytes of padding: bullet pads the file with the
    // ASCII string "bullet" repeated to a 64-byte boundary.
    constexpr long PAYLOAD = long(sizeof(std::int16_t)) * (INPUTS * HIDDEN + HIDDEN + 2 * HIDDEN + 1);
    if (size < PAYLOAD || size >= PAYLOAD + 64) {
        std::fclose(f);
        std::fprintf(stderr,
                     "info string net %s is %ld bytes, expected %ld -- wrong architecture?\n",
                     path, size, PAYLOAD);
        return false;
    }

    // Read the sections individually rather than the struct in one go: the file
    // ends with padding which is not part of any weight.
    const bool ok = std::fread(Net.l0w, sizeof(std::int16_t), INPUTS * HIDDEN, f)
                        == INPUTS * HIDDEN
                 && std::fread(Net.l0b, sizeof(std::int16_t), HIDDEN, f) == HIDDEN
                 && std::fread(Net.l1w, sizeof(std::int16_t), 2 * HIDDEN, f) == 2 * HIDDEN
                 && std::fread(&Net.l1b, sizeof(std::int16_t), 1, f) == 1;

    std::fclose(f);
    Loaded = ok;
    return ok;
}

bool loaded() { return Loaded; }

void refresh(const Position& pos, Accumulator& acc) {
    for (Color pov : {WHITE, BLACK})
        std::memcpy(acc.v[pov], Net.l0b, sizeof(Net.l0b));

    for (Color c : {WHITE, BLACK})
        for (PieceType pt = PAWN; pt <= KING; pt = PieceType(pt + 1)) {
            Bitboard b = pos.pieces(c, pt);
            while (b) {
                const Square s = lsb(b);
                b &= b - 1;

                for (Color pov : {WHITE, BLACK}) {
                    const std::int16_t* w =
                        &Net.l0w[feature_index(pov, c, pt, s) * HIDDEN];
                    for (int i = 0; i < HIDDEN; ++i)
                        acc.v[pov][i] = std::int16_t(acc.v[pov][i] + w[i]);
                }
            }
        }
}

namespace {

void add_feature(Accumulator& a, Color c, PieceType pt, Square s) {
    for (Color pov : {WHITE, BLACK}) {
        const std::int16_t* w = &Net.l0w[feature_index(pov, c, pt, s) * HIDDEN];
        for (int i = 0; i < HIDDEN; ++i)
            a.v[pov][i] = std::int16_t(a.v[pov][i] + w[i]);
    }
}

void remove_feature(Accumulator& a, Color c, PieceType pt, Square s) {
    for (Color pov : {WHITE, BLACK}) {
        const std::int16_t* w = &Net.l0w[feature_index(pov, c, pt, s) * HIDDEN];
        for (int i = 0; i < HIDDEN; ++i)
            a.v[pov][i] = std::int16_t(a.v[pov][i] - w[i]);
    }
}

}  // namespace

void apply_move(const Position& pos, Move m, const Accumulator& src, Accumulator& dst) {
    // This duplicates make_move's understanding of what a move does to the
    // board, which is a real risk of drift.  tests/run_nnue.cpp walks a tree and
    // asserts this agrees with a from-scratch refresh at every node -- treat
    // that as the perft of the accumulator and never skip it.
    dst = src;

    const Color    us   = pos.side_to_move();
    const Square   from = from_sq(m);
    const Square   to   = to_sq(m);
    const MoveType mt   = type_of(m);
    const PieceType pt  = type_of(pos.piece_on(from));

    remove_feature(dst, us, pt, from);

    if (mt == CASTLING) {
        // `to` is the king's landing square, and the rook jumps over it.
        const bool   kingSide = to > from;
        const Square rfrom    = relative_square(us, kingSide ? SQ_H1 : SQ_A1);
        const Square rto      = relative_square(us, kingSide ? SQ_F1 : SQ_D1);

        add_feature(dst, us, KING, to);
        remove_feature(dst, us, ROOK, rfrom);
        add_feature(dst, us, ROOK, rto);
        return;
    }

    if (mt == EN_PASSANT)
        remove_feature(dst, ~us, PAWN, Square(int(to) - int(pawn_push(us))));
    else if (!pos.empty(to))
        remove_feature(dst, ~us, type_of(pos.piece_on(to)), to);

    add_feature(dst, us, mt == PROMOTION ? promotion_type(m) : pt, to);
}

Score evaluate(const Position& pos, const Accumulator& acc) {
    const Color us   = pos.side_to_move();
    const Color them = ~us;

    // The side to move always occupies the first half of the output weights.
    //
    // Multiply in int32, widen only the ADDEND.  The product provably fits:
    // screlu maxes at QA*QA = 65,025 and a weight at 32,767, so the worst term
    // is 2,130,674,175 < 2^31.  Only the SUM of 512 of them can overflow, so
    // that is the only place the 64-bit type is needed.
    //
    // Widening before the multiply -- which is what this used to do -- makes it
    // a 64-bit multiply per term, 512 per call, and forces any vectorisation
    // down to four int64 lanes per vector with a pile of unpacking.  Same
    // safety, much slower, for nothing.
    std::int64_t out = 0;
    for (int i = 0; i < HIDDEN; ++i)
        out += std::int64_t(screlu(acc.v[us][i]) * int(Net.l1w[i]));
    for (int i = 0; i < HIDDEN; ++i)
        out += std::int64_t(screlu(acc.v[them][i]) * int(Net.l1w[HIDDEN + i]));

    // screlu squared the QA-quantised activations, so the running total is at
    // QA*QA*QB.  One division brings it back to QA*QB, which is the scale the
    // bias is stored at.
    out = out / QA + int(Net.l1b);

    return Score(out * SCALE / (QA * QB));
}

Score evaluate(const Position& pos) {
    Accumulator acc;
    refresh(pos, acc);
    return evaluate(pos, acc);
}

}  // namespace rogatia::nnue
