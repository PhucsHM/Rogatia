# Rogatia roadmap

Target: **3200+ CCRL**, blitz-optimized (5+0 and faster). Solo development, serious-hobby pace.

## Calibrating the target

From the CCRL lists (July 2026): 3200 sits at rank ~185 of 667 on CCRL Blitz, among Rybka 4.1 and Critter 1.6a. It is the ceiling of an excellent hand-crafted evaluation, or the *floor* of a competent NNUE engine. A modern search with a mediocre self-trained net reaches 3300–3450; top-20 open source is 3620–3790.

So **3200 is a milestone passed en route, not the finish line.** This plan is built to reach ~3400 and treats 3200 as the Phase 8 gate.

## The two engines of improvement

| | What it is | Worth |
|---|---|---|
| **Search work** | Writing C++ — pruning, reductions, extensions, move ordering | ~2000 → 3100 |
| **Network training** | Compute — self-play data, GPU training | ~+400, then compounding |

**Search is the slow part; training is the fast part.** Self-play data generation runs at 4k–10k positions/sec, so 100M positions takes a day and 1B takes 1–2 weeks. The months in this plan are keyboard time, not compute time.

Critically: **the network learns to imitate the search.** It is trained on the search's own evaluations, not on game outcomes — this is supervised learning, not reinforcement learning. A weak search teaches a weak net, and no amount of self-play compensates. That is why search quality gates everything.

## Locked decisions

| Decision | Choice | Rationale |
|---|---|---|
| License | GPL-3.0 | Lets us read and adapt from every top engine; universal in the field |
| Language | C++20 | Matches the modern engine idiom |
| Toolchain | clang + LTO + PGO | Best codegen for bitboard and SIMD work |
| Evaluation path | **PSQT → NNUE, no full HCE** | A tuned hand-crafted eval is 3–6 months that NNUE then discards |
| NNUE data | **Own self-play only** | Datagen is needed regardless; gives permanent clean provenance |
| Search paradigm | **Alpha-beta, not MCTS** | See "Why not Leela-style" below |
| Trainer | bullet (MIT, CUDA) | De-facto standard for non-Stockfish engines |
| Test harness | fastchess → OpenBench | fastchess replaced cutechess-cli |
| Move generation | Pseudo-legal + legality check | Legal movegen is a few % faster and much harder to get right |
| Sliding attacks | Black magic, PEXT behind `#ifdef` | Zen 4 PEXT is fast but only ~2–5% NPS; portable path first so the two can be perft-diffed |
| State | Make/unmake with undo stack | Copy-make gets expensive once accumulators and five Zobrist keys exist |

---

## Why not Leela-style (settled — do not re-litigate)

Leela and Stockfish differ on three independent axes: **search** (MCTS vs alpha-beta), **network** (large GPU net with policy+value heads vs small CPU eval net), and **training data** (own self-play vs mixed).

Rogatia already takes Leela's best idea. **NNUE *is* the hybrid** — neural evaluation inside alpha-beta search is precisely what Stockfish adopted from the AlphaZero line in 2020, and it beat both pure approaches. Self-play-only training is already this project's policy.

The remaining Leela-specific piece is MCTS with a policy network. Rejected for three concrete reasons:

1. **Blitz.** MCTS needs GPU batching, which means per-move latency. At 5+0 alpha-beta on CPU is strictly better suited. This is the stated goal of the project.
2. **Hardware.** Leela's zero-knowledge run consumed thousands of volunteer GPUs over months. One 3090 cannot replicate it.
3. **Rating lists.** CCRL — where the 3200 target lives — tests on CPU. A GPU engine competes in a different category.

Note that "training from zero knowledge" does **not** require Leela's compute, because alpha-beta search is a competent teacher from day one (Leela had to learn from win/loss alone with a random net, which is why it needed a farm). Stormphrax and Viridithas both train from random weights on self-generated data alone, and both are 3600+.

