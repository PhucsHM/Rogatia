#!/usr/bin/env python3
"""Playing-style profile from fastchess PGNs, per engine name.

Implements Stefan Pohl's EAS (Engines Aggressiveness Statistic) definitions as
published on TalkChess -- sacrifices sustained >= 8 plies in won games, wins
reached before the endgame, and draws thrown away from an advantage -- plus the
structural numbers (game length, decisiveness, eval volatility) that need no
interpretation at all.

  python scripts/style.py sprt-results/*.pgn

Two things to know before quoting anything this prints.

**The EAS number is noise below a few thousand won games.**  Measured here, not
assumed: two configurations of the SAME binary differing only in SingularDepth
scored 15,934 and 9,555 -- a 67% gap -- while the "stable core" (the x3 and x10
buckets only) put them at 2,652 and 2,514, a 5% gap.  The whole difference came
from eleven games out of 501 wins: 8 vs 3 with a >=5-pawn sacrifice, 4 vs 1 won
inside 40 moves.  Those buckets carry a x100 weight and are populated by
single-digit counts at this sample size, so EAS reports whichever side happened
to get the brilliancies.  Read the stable core, or get more games.

**Self-play from an unbalanced book is not what EAS was designed for.**  Our
SPRT PGNs are Rogatia vs Rogatia at 8+0.08 out of UHO, where one side starts
better by construction and both sides evaluate identically.  Published EAS
figures come from varied opponents and balanced books at longer time controls.
Absolute values here are not comparable to any published table; use this to
compare our own versions against each other, and run a real gauntlet if a
quotable number is wanted.
"""

import collections
import re
import sys

import chess
import chess.pgn

VALUE = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3,
         chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 0}

# Pohl's multipliers.  Sacrifice buckets are pawn-units of sustained deficit;
# short-win buckets are total move number at the end of a won game.
SAC_MULT   = [(1, 3), (2, 10), (3, 25), (4, 50), (5, 100)]
SHORT_MULT = [(60, 3), (55, 10), (50, 25), (45, 50), (40, 100)]

# A sacrifice must be held, not momentary: material back within a few plies is
# a combination, not a sacrifice.  8 plies is Pohl's threshold.
SAC_PLIES = 8
# "Before the endgame".  Total non-king material at the final position, both
# colours, out of 78 at the start of a normal game.  Pohl checks "low material"
# without publishing the cut, so this is a stated choice, not a quotation --
# report_sensitivity() shows how much it moves the answer.
ENDGAME_MATERIAL = 20

EVAL_RE = re.compile(r"^([+-]?(?:\d+\.\d+|M\d+|\d+))/(\d+)")


def material(board):
    """(white, black) material in pawn-units, kings excluded."""
    w = b = 0
    for sq, pc in board.piece_map().items():
        if pc.color == chess.WHITE:
            w += VALUE[pc.piece_type]
        else:
            b += VALUE[pc.piece_type]
    return w, b


def parse_eval(comment):
    """Score in pawns from White's point of view, and depth. (None, None) if absent.

    fastchess writes the score from the MOVING side's point of view, so the
    caller must know whose move it was.
    """
    m = EVAL_RE.match(comment.strip())
    if not m:
        return None, None
    raw, depth = m.group(1), int(m.group(2))
    if "M" in raw:                      # mate score: clamp, do not average
        return (100.0 if not raw.startswith("-") else -100.0), depth
    return float(raw), depth


class Side:
    def __init__(self, name):
        self.name = name
        self.games = self.wins = self.losses = self.draws = 0
        self.mates = 0
        self.movelens = []
        self.sacs = collections.Counter()      # pawn-units -> won games with one
        self.shorts = collections.Counter()    # move-limit -> won games under it
        self.draw_no_endgame = 0     # drawn with the board still full
        self.draw_from_plus = 0      # drawn after having reached +1.00
        self.eval_steps = []                   # |delta eval| per own move
        self.big_swings = 0
        self.own_moves = 0
        self.depths = []


