// Rogatia chess engine -- tunable search constants.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tunable.h"

#include <algorithm>
#include <cstdio>

namespace rogatia::tunable {

#define ROGATIA_DEFINE(name, def, lo, hi) int name = def;
ROGATIA_TUNABLES(ROGATIA_DEFINE)
#undef ROGATIA_DEFINE

void print_options() {
#define ROGATIA_PRINT(name, def, lo, hi) \
    std::printf("option name %s type spin default %d min %d max %d\n", #name, def, lo, hi);
    ROGATIA_TUNABLES(ROGATIA_PRINT)
#undef ROGATIA_PRINT
}

bool set_option(const std::string& optName, int value) {
#define ROGATIA_SET(name, def, lo, hi)              \
    if (optName == #name) {                         \
        name = std::clamp(value, lo, hi);           \
        return true;                                \
    }
    ROGATIA_TUNABLES(ROGATIA_SET)
#undef ROGATIA_SET
    return false;
}

}  // namespace rogatia::tunable
