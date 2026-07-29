# Rogatia — engine development guide

UCI chess engine. C++20, GPL-3.0. Target: **3500+ CCRL Blitz**. Blitz is the tuning
time control and the near-term goal -- the engine is tuned at **5+0 and faster** and
measured on CCRL Blitz.

**Standard time controls are a later goal, not an excluded one** (set 2026-07-28).
That does not reorder the roadmap: the search architecture is the same at 5+0 and
40+40, and everything built so far is time-control neutral or better at long time
controls. What it does change is that blitz bias must stay *reversible* -- pruning
margins are the divergence risk, and each phase ends with one non-regression run at
20+0.2 rather than every patch being verified there. See "Blitz focus" below.

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
make release              # portable x86-64-v3 static build, black magic sliders
make release-pext         # the same with PEXT -- only for a known Haswell+/Zen 3+ CPU
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

**Phases 1–6 complete. Phase 7 (full search build-out) is in progress.** See `docs/ROADMAP.md` for the full arc.

Merged in Phase 7 so far: **singular extensions** (+39.04 +/- 12.69, 1,296 games),
**correction history** on the pawn and non-pawn keys (+33.13 +/- 11.60, 1,504 games),
**time management** (+28.34 +/- 10.32, 1,708 games) and **Syzygy probing**
(+24.07 +/- 9.41).

**Every SPRT before 2026-07-29 ran without tablebases.** No script passed
`SyzygyPath`, and the engine defaults it to `<empty>`, so Syzygy was inert in
every game after it merged. Numbers either side of that date are not directly
comparable: the earlier ones measured an engine about 24 Elo below its own
merged strength.

**The Phase 7 work is now aimed by measurement rather than by the roadmap's
feature list.** Replaying the 720-game Zahak gauntlet through `scripts/style.py`
and `scripts/draw-anatomy.py` found one dominant weakness: against Zahak 8.0, the
opponent it is evenly matched with, **46 of 86 draws were positions the engine
had itself evaluated at +1.00 or better — 19% of every game played.** Zahak threw
away far fewer from the same 86 games. Split by how the draw arrived:

| Draw ended as | We were +1.00 | We never were |
|---|---|---|
| Three-fold repetition | **22** | 20 |
| Fifty-move rule | **7** | 19 |
| Dead material | **17** | 1 |

Three different causes, three different fixes, and the fifty-move bucket is the
smallest rather than the largest — 19 of the 26 fifty-move draws came from
positions that were never winning, where a fifty-move draw is a good result.
This is what Phase 7 is now working through, in that order of size.

All three conversion patches have now returned. **Syzygy probing** merged
(+24.07 +/- 9.41). **The repetition ply distinction** is parked (~+2.34 +/- 7.43,
stalled at 3,260 games). **The fifty-move eval taper** was rejected
(-15.03 +/- 7.97) and is retuned on `phase7-rule50b`, threshold 20 -> 65.

**Phase 7 is a breadth pass.** The aim is to bring the search to its strongest
state by trying as many known techniques as the machine has time for, so each
idea is built on its own branch, tested briefly, and **parked rather than
abandoned** if it is overwhelmingly negative or cannot resolve. Parked patches
come back later, retuned from the saved snapshot plus what the engine's own PGNs
and the published numbers say. `scripts/testqueue.sh` runs the queue back to back
unattended — see "How Phase 7 runs" in `CHANGELOG.md` and `docs/TESTING.md`.

**Never delete a `phase7-*` branch.** A parked branch is a saved starting point,
not a dead end.

