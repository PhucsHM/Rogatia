// Rogatia chess engine -- tunable search constants.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every search constant that a tuner could plausibly move, in one list, exposed
// as a UCI spin option so SPSA can drive it without a recompile.  Defaults are
// the values the Phase 4 SPRT passed with, so an engine that is never sent a
// setoption behaves exactly as tested -- including its bench node count.
//
// ponytail: plain ints, not a compile-time TUNE switch.  They are read a
// handful of times per node against a move generator that costs far more, so
// the lost immediates do not show up.  Upgrade path if they ever do: wrap the
// declarations in `#ifdef TUNE` and fall back to constexpr.
#ifndef ROGATIA_TUNABLE_H
#define ROGATIA_TUNABLE_H

#include <string>

// name, default, min, max
#define ROGATIA_TUNABLES(X)                     \
    X(AspWindow,        20,     5,     50)      \
    X(TmNodeBase,      180,   100,    300)      \
    X(TmNodeSlope,     110,     0,    250)      \
    X(RazorDepth,        4,     1,      8)      \
    X(RazorMargin,     400,   100,    900)      \
    X(RfpDepth,          8,     4,     12)      \
    X(RfpMargin,        75,    20,    200)      \
    X(NmpDepth,          3,     2,      6)      \
    X(NmpBase,           3,     1,      6)      \
    X(NmpDepthDiv,       3,     1,      8)      \
    X(NmpEvalDiv,      200,    50,    600)      \
    X(NmpEvalCap,        3,     0,      6)      \
    X(LmrDepth,          3,     2,      6)      \
    X(LmrMoveCount,      2,     1,      6)      \
    X(LmrQuietBase,    819,   200,   1600)      \
    X(LmrQuietDiv,    2304,  1000,   4000)      \
    X(LmrNoisyBase,    205,     0,   1000)      \
    X(LmrNoisyDiv,    3277,  1000,   6000)      \
    X(LmrCutNode,     2048,     0,   4096)      \
    X(LmrHistDiv,    24576,  2048,  65536)      \
    X(LmrCaptHistDiv, 8192,  1024,  32768)      \
    X(IirDepth,          4,     2,      8)      \
    X(SingularDepth,     8,     4,     12)      \
    X(SingularMargin,   32,     8,    128)      \
    X(FpDepth,           8,     2,     12)      \
    X(FpMargin,        150,    40,    400)      \
    X(HistPruneDepth,    6,     2,     10)      \
    X(HistPruneMargin, 2048,   256,   8192)     \
    X(LmpDepth,          8,     4,     12)      \
    X(LmpBase,           3,     1,     10)      \
    X(SeeDepth,          8,     4,     12)      \
    X(SeeQuietMargin,   80,    20,    200)      \
    X(SeeNoisyMargin,   30,    10,    120)      \
    X(HistBonusMul,    300,   100,    600)      \
    X(HistBonusSub,    250,     0,    600)      \
    X(HistBonusMax,   2400,  1000,   4000)

namespace rogatia::tunable {

#define ROGATIA_DECLARE(name, def, lo, hi) extern int name;
ROGATIA_TUNABLES(ROGATIA_DECLARE)
#undef ROGATIA_DECLARE

// Writes one `option name ... type spin ...` line per parameter.
void print_options();

// Clamps into the declared range.  False when `name` is not a tunable, which
// lets the caller fall through to its own options.
bool set_option(const std::string& name, int value);

}  // namespace rogatia::tunable

#endif  // ROGATIA_TUNABLE_H
