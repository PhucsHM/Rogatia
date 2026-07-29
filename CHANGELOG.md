# Changelog and machine handoff

Two machines work on this engine, each with its own Claude Code session. They
share a git remote and nothing else. **This file is the handoff protocol** —
read it before starting work, update it when you finish a phase.

`CLAUDE.md` says what the engine *is*. This file says what state it is *in*,
what the other machine is probably doing, and what to do next from where you
are sitting.

**Reading this from outside the project?** It is a working log kept in reverse
chronological order, not a document — long, detailed, and written to be useful
six months later rather than to be read start to finish. Start with the state
table below, then jump to whichever dated entry interests you. For an overview
of the engine read [`README.md`](README.md); for the plan,
[`docs/ROADMAP.md`](docs/ROADMAP.md).

The entries that generalise beyond this project, if you want the interesting
bits: the game analysis that found a merged feature had been switched off in
every match since it landed; the three micro-optimisations measured on an idle
machine, two of which turned out to be wrong; and the two gates discovered to be
incapable of failing.

---

## State right now

| | |
|---|---|
| Phases complete | 1–6. **Phase 7 in progress.** Merged: singular extensions (+39.04 +/- 12.69), correction history (+33.13 +/- 11.60), **time management (+28.34 +/- 10.32)**, Syzygy probing (+24.07 +/- 9.41), the PV stale-tail fix (bench-neutral). Parked: repetition (~+2, stalled), rule50 taper (**-15.03, rejected**; its TT guard split off and queued separately). See "Phase 7 conversion work" below. |
| Target | **3500+ CCRL Blitz** (raised from 3200 on 2026-07-28). Blitz-only: measured on CCRL Blitz, tuned at the time control it plays, never verified at a slower one. |
| Strength | **3379 +/- 20 CCRL Blitz**, 2026-07-29, six anchors across four families at `tc=120+1`. Supersedes ~3175, which was measured at 8+0.08 against the p6 net. See "The 2+1 measurement" below — **three caveats, and the third is that no anchor sits above the engine.** |
| Bench, with a net | **4,656,884** (4,063,328 at `base-phase6`) |
| Bench, no net | **6,951,633** |
| Current net | `nets/rogatia-p8a.nnue`, `(768→256)x2→1`, 160M filtered positions. SPRT'd **+184.38 +/- 28.01** over p6. |
| SPRT baseline | **current `main`** — `rogatia-base.exe` benches 4,772,409, which is `main`'s with-net count. Every queued test measures its own patch, not the accumulated phase. The tag `base-phase6` is the *phase* baseline, used only for the end-of-phase gate. **Do not rebuild `rogatia-base` while the queue is draining.** |
| Work split | **See "Active work split — set 2026-07-28" below.** Training box regenerates the corpus and retrains; laptop does Phase 7 search. |
| Laptop | **Draining the Phase 7 search queue.** Net fetched, checksum verified, all four gates pass. Bounds are per test — see "bounds tightened" below |

Tags: `base-phase3` (bench 54095910, depth 8), `base-phase4` (5356740),
`base-phase6` (4063328). **Bench depth changed 8 → 12 at commit `655f93c`** —
counts either side of it are not comparable.

---

## 2026-07-29 -- the 2+1 measurement: 3379 +/- 20

540 games, both machines, `8moves_v3.epd`, Hash=16, Threads=1.

**Run at `tc=120+1`, which is what CCRL Blitz states for itself** -- "equivalent
to 2'+1" on an Intel i7-4770K". Every earlier rating in this file was measured at
8+0.08. `scripts/gauntlet.sh` had described the list as 40/4, which is its old
control; that comment was wrong and is fixed.

| Opponent | CCRL Blitz | Games | W-L-D | Score | Implied |
|---|---|---|---|---|---|
| Zahak 8.0 | 3160 +/- 16 | 80 | 52-2-26 | 81.25% | 3415 +/- 72 |
| Zahak 9.0 | 3292 +/- 12 | 80 | 22-12-46 | 56.25% | 3336 +/- 50 |
| Zahak 10.0 | 3334 +/- 8 | 80 | 21-8-51 | 58.13% | 3391 +/- 43 |
| Smallbrain 6.0 | 3361 +/- 15 | 100 | 29-11-60 | 59.00% | 3424 +/- 45 |
| Clover 3.1 | 3399 +/- 11 | 100 | 11-33-56 | 39.00% | 3321 +/- 45 |
| Alexandria 3.5 | 3405 +/- 13 | 100 | 26-25-49 | 50.50% | 3408 +/- 52 |

Inverse-variance weighted: **3379 +/- 20**. Dropping the 81% row gives 3376 +/-
21, so the compressed anchor is not carrying the figure.

The jump from 3195 is accounted for: the p8a net SPRT'd at **+184.38 +/- 28.01**
over p6, and 3195 + 184 lands here.

### What this measurement cannot tell you

1. **80-100 games per anchor is thin.** The 3195 figure used 240 each.
2. **The six implied values span 103 points** -- 3321 to 3424. They agree on a
   band, not on a number.
3. **No anchor sits above the engine.** All six are rated 3160-3405.

The third one was in the process of being closed and was reopened by choice.
**Stormphrax 5.0.0 (3619) reached 20 games at 22.5%**, which converts to ~3404
and agrees with the other six. 22.5% is nowhere near the <5% floor at which
`gauntlet.sh` refuses to convert a score, so it was a usable anchor, not a
saturated one. It and Viridithas 15.0.0 (3681) were cancelled on the judgement
that both are too strong to evaluate against. Re-run them before this number is
published anywhere.

### Two process failures this run exposed

- **The laptop's anchor ratings lived only in a shell variable.** They were
  passed as `ANCHORS=` on the command line; the laptop was moved, the shell died,
  and three finished matches held scores that converted to nothing until the
  ratings were fetched again. Now recorded as `ANCHORS_WINDOWS` in
  `scripts/gauntlet.sh`. CCRL returns **403** to a plain fetcher -- use a browser
  User-Agent.
