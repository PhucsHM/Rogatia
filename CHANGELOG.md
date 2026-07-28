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
| Phases complete | 1–6. **Phase 7 (search build-out) is next.** |
| Strength | **3195 +/- 24 CCRL Blitz** (2026-07-28, 720 games, Zahak bracket) |
| Bench, with a net | **4,063,328** |
| Bench, no net | **5,001,521** |
| Current net | `nets/rogatia-p6.nnue`, `(768→256)x2→1`, 112M positions |
| SPRT baseline | tag `base-phase6` |

Tags: `base-phase3` (bench 54095910, depth 8), `base-phase4` (5356740),
`base-phase6` (4063328). **Bench depth changed 8 → 12 at commit `655f93c`** —
counts either side of it are not comparable.

---

## The one thing that will trip you up

**The net is not in git.** `nets/` and `*.nnue` are gitignored on purpose, so a
fresh clone builds the *PSQT fallback* engine, which is ~360 Elo weaker and
benches 5,001,521. You cannot reproduce the 3195 measurement, and you cannot
SPRT anything about the evaluation, without the net file.

If `./rogatia bench` prints 5,001,521 you have no net. With the net it is
4,063,328.

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
./rogatia bench          # must print 4063328
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

1. `git pull`, fetch the net (above), confirm bench 4063328.
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
- 31 search constants exposed as UCI spin options in `src/tunable.h`.
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

## Updating this file

Whoever completes a phase updates: the state table, a changelog entry with the
SPRT number, and the net checksum if it changed. Push before the other machine
starts work — the whole point is that neither session has to guess.
