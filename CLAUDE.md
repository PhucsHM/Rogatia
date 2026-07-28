# Rogatia — engine development guide

UCI chess engine. C++20, GPL-3.0. Target: **3200+ CCRL**, tuned for **blitz (5+0 and faster)**.

Read this before touching anything. The full phased roadmap with Elo gates is in `docs/ROADMAP.md`, and **`CHANGELOG.md` is the handoff protocol between the two machines** — what state the engine is in, what the other box is probably doing, and how to get a working build (the net is not in git).

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

**Phases 1–6 complete. Phase 7 (full search build-out) is next.** See `docs/ROADMAP.md` for the full arc.

Working now: bitboards, black magic attacks, five Zobrist key sets, make/unmake, movegen (perft 37/37, 626,461,214 nodes bit-exact), fail-soft PVS with iterative deepening and aspiration windows, quiescence with SEE and delta pruning, bucketed TT, killers, butterfly history, continuation history, null move, LMR, RFP, LMP, SEE pruning, futility pruning, razoring, history pruning of quiets, internal iterative reduction, singular extensions, **NNUE evaluation** (`(768 → 256)x2 → 1`, SCReLU, incremental accumulator), tapered PeSTO PSQT as the no-net fallback, full UCI, 33 search constants exposed as UCI spin options (`src/tunable.h`), deterministic bench (**4,994,552** with a net, **6,991,803** without, at depth 12 — these move with every search change, so trust the CHANGELOG state table over any number quoted in prose).

Build with a net: `make EVALFILE=/abs/path/to/net.nnue`. Nets are gitignored; the Phase 6 net is `nets/rogatia-p6.nnue`, trained on 112,000,683 self-play positions. `make run-nnue EVALFILE=...` is the accumulator gate — treat it as perft for the evaluation.

**Measured: ~3175 CCRL Blitz** (gauntlet arithmetic returns 3195 +/- 24; see `CHANGELOG.md`, "What the rating actually says" — three caveats, all pushing down). 720 games at 8+0.08, `8moves_v3.epd`, Hash=16, Threads=1, concurrency 6, home box, 2026-07-28, against a new anchor set that brackets the engine instead of sitting under it.

| Opponent | CCRL Blitz | Games | W-L-D | Score | Implied Rogatia |
|---|---|---|---|---|---|
| Zahak 7.1 | 2972 +/- 18 | 240 | 175-14-51 | 83.5% | 3254 +/- 46 |
| Zahak 8.0 | 3160 +/- 16 | 240 | 84-70-86 | 52.9% | 3180 +/- 39 |
| Zahak 9.0 | 3292 +/- 12 | 240 | 37-120-83 | 32.7% | 3167 +/- 39 |

Inverse-variance weighted: **3195 +/- 24**. The three anchors disagree by only 87 points, against 189 for the old set, and the two nearest an even score — the ones the Elo model handles best — agree to within 13. The 83.5% row reads high, which is the usual compression at a wide gap. **This is the first measurement since Phase 4 that is worth quoting**, because every anchor returned a usable number rather than saturating.

**Caveat: the three anchors are three versions of one engine**, so they share a playing style and are not fully independent the way three different engines would be. Zahak was chosen because it publishes a Linux binary for every version and its versions happen to span the band; Weiss and Simbelmyne have no usable Linux x86-64 assets, and Viridithas jumps 3244 -> 3423 with nothing between. Add a second family before treating 3195 as settled.

The previous anchor set (Toad 1776, Goldfish 2252, Blunder 8.5.5 2664) is retired: the engine scored 99.4%, 96.0% and 95.4% against them. Ratings read 2026-07-28 from `https://computerchess.org.uk/404/rating_list_all.html`.

Reproduce: `CONCURRENCY=6 scripts/gauntlet.sh 240 ./rogatia`. Full protocol in `docs/TESTING.md`.

Next concrete task: **Phase 7, the full search build-out** — extensions, singular search, better time management, SMP. The evaluation is no longer the weak link; the search is.

**Superseded — the anchor set was exhausted, and has been replaced.** For the record: on 2026-07-28 the engine scored **219-1-20 (95.4%) against Blunder 8.5.5**, the last of the original anchors that still worked, which is the same saturated regime that had already made Toad and Goldfish useless. A naive conversion read ~3190 and was not quoted, because at a 95% score the Elo model is too compressed to trust. The Zahak bracket above replaced the set the same day and put the engine at 3195 +/- 24 — which happens to land on the same figure, but this time from three unsaturated anchors that agree, rather than one that had run out of resolution.

