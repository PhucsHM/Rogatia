# Rogatia — engine development guide

UCI chess engine. C++20, GPL-3.0. Target: **3200+ CCRL**, tuned for **blitz (5+0 and faster)**.

Read this before touching anything. The full phased roadmap with Elo gates is in `docs/ROADMAP.md`.

---

## Two machines

| Machine | CPU / GPU | Role |
|---|---|---|
| **Laptop** — Ryzen AI 9 465 (10c/20t), RTX 5060 (8 GB), Windows | 10 physical cores | **Primary SPRT machine** — it has *more* physical cores than the home box, so it is the faster tester. Also code, correctness, perft. |
| **Home box** — Ryzen 7 7700 (8c/16t, Zen 4), 32 GB, RTX 3090 (24 GB), CachyOS, 24/7 | 8 physical cores | **All NNUE training and all datagen.** The 3090's 24 GB is the thing the laptop genuinely cannot match. Runs SPRT too, on whatever is idle. |

The split is by *what each machine is better at*, not by which one is "the dev box". CPU cores → SPRT (laptop wins 10 vs 8). VRAM → training (home box wins 24 GB vs 8 GB). The home box's real advantage is that it is on 24/7, so long datagen runs live there.

**The two machines run different tests concurrently, never the same test.** fastchess has no distributed mode and merging two PGN sets breaks SPRT's sequential stopping rule. Two verdicts per day, not one verdict twice as fast. See `docs/TESTING.md`.

---

## Two standing rules

**1. Perft must stay bit-exact.** Any change touching move generation, `make_move`/`unmake_move`, or `Position` state runs `make run-perft` before commit. A movegen bug found later costs tenfold to track down. This gate is never softened.

**2. Never merge an untested patch.** The most common way an engine project dies is a stack of "obviously correct" improvements that collectively lose Elo. Everything that changes search behaviour gets an SPRT. No exceptions, no "this is clearly better."

---

## Build

```bash
make                      # optimized native build -> ./rogatia
make debug                # -O0 -g + ASan/UBSan
make release              # portable x86-64-v3 static build
make run-perft            # the Phase 1 correctness gate
make bench                # deterministic node-count fingerprint
```

OpenBench requires `make EXE=<name>`, `CXX=<compiler>`, and `EVALFILE=<path>` to all work. Do not break them.

**Every commit message ends with the bench node count.** It is the search-behaviour fingerprint — it makes `git log` show at a glance which commits changed what the engine actually does. A commit that changes search but not bench, or vice versa, is a red flag worth investigating.

```
Add reverse futility pruning

bench 4712710
```

---

## Determinism (OpenBench eligibility)

`./rogatia bench` **must print the same node count on every machine and every build**, including across `-march` levels. Zobrist keys use a hardcoded seed for exactly this reason. From Phase 6 onward, NNUE inference is the risk — different SIMD widths can accumulate in different orders and silently diverge. Check bench equality across `-march=x86-64-v2 / v3 / native` whenever inference changes.

---

## Current status

**Phases 1, 2 and 3 complete. Phase 4 (core pruning) is next.** See `docs/ROADMAP.md` for the full arc.

Working now: bitboards, black magic attacks, five Zobrist key sets, make/unmake, movegen (perft 37/37, 626,461,214 nodes bit-exact), fail-soft PVS with iterative deepening and aspiration windows, quiescence with SEE and delta pruning, bucketed TT, killers, butterfly history, tapered PeSTO PSQT, full UCI, deterministic bench (54,095,910).

**Measured: ~2197 +/- 29 CCRL Blitz.** 720 games at 8+0.08, `8moves_v3.epd`, Hash=16, Threads=1, concurrency 8, laptop, 2026-07-27. Every game ended in a chess result — no time losses, no illegal moves.

| Opponent | CCRL Blitz | Games | W-L-D | Score | Implied Rogatia |
|---|---|---|---|---|---|
| Toad 1.0.0 | 1776 +/- 18 | 240 | 191-19-30 | 85.8% | 2089 +/- 57 |
| Goldfish 2.1.1 | 2252 +/- 16 | 240 | 73-99-68 | 44.6% | 2214 +/- 40 |
| Blunder 8.5.5 | 2664 +/- 11 | 240 | 7-200-33 | 9.8% | 2278 +/- 60 |

Inverse-variance weighted: **2197 +/- 29**. The three anchors disagree by 189 points — far more than their own error bars — which is the usual Elo-model compression at wide rating gaps, not a harness fault. Treat 2197 as approximate and the *ranking* (above Toad, just under Goldfish, far under Blunder) as the solid part. Re-anchor at phase boundaries, not per patch.

