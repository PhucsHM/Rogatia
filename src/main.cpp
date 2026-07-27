// Rogatia chess engine -- entry point.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdlib>
#include <cstring>

#include "bench.h"
#include "position.h"
#include "tt.h"
#include "uci.h"

int main(int argc, char** argv) {
    rogatia::init();

    // `rogatia bench [depth]` is how OpenBench measures the fingerprint: run
    // it and exit, never touching stdin.
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        const int depth = (argc > 2) ? std::atoi(argv[2]) : rogatia::bench_depth_default();
        rogatia::run_bench(depth > 0 ? depth : rogatia::bench_depth_default());
        return 0;
    }

    rogatia::uci_loop();
    return 0;
}
