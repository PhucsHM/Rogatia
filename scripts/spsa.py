#!/usr/bin/env python3
"""SPSA tuner for Rogatia's UCI spin options, driving fastchess.

fastchess has no built-in tuning, so this drives it from outside: each
iteration perturbs every parameter at once, plays a short match between the two
perturbed configurations, and nudges every parameter by the same measured
result.  That is the whole point of SPSA -- the cost per iteration is two
evaluations regardless of how many parameters you are tuning, where a naive
gradient would need two per parameter.

No rebuild is ever needed.  Every constant in src/tunable.h is exposed as a UCI
spin option, so a configuration is just a set of `option.Name=value` arguments.

  python scripts/spsa.py --selftest              # validate the maths, no games
  python scripts/spsa.py --engine ./rogatia-base --games 40000
  python scripts/spsa.py --resume spsa-results/state.json

**Run --selftest first.**  A tuning run costs one to three days of the only
SPRT machine, and the failure mode of a broken driver is not a crash -- it is
plausible-looking numbers that are worse than what you started with.  The
self-test optimises a function whose answer is known and asserts convergence,
which catches sign errors, scaling errors and clamping errors for free.

Nothing this produces is trustworthy until it wins its own SPRT against the
current defaults.  SPSA optimises against the noise it was shown; overfitting
is the normal outcome, not the exceptional one.
"""

import argparse
import json
import os
import random
import re
import subprocess
import sys
import time

# SPSA gain-sequence exponents.  These are the values from the original Spall
# paper and are what fishtest and OpenBench both use; there is no reason to
# invent others.
ALPHA = 0.602
GAMMA = 0.101

# name, initial, min, max
# Defaults target the parameters that have never been tuned AND were chosen by
# reasoning rather than measurement -- everything added during Phase 7, plus
# LmrHistDiv, which was reset by hand when the LMR history statistic changed
# from one table to three.  Tuning all 36 at once converges far more slowly for
# the same games; this subset is where the error is most likely to be.
DEFAULT_PARAMS = [
    ("SingularDepth",    8,     4,    12),
    ("SingularMargin",  32,     8,   128),
    ("FpDepth",          8,     2,    12),
    ("FpMargin",       150,    40,   400),
    ("RazorDepth",       4,     1,     8),
    ("RazorMargin",    400,   100,   900),
    ("HistPruneDepth",   6,     2,    10),
    ("HistPruneMargin", 2048, 256,  8192),
    ("IirDepth",         4,     2,     8),
    ("LmrHistDiv",   24576,  2048, 65536),
]


class Param:
    def __init__(self, name, value, lo, hi, c_end=None, r_end=None):
        self.name, self.lo, self.hi = name, lo, hi
        self.value = float(value)
        # Perturbation size at the end of the run.  A twentieth of the range is
        # the usual starting point: large enough that a match can see the
        # difference, small enough not to be a different engine.
        self.c_end = c_end if c_end else max(1.0, (hi - lo) / 20.0)
        # Learning rate, and it must NOT be divided by c_end^2 -- a() already
        # multiplies by it, so dividing here cancels the term exactly and gives
        # every parameter the same absolute step regardless of range.  The
        # effective step is a/c ~ r_end * c_end, so keeping r_end constant makes
        # each parameter move in proportion to its own range, which is the
        # intent.  Sized so a consistent gradient can cross roughly half a range
        # over the run: the default 2500 iterations is few by fishtest
        # standards, and too small a rate simply does not move in that budget.
        self.r_end = r_end if r_end else 0.02

    def c(self, k, n):
        return self.c_end * ((n + 1) / (k + 1)) ** GAMMA

    def a(self, k, n, A):
        return self.r_end * self.c_end ** 2 * ((A + n + 1) / (A + k + 1)) ** ALPHA

    def clamped(self, v):
        return int(round(max(self.lo, min(self.hi, v))))


def parse_score(text, name_a):
    """Return engine A's score in [0, 1] from fastchess output."""
    m = re.search(r"Score of %s vs \S+:\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)" % re.escape(name_a), text)
    if not m:
        return None
    w, l, d = (int(g) for g in m.groups())
    n = w + l + d
    return None if n == 0 else (w + 0.5 * d) / n


def play(cfg, plus, minus, pairs):
    """One match between the two perturbed configurations. Returns A's score."""
    def opts(vals):
        return ["option.%s=%d" % (p.name, v) for p, v in zip(cfg.params, vals)]

    cmd = [cfg.fastchess,
           "-engine", "cmd=%s" % cfg.engine, "name=A", *opts(plus),
           "-engine", "cmd=%s" % cfg.engine, "name=B", *opts(minus),
           "-each", "tc=%s" % cfg.tc, "option.Hash=%d" % cfg.hash, "option.Threads=1",
           "-openings", "file=%s" % cfg.book, "format=epd", "order=random",
           "-rounds", str(pairs), "-games", "2", "-repeat",
           "-concurrency", str(cfg.concurrency), "-recover"]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=cfg.timeout).stdout
    return parse_score(out, "A")


