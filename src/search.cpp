// Rogatia chess engine -- search.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fail-soft negamax with PVS, iterative deepening, aspiration windows, a
// quiescence search and a transposition table, plus the Phase 4 pruning set:
// reverse futility pruning, null move, late move reductions, late move
// pruning, SEE pruning and continuation history.  Extensions and singular
// search are Phase 7.
#include "search.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "movegen.h"
#include "nnue.h"
#include "perft.h"  // move_to_uci
#include "tbprobe.h"
#include "tt.h"
#include "tunable.h"

namespace rogatia::search {

namespace {

using Clock = std::chrono::steady_clock;

// ----------------------------------------------------------- move ordering --

constexpr int SCORE_TT      = 1 << 24;
constexpr int SCORE_GOODCAP = 1 << 20;
constexpr int SCORE_KILLER1 = 1 << 19;
constexpr int SCORE_KILLER2 = SCORE_KILLER1 - 1;
constexpr int SCORE_BADCAP  = -(1 << 20);

// History is a running average, so it has to be bounded or the gravity term
// stops pulling.  Well below SCORE_KILLER2 so a killer always outranks it.
constexpr int MAX_HISTORY = 16384;

constexpr int MAX_QUIETS_TRACKED = 32;

// ---------------------------------------------- correction history ---------

// The search routinely disagrees with the static evaluation, and the
// disagreement is not random -- it correlates with structural features the
// evaluation is systematically weak on.  Record the running difference keyed on
// those features and apply it the next time the same structure appears.
//
// This only became worth doing once NNUE existed.  Against the piece-square
// tables the residual was large and unstructured, and the table would have
// learned noise; against a network it is small and has shape.
constexpr int CORRHIST_SIZE  = 16384;                 // power of two, masked
constexpr int CORRHIST_GRAIN = 256;                   // table units per centipawn
constexpr int CORRHIST_MAX   = CORRHIST_GRAIN * 32;   // +/- 32 cp of authority

// ------------------------------------------------------------------- LMR ---

// Reductions are carried in 1/1024 of a ply so that the adjustments below can
// be fractional without floating point anywhere on the search path.
constexpr int LMR_SCALE = 1024;

// 1024 * ln(i).  Hardcoded rather than computed from std::log at startup: the
// bench node count has to be identical across machines and libm is not
// guaranteed to round the same way everywhere.
constexpr int Ln[64] = {
       0,    0,  710, 1125, 1420, 1648, 1835, 1993,
    2129, 2250, 2358, 2455, 2545, 2627, 2702, 2773,
    2839, 2901, 2960, 3015, 3068, 3118, 3165, 3211,
    3254, 3296, 3336, 3375, 3412, 3448, 3483, 3516,
    3549, 3580, 3611, 3641, 3670, 3698, 3725, 3751,
    3777, 3803, 3827, 3851, 3875, 3898, 3921, 3943,
    3964, 3985, 4006, 4026, 4046, 4066, 4085, 4104,
    4122, 4140, 4158, 4175, 4193, 4210, 4226, 4243,
};

// base + ln(depth)*ln(moveCount)/divisor, indexed [isNoisy][depth][moveCount].
// A capture or promotion is reduced far less: it changes the material balance,
// so a wrong guess about it is expensive.
int lmr_base(bool noisy, int depth, int moveCount) {
    const int product = Ln[std::min(depth, 63)] * Ln[std::min(moveCount, 63)];
    return noisy ? tunable::LmrNoisyBase + product / tunable::LmrNoisyDiv
                 : tunable::LmrQuietBase + product / tunable::LmrQuietDiv;
}

// ------------------------------------------------------------ search state --

struct Stack {
    int   ply         = 0;
    Move  killers[2]  = {MOVE_NONE, MOVE_NONE};
    Score staticEval  = VALUE_NONE;
    Move  currentMove = MOVE_NONE;
    // The piece moved at this ply and where it landed -- the key continuation
    // history is indexed by.  NO_PIECE means "no real move here" (the plies
    // below the root, and a null move), which suppresses the lookup.
    Piece  movedPiece = NO_PIECE;
    Square movedTo    = SQ_A1;

    // Set only while proving whether the TT move is singular.  That proof is a
    // search of this same position at this same ply, so it has to be told which
    // move it is not allowed to see.
    Move   excludedMove = MOVE_NONE;

    // The NNUE accumulator for the position at this ply, carried down the tree
    // incrementally rather than rebuilt.  Unused when no network is loaded.
    nnue::Accumulator acc{};

    // Which accumulator is in force at this ply.  Normally &acc, but a null
    // move changes no feature, so its child points back at the PARENT's rather
    // than copying 1 KB to produce a byte-identical object.
    //
    // Only ever read through.  apply_move always writes the child's own `acc`
    // and then repoints, so a shared parent buffer is never written.
    const nnue::Accumulator* accPtr = nullptr;
};

// The root sits at stack[ROOT_OFFSET], not stack[0]: "improving" reads
// (ss-2)->staticEval and continuation history reaches (ss-2) as well, so the
// plies below the root have to exist as real, zero-initialised entries rather
// than as whatever precedes the array.
constexpr int ROOT_OFFSET = 4;

std::atomic<bool> Stopped{false};

struct Worker {
    std::uint64_t nodes    = 0;
    std::uint64_t tbHits   = 0;
    int           seldepth = 0;

    Move  rootBestMove = MOVE_NONE;
    Score rootScore    = VALUE_NONE;

    std::int16_t history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    // [piece moved N plies ago][where it landed][piece moved now][where it
    // lands].  4 MB, so it lives in the Worker rather than on the stack.
    std::int16_t contHist[PIECE_NB][SQUARE_NB][PIECE_NB][SQUARE_NB] = {};

    // [side to move][feature hash].  Pawn structure and own non-pawn material
    // are the two things the evaluation is most often wrong about in a
    // consistent direction.  The five Zobrist key sets exist for this.
    // int16, not int.  Every slot is clamped to +-CORRHIST_MAX == 8192, which
    // fits with 2 bits to spare, and the pair of tables is walked by a hash of
    // the position -- a scattered access with no locality to lose.  128 KB
    // instead of 256 KB is the difference between fitting a core's L2 beside
    // everything else the search wants there and not.
    std::int16_t pawnCorr[COLOR_NB][CORRHIST_SIZE]    = {};
    std::int16_t nonPawnCorr[COLOR_NB][CORRHIST_SIZE] = {};

    Move pv[MAX_PLY][MAX_PLY] = {};
    int  pvLen[MAX_PLY]       = {};

    Stack stack[MAX_PLY + ROOT_OFFSET + 4];

    // Time control.  All integer milliseconds; nothing on this path may be
    // floating point or the bench stops being reproducible.
    Clock::time_point start;
    bool              useTime   = false;
    std::int64_t      softLimit = 0;
    std::int64_t      hardLimit = 0;
    std::uint64_t     nodeLimit = 0;
    bool              quiet     = false;  // bench: suppress all output

