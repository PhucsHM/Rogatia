# Sources

Per-feature record of where each technique came from. Almost everything traces
to the Chess Programming Wiki. The notes column records what was chosen locally
and which decisions have a measurement behind them.

---

## Phase 1 — Board and move generation

| Feature | Source | Notes |
|---|---|---|
| Bitboard representation | CPW: Bitboards | Standard piece-centric layout, universal across engines |
| Black magic bitboards | CPW: Magic Bitboards; Analog Hors, "Magic Bitboards" (analog-hors.github.io) | Magic constants generated locally by seeded search, not copied from any engine's table |
| PEXT sliding attacks | CPW: BMI2, PEXT Bitboards | Opt-in path behind `ROGATIA_PEXT`, not `__BMI2__`: PEXT is microcoded on Zen 1/2. Both indexers verified against the ray-walk reference at the same perft and bench |
| Zobrist hashing | CPW: Zobrist Hashing | Fixed-seed splitmix64 for cross-machine determinism (OpenBench requirement) |
| Five key sets (main/pawn/non-pawn/major/minor) | CPW: Static Evaluation Correction History | Required by correction history; added early because retrofitting them through make/unmake is costly |
| Make/unmake with undo stack | CPW: Make Move, Unmake Move | Chosen over copy-make because accumulators and five keys make copying expensive |
| Legality filter incl. en-passant discovered check | CPW: Legal Move, Pinned Pieces | The en-passant case is the classic movegen bug — both pawns leave the rank at once |
| Perft with bulk counting | CPW: Perft, Perft Results | Node counts verified against the published suite |

## Phase 2 — Search core

| Feature | Source | Notes |
|---|---|---|
| PeSTO-style tapered PSQT | CPW: PeSTO's Evaluation Function; Ronald Friederich, RofChade | Published constant tables reproduced as data, verified entry-by-entry; blend and side-to-move framing written here |
| Tapered evaluation / game phase | CPW: Tapered Eval | Standard 24-point phase, integer interpolation (float would break bench determinism) |
| Bucketed transposition table | CPW: Transposition Table, Shared Hash Table | 6x10-byte entries per 64-byte line; layout and packing chosen locally |
| Depth-preferred + aging replacement | CPW: Replacement Strategies | `depth - age*2` scoring; 5-bit generation packed with bound and PV flag |
| Mate-score adjustment on store/probe | CPW: Mate Scores | Distance-from-node stored, distance-from-root returned |
| Fail-soft alpha-beta | CPW: Alpha-Beta, Fail-Soft | Returns the best score found rather than clamping to the window |
| Principal variation search | CPW: Principal Variation Search | Full window on move 1, null window after, re-search on a PV-node fail-high |
| Iterative deepening | CPW: Iterative Deepening | Previous iteration seeds both the TT move ordering and the aspiration centre |
| Aspiration windows | CPW: Aspiration Windows | Narrow window around the last score, widened by `delta += delta/3 + 5` on a fail |
| Quiescence search | CPW: Quiescence Search, Stand Pat | Captures and queen promotions; every move when in check |
| Delta pruning | CPW: Delta Pruning | Skips captures that cannot reach alpha even winning the piece for free |
| Static exchange evaluation | CPW: Static Exchange Evaluation, SEE - The Swap Algorithm | Swap-off loop with x-ray re-detection; derived from the algorithm description, not transcribed |
| MVV-LVA ordering | CPW: MVV-LVA | Victim value dominant, attacker as tie-break, split into good/bad by SEE |
| Killer moves | CPW: Killer Heuristic | Two slots per ply, quiet cutoffs only |
| Butterfly history with gravity | CPW: History Heuristic, Relative History Heuristic | `[stm][from][to]`, gravity update, malus for tried-and-failed quiets |
| Mate distance pruning | CPW: Mate Distance Pruning | Window clamped to what the remaining distance can deliver |
| Repetition / 50-move detection | CPW: Repetitions, Fifty-move Rule | Single repetition inside search, three for a game result -- split into two methods |
| Null move make/unmake | CPW: Null Move Pruning | State plumbing only |
| Reverse futility pruning | CPW: Reverse Futility Pruning | Linear margin per ply, one ply of relief when improving; constants picked here |
| Null move pruning | CPW: Null Move Pruning | `R = 3 + depth/3 + min((eval-beta)/200, 3)`, non-pawn-material guard, no verification search |
| Late move reductions | CPW: Late Move Reductions | `base + ln(d)*ln(mc)/divisor` in 1/1024 plies from a hardcoded integer ln table (determinism); adjustments for cut node, PV, improving, in check, history |
| Late move pruning | CPW: Futility Pruning (move count based) | `3 + depth^2/(2 - improving)` |
| SEE pruning in main search | CPW: Static Exchange Evaluation | Depth-scaled thresholds, separate for quiet and noisy |
| Continuation history | CPW: History Heuristic (countermove/follow-up variants) | `[prev piece][prev to][piece][to]` at 1- and 2-ply offsets, same bonus/malus as butterfly history |
| Internal iterative reduction | CPW: Internal Iterative Deepening (IIR section) | Reduce a ply when the node has no TT move, at PV and cut nodes only; the modern replacement for IID, which measures at roughly zero |
| Futility pruning (child node) | CPW: Futility Pruning | Linear depth margin against alpha, quiets only; guards for in-check (`staticEval` is `VALUE_NONE`) and mate-score alpha are ours |
| Razoring (quiescence-verified) | CPW: Razoring | The verified form: drop to qsearch and fail low only if it agrees. The bare-margin form measured at zero and was removed from Stockfish in 2020 |
| History pruning of quiets | CPW: History Leaf Pruning | Depth-scaled negative threshold; reuses the summed score `score_move` already wrote into `scores[]`, so it costs one load |
| ttPv reduction exemption | CPW: Transposition Table (PV flag) | The was-a-PV flag was already stored and discarded; made sticky and fed to LMR |
| En passant hashed only when capturable | CPW: Zobrist Hashing, En passant | Standard condition. Without it one position takes two keys, which costs TT hits and silently breaks repetition matching |
| Repetition bounded by plies-from-null | CPW: Null Move Pruning, Repetitions | A null move is not a legal continuation, so nothing before one can be repeated by real moves |
| `is_pseudo_legal` validation | CPW: Encoding Moves, Legal Move | Required before a TT move can be trusted; validates a bare 16-bit move from scratch |
| Time management (soft/hard split) | CPW: Time Management | Flat clock fraction, move overhead, soft checked between iterations and hard every 1024 nodes |
| UCI protocol | Stefan-Meyer Kahlen, UCI specification (public protocol document) | Protocol text only; no engine's implementation consulted |
| Deterministic bench | OpenBench README (public harness requirement) | Position set generated and legality-checked with this engine's own movegen |
| Self-play datagen | CPW: Automated Tuning | Structure is the field-standard loop: random plies, node-limited self-play, quiet filter, adjudication |
| bulletformat record layout | `jw1912/bulletformat` `src/chess.rs` (MIT) | Layout only -- a wire format, which has to match byte for byte. The packing code in `src/datagen.cpp` is written here |
| Syzygy probing | **Fathom** (`jdart1/Fathom`, MIT) | Vendored verbatim in `src/fathom/`. `tbchess.c` is `#include`d textually by `tbprobe.cpp`; renamed to `.cpp` so the Makefile rule compiles it, with no source edits. LICENSE copied alongside |