**Partly fixed in Phase 4: the corrupt PV lines.** The `Illegal PV move` class is
genuinely gone — 720 gauntlet games and a 2,308-game SPRT both produced zero.
A second class survives: **`PV continues after checkmate`, ~0.4 per game**, seen
from both engines in the 2026-07-28 SPRT, so it predates Phase 4's fix rather
than being caused by it. Harmless to results — fastchess warns and plays the
`bestmove` — but it means PV construction still has a gap. Not yet diagnosed.

### Phase 6 2026-07-28 — first NNUE, SPRT +360 +/- 70

170 games at 8+0.08 against the same tree built without a net: 141-9-20, 88.8%,
LLR 2.95, H1 accepted on `[0.00, 10.00]`. Bench 5,001,521 → **4,063,328**.

Trained at **wdl=0.3**, not bullet's example default of 0.75. At 0.75 the target
is mostly game outcomes, which saturates the net — the first scaffold net's evals
came out about twice the scale of the search scores, and every pruning margin in
`tunable.h` is calibrated to the old scale, so an inflated eval silently makes
all of them more aggressive. At 0.3 the slope against held-out labels is 1.075,
correlation 0.958. **If the eval scale ever changes again, re-check the pruning
margins before trusting an SPRT.**

### Merged 2026-07-28 — second pruning set, SPRT +14.61 +/- 10.08

2,308 games at 8+0.08, LOS 99.78%, LLR 2.97 on `[0.00, 10.00]`, zero illegal
moves and zero time losses. Bench 5,356,740 → **5,001,521**.

Landed: futility pruning at the child node, razoring verified by quiescence,
history pruning of quiets, internal iterative reduction, LMR reducing on the
full history statistic, two datagen label fixes, en passant hashed only when a
pawn can capture, repetition bounded by plies-from-null, slider blockers for
the side to move only, history tables narrowed to `int16`.

Tested as one patch, deliberately. These techniques overlap — futility, LMP,
history pruning and razoring all prune quiets at low depth — so per-patch
numbers against a base missing the others would not have summed to this anyway.

**Not merged:** the ttPv reduction exemption (branch `phase4-fixes`). It cost
17% more nodes alone and the flag is sticky, so it accumulates through the table
and weakens LMR everywhere. Worth its own test, not a bundle seat.

**Tried and rejected 2026-07-28: deferring generation behind the TT move.**
Search the TT move first and only run `generate<ALL>` plus its scoring pass if
it fails to cut. Measured on an idle laptop, medians of 9:

| | nodes | nps | time to depth 12 |
|---|---|---|---|
| main | 5,001,521 | 2,928,290 | 1,708 ms |
| deferred | 5,945,862 | 2,946,413 | 2,018 ms |

The premise fails. The nps gain is +0.6%, inside the 5–7% noise floor, while the
tree grows 18.9% — **18% slower to the same depth**. Cause is isolated and
certain: a scaffold running the identical two-stage loop with generation still
eager benched 5,001,521 *to the node*, so the restructuring is order-exact. The
whole regression is that scoring now happens after the TT move's subtree has
written the history tables `score_move` reads. A larger tree at equal depth is
worse ordering, so fresher statistics hurt here.

**Do not retry this narrow form.** It does not refute the published staged-picker
numbers (Viridithas +6.87, MadChess +39 at ~2210) — those are a full categorical
picker (TT → good captures → killers → quiets) that skips generating quiets
entirely on a cutoff, saving far more than one deferred scoring pass. That
remains open, carries the same ordering coupling, and needs the same
eager-generation scaffold as its correctness check.

Also worth knowing before the next datagen run: the node budget has a
**granularity and floor of 1024** (`check_stop` only tests the limit when
`(nodes & 1023) == 0`). `5000` really searches `5120`, and anything below 1024
is identical to 1024 — verified by three budgets producing byte-identical
output. Not a cost: it is a quality dial, so 5120 is more work *and* better
labels. Pick budgets on 1024 boundaries so the script says what it does.

**Datagen (Phase 5):** `scripts/datagen.sh [positions-per-worker] [workers] [nodes]`, one process per thread, output in **bulletformat** (32 B/position) under `data/`, which is gitignored. Syzygy 3-4-5 lives at `~/syzygy/3-4-5` (290 files, 939 MB) and is picked up via `$SYZYGY_PATH`. **An incomplete tablebase set is worse than none** — the engine builds `-DNDEBUG`, so Fathom's own asserts are gone and a truncated file reads as garbage; `datagen.sh` checks the file count for exactly this reason.

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
