// Rogatia chess engine -- Zobrist key generation.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "zobrist.h"

namespace rogatia {

namespace zobrist {
Key Psq[PIECE_NB][SQUARE_NB][KEY_SET_NB];
Key Castling[CASTLING_RIGHT_NB];
Key EnPassant[FILE_NB];
Key Side;
}  // namespace zobrist

namespace {

// splitmix64, hardcoded seed.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : s_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (s_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

private:
    std::uint64_t s_;
};

}  // namespace

void init_zobrist() {
    SplitMix64 rng(0x524F474154494100ULL);  // "ROGATIA\0"

    // The LOOP NEST IS THE WIRE FORMAT.  Psq is piece-major in memory now, but
    // the draw order must stay set-major or every key in the engine changes --
    // and with them every TT hit, every bench count, and the perft the project
    // gates on.  Transposing the storage is free only as long as this nest is
    // left exactly as it is: set outside, then piece, then square.
    for (int set = 0; set < KEY_SET_NB; ++set)
        for (int pc = 0; pc < PIECE_NB; ++pc)
            for (int s = 0; s < SQUARE_NB; ++s)
                zobrist::Psq[pc][s][set] = rng.next();

    for (int cr = 0; cr < CASTLING_RIGHT_NB; ++cr)
        zobrist::Castling[cr] = rng.next();

    for (int f = 0; f < FILE_NB; ++f)
        zobrist::EnPassant[f] = rng.next();

    zobrist::Side = rng.next();
}

}  // namespace rogatia