---

## Phase 7 — search build-out

| Feature | Source | Note |
|---|---|---|
| Singular extensions | CPW "Singular Extensions"; shape common to every modern engine | Exclusion search at the same ply with `ss->excludedMove`, verified inert by raising `SingularDepth` past the bench depth and confirming bench returned to base to the node |
| Correction history | CPW; Ethereal and Stockfish both publish the idea | Keyed on the pawn and non-pawn Zobrist sets, which is why five key sets exist. Raw eval goes to the TT, corrected eval to pruning -- conflating them compounds the correction on every revisit |
| Capture history | CPW "History Heuristic" | Implemented. **Two NULL results, not rejections** (-20 +/- 23 over 400 games, -8.69 +/- 33.62 over ~1,500). Alexandria measures this at +2.80 +/- 2.22 over 44,640 games at the same time control, so neither of our runs could have seen it. `phase7-capthist3` rebuilds it on current main with CaptHistDiv 8 -> 2, because at /8 the history term was 0.58x the pawn-to-knight gap where every reference engine is 2.6-6x |
| Syzygy probing in search | Fathom (`src/fathom/`, MIT, vendored since Phase 5) and its own header docs | WDL at internal nodes, DTZ at the root for won positions only -- Fathom's docs warn DTZ plays unnaturally when losing. Fathom's WDL wrapper refuses a non-zero fifty-move counter, so probing fires at tablebase *entry* |
| Repetition ply distinction | Stockfish's `is_draw(ply)` comment describes the rule; implemented from that description | One recurrence counts at or after the root, two before it. The engine previously conflated them and scored a position the real game had visited once as a draw |
| Fifty-move eval taper | CPW; the general idea is universal, the threshold is ours | Evaluation slides toward zero above a counter threshold. **Measured -15.03 +/- 7.97 and rejected.** Parked on `phase7-rule50c`. The objection is that it fires from counter 0, so at an ordinary counter of 20 it is a ~10% haircut on every score -- and all 33 margins in `tunable.h` are calibrated to the undamped scale, making it a global eval rescale wearing a fifty-move label |
| Fifty-move TT-cutoff guard | Stockfish uses 90; the mechanism is described in CPW's Transposition Table article | Split out of the taper and tested alone. The fifty-move counter is not in the Zobrist key, so one entry serves the same position at counter 3 and counter 93. Threshold 90 rather than a number of our own. Bench cannot gate it -- bench never reaches a counter of 90 -- so it was verified live from a root at 96 |
| Node-based and stability time management | CPW: Time Management; the two scalers are standard shapes | Soft limit multiplied by a node-fraction term (if the best move consumed most of the tree the position is easy) and a best-move-stability term. **Measured +28.34 +/- 10.32 over 1,708 games.** Constants are ours |
| PV assembly guarded by a real PV child | Own diagnosis, no source | Not a technique; a defect fix. The child's PV is only copied when a PV child was actually searched |

