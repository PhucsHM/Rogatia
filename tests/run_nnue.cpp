// Rogatia chess engine -- accumulator consistency gate.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// nnue::apply_move duplicates make_move's understanding of what a move does to
// the board.  That duplication is the price of incremental updates, and the
// failure mode is silent: a wrong accumulator still evaluates to a plausible
// number, so the engine plays slightly worse for reasons nothing reports.
//
// This walks the tree from several positions and asserts, at every node, that
// the incrementally-derived accumulator is bit-identical to a from-scratch
// refresh of the resulting position.  It is to the accumulator what perft is to
// move generation, and it is fast enough to run on every commit.
#include <cstdio>
#include <cstring>

#include "movegen.h"
#include "nnue.h"
#include "position.h"

using namespace rogatia;

namespace {

std::uint64_t Nodes    = 0;
std::uint64_t Mismatch = 0;

void walk(Position& pos, const nnue::Accumulator& acc, int depth) {
    if (depth == 0)
        return;

    for (Move m : MoveList<ALL>(pos)) {
        if (!pos.is_legal(m))
            continue;

        nnue::Accumulator incremental;
        nnue::apply_move(pos, m, acc, incremental);

        pos.make_move(m);
        ++Nodes;

        nnue::Accumulator scratch;
        nnue::refresh(pos, scratch);

        if (std::memcmp(&incremental, &scratch, sizeof(nnue::Accumulator)) != 0) {
            if (++Mismatch <= 5)
                std::printf("  MISMATCH after %s%s in %s\n",
                            "", "", pos.fen().c_str());
        }

        walk(pos, incremental, depth - 1);
        pos.unmake_move(m);
    }
}

struct Case {
    const char* fen;
    int         depth;
    const char* label;
};

// Chosen for what they exercise, not for size: castling both sides, en passant,
// promotions with and without capture.
const Case Cases[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, "startpos"},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, "kiwipete (castling)"},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, "en passant"},
    {"n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1", 4, "promotions"},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, "promo captures"},
};

}  // namespace

int main(int argc, char** argv) {
    rogatia::init();

    const char* net = (argc > 1) ? argv[1] : nullptr;
    if (!net || !nnue::load(net)) {
        std::printf("usage: run_nnue <net.nnue>   (a network is required)\n");
        return 1;
    }

    for (const Case& c : Cases) {
        Position pos;
        if (!pos.set(c.fen)) {
            std::printf("bad FEN: %s\n", c.fen);
            return 1;
        }

        nnue::Accumulator acc;
        nnue::refresh(pos, acc);

        const std::uint64_t before    = Nodes;
        const std::uint64_t bad       = Mismatch;
        walk(pos, acc, c.depth);

        std::printf("  %-22s depth %d  %8llu nodes  %s\n", c.label, c.depth,
                    (unsigned long long) (Nodes - before),
                    (Mismatch == bad) ? "OK" : "MISMATCH");
    }

    if (Mismatch) {
        std::printf("\n%llu accumulator mismatches in %llu nodes\n",
                    (unsigned long long) Mismatch, (unsigned long long) Nodes);
        return 1;
    }

    std::printf("\naccumulator matches a full refresh at all %llu nodes\n",
                (unsigned long long) Nodes);
    return 0;
}