Reproduce: `scripts/gauntlet.sh 240 ./rogatia`. Full protocol in `docs/TESTING.md`.

Next concrete task: **Phase 4, core pruning** — null move, LMR, RFP, LMP, SEE pruning, continuation history. Every one of them goes through `scripts/sprt.sh` against the `base-phase3` tag. Only your own SPRT numbers mean anything; skipping that is how engine projects die with a stack of patches that each "obviously" helped and collectively lost Elo.

Known issue, not yet fixed: the engine emits **corrupt PV lines** (repeated moves, moves continuing past checkmate). fastchess warns but plays the `bestmove`, so it does not affect results. Worth a look in Phase 4.

### Mental model for what follows

Search work is the slow part (keyboard time, months). Training is the fast part (compute, days-to-weeks — 100M positions in ~1 day, 1B in 1–2 weeks). They **overlap**: once datagen exists it runs on the CPU 24/7 while search development continues.

The network is trained on **the search's own evaluations**, not game outcomes — supervised learning, not reinforcement learning. It learns to instantly imitate what a deep search would conclude. So **a weak search teaches a weak net**, and no amount of self-play compensates. Search quality gates everything.

The phase order deliberately front-loads the first net to roughly month 2, after only the core pruning set, rather than waiting for the full search build-out. The first net is scaffolding — it exists to make the next one possible, and it gets retrained 4–8 times regardless.

---

## SPRT protocol

Harness is **fastchess** (MIT, replaced cutechess-cli; fishtest migrated to it). Wrapped by `scripts/sprt.sh` and `scripts/gauntlet.sh`; `scripts/setup-testing.sh` installs it on a fresh box. **Read `docs/TESTING.md` before running a test** — bounds, output interpretation, the baseline-tag convention and the two-machine split live there.

```bash
fastchess \
  -engine cmd=./rogatia-dev  name=dev \
  -engine cmd=./rogatia-base name=base \
  -each tc=8+0.08 option.Hash=16 option.Threads=1 \
  -openings file=books/UHO_Lichess_4852_v1.epd format=epd order=random \
  -sprt elo0=0 elo1=5 alpha=0.05 beta=0.05 model=normalized \
  -rounds 100000 -concurrency 8 -recover
```

- **`-concurrency 8`, not 16.** SMT siblings distort timing at 8+0.08 and cause spurious losses on time. Use physical cores only.
- **Time control 8+0.08** for the SPRT. Verify passing patches at 20+0.2. Do not chase LTC-only patches — this engine is for blitz, so short-TC tuning bias is *aligned* with the goal.
- **Bounds by strength:** `[0.00, 10.00]` while under ~2800, `[0.00, 5.00]` mid, `[0.00, 3.00]` above ~3300. Non-regressions `[-10.00, 0.00]`. Always alpha=beta=0.05.
- **Books:** `8moves_v3.epd` while weak; switch to `UHO_Lichess_4852_v1.epd` above ~2800. Both from OpenBench's `Books/`, fetched by `scripts/setup-testing.sh`.
- Pentanomial (game-pair) statistics — fastchess does this by default and it converges meaningfully faster than trinomial.

**Throughput (measured, laptop, 8+0.08, concurrency 8):** **~1,180 games/hour** ≈ 28k games/day ≈ **one decisive test per day.** That is the real constraint on this project, not ideas. Plan accordingly.

**Do not SPRT the textbook.** Standard techniques (PVS, null move, LMR, the history stack) are known-good from the literature — implement, sanity-check, move on. Spend SPRT budget on tuning and genuinely novel changes.

Above ~3300 a single test needs 50k–150k games and one box is not enough. The answer is **OpenBench** — contribute this machine's threads 24/7 to a shared instance, draw on pooled hundreds of cores. Access is arranged through the OpenBench Discord; you show up with a working engine and a worker.

---

## Why not Leela-style (settled — do not re-litigate)

Leela and Stockfish differ on three independent axes: search (MCTS vs alpha-beta), network (large GPU net with policy+value heads vs small CPU eval net), and training data (own self-play vs mixed).

**NNUE is already the hybrid** — neural evaluation inside alpha-beta is exactly what Stockfish took from the AlphaZero line in 2020, and it beat both pure approaches. Self-play-only training is already this project's policy. So Rogatia already has the Leela ideas worth having.

MCTS with a policy network is rejected on three concrete grounds: it needs GPU batching and therefore per-move latency, which is wrong for **5+0 blitz**; one 3090 cannot replicate a run that consumed thousands of volunteer GPUs; and **CCRL tests on CPU**, so a GPU engine competes in a different category.

