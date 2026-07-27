# Rogatia roadmap

Target: **3200+ CCRL**, blitz-optimized (5+0 and faster). Solo development, serious-hobby pace.

## Calibrating the target

From the CCRL lists (July 2026): 3200 sits at rank ~185 of 667 on CCRL Blitz, among Rybka 4.1 and Critter 1.6a. It is the ceiling of an excellent hand-crafted evaluation, or the *floor* of a competent NNUE engine. A modern search with a mediocre self-trained net reaches 3300–3450; top-20 open source is 3620–3790.

So **3200 is a milestone passed en route, not the finish line.** This plan is built to reach ~3400 and treats 3200 as the Phase 6b gate.

## Locked decisions

| Decision | Choice | Rationale |
|---|---|---|
| License | GPL-3.0 | Lets us read and adapt from every top engine; universal in the field |
| Language | C++20 | Matches the modern engine idiom |
| Toolchain | clang + LTO + PGO | Best codegen for bitboard and SIMD work |
| Evaluation path | **PSQT → NNUE, no full HCE** | A tuned hand-crafted eval is 3–6 months that NNUE then discards |
| NNUE data | **Own self-play only** | Datagen is needed regardless; gives permanent clean provenance |
| Trainer | bullet (MIT, CUDA) | De-facto standard for non-Stockfish engines |
| Test harness | fastchess → OpenBench | fastchess replaced cutechess-cli |
| Move generation | Pseudo-legal + legality check | Legal movegen is a few % faster and much harder to get right |
| Sliding attacks | Black magic, PEXT behind `#ifdef` | Zen 4 PEXT is fast but only ~2–5% NPS; portable path first so the two can be perft-diffed |
| State | Make/unmake with undo stack | Copy-make gets expensive once accumulators and five Zobrist keys exist |

---

## Phases

### Phase 0 — Environment ✅
Toolchain, OpenBench-compatible Makefile, repo skeleton.

### Phase 1 — Board and move generation
Bitboards, black magic sliders, five fixed-seed Zobrist key sets, make/unmake with undo stack, staged pseudo-legal movegen with legality filter, perft with bulk counting.

**Gate — hard, non-negotiable:** perft bit-exact to depth 6 on the full standard suite. No search code before this passes.

### Phase 2 — Search core and UCI
Negamax, fail-soft alpha-beta, PVS, iterative deepening, aspiration windows, quiescence with stand-pat and SEE pruning, bucketed transposition table, staged move ordering, killers, PeSTO PSQT.

Full UCI plus a deterministic `bench`.

**Gate:** ~1800–2200 Elo, clean games in a GUI, reproducible bench.

### Phase 3 — Testing infrastructure ⚠️ before Phase 4
fastchess SPRT at 8+0.08, `-concurrency 8`, pentanomial, OpenBench books. Expose every search constant as a UCI option in dev builds so SPSA can drive them later — the cheapest decision on this list.

This comes *before* the features it validates. Published Elo figures are order- and engine-dependent; only your own SPRT numbers mean anything.

### Phase 4 — Modern search build-out

**Pre-loop pruning:** TT cutoff → internal iterative reduction → reverse futility pruning (quadratic margin, relaxed when improving) → razoring → null move pruning (zugzwang guard, verification search at high depth) → ProbCut → multicut.

**In-loop pruning:** late move pruning (history mixed into the move count) → futility pruning (keyed on *reduced* depth) → SEE pruning → history pruning.

**LMR** — the largest single gain (~100+ Elo). Base table `[isNoisy][depth][moveCount]`, then adjusted by non-PV, TT-PV, improving, in-check, **cut node (largest adjustment, ~2×)**, alpha-raised, TT-move-is-noisy, and history. The adjustment set is worth as much as the base formula.

**Extensions:** singular extension (highest value by far), double/triple/negative variants, check extensions, do-deeper/do-shallower.

**History stack** — butterfly `[stm][from][to]`, capture history, continuation history at 1/2/3/4/6-ply. Gravity updates; apply **malus** to tried-and-failed quiets, not just bonus to the cutoff move. Age rather than clear.

**Correction history** — record `searchScore − staticEval` keyed on board features. Use the correction's *magnitude* as an uncertainty signal to widen RFP margins and shrink LMR.

**Gate: ~2400–2600 Elo with PSQT only.**

Calibrate carefully: ~3000–3100 is the number for a *tuned full HCE*, which this plan skips. With PSQT only, a fully modern search tops out around 2400–2600 and that is success. The missing 400 Elo is not in the search — it is in Phase 6.

### Phase 5 — Datagen
~300 lines as an engine subcommand. No generic tool exists; every engine writes its own. 8 random opening plies, 5000-node soft limit, quiet-position filter, eval and Syzygy adjudication, viriformat output.

### Phase 6a — First NNUE
`(768 → 256)x2 → 1` on ~100M positions. AVX2 inference with incremental accumulator updates. Build the `-march` bench-determinism check here, the moment inference first exists.

**Gate: ~2800–2900** (+300–400 over the PSQT gate). Worth more than all of Phase 4's tuning.

### Phase 6b — Scale the net
~1B positions of self-play, retrain at `(768 → 1024)x2 → 1` with 8 output buckets.

**Gate: 3200+.**

### Phase 7 — Iterate to ~3400
King input buckets + Finny tables → L2/L3 → threat inputs → AVX-512/VNNI. Each cycle is worth +50–130 Elo and decaying; expect 4–8 of them.

Blitz-specific work lives here: node-based time management, best-move stability, score-trend scaling, move overhead.

---

## Risks

- **Movegen bugs found after Phase 2** are the classic project-killer. Phase 1's perft gate is the entire mitigation. Never soften it.
- **Untested patches accumulating** — Phase 3 before Phase 4 exists to prevent exactly this.
- **Determinism drift** silently breaks OpenBench eligibility. Fixed Zobrist seeds from day one; `-march` bench equality checked from Phase 6a.
- **Testing throughput above ~3300** — patches worth <3 Elo need 50k–150k games each. Arrange OpenBench access early rather than hitting the wall at month 12.

## Timeline

~12–24 months of serious part-time work to 3200, NNUE arriving around month 6–9.

Calibration from comparable projects: Blunder reached ~2900 with a hand-crafted eval in ~2 years; Stormphrax went 3400→3560 over ~2 years of NNUE iteration; Alexandria's late-stage releases are worth ~+9 Elo each.

At a casual pace the machine does the heavy lifting — datagen and SPRT run 24/7 whether anyone is at the keyboard or not.