- **The 5+0 run never started and looked like it had.** It was an inline heredoc
  that died at line 3 on an unterminated quote, wrote one error line to its log,
  and exited. `scripts/gauntlet-later.sh` and `scripts/wait-for-machine.sh` now
  hold the wait and the detach, which are the only parts that cannot go on a
  command line safely. There is no per-time-control script: `lib.sh` already
  reads `TC` from the environment.

---

## 2026-07-29 -- time management merged, +28.34 +/- 10.32

`phase7-timeman3`, SPRT accepted H1 on `[0.00, 5.00]`:

```
1708 games, 545-406-757, 54.07%
Elo +28.34 +/- 10.32, nElo +45.47 +/- 16.48
```

The largest single Phase 7 result so far, and it resolved in **80 minutes**
against the 17-hour ceiling the stall limit allows.

Two scalers multiply the soft limit:

- **Node fraction.** If the best move already consumed most of the search tree,
  the position is easy -- move sooner.
- **Best-move stability.** The longer the root move has survived across
  iterations, the less the next iteration is worth.

**This is the first SPRT this project has ever run with tablebases enabled**, so
it is not directly comparable with the earlier Phase 7 numbers. Those all
measured an engine roughly 24 Elo below its own merged strength, because no
script passed `SyzygyPath` until the same day.

### Bench cannot gate it

The scalers sit behind `W.useTime`, which `bench` leaves false because bench is
depth-limited. The count does not move and that proves nothing either way, so
the merge was gated on the wiring surviving instead -- `scaled_soft_limit`
called from the soft-limit check rather than merely defined, `bestMoveStability`
reset and updated at the root, `rootNodes` accumulated in the move loop -- plus
a live timed search, which moved in 498 ms of a 10-second budget.

That check is not ceremony. `phase7-dblext2` was queued with a feature that was
defined, wired, and inert across null moves, and it would have returned a
perfectly believable number for something that was not running.

---

## 2026-07-29 -- the three timing questions, measured on an idle machine

These three were left open because they were cache-behaviour questions and the
box had been running matches all day. With both machines free they were measured
properly: **interleaved A/B**, alternating the two binaries so thermal drift and
background noise hit both equally, medians of 9 to 25 pairs, `bench` depth 15 for
a lower noise floor. Every one is bit-identical, so the node count is the gate
and only the time differs.

| Change | Result | Verdict |
|---|---|---|
| `Magic` struct, 32 -> 16 bytes under PEXT | +0.89%, -0.04%, +0.40% | **kept** |
| `see_ge` magic-lookup early-out | +1.52%, +0.38%, -0.26% | **reverted** |
| `contHist` 16 -> 12 piece slots | -1.19%, -1.66%, -1.17% | **reverted** |

**Two of the three were wrong, and only measurement could say so.** The estimates
had them at 1-4%, 1-4% and 0.5-1.5% respectively.

### `contHist` was the instructive failure

Packing `Piece` (encoded 1..6 and 9..14) into a dense 0..11 takes the table from
2 MB to 1.125 MB, and it is **reliably 1.3% slower**. Halving the footprint did
not matter; changing the strides did. `[16][64][16][64]` indexes with shifts
because every dimension is a power of two. `[12][64][12][64]` needs real
multiplies, on a table read several times per node by `score_move` and again by
`update_cont_hist`. The address arithmetic cost more than the cache saved.

The lesson generalises: **a smaller table is not automatically a faster one when
the index stops being a shift.**

### The `see_ge` early-out measured nothing

Three runs at +1.52%, +0.38% and -0.26% -- the sign changed, so the effect is
indistinguishable from zero. Note the first number: at 11 pairs it looked like a
solid win. It was noise, and only the larger samples showed it.

### First-run numbers are not results

Every one of these three looked different at 9-11 pairs than at 25. Anything
under about 1% needs 25 pairs at depth 15 on an idle box, and below ~0.5% this
harness cannot resolve it at all.

---

## 2026-07-29 -- what 612 gauntlet games say about how this engine plays

Analysis of every PGN from the 2+1 gauntlet, both machines. `scripts/style.py`
and `scripts/draw-anatomy.py`, plus the eval-trajectory work below.

### The critical finding: the tablebases were never switched on

**No test script sets `option.SyzygyPath`.** Not `gauntlet.sh`, not
`testqueue.sh`, not `sprt.sh`. The engine's default is `<empty>` and `main.cpp`
reads no environment variable -- only `datagen.cpp` does, for datagen alone.

So Syzygy probing, **merged at +24.07 +/- 9.41**, has been inert in every match
the engine has played since. The one run that measured it passed the option by
hand to one side:

```
"syzygy|rogatia-tb|rogatia-tb|0|5|option.SyzygyPath=C:/Users/minhp/syzygy/3-4-5"
```

Same binary on both sides. The tablebases *were* the patch. Nothing has enabled
them since, so the 3379 measurement is of an engine playing ~24 Elo below its
own merged strength, and every queued Phase 7 SPRT is measuring the same
handicapped engine.

### And it is visible in the games, which is how it was found

Rogatia throws away winning positions at three to five times the rate of every
opponent it faced. Share of each engine's own draws that came after it reached
+1.00:

| Engine | draws from +1.00 |
|---|---|
| **Rogatia** | **35.7% - 73.1%** (mean ~54%) |
| Clover 3.1 | 37.5% |
| Zahak 10.0 | 17.6% |
| Zahak 8.0 | 15.4% |
| Smallbrain 6.0 | 10.0% |
| Zahak 9.0 | 8.7% |
| Alexandria 3.5 | 6.8% |
| Stormphrax 5.0.0 | 0.0% |

This is not "chess is drawish", and it is not eval noise. Of 317 drawn games,
**109 held +1.00 for ten or more consecutive moves** and 90 held it for sixteen
or more. Median hold among the 168 that reached +1.00 is **21 moves**.

