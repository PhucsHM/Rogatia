#!/usr/bin/env python3
"""Replay real games through one engine process and validate every PV it reports.

fastchess warns `PV continues after checkmate` about 0.4 times per game.  The
cause needs a WARM engine: W.pvLen starts zeroed, so a cold fixed-depth search
has no stale slot to splice in and the bug cannot appear.  Driving the engine
the way a game does -- one process, `position startpos moves ...` growing by a
move at a time, no ucinewgame -- is what reproduces it.

Each reported principal variation is replayed on a board.  Two failures are
counted: a move illegal in the position it is played from, and any move at all
after checkmate.

  python pvcheck.py <engine.exe> <games.pgn> [n_games] [movetime_ms]
"""
import subprocess
import sys

import chess
import chess.pgn


def run_game(engine, start_fen, moves, movetime):
    """Feed one game move by move to a single engine process; collect its PVs."""
    lines = ["uci", "isready"]
    for i in range(len(moves)):
        lines.append("position fen %s moves %s" % (start_fen, " ".join(moves[:i])))
        lines.append("go movetime %d" % movetime)
    lines.append("quit")

    out = subprocess.run([engine], input="\n".join(lines) + "\n",
                         capture_output=True, text=True, timeout=900).stdout

    # Split the output per search: a `bestmove` ends one position's output.
    pvs, cur = [], []
    idx = 0
    for line in out.splitlines():
        if line.startswith("info ") and " pv " in line:
            cur.append(line.split(" pv ", 1)[1].split())
        elif line.startswith("bestmove"):
            pvs.append((idx, cur))
            cur = []
            idx += 1
    return pvs


def main():
    engine = sys.argv[1]
    pgn_path = sys.argv[2]
    n_games = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    movetime = int(sys.argv[4]) if len(sys.argv) > 4 else 60

    illegal = after_mate = total = 0
    done = 0
    with open(pgn_path, encoding="utf-8", errors="replace") as fh:
        while done < n_games:
            game = chess.pgn.read_game(fh)
            if game is None:
                break
            start_fen = game.board().fen()
            moves, board = [], game.board()
            for node in game.mainline():
                moves.append(node.move.uci())
                board.push(node.move)
            if len(moves) < 20:
                continue
            done += 1

            for ply, pvlist in run_game(engine, start_fen, moves, movetime):
                start = chess.Board(start_fen)
                for mv in moves[:ply]:
                    start.push(chess.Move.from_uci(mv))
                for pv in pvlist:
                    total += 1
                    b = start.copy()
                    mated = False
                    for uci in pv:
                        if mated:
                            after_mate += 1
                            if after_mate <= 3:
                                print("  AFTER-MATE from %s | pv %s"
                                      % (start.fen(), " ".join(pv)))
                            break
                        try:
                            mv = chess.Move.from_uci(uci)
                        except ValueError:
                            illegal += 1
                            break
                        if mv not in b.legal_moves:
                            illegal += 1
                            if illegal <= 3:
                                print("  ILLEGAL %s from %s | pv %s"
                                      % (uci, start.fen(), " ".join(pv)))
                            break
                        b.push(mv)
                        if b.is_checkmate():
                            mated = True

    print("%-22s %d games  %5d PVs   illegal: %d   after mate: %d"
          % (engine.rsplit("/", 1)[-1], done, total, illegal, after_mate))
    return 1 if (illegal or after_mate) else 0


if __name__ == "__main__":
    sys.exit(main())
