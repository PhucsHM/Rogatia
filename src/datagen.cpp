// Rogatia chess engine -- self-play data generation.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plays games against itself at a fixed node budget and writes the quiet
// positions in bulletformat, which is what the trainer consumes directly.
//
// ponytail: single-threaded, one process per core.  The searcher is a single
// global Worker (see search.cpp) and datagen is embarrassingly parallel, so N
// processes with different seeds beat making the engine thread-safe -- which is
// Phase 7's job anyway.  `scripts/datagen.sh` launches the fleet.
#include "datagen.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "eval.h"
#include "movegen.h"
#include "position.h"
#include "search.h"
#include "tt.h"

// Fathom (jdart1/Fathom, MIT) vendored verbatim in src/fathom.  Compiled as
// C++ because it happens to be valid C++20; nothing in it was edited.
#include "tbprobe.h"

namespace rogatia::datagen {

namespace {

// ------------------------------------------------------------ bulletformat --

// The trainer's own 32-byte record, laid out exactly as `bulletformat`'s
// ChessBoard (jw1912/bulletformat, MIT).  Verified against src/chess.rs rather
// than reconstructed from memory: the board is stored ALREADY FLIPPED to the
// side to move, which is why there is no side-to-move field.
struct BulletBoard {
    std::uint64_t occ;
    std::uint8_t  pcs[16];
    std::int16_t  score;   // centipawns, side-to-move relative after the flip
    std::uint8_t  result;  // 0 = stm loses, 1 = draw, 2 = stm wins
    std::uint8_t  ksq;
    std::uint8_t  oppKsq;
    std::uint8_t  extra[3];
};
static_assert(sizeof(BulletBoard) == 32, "bulletformat records are 32 bytes");

// Everything needed to emit a record once the game's result is known.  The
// result cannot be applied earlier: flipping to the black point of view mirrors
// the result and negates the score together.
struct Sample {
    Bitboard     bbs[8];  // white, black, pawn, knight, bishop, rook, queen, king
    std::uint8_t stm;
    std::int16_t whiteScore;
};

Sample snapshot(const Position& pos, int whiteScore) {
    Sample s;
    s.bbs[0] = pos.pieces(WHITE);
    s.bbs[1] = pos.pieces(BLACK);
    for (PieceType pt = PAWN; pt <= KING; pt = PieceType(pt + 1))
        s.bbs[1 + int(pt)] = pos.pieces(pt);
    s.stm        = std::uint8_t(pos.side_to_move());
    s.whiteScore = std::int16_t(std::clamp(whiteScore, -30000, 30000));
    return s;
}

// `whiteResult` is 0 (black won), 1 (draw) or 2 (white won).
BulletBoard pack(Sample s, int whiteResult) {
    int score  = s.whiteScore;
    int result = whiteResult;

    if (s.stm == 1) {
        // A vertical flip, because squares are rank-major with A1 in the low
        // byte: byte-swapping a bitboard mirrors rank 1 onto rank 8.
        for (Bitboard& bb : s.bbs)
            bb = __builtin_bswap64(bb);
        std::swap(s.bbs[0], s.bbs[1]);
        score  = -score;
        result = 2 - result;
    }

    BulletBoard b{};
    b.occ    = s.bbs[0] | s.bbs[1];
    b.score  = std::int16_t(score);
    b.result = std::uint8_t(result);
    b.ksq    = std::uint8_t(lsb(s.bbs[0] & s.bbs[7]));
    b.oppKsq = std::uint8_t(lsb(s.bbs[1] & s.bbs[7]) ^ 56);

    // One nibble per occupied square, in ascending square order.
    int      idx = 0;
    Bitboard occ = b.occ;
    while (occ) {
        const Square   sq  = lsb(occ);
        const Bitboard bit = square_bb(sq);
        occ &= occ - 1;

        std::uint8_t pc = (bit & s.bbs[1]) ? 8 : 0;
        for (int p = 2; p < 8; ++p)
            if (bit & s.bbs[p]) {
                pc |= std::uint8_t(p - 2);  // pawn = 0 ... king = 5
                break;
            }

        b.pcs[idx / 2] |= std::uint8_t(pc << (4 * (idx & 1)));
        ++idx;
    }
    return b;
}

// -------------------------------------------------------------------- rng --

// splitmix64.  Seeded per process so a fleet of workers never overlaps, and
// deterministic so a suspicious game can be replayed exactly.
struct Rng {
    std::uint64_t state;

    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z               = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    int below(int n) { return int(next() % std::uint64_t(n)); }
};

// ------------------------------------------------------------------ tuning --

constexpr int RANDOM_PLIES = 8;

// A random opening that is already lopsided teaches the net about positions the
// search would never reach.  Discard and reseed instead.
constexpr int OPENING_REJECT = 1000;

// Adjudication: a score this large, held this many plies by the same side, is
// not going to be given back at 5000 nodes.
constexpr int ADJUDICATE_SCORE = 2000;
constexpr int ADJUDICATE_PLIES = 4;

// The quiet filter from the roadmap: if the quiescence search moves the score
// this far, the position has tactics in it and its static label would be a lie.
constexpr int QUIET_MARGIN = 60;

constexpr int MAX_GAME_PLIES = 400;

constexpr std::size_t DATAGEN_HASH_MB = 16;

// ----------------------------------------------------------------- helpers --

int count_legal(Position& pos) {
    int n = 0;
    for (Move m : MoveList<ALL>(pos))
        n += pos.is_legal(m);
    return n;
}

// Syzygy hard adjudication.  Returns 0/1/2 white-relative, or -1 when the
// position cannot be probed -- which is most of them: Fathom's WDL probe
// refuses anything with castling rights or a non-zero fifty-move counter, so
// in practice this fires on the ply right after the capture that reached the
// tablebase.  That is exactly when it is worth firing.
int probe_syzygy(const Position& pos) {
    if (TB_LARGEST == 0 || unsigned(popcount(pos.pieces())) > TB_LARGEST)
        return -1;
    if (pos.castling_rights() != NO_CASTLING || pos.rule50_count() != 0)
        return -1;

    const unsigned wdl = tb_probe_wdl(
        pos.pieces(WHITE), pos.pieces(BLACK), pos.pieces(KING), pos.pieces(QUEEN),
        pos.pieces(ROOK), pos.pieces(BISHOP), pos.pieces(KNIGHT), pos.pieces(PAWN),
        0, 0, pos.ep_square() == SQ_NONE ? 0u : unsigned(pos.ep_square()),
        pos.side_to_move() == WHITE);

    if (wdl == TB_RESULT_FAILED)
        return -1;

    // A cursed win and a blessed loss are both draws under the fifty-move rule,
    // which is the rule the games are actually played under.
    int stmResult;
    if (wdl == TB_WIN)
        stmResult = 2;
    else if (wdl == TB_LOSS)
        stmResult = 0;
    else
        stmResult = 1;

    return (pos.side_to_move() == WHITE) ? stmResult : 2 - stmResult;
}

Move random_legal(Position& pos, Rng& rng) {
    Move legal[MAX_MOVES];
    int  n = 0;
    for (Move m : MoveList<ALL>(pos))
        if (pos.is_legal(m))
            legal[n++] = m;
    return n ? legal[rng.below(n)] : MOVE_NONE;
}

}  // namespace

void run(const Config& cfg) {
    std::FILE* out = std::fopen(cfg.output.c_str(), "ab");
    if (!out) {
        std::fprintf(stderr, "datagen: cannot open %s\n", cfg.output.c_str());
        return;
    }

    TT.resize(DATAGEN_HASH_MB);

    // Path comes from the environment rather than another positional argument:
    // it is the same for every worker in a fleet and never varies per run.
    if (const char* tbPath = std::getenv("SYZYGY_PATH")) {
        if (tb_init(tbPath) && TB_LARGEST > 0)
            std::fprintf(stderr, "syzygy: %u-piece tablebases from %s\n", TB_LARGEST, tbPath);
        else
            std::fprintf(stderr, "syzygy: no tablebases found at %s -- eval adjudication only\n",
                         tbPath);
    }

    Rng rng{cfg.seed};

    std::uint64_t written = 0;
    std::uint64_t games   = 0;
    std::vector<Sample>      samples;
    std::vector<BulletBoard> batch;

    const auto start = std::chrono::steady_clock::now();

    Position pos;

    while (written < cfg.positions) {
        // ---------------------------------------------------- new game --
        search::clear();  // wipes TT and every history table
        pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        samples.clear();

        bool usable = true;
        for (int i = 0; i < RANDOM_PLIES; ++i) {
            const Move m = random_legal(pos, rng);
            if (m == MOVE_NONE) {  // random walk into mate or stalemate
                usable = false;
                break;
            }
            pos.make_move(m);
        }
        if (!usable || count_legal(pos) == 0)
            continue;

        // A cheap look before committing a whole game to the random opening.
        const search::Result probe = search::search_fixed_nodes(pos, cfg.nodes);
        if (probe.best == MOVE_NONE || probe.score == VALUE_NONE
            || std::abs(probe.score) > OPENING_REJECT)
            continue;

        // ------------------------------------------------------- play --
        int whiteResult  = 1;  // draw unless something below says otherwise
        int decisive     = 0;  // plies in a row above the adjudication score
        int decisiveSide = 0;  // which side those plies favoured: +1 white, -1 black

        for (int ply = 0; ply < MAX_GAME_PLIES; ++ply) {
            if (pos.is_game_draw())
                break;

            // Hard adjudication: a tablebase result is the truth, and playing
            // the ending out generates hundreds of positions that teach the net
            // nothing it cannot get from the tablebase.
            const int tb = probe_syzygy(pos);
            if (tb >= 0) {
                whiteResult = tb;
                break;
            }

            if (count_legal(pos) == 0) {
                // Checkmate is a loss for the side to move; stalemate is a draw.
                if (pos.in_check())
                    whiteResult = (pos.side_to_move() == WHITE) ? 0 : 2;
                break;
            }

            const search::Result r = search::search_fixed_nodes(pos, cfg.nodes);
            // No completed iteration means no score to label with -- the node
            // budget is too small for this position.  Abandon the game rather
            // than write a position labelled with a stale score.
            if (r.best == MOVE_NONE || r.score == VALUE_NONE) {
                // Abandon means abandon: the samples already collected have no
                // result to carry, and emitting them would label the whole game
                // with the default draw -- a fabricated result, which is worse
                // than the stale score this check exists to prevent.
                samples.clear();
                break;
            }

            const Color us         = pos.side_to_move();
            const int   whiteScore = (us == WHITE) ? r.score : -r.score;

            // A decisive score ends the game now: playing it out adds nothing
            // and its positions are not the kind the net needs labelled.
            //
            // This MUST test the union of the mate and tablebase bands.  With
            // is_mate_score() alone a tablebase score -- deliberately invisible
            // to that test -- fell through to the quiet filter below and was
            // written as a training label of about 31753, roughly 79 pawns.
            // Datagen calls tb_init(), so the search really can return one:
            // the root probe needs five men or fewer, while the search's own
            // WDL probe fires on any child that enters the table.
            if (is_decisive(r.score)) {
                whiteResult = (whiteScore > 0) ? 2 : 0;
                break;
            }

            // ------------------------------------------ quiet filter --
            const bool  inCheck   = pos.in_check();
            const Score staticEval = inCheck ? VALUE_NONE : evaluate(pos);
            const bool  noisy      = inCheck
                                  || std::abs(staticEval - search::qsearch_eval(pos)) > QUIET_MARGIN;
            if (!noisy)
                samples.push_back(snapshot(pos, whiteScore));

            // ------------------------------------------ adjudication --
            // The same side has to hold the advantage throughout.  A score that
            // swings between +2000 and -2000 is a position the search cannot
            // resolve, not a won game -- adjudicating it on the last ply's sign
            // labels every sample in that game with a coin flip.
            if (std::abs(whiteScore) >= ADJUDICATE_SCORE) {
                const int side = (whiteScore > 0) ? 1 : -1;
                decisive       = (side == decisiveSide) ? decisive + 1 : 1;
                decisiveSide   = side;
            } else {
                decisive     = 0;
                decisiveSide = 0;
            }
            if (decisive >= ADJUDICATE_PLIES) {
                whiteResult = (decisiveSide > 0) ? 2 : 0;
                break;
            }

            pos.make_move(r.best);
        }

        // ------------------------------------------------------ emit --
        ++games;
        batch.clear();
        batch.reserve(samples.size());
        for (const Sample& s : samples)
            batch.push_back(pack(s, whiteResult));

        if (!batch.empty())
            std::fwrite(batch.data(), sizeof(BulletBoard), batch.size(), out);
        written += batch.size();

        if ((games & 63) == 0) {
            const double secs =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::fprintf(stderr, "\r%llu positions, %llu games, %.0f pos/s",
                         (unsigned long long) written, (unsigned long long) games,
                         double(written) / std::max(secs, 1e-9));
            std::fflush(stderr);
        }
    }

    std::fclose(out);

    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::fprintf(stderr, "\n%llu positions from %llu games in %.1fs (%.0f pos/s) -> %s\n",
                 (unsigned long long) written, (unsigned long long) games, secs,
                 double(written) / std::max(secs, 1e-9), cfg.output.c_str());
}

}  // namespace rogatia::datagen
