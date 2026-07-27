// Rogatia chess engine -- deterministic bench.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_BENCH_H
#define ROGATIA_BENCH_H

namespace rogatia {

// Searches a fixed position set at a fixed depth and prints
// "<nodes> nodes <nps> nps".  The node count is the determinism fingerprint.
void run_bench(int depth);
int  bench_depth_default();

}  // namespace rogatia

#endif  // ROGATIA_BENCH_H
