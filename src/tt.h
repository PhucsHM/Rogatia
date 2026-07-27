// Rogatia chess engine -- transposition table.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_TT_H
#define ROGATIA_TT_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "eval.h"
#include "types.h"

namespace rogatia {

enum Bound : std::uint8_t {
    BOUND_NONE  = 0,
    BOUND_UPPER = 1,  // score is an upper bound: the real score is <= this
    BOUND_LOWER = 2,  // score is a lower bound: the real score is >= this
    BOUND_EXACT = BOUND_UPPER | BOUND_LOWER
};

// What a probe hands back, already unpacked and already corrected for ply.
struct TTData {
    Move  move  = MOVE_NONE;
    Score score = VALUE_NONE;
    Score eval  = VALUE_NONE;
    int   depth = 0;
    Bound bound = BOUND_NONE;
    bool  pv    = false;
};

// 10 bytes.  genBound8 packs: bits 0-1 bound, bit 2 was-a-PV-node,
// bits 3-7 generation (32 values, wrapping).
struct TTEntry {
    std::uint16_t key16;
    std::uint16_t move16;
    std::int16_t  score16;
    std::int16_t  eval16;
    std::int8_t   depth8;
    std::uint8_t  genBound8;
};

static_assert(sizeof(TTEntry) == 10, "TTEntry must stay packed");

// Six entries share one cache line, so a probe is a single memory access.
constexpr int TT_BUCKET_SIZE = 6;

struct alignas(64) TTBucket {
    TTEntry entry[TT_BUCKET_SIZE];
    char    pad[64 - TT_BUCKET_SIZE * sizeof(TTEntry)];
};

static_assert(sizeof(TTBucket) == 64, "one bucket, one cache line");

class TranspositionTable {
public:
    // Rounded down to a whole number of buckets.  Clears the table.
    void resize(std::size_t mb);
    void clear();

    // Bumped once per `go`, so entries from earlier searches lose replacement
    // priority without being wiped.
    void new_search() { generation_ = (generation_ + 1) & GEN_MASK; }

    bool probe(Key key, int ply, TTData& out) const;
    void store(Key key, int ply, int depth, Bound bound, Move move, Score score, Score eval,
               bool pv);

    void prefetch(Key key) const;

    // Per mille of entries written by the current search, for `info hashfull`.
    int hashfull() const;

private:
    static constexpr unsigned GEN_MASK  = 31;
    static constexpr int      GEN_SHIFT = 3;

    // Fibonacci-hashed high bits: index depends only on the key and the bucket
    // count, never on the allocation address.  Bench determinism depends on it.
    std::size_t index_of(Key key) const {
        return std::size_t((__uint128_t(key) * __uint128_t(buckets_.size())) >> 64);
    }

    std::vector<TTBucket> buckets_;
    unsigned              generation_ = 0;
};

extern TranspositionTable TT;

}  // namespace rogatia

#endif  // ROGATIA_TT_H
