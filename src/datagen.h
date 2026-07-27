// Rogatia chess engine -- self-play data generation.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_DATAGEN_H
#define ROGATIA_DATAGEN_H

#include <cstdint>
#include <string>

namespace rogatia::datagen {

struct Config {
    std::string   output;
    std::uint64_t positions = 0;  // stop once this many have been written
    std::uint64_t seed      = 1;
    std::uint64_t nodes     = 5000;  // soft node limit per move
};

// Plays self-play games until `positions` labelled positions are written, then
// returns.  Single-threaded on purpose -- see the note in datagen.cpp.
void run(const Config& cfg);

}  // namespace rogatia::datagen

#endif  // ROGATIA_DATAGEN_H