def analyse(path, sides):
    with open(path, encoding="utf-8", errors="replace") as fh:
        while True:
            game = chess.pgn.read_game(fh)
            if game is None:
                return
            w = game.headers.get("White", "?")
            b = game.headers.get("Black", "?")
            res = game.headers.get("Result", "*")
            board = game.board()

            # Replay once, recording everything both sides need.
            deficit_run = {chess.WHITE: 0, chess.BLACK: 0}   # consecutive plies behind
            max_sac = {chess.WHITE: 0, chess.BLACK: 0}       # best sustained deficit
            best_adv = {chess.WHITE: 0.0, chess.BLACK: 0.0}  # best eval seen
            prev_eval = {chess.WHITE: None, chess.BLACK: None}
            steps = {chess.WHITE: [], chess.BLACK: []}
            swings = {chess.WHITE: 0, chess.BLACK: 0}
            depths = {chess.WHITE: [], chess.BLACK: []}
            nmoves = {chess.WHITE: 0, chess.BLACK: 0}

            for node in game.mainline():
                mover = board.turn          # the comment belongs to this side's move
                board.push(node.move)
                wm, bm = material(board)

                for c, (mine, theirs) in ((chess.WHITE, (wm, bm)),
                                          (chess.BLACK, (bm, wm))):
                    if mine < theirs:
                        deficit_run[c] += 1
                        if deficit_run[c] >= SAC_PLIES:
                            max_sac[c] = max(max_sac[c], theirs - mine)
                    else:
                        deficit_run[c] = 0

                ev, dp = parse_eval(node.comment or "")
                if ev is not None:
                    nmoves[mover] += 1
                    if dp:
                        depths[mover].append(dp)
                    best_adv[mover] = max(best_adv[mover], ev)
                    # Mate scores are clamped to +-100, so any step into or out
                    # of one is an artefact of the clamp, not a real eval swing.
                    # Every game here ends in mate, so leaving them in would
                    # dominate the mean.
                    if prev_eval[mover] is not None and abs(ev) < 100 \
                            and abs(prev_eval[mover]) < 100:
                        d = abs(ev - prev_eval[mover])
                        steps[mover].append(d)
                        if d >= 1.0:
                            swings[mover] += 1
                    prev_eval[mover] = ev

            final_material = sum(material(board))
            before_endgame = final_material > ENDGAME_MATERIAL
            endmove = board.fullmove_number - 1
            mated = board.is_checkmate()

            for colour, name in ((chess.WHITE, w), (chess.BLACK, b)):
                s = sides[name]
                s.games += 1
                s.movelens.append(endmove)
                s.own_moves += nmoves[colour]
                s.eval_steps.extend(steps[colour])
                s.big_swings += swings[colour]
                s.depths.extend(depths[colour])

                won = (res == "1-0") == (colour == chess.WHITE) and res in ("1-0", "0-1")
                if res == "1/2-1/2":
                    s.draws += 1
                    # Pohl: a draw before the endgame, or thrown away from a
                    # material/eval advantage, is a "bad" draw.  A draw held
                    # from behind is not -- it saved half a point.
                    if before_endgame:
                        s.draw_no_endgame += 1
                    if best_adv[colour] >= 1.0:
                        s.draw_from_plus += 1
                elif won:
                    s.wins += 1
                    if mated:
                        s.mates += 1
                    if max_sac[colour]:
                        s.sacs[min(max_sac[colour], 5)] += 1
                    if before_endgame:
                        for limit, _ in SHORT_MULT:
                            if endmove <= limit:
                                s.shorts[limit] += 1
                else:
                    s.losses += 1


def eas(s, core_only=False):
    """Pohl's shape: percentages of WON games, weighted, scaled by 10.

    core_only keeps just the x3 and x10 buckets -- the ones backed by enough
    games at our sample sizes to mean anything.
    """
    if not s.wins:
        return 0.0
    cap = 10 if core_only else 100
    total = 0.0
    for units, mult in SAC_MULT:
        if mult > cap:
            continue
        # cumulative: a 5-unit sacrifice is also a >=1-unit sacrifice
        n = sum(c for u, c in s.sacs.items() if u >= units)
        total += (n / s.wins * 100.0) * mult
    for limit, mult in SHORT_MULT:
        if mult > cap:
            continue
        total += (s.shorts[limit] / s.wins * 100.0) * mult
    return total * 10.0


def report(path, sides):
    print("\n" + "=" * 78)
    print(path)
    print("=" * 78)
    for name, s in sides.items():
        if not s.games:
            continue
        pts = s.wins + 0.5 * s.draws
        ml = sorted(s.movelens)
        med = ml[len(ml) // 2] if ml else 0
        print("\n  [%s]  %d games   %d-%d-%d   %.2f%%"
              % (name, s.games, s.wins, s.losses, s.draws, pts / s.games * 100))
        # No resign or draw adjudication is configured, so fastchess plays every
        # game out and the mate rate should read 100%.  Printed to confirm that,
        # which is also what makes "game length" mean something here.
        print("    decisive %.1f%%   (wins ending in mate on the board: %.1f%%)"
              % ((s.wins + s.losses) / s.games * 100,
                 s.mates / s.wins * 100 if s.wins else 0))
        print("    game length  mean %.1f  median %d moves" %
              (sum(s.movelens) / len(s.movelens), med))
        print("    sacrifices held >=%d plies, in won games:" % SAC_PLIES)
        line = "      "
        for units, mult in SAC_MULT:
            n = sum(c for u, c in s.sacs.items() if u >= units)
            line += "%s%d: %.1f%% (%d)   " % (">=" if units == 5 else "  ", units,
                                              n / s.wins * 100 if s.wins else 0, n)
        print(line)
        print("    short wins (won before the endgame, by final move number):")
        line = "      "
        for limit, mult in SHORT_MULT:
            line += "<=%d: %.1f%% (%d)   " % (limit, s.shorts[limit] / s.wins * 100
                                              if s.wins else 0, s.shorts[limit])
        print(line)
        if s.draws:
            print("    draws: %.1f%% still before the endgame, %.1f%% after reaching +1.00"
                  % (s.draw_no_endgame / s.draws * 100,
                     s.draw_from_plus / s.draws * 100))
        if s.eval_steps:
            print("    eval volatility  mean |d| %.3f pawns/move   >=1.00 swings %.2f%% of moves"
                  % (sum(s.eval_steps) / len(s.eval_steps),
                     s.big_swings / s.own_moves * 100 if s.own_moves else 0))
        if s.depths:
            print("    mean depth reached %.2f" % (sum(s.depths) / len(s.depths)))
        # The x50 and x100 buckets are rare events, so at a few hundred won
        # games they swing EAS by thousands off three or four games.  The
        # "stable core" drops them and keeps only the x3 and x10 weights; if the
        # two disagree on which config is more aggressive, EAS is reporting
        # sample noise and not style.
        print("    EAS (reimplementation, comparable only within this output): "
              "%.0f   stable core %.0f" % (eas(s), eas(s, core_only=True)))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    # Side needs its own name, which defaultdict cannot pass in.
    class ByName(dict):
        def __missing__(self, k):
            self[k] = Side(k)
            return self[k]

    for path in sys.argv[1:]:
        sides = ByName()
        analyse(path, sides)
        report(path, sides)


if __name__ == "__main__":
    main()
