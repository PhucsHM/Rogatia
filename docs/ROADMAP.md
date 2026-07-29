# Rogatia roadmap

Target: **3500+ CCRL Blitz**. Blitz-optimized — measured on CCRL Blitz and tuned
at the time control it plays.

**No per-phase Elo gates, and no time estimates.** Earlier versions of this file
had both, and both were wrong: the phase targets ended up below the strength the
engine had already reached, which makes a roadmap worse than useless. Phases here
are *ordered work*. Measurements are recorded as history, after the fact, in the
table at the bottom.

## What moves the number

| | What it is | Where the Elo comes from |
|---|---|---|
| **Search work** | Writing C++ — pruning, reductions, extensions, move ordering | The bulk of it, in many small steps |
| **Network training** | Compute — self-play data, GPU training | Large single jumps, then compounding |

Search is the slow part; training is the fast part. Data generation runs at
thousands of positions per second, so the two overlap: the CPU generates data in
the background while search development continues.

**The network learns to imitate the search.** It is trained on the search's own
evaluations, not on game outcomes — supervised learning, not reinforcement
learning. A weak search teaches a weak net and no amount of self-play
compensates. That is why search quality gates everything.

## Locked decisions

| Decision | Choice | Rationale |
|---|---|---|
| Language | C++20 | Matches the modern engine idiom |
| Evaluation path | PSQT → NNUE, no full HCE | A tuned hand-crafted eval is a large body of work that NNUE then discards |
| NNUE data | Own self-play only | Datagen is needed regardless |
| Search paradigm | Alpha-beta, not MCTS | See below |
| Trainer | bullet (CUDA) | De-facto standard outside Stockfish |
| Test harness | fastchess, then OpenBench | |
| Move generation | Pseudo-legal + legality check | Legal movegen is a few % faster and much harder to get right |
| Sliding attacks | Black magic, PEXT opt-in | PEXT is microcoded on Zen 1/2, so it cannot be the default |
| State | Make/unmake with undo stack | Copy-make gets expensive with accumulators and five Zobrist keys |

## Why not Leela-style

Leela and Stockfish differ on three independent axes: search (MCTS vs
alpha-beta), network (large GPU net with policy and value heads vs small CPU eval
net), and training data.

**NNUE is already the hybrid.** Neural evaluation inside alpha-beta is what
Stockfish took from the AlphaZero line in 2020, and it beat both pure approaches.
Self-play-only training is already this project's policy.

MCTS with a policy network is rejected on three grounds: it needs GPU batching
and therefore per-move latency, which is wrong for blitz; one 3090 cannot
replicate a run that consumed thousands of volunteer GPUs; and CCRL tests on CPU,
so a GPU engine competes in a different category.

Training from zero knowledge does **not** require Leela's compute — alpha-beta is
a competent teacher from day one, whereas Leela had to learn from win/loss alone
with a random net. Stormphrax and Viridithas both train from random weights on
self-generated data and both are 3600+.

Worth revisiting later only as **a policy network for move ordering** inside
alpha-beta. Not replacing the search.

---

## Phases

### Phase 0 — Environment ✅
Toolchain, OpenBench-compatible Makefile, repo skeleton.

### Phase 1 — Board and move generation ✅
Bitboards, black magic sliders, five fixed-seed Zobrist key sets, make/unmake
with undo stack, pseudo-legal movegen with legality filter, perft with bulk
counting.

**Gate: perft bit-exact.** 37/37 checks, 626,461,214 nodes. The black-magic and
PEXT indexers produce identical counts and both are checked against a ray-walk
reference.

### Phase 2 — Search core and UCI ✅
Fail-soft PVS, iterative deepening, aspiration windows, quiescence with SEE and
delta pruning, bucketed TT with depth-preferred aging replacement, MVV-LVA/SEE
ordering, killers, butterfly history, mate distance pruning, soft/hard time
management, tapered PeSTO PSQT.

**Gate: bench deterministic** across `x86-64`, `v2`, `v3`, `native` and PEXT
builds.