    std::int64_t elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }
};

// ponytail: one global worker -- the engine is single-threaded and Phase 7
// owns SMP.  Upgrade path is one Worker per thread plus a shared TT.
Worker W;

// Evaluation goes through the accumulator this ply already carries; without a
// network it falls back to the piece-square tables.
Score eval_at(const Position& pos, const Stack* ss) {
    return nnue::loaded() ? nnue::evaluate(pos, *ss->accPtr) : evaluate(pos);
}

// ---------------------------------------------------------------- syzygy ---
// TB_LARGEST is zero until tb_init() succeeds, so an engine that was never
// given a SyzygyPath -- which includes every bench run -- never probes and its
// node count is unchanged.  That is what keeps `bench` comparable across
// machines, and it is the reason nothing here is behind a compile-time flag.

bool tb_in_range(const Position& pos) {
    return TB_LARGEST > 0 && unsigned(popcount(pos.pieces())) <= TB_LARGEST;
}

// Side-to-move relative WDL, or TB_RESULT_FAILED.  A cursed win and a blessed
// loss are draws under the fifty-move rule, which is the rule the game is
// actually played under, so both collapse to a draw here.
unsigned probe_wdl(const Position& pos) {
    // Fathom cannot handle castling rights, and its WDL tables assume the
    // fifty-move counter is zero -- DTZ is what accounts for a running counter,
    // and that is a root-only probe.
    if (pos.castling_rights() != NO_CASTLING || pos.rule50_count() != 0)
        return TB_RESULT_FAILED;

    return tb_probe_wdl(
        pos.pieces(WHITE), pos.pieces(BLACK), pos.pieces(KING), pos.pieces(QUEEN),
        pos.pieces(ROOK), pos.pieces(BISHOP), pos.pieces(KNIGHT), pos.pieces(PAWN),
        0, 0, pos.ep_square() == SQ_NONE ? 0u : unsigned(pos.ep_square()),
        pos.side_to_move() == WHITE);
}

// The DTZ-optimal move in a WON table position, or MOVE_NONE.
//
// Only wins are taken from the table.  Fathom's own documentation warns that
// DTZ suggests unnatural moves in lost positions, where a real search sets
// better practical problems; and in drawn ones the WDL probe inside the search
// already keeps the engine out of trouble.  Winning is the case that needs it,
// because WDL alone reports every winning move as equally winning and gives the
// engine no reason to make progress -- which is how a won position reaches the
// fifty-move rule.  DTZ is a distance to *zeroing*, so following it resets the
// counter by construction.
Move probe_root(const Position& pos) {
    if (!tb_in_range(pos) || pos.castling_rights() != NO_CASTLING)
        return MOVE_NONE;

    const unsigned res = tb_probe_root(
        pos.pieces(WHITE), pos.pieces(BLACK), pos.pieces(KING), pos.pieces(QUEEN),
        pos.pieces(ROOK), pos.pieces(BISHOP), pos.pieces(KNIGHT), pos.pieces(PAWN),
        unsigned(pos.rule50_count()), 0,
        pos.ep_square() == SQ_NONE ? 0u : unsigned(pos.ep_square()),
        pos.side_to_move() == WHITE, nullptr);

    if (res == TB_RESULT_FAILED || res == TB_RESULT_CHECKMATE
        || res == TB_RESULT_STALEMATE || TB_GET_WDL(res) != TB_WIN)
        return MOVE_NONE;

    // Match Fathom's suggestion against our own legal moves rather than
    // decoding its move encoding: a from/to/promotion triple is unambiguous,
    // and anything that fails to match is dropped instead of played.
    // TB_GET_PROMOTES is a 3-bit field, so it can carry 0-7.  Fathom emits only
    // 0-4, but this build is -DNDEBUG so Fathom's own asserts are gone -- size
    // the table to the field, not to the values we expect.
    static constexpr PieceType PromoOf[8] = {NO_PIECE_TYPE, QUEEN, ROOK, BISHOP, KNIGHT,
                                             NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE};
    const unsigned from = TB_GET_FROM(res), to = TB_GET_TO(res);
    const unsigned promo = TB_GET_PROMOTES(res);

    Move  moves[MAX_MOVES];
    Move* end = generate<ALL>(pos, moves);

    for (Move* m = moves; m != end; ++m) {
        if (unsigned(from_sq(*m)) != from || unsigned(to_sq(*m)) != to)
            continue;
        if (promo != TB_PROMOTES_NONE
            && (type_of(*m) != PROMOTION || promotion_type(*m) != PromoOf[promo]))
            continue;
        if (promo == TB_PROMOTES_NONE && type_of(*m) == PROMOTION)
            continue;
        if (pos.is_legal(*m))
            return *m;
    }
    return MOVE_NONE;
}

template<typename T>
void update_history(T& slot, int bonus) {
    bonus = std::clamp(bonus, -MAX_HISTORY, MAX_HISTORY);
    // Gravity: the closer a slot is to the cap the less a new bonus moves it,
    // so a move has to keep earning its rank instead of saturating once.
    slot += bonus - slot * std::abs(bonus) / MAX_HISTORY;
}

int history_bonus(int depth) {
    return std::min(tunable::HistBonusMul * depth - tunable::HistBonusSub,
                    tunable::HistBonusMax);
}

// The continuation-history slot for playing `pc` to `to` after the move made
// at `prev`.  Null when that ply holds no real move.
std::int16_t* cont_hist(const Stack* prev, Piece pc, Square to) {
    if (prev->movedPiece == NO_PIECE)
        return nullptr;
    return &W.contHist[prev->movedPiece][prev->movedTo][pc][to];
}

// Bonus (or malus) to both offsets at once -- they are always updated together.
void update_cont_hist(const Stack* ss, Piece pc, Square to, int bonus) {
    for (int off : {1, 2})
        if (std::int16_t* slot = cont_hist(ss - off, pc, to))
            update_history(*slot, bonus);
}

// ------------------------------------------------- correction history ------

std::size_t corr_index(Key k) { return std::size_t(k) & (CORRHIST_SIZE - 1); }

// Centipawns to add to the raw evaluation of this position.
Score correction(const Position& pos) {
    const Color us = pos.side_to_move();
    const int   c  = W.pawnCorr[us][corr_index(pos.pawn_key())]
                 + W.nonPawnCorr[us][corr_index(pos.non_pawn_key(us))];
    return Score(c / CORRHIST_GRAIN);
}

// Clamped clear of the mate range: every pruning decision above treats a mate
// score as proven, and a correction is a guess.
Score corrected_eval(Score raw, const Position& pos) {
    if (raw == VALUE_NONE)
        return VALUE_NONE;
    // Clamp clear of the DECISIVE range, not just the mate range.
    // VALUE_MATE_IN_MAX_PLY - 1 is exactly VALUE_TB, for which is_decisive() is
    // true -- so the old bound did not clear what the guards now test.
    return std::clamp(Score(raw + correction(pos)), Score(-VALUE_TB_WIN_IN_MAX_PLY + 1),
                      Score(VALUE_TB_WIN_IN_MAX_PLY - 1));
}

void update_corr(std::int16_t& slot, int diff, int depth) {
    // Exponential moving average weighted by depth: a deep search's verdict
    // should move the estimate further than a shallow one's.
    //
    // The arithmetic stays in int: the largest term is 8192 * 255, so the sum
    // cannot reach 2.2 million, and the clamp brings it back inside int16
    // before it is stored.
    const int weight = std::min(depth + 1, 16);
    const int target = std::clamp(diff * CORRHIST_GRAIN, -CORRHIST_MAX, CORRHIST_MAX);
    slot = std::int16_t(std::clamp((slot * (256 - weight) + target * weight) / 256,
                                   -CORRHIST_MAX, CORRHIST_MAX));
}

void update_correction(const Position& pos, int depth, int diff) {
    const Color us = pos.side_to_move();
    update_corr(W.pawnCorr[us][corr_index(pos.pawn_key())], diff, depth);
    update_corr(W.nonPawnCorr[us][corr_index(pos.non_pawn_key(us))], diff, depth);
}

// -------------------------------------------------------------------- SEE --

}  // namespace

