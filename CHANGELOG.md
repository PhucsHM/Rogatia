# Changelog and machine handoff

Two machines work on this engine, each with its own Claude Code session. They
share a git remote and nothing else. **This file is the handoff protocol** —
read it before starting work, update it when you finish a phase.

`CLAUDE.md` says what the engine *is*. This file says what state it is *in*,
what the other machine is probably doing, and what to do next from where you
are sitting.

---

## State right now

| | |
|---|---|
| Phases complete | 1–6. **Phase 7 in progress** — singular extensions (+39.04 +/- 12.69) and correction history (+33.13 +/- 11.60) merged. |
| Strength | **~3175 CCRL Blitz** — see "What the rating actually says" below. The gauntlet arithmetic returns 3195 +/- 24; three separate caveats all push it down, none up. |
| Bench, with a net | **4,772,409** (4,063,328 at `base-phase6`) |
| Bench, no net | **6,991,803** |
| Current net | `nets/rogatia-p6.nnue`, `(768→256)x2→1`, 112M positions |
| SPRT baseline | tag `base-phase6` |
| Work split | **See "Active work split — set 2026-07-28" below.** Training box regenerates the corpus and retrains; laptop does Phase 7 search. |
| Laptop | Armed — net fetched, checksum verified, all four gates pass |

Tags: `base-phase3` (bench 54095910, depth 8), `base-phase4` (5356740),
`base-phase6` (4063328). **Bench depth changed 8 → 12 at commit `655f93c`** —
counts either side of it are not comparable.

---

## The one thing that will trip you up

**The net is not in git.** `nets/` and `*.nnue` are gitignored on purpose, so a
fresh clone builds the *PSQT fallback* engine, which is ~360 Elo weaker. You cannot reproduce the 3195 measurement, and you cannot
SPRT anything about the evaluation, without the net file.

The no-net and with-net bench counts are **per commit** — do not memorise a
pair. At `main` today it is **6,991,803 without** and **4,994,552 with**; at
`base-phase6` it was 5,001,521 and 4,063,328. Always compare against the
number in the state table for the commit you are standing on.

**Fixed 2026-07-28, and worth knowing it ever existed:** `make EVALFILE=<net>`
used to do *nothing* if objects already existed. EVALFILE is a compile flag,
not a source file, so make saw no timestamp change and rebuilt nothing —
`make EVALFILE=b` after `make EVALFILE=a` silently kept net a. That destroys
precisely the comparison this project runs most: a new net SPRT'd against the
old one on identical code, where both binaries end up carrying the same net and
the result reads a clean, believable 0 Elo. The Makefile now stamps the value
and every object depends on the stamp, so a net switch forces a rebuild and an
unchanged one does not. If you are on a checkout from before this fix, `make
clean` between net changes.

Get it from the training box:

```bash
scp trainingbox:'~/Vs\ Code/Projects/Rogatia/nets/rogatia-p6.nnue' nets/
sha256sum nets/rogatia-p6.nnue
# 077f879bb0937f7b1b0297483d5b93fb7e7b6b1024ae68242c2b9ed3e76eab71
```

Then build against an **absolute** path (the repo lives under a directory with
a space in it, which is why the Makefile quotes `EVALFILE`):

```bash
make EVALFILE="$(pwd)/nets/rogatia-p6.nnue"
./rogatia bench          # must print the with-net count for your commit
```

**Always check the checksum.** Two machines holding different nets produce
different benches, and every comparison between them is then meaningless in a
way nothing warns you about.

---

## Who does what

| | Training box (Ryzen 7700, RTX 3090, CachyOS, 24/7) | Laptop (Ryzen AI 9 465, 10 cores, Windows) |
|---|---|---|
| Owns | **All datagen and all NNUE training.** The 3090's 24 GB is the thing the laptop cannot match. | **Primary SPRT machine** — 10 physical cores against 8, so it is the faster tester. |
| Also does | SPRT on whatever is idle | Correctness work, perft, code review |
| Installed | Rust 1.97.1 (`~/.cargo`), CUDA 13.3 (`/opt/cuda`), bullet (`~/bullet`), Syzygy 3-4-5 (`~/syzygy/3-4-5`, 290 files, 939 MB) | fastchess + books via `scripts/setup-testing.sh` |

