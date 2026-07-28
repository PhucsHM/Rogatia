#!/usr/bin/env python3
"""Cross-tabulate how our draws end against whether we were winning.

Splits every drawn game by its ending -- three-fold, fifty-move, or dead
material -- and by whether Rogatia had reached +1.00.  The overlap is what a
patch can actually target: a fifty-move draw from a winning position is a
conversion failure, a fifty-move draw from a dead position is just chess.
"""
import re
import sys
from collections import Counter

import chess
import chess.pgn

EVAL_RE = re.compile(r"^([+-]?(?:\d+\.\d+|M\d+|\d+))/")
# A bare king plus at most one minor cannot mate.
DEAD = {chess.PAWN: 1, chess.ROOK: 5, chess.QUEEN: 9}


def ev(comment):
    m = EVAL_RE.match((comment or "").strip())
    if not m:
        return None
    raw = m.group(1)
    if "M" in raw:
        return -100.0 if raw.startswith("-") else 100.0
    return float(raw)


for path in sys.argv[1:]:
    rows = Counter()
    draws = 0
    with open(path, encoding="utf-8", errors="replace") as fh:
        while True:
            g = chess.pgn.read_game(fh)
            if g is None:
                break
            if g.headers.get("Result") != "1/2-1/2":
                continue
            draws += 1
            us = chess.WHITE if g.headers.get("White") == "Rogatia" else chess.BLACK
            board = g.board()
            seen = Counter({board._transposition_key(): 1})
            best = -100.0
            for node in g.mainline():
                mover = board.turn
                board.push(node.move)
                seen[board._transposition_key()] += 1
                e = ev(node.comment)
                if e is not None and mover == us:
                    best = max(best, e)

            if seen[board._transposition_key()] >= 3:
                how = "3-fold"
            elif board.halfmove_clock >= 100:
                how = "fifty-move"
            else:
                how = "dead material / other"
            rows[(how, best >= 1.0)] += 1

    name = path.rsplit("\\", 1)[-1].rsplit("/", 1)[-1]
    print("\n%s -- %d draws" % (name, draws))
    print("  %-24s %14s %14s" % ("ending", "we were +1.00", "we never were"))
    for how in ("3-fold", "fifty-move", "dead material / other"):
        w, n = rows[(how, True)], rows[(how, False)]
        print("  %-24s %10d     %10d" % (how, w, n))
    tgt = rows[("3-fold", True)] + rows[("fifty-move", True)]
    print("  -> conversion failures (winning, but drawn by rule): %d of %d draws (%.0f%%)"
          % (tgt, draws, tgt / draws * 100 if draws else 0))