bool see_ge(const Position& pos, Move m, int threshold) {
    // ponytail: promotions and en passant are assumed break-even rather than
    // swapped off properly.  They are a small share of captures and getting
    // the promoted-piece bookkeeping right is a lot of code for little gain.
    if (type_of(m) != NORMAL)
        return 0 >= threshold;

    const Square from = from_sq(m);
    const Square to   = to_sq(m);

    // `swap` is always "what the side to move still has to win to beat the
    // threshold", recomputed from the other side's point of view each ply.
    int swap = PieceValue[type_of(pos.piece_on(to))] - threshold;
    if (swap < 0)
        return false;

    swap = PieceValue[type_of(pos.piece_on(from))] - swap;
    if (swap <= 0)
        return true;

    Bitboard occ       = (pos.pieces() ^ square_bb(from)) | square_bb(to);
    Bitboard attackers = pos.attackers_to(to, occ);
    Color    stm       = pos.side_to_move();
    bool     result    = true;

    while (true) {
        stm = ~stm;
        // Pieces already consumed by the exchange drop out here; so does the
        // mover, whose square left `occ` before the loop started.
        attackers &= occ;

        const Bitboard ours = attackers & pos.pieces(stm);
        if (!ours)
            break;

        result = !result;

        // Recapture with the cheapest attacker available.
        PieceType pt = PAWN;
        Bitboard  b  = 0;
        for (; pt <= KING; pt = PieceType(pt + 1)) {
            b = ours & pos.pieces(pt);
            if (b)
                break;
        }

        if (pt == KING) {
            // A king may only take last: if the other side still attacks the
            // square this capture never happens, so the previous verdict holds.
            if (attackers & pos.pieces(~stm))
                result = !result;
            break;
        }

        occ ^= square_bb(lsb(b));

        // Removing an attacker can expose a slider behind it on the same ray.
        if (pt == PAWN || pt == BISHOP || pt == QUEEN)
            attackers |= attacks<BISHOP>(to, occ) & pos.pieces(BISHOP, QUEEN);
        if (pt == ROOK || pt == QUEEN)
            attackers |= attacks<ROOK>(to, occ) & pos.pieces(ROOK, QUEEN);

        swap = PieceValue[pt] - swap;
        if (swap < int(result))
            break;
    }
    return result;
}

