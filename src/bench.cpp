// Rogatia chess engine -- deterministic bench.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The node count printed here is the commit fingerprint and an OpenBench hard
// requirement.  It must be identical on every run, every machine and every
// -march level, so nothing on this path may depend on wall-clock time, the
// Hash option, or leftover table state.
#include "bench.h"

#include <chrono>
#include <cstdio>

#include "position.h"
#include "search.h"
#include "tt.h"

namespace rogatia {

namespace {

// Sixteen positions reached by playing standard opening lines out of the start
// position, plus eight endgames.  Generated and legality-checked with this
// engine's own movegen, so they carry no provenance from anyone else's bench.
constexpr const char* BenchFens[] = {
    "r1bqk2r/2p1bppp/p1np1n2/1p2p3/4P3/1B3N2/PPPP1PPP/RNBQR1K1 w kq - 0 8",
    "rnbqkb1r/5ppp/p2ppn2/1p6/3NP3/2N1BP2/PPP3PP/R2QKB1R w KQkq b6 0 8",
    "rnbq1rk1/p1p1bpp1/1p2pn1p/3p4/2PP3B/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 8",
    "r1bq1rk1/ppp2pbp/2np1np1/4p3/2PPP3/2N2N2/PP2BPPP/R1BQ1RK1 w - - 2 8",
    "r1bqk2r/pp2nppp/2n1p3/2ppP3/3P4/P1P2N2/2P2PPP/R1BQKB1R w KQkq - 3 8",
    "r2qkbnr/pp1nppp1/2p3bp/8/3P3P/5NN1/PPP2PP1/R1BQKB1R w KQkq - 2 8",
    "r1bqk2r/ppp1bppp/1nn5/4p3/8/2N2NP1/PP1PPPBP/R1BQ1RK1 w kq - 4 8",
    "rn1qk2r/pp3ppp/2p1pn2/5b2/PbBP4/2N1PN2/1P3PPP/R1BQK2R w KQkq - 1 8",
    "rnb1qrk1/ppp1b1pp/3ppn2/5p2/2PP4/2N2NP1/PP2PPBP/R1BQ1RK1 w - - 2 8",
    "r1bqk2r/pppp1ppp/2n2n2/8/2BPP3/5N2/PP1b1PPP/RN1QK2R w KQkq - 0 8",
    "rn2kb1r/pp2pppp/2p2n2/q7/3P4/2N2Q1P/PPP2PP1/R1B1KB1R w KQkq - 0 8",
    "r1bqkb1r/1p3ppp/p1n1pn2/2p5/P1BP4/4PN2/1P3PPP/RNBQ1RK1 w kq - 1 8",
    "r1bq1rk1/pp2bppp/2n1pn2/2pp4/4P3/3P1NP1/PPPN1PBP/R1BQ1RK1 w - - 1 8",
    "r1bq1rk1/pp2ppbp/n2p1np1/2p5/3PPP2/2NB1N2/PPP3PP/R1BQ1RK1 w - c6 0 8",
    "rn1q1rk1/pbppbppp/1p2p3/8/2PPn3/2N2NP1/PP2PPBP/R1BQ1RK1 w - - 7 8",
    "rnbq1rk1/ppp1b1pp/8/3pPp2/3Pn3/2NB1N2/PPP3PP/R1BQK2R w KQ f6 0 8",
    "8/8/8/4k3/8/4K3/4P3/8 w - - 0 1",
    "8/2k5/8/8/8/4K3/1R6/8 w - - 0 1",
    "4r1k1/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1",
    "8/3k4/3p4/8/3P4/3K4/8/8 w - - 0 1",
    "2r3k1/pp3pp1/4p2p/8/8/1P3P1P/P4RP1/6K1 w - - 0 1",
    "8/8/4bppk/8/8/5N1P/5PPK/8 w - - 0 1",
    "6k1/1p3ppp/p1p5/8/1PP5/P4PPP/6K1/8 w - - 0 1",
    "r5k1/pp3ppp/8/8/8/8/PP3PPP/R5K1 w - - 0 1",
};

// Fixed regardless of the Hash option: table size changes which entries get
// evicted, which changes the node count.
constexpr std::size_t BENCH_HASH_MB = 16;

}  // namespace

// ponytail: depth 8, not the customary 10-12.  With no pruning or reductions
// yet, depth 9 is 300M nodes and 95 seconds -- too slow to run on every commit.
// Raise this once Phase 4's LMR and null move land and the tree shrinks.
int bench_depth_default() { return 8; }

void run_bench(int depth) {
    TT.resize(BENCH_HASH_MB);

    std::uint64_t total = 0;
    const auto    start = std::chrono::steady_clock::now();

    for (const char* fen : BenchFens) {
        // Wipes the TT *and* the history heuristic.  Both carry over between
        // positions otherwise, which would make each count depend on the
        // previous position's tree and on whatever the process did beforehand.
        search::clear();

        Position pos;
        pos.set(fen);
        total += search::search_fixed_depth(pos, depth).nodes;
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();

    const std::uint64_t nps = total * 1000 / std::uint64_t(ms > 0 ? ms : 1);

    // OpenBench parses exactly this line.
    std::printf("%llu nodes %llu nps\n", (unsigned long long) total, (unsigned long long) nps);
    std::fflush(stdout);
}

}  // namespace rogatia
