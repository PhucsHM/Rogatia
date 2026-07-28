# Rogatia roadmap

Target: **3500+ CCRL Blitz**, blitz-optimized (5+0 and faster). Solo development,
serious-hobby pace.

**Raised from 3200 to 3500 on 2026-07-28, deliberately.** The engine is a blitz
engine and is not intended to be competitive at classical, so it is measured on
CCRL Blitz and tuned at the time control it plays. Two consequences follow and
are not up for rediscussion: Phase 9 (king buckets, L2/L3, threat inputs) is
required rather than optional, and OpenBench becomes a requirement, because
above ~3300 a patch worth <3 Elo needs 50k-150k games and one box cannot supply
them.

## Calibrating the target

From the CCRL lists (July 2026): 3200 sits at rank ~185 of 667 on CCRL Blitz, among Rybka 4.1 and Critter 1.6a. It is the ceiling of an excellent hand-crafted evaluation, or the *floor* of a competent NNUE engine. A modern search with a mediocre self-trained net reaches 3300–3450; top-20 open source is 3620–3790.

So **3200 is a milestone passed en route, not the finish line.** This plan is built to reach ~3400 and treats 3200 as the Phase 8 gate; the 3500 target puts the finish line past the end of the phases written here, in continued Phase 9 iteration.

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

**Deferred to Phase 4:** exposing every search constant as a UCI option so SPSA can drive it later. It is a `src/` change and Phase 3 deliberately added no engine code — do it with the first pruning patch, not after fifty of them. **Done** in `src/tunable.h`, 24 parameters.

### Phase 4 — Core pruning ✅ → **2799 +/- 42**
The highest-value subset of modern search, and the minimum needed to be a decent teacher for the first network. All six landed, one commit each, on top of a bench-identical groundwork commit that moved the root to `stack[ROOT_OFFSET]` and threaded `cutNode` through `search()`.

- **Null move pruning** — `R = 3 + depth/3 + min((eval-beta)/200, 3)`, non-pawn-material guard. *No verification search*: the guard alone was enough to leave endgame play sane, and the extra re-search is Phase 7 if a zugzwang loss ever shows up.
- **Late move reductions** — `base + ln(depth)·ln(moveCount)/divisor`, separate bases for quiet and noisy, adjusted by cut node (2 plies, the largest term), PV, improving, in-check and history. Carried in 1/1024 plies; the `ln` table is **hardcoded integers, not `std::log`**, because bench has to be identical across libm implementations.
- **Reverse futility pruning** — *linear* margin (75 per ply), not quadratic, relaxed by one ply when improving. Linear is what the literature actually converged on.
- **Late move pruning** — `3 + depth²/(2 - improving)`
- **SEE pruning** in the main search — `-80·depth` quiet, `-30·depth` noisy
- **Continuation history** at 1- and 2-ply offsets, `[prev piece][prev to][piece][to]`

**SPRT passed** vs `base-phase3`, 2026-07-27, home box, 8+0.08, `8moves_v3.epd`, concurrency 6: **145-0-7 in 152 games, +651 ± 156 Elo, LLR 2.96, H1 accepted** at bounds [0.00, 10.00]. Fixed-time depth at 5s went from 9–15 plies to 19–34.

Bench depth was raised from 8 to 12 at the same time — the pruned tree made depth 8 a 0.1s fingerprint. Counts before commit `655f93c` are not comparable with counts after it.

**Gate passed: 2799 +/- 42 CCRL Blitz**, 720-game gauntlet, home box, 2026-07-27 — ~300 above the gate. Only Blunder (2664) still yields a usable anchor; Toad and Goldfish are saturated at 99.4% and 96.0%. Treat the number with suspicion until the anchor set is replaced with engines in the 2700–3000 band, which has to happen before Phase 6 or that phase has nothing to measure against. Table and caveats in `CLAUDE.md`.

Also fixed here: the corrupt PV lines carried over from Phase 3. Zero `Illegal PV move` warnings across all 720 games.

