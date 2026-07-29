// Rogatia chess engine -- entry point.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    const char* env = std::getenv("EVALFILE");
    std::string netUsed = (env && *env) ? env : EVALFILE;

    bool netOk = rogatia::nnue::load(netUsed.c_str());

    // A RELEASE bakes a relative net name, so the file sitting beside the
    // binary is found with no setup at all.  But a GUI may launch the engine
    // from any working directory, and then that relative name resolves to
    // nothing -- measured, not guessed: run from another directory the release
    // printed "NNUE load FAILED" and benched the no-net count, which is a
    // silent ~360 Elo loss for anyone who downloads it.
    //
    // So try again beside the executable itself before giving up.
    if (!netOk && netUsed.find_first_of("/\\") == std::string::npos) {
        const std::string self(argv[0] ? argv[0] : "");
        const auto        cut = self.find_last_of("/\\");
        if (cut != std::string::npos) {
            const std::string beside = self.substr(0, cut + 1) + netUsed;
            if (rogatia::nnue::load(beside.c_str())) {
                netUsed = beside;
                netOk   = true;
            }
        }
    }

    // Say so when the net does not load.  Silence here costs about 360 Elo --
    // the engine falls back to the piece-square tables and plays on happily.
    // `bench` would catch it, but a game never runs bench, so a whole SPRT can
    // measure the wrong engine and report a clean, believable result.
    if (!netOk)
        std::fprintf(stderr, "info string NNUE load FAILED for %s -- using the PSQT fallback\n",
                     netUsed.c_str());
    else {
        // Sanity-check the eval SCALE against the net that just loaded.  SCALE
        // is a property of the trained net, not of this code, so it cannot be
        // asserted -- but every pruning margin in tunable.h is calibrated to
        // it, and an inflated eval silently makes all of them more aggressive.
        // Phase 6 hit exactly that: a net trained at wdl=0.75 came out at about
        // twice the search scale, and nothing said a word.
        //
        // QUEEN ODDS, not the start position.  The start position is worth
        // about +20 to +40 cp, so at twice the scale it reads +40 to +80 --
        // comfortably inside any sane band, which means the obvious check
        // cannot see the exact failure it was written for.  A missing queen is
        // worth roughly +900, and a doubled scale reads ~+1800.
        rogatia::Position probe;
        probe.set("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        const int v = int(rogatia::nnue::evaluate(probe));
        if (v < 450 || v > 1500)
            std::fprintf(stderr,
                         "info string net evaluates queen odds at %d cp, expected ~900 -- the "
                         "eval scale may have moved, re-check the pruning margins in tunable.h\n",
                         v);
    }
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