## Performance work -- 2026-07-29

Speed only.  Every item here is **bit-identical**: same tree, same node count,
less time. Their gate is `bench` printing the same number rather than a better
one, so none requires an SPRT.

| Change | Source | Note |
|---|---|---|
| int32 multiply in NNUE inference | Own analysis | The int64 accumulator added the same day was correct but widened BEFORE the multiply. The product provably fits int32 (65,025 x 32,767); only the sum needs 64 bits |
| `alignas(64)` on `Network` | Own analysis | Natural alignment was 2. Every feature column is 512 bytes apart, so all of them inherit the base's residue |
| Fused accumulator update | Shape is universal; the mod-2^16 argument is ours | Bit-identical because storing through `int16_t` is reduction mod 2^16, which commutes with addition -- so regrouping is safe whether or not the accumulator wraps |
| `nnue::loaded()` inline | Own analysis | Out-of-line, it is a real call per `make_move` in the non-LTO fallback build |
| Null-move accumulator pointer | CPW: Incremental Updates; the shape is universal | A null move changes no feature, so the child shares the parent's accumulator rather than copying 1 KB |
| Zobrist keys stored piece-major | Own analysis | The five key sets for one (piece, square) were 8 KB apart and `update_keys` reads four or five per piece event; now 40 contiguous bytes. The fill loop order is the wire format and must not change |
| Correction tables narrowed to `int16` | Own analysis | Values are clamped to +-8192, so the top half of each `int` was unreachable. 256 KB -> 128 KB |
| Second SEE skipped on a proven-good capture | Own analysis | `score_move` already ran `see_ge(m, -20)`, so SEE pruning repeated a full exchange for a known verdict. Fires on 8.2% of SEE pruning tests |
| `Magic` struct compacted under PEXT | Own analysis | `magic` and `shift` are black-magic-only. 32 -> 16 bytes, four descriptors per cache line |

Measured and abandoned:

| Candidate | Result |
|---|---|
| `see_ge` early-out | +1.52%, +0.38%, -0.26% over three runs -- indistinguishable from zero |
| `contHist` 16 -> 12 piece slots | 1.3% slower; packing to 12 turns power-of-two strides into multiplies |
| `lmr_base` lookup table | Not built. One division, not two; a table also makes two SPSA-tunable divisors inert |

**Not taken, and why.** Hand-written SIMD for the SCReLU multiply-accumulate is
the largest remaining item (~5-12%), and it stays out for now. The 128-bit form
needs `|l1w| <= 128`; both nets measure **127**, so they pass with one unit of
margin and a future net could be refused at load. That is a decision about
future-net brittleness, not a free win, so it waits for a measurement rather
than an argument.

---

## Tooling written for this project

| Script | Purpose |
|---|---|
| `scripts/style.py` | Playing-style profile from PGNs, implementing Stefan Pohl's published EAS component definitions. Reimplementation, not his tool |
| `scripts/draw-anatomy.py` | Splits drawn games by how they ended and whether we were winning |
| `scripts/pvcheck.py` | Replays games through a warm engine and validates every reported PV |
| `scripts/spsa.py` | SPSA driver. Gain sequences from the Spall paper; Rk/ck guidance from the published fishtest practice |
| `scripts/testqueue.sh` | Runs queued SPRTs unattended, one at a time. Bash, not PowerShell, for a measured reason — see `docs/TESTING.md` |

---

## Standing sources

- **Chess Programming Wiki** — https://www.chessprogramming.org/ — the default
  reference for every technique here.
- **Engine Programming Discord** — community norms, review, OpenBench access.
- **TalkChess** — https://talkchess.com/ — historical threads, testing methodology.

## Other engines

**Policy since 2026-07-30: other engines' source is not read.** Several authors
have asked that their repositories not be used as material for an AI-assisted
project, and some now carry an explicit notice. Ciekce asked directly that
Stormphrax not be used; that request is honoured, and it is applied generally
rather than only where asked.

What is used instead: the Chess Programming Wiki, published papers, and engine
authors' own written descriptions of their work.

**Published Elo figures remain valuable and are cited in the notes above.**
Several times a number from another project showed that one of this engine's
null results was an undersized sample rather than a rejection — Alexandria's
+2.80 +/- 2.22 over 44,640 games for capture history, against two runs here of
400 and ~1,500, is the clearest case.

Playing against a released binary in a gauntlet is a separate matter and
continues; that is what released binaries are for.