### Phase 3 — Testing infrastructure ✅
fastchess SPRT with pentanomial statistics, OpenBench books,
`scripts/setup-testing.sh` to reproduce the harness on a fresh machine.

This deliberately comes *before* the features it validates. Published Elo figures
are order- and engine-dependent; only your own SPRT numbers mean anything.
Skipping it is how engine projects die — with a stack of patches that each
obviously helped and collectively lost Elo.

**Gate: harness self-test** on identical binaries reports no difference.

### Phase 4 — Core pruning ✅
Null move, LMR, reverse futility, late move pruning, SEE pruning, continuation
history. One commit each, on a bench-identical groundwork commit.

Notes worth keeping:
- LMR's `ln` table is **hardcoded integers, not `std::log`**, because bench has
  to be identical across libm implementations.
- Reverse futility uses a *linear* margin, which is what the literature converged
  on.
- Null move has no verification search; the non-pawn-material guard was enough.

Bench depth was raised 8 → 12 here, so counts either side of commit `655f93c`
are not comparable.

### Phase 5 — Datagen ✅
`rogatia datagen`, one process per thread. 8 random opening plies, no book,
node-limited self-play, a quiet filter, eval adjudication and Syzygy hard
adjudication.

Output is **bulletformat** (32 B/position) rather than viriformat — it is what
bullet consumes with no conversion step, and the compression buys storage that is
not scarce.

**Validation, four checks**, because bad data fails silently and expensively:
every record decodes to a legal position; score correlates with result as a clean
sigmoid; material balance *in the stored frame* tracks the stored score; and
`bullet-utils validate` agrees with our own decoder. The third check is the one
that catches a board and its labels ending up in different frames.

### Phase 6 — First NNUE ✅
`(768 → 256)x2 → 1`, SCReLU, QA=255, QB=64, incremental accumulator.

Trained at **wdl=0.3**, not bullet's example default of 0.75. At 0.75 the target
is mostly game outcomes, which saturates the net — the first scaffold net's evals
came out about twice the scale of the search scores, and every pruning margin is
calibrated to the old scale, so an inflated eval silently makes all of them more
aggressive. **If the eval scale ever changes again, re-check the pruning margins
before trusting an SPRT.**

The `-march` bench-determinism check belongs here, the moment inference exists.
NNUE accumulation order can differ between SIMD widths and silently break
OpenBench eligibility.

### Phase 7 — Full search build-out — **in progress**

Merged: singular extensions, correction history, time management, Syzygy
probing, and a PV assembly fix. Parked: the repetition ply distinction (too small
to resolve) and the fifty-move eval taper (rejected; its TT-cutoff half was split
out and is queued separately).

**This is a breadth pass, not a depth pass.** The aim is to bring the search to
its strongest state by trying as many known techniques as the machine has time
for. Machine time is the binding constraint — one test at a time — so no single
idea is allowed to consume the phase.

1. **Build the idea** on its own `phase7-*` branch, with an inert setting that
   returns bench to base exactly. That separates the SPRT result from a bug in
   the scaffolding.
2. **Test it** to a verdict, *or* to the conclusion that there is no verdict at
   these bounds.
3. **Park it** if it is overwhelmingly negative or cannot resolve. Keep the
   branch and the resume state.
4. **Retune later** from the parked snapshot, the engine's own PGNs, and
   published numbers from other engines.

Stall limits live in `scripts/testqueue.sh` and are derived from the bounds:
20,000 games at `[0, 5]`, 30,000 at `[0, 3]`. Past that with the LLR still inside
±0.6, the effect is too small to resolve and that answer is already in hand.

It is deliberately **not** an early abort on a losing result — SPRT rejects a real
loss quickly by itself, and second-guessing it throws away verdicts that were
about to arrive.

**Never delete a `phase7-*` branch.** A parked branch is a saved starting point.

**The phase is aimed by measurement, not by this list.** Analysis of the engine's
own games found one dominant weakness — see "Phase 7 conversion work" and the
game analysis in `CHANGELOG.md`. Two items changed status as a result: **SPSA is
dropped** on this hardware, because a test of the best-attested tuning gain
returned a negative and the published constants need tens of thousands of games
to move a parameter; and **SMP is deprioritised**.

