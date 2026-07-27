// Rogatia chess engine -- transposition table.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tt.h"

#include <algorithm>

namespace rogatia {

TranspositionTable TT;

namespace {

// A mate score is "mate in N plies from HERE".  The table is shared by every
// ply, so it has to store the distance from the mate instead and convert back
// on the way out.  Getting this wrong makes the engine announce mates it
// cannot deliver.
Score score_to_tt(Score s, int ply) {
    if (s >= VALUE_MATE_IN_MAX_PLY)
        return s + ply;
    if (s <= -VALUE_MATE_IN_MAX_PLY)
        return s - ply;
    return s;
}

Score score_from_tt(Score s, int ply) {
    if (s >= VALUE_MATE_IN_MAX_PLY)
        return s - ply;
    if (s <= -VALUE_MATE_IN_MAX_PLY)
        return s + ply;
    return s;
}

}  // namespace

void TranspositionTable::resize(std::size_t mb) {
    const std::size_t bytes   = std::max<std::size_t>(mb, 1) * 1024 * 1024;
    const std::size_t buckets = std::max<std::size_t>(bytes / sizeof(TTBucket), 1);

    buckets_.assign(buckets, TTBucket{});
    generation_ = 0;
}

void TranspositionTable::clear() {
    std::fill(buckets_.begin(), buckets_.end(), TTBucket{});
    generation_ = 0;
}

void TranspositionTable::prefetch(Key key) const {
    if (!buckets_.empty())
        __builtin_prefetch(&buckets_[index_of(key)]);
}

bool TranspositionTable::probe(Key key, int ply, TTData& out) const {
    if (buckets_.empty())
        return false;

    const TTBucket&     bucket = buckets_[index_of(key)];
    const std::uint16_t key16  = std::uint16_t(key);

    for (const TTEntry& e : bucket.entry) {
        // genBound8 == 0 in every bit of the bound field means "never written",
        // which keeps a zeroed table from matching key16 == 0.
        if (e.key16 != key16 || (e.genBound8 & 3) == 0)
            continue;

        out.move  = Move(e.move16);
        out.score = score_from_tt(Score(e.score16), ply);
        out.eval  = Score(e.eval16);
        out.depth = e.depth8;
        out.bound = Bound(e.genBound8 & 3);
        out.pv    = (e.genBound8 & 4) != 0;
        return true;
    }
    return false;
}

void TranspositionTable::store(Key key, int ply, int depth, Bound bound, Move move, Score score,
                               Score eval, bool pv) {
    if (buckets_.empty())
        return;

    TTBucket&           bucket = buckets_[index_of(key)];
    const std::uint16_t key16  = std::uint16_t(key);

    TTEntry* replace = nullptr;
    int      worst   = 0;

    for (TTEntry& e : bucket.entry) {
        if ((e.genBound8 & 3) == 0) {  // free slot, take it
            replace = &e;
            break;
        }
        if (e.key16 == key16) {  // same position always wins
            replace = &e;
            // A shallower result for a position we already know deeper is only
            // worth keeping if it is exact; otherwise keep what is there and
            // just refresh the generation so it survives another search.
            if (bound != BOUND_EXACT && depth + 4 <= e.depth8) {
                e.genBound8 = std::uint8_t((generation_ << GEN_SHIFT) | (e.genBound8 & 7));
                return;
            }
            break;
        }

        // Depth is what makes an entry valuable; age is what makes it stale.
        // The factor 2 says one search generation costs about two plies.
        const int age   = int((GEN_MASK + 1 + generation_ - (e.genBound8 >> GEN_SHIFT)) & GEN_MASK);
        const int worth = e.depth8 - age * 2;
        if (!replace || worth < worst) {
            replace = &e;
            worst   = worth;
        }
    }

    // An entry with no move is worse than a stale one that has one.
    if (move == MOVE_NONE && replace->key16 == key16)
        move = Move(replace->move16);

    replace->key16     = key16;
    replace->move16    = std::uint16_t(move);
    replace->score16   = std::int16_t(score_to_tt(score, ply));
    replace->eval16    = std::int16_t(eval);
    replace->depth8    = std::int8_t(std::clamp(depth, -1, 127));
    replace->genBound8 = std::uint8_t((generation_ << GEN_SHIFT) | (pv ? 4 : 0) | bound);
}

int TranspositionTable::hashfull() const {
    if (buckets_.empty())
        return 0;

    // Sample the first 1000 entries; an exact count would walk the whole table.
    int used = 0, seen = 0;
    for (const TTBucket& b : buckets_) {
        for (const TTEntry& e : b.entry) {
            if ((e.genBound8 & 3) != 0 && (e.genBound8 >> GEN_SHIFT) == generation_)
                ++used;
            if (++seen == 1000)
                return used;
        }
    }
    return seen ? used * 1000 / seen : 0;
}

}  // namespace rogatia