**38 drawn games peaked at +5.00 or better** -- a rook or more -- and 31 of
those still had a minor piece or better on the board at the final position.

### The mechanism: it liquidates into dead tablebase endings

Of 59 drawn games that peaked above +3.00 and held it ten-plus moves, the final
position was:

| pieces on board | games |
|---|---|
| 2 (bare kings) | 5 |
| 3 | 35 |
| 4 | 6 |
| 5 | 7 |
| 6+ | 6 |

**90% ended inside the 3-4-5 set already sitting on both machines' disks.**
Actual final positions, with the peak eval the engine had reported:

```
8/7k/4K3/8/6B1/8/8/8    K+B vs K    peak +12.80
k7/8/4n3/1K6/8/8/8/8    K+N vs K    peak  +7.31
8/1k1K4/8/8/8/8/8/8     K   vs K    peak  +4.24
8/8/k7/3K4/8/8/8/8      K   vs K    peak  +6.43
```

The engine wins a piece, evaluates it at +7 to +12, trades everything else off,
and arrives at a position that has been a known draw since before computers. It
reached **bare kings** from +4.24 five times.

Two causes, and they compound:

1. **The net does not know a lone minor piece cannot mate.** K+B vs K at +12.80
   is not a search failure, it is the evaluation being confidently wrong about a
   position with three pieces on it. This is the classic NNUE blind spot.
2. **The thing that would have masked it was switched off.** A WDL probe at
   depth would have returned `draw` for every position in that table and the
   search would have avoided the trade that reached it.

Fix (2) first -- it is one option in three scripts and it is already merged and
measured. Then re-measure before deciding how much of (1) is left.

### Style, for the record

Aggressive and tactically sharp, weak at converting.

- **Eval volatility 0.29-0.54 pawns/move**, against 0.17 for Alexandria, 0.19
  for Smallbrain and 0.22 for Stormphrax. Its evaluation swings two to three
  times as much as the strong opponents' -- consistent with an eval that reads
  +7 and then collapses to 0.
- **Sacrifices held 8+ plies in 25-50% of won games**, and the EAS stable core
  runs 4,000-7,300 against Smallbrain's 1,727. It plays sharply.
- **100% of wins ended in mate on the board**, never adjudication.
- **Mean depth 29-33**, ahead of every opponent except Stormphrax at 36.3.
- Game length **74-94 moves mean**, longest against the engines it cannot beat.

### Clean

Zero illegal moves, zero losses on time, zero disconnects, zero crashes across
612 games and 48,385 Rogatia moves.

`depth 244` appears on 1,008 moves and is not a bug: `MAX_PLY - 2` is the
iterative-deepening cap, and these are solved positions where every iteration
returns from the table instantly. Median 0.003s, 42.9s total across all 612
games. Not worth an early-exit rule.

---

## The one thing that will trip you up

**The net is not in git.** `nets/` and `*.nnue` are gitignored on purpose, so a
fresh clone builds the *PSQT fallback* engine, which is ~360 Elo weaker. You cannot reproduce the 3379 measurement, and you cannot
SPRT anything about the evaluation, without the net file.

The no-net and with-net bench counts are **per commit** — do not memorise a
pair. At `main` today it is **6,951,633 without** and **4,656,884 with**; at
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

**Fixed 2026-07-28, same family of bug: every UCI tunable was invisible to
every harness.** `tunable::print_options()` writes its 35 `option name ...`
lines through `std::printf`, while `uci.cpp` writes Hash/Threads and the
closing `uciok` through `std::cout`. Two buffers — and stdout is fully
buffered on a pipe, so the tunables sat in it while the explicitly-flushed
`uciok` went out ahead of them. On the wire the handshake read *three options,
uciok, then the tunables*, and every harness stops collecting options at
`uciok`. fastchess therefore built a three-entry option map and **discarded
every `option.SingularDepth=10`-style argument**, warning once per game start.
Typing `setoption` by hand always worked, which is why it survived so long.

No merged result is affected — every SPRT to date compared two *binaries* and
never set an option. But if you are on a checkout from before this fix,
**no options-based test on it means anything**, including anything `scripts/
spsa.py` would have produced. Sanity check before trusting any such run:

```bash
printf 'uci\nquit\n' | ./rogatia | tail -1     # must print uciok, not an option line
```

**Syzygy is now wired into the search (branch `phase7-syzygy`), and it has one
design property that reads like a bug if you meet it cold.** Fathom's
`tb_probe_wdl` wrapper refuses any position whose fifty-move counter is
non-zero, so the WDL probe fires at tablebase *entry* -- the child of the
capture that drops the position to five men -- and not throughout the ending.
That is deliberate and it is the case worth having: it is the moment the engine
decides whether to trade into the ending at all, which is the bucket the
gauntlet said we were losing (17 of 86 draws against Zahak 8.0 were positions
we evaluated at +1.00 and then liquidated into dead material). Sitting inside a
drawn KBvK the engine will still report the bishop as +3.87, and that costs
nothing, because the game is drawn either way. Won positions are handled
separately at the root by a DTZ probe, which is what actually makes progress.

Probing is dormant until `SyzygyPath` is set, so `bench` is unchanged at
4,772,409 and a machine with tablebases benches identically to one without.
**Setting the option by hand and then running `bench` will differ -- don't.**

The laptop now has the full set locally at `C:/Users/minhp/syzygy/3-4-5`, all
290 files, verified byte-for-byte against the home box. The home box's copy is
at `~/syzygy/3-4-5`.

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

## Phase 7 conversion work — set 2026-07-28

Replaying the 720-game Zahak gauntlet found one dominant weakness, and it is not
the one the roadmap predicted. Against Zahak 8.0 — the opponent the engine is
evenly matched with — **46 of 86 draws were positions the engine had itself
evaluated at +1.00 or better.** That is 19% of every game played, and Zahak threw
away far fewer from the same 86 games.

| Draw ended as | We were +1.00 | We never were |
|---|---|---|
| Three-fold repetition | **22** | 20 |
| Fifty-move rule | **7** | 19 |
| Dead material | **17** | 1 |