namespace {

// --------------------------------------------------------- move scoring ----

int score_move(const Position& pos, Move m, Move ttMove, const Stack* ss) {
    if (m == ttMove)
        return SCORE_TT;

    const bool capture = pos.is_capture(m);
    const bool promo   = type_of(m) == PROMOTION;

    if (capture || promo) {
        const PieceType victim = (type_of(m) == EN_PASSANT) ? PAWN
                                                            : type_of(pos.piece_on(to_sq(m)));
        // MVV first, LVA only as a tie-break -- winning a queen with a queen
        // still beats winning a pawn with a pawn.
        int value = PieceValue[victim] * 16 - PieceValue[type_of(pos.piece_on(from_sq(m)))];
        if (promo)
            value += PieceValue[promotion_type(m)] * 16;

        // A small negative threshold keeps roughly-even trades in the good
        // bucket; only clearly losing captures get pushed behind the quiets.
        return (see_ge(pos, m, -20) ? SCORE_GOODCAP : SCORE_BADCAP) + value;
    }

    if (m == ss->killers[0])
        return SCORE_KILLER1;
    if (m == ss->killers[1])
        return SCORE_KILLER2;

    int score = W.history[pos.side_to_move()][from_sq(m)][to_sq(m)];
    for (int off : {1, 2})
        if (const std::int16_t* slot = cont_hist(ss - off, pos.piece_on(from_sq(m)), to_sq(m)))
            score += *slot;
    return score;
}

// Selection sort, one pick per iteration: most move loops end on a cutoff long
// before the tail is ever looked at, so sorting it up front is wasted work.
// ponytail: a staged move picker (TT / captures / killers / quiets) would skip
// generating quiets entirely on many cutoffs.  Phase 4 territory.
void pick_next(Move* moves, int* scores, int count, int idx) {
    int best = idx;
    for (int i = idx + 1; i < count; ++i)
        if (scores[i] > scores[best])
            best = i;
    if (best != idx) {
        std::swap(moves[idx], moves[best]);
        std::swap(scores[idx], scores[best]);
    }
}

// ----------------------------------------------------------------- timing --

bool out_of_time() {
    // Checked every 1024 nodes, so it must be cheap and must never fire in a
    // depth- or infinite-limited search: bench determinism depends on it.
    if (W.nodeLimit && W.nodes >= W.nodeLimit)
        return true;
    return W.useTime && W.elapsed() >= W.hardLimit;
}

void check_stop() {
    if ((W.nodes & 1023) == 0 && out_of_time())
        Stopped.store(true, std::memory_order_relaxed);
}

bool aborted() { return Stopped.load(std::memory_order_relaxed); }

// ------------------------------------------------------------ quiescence ---

template<bool PvNode>
Score qsearch(Position& pos, Stack* ss, Score alpha, Score beta) {
    ++W.nodes;
    check_stop();

    W.seldepth = std::max(W.seldepth, ss->ply);

    // search() jumps straight here at depth <= 0, *before* it resets pvLen for
    // this ply.  Without this reset the slot keeps a stale length from an
    // earlier, deeper line, and the parent's memcpy copies that many junk moves
    // into its PV -- which shows up as repeated moves and a PV that runs past
    // checkmate.  qsearch builds no PV of its own, so zero is the right length:
    // the reported PV correctly truncates at the qsearch boundary.
    if (PvNode)
        W.pvLen[ss->ply] = 0;

    if (pos.is_draw_for_search())
        return VALUE_DRAW;
    if (ss->ply >= MAX_PLY - 1)
        return pos.in_check() ? VALUE_DRAW : eval_at(pos, ss);

    TTData tt;
    const bool ttHit = TT.probe(pos.key(), ss->ply, tt);

    if (!PvNode && ttHit && tt.depth >= 0
        && (tt.bound == BOUND_EXACT || (tt.bound == BOUND_LOWER && tt.score >= beta)
            || (tt.bound == BOUND_UPPER && tt.score <= alpha)))
        return tt.score;

    const bool inCheck = pos.in_check();
    Score      best    = -VALUE_INFINITE;
    Score      staticEval = VALUE_NONE;
    Score      rawEval    = VALUE_NONE;  // uncorrected; only this goes in the table

    if (!inCheck) {
        rawEval        = (ttHit && tt.eval != VALUE_NONE) ? tt.eval : eval_at(pos, ss);
        staticEval     = corrected_eval(rawEval, pos);
        ss->staticEval = staticEval;

        // Stand pat: nobody is forced to capture, so the quiet score is a
        // floor on what this node is worth.
        best = staticEval;
        if (best >= beta) {
            TT.store(pos.key(), ss->ply, 0, BOUND_LOWER, MOVE_NONE, best, rawEval, PvNode);
            return best;
        }
        if (best > alpha)
            alpha = best;
    }

    const Color    us         = pos.side_to_move();
    const Bitboard promoPawns = pos.pieces(us, PAWN) & relative_rank_bb(us, RANK_7);

    Move  moves[MAX_MOVES];
    Move* end;
    if (inCheck || promoPawns)
        // In check every move is an evasion candidate; with a pawn on the 7th
        // the quiet queen promotion matters as much as any capture.
        end = generate<ALL>(pos, moves);
    else
        end = generate<CAPTURES>(pos, moves);

    const int generated = int(end - moves);
    int       scores[MAX_MOVES];
    int       count = 0;

    // Compact the keepers to the front of `moves` in place -- a second array
    // would add half a kilobyte to a stack frame that can nest MAX_PLY deep.
    for (int i = 0; i < generated; ++i) {
        const Move m = moves[i];
        if (!inCheck) {
            // Captures and queen promotions only: an under-promotion is never
            // the point of a tactical sequence.
            const bool queenPromo = type_of(m) == PROMOTION && promotion_type(m) == QUEEN;
            if (type_of(m) == PROMOTION ? !queenPromo : !pos.is_capture(m))
                continue;
        }
        moves[count]  = m;
        scores[count] = score_move(pos, m, tt.move, ss);
        ++count;
    }

    Move bestMove  = MOVE_NONE;
    int  legalSeen = 0;

    for (int i = 0; i < count; ++i) {
        pick_next(moves, scores, count, i);
        const Move m = moves[i];

        if (!pos.is_legal(m))
            continue;
        ++legalSeen;

        if (!inCheck) {
            // Delta pruning: even winning this piece for free leaves us far
            // enough below alpha that the rest of the line cannot matter.
            const PieceType victim = (type_of(m) == EN_PASSANT) ? PAWN
                                                                : type_of(pos.piece_on(to_sq(m)));
            if (staticEval + PieceValue[victim] + 200 <= alpha
                && type_of(m) != PROMOTION)
                continue;

            if (!see_ge(pos, m, 0))
                continue;
        }

        if (nnue::loaded())
            nnue::apply_move(pos, m, *ss->accPtr, (ss + 1)->acc);
        // A real move writes the child's OWN accumulator, so repoint it: a null
        // move deeper in the line must not inherit a stale parent pointer.
        (ss + 1)->accPtr = &(ss + 1)->acc;
        pos.make_move(m);
        TT.prefetch(pos.key());
        const Score score = -qsearch<PvNode>(pos, ss + 1, -beta, -alpha);
        pos.unmake_move(m);

        if (aborted())
            return VALUE_DRAW;

        if (score > best) {
            best = score;
            if (score > alpha) {
                bestMove = m;
                if (score >= beta)
                    break;
                alpha = score;
            }
        }
    }

    // Only a check evasion search sees every move, so only it can conclude mate.
    if (inCheck && legalSeen == 0)
        return mated_in(ss->ply);

    const Bound bound = (best >= beta) ? BOUND_LOWER : BOUND_UPPER;
    TT.store(pos.key(), ss->ply, 0, bound, bestMove, best, rawEval, PvNode);
    return best;
}

// ---------------------------------------------------------------- search ---

// `cutNode` is the caller's expectation, not a fact: true means this node was
// entered on a null window that we expect to fail high.  It carries no meaning
// on its own -- reductions read it, and it is the single largest LMR input.
template<bool PvNode>
Score search(Position& pos, Stack* ss, Score alpha, Score beta, int depth, bool cutNode) {
    if (depth <= 0)
        return qsearch<PvNode>(pos, ss, alpha, beta);

    ++W.nodes;
    check_stop();

    const bool rootNode = ss->ply == 0;

    if (PvNode) {
        W.pvLen[ss->ply] = 0;
        W.seldepth       = std::max(W.seldepth, ss->ply);
    }

    if (!rootNode) {
        if (pos.is_draw_for_search())
            return VALUE_DRAW;
        if (ss->ply >= MAX_PLY - 1)
            return pos.in_check() ? VALUE_DRAW : eval_at(pos, ss);

        // Mate distance pruning: a mate found closer to the root than anything
        // this subtree could deliver makes the whole window unreachable.
        alpha = std::max(alpha, mated_in(ss->ply));
        beta  = std::min(beta, mate_in(ss->ply + 1));
        if (alpha >= beta)
            return alpha;
    }

    // Non-zero only inside a singular proof.  It suppresses everything below
    // that would either answer from the table -- whose entry was written by a
    // search that DID see the excluded move, so it answers the wrong question --
    // or write back to a key that does not describe this restricted search.
    const Move excluded = ss->excludedMove;

    TTData tt;
    const bool ttHit = TT.probe(pos.key(), ss->ply, tt);

    if (!PvNode && !excluded && ttHit && tt.depth >= depth
        && (tt.bound == BOUND_EXACT || (tt.bound == BOUND_LOWER && tt.score >= beta)
            || (tt.bound == BOUND_UPPER && tt.score <= alpha)))
        return tt.score;

    // Syzygy.  A table hit is exact knowledge, so it outranks anything the
    // evaluation below could say about the position.  Not inside a singular
    // proof: that search deliberately asks a different question, and its key
    // does not describe this position.
    if (!rootNode && !excluded && tb_in_range(pos)) {
        const unsigned wdl = probe_wdl(pos);
        if (wdl != TB_RESULT_FAILED) {
            ++W.tbHits;

            const Score value = wdl == TB_WIN  ? tb_win_in(ss->ply)
                              : wdl == TB_LOSS ? tb_loss_in(ss->ply)
                                               : VALUE_DRAW;
            const Bound bound = wdl == TB_WIN  ? BOUND_LOWER
                              : wdl == TB_LOSS ? BOUND_UPPER
                                               : BOUND_EXACT;

            // A win is a lower bound and a loss an upper one: WDL knows the
            // result but not the distance, so claiming an exact score would
            // overwrite a real mate the search could still find.
            if (bound == BOUND_EXACT || (bound == BOUND_LOWER && value >= beta)
                || (bound == BOUND_UPPER && value <= alpha)) {
                TT.store(pos.key(), ss->ply, std::min(depth + 6, MAX_PLY - 1), bound,
                         MOVE_NONE, value, VALUE_NONE, PvNode);
                return value;
            }
        }
    }

    const bool  inCheck = pos.in_check();
    const Color us      = pos.side_to_move();

    // Two values, deliberately.  rawEval is what the evaluation actually said;
    // staticEval is that plus the correction, and it is what every pruning
    // decision below reads.  They must not be conflated: tt.eval caches the RAW
    // value and the next probe reads it straight back, so storing a corrected
    // number there would feed each correction into the input of the next one and
    // compound it on every visit to the position.
    const Score rawEval = inCheck ? VALUE_NONE
                        : (ttHit && tt.eval != VALUE_NONE) ? tt.eval
                                                           : eval_at(pos, ss);
    const Score staticEval = corrected_eval(rawEval, pos);
    ss->staticEval         = staticEval;

    // A TT move from a different position must be re-validated from scratch --
    // key16 collides once every 65536 entries and playing a move that is not
    // in this position corrupts the board.
    const Move ttMove = (tt.move != MOVE_NONE && pos.is_pseudo_legal(tt.move)) ? tt.move
                                                                              : MOVE_NONE;

    (ss + 1)->killers[0] = (ss + 1)->killers[1] = MOVE_NONE;

    // "Improving": our own static eval is better than it was two plies ago, so
    // the position is trending our way and pruning can afford to be greedier.
    // VALUE_NONE means that ply was in check and has no comparable eval.
    const bool improving = !inCheck && (ss - 2)->staticEval != VALUE_NONE
                        && staticEval > (ss - 2)->staticEval;

    // Razoring: the static eval is so far below alpha that only tactics could
    // rescue this node, so ask quiescence directly instead of guessing.  If
    // even a full capture sequence cannot reach alpha there is nothing here.
    // Unlike a bare margin test this one is verified, which is what separates
    // it from the 2019-era razoring that measured at zero and was removed.
    if (!PvNode && !excluded && !inCheck && !is_decisive(alpha) && depth <= tunable::RazorDepth
        && staticEval + tunable::RazorMargin * depth < alpha) {
        const Score score = qsearch<false>(pos, ss, alpha - 1, alpha);
        if (score < alpha)
            return score;
    }

    // Reverse futility pruning: we are so far above beta that even giving up
    // the margin -- roughly a piece per ply of depth -- would not bring the
    // score back down.  Fail high on the static eval without searching.
    if (!PvNode && !excluded && !inCheck && depth <= tunable::RfpDepth && !is_decisive(beta)
        && staticEval - tunable::RfpMargin * (depth - improving) >= beta)
        return staticEval;

    // Null move pruning: hand the opponent a free move.  If the position is
    // still above beta after that, it is good enough that searching our own
    // moves properly is a waste.  The zugzwang case -- where being forced to
    // move is itself the problem -- is guarded by requiring a piece on the
    // board, since pawn endings are where a null move lies.
    // ponytail: no verification search at high depth.  Upgrade path is a
    // re-search at depth-R with null move disabled before trusting the cutoff.
    if (!PvNode && !excluded && !inCheck && depth >= tunable::NmpDepth && staticEval >= beta
        && (ss - 1)->currentMove != MOVE_NULL && !is_decisive(beta)
        && (pos.pieces(pos.side_to_move()) & ~pos.pieces(PAWN) & ~pos.pieces(KING))) {

        const int R = tunable::NmpBase + depth / tunable::NmpDepthDiv
                    + std::min((staticEval - beta) / tunable::NmpEvalDiv, tunable::NmpEvalCap);

        ss->currentMove = MOVE_NULL;
        ss->movedPiece  = NO_PIECE;
        // A null move changes no feature, so the child's accumulator would be
        // byte-identical to ours.  Point at ours instead of copying 1 KB.
        (ss + 1)->accPtr = ss->accPtr;
        pos.make_null_move();
        const Score score =
            -search<false>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
        pos.unmake_null_move();

        if (aborted())
            return VALUE_DRAW;

        // A mate score proved with a free move for the opponent is not a mate
        // we can actually deliver; report the bound instead.
        if (score >= beta)
            return is_decisive(score) ? beta : score;
    }

    // Internal iterative reduction: no table move at this depth means no
    // ordering information, and searching blind at full depth is the most
    // expensive way to find one.  Search shallower, and let the entry this
    // leaves behind order the re-visit properly.
    // Only where the node is expected to matter: a PV node, or one we expect
    // to fail high and therefore want a good first move for.  Applying it at
    // all-nodes as well fires almost everywhere against a cold table and
    // compounds down the tree -- measured at -59% nodes, which is not a
    // reduction, it is searching a different and much shallower tree.
    if ((PvNode || cutNode) && depth >= tunable::IirDepth && ttMove == MOVE_NONE)
        --depth;

    Move moves[MAX_MOVES];
    int  scores[MAX_MOVES];
    const int count = int(generate<ALL>(pos, moves) - moves);

    for (int i = 0; i < count; ++i)
        scores[i] = score_move(pos, moves[i], ttMove, ss);

    Score best     = -VALUE_INFINITE;
    Move  bestMove = MOVE_NONE;
    int   moveCount = 0;

    Move quietsTried[MAX_QUIETS_TRACKED];
    int  quietCount = 0;

    for (int i = 0; i < count; ++i) {
        pick_next(moves, scores, count, i);
        const Move m = moves[i];

        // The whole point of a singular proof is to score this node without it.
        if (m == excluded)
            continue;

        if (!pos.is_legal(m))
            continue;
        ++moveCount;

        const bool isQuiet = !pos.is_capture(m) && type_of(m) != PROMOTION;

        // Late move pruning: past a move count that grows with depth, the
        // remaining quiets are ordered so far down that searching them at all
        // is not worth it.  Only once something non-losing is already in hand,
        // so a node can never be left without a score.
        if (!PvNode && !inCheck && isQuiet && best > -VALUE_TB_WIN_IN_MAX_PLY
            && depth <= tunable::LmpDepth
            && moveCount >= tunable::LmpBase + depth * depth / (2 - improving))
            continue;

        // Futility pruning: the static eval sits so far below alpha that a
        // quiet move -- which by definition wins no material -- cannot lift it
        // into the window.  Two guards earn their place: in check staticEval is
        // VALUE_NONE and the comparison is meaningless, and against a mate-score
        // alpha the test is true for everything, which would prune the mating
        // move along with the rest.
        if (!PvNode && !inCheck && isQuiet && best > -VALUE_TB_WIN_IN_MAX_PLY
            && !is_decisive(alpha) && depth <= tunable::FpDepth
            && staticEval + tunable::FpMargin * depth <= alpha)
            continue;

        // History pruning: every time this quiet has been tried it has failed,
        // and the tables say so.  scores[i] is exactly the summed statistic
        // score_move already computed for this move -- pick_next keeps moves
        // and scores in step -- so the test costs a load.  Killers and the TT
        // move carry large positive scores and can never trip it.
        if (!PvNode && !inCheck && isQuiet && best > -VALUE_TB_WIN_IN_MAX_PLY
            && depth <= tunable::HistPruneDepth
            && scores[i] < -tunable::HistPruneMargin * depth)
            continue;

        // SEE pruning: the move loses material outright by more than the depth
        // left could plausibly win back.  Quiets are given a wider allowance --
        // a quiet move that hangs a piece is usually still a real idea, whereas
        // a capture that loses material rarely is.
        if (!rootNode && best > -VALUE_TB_WIN_IN_MAX_PLY && depth <= tunable::SeeDepth
            && !see_ge(pos, m, -(isQuiet ? tunable::SeeQuietMargin : tunable::SeeNoisyMargin)
                                   * depth))
            continue;

        if (isQuiet && quietCount < MAX_QUIETS_TRACKED)
            quietsTried[quietCount++] = m;

        // Singular extension.  The table says this move is at least tt.score;
        // ask whether anything ELSE reaches nearly that, by searching the same
        // position at reduced depth with this move removed and the window set
        // just below it.  If every alternative fails low the move is carrying
        // the position alone, and a position with one playable move is worth a
        // ply more than its depth suggests.
        //
        // Runs at this same stack slot, so it clobbers ss->currentMove and
        // friends -- which is why it sits above where those are assigned.
        int extension = 0;
        if (!rootNode && !excluded && m == ttMove && depth >= tunable::SingularDepth
            && ttHit && tt.depth >= depth - 3 && (tt.bound & BOUND_LOWER)
            && !is_decisive(tt.score)) {

            const Score singularBeta = tt.score - tunable::SingularMargin * depth / 16;

            ss->excludedMove = m;
            const Score s = search<false>(pos, ss, singularBeta - 1, singularBeta,
                                          (depth - 1) / 2, cutNode);
            ss->excludedMove = MOVE_NONE;

            // An aborted search returns VALUE_DRAW, which is below singularBeta
            // for any winning position and would extend on nothing at all.
            if (aborted())
                return VALUE_DRAW;

            if (s < singularBeta)
                extension = 1;
        }

        ss->currentMove = m;
        ss->movedPiece  = pos.piece_on(from_sq(m));
        ss->movedTo     = to_sq(m);
        if (nnue::loaded())
            nnue::apply_move(pos, m, *ss->accPtr, (ss + 1)->acc);
        // A real move writes the child's OWN accumulator, so repoint it: a null
        // move deeper in the line must not inherit a stale parent pointer.
        (ss + 1)->accPtr = &(ss + 1)->acc;
        pos.make_move(m);
        TT.prefetch(pos.key());

        // Full depth for this move, extension included.  Only the TT move can
        // carry one, and the TT move is almost always moveCount 1, but both
        // branches read it so a pruned or illegal TT move cannot desynchronise
        // the two.  With extension == 0 every expression below is unchanged.
        const int fullDepth = depth - 1 + extension;

        Score score;
        if (moveCount == 1) {
            // The first move gets the full window: it is the PV candidate and
            // there is nothing yet to prove it wrong against.
            score = -search<PvNode>(pos, ss + 1, -beta, -alpha, fullDepth,
                                    PvNode ? false : !cutNode);
        } else {
            // Late move reductions: move ordering is good enough that a move
            // this far down the list is unlikely to be best, so search it
            // shallower and only pay full depth if it surprises us.
            int newDepth = fullDepth;

            if (depth >= tunable::LmrDepth && moveCount > tunable::LmrMoveCount) {
                int r = lmr_base(!isQuiet, depth, moveCount);

                r += cutNode * tunable::LmrCutNode;  // by far the largest term
                r -= PvNode * LMR_SCALE;
                r -= improving * LMR_SCALE;
                r -= inCheck * LMR_SCALE;
                if (isQuiet) {
                    // The same statistic move ordering already scores with:
                    // butterfly history plus both continuation offsets.  Reading
                    // only the butterfly half threw away two thirds of what the
                    // node already knows about this move.  ss->movedPiece was
                    // captured before make_move, which has run by now.
                    int hist = W.history[us][from_sq(m)][to_sq(m)];
                    for (int off : {1, 2})
                        if (const std::int16_t* slot = cont_hist(ss - off, ss->movedPiece, to_sq(m)))
                            hist += *slot;
                    r -= hist * LMR_SCALE / tunable::LmrHistDiv;
                }

                newDepth = std::clamp(depth + extension - (r / LMR_SCALE), 1, fullDepth);
            }

            // Everything after the first move only has to be shown to be no
            // better, which a null window does far more cheaply.  A null-window
            // child is by construction expected to refute, hence a cut node.
            score = -search<false>(pos, ss + 1, -alpha - 1, -alpha, newDepth, !cutNode);

            // Reduced and still beat alpha: the reduction was wrong, so redo it
            // at full depth before believing the score.
            if (score > alpha && newDepth < fullDepth)
                score = -search<false>(pos, ss + 1, -alpha - 1, -alpha, fullDepth, !cutNode);

            if (PvNode && score > alpha && score < beta)
                score = -search<true>(pos, ss + 1, -beta, -alpha, fullDepth, false);
        }

        pos.unmake_move(m);

        if (aborted())
            return VALUE_DRAW;

        if (score > best) {
            best = score;

            if (score > alpha) {
                bestMove = m;

                if (PvNode) {
                    // The child only left a PV behind if it was searched as a
                    // PV node: moveCount 1, or the full-window re-search above,
                    // which runs only while the score stays under beta.  When a
                    // null window fails high no PV child ever ran, and
                    // pvLen[ply + 1] still holds the length an earlier, deeper
                    // search left in that slot -- copying it splices a stale
                    // tail onto a legitimate move.  That is the `PV continues
                    // after checkmate` warning: the tail outlives the mate that
                    // ended the real line.  The move itself is known good, so
                    // keep it and drop the tail.
                    const bool childHasPv = moveCount == 1 || score < beta;

                    W.pv[ss->ply][0]   = m;
                    const int childLen = childHasPv ? W.pvLen[ss->ply + 1] : 0;
                    std::memcpy(&W.pv[ss->ply][1], &W.pv[ss->ply + 1][0],
                                std::size_t(childLen) * sizeof(Move));
                    W.pvLen[ss->ply] = childLen + 1;
                }

                if (score >= beta) {
                    if (isQuiet) {
                        if (m != ss->killers[0]) {
                            ss->killers[1] = ss->killers[0];
                            ss->killers[0] = m;
                        }

                        const int bonus = history_bonus(depth);
                        update_history(W.history[us][from_sq(m)][to_sq(m)], bonus);
                        update_cont_hist(ss, ss->movedPiece, to_sq(m), bonus);

                        // Malus: the quiets that were tried first and did not
                        // cut were, on this evidence, ordered too high.
                        for (int q = 0; q < quietCount; ++q) {
                            const Move q_ = quietsTried[q];
                            if (q_ == m)
                                continue;
                            update_history(W.history[us][from_sq(q_)][to_sq(q_)], -bonus);
                            update_cont_hist(ss, pos.piece_on(from_sq(q_)), to_sq(q_), -bonus);
                        }
                    }
                    break;
                }
                alpha = score;
            }
        }
    }

    if (moveCount == 0)
        // Inside a proof, "no moves" means the excluded move was the only one.
        // That is a fact about this restricted search, not about the position,
        // and returning a mate score would make every forced move look singular
        // by way of a mate that does not exist.
        return excluded ? alpha : (inCheck ? mated_in(ss->ply) : VALUE_DRAW);

    // LMP and SEE pruning skip moves after moveCount has already counted them,
    // so "moveCount > 0" no longer implies "something was searched".  What
    // guarantees a real score here is that both guards require best to be above
    // the mate range, which -VALUE_INFINITE is not: the first legal move can
    // never be pruned.  Assert it rather than trust the reading.
    assert(best > -VALUE_INFINITE);

    if (aborted())
        return VALUE_DRAW;

    const Bound bound = (best >= beta)      ? BOUND_LOWER
                      : (PvNode && bestMove != MOVE_NONE) ? BOUND_EXACT
                                                          : BOUND_UPPER;
    // Never from inside a proof.  This search deliberately refused to look at
    // the position's best move, so its score does not describe the position --
    // storing it under the real key poisons the entry for every later probe.
    // Teach the correction table where the search disagreed with the evaluation,
    // but only where the disagreement is real.  A fail-low bound cannot claim
    // the position is better than the eval said, a fail-high cannot claim it is
    // worse, and a swing driven by a capture is tactics rather than a structural
    // miss.  Never from inside a singular proof: that score describes a search
    // that refused to look at the best move.
    if (!excluded && !inCheck && rawEval != VALUE_NONE
        && !(bestMove != MOVE_NONE && pos.is_capture(bestMove))
        && (bound == BOUND_EXACT || (bound == BOUND_LOWER && best > staticEval)
            || (bound == BOUND_UPPER && best < staticEval)))
        update_correction(pos, depth, best - staticEval);

    // rawEval, not staticEval -- see the note where the two are computed.
    if (!excluded)
        TT.store(pos.key(), ss->ply, depth, bound, bestMove, best, rawEval, PvNode);

    return best;
}

// ------------------------------------------------------------- reporting ---

std::string pv_string() {
    std::string out;
    for (int i = 0; i < W.pvLen[0]; ++i) {
        if (i)
            out += ' ';
        out += move_to_uci(W.pv[0][i]);
    }
    return out;
}

void print_info(int depth, Score score) {
    const std::int64_t ms    = W.elapsed();
    const std::int64_t nps   = W.nodes * 1000 / std::max<std::int64_t>(ms, 1);

    std::printf("info depth %d seldepth %d ", depth, W.seldepth);

    if (is_mate_score(score)) {
        const int plies = (score > 0) ? (VALUE_MATE - score) : (-VALUE_MATE - score);
        // UCI counts moves, and a mate delivered on ply N lands on move (N+1)/2.
        std::printf("score mate %d ", (plies + (score > 0 ? 1 : -1)) / 2);
    } else {
        std::printf("score cp %d ", score);
    }

    std::printf("nodes %llu nps %llu hashfull %d tbhits %llu time %lld pv %s\n",
                (unsigned long long) W.nodes, (unsigned long long) nps, TT.hashfull(),
                (unsigned long long) W.tbHits, (long long) ms, pv_string().c_str());
    std::fflush(stdout);
}

// ------------------------------------------------------- time allocation ---

void set_up_time(const Limits& limits, Color us) {
    W.nodeLimit = limits.nodes;
    W.useTime   = false;

    if (limits.infinite || limits.depth)
        return;

    if (limits.movetime) {
        W.useTime   = true;
        W.softLimit = W.hardLimit =
            std::max<std::int64_t>(limits.movetime - limits.moveOverhead, 1);
        return;
    }

    if (!limits.time[us])
        return;

    const std::int64_t left =
        std::max<std::int64_t>(limits.time[us] - limits.moveOverhead, 1);
    const std::int64_t inc = limits.inc[us];

    // ponytail: a flat fraction of the clock, no node-based or best-move
    // stability scaling -- Phase 7 owns blitz time management.  Conservative
    // on purpose: losing on time costs a whole game, thinking 20% longer does
    // not.
    if (limits.movestogo > 0)
        W.softLimit = left / std::max(limits.movestogo, 1) + inc / 2;
    else
        W.softLimit = left / 25 + inc / 2;

    // The hard limit only ever stops an iteration that has already overrun the
    // soft one, so it can be a multiple of it -- but never so large that one
    // bad iteration eats the moves still to be played.
    W.hardLimit = std::min<std::int64_t>(W.softLimit * 3, left / 3);
    W.softLimit = std::min(W.softLimit, W.hardLimit);
    W.useTime   = true;
}

// --------------------------------------------------- iterative deepening ---

Move first_legal_move(const Position& pos) {
    for (Move m : MoveList<ALL>(pos))
        if (pos.is_legal(m))
            return m;
    return MOVE_NONE;
}

void iterative_deepening(Position& pos, int maxDepth) {
    for (int i = 0; i < MAX_PLY + ROOT_OFFSET + 4; ++i) {
        W.stack[i]     = Stack{};
        W.stack[i].ply = i - ROOT_OFFSET;
    }
    Stack* const root = W.stack + ROOT_OFFSET;

    // The only full refresh in a normal search: every other accumulator in the
    // tree is derived from this one.
    if (nnue::loaded())
        nnue::refresh(pos, root->acc);
    root->accPtr = &root->acc;

    Score previous = VALUE_NONE;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        W.seldepth = 0;

        // Aspiration windows: the score rarely moves far between iterations,
        // so guessing a narrow window around the last one gets far more cutoffs
        // -- at the cost of a re-search when the guess is wrong.
        Score delta = tunable::AspWindow;
        Score alpha = -VALUE_INFINITE;
        Score beta  = VALUE_INFINITE;

        if (depth >= 4 && !is_decisive(previous)) {
            alpha = std::max(previous - delta, -VALUE_INFINITE);
            beta  = std::min(previous + delta, VALUE_INFINITE);
        }

        Score score;
        while (true) {
            score = search<true>(pos, root, alpha, beta, depth, false);

            if (aborted())
                break;

            if (score <= alpha) {
                // Fail low: pull alpha down and relax beta too, so the
                // re-search is not immediately squeezed from the other side.
                beta  = (alpha + beta) / 2;
                alpha = std::max(score - delta, -VALUE_INFINITE);
            } else if (score >= beta) {
                beta = std::min(score + delta, VALUE_INFINITE);
            } else {
                break;
            }

            delta += delta / 3 + 5;
        }

        if (aborted())
            break;

        previous = score;
        if (W.pvLen[0] > 0)
            W.rootBestMove = W.pv[0][0];
        W.rootScore = score;

        if (!W.quiet)
            print_info(depth, score);

        // Soft limit: checked only between iterations.  Starting an iteration
        // we cannot finish wastes the whole thing.
        if (W.useTime && W.elapsed() >= W.softLimit)
            break;
    }
}

}  // namespace