Working now: bitboards, black magic attacks, five Zobrist key sets, make/unmake, movegen (perft 37/37, 626,461,214 nodes bit-exact), fail-soft PVS with iterative deepening and aspiration windows, quiescence with SEE and delta pruning, bucketed TT, killers, butterfly history, continuation history, null move, LMR, RFP, LMP, SEE pruning, futility pruning, razoring, history pruning of quiets, internal iterative reduction, singular extensions, correction history, Syzygy probing, node-based time management, **NNUE evaluation** (`(768 → 256)x2 → 1`, SCReLU, incremental accumulator), tapered PeSTO PSQT as the no-net fallback, full UCI, 33 search constants exposed as UCI spin options (`src/tunable.h`), deterministic bench (**4,656,884** with a net, **6,951,633** without, at depth 12 — these move with every search change, so trust the CHANGELOG state table over any number quoted in prose).

Build with a net: `make EVALFILE=/abs/path/to/net.nnue`. Nets are gitignored; the current net is `nets/rogatia-p8a.nnue`, trained on 160M filtered self-play positions and SPRT'd at +184.38 +/- 28.01 over the Phase 6 net. `make run-nnue EVALFILE=...` is the accumulator gate — treat it as perft for the evaluation.

**Measured: 3379 +/- 20 CCRL Blitz.** 540 games, 2026-07-29, `8moves_v3.epd`,
Hash=16, Threads=1, split across both machines. Six anchors, four families.