Three causes, three fixes, each on its own branch and each with its own test.
The fifty-move bucket is the *smallest*, not the largest: 19 of the 26 fifty-move
draws came from positions that were never winning, where a fifty-move draw is a
good result and nothing should change.

| Branch | Fixes | Bench | State |
|---|---|---|---|
| `phase7-syzygy` | Dead material — the search had never probed the tablebases sitting on disk | 4,772,409 (unchanged, dormant without `SyzygyPath`) | **merged, +24.07 +/- 9.41** |
| `phase7-repetition` | Three-fold — the engine scored a position the real game had visited *once* as a draw, where the rules need three | 4,772,409 (unchanged; bench FENs carry no game history, so the change is only reachable through `position ... moves ...`) | **parked** — ~+2.34 +/- 7.43, stalled at 3,260 games |
| `phase7-rule50` | Fifty-move — the evaluation could not read the counter at all | 4,772,409 (unchanged; the taper starts above a threshold an ordinary search never reaches) | **rejected, -15.03 +/- 7.97.** Retuned on `phase7-rule50b` (threshold 20 -> 65), queued |
| `phase7-pvfix` | The `PV continues after checkmate` warning, unexplained since before Phase 4 | 4,772,409 (unchanged — which is the proof: it changes what the engine says, never what it plays) | **merged**, no SPRT owed |

**All four are bench-neutral, and that is the point rather than a coincidence.**
Bench is the fingerprint of what the engine decides, so an unchanged count means
identical decisions in the bench positions. These fixes only reach situations
bench never visits: real game history behind the position, a running fifty-move
counter, and tablebase range. It is also why the datagen run in progress was left
alone — the labels for ordinary positions are unchanged bit for bit.

`scripts/testqueue.sh` runs the queue unattended; `docs/TESTING.md` has the
protocol and the analysis scripts that found all of this.

### Settled the same day, do not relitigate

- **Target is 3500**, not 3200. Phase 9 and OpenBench both become requirements.
- **A second anchor family** is deprioritised — the current rating is not what is
  being optimised.
- ~~**Verification at 20+0.2** will not be run.~~ **Superseded the same evening
  at 23:56** — standard time controls became a later goal, so a *per-phase*
  non-regression at 20+0.2 is now required. Per-*patch* verification is still
  not run. See "The long-time-control gate" in `docs/TESTING.md`.
- **"Search or evaluation?"** — both, as opportunities appear. The 24.8-vs-15.5
  depth observation is a reason to expect Phase 8 to pay well, not a gate.
- **SPSA is off the table on this box.** `SingularDepth` 10-vs-8 measured
  -9.41 +/- 12.37 over 1,440 games, so the hand-picked defaults are not badly
  set; and with the published constants (Rk 0.002, ck 4cp) the driver needs tens
  of thousands of games to move a parameter at all. It belongs on OpenBench.

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
2. Your job is **Phase 7 search work** and the SPRTs that validate it. The
   per-patch baseline is **current `main`**, so each test measures one patch
   rather than the whole accumulated phase:
   ```bash
   git checkout main && make EXE=rogatia-base EVALFILE="$(pwd)/nets/rogatia-p6.nnue"
   git checkout <patch-branch> && make EXE=rogatia-dev EVALFILE="$(pwd)/nets/rogatia-p6.nnue"
   scripts/sprt.sh ./rogatia-dev ./rogatia-base
   ```
   Rebuild `rogatia-base` whenever a patch merges into `main`. `base-phase6` is
   the *phase* baseline and is used only for the end-of-phase gate at 20+0.2.
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

Per-patch baseline is current `main` (`base-phase6` is the phase-gate baseline),
net pinned to `rogatia-p6.nnue`, bounds `[0.00, 5.00]`.

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

- ~~**`PV continues after checkmate`**~~ — **diagnosed and fixed 2026-07-28**
  (`phase7-pvfix`, merged as `337fe99`). A null-window fail-high left a stale
  `pvLen[ply + 1]`, so the memcpy spliced a line from a different position onto
  a good move. Bench-neutral: it changed what the engine said, never what it
  played.
- **The current net learned from pre-bugfix labels.** The 112M positions were
  generated before the en passant hashing and slider-blocker fixes. Regenerating
  should give cleaner labels. Not urgent; the net gets retrained regardless.
- **No SPSA tuning has ever been run.**
- **Anchors are one engine family** (above).

---

## Next steps

1. **Phase 7 — search build-out.** Work the queue, then retune the parked
   patches. See "How Phase 7 runs" below for the loop.
2. **Regenerate data and retrain** on post-bugfix labels — running on the
   training box now, ETA 2026-07-29 ~13:15.
3. **The end-of-phase gate at 20+0.2**, `[-10.00, 0.00]` against `base-phase6`.
   It has never been run and Phase 7 is the first phase that should end with it.
4. **A second anchor family** before quoting 3195 as settled. Deprioritised.
5. **OpenBench** once patches need 20k+ games, which one box cannot supply.

~~SPSA the 31 tunables~~ — dropped on this hardware, see "Settled the same day".

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

## 2026-07-29, laptop — three verdicts, and how Phase 7 actually runs

The queue ran overnight and returned all three conversion patches. One merged,
one parked, one rejected.

| Patch | Games | Result | State |
|---|---|---|---|
| `syzygy` | 1,966 | **+24.07 +/- 9.41**, H1 accepted | merged |
| `repetition` | 3,260 | +2.34 +/- 7.43, LLR stuck at 0.08 | **parked** with its resume config |
| `rule50` | 2,568 | **-15.03 +/- 7.97**, H0 accepted | **rejected**, retuned as `rule50b` |

The syzygy PGNs also showed *why* it worked, not just that it did. Dead-material
draws stayed at 58 total but inverted: 57 -> **17** while winning, 1 -> **41**
while not winning. Three-fold draws from winning positions went 69 -> 88, because
games escaped one draw bucket into another. That is how the next test was chosen.