"Training from zero knowledge" does **not** require Leela's compute — alpha-beta is a competent teacher from day one, whereas Leela had to learn from win/loss alone with a random net. Stormphrax and Viridithas both train from random weights on self-generated data and both are 3600+.

Revisit only post-3400, and only as: **a policy network for move ordering** inside alpha-beta. Not replacing the search.

## NNUE (Phase 6+)

**Trainer: `bullet`** (MIT, Rust, CUDA). The 3090 is a first-class target and training is *not* the bottleneck — data generation is.

**Data policy — this is deliberate and non-negotiable: own self-play data only.** No Leela data, no Stockfish data, no third-party nets. Reasons: datagen has to be written regardless, the engine's own games deliver the same jump, and it makes provenance unimpeachable forever — no rating list or tournament can ever question the engine's originality. Leela's ODbL data exists as a fallback only if the bootstrap genuinely stalls, and using it would be a documented, deliberate decision.

Datagen settings: 8 random opening plies (no book — deliberate diversity), 5000-node soft limit per move, drop in-check positions and any where `|static eval − qsearch eval| > ~60cp`, adjudicate on sustained eval, Syzygy hard adjudication. Emit viriformat.

Expect ~4k–10k positions/sec on 16 threads ≈ 350M–850M/day. **Measure it; don't trust the estimate.**

Architecture progression — do not skip ahead, each step needs the data volume the previous one generated:
1. **Phase 6** — `(768 → 256)x2 → 1` on ~100M positions (~1 day of datagen). Validates the pipeline, expect +300–400 Elo → ~2800. Comes right after the core pruning set, *not* after the full search build-out.
2. **Phase 8** — `(768 → 1024)x2 → 1` with 8 output buckets on ~1B positions, generated by the much stronger post-Phase-7 engine. This is where 3200 happens.
3. **Phase 9** — king input buckets (4→8→16, horizontally mirrored) + Finny tables
4. **Phase 9** — L2/L3 layers, threat inputs, AVX-512/VNNI dispatch

Quantization: QA=255 (feature transformer, int16), QB=64 (output, int8), SCReLU, eval scale ~400.

Reference point: Alexandria reached CCRL Blitz **top-6** on `(768→1536)x2→1x8` — no king buckets, no L2/L3. **Net size is not what gates 3200; search quality is.** Do not chase a bigger net to fix a search problem.

---

## Licensing and provenance

GPL-3.0. Reading other engines is fine and is how the whole field works — copyright protects expression, not algorithms. **Transliterating is not.** Read a technique, close the file, implement from your own understanding. If a function has the same statement order, the same magic constants, and only renamed identifiers, that is a translation and translations are derivative works.

Log every non-obvious feature in `docs/PROVENANCE.md` with a one-line source note. Cheap insurance, and it defuses derivative accusations before they start.

Reference engines worth reading: **Stormphrax** (best modern C++; `src/tunable.h` maps the whole modern search), **Alexandria** (most readable), **Obsidian** (smallest strong C++), **Caissa** (MIT — the one you may actually copy from). Avoid **Motor** — no LICENSE file means all rights reserved.

---

## Blitz focus

The search architecture for 5+0 is identical to 40+40. Only these actually differ, so this is where "optimized for blitz" lives:

- **Time management.** Soft limit checked between ID iterations, hard limit checked every ~1024 nodes. Multiply the soft budget by three independent scalers: **node-based TM** (if the best move consumed most of the search tree the position is easy — move fast; ~15–25 Elo and the cheapest big win available), **best-move stability** (unchanged for many iterations → shrink the budget), and **score trend** (asymmetric — spend more when the eval is falling than when it is rising).
- **Move overhead** 10–50 ms. Losing on time costs a full point; a 3% weaker move costs ~0.005. The asymmetry is enormous — be conservative.
- **Don't over-allocate hash.** At 5+0 you cannot fill a large table anyway, and cache locality beats capacity.
- Accept that an engine tuned hard for blitz scores relatively worse on CCRL 40/15 than on CCRL Blitz. That is the intended trade, not a bug.

---

## Verification ladder

In increasing order of what each actually proves:

1. **Perft** — bit-exact vs published counts. Pure correctness, no judgement involved.
2. **`bench`** — deterministic across builds and `-march` levels.
3. **SPRT vs previous version** — the only meaningful measure of a patch.
4. **Gauntlet vs known-rated engines** — the only thing that substantiates an Elo *number*. Several hundred games each against anchors spanning the target, then place via a rating calculator.

Tactical suites (WAC, ERET, STS) are for smoke-testing regressions only. They correlate poorly with strength above ~2800 — never quote them as a strength claim.