Still to try: ProbCut, multicut, check extensions reworked as an LMR term, the
full history stack (capture history, more continuation offsets, a side-to-move
dimension), and correction-magnitude as an uncertainty signal to widen RFP
margins and shrink LMR.

### Phase 8 — Scale the net
Roughly 1B positions of self-play from the much stronger post-Phase-7 engine,
retrained at `(768 → 1024)x2 → 1` with 8 output buckets.

Reference point: Alexandria reached CCRL Blitz top-6 on `(768→1536)x2→1x8` — no
king buckets, no L2/L3. **Net size is not what gates strength; search quality is.**
Do not chase a bigger net to fix a search problem.

### Phase 9 — Iterate
King input buckets (4→8→16, horizontally mirrored) with Finny tables, then L2/L3
layers, threat inputs, and AVX-512/VNNI dispatch. Each cycle is datagen, training
and a few thousand SPRT games, worth progressively less.

At a 3500 target this phase is required rather than optional, and **OpenBench
moves from nice-to-have to requirement** — above ~3300 a patch worth a few Elo
needs 50k–150k games and one machine cannot supply them.

### SMP — after Phase 8, not instead of it

CCRL Blitz is a **single-CPU list**, so multithreading contributes nothing to the
stated target. It does not speed up testing either: an SPRT runs
`option.Threads=1` at concurrency 8, and making the engine multithreaded changes
none of that.

It is still worth building for tournaments, the 4CPU lists and ordinary use — but
build it deliberately, not as the tail of an overnight batch. A threading bug is
the one class of defect none of this project's gates can catch: perft is
single-threaded, bench is single-threaded, and an SPRT runs `Threads=1`.

Design, when it happens — **lazy SMP**: N threads search the root independently
and share the transposition table, diverging naturally through timing.

1. `Worker` becomes per-thread. History, continuation history, correction
   history, the PV array and the stack all move with it. The TT stays shared —
   that is the whole mechanism.
2. The TT needs no locks. A torn entry yields a garbage move, and every TT move
   already goes through `is_pseudo_legal` before it is played.
3. Only the main thread reports. `print_info`, `bestmove` and the time checks
   belong to thread 0.
4. `Threads` stops being accepted-and-ignored.
5. Determinism is lost above one thread and that is expected. `bench` must keep
   running single-threaded so the fingerprint survives.

---

## Measured strength, as history

Recorded after the fact. Anchors and conditions differ between rows, so these are
a record of progress rather than a comparable series.

| When | Result | Conditions |
|---|---|---|
| Phase 3 | ~2197 +/- 29 | 3 anchors, 240 games each |
| Phase 4 | 2799 +/- 42 | 720 games; anchors already saturating |
| Phase 6 | 3195 +/- 24 | 3 versions of one engine, 8+0.08 |
| Phase 7 (current) | **3379 +/- 20** | 6 anchors, 4 families, `tc=120+1` |

**The Phase 7 row is not comparable with the ones above it.** It is the first
measured at CCRL Blitz's own time control, and the first run with tablebases
enabled — no script passed `SyzygyPath` before then, so every earlier number
measured an engine roughly 24 Elo below its own merged strength.

## Risks

- **Movegen bugs found later** are the classic project-killer. The perft gate is
  the entire mitigation and must stay green on every change touching `Position`
  or move generation.
- **Untested patches accumulating.** Phase 3 before Phase 4 exists to prevent it.
- **Determinism drift** silently breaks OpenBench eligibility. Fixed Zobrist
  seeds from day one, `-march` bench equality checked from Phase 6.
- **Testing throughput above ~3300.** Patches worth a few Elo need 50k–150k games
  each. Arrange OpenBench access before hitting the wall.
- **Gates that cannot fail.** Two were found in one day: a perft runner that
  segfaulted before printing anything on one machine, and a tablebase option no
  script ever passed. A gate that cannot run reads as a passing one.
