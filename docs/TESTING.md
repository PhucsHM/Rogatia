# Testing

Everything here runs from the repo root. One-time setup on a fresh machine:

```bash
scripts/setup-testing.sh     # fastchess + books + reference engines
```

It fetches into `tools/` and `books/`, both gitignored. The script is the only
record of where those binaries came from — keep it current.

---

## The baseline binary

SPRT compares two binaries. The one you compare *against* is the **baseline**:
the last build that passed a test. Binaries are gitignored (`*.exe`, `rogatia`),
so the durable artifact is a **git tag**, and the binary is rebuilt from it.

Convention: the tag is `base-phase<N>`, the binary is `rogatia-base`.

```bash
git tag -a base-phase3 -m "Phase 3 SPRT baseline, bench 54095910"
git checkout base-phase3 && make CXX=g++ EXE=rogatia-base && git checkout -
./rogatia-base bench            # must print the bench count in the tag message
```

Rebuild the baseline whenever a patch passes and becomes the new reference —
move the tag forward, rebuild `rogatia-base`, and note the new bench count.

Always verify the baseline's bench before trusting a result. Two binaries with
different bench counts than you expect means you tested the wrong thing.

**The bench depth changed at commit `655f93c`** (Phase 4), from 8 to 12. Bench
counts in commit messages before that commit are depth-8 numbers and are not
comparable with anything after it — including `base-phase3`'s 54095910, which
only reproduces by checking the tag out and running `./rogatia-base bench 8`.

---

## Running an SPRT

```bash
scripts/sprt.sh ./rogatia ./rogatia-base            # bounds [0, 5], the current band
scripts/sprt.sh ./rogatia ./rogatia-base -10 0      # non-regression
CONCURRENCY=4 TC=20+0.2 scripts/sprt.sh ./a ./b     # verify a passing patch
```

Env overrides: `TC`, `CONCURRENCY`, `BOOK`, `HASH`, `ROUNDS`. Logs and PGNs land
in `sprt-results/`.

**Bounds by strength** — always `alpha=0.05 beta=0.05`:

| Engine strength | Bounds |
|---|---|
| under ~2800 | `[0.00, 10.00]` |
| ~2800–3300 | `[0.00, 5.00]` |
| above ~3300 | `[0.00, 3.00]` |
| non-regression (simplification, refactor) | `[-10.00, 0.00]` |

The bounds tighten as the engine strengthens because the patches get smaller.
A `[0, 10]` test at 3400 would accept noise; a `[0, 3]` test at 2200 would burn
a day of games proving something a `[0, 10]` test settles in an hour.

**Books:** `8moves_v3.epd` (balanced) under ~2800, `UHO_Lichess_4852_v1.epd`
(biased, fewer draws, faster convergence) above it. The two harnesses now pick
different defaults on purpose: **`sprt.sh` uses the sharp book**, because past
~2800 a balanced one draws too often to resolve a small patch, while
**`gauntlet.sh` stays on the balanced book**, because a rating is only
meaningful against the runs it is compared with and the current 3195 figure was
measured there. `BOOK=` overrides either. Both names live in `lib.sh`.

**Concurrency 8, not 16 or 20.** Physical cores only. SMT siblings distort
timing at 8+0.08 and produce spurious losses on time, which look exactly like a
patch regression. `lib.sh` derives `physical_cores - 2`.

**Never run two matches at once on one machine.** Oversubscription corrupts
both measurements.

---

## Reading the output

fastchess prints this block on finish and periodically during the run:

```
Results of dev vs base (8+0.08, 1t, 16MB, 8moves_v3.epd):
Elo: 4.21 +/- 6.83, nElo: 5.90 +/- 9.55
LOS: 88.60 %, DrawRatio: 31.2 %, PairsRatio: 1.09
Games: 1600, Wins: 402, Losses: 383, Draws: 815, Points: 809.5 (50.59 %)
Ptnml(0-2): [12, 180, 421, 195, - ], WL/DD Ratio: 0.98
```

- **Elo** — the difference `dev - base`, with a 95% interval. Positive means dev
  is ahead; the interval bracketing 0 means the test has not decided yet.
- **nElo** — normalized Elo. Comparable across books and time controls; raw Elo
  is not.
- **LOS** — likelihood dev is better than base. Not a verdict, just a running
  probability.