Worth revisiting only as a post-3400 experiment: **a policy network for move ordering** inside alpha-beta. Not replacing the search — just predicting which moves to try first. Active frontier, not settled practice.

---

## Phases

### Phase 0 — Environment ✅
Toolchain, OpenBench-compatible Makefile, repo skeleton.

### Phase 1 — Board and move generation ✅
Bitboards, black magic sliders, five fixed-seed Zobrist key sets, make/unmake with undo stack, staged pseudo-legal movegen with legality filter, perft with bulk counting.

**Gate passed:** perft bit-exact, 37/37 checks, 626,461,214 nodes. Magic and PEXT indexers produce identical counts.

### Phase 2 — Search core and UCI ✅
Fail-soft PVS, iterative deepening, aspiration windows, quiescence with SEE and delta pruning, bucketed TT with depth-preferred aging replacement, MVV-LVA/SEE ordering, killers, butterfly history, mate distance pruning, soft/hard time management, tapered PeSTO PSQT.

**Gate passed:** builds clean, bench deterministic (54,095,910) across `x86-64`, `v2`, `v3`, `native`, and PEXT builds. Plays legal games via UCI.

### Phase 3 — Testing infrastructure ✅
fastchess SPRT at 8+0.08, `-concurrency 8`, pentanomial, OpenBench books. `scripts/setup-testing.sh` reproduces the whole harness on a fresh machine; `scripts/sprt.sh` and `scripts/gauntlet.sh` drive it; `docs/TESTING.md` documents it.

This comes *before* the features it validates. Published Elo figures are order- and engine-dependent; only your own SPRT numbers mean anything. Zero Elo gained here, and skipping it is how engine projects die with a stack of patches that each "obviously" helped and collectively lost Elo.

**Gate passed:** harness self-test on identical binaries reports no difference; 720-game gauntlet against three CCRL-rated anchors produced the first real measurement.

**Measured: ~2197 +/- 29 CCRL Blitz** (Toad 1.0.0 85.8%, Goldfish 2.1.1 44.6%, Blunder 8.5.5 9.8%, 240 games each). The estimate that stood here before was 2000–2400; the measurement lands inside it. Table and caveats in `CLAUDE.md`.

**Deferred to Phase 4:** exposing every search constant as a UCI option so SPSA can drive it later. It is a `src/` change and Phase 3 deliberately added no engine code — do it with the first pruning patch, not after fifty of them.

### Phase 4 — Core pruning (~3–4 weeks) → **~2400–2500**
The highest-value subset of modern search, and the minimum needed to be a decent teacher for the first network:

- **Null move pruning** — `R = 3 + depth/3 + min((eval-beta)/margin, cap)`, zugzwang guard, verification search at high depth
- **Late move reductions** — base table `[isNoisy][depth][moveCount]` ≈ `base + ln(depth)·ln(moveCount)/divisor`, adjusted by non-PV, improving, in-check, **cut node (largest adjustment, ~2×)**, and history. Worth ~100 Elo on its own; budget a week to get right
- **Reverse futility pruning** — quadratic margin in depth, relaxed when improving
- **Late move pruning** — `base + depth²/(2 - improving)`
- **SEE pruning** in the main search
- **Continuation history** at 1- and 2-ply offsets

**Gate: ~2400–2500, SPRT-verified against the Phase 3 baseline.**

### Phase 5 — Datagen (~1 week to write, then runs forever)
~300 lines as an engine subcommand. No generic tool exists; every engine writes its own. 8 random opening plies (no book — deliberate diversity), 5000-node soft limit per move, quiet-position filter (drop in-check, and drop where `|static eval − qsearch eval| > ~60cp`), eval and Syzygy adjudication, viriformat output.

~4k–10k positions/sec on 16 threads ≈ 350M–850M/day. **Measure it; don't trust the estimate.**

From here the CPU generates data 24/7 in the background while search development continues. These are not sequential.