// ------------------------------------------------------------ public API ---

void clear() {
    TT.clear();
    std::memset(W.history, 0, sizeof(W.history));
    std::memset(W.contHist, 0, sizeof(W.contHist));
    std::memset(W.pawnCorr, 0, sizeof(W.pawnCorr));
    std::memset(W.nonPawnCorr, 0, sizeof(W.nonPawnCorr));
    W.rootBestMove = MOVE_NONE;
    W.rootScore    = VALUE_NONE;
}

void stop() { Stopped.store(true, std::memory_order_relaxed); }

void prepare() { Stopped.store(false, std::memory_order_relaxed); }

void go(Position& pos, const Limits& limits) {
    W.nodes    = 0;
    W.tbHits   = 0;
    W.seldepth = 0;
    W.quiet    = false;
    W.start    = Clock::now();

    // A move to fall back on if the very first iteration is cut short.
    W.rootBestMove = first_legal_move(pos);

    set_up_time(limits, pos.side_to_move());
    TT.new_search();

    // A won table position is solved.  Searching it cannot improve on the
    // DTZ-optimal move and can lose the win outright, because every move the
    // search compares looks equally winning to a WDL probe and none of them is
    // pressed to make progress.  Play the table's move and keep the clock.
    const Move tbMove = probe_root(pos);
    const int maxDepth = limits.depth ? std::min(limits.depth, MAX_PLY - 2) : MAX_PLY - 2;

    if (tbMove != MOVE_NONE && !limits.infinite) {
        W.rootBestMove = tbMove;
        W.rootScore    = VALUE_TB;   // or the previous search's score leaks out
        W.tbHits       = 1;
        // depth 1, not maxDepth: a table hit searched nothing, and printing
        // "depth 244" lands in the PGN of every won tablebase ending.
        std::printf("info depth 1 score cp %d nodes 0 tbhits 1 time %lld pv %s\n",
                    int(VALUE_TB), (long long) W.elapsed(), move_to_uci(tbMove).c_str());
        std::fflush(stdout);
    } else if (W.rootBestMove != MOVE_NONE)
        iterative_deepening(pos, maxDepth);

    std::printf("bestmove %s\n",
                W.rootBestMove == MOVE_NONE ? "0000" : move_to_uci(W.rootBestMove).c_str());
    std::fflush(stdout);

    Stopped.store(true, std::memory_order_relaxed);
}