**At `tc=120+1`, which is the control CCRL Blitz states for itself** ("equivalent
to 2'+1" on an Intel i7-4770K"). Every earlier figure in this file was measured
at 8+0.08 and is not comparable to this one.

| Opponent | CCRL Blitz | Games | W-L-D | Score | Implied Rogatia |
|---|---|---|---|---|---|
| Zahak 8.0 | 3160 +/- 16 | 80 | 52-2-26 | 81.25% | 3415 +/- 72 |
| Zahak 9.0 | 3292 +/- 12 | 80 | 22-12-46 | 56.25% | 3336 +/- 50 |
| Zahak 10.0 | 3334 +/- 8 | 80 | 21-8-51 | 58.13% | 3391 +/- 43 |
| Smallbrain 6.0 | 3361 +/- 15 | 100 | 29-11-60 | 59.00% | 3424 +/- 45 |
| Clover 3.1 | 3399 +/- 11 | 100 | 11-33-56 | 39.00% | 3321 +/- 45 |
| Alexandria 3.5 | 3405 +/- 13 | 100 | 26-25-49 | 50.50% | 3408 +/- 52 |

Inverse-variance weighted: **3379 +/- 20**. Dropping the 81% Zahak 8.0 row, where
the Elo model compresses, gives 3376 +/- 21 — the outlier is not carrying it.

This supersedes 3195, and the jump is explained: the p8a net SPRT'd at
**+184.38 +/- 28.01** over the p6 net. 3195 + 184 lands where this measurement
lands.

**Three caveats, and the third is the real one.**

1. **80-100 games per anchor is thin.** The 3195 figure used 240 each, which is
   why its bars read +/- 39-46 against +/- 43-72 here.
2. **The six implied values span 103 points**, 3321 (Clover) to 3424
   (Smallbrain). They agree on the band, not on the number.
3. **Every anchor is rated 3160-3405, so none of them sits above the engine.**
   This measurement can show the engine is *around* 3380; it cannot show what
   happens against stronger opposition, because it never faced any.

That third caveat was half-closed and then reopened by choice. Stormphrax 5.0.0
(3619) reached **20 games at 22.5%**, which converts to ~3404 and agrees with
the rest — 22.5% is nowhere near the <5% saturation floor, so it was a usable
anchor. It and Viridithas 15.0.0 (3681) were **cancelled on 2026-07-29** on the
judgement that both are too far above to evaluate usefully. Re-run them before
publishing this number anywhere; until then 3379 rests entirely on anchors the
engine is level with or beating.

**Reproduce:** `TC=120+1 scripts/gauntlet.sh 80 ./rogatia-p8a` on the home box
(Linux anchors, the default set) and `TC=120+1 ANCHORS="$ANCHORS_WINDOWS"
scripts/gauntlet.sh 100 ./rogatia-p8a` on the laptop.

Two anchor sets are retired, both by saturation. Toad 1776, Goldfish 2252 and
Blunder 8.5.5 2664: the engine scored 99.4%, 96.0% and 95.4%. Then Zahak 7.1
2972, which gave 83.5% at the 3195 measurement and would now be worse. Ratings
read from `https://computerchess.org.uk/404/rating_list_all.html` — fetch that
page with a browser User-Agent, the plain one gets 403.

Full protocol in `docs/TESTING.md`.

Next concrete task: **drain the search queue, retune the parked patches, then run
the end-of-phase gate at 20+0.2 before Phase 8.** SMP is
explicitly NOT next -- CCRL Blitz is a single-CPU list, so multithreading buys no
rating, and it does not speed up testing either (an SPRT runs `option.Threads=1`
at concurrency 8; making the engine multithreaded changes nothing). See
`docs/ROADMAP.md`.

**Settled 2026-07-28, do not relitigate.** "Is the search or the evaluation the weak
link?" — both get worked as opportunities appear. The gauntlet showed the engine reaching
**24.8 plies against Zahak 8.0's 15.5 for an even score**, which is a reason to expect
Phase 8's larger net to pay well, but nominal depth is not comparable across engines and
it gates nothing.

**Superseded — the anchor set was exhausted, and has been replaced.** For the record: on 2026-07-28 the engine scored **219-1-20 (95.4%) against Blunder 8.5.5**, the last of the original anchors that still worked, which is the same saturated regime that had already made Toad and Goldfish useless. A naive conversion read ~3190 and was not quoted, because at a 95% score the Elo model is too compressed to trust. The Zahak bracket above replaced the set the same day and put the engine at 3195 +/- 24 — which happens to land on the same figure, but this time from three unsaturated anchors that agree, rather than one that had run out of resolution.

**Partly fixed in Phase 4: the corrupt PV lines.** The `Illegal PV move` class is
genuinely gone — 720 gauntlet games and a 2,308-game SPRT both produced zero.
**The second class is now diagnosed and fixed** (`phase7-pvfix`, merged as
`337fe99`; measured at 0.27 per game before). At a PV node the line is assembled by copying
the child's: `pv[ply] = move`, then a memcpy of `pv[ply + 1]` for
`pvLen[ply + 1]` moves. That is only valid when the child actually ran as a PV
node — moveCount 1, or the full-window re-search, which is guarded by
`score > alpha && score < beta`. **When a null window fails high, no PV child
ever runs and `pvLen[ply + 1]` still holds the length an earlier, deeper search
left in that slot**, so the memcpy splices a line from a different position onto
a perfectly good move.

It needs no SPRT and bench proves it: `W.pv` is read only for reporting and for
`rootBestMove`, which still takes `pv[0][0]`, so the move chosen at every node is
identical and bench is unchanged at 4,772,409. It changes what the engine *says*,
never what it plays.

Worth knowing how it hid: **a cold fixed-depth search cannot show it.** `pvLen`
starts zeroed, so there is nothing stale to copy; the engine has to be warm, the
way a game leaves it. `scripts/pvcheck.py` replays real games through one process
and validates every reported PV against a board — 20 games, ~65,000 PVs, one
corrupt line before and none after.

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
- **In Phase 7 the bounds are per test, not per phase.** A first test of a new
  technique uses `[0.00, 5.00]` with a 4,000-game stall limit. A **retune** of a
  patch that already failed uses `[0.00, 3.00]` with a 12,000-game limit,
  because a retune's honest expectation is a few Elo and that is exactly what
  `[0, 5]` coin-flips. `scripts/testqueue.sh` derives the stall limit from
  `elo1`; the two cannot be set apart. See `docs/TESTING.md`.
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
2. **Phase 8** — `(768 → 1024)x2 → 1` with 8 output buckets on ~1B positions, generated by the much stronger post-Phase-7 engine. This is where 3200 happens -- a milestone on the way to 3500, no longer the destination.
3. **Phase 9** — king input buckets (4→8→16, horizontally mirrored) + Finny tables
4. **Phase 9** — L2/L3 layers, threat inputs, AVX-512/VNNI dispatch

Quantization: QA=255 (feature transformer, int16), QB=64 (output, int8), SCReLU, eval scale ~400.

Reference point: Alexandria reached CCRL Blitz **top-6** on `(768→1536)x2→1x8` — no king buckets, no L2/L3. **Net size is not what gates strength; search quality is.** Do not chase a bigger net to fix a search problem.

**At a 3500 target, Phase 9 stops being optional.** King input buckets, L2/L3 layers
and threat inputs are how the last few hundred Elo are found, and OpenBench moves
from a nice-to-have to a requirement -- above ~3300 a single patch needs 50k-150k
games and one box cannot supply them.

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
- An engine tuned hard for blitz scores relatively worse on CCRL 40/15 than on CCRL Blitz. **That trade is accepted for now but is no longer permanent** — see below.

### Standard time controls are a later goal (set 2026-07-28)

Blitz stays the tuning time control and the near-term target. Standard is wanted
eventually, which changes nothing about *what* gets built and two things about
*how it is checked*.

**Why so little changes:** the search architecture for 5+0 and 40+40 is the same,
as the top of this section says. Everything merged so far — singular extensions,
correction history, Syzygy probing, the fifty-move taper — is time-control
neutral in direction, and the first two are worth *more* at long time controls,
not less, because a deeper search compounds a better move ordering. Phase 8 is
the same story: a stronger evaluation pays more when the search goes deeper. So
the roadmap does not need reordering for this.

**What does change — and it is the real exposure:**

1. **Pruning margins are the STC/LTC divergence risk.** A margin tuned to be
   aggressive at 8 seconds can be actively wrong at 40 minutes, where the extra
   depth would have found what the pruning threw away. Search *features* usually
   keep their sign across time controls; pruning *thresholds* do not reliably.
   Treat any change to `tunable.h` margins as time-control-suspect, and say so
   in its commit.

2. **A phase-boundary LTC gate, not a per-patch one.** Verifying every patch at
   20+0.2 would cut testing throughput by roughly five, which is unaffordable
   against one machine. Instead, once per phase, run the accumulated stack
   against the previous phase tag as a **non-regression at `[-10.00, 0.00]`,
   tc=20+0.2**. That catches a phase's worth of accumulated blitz bias for the
   cost of one test rather than of every test. It has never been run; Phase 7 is
   the first one that should.

3. **Time management is the one genuinely TC-specific component**, and the only
   place where blitz and standard want different code rather than different
   numbers. `phase7-timeman` is written on blitz assumptions -- move overhead,
   a node-based scaler, aggressive stability shrinking -- and will need a second
   set of behaviour for long time controls rather than a retune.

4. **SMP moves from "off the path" to "eventually needed."** It still buys
   nothing on the single-CPU CCRL lists, blitz or 40/15, so it is still not next.
   But tournament play and the 4CPU lists are multi-core, and those only become
   reachable with standard time controls in scope. Build it after Phase 8, not
   instead of it.

5. **Hash sizing stops being a blitz-only assumption.** "Do not over-allocate
   hash" is true at 5+0 and false at 40/15, where a large table does get filled.
   The `Hash` option already spans 1--65536 MB, so nothing needs building; just
   do not bake the small-table assumption into anything.

---

## Verification ladder

In increasing order of what each actually proves:

1. **Perft** — bit-exact vs published counts. Pure correctness, no judgement involved.
2. **`bench`** — deterministic across builds and `-march` levels.
3. **SPRT vs previous version** — the only meaningful measure of a patch.
4. **Gauntlet vs known-rated engines** — the only thing that substantiates an Elo *number*. Several hundred games each against anchors spanning the target, then place via a rating calculator.

Tactical suites (WAC, ERET, STS) are for smoke-testing regressions only. They correlate poorly with strength above ~2800 — never quote them as a strength claim.
