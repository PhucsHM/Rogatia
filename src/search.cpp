// Rogatia chess engine -- search.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fail-soft negamax with PVS, iterative deepening, aspiration windows, a
// quiescence search and a transposition table.  Pruning and reductions (null
// move, LMR, RFP, futility, extensions) are deliberately absent: they belong
// to Phase 4, after the SPRT harness exists to measure them.
#include "search.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "movegen.h"
#include "perft.h"  // move_to_uci
#include "tt.h"

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

// ------------------------------------------------------------ search state --

struct Stack {
    int   ply        = 0;
    Move  killers[2] = {MOVE_NONE, MOVE_NONE};
    Score staticEval = VALUE_NONE;
};

std::atomic<bool> Stopped{false};

struct Worker {
    std::uint64_t nodes    = 0;
    int           seldepth = 0;

    Move  rootBestMove = MOVE_NONE;
    Score rootScore    = VALUE_NONE;

    int history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};

    Move pv[MAX_PLY][MAX_PLY] = {};
    int  pvLen[MAX_PLY]       = {};

    Stack stack[MAX_PLY + 4];

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

void update_history(int& slot, int bonus) {
    bonus = std::clamp(bonus, -MAX_HISTORY, MAX_HISTORY);
    // Gravity: the closer a slot is to the cap the less a new bonus moves it,
    // so a move has to keep earning its rank instead of saturating once.
    slot += bonus - slot * std::abs(bonus) / MAX_HISTORY;
}