### Phase 6 — First NNUE (~1 week) → **~2800**
`(768 → 256)x2 → 1` on ~100M positions (~1 day of datagen, hours of training on the 3090). AVX2 inference with incremental accumulator updates. SCReLU, QA=255, QB=64, eval scale ~400.

Build the **`-march` bench-determinism check here**, the moment inference first exists. NNUE accumulation order can differ between SIMD widths and silently break OpenBench eligibility. Trivial to catch with one code path; painful with three.

**Gate: ~2800.** +300–400 Elo in a single step — more than all of Phase 4.

This is deliberately early. The net is scaffolding: it exists to make the *next* net possible, and the first one being trained on somewhat weak labels costs little because it gets retrained 4–8 times regardless.

### Phase 7 — Full search build-out (2–3 months) → **~3000–3100**
The rest of the modern search, now on top of an engine that is already ~2800:

**Pre-loop:** internal iterative reduction → razoring → ProbCut → multicut (non-PV only).

**In-loop:** futility pruning (keyed on *reduced* depth) → history pruning.

**Extensions:** singular extension (highest value by far — re-search non-TT moves at `(depth-1)/2`, extend if all fail low), double/triple/negative variants, check extensions, do-deeper/do-shallower.

**Full history stack:** capture history, continuation history at 1/2/3/4/6-ply, side-to-move dimension on butterfly history, ageing rather than clearing.

**Correction history** — record `searchScore − staticEval` keyed on pawn / non-pawn / major / minor / continuation features. Use the correction's *magnitude* as an uncertainty signal ("corrplexity") to widen RFP margins and shrink LMR. The defining new idea of the 2023–2026 era.

### Phase 8 — Scale the net → **3200+**
~1B positions of self-play from the now much stronger engine, retrained at `(768 → 1024)x2 → 1` with 8 output buckets.

**Gate: 3200+ — the stated target.**

Reference point: Alexandria reached CCRL Blitz **top-6** on `(768→1536)x2→1x8` — no king buckets, no L2/L3. **Net size is not what gates 3200; search quality is.** Do not chase a bigger net to fix a search problem.

### Phase 9 — Iterate to ~3400 (ongoing)
King input buckets (4→8→16, horizontally mirrored) + **Finny tables** → L2/L3 layers → threat inputs → AVX-512/VNNI dispatch (Zen 4 is double-pumped 256-bit, so expect **5–20%**, not 2×).

Each cycle is days of datagen + hours of training + a few thousand SPRT games, worth +50–130 Elo and decaying. Expect 4–8 cycles.

Blitz-specific work lives here: node-based time management, best-move stability scaling, score-trend scaling, move overhead tuning.

---

## Risks

- **Movegen bugs found later** are the classic project-killer. Phase 1's perft gate is the entire mitigation, and it must stay green on every change touching `Position` or movegen.
- **Untested patches accumulating** — Phase 3 before Phase 4 exists to prevent exactly this.
- **Determinism drift** silently breaks OpenBench eligibility. Fixed Zobrist seeds from day one; `-march` bench equality checked from Phase 6.
- **Testing throughput above ~3300** — patches worth <3 Elo need 50k–150k games each. Arrange OpenBench access early rather than hitting the wall at month 12.
- **Motivation.** This is a 12–24 month solo project. The phase ordering deliberately front-loads the +400 Elo NNUE jump to month ~2 rather than month ~7, because a long stretch with nothing visible happening is how these projects actually die.

## Timeline

~12–24 months of serious part-time work to 3200. First network around **month 2**, not month 7.

Calibration from comparable projects: Blunder reached ~2900 with a hand-crafted eval in ~2 years; Stormphrax went 3400→3560 over ~2 years of NNUE iteration; Alexandria's late-stage releases are worth ~+9 Elo each.

The pace-setter is **test throughput, not ideas**: ~30k games/day on 8 cores ≈ one decisive SPRT per day. The machine running 24/7 matters more than hours at the keyboard.