### Phase 5 — Datagen ✅
`rogatia datagen <out> <positions> [seed] [nodes]` in `src/datagen.cpp`, with `scripts/datagen.sh` launching one process per thread. 8 random opening plies (no book), openings over 1000cp discarded, 5000-node soft limit per move, the quiet filter (drop in-check, drop where `|static eval − qsearch eval| > 60cp`), eval adjudication at 2000cp held four plies, and Syzygy hard adjudication.

**Measured: 6,118 positions/sec on 16 workers ≈ 528M/day.** Inside the 4k–10k estimate, so the 100M positions Phase 6 needs is ~4.5 hours, not a day.

**Deviation — bulletformat, not viriformat.** 32 B/position is 3.2 GB for 100M against 1.7 TB free, and it is the struct `bullet` consumes with no conversion step. Viriformat's ~8x compression buys storage that is not scarce. Layout was verified against `bulletformat/src/chess.rs`, not reconstructed: the record is stored **already flipped to the side to move**, which is why it carries no side-to-move field.

**Syzygy via Fathom** (MIT), vendored verbatim in `src/fathom/`. 3-4-5 set, 290 files / 939 MB, at `$SYZYGY_PATH` (default `~/syzygy/3-4-5`). Hard adjudication cut the draw share from 33.7% to 27.3% and ended games sooner — 279 games per 20k positions against 241 without it — because dead endings stop being ground out to a 50-move draw.

**Validation**, four checks, because bad data fails silently and expensively:
1. Every record decodes back to a legal position (two kings, king squares consistent, no pawns on rank 1/8).
2. Score correlates with result as a clean sigmoid (−800cp → 2%, +800cp → 97%).
3. Material balance **in the stored frame** tracks the stored score (−3 → −680cp, +3 → +623cp, 96.7% sign agreement). Checks 1 and 2 both pass even if the board and the labels ended up in different frames; this is the one that does not.
4. `bullet-utils validate` — the real consumer — reports *"No invalid positions!"* with a WDL split matching our own decoder.

From here the CPU generates data 24/7 in the background while search development continues. These are not sequential.

### Phase 6 — First NNUE (~1 week) → **~2800**
`(768 → 256)x2 → 1` on ~100M positions. AVX2 inference with incremental accumulator updates. SCReLU, QA=255, QB=64, eval scale ~400.

**Toolchain is already installed and proven end-to-end** (2026-07-27, home box):

| Piece | Where | Note |
|---|---|---|
| Rust | `~/.cargo`, 1.97.1 | rustup, no root |
| bullet | `~/bullet` | build with `CUDA_PATH=/opt/cuda cargo b -r --package bullet_lib --features cuda --example simple` |
| CUDA | `/opt/cuda`, 13.3.73 | `LD_LIBRARY_PATH=/opt/cuda/lib64` to run |
| `bullet-utils` | `~/bullet/target/release/` | needs no CUDA; `validate` is the data gate |

A smoke test on real datagen output reported **`Training on NVIDIA GeForce RTX 3090 (sm_86)`** at **~9.1M positions/sec**, so a 100M-position superbatch is ~11 seconds and the whole 40-superbatch schedule is under ten minutes. **Training is not the bottleneck; datagen is** — which is why datagen runs 24/7 and this phase is cheap to repeat.

`examples/simple.rs` is already this exact architecture (`Chess768` inputs, `(768 → HIDDEN)x2 → 1`, `DirectSequentialDataLoader`), so Phase 6 starts by editing it rather than writing a trainer. Our config is `trainer/rogatia.rs`; copy it over `~/bullet/examples/simple.rs` to run it.

**Quantised net layout** (`quantised.bin`, 394,816 bytes at HIDDEN=256), which the engine loader has to match exactly:

| Section | Byte offset | Count | Quantisation |
|---|---|---|---|
| `l0w` | 0 | 196,608 `i16` — **column-major 256×768**, so a feature's 256 weights are contiguous | QA |
| `l0b` | 393,216 | 256 `i16` | QA |
| `l1w` | 393,728 | 512 `i16` | QB |
| `l1b` | 394,752 | 1 `i16` | QA·QB |
| padding | 394,754 | 62 bytes, the ASCII string `bullet` repeated to a 64-byte boundary | — |