int history_bonus(int depth) { return std::min(300 * depth - 250, 2400); }

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

    return W.history[pos.side_to_move()][from_sq(m)][to_sq(m)];
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
        return pos.in_check() ? VALUE_DRAW : evaluate(pos);

    TTData tt;
    const bool ttHit = TT.probe(pos.key(), ss->ply, tt);

    if (!PvNode && ttHit && tt.depth >= 0
        && (tt.bound == BOUND_EXACT || (tt.bound == BOUND_LOWER && tt.score >= beta)
            || (tt.bound == BOUND_UPPER && tt.score <= alpha)))
        return tt.score;

    const bool inCheck = pos.in_check();
    Score      best    = -VALUE_INFINITE;
    Score      staticEval = VALUE_NONE;

    if (!inCheck) {
        staticEval = (ttHit && tt.eval != VALUE_NONE) ? tt.eval : evaluate(pos);
        ss->staticEval = staticEval;

        // Stand pat: nobody is forced to capture, so the quiet score is a
        // floor on what this node is worth.
        best = staticEval;
        if (best >= beta) {
            TT.store(pos.key(), ss->ply, 0, BOUND_LOWER, MOVE_NONE, best, staticEval, PvNode);
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
    TT.store(pos.key(), ss->ply, 0, bound, bestMove, best, staticEval, PvNode);
    return best;
}

// ---------------------------------------------------------------- search ---

template<bool PvNode>
Score search(Position& pos, Stack* ss, Score alpha, Score beta, int depth) {
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
            return pos.in_check() ? VALUE_DRAW : evaluate(pos);

        // Mate distance pruning: a mate found closer to the root than anything
        // this subtree could deliver makes the whole window unreachable.
        alpha = std::max(alpha, mated_in(ss->ply));
        beta  = std::min(beta, mate_in(ss->ply + 1));
        if (alpha >= beta)
            return alpha;
    }

    TTData tt;
    const bool ttHit = TT.probe(pos.key(), ss->ply, tt);

    if (!PvNode && ttHit && tt.depth >= depth
        && (tt.bound == BOUND_EXACT || (tt.bound == BOUND_LOWER && tt.score >= beta)
            || (tt.bound == BOUND_UPPER && tt.score <= alpha)))
        return tt.score;

    const bool inCheck = pos.in_check();

    const Score staticEval = inCheck ? VALUE_NONE
                           : (ttHit && tt.eval != VALUE_NONE) ? tt.eval
                                                              : evaluate(pos);
    ss->staticEval = staticEval;

    // A TT move from a different position must be re-validated from scratch --
    // key16 collides once every 65536 entries and playing a move that is not
    // in this position corrupts the board.
    const Move ttMove = (tt.move != MOVE_NONE && pos.is_pseudo_legal(tt.move)) ? tt.move
                                                                              : MOVE_NONE;

    (ss + 1)->killers[0] = (ss + 1)->killers[1] = MOVE_NONE;

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

        if (!pos.is_legal(m))
            continue;
        ++moveCount;

        const bool isQuiet = !pos.is_capture(m) && type_of(m) != PROMOTION;
        if (isQuiet && quietCount < MAX_QUIETS_TRACKED)
            quietsTried[quietCount++] = m;

        pos.make_move(m);
        TT.prefetch(pos.key());

        Score score;
        if (moveCount == 1) {
            // The first move gets the full window: it is the PV candidate and
            // there is nothing yet to prove it wrong against.
            score = -search<PvNode>(pos, ss + 1, -beta, -alpha, depth - 1);
        } else {
            // Everything after it only has to be shown to be no better, which
            // a null window does far more cheaply.
            score = -search<false>(pos, ss + 1, -alpha - 1, -alpha, depth - 1);
            if (PvNode && score > alpha && score < beta)
                score = -search<true>(pos, ss + 1, -beta, -alpha, depth - 1);
        }

        pos.unmake_move(m);

        if (aborted())
            return VALUE_DRAW;

        if (score > best) {
            best = score;

            if (score > alpha) {
                bestMove = m;

                if (PvNode) {
                    W.pv[ss->ply][0] = m;
                    const int childLen = W.pvLen[ss->ply + 1];
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
                        update_history(W.history[pos.side_to_move()][from_sq(m)][to_sq(m)], bonus);

                        // Malus: the quiets that were tried first and did not
                        // cut were, on this evidence, ordered too high.
                        for (int q = 0; q < quietCount; ++q)
                            if (quietsTried[q] != m)
                                update_history(W.history[pos.side_to_move()][from_sq(quietsTried[q])]
                                                        [to_sq(quietsTried[q])],
                                               -bonus);
                    }
                    break;
                }
                alpha = score;
            }
        }
    }

    if (moveCount == 0)
        return inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    if (aborted())
        return VALUE_DRAW;

    const Bound bound = (best >= beta)      ? BOUND_LOWER
                      : (PvNode && bestMove != MOVE_NONE) ? BOUND_EXACT
                                                          : BOUND_UPPER;
    TT.store(pos.key(), ss->ply, depth, bound, bestMove, best, staticEval, PvNode);

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

    std::printf("nodes %llu nps %llu hashfull %d time %lld pv %s\n",
                (unsigned long long) W.nodes, (unsigned long long) nps, TT.hashfull(),
                (long long) ms, pv_string().c_str());
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
    for (int i = 0; i < MAX_PLY + 4; ++i)
        W.stack[i] = Stack{};
    for (int i = 0; i < MAX_PLY + 4; ++i)
        W.stack[i].ply = i;

    Score previous = VALUE_NONE;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        W.seldepth = 0;

        // Aspiration windows: the score rarely moves far between iterations,
        // so guessing a narrow window around the last one gets far more cutoffs
        // -- at the cost of a re-search when the guess is wrong.
        Score delta = 20;
        Score alpha = -VALUE_INFINITE;
        Score beta  = VALUE_INFINITE;

        if (depth >= 4 && !is_mate_score(previous)) {
            alpha = std::max(previous - delta, -VALUE_INFINITE);
            beta  = std::min(previous + delta, VALUE_INFINITE);
        }

        Score score;
        while (true) {
            score = search<true>(pos, W.stack, alpha, beta, depth);

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
    W.rootBestMove = MOVE_NONE;
    W.rootScore    = VALUE_NONE;
}

void stop() { Stopped.store(true, std::memory_order_relaxed); }

void go(Position& pos, const Limits& limits) {
    Stopped.store(false, std::memory_order_relaxed);

    W.nodes    = 0;
    W.seldepth = 0;
    W.quiet    = false;
    W.start    = Clock::now();

    // A move to fall back on if the very first iteration is cut short.
    W.rootBestMove = first_legal_move(pos);

    set_up_time(limits, pos.side_to_move());
    TT.new_search();

    const int maxDepth = limits.depth ? std::min(limits.depth, MAX_PLY - 2) : MAX_PLY - 2;
    if (W.rootBestMove != MOVE_NONE)
        iterative_deepening(pos, maxDepth);

    std::printf("bestmove %s\n",
                W.rootBestMove == MOVE_NONE ? "0000" : move_to_uci(W.rootBestMove).c_str());
    std::fflush(stdout);

    Stopped.store(true, std::memory_order_relaxed);
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