- **Ptnml(0-2)** — the pentanomial counts: game pairs scoring 0, 0.5, 1, 1.5, 2.
  fastchess uses pairs rather than individual games, which converges meaningfully
  faster. A roughly symmetric distribution around the middle bucket is what a
  no-difference result looks like.
- **SPRT** lines report the log-likelihood ratio and the accept/reject bounds.
  `H1 was accepted` = the patch is an improvement, commit it. `H0 was accepted` =
  no improvement above `elo0`, throw it away. Anything else means keep running.

Do not stop a test early because the Elo looks good. Early Elo swings wildly;
that is the entire reason SPRT exists.

### Harness self-test

Identical binaries must show no difference. Run it whenever the harness or the
machine changes:

```bash
ROUNDS=150 scripts/sprt.sh ./rogatia ./rogatia 0 10
```

Expected: Elo brackets 0 within its error bar and `Ptnml(0-2)` is roughly
symmetric. It will *not* reach a verdict in 300 games — that is fine and not
what the check is for. A harness reporting a confident gain on identical
binaries is worse than no harness.

Result on the laptop, 2026-07-27, both binaries at `base-phase3`:

```
Elo: 2.32 +/- 22.73, nElo: 4.01 +/- 39.32
LOS: 57.93 %, DrawRatio: 57.33 %, PairsRatio: 1.00
Games: 300, Wins: 102, Losses: 100, Draws: 98, Points: 151.0 (50.33 %)
Ptnml(0-2): [5, 27, 86, 25, 7], WL/DD Ratio: 2.74
```

+2.32 ± 22.73 brackets 0, the pentanomial is symmetric, LOS is a coin flip.
That is what a correct harness looks like on identical binaries.

---

## Gauntlet — the Elo *number*

An SPRT only says whether one build beats another. To put a number on the
engine you play rated opponents:

```bash
scripts/gauntlet.sh 240 ./rogatia
```

Anchors live in the `ANCHORS` line of `scripts/gauntlet.sh` with their CCRL
Blitz (40/4) ratings and error bars. The script prints, per opponent, the
W/L/D, the score, and `anchor_rating + measured_diff` with the two error bars
combined in quadrature.

An opponent that scores near 0% or 100% is reported as **saturated** and
yields no rating — the Elo model gives infinity there. Only anchors landing
between 5% and 95% contribute. If every anchor saturates, the anchor set is
wrong for the engine's current strength, not the engine.

Re-run the gauntlet at phase boundaries, not per patch. Per-patch measurement
is what SPRT is for.

---

## Two machines

| Machine | Cores | Role |
|---|---|---|
| Laptop — Ryzen AI 9 465, 10c/20t, RTX 5060 (8 GB), Windows | 10 physical | **Primary SPRT machine.** More physical cores than the home box. |
| Home box — Ryzen 7 7700, 8c/16t, RTX 3090 (24 GB), CachyOS, 24/7 | 8 physical | Datagen and **all NNUE training** — the 3090's 24 GB is what the laptop cannot match. Secondary SPRT when idle. |

**The two machines run different tests concurrently, never the same test.**
fastchess cannot pool two hosts onto one SPRT — there is no distributed mode,
and manually merging two PGN sets breaks the sequential stopping rule the test
depends on. So: laptop runs patch A, home box runs patch B, each to its own
verdict. Two tests per day instead of one test twice as fast.

Pooling machines is what **OpenBench** does, and it is the right answer — but
only above ~3300, where a single test needs 50k–150k games and one box genuinely
cannot finish it. Below that the setup cost buys nothing. Arrange OpenBench
access (via their Discord, with a working engine and a worker) before hitting
that wall, not after.

---

## The verification ladder

In increasing order of what each actually proves:

1. **`make run-perft`** — bit-exact vs published counts, 37/37. Pure
   correctness. Runs before every commit touching movegen or `Position`.
2. **`./rogatia bench`** — same node count on every machine, every build, every
   `-march` level. Guards OpenBench eligibility.
3. **SPRT vs baseline** — the only meaningful measure of a patch.
4. **Gauntlet vs rated engines** — the only thing that substantiates an Elo
   number.

Tactical suites (WAC, ERET, STS) smoke-test regressions only. They correlate
poorly with strength above ~2800. Never quote them as a strength claim.