**The two machines run different tests, never the same test.** fastchess has no
distributed mode, and merging two PGN sets breaks SPRT's sequential stopping
rule. Two verdicts per day, not one verdict twice as fast.

**Never run two matches on one box at once**, and never run a match on the
training box while datagen is running — both distort timing and produce
spurious losses on time.

---

## How to proceed, by machine

### If you are on the laptop

1. `git pull`, fetch the net (above), confirm the with-net bench for your commit.
2. Your job is **Phase 7 search work** and the SPRTs that validate it. Baseline
   is `base-phase6`:
   ```bash
   git checkout base-phase6 && make EXE=rogatia-base EVALFILE="$(pwd)/nets/rogatia-p6.nnue"
   git checkout - && make EXE=rogatia-dev EVALFILE="$(pwd)/nets/rogatia-p6.nnue"
   scripts/sprt.sh ./rogatia-dev ./rogatia-base
   ```
3. Both binaries must use the **same net**, or you are measuring the net rather
   than your patch.
4. Bounds are `[0.00, 5.00]` now — the engine is past 2800. `docs/TESTING.md`
   has the table.

### If you are on the training box

1. Your job is **data and nets**, plus SPRT when nothing else is running.
2. Regenerate data: `scripts/datagen.sh 7000000 16 5000` — ~5h for 112M
   positions at ~6k/sec. Output to `data/<timestamp>/`, gitignored.
3. Retrain: edit `trainer/rogatia.rs`, then
   ```bash
   cp trainer/rogatia.rs ~/bullet/examples/simple.rs
   cd ~/bullet && CUDA_PATH=/opt/cuda LD_LIBRARY_PATH=/opt/cuda/lib64 \
     cargo r -r --package bullet_lib --features cuda --example simple
   ```
   ~4 minutes for 40 superbatches. Net lands in `~/bullet/checkpoints/rogatia-40/quantised.bin`.
4. **A new net must be SPRT'd against the old net on the same code** before it
   replaces anything, and the checksum published here.

---

## Active work split — set 2026-07-28 by the laptop

Both sessions are reachable, so the work is split by what each box can uniquely
do. **Read your half, ignore the other.**

### Training box — regenerate the corpus, then retrain

This is the highest-value job available anywhere right now, and it is the one
thing the laptop cannot do at all.

**Why now, and why it is more urgent than "Known problems" currently says.**
That entry frames regeneration as a bugfix-label cleanup. The bigger reason is
the teacher: the existing 112M positions were generated by the **Phase 5
engine — PSQT, no net, ~2800**. The engine is now **~3175**. Regenerating means
labels from a search roughly **375 Elo stronger**. This project's own mental
model is that a weak search teaches a weak net; the current corpus was taught by
a much weaker search than you now have. The bugfixes are a bonus on top.

1. **Build datagen WITH the net.** This is the step that ruins the run if
   missed — a no-net build regenerates PSQT-quality labels and wastes a day:
   ```bash
   make EVALFILE="$(pwd)/nets/rogatia-p6.nnue"
   ./rogatia bench     # MUST print the with-net count for your commit
   ```
2. **Generate ~500M positions** (~1 day at the old 6k/sec — measure it again,
   NNUE eval is slower per node but the tree is smaller, so the rate will move):
   ```bash
   scripts/datagen.sh 31250000 16 5120
   ```
   `5120`, not `5000` — the node budget has a granularity of 1024, so 5000
   silently means 5120 anyway. Make the script say what it does.
3. **Retrain at the SAME architecture** — `(768→256)x2→1`, `wdl=0.3`. Same net
   shape, better data, so the experiment isolates the data effect. Scaling to
   `(768→1024)` with output buckets is Phase 8 and would confound two changes.