Result search_fixed_nodes(Position& pos, std::uint64_t nodes) {
    Stopped.store(false, std::memory_order_relaxed);

    W.nodes     = 0;
    W.seldepth  = 0;
    W.quiet     = true;
    W.useTime   = false;
    W.nodeLimit = nodes;
    W.start     = Clock::now();

    // iterative_deepening only writes rootScore after an iteration completes,
    // and nothing else clears it.  At 5000 nodes depth 1 always finishes, but
    // the node budget is datagen's quality dial: turn it low enough that even
    // depth 1 aborts and the caller would otherwise be handed the *previous*
    // position's score with no way to tell.  VALUE_NONE says "no score".
    W.rootScore    = VALUE_NONE;
    W.rootBestMove = first_legal_move(pos);
    W.pvLen[0]     = 0;

    TT.new_search();
    if (W.rootBestMove != MOVE_NONE)
        iterative_deepening(pos, MAX_PLY - 2);

    Result r;
    r.nodes = W.nodes;
    r.best  = W.rootBestMove;
    r.score = W.rootScore;
    r.pv.assign(W.pv[0], W.pv[0] + W.pvLen[0]);
    return r;
}

Score qsearch_eval(Position& pos) {
    Stopped.store(false, std::memory_order_relaxed);

    W.nodes     = 0;
    W.quiet     = true;
    W.useTime   = false;
    W.nodeLimit = 0;
    W.start     = Clock::now();

    for (int i = 0; i < MAX_PLY + ROOT_OFFSET + 4; ++i) {
        W.stack[i]     = Stack{};
        W.stack[i].ply = i - ROOT_OFFSET;
    }

    if (nnue::loaded())
        nnue::refresh(pos, W.stack[ROOT_OFFSET].acc);
    W.stack[ROOT_OFFSET].accPtr = &W.stack[ROOT_OFFSET].acc;

    return qsearch<false>(pos, W.stack + ROOT_OFFSET, -VALUE_INFINITE, VALUE_INFINITE);
}

Result search_fixed_depth(Position& pos, int depth) {
    Stopped.store(false, std::memory_order_relaxed);

    W.nodes     = 0;
    W.seldepth  = 0;
    W.quiet     = true;
    W.useTime   = false;
    W.nodeLimit = 0;
    W.start     = Clock::now();

    W.rootBestMove = first_legal_move(pos);
    W.pvLen[0]     = 0;

    TT.new_search();
    if (W.rootBestMove != MOVE_NONE)
        iterative_deepening(pos, depth);

    Result r;
    r.nodes = W.nodes;
    r.best  = W.rootBestMove;
    r.score = W.rootScore;
    r.pv.assign(W.pv[0], W.pv[0] + W.pvLen[0]);
    return r;
}

}  // namespace rogatia::search
