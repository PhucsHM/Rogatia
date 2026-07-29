// Rogatia chess engine -- entry point.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bench.h"
#include "datagen.h"
#include "nnue.h"
#include "position.h"
#include "tt.h"
#include "uci.h"

int main(int argc, char** argv) {
    rogatia::init();

    // EVALFILE is baked in at build time and may be overridden at runtime, so a
    // net can be swapped without a rebuild while it is still being iterated on.
#ifdef EVALFILE
    const char* netPath = std::getenv("EVALFILE");
    const char* netUsed = netPath && *netPath ? netPath : EVALFILE;
    // Say so when the net does not load.  Silence here costs about 360 Elo --
    // the engine falls back to the piece-square tables and plays on happily.
    // `bench` would catch it, but a game never runs bench, so a whole SPRT can
    // measure the wrong engine and report a clean, believable result.
    if (!rogatia::nnue::load(netUsed))
        std::fprintf(stderr, "info string NNUE load FAILED for %s -- using the PSQT fallback\n",
                     netUsed);
#endif

    // `rogatia bench [depth]` is how OpenBench measures the fingerprint: run
    // it and exit, never touching stdin.
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        const int depth = (argc > 2) ? std::atoi(argv[2]) : rogatia::bench_depth_default();
        rogatia::run_bench(depth > 0 ? depth : rogatia::bench_depth_default());
        return 0;
    }

    // `rogatia datagen <out> <positions> [seed] [nodes]`, so a fleet of workers
    // can be launched from a shell without piping anything into stdin.
    if (argc > 2 && std::strcmp(argv[1], "datagen") == 0) {
        rogatia::datagen::Config cfg;
        cfg.output    = argv[2];
        cfg.positions = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 0;
        if (argc > 4)
            cfg.seed = std::strtoull(argv[4], nullptr, 10);
        if (argc > 5)
            cfg.nodes = std::strtoull(argv[5], nullptr, 10);
        rogatia::datagen::run(cfg);
        return 0;
    }

    rogatia::uci_loop();
    return 0;
}