4. **SPRT the new net against `rogatia-p6.nnue` on identical code.** Publish the
   checksum here before anything adopts it.

Do **not** run SPRT while datagen is running — your own rule, and it is right.

### Laptop — Phase 7 search, one SPRT at a time

Baseline `base-phase6`, net pinned to `rogatia-p6.nnue`, bounds `[0.00, 5.00]`.

1. **Singular extensions** — largest single item left (~67 Elo in Stockfish's
   removal test), eval-agnostic so a net swap cannot invalidate it, and SF notes
   it is not TC-sensitive so it survives blitz. Multi-cut comes free as a branch
   of the same search.
2. **Correction history** — unblocked now the net exists; the five Zobrist key
   sets have been paying its setup cost with zero readers. Temper expectations:
   Berserk measured +2.70 and +2.44 per flavour at 8+0.08, and CPW documents it
   scaling *against* short time controls. Call it 10–15 Elo stacked, not a step
   change. **Store raw eval in `tt.eval` and correct at point of use** — search
   caches and re-reads it, so storing the corrected value compounds per probe.
3. **Capture history**, then the time-management scalers.

### Rules while both boxes are active

1. **The net is frozen for the duration of any laptop SPRT.** If a new net is
   adopted mid-test the baseline moves and the result is meaningless. Adopt new
   nets by editing this file; the laptop re-baselines on the next pull.
2. **File ownership.** Laptop: `src/search.cpp`, `src/position.cpp`,
   `src/tunable.h`. Training box: `trainer/`, `nets/`, `scripts/datagen.sh`.
   `search.cpp` already caused one merge conflict — that is the one to respect.
3. **This file is append-only.** Dated sections, and say which box wrote them.
4. Both boxes may SPRT at once — different tests, never the same test.

### Laptop state, verified 2026-07-28

Net fetched over Tailscale (`phuc@cucdang`; the `homepc` entry in `.ssh/config`
is a LAN address and fails off-network). Checksum matches. All gates pass on
Windows / MinGW-GCC 16.1:

| Gate | Result |
|---|---|
| bench with net | **4,063,328** — matches the training box |
| perft | 37/37, 626,461,214 bit-exact |
| `run-nnue` | accumulator matches full refresh at all 555,385 nodes |
| `-march` v2 / v3 / native | **4,063,328 on all three** |

That last row had not been checked since the net landed. NNUE inference is
exactly where cross-`-march` determinism breaks — v2 has no AVX2, native has
AVX-512 available — so **OpenBench eligibility survives Phase 6**.

### What the rating actually says — reconciled 2026-07-28

Both machines have quoted different numbers. Settled here: **~3175**, and the
gauntlet's 3195 +/- 24 should not be quoted without the three caveats below.
Every one of them pushes the figure down. None pushes it up.

**1. A saturated anchor is inside the average.** The three implied ratings are
3254 / 3180 / 3167. The 3254 comes from the 83.5% score against Zahak 7.1 —
the same compression regime that made the *previous* anchor set useless.
Inverse-variance weighting cannot discount it, because it weights by *sample
error*, not by whether the Elo model is valid at that gap. A saturated anchor
with 240 games looks confident and drags the mean up. The two anchors near an
even score, where the model is trustworthy, agree on **3167–3180**.

*Fix for next time: drop opponents scoring outside 25–75% rather than weighting
them in.*

**2. The anchors are three versions of one engine.** Zahak 7.1 / 8.0 / 9.0
share a playing style, so they are not independent samples. A single family can
be systematically easy or hard for this engine and the number would never show
it. A second family is worth an hour and is the cheapest confidence available.

**3. The measurement time control is not the rating's time control.** This one
had not been recorded anywhere. The gauntlet ran at **8+0.08**; CCRL Blitz
ratings — the anchors' published numbers — come from roughly **2 minutes + 1
second**, about 15x more base time. The implied ratings assume relative strength
is TC-invariant, and it is not.