Inference: `screlu(x) = clamp(x, 0, QA)²`; `out = Σ screlu(acc_us[j])·l1w[j] + Σ screlu(acc_them[j])·l1w[256+j]`, then `/= QA`, `+= l1b`, `*= SCALE`, `/= QA·QB`.

**Scaffold net trained 2026-07-27** on the first 10.2M datagen positions, 20 superbatches in 23 seconds, final loss 0.0517. It is far too weak to gate on — it exists so the C++ inference can be written and tested against a real file while datagen is still running. Retrain on the full set and swap the file.

Build the **`-march` bench-determinism check here**, the moment inference first exists. NNUE accumulation order can differ between SIMD widths and silently break OpenBench eligibility. Trivial to catch with one code path; painful with three.

**Gate: ~2800.** +300–400 Elo in a single step — more than all of Phase 4.

This is deliberately early. The net is scaffolding: it exists to make the *next* net possible, and the first one being trained on somewhat weak labels costs little because it gets retrained 4–8 times regardless.

### Phase 7 — Full search build-out (2–3 months) → **~3000–3100**

**In progress.** Merged: singular extensions (+39.04 +/- 12.69), correction
history (+33.13 +/- 11.60). Under test or queued: Syzygy probing in search, the
repetition ply distinction, the fifty-move eval taper.

**The phase is being driven by measurement rather than by this list.** The
720-game gauntlet showed 19% of all games were positions the engine evaluated as
winning and then drew, from three separate causes -- see "Phase 7 conversion
work" in `CHANGELOG.md`. Two items on this list changed status as a result:
**SPSA is dropped** on this hardware (a 1,440-game test of the single
best-attested tuning gain returned -9.41 +/- 12.37, and the published constants
need tens of thousands of games to move a parameter), and **SMP is
deprioritised**.

**The SMP reasoning is worth writing down**, because it was briefly recorded
here the other way round -- as "the largest remaining item", on the grounds that
it buys testing throughput as well as strength. Both halves are wrong.

**CCRL Blitz is a single-CPU list** -- i7-4770k, 2'+1", with a separate 4CPU
list -- so multithreading contributes nothing to the stated target. And it does
not speed up testing either: an SPRT runs `option.Threads=1` at concurrency 8,
eight single-threaded games at once, and making the engine multithreaded changes
none of that. Testing at Threads=2 and concurrency 4 would use the same cores
for *fewer* games per hour.

SMP is still worth building -- tournaments, the 4CPU list, ordinary use, and
the stated intent that this become a complete engine rather than a blitz
specialist -- but it is not on the path to 3500 and must not displace work that
is.  **Build it deliberately, in daylight, not as the tail of an overnight
batch.**  A threading bug is the one class of defect none of this project's
gates can catch: perft is single-threaded, bench is single-threaded, and an
SPRT runs `Threads=1`.  A race would reach a tournament unmeasured.

### SMP design, when it is built

Lazy SMP.  N threads all search the root independently and share the
transposition table; they diverge naturally through timing and the table
carries what one finds to the others.  Concretely, in this engine:

1. **`Worker` becomes per-thread.**  It is one global today, and the comment on
   it already says so.  History, continuation history, correction history, the
   PV array and the stack all move with it.  The transposition table stays
   shared -- that is the whole mechanism.
2. **The TT needs no locks, and this engine is already safe for it.**  Races
   produce torn entries, and a torn entry yields a garbage move.  Every TT move
   already goes through `is_pseudo_legal` before it is played, for the key16
   collision case, and that same check makes a torn entry harmless.  Confirm
   the reasoning still holds before relying on it.
3. **Only the main thread reports.**  `print_info`, `bestmove` and the time
   checks belong to thread 0; helpers search and are stopped by the same
   `Stopped` flag.
4. **`Threads` stops being accepted-and-ignored.**  Its maximum rises from 1,
   and OpenBench needs it to mean what it says.
5. **Determinism is lost above one thread, and that is expected.**  `bench`
   must keep running single-threaded so the fingerprint survives; verify
   `Threads=1` reproduces the current node count exactly before trusting
   anything above it.

The honest gate: SMP is worth roughly +60-80 Elo at 4 threads on hardware that
allows it, and 0 on the lists this project is measured against.

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