def spsa(cfg, state=None):
    params = cfg.params
    n = cfg.iterations
    A = 0.1 * n
    k0 = 0

    if state:
        for p, v in zip(params, state["values"]):
            p.value = v
        k0 = state["k"]
        random.setstate(tuple(state["rng"]) if isinstance(state["rng"], list) else state["rng"])
        print("resumed at iteration %d/%d" % (k0, n))

    os.makedirs(cfg.outdir, exist_ok=True)
    log = open(os.path.join(cfg.outdir, "trace.csv"), "a", buffering=1)
    if k0 == 0:
        log.write("iter,games,score," + ",".join(p.name for p in params) + "\n")

    start = time.time()
    for k in range(k0, n):
        delta = [random.choice((-1, 1)) for _ in params]
        cs = [p.c(k, n) for p in params]
        plus = [p.clamped(p.value + d * c) for p, d, c in zip(params, delta, cs)]
        minus = [p.clamped(p.value - d * c) for p, d, c in zip(params, delta, cs)]

        score = cfg.objective(plus, minus) if cfg.objective else play(cfg, plus, minus, cfg.pairs)
        if score is None:
            print("iteration %d produced no result; skipping" % k, file=sys.stderr)
            continue

        # Every parameter is updated from the SAME match result.  Whether that
        # is signal or noise for any individual parameter averages out over
        # thousands of iterations -- which is why SPSA needs so many of them.
        for p, d, c, ak in zip(params, delta, cs, (p.a(k, n, A) for p in params)):
            p.value = max(p.lo, min(p.hi, p.value + ak * (score - 0.5) / (c * d)))

        log.write("%d,%d,%.4f,%s\n" % (k, (k + 1) * cfg.pairs * 2, score,
                                       ",".join("%.2f" % p.value for p in params)))

        if k % cfg.checkpoint == 0 or k == n - 1:
            with open(os.path.join(cfg.outdir, "state.json"), "w") as f:
                json.dump({"k": k + 1, "values": [p.value for p in params],
                           "rng": random.getstate()}, f, default=list)
            if not cfg.objective:
                done = (k + 1) * cfg.pairs * 2
                rate = done / max(time.time() - start, 1) * 3600
                print("iter %5d/%d  %6d games  %.0f games/h  eta %.1f h" % (
                    k + 1, n, done, rate,
                    (n - k - 1) * cfg.pairs * 2 / max(rate, 1)))
                print("   " + "  ".join("%s=%d" % (p.name, p.clamped(p.value)) for p in params))
    log.close()
    return params


def selftest():
    """Optimise a function whose answer is known, with no games played.

    Catches the failures that matter: a sign error walks away from the optimum,
    a scaling error stalls, a clamping error pins parameters at a bound.  All
    three look like "SPSA just didn't find much" after two days of real games.
    """
    target = {"SingularDepth": 10, "FpMargin": 260, "LmrHistDiv": 40000}
    params = [Param(nm, init, lo, hi) for nm, init, lo, hi in DEFAULT_PARAMS
              if nm in target]

    def objective(plus, minus):
        # A quadratic bowl centred on `target`, in units of each range so no
        # single parameter dominates.  Returns the score of `plus`, so better
        # means closer.
        def loss(vals):
            return sum(((v - target[p.name]) / (p.hi - p.lo)) ** 2
                       for p, v in zip(params, vals))
        # Squash into [0,1] the way a match score behaves.
        return 0.5 + 0.5 * max(-1.0, min(1.0, (loss(minus) - loss(plus)) * 8))

    class Cfg: pass
    cfg = Cfg()
    cfg.params, cfg.iterations, cfg.pairs = params, 3000, 1
    cfg.outdir, cfg.checkpoint, cfg.objective = "spsa-results/selftest", 10 ** 9, objective

    print("optimising a known quadratic; no games are played")
    print("start :  " + "  ".join("%s=%d" % (p.name, p.clamped(p.value)) for p in params))
    spsa(cfg)
    print("end   :  " + "  ".join("%s=%d" % (p.name, p.clamped(p.value)) for p in params))
    print("target:  " + "  ".join("%s=%d" % (nm, target[nm]) for nm in target))

    ok = True
    for p in params:
        span = p.hi - p.lo
        err = abs(p.clamped(p.value) - target[p.name]) / span
        flag = "ok" if err < 0.10 else "FAIL"
        if err >= 0.10:
            ok = False
        print("  %-16s within %.1f%% of range  [%s]" % (p.name, err * 100, flag))
    print("\nSELFTEST %s" % ("PASSED -- the maths converges" if ok else
                             "FAILED -- do not spend games on this driver"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--engine", default="./rogatia-base")
    ap.add_argument("--fastchess", default="./tools/fastchess.exe")
    ap.add_argument("--book", default="books/UHO_Lichess_4852_v1.epd")
    ap.add_argument("--tc", default="8+0.08")
    ap.add_argument("--hash", type=int, default=16)
    ap.add_argument("--concurrency", type=int, default=8)
    ap.add_argument("--games", type=int, default=40000)
    ap.add_argument("--pairs", type=int, default=8,
                    help="game pairs per iteration; 8 keeps fastchess startup "
                         "under ~5%% of wall clock at this time control")
    ap.add_argument("--outdir", default="spsa-results")
    ap.add_argument("--checkpoint", type=int, default=10)
    ap.add_argument("--timeout", type=int, default=3600)
    ap.add_argument("--resume", default=None)
    cfg = ap.parse_args()

    if cfg.selftest:
        return selftest()

    cfg.params = [Param(*p) for p in DEFAULT_PARAMS]
    cfg.iterations = max(1, cfg.games // (cfg.pairs * 2))
    cfg.objective = None

    state = json.load(open(cfg.resume)) if cfg.resume else None
    print("%d parameters, %d iterations, %d games, tc=%s" % (
        len(cfg.params), cfg.iterations, cfg.games, cfg.tc))
    spsa(cfg, state)

    print("\nPaste into src/tunable.h, then SPRT it against the current defaults.")
    print("A tuned set that has not beaten the old one in its own test is a"
          " guess with extra steps.\n")
    for p in cfg.params:
        print("    X(%-16s %6d, %6d, %6d)      \\" % (p.name + ",", p.clamped(p.value), p.lo, p.hi))
    return 0


if __name__ == "__main__":
    sys.exit(main())