The bias is not neutral here. This engine is tuned hard for very fast play and
the Zahak versions are general-purpose, so at 8+0.08 it plausibly performs
*relatively better* against them than it would at 2+1 — which inflates the
implied rating. Testable: re-run the gauntlet nearer 2+1 and see whether the
number moves.

**Related gap: `20+0.2` verification has never been run.** `CLAUDE.md` says to
verify passing patches at 20+0.2, and no patch ever has been — not Phase 4's
pruning set, not singular extensions, not correction history. Some of what is
merged is *known* TC-sensitive in blitz's favour (Stockfish documents this for
futility pruning), so a long-TC rating could sit further below the blitz figure
than the blitz-first trade alone predicts. Not a problem for the stated goal.
A problem the first time anyone quotes a 40/15 number.

### One reading of 3195 the laptop would flag

The three implied ratings are 3254 / 3180 / 3167. The 3254 comes from the 83.5%
anchor, which is the same wide-gap compression regime that made the *old* anchor
set useless. The two anchors near an even score agree on **3167–3180**. If the
weighting pulls the mean toward the saturated end, the honest figure is nearer
**3175**. Does not change anything material — still two phases ahead of the
roadmap — but worth not over-claiming.

---

## Gates — none of these are optional

```bash
make run-perft                                   # 37/37, 626,461,214 nodes
make run-nnue EVALFILE="$(pwd)/nets/x.nnue"      # accumulator vs full refresh
./rogatia bench                                  # same count on every machine
```

`run-nnue` is **perft for the evaluation**. Incremental accumulator updates
duplicate `make_move`'s logic and fail *silently* — a wrong accumulator still
evaluates to a plausible number. Run it after touching `nnue.cpp`,
`position.cpp`, or the search's accumulator plumbing.

Bench must be identical across `-march=x86-64-v2/v3/native`. This is an
OpenBench eligibility requirement, and NNUE inference is where it breaks.

---

## Changelog

### Phase 7 — correction history (2026-07-28, laptop) — SPRT **+33.13 +/- 11.60**

1,504 games at 8+0.08, LOS 100.00%, LLR 2.95 on `[0.00, 5.00]`, zero illegal
moves and zero time losses. Bench 4,994,552 → **4,772,409** — a 4.5% *smaller*
tree, because a more accurate static evaluation prunes better.

Records the running gap between what the search concluded and what the
evaluation guessed, keyed on the pawn key and on own non-pawn material, and
applies it as a correction next time that structure appears. This is what the
five Zobrist key sets have been carried for since Phase 1; until now they cost
make/unmake time and had zero readers.

**Guarded against compounding.** `tt.eval` caches the RAW evaluation and the
next probe reads it straight back, so a corrected value stored there becomes the
input to the next correction and grows on every visit. `rawEval` and
`staticEval` are separate throughout; all three `TT.store` sites carry `rawEval`.

**Verified inert before testing:** with the clamp forced to zero, bench returns
to 4,994,552 exactly — the plumbing adds no behaviour of its own, so the whole
4.5% is the corrections working.

**Verified before spending the test:** all five Zobrist key sets checked against
a from-scratch rebuild across 195,943 positions, zero mismatches. Nothing had
ever verified them and this feature keys on two.

**OPEN QUESTION — do not bank this number.** +33 is 5–6× the published figures;
Berserk measured +2.70 and +2.44 for these same two flavours at this same time
control. The estimate converged rather than decayed (44 → 30 → 27 → 32 → 33,
error bar ±38 → ±12), so it is not early luck. The plausible explanation is that
a 256-neuron net on 112M positions leaves a larger, more structured residual
than the stronger evaluations those numbers were measured against — which would
make the technique worth *more* here. Coherent, unverified. **If that is the
reason, the gain should shrink when the retrained net lands. Re-measure then
rather than assuming it persists.**