### How Phase 7 runs

**Phase 7 is a breadth pass, not a depth pass.** The goal is to bring the search
to its strongest state by trying as many known techniques as the machine has time
for. So the loop is:

1. **Build the idea** on its own `phase7-*` branch, with a gate that proves the
   plumbing is inert when the feature is off. Bench must return to base exactly.
2. **Test it briefly.** Not to a verdict at any cost — to a verdict *or* to the
   conclusion that there is no verdict at these bounds.
3. **Park it if it is overwhelmingly negative or unresolvable.** Keep the branch
   and the resume state. Move to the next idea; the machine is the constraint.
4. **Come back and retune** using the parked snapshot, what the engine's own
   PGNs say, and published numbers from other engines. Then re-test.

**"Briefly" has numbers, and they live in `scripts/testqueue.sh`:**
`STALL_GAMES=4000` and `STALL_LLR=0.6`. Past 4,000 games with the LLR still
inside +-0.6, the answer is "too small to resolve at these bounds" — and that
answer is already in hand, so the test stops and the machine moves on.

**This is deliberately NOT an early abort on a losing result.** SPRT rejects a
real loss quickly on its own. Second-guessing it would throw away verdicts that
were about to arrive. `rule50` proves the point: it reached H0 in 2,568 games
without any help.

**Both live examples of the loop:**

- `repetition` stalled at 3,260 games projecting to ~120,000 for a number
  already known to be about +2. Parked. The branch and the resume config are
  kept, so the retune starts from 3,260 games rather than zero.
- `rule50` was rejected at threshold 20. `phase7-rule50b` raises it to **65**,
  built from the parked branch. Queued.
- `capthist` was rejected twice (-20 +/- 23 raw, then -8.69 +/- 33.62 scaled).
  `phase7-capthist2` rebuilds the same two commits on current main, because the
  base has moved by roughly 96 Elo since those tests and the earlier numbers no
  longer describe this engine. Queued.

A parked branch is therefore **not** a dead end. It is a saved starting point.
Do not delete `phase7-*` branches.

### The 20+0.2 gate — superseding the 17:12 entry

"Settled the same day" says verification at 20+0.2 will not be run. That was
written at 17:12 on 2026-07-28 and superseded at 23:56 the same evening, when
standard time controls became a later goal.

The settled position now: **no per-patch verification at 20+0.2** — it would cut
throughput by roughly five against one machine — but **one per-phase
non-regression** at `[-10.00, 0.00]`, tc=20+0.2, of the whole accumulated phase
against the previous phase tag. Phase 7 is the first phase that should end with
it. Suspect `src/tunable.h` margins first if it fails; search *features* keep
their sign across time controls, pruning *thresholds* do not.

### Queue state at 07:36

Running: `ttpv`. Behind it: `checkext, corrplexity, capthist, rule50b, conthist,
histage, dblext, probcut`. Eight tests at ~1,100 games/hour is roughly a day of
the only test machine, unattended.

**The runner now executes an immutable copy.** `sprt-results/.queue-running.sh`
is copied at launch, so editing `scripts/testqueue.sh` can no longer kill a
running queue — the failure that cost six hours on 2026-07-28. Edits to the
source take effect at the next launch.

---

## 2026-07-29, laptop — bounds tightened to [0.00, 3.00]

**Decision: Phase 7 tests at `[0.00, 3.00]`, ahead of the strength table**, which
puts that band above ~3300 and the engine at ~3175.

**Why.** Many modern search refinements are worth 2-4 Elo, and this phase exists
to find which of them help this engine. `elo1` is not a threshold a patch must
clear -- H1 accepted means "better than `elo0`" -- but a true +3 patch sits near
the middle of `[0, 5]`, which is where SPRT is slowest and closest to a coin flip
on which hypothesis it accepts. At `[0, 3]` that same patch is the design point
and accepts H1 about 95% of the time.

**What it costs.** Rejecting a bad patch gets about 2.8x slower; games to a
verdict scale about `1/(elo1 - elo0)^2`. `STALL_GAMES` therefore rises **4,000 ->
12,000** in `scripts/testqueue.sh`. The two must move together: a stall limit
that fires before the LLR can leave `+-0.6` resolves nothing and parks
everything, which is the opposite of why the bounds were tightened.

**The honest limit.** 12,000 games is ~11 hours on this box. Reliably resolving a
+2 patch needs more games than one machine supplies. `[0, 3]` reaches that wall
sooner than `[0, 5]` did, and the wall is what OpenBench exists to break.

**Rejected the same day: bundling the history stack.** capthist + conthist +
histage as one test, on Phase 4's precedent. Phase 4 bundled three *untested*
overlapping techniques; capthist has already measured negative twice, so a
failed bundle could not say which component caused it.

**Takes effect at the next queue launch.** A running drain keeps the bounds it
started with, and a resumed test keeps the bounds baked into its `config.json`.

### Revised the same morning — bounds are per test, not per phase

Applying `[0, 3]` to the whole queue put the drain at 1.5 to 4 days, because six
of the eight tests are first tests of new techniques and do not need it. The band
now follows **what each test asks**:

| The test is | Bounds | Stall limit |
|---|---|---|
| The first test of a new technique | `[0.00, 5.00]` | 4,000 (~3.5h) |
| A retune of a patch that already failed | `[0.00, 3.00]` | 12,000 (~11h) |

A new technique can plausibly be worth 10-40 Elo, and `[0, 5]` settles that in
~2-3 hours; `[0, 3]` there buys no answer `[0, 5]` would not already give. A
retune already measured at or below zero once, so a few Elo is the honest
expectation, and that is exactly the case `[0, 5]` coin-flips.

`STALL_GAMES` is now derived from `elo1` rather than set globally. The two cannot
be set apart: a `[0, 3]` test under a 4,000-game limit parks before its LLR can
move, and a `[0, 5]` test under a 12,000-game limit burns ~7 extra hours.

