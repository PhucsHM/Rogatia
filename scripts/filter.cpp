// Rogatia -- training corpus filter.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads bulletformat shards, drops the records that should never have been
// written, removes duplicate positions, and thins what is left down to a target
// count.  Writes one output shard.
//
//   filter <out.bin> <max-score> <target-millions> <in.bin> [in.bin ...]
//
// THREE FILTERS, in this order.
//
// 1. Score outliers.  Until 2026-07-29 the datagen loop ended a game on
//    is_mate_score(), which is deliberately FALSE for tablebase scores -- so a
//    tablebase score fell through to the quiet filter and was written as a
//    label.  snapshot() clamps to +-30000, so the poison signature is a label
//    near +-30000, roughly 75 pawns.  Nothing legitimate lives up there: the
//    game adjudicates at +-2000 held for four plies, so real labels sit well
//    inside that.
//
// 2. Duplicate positions.  Keyed on the 24 bytes that define the position --
//    the occupancy word and the packed piece list.  The score and result are
//    NOT part of the key, so the first occurrence wins and later ones go,
//    whatever they were labelled.  A 64-bit hash in an open-addressed table;
//    at 473M positions that is an 8 GB table, which the training box has.
//
// 3. Thinning.  Consecutive plies of one game are nearly the same position, so
//    a corpus of every ply carries far less information than its size suggests.
//    Keeping every Nth survivor spaces them out.  This is a blunt instrument --
//    the flat file has no game boundaries to respect -- but it decorrelates,
//    and it is what turns 473M correlated positions into ~150M useful ones.
//
// Two passes over the input: the first counts survivors so the stride is exact,
// the second writes.  The dedupe table is rebuilt identically in pass two, so
// both passes see the same survivors in the same order.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr int REC = 32;      // bytes per bulletformat record
constexpr int KEY = 24;      // occ(8) + pcs(16); score/result deliberately excluded
constexpr int SCORE_OFF = 24;

// splitmix64, the same mixer the engine uses for Zobrist keys.
std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

std::uint64_t key_of(const unsigned char* r) {
    std::uint64_t h = 0;
    for (int i = 0; i < KEY; i += 8) {
        std::uint64_t w;
        std::memcpy(&w, r + i, 8);
        h = mix(h ^ w);
    }
    return h ? h : 1;   // 0 is the empty sentinel
}

// Open addressing, linear probing, power-of-two capacity.
struct Seen {
    std::vector<std::uint64_t> slot;
    std::uint64_t              mask;

    explicit Seen(std::uint64_t cap) {
        std::uint64_t n = 1024;
        while (n < cap * 2)
            n <<= 1;
        slot.assign(n, 0);
        mask = n - 1;
    }

    // True if this key was already present.
    bool insert(std::uint64_t h) {
        std::uint64_t i = h & mask;
        while (slot[i]) {
            if (slot[i] == h)
                return true;
            i = (i + 1) & mask;
        }
        slot[i] = h;
        return false;
    }

    void reset() { std::fill(slot.begin(), slot.end(), std::uint64_t(0)); }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: %s <out.bin> <max-score> <target-millions> <in.bin> [in.bin ...]\n"
                     "  max-score        drop |label| above this (5000 is a good default)\n"
                     "  target-millions  0 keeps everything that survives filters 1 and 2\n",
                     argv[0]);
        return 2;
    }

    const char* outPath  = argv[1];
    const int   maxScore = std::atoi(argv[2]);
    const std::uint64_t target =
        std::uint64_t(std::strtoull(argv[3], nullptr, 10)) * 1000000ULL;
    const int firstIn = 4;

    // Size the table from the total input, so pass one never rehashes.
    std::uint64_t totalRecs = 0;
    for (int a = firstIn; a < argc; ++a) {
        std::FILE* f = std::fopen(argv[a], "rb");
        if (!f) {
            std::fprintf(stderr, "cannot open %s\n", argv[a]);
            return 1;
        }
        std::fseek(f, 0, SEEK_END);
        const long long sz = std::ftell(f);
        std::fclose(f);
        // A trailing partial record is expected, not an error: datagen may still
        // be appending to this shard.  Round down to the last whole record and
        // say so, rather than refusing to run on a live directory.
        if (sz % REC)
            std::fprintf(stderr, "note       %s ends mid-record (%lld bytes); ignoring the tail\n",
                         argv[a], sz % REC);
        totalRecs += std::uint64_t(sz / REC);
    }
    std::fprintf(stderr, "input      %llu positions in %d shards\n",
                 (unsigned long long) totalRecs, argc - firstIn);

    Seen seen(totalRecs ? totalRecs : 1024);
    std::fprintf(stderr, "dedupe     %.1f GB table\n", double(seen.slot.size() * 8) / 1e9);

    std::vector<unsigned char> buf(REC * 65536);

    // ---- pass 1: count what survives the score filter and the dedupe ----
    std::uint64_t dropScore = 0, dropDup = 0, survivors = 0;
    for (int pass = 1; pass <= 2; ++pass) {
        if (pass == 2) {
            seen.reset();
            dropScore = dropDup = 0;
        }

        // Stride is computed between the passes, once survivors is known.
        std::uint64_t stride = 1;
        if (pass == 2 && target && survivors > target)
            stride = survivors / target;

        std::FILE* out = nullptr;
        if (pass == 2) {
            out = std::fopen(outPath, "wb");
            if (!out) {
                std::fprintf(stderr, "cannot write %s\n", outPath);
                return 1;
            }
        }

        std::uint64_t kept = 0, seenSoFar = 0;
        for (int a = firstIn; a < argc; ++a) {
            std::FILE* f = std::fopen(argv[a], "rb");
            if (!f)
                return 1;
            std::size_t n;
            while ((n = std::fread(buf.data(), 1, buf.size(), f)) >= REC) {
                for (std::size_t o = 0; o + REC <= n; o += REC) {
                    unsigned char* r = buf.data() + o;

                    std::int16_t score;
                    std::memcpy(&score, r + SCORE_OFF, 2);
                    if (score > maxScore || score < -maxScore) {
                        ++dropScore;
                        continue;
                    }
                    if (seen.insert(key_of(r))) {
                        ++dropDup;
                        continue;
                    }

                    if (pass == 1)
                        ++survivors;
                    else if (seenSoFar++ % stride == 0) {
                        std::fwrite(r, 1, REC, out);
                        ++kept;
                    }
                }
            }
            std::fclose(f);
        }

        if (pass == 1) {
            std::fprintf(stderr,
                         "pass 1     %llu dropped on score, %llu duplicates, %llu survive\n",
                         (unsigned long long) dropScore, (unsigned long long) dropDup,
                         (unsigned long long) survivors);
            if (target && survivors > target)
                std::fprintf(stderr, "thinning   keeping 1 in %llu to reach %llu\n",
                             (unsigned long long) (survivors / target),
                             (unsigned long long) target);
        } else {
            std::fclose(out);
            std::fprintf(stderr, "written    %llu positions to %s\n",
                         (unsigned long long) kept, outPath);
        }
    }
    return 0;
}