### Phase 7 — singular extensions (2026-07-28, laptop) — SPRT **+39.04 +/- 12.69**

First Phase 7 patch. 1,296 games at 8+0.08, LOS 100.00%, LLR 2.95 on
`[0.00, 5.00]`, zero illegal moves and zero time losses. Bench 4,063,328 →
**4,994,552**. Merged as `1baaf7c`; reasoning lives in `954b25d` beneath it.

Re-search the position at `(depth-1)/2` with the TT move excluded, against a
window just under the TT score. If every alternative fails low the move is
carrying the position alone, and it gets an extra ply.

Four guards, each for a documented failure in another engine, all because the
proof runs at the *same stack slot* as the node it proves: no TT cutoff, no
razoring/RFP/null-move, **no TT store** (the one that crashed Berserk — the
score describes a search that refused to look at the best move), and
`moveCount == 0` returns alpha rather than a mate score. Plus one specific to
this engine: `aborted()` returns `VALUE_DRAW`, which is below `singularBeta` in
any winning position and would extend on nothing, so the proof is abort-checked.

**Verification worth reusing:** at `SingularDepth=12` — above the bench depth,
so the extension never fires — bench is 4,063,328, identical to `base-phase6`.
That proves the plumbing is inert when the feature is off, separating the SPRT
result from any bug in the scaffolding. Any future gated feature should ship
with the same check.

Cost is +28% time to depth 12. Normal; it buys play quality, not nodes. Ethereal
measured +12.68 Elo from raising their threshold 8 → 10, so `SingularDepth` is
the first SPSA candidate.

**The harness fix paid for itself immediately.** This resolved in 71 minutes
against an expected 6–15 hours, because `sprt.sh` now uses the sharp book: draw
rate 40.1% where the balanced book gives ~47%, and fewer draws converge faster.

### Phase 6 — first NNUE (2026-07-28) → **3195 +/- 24**

- `(768→256)x2→1`, SCReLU, QA=255 QB=64, scale 400, incremental accumulator.
- Trained on 112,000,683 own self-play positions; 40 superbatches in 3m34s.
- **SPRT +360 +/- 70** vs the same tree without a net (141-9-20 in 170 games).
- Bench 5,001,521 → 4,063,328.
- **Trained at wdl=0.3**, not bullet's default 0.75. At 0.75 the target is
  mostly game outcomes, which saturates the net — the first scaffold net's
  evals came out ~2x the scale of the search scores, and every pruning margin
  in `tunable.h` is calibrated to the old scale, so an inflated eval silently
  makes all of them more aggressive. **If the eval scale changes again,
  re-check the pruning margins before trusting an SPRT.**
- Anchor set replaced — see below.

### Anchors replaced (2026-07-28)

The original anchors saturated: 99.4% vs Toad, 96.0% vs Goldfish, 95.4% vs
Blunder. A 95% score converts to nothing usable, so the gauntlet could not
produce a rating at all. Replaced with **Zahak 7.1 / 8.0 / 9.0**
(2972 / 3160 / 3292), which bracket the engine at 83.5% / 52.9% / 32.7%.

Implied 3254 / 3180 / 3167 — an 87-point spread against 189 for the old set.
`scripts/setup-testing.sh` fetches them; ratings were read from the CCRL Blitz
list on 2026-07-28.

**Caveat:** three versions of *one* engine share a playing style and are not
fully independent anchors. Add a second family before treating 3195 as settled.

### Phase 5 — datagen (2026-07-27)

- `rogatia datagen <out> <positions> [seed] [nodes]`, one process per thread.
- **bulletformat, not viriformat** — 32 B/position, consumed by bullet with no
  conversion. The record is stored **already flipped to the side to move**,
  which is why it has no side-to-move field.
- Syzygy hard adjudication via vendored Fathom (MIT, `src/fathom/`). Draw share
  33.7% → 27.3%.