Retunes in the queue now: **capthist** (rejected twice) and **rule50b** (rejected
at threshold 20). The drain estimate falls to roughly **1 to 1.5 days**.

**The resume path is no longer unverified.** Two clean stop-and-resume cycles,
at 1,200 and 1,378 games, both carried every game rather than restarting from
zero. Order is what makes it safe: **kill the queue runner before fastchess.**
Kill fastchess first and the script logs a verdict, appends the test to
`queue-state`, and deletes its resume file.

---

## 2026-07-29, laptop -- audit corrections and a rebuilt queue

Ten review agents went over the merged engine and every unmerged branch. Four
things recorded in this file were wrong, and they had been steering decisions.

**Capture history was never rejected.** "Rejected twice" is not what happened.
The runs were 400 games (-20 +/- 23) and about 1,500 (-8.69 +/- 33.62).
Alexandria measures the same technique at +2.80 +/- 2.22 over 44,640 games at
our exact time control. At our error bars a +3 effect is invisible: both results
were null. The general lesson is bigger than this patch -- **the reference SPRTs
for everything left in Phase 7 needed 15,000 to 130,000 games, and this project
has been running 400 to 4,000.** STALL_GAMES rose 4,000 -> 20,000 and 12,000 ->
30,000 as a result. Read a stalled verdict as "this box cannot resolve this".

**Correction history has +-64 cp of authority, not +-32.** The comment, the
commit message and this file all said 32. `correction()` sums TWO tables, each
clamped to +-32. Against RfpMargin = 75 per ply that is nearly a full depth of
margin, so the difference is not cosmetic.

**The +33.13 should be quoted as ~20-25.** The pentanomial reconstructs exactly,
so the harness was right and nothing was bundled -- but the SPRT accepted at 752
pairs, near the earliest point LLR can cross 2.94, and a boundary crossing that
early is biased high. It is also not Berserk's +2.70: P(observing +33 given a
true +2.70) is about 1.4e-7. Elo gains scale roughly inversely with engine
strength, and this engine's whole scale runs hot.

**The 4.5% smaller tree was not a margin effect.** This file said "a more
accurate static evaluation prunes better". The dominant channel is `improving`,
a BOOLEAN: both sides of the comparison are now corrected values from different
slots, so the table injects a differential rather than a cancelling offset. That
boolean gates a full ply of LMR, doubles the LMP threshold, and shifts RFP by a
whole RfpMargin.

**The fifty-move explanation was also wrong**, and its correction is now on
`phase7-rule50c` -- see that commit.

---

## 2026-07-29 -- the Phase 8 corpus is filtered

`data/filtered-p8.bin` on the training box, **152,318,042 positions, 4.6 GB**,
built by `scripts/filter.cpp` (`make filter`) from the 20260728-090808 run:

```
./filter data/filtered-p8.bin 5000 150 data/20260728-090808/*.bin
```

| | |
|---|---|
| input | 474,648,448 in 16 shards |
| dropped, label above 5000 | **2** |
| dropped, duplicate position | 18,004,790 (3.8%) |
| survived both | 456,769,864 |
| thinned 1 in 3 | **152,318,042** |

**The tablebase label bug was real and almost never fired.** Two records in
474 million. Worth knowing why, so nobody re-panics about it: the datagen root
probe adjudicates as soon as the position reaches five men, and a 5,120-node
search almost never returns a tablebase score from a root the loop still
considers ordinary. The engine fix stands, but the corpus never needed rescuing
from it. The duplicates were the filter that actually mattered.

Sanity check on a 4M sample of the output: max |label| 4,447, no value near the
30,000 poison signature, results 37/29/34 percent loss/draw/win, and the label
distribution decays smoothly from zero. That is a healthy corpus.

**Datagen was still running when this was filtered** -- 474M of the 500M target,
finishing about 15:15. Re-run the same command when it completes to pick up the
last 25M; it costs about ten minutes and yields roughly 160M. The filter
tolerates a shard that is still being appended to, so it can be run at any time.

**Thinning is a blunt instrument and the number is a judgement.** The flat file
has no game boundaries, so 1-in-3 decorrelates consecutive plies without
respecting game structure. If the next net underfits, keep more; if it overfits
on near-duplicate positions, keep less.

---

## 2026-07-29 -- the p8a net: +184.38 +/- 28.01, adopted

`nets/rogatia-p8a.nnue`, sha256
`3874e70304b71b18aad8d60f2f1a36107432bf7050a0ecac26edd6240cdf9ed7`.

Same architecture as Phase 6 -- `(768 -> 256)x2 -> 1`, SCReLU, wdl=0.3, 40
superbatches. **Only the data changed**, deliberately, so the SPRT measures the
data and nothing else. Scaling to 1024 with output buckets is a separate
experiment and would have confounded the two.

| | |
|---|---|
| Corpus | 160,000,000 filtered from 476,630,045 raw |
| Trained | 5m02s on the 3090 |
| SPRT vs `rogatia-p6.nnue`, identical code | **+184.38 +/- 28.01**, 426 games, LLR 2.95, **H1 accepted**, LOS 100%, PairsRatio 10.27 |
| Bench | 4,772,409 -> **4,656,884** |

**Why so large.** The Phase 6 corpus was generated by the ~2800 PSQT engine.
This one came from a ~3175 NNUE engine -- a teacher roughly 375 Elo stronger --
then deduplicated and thinned. The net learns to imitate the search, so the
search's strength is the ceiling on the net, and that ceiling moved.

Gates, all clean: `run-nnue` matches a full refresh at all 555,385 nodes; bench
identical on `-march=native` and `-march=x86-64-v3`; bench identical on the
laptop and the training box. The start-position eval-scale check added the same
day stayed silent, so the scale held and the `tunable.h` pruning margins are
still calibrated to it.

### Game review -- 426 games, clean

| Check | Result |
|---|---|
| Illegal moves | **0** |
| Losses on time | **0** |
| Crashes / disconnects | **0** |
| `PV continues after checkmate` | **0** |
| `pvcheck.py`, warm engine | 20 games, **53,365 PVs, 0 illegal, 0 after mate** |

The PV number is the one worth keeping. Every pre-fix log has hundreds of those
warnings -- `ttpv.log` has 874 over 2,375 games -- and both engines here were
built from post-fix `main`. `pvcheck.py` replays through a WARM engine, which
`CHANGELOG` records as the only condition that can expose the bug at all.

### Style -- measurably sharper, and one problem unchanged

Against the p6 net over the same 426 games:

| | p8a | p6 |
|---|---|---|
| Sacrifices held >= 8 plies (level 1) | **34.2%** | 18.8% |
| Short wins, <= 60 moves | **15.6%** | 10.4% |
| Eval volatility, pawns/move | 0.559 | 0.412 |
| Mean depth reached | 21.93 | 21.45 |
| EAS stable core | **4918** | 2542 |

**The conversion problem is untouched, as expected.** 47 of 123 draws (38%) were
positions the engine had evaluated at +1.00 or better -- 42 three-fold, 5
fifty-move, 11 dead material. That is a SEARCH problem, not an evaluation one,
and it is what the Phase 7 queue addresses. A better net does not stop the
engine repeating its way out of a won game.

### Anchors replaced again

The old set was three versions of Zahak. With +184 the bottom of it saturates,
and one family was never independent enough to trust. Now three families
spanning 3160 to 3681: Zahak 8.0/9.0/10.0 (Go), Stormphrax 5.0.0 (C++),
Viridithas 15.0.0 (Rust). See `scripts/gauntlet.sh` for why the two strong ones
sit so far above.

---

## 2026-07-29 -- performance pass, and what six review agents found

Six agents went over the engine: four hunting speed, two re-reviewing the day's
own commits. The framing that made it productive: **a speedup that keeps the
node count bit-identical is free Elo and owes no SPRT.** Same tree, less time.
The gate for every item below is `bench` printing the SAME number, not a better
one -- a bench delta means the patch is wrong, not that it worked.

### Landed (branch `perf-nnue-cheap`, one commit per fix)

| Change | Est. nps |
|---|---|
| int32 multiply in `nnue::evaluate` | 2-6% |
| `alignas(64)` on `Network` | 0.5-2% |
| Fused accumulator update, 1 KB copy removed | 3-8% |
| Null-move accumulator pointer | 1-2% |
| `nnue::loaded()` inline | 0-2% |

**One of these was a self-inflicted regression.** The int64 accumulator added
that morning fixed a real overflow but widened BEFORE the multiply, making it a
64-bit multiply per term and suppressing vectorisation. The overflow was only
ever in the sum. Same safety, no cost, by moving one cast.

### The rest of that list, landed 2026-07-29

Same gate throughout: `bench` printing **4,656,884** and nothing else. Every one
of these is bit-identical by construction, so an equal bench is the proof the
patch is right, not a disappointment.

| Change | Commit | What it was |
|---|---|---|
| `zobrist::Psq` piece-major | `60cbd32` | Five keys for one (piece, square) sat 8 KB apart; now 40 contiguous bytes |
| Correction tables `int16` | `617fc36` | Slots clamped to +-8192 in an `int`; 256 KB -> 128 KB |
| Skip the second SEE | `81e6024` | `score_move` had already proved the capture good |
| Hoist the SEE slider unions | `aa0559d` | Rebuilt from two loads on every x-ray refresh |
| `make release` off PEXT | `719c8bf` | See below |

Two of the review's numbers did not survive contact.

**The SEE skip is worth far less than the 1-4% claimed.** An instrumented build
counted it over the whole bench: it fires on **163,108 of every 2,000,000** SEE
pruning tests, 8.2%, and disagreed with the full exchange zero times. The cap is
structural -- the test runs on every move and quiets dominate, so the noisy-move
share is the ceiling. It is free and it is proven, but call it well under 1%.

**`lmr_base()` does one `idiv`, not two.** The claim was that both divisions
issue because `tunable::` cannot be constant-folded. Compiling `search.cpp` to
assembly refutes it: the function is a **ternary**, and gcc branched rather than
`cmov`ing over both divisions -- `LmrNoisyDiv` and `LmrQuietDiv` appear at
separate sites on separate paths. The quiet path does still pay two, but the
second is `LmrHistDiv` in the history term, which no table can remove.

**So the table is not built.** At one division the 2-5% estimate roughly halves,
against a `[2][64][64]` int16 table of 16 KB competing for a 32 KB L1D -- it
could easily be net negative. Worse, a table not rebuilt on `setoption` makes
`LmrQuietDiv` and `LmrNoisyDiv` **silently inert**, which would quietly poison
an OpenBench SPSA run. It needs a timing run to justify, and the queue owns the
machine. Left open, with the reason recorded rather than the estimate.

The `see_ge` early-out -- skip the magic lookup when no slider of that class
bears on the square at all -- is open for the same reason: it adds a lookup up
front to save lookups later, so it is a measurement question, not a reasoning
one.

### The two slider indexers have now been compared

`make release` compiled the PEXT indexer, because BMI2 is part of `x86-64-v3`.
So does **Zen 1, Zen 2 and Excavator**, where PEXT is microcoded and much slower
than the multiply-shift it replaces -- and `release` is the binary other people
run. The path is now chosen by `ROGATIA_PEXT`: `make release` defines
`ROGATIA_NO_PEXT` and takes black magic, `make release-pext` opts back in for a
known Haswell-or-later or Zen-3-or-later target. `make` (native) is unchanged.

The earlier entry below says no build had ever run `verify_slider_tables()` with
PEXT. That was **half wrong**: `run_perft.cpp` calls it directly rather than
through an `assert`, so the native build has been checking PEXT against the
ray-walk reference on every perft run. The black-magic build was the one nothing
had executed. Both now agree:

| Indexer | bench | perft |
|---|---|---|
| PEXT | 4,656,884 | 37/37, 626,461,214 |
| black magic | 4,656,884 | 37/37, 626,461,214 |

Equal bench across the two also holds up the OpenBench rule that the count must
not move with the `-march` level.

### The perft gate printed its verdict in the wrong place

Fixed in `90a87cb`. The per-depth lines and the summary went to `std::printf`
while the FENs went to `std::cout`, and the two carry separate buffers: redirect
the run to a file and `tail` showed a bare FEN and no result, which reads
exactly like a crash. It cost a real diagnosis before anyone noticed the output
was simply out of order.

That is the same defect class as the `std::ifstream` segfault this runner was
rescued from hours earlier. **A gate whose output reads as a failure is worse
than no gate**, and this file has now produced two of them in one day.

### Two latent bugs, neither about speed

- **`history_` is reserved for 310 entries and a 130-move game needs 506.**
  `uci.cpp` replays the whole game before every search, so this is game plies
  PLUS search plies. Today it costs a silent reallocation and a ~57 KB copy mid
  search. If anyone converts it to a fixed array without resizing, it becomes a
  buffer overflow.
- **`make release` ships the PEXT path to hardware where PEXT is slow.** BMI2 is
  part of `x86-64-v3`, which is also satisfied by Zen 1, Zen 2 and Excavator,
  where PEXT is microcoded. That is the binary a rating list runs. Stockfish
  keeps `USE_PEXT` as a separate target for exactly this reason.
  *(Fixed the same day in `719c8bf` -- see "The two slider indexers have now
  been compared" above.)*

### The assert added that morning never ran

`verify_slider_tables()` was added to prove the black-magic and PEXT paths
agree. `native` and `release` both carry `-DNDEBUG` so the assert compiles out;
`debug` keeps asserts but passes **no `-march` flag**, so `__BMI2__` is
undefined and it takes the black-magic path. Add `-march=native` to the debug
flags.

**Correction, same day.** The conclusion drawn here -- that no build had ever
executed the check with PEXT -- was wrong, and only the `assert` reasoning was
right. `run_perft.cpp` calls `verify_slider_tables()` **directly**, not through
an `assert`, so `-DNDEBUG` never removed it and the native PEXT build has been
checking itself on every perft run. The build that had genuinely never run was
the black-magic one. Both were compared for the first time in `719c8bf`.

### `phase7-dblext2` would have measured the wrong thing

The double-extension cap was propagated in the move loop but **not across null
moves**, so the child read whatever the previous occupant of that stack slot
left -- almost always lower. A null move reset the cap, and null moves are
everywhere. The queued SPRT would have completed normally and returned a number
describing an **uncapped** feature. Fixed as `9c718c1`, before it ran.

Its inert setting is also not all-zeros: `DoubleExtCap=0` alone leaves the
negative extensions live.

### Open question on `phase7-rule50c`

The code is correct, but with no threshold the damping is active at essentially
every node -- `rule50_count()` is 0 only right after a capture or pawn move. So
every margin in `tunable.h` is now compared against a damped eval, and this
file's own rule says to re-check those margins when the eval scale moves.
Stockfish's margins were tuned WITH its damping present; ours were not. Decide
before spending a 30,000-game `[0, 3]` slot.

---

## 2026-07-29 -- the perft gate had never run on the laptop

`make run-perft` segfaulted with **zero output**, which reads as a crash in the
engine and is not. **Constructing a `std::ifstream` segfaults on this MinGW
toolchain.** `std::cout` works, C stdio works, only the file streams die -- an
eight-line program that opens a text file crashes.

So standing rule #1 -- *"Perft must stay bit-exact... runs `make run-perft`
before commit"* -- has been **unenforceable on the machine that makes most of
the commits**, and nothing said so. The same toolchain cannot link `make debug`
either (no `-lasan`/`-lubsan`), so two of the four gates were dead here and the
37/37 figure had only ever been verified on the training box.

`tests/run_perft.cpp` was the only file in the tree using `ifstream`. It now
uses `fgets`. **First run: 37/37, 626,461,214 nodes** -- exactly the recorded
count, so the engine was never the problem.

Worth generalising: a gate that cannot run is worse than no gate, because it
reads as a passing one. Both dead gates here failed in ways that looked like
someone else's problem -- a segfault with no message, and a link error about
sanitiser libraries.

### Also fixed this round

- `bench` calls `tb_free()`, so a UCI session that had set `SyzygyPath` silently
  played on without tablebases. `uci.cpp` already restored `Hash` after bench
  for the same reason; the tablebase path had not been given the same treatment.
- `history_` was reserved for 310 entries. `uci.cpp` replays the whole game
  before every search, so it holds game plies PLUS search plies -- a 130-move
  game needs 506, which cost a silent reallocation and a ~57 KB copy mid-search.
- The `debug` build passed no `-march`, so `__BMI2__` was undefined and it took
  the black-magic path. Since `native` and `release` both carry `-DNDEBUG`,
  `debug` is the only build that runs `verify_slider_tables()` -- which had
  therefore never compared the PEXT indexer to anything.
- The eval-scale check could not detect the drift it cites. The start position
  is worth ~+30 cp, so at twice the scale it reads ~+60 -- inside any sane band.
  It now probes queen odds (~+900), where a doubled scale reads ~+1800.
- `corrected_eval` clamped to `VALUE_MATE_IN_MAX_PLY - 1`, which is exactly
  `VALUE_TB` -- a value `is_decisive()` returns true for. The clamp did not
  clear the range the guards now test.
- The corpus filter could read a shard out of phase after a partial record, and
  its shuffle truncated the good output before rewriting it with an unchecked
  `fclose`. Both fixed; the second fix then failed on Windows because
  `std::rename` does not overwrite there, which the test caught.

---

## Updating this file

Whoever completes a phase updates: the state table, a changelog entry with the
SPRT number, and the net checksum if it changed. Push before the other machine
starts work — the whole point is that neither session has to guess.