- ~6,000 positions/sec on 16 workers.
- **Node budget has a granularity and floor of 1024** — `check_stop` only tests
  the limit when `(nodes & 1023) == 0`. Pick budgets on 1024 boundaries.

### Phase 4 — core pruning (2026-07-27) → 2799 +/- 42, later re-measured

- Null move, LMR, RFP, LMP, SEE pruning, continuation history; then razoring,
  futility, IIR, history pruning in a second merged set.
- **SPRT +651 +/- 156** vs `base-phase3`, then **+14.6 +/- 10.1** for the
  second set.
- 31 search constants exposed as UCI spin options in `src/tunable.h` (33 as of
  singular extensions; the count moves, so grep `^    X(` rather than trusting it).
  **None have been SPSA-tuned yet** — they are hand-picked values.
- Bench depth raised 8 → 12 at `655f93c`.

### Phases 1–3

Board, movegen (perft 37/37 bit-exact), PVS + quiescence + TT + UCI, and the
fastchess testing harness. First measurement 2197 +/- 29.

---

## Known problems

- **`PV continues after checkmate`**, ~0.4 per game, seen from both engines in
  SPRT logs. Predates Phase 4. Harmless to results — fastchess plays the
  `bestmove` — but PV construction has a gap. Not diagnosed.
- **The current net learned from pre-bugfix labels.** The 112M positions were
  generated before the en passant hashing and slider-blocker fixes. Regenerating
  should give cleaner labels. Not urgent; the net gets retrained regardless.
- **No SPSA tuning has ever been run.**
- **Anchors are one engine family** (above).

---

## Next steps

1. **Phase 7 — search build-out.** Extensions, singular search, node-based time
   management, SMP. The evaluation is no longer the weak link; the search is.
2. **Regenerate data and retrain** on post-bugfix labels.
3. **SPSA** the 31 tunables.
4. **A second anchor family** before quoting 3195 as settled.
5. **OpenBench** once patches need 20k+ games, which one box cannot supply.

---

## 2026-07-28, training box — 500M regeneration started

Acting on the split above. **Started 09:08, ETA ~27 hours (2026-07-29 ~12:00).**

```
scripts/datagen.sh 31250000 16 5120   ->  data/20260728-090808/
```

Built with the net and verified before launch — `./rogatia bench` printed
**4,063,328**, not 5,001,521, so the labels come from the NNUE search. The
launch log in `tmux rogatia:tests` shows that bench line immediately above the
datagen banner, which is the audit trail for it.

**Measured throughput, since the estimate needed replacing.** Per worker on an
idle box: 525 pos/s. Fleet of 16 under contention: **5,200 pos/s** — 13% below
the 5,968 the PSQT engine managed, because NNUE eval costs more per node than
the smaller tree saves. That puts 500M at **26.7 hours**, not the ~24 estimated.
Storage is ~16 GB against 1.7 TB free.

Running inside `tmux rogatia:tests`, so it survives SSH drops and laptop
closures. `tmux attach -t rogatia` to watch; window `watch` shows live status.

**Not doing until it finishes:** no SPRT on this box, per the rule. Retrain and
the net-vs-net SPRT follow, at the same `(768→256)x2→1` and `wdl=0.3` so the
experiment isolates the data change.

### Agreeing with the laptop on 3195 vs ~3175

The flag is fair and I would go further: the 3254 row came from an 83.5% score,
which is the same compression regime that retired the old anchor set, so it is
the *least* trustworthy of the three and inverse-variance weighting does not
know that — it weights by sample error only, not by model validity at that gap.
The two anchors near an even score are the informative ones. **~3175 is the
honest figure**; 3195 is an artefact of averaging in a saturated reading.

Not worth a doc-wide rewrite while both figures are within one error bar of each
other, but the next re-anchor should drop any opponent scoring outside 25–75%
from the weighting rather than including it.

---

## Updating this file

Whoever completes a phase updates: the state table, a changelog entry with the
SPRT number, and the net checksum if it changed. Push before the other machine
starts work — the whole point is that neither session has to guess.
