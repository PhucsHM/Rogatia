# Provenance

Per-feature record of where each technique came from.

**Why this file exists.** Rogatia is GPL-3.0 and reads other GPL-3 engines as references, which is legally fine — copyright protects expression, not algorithms, and the entire engine programming field is built on shared published techniques. What is *not* fine is transliteration: same statement order, same magic constants, renamed identifiers. That is a translation, and translations are derivative works.

The line is procedural, not just legal. Read a technique, close the file, implement from your own notes. This log is the record that the line was respected. It costs one line per feature and it defuses derivative accusations before they start.

**Format:** `Feature — source(s) consulted — notes`

---

## Phase 1 — Board and move generation

| Feature | Source | Notes |
|---|---|---|
| Bitboard representation | CPW: Bitboards | Standard piece-centric layout, universal across engines |
| Black magic bitboards | CPW: Magic Bitboards; Analog Hors, "Magic Bitboards" (analog-hors.github.io) | Magic constants generated locally by seeded search, not copied from any engine's table |
| PEXT sliding attacks | CPW: BMI2, PEXT Bitboards | Alternative path behind `__BMI2__`; kept perft-diffable against the magic path |
| Zobrist hashing | CPW: Zobrist Hashing | Fixed-seed splitmix64 for cross-machine determinism (OpenBench requirement) |
| Five key sets (main/pawn/non-pawn/major/minor) | CPW: Static Evaluation Correction History | Added early because retrofitting them through make/unmake later is painful |
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
| Razoring (quiescence-verified) | CPW: Razoring | Deliberately the verified form -- drop to qsearch and fail low only if it agrees -- not the bare-margin form that measured at zero and was removed from Stockfish in 2020 |
| History pruning of quiets | CPW: History Leaf Pruning | Depth-scaled negative threshold; reuses the summed score `score_move` already wrote into `scores[]`, so it costs one load |
| ttPv reduction exemption | CPW: Transposition Table (PV flag) | The was-a-PV flag was already stored and discarded; made sticky and fed to LMR |
| En passant hashed only when capturable | CPW: Zobrist Hashing, En passant | Standard condition. Without it one position takes two keys, which costs TT hits and silently breaks repetition matching |
| Repetition bounded by plies-from-null | CPW: Null Move Pruning, Repetitions | A null move is not a legal continuation, so nothing before one can be repeated by real moves |
| `is_pseudo_legal` validation | CPW: Encoding Moves, Legal Move | Required before a TT move can be trusted; validates a bare 16-bit move from scratch |
| Time management (soft/hard split) | CPW: Time Management | Flat clock fraction, move overhead, soft checked between iterations and hard every 1024 nodes |
| UCI protocol | Stefan-Meyer Kahlen, UCI specification (public protocol document) | Protocol text only; no engine's implementation consulted |
| Deterministic bench | OpenBench README (public harness requirement) | Position set generated and legality-checked with this engine's own movegen |
| Self-play datagen | CPW: Automated Tuning; the datagen sections of Stormphrax and Viridithas READMEs | Structure is the field-standard loop (random plies, node-limited self-play, quiet filter, adjudication); no code read or adapted |
| bulletformat record layout | `jw1912/bulletformat` `src/chess.rs` (MIT) | **Layout only** -- a wire format, not an implementation. Fields transcribed deliberately, because a format has to match byte for byte to be worth anything. The packing code in `src/datagen.cpp` is our own |
| Syzygy probing | **Fathom** (`jdart1/Fathom`, MIT) | **Vendored verbatim** in `src/fathom/`, not reimplemented. `tbchess.c` kept as `.c` because `tbprobe.cpp` `#include`s it textually. Renamed `.c` to `.cpp` only so the existing Makefile rule compiles it; no source edits. LICENSE copied alongside |

---

## Standing sources

- **Chess Programming Wiki** — https://www.chessprogramming.org/ — CC-BY-SA prose, the safest source of all. Default reference for every technique.
- **Engine Programming Discord** — community norms, review, OpenBench access.
- **TalkChess** — https://talkchess.com/ — historical threads, testing methodology.

## Reference engines read (never transliterated)

| Engine | License | Used for |
|---|---|---|
| Stormphrax | GPL-3.0 | `src/tunable.h` as a map of which modern search features exist and roughly what magnitude their constants take |
| Alexandria | GPL-3.0 | Readable structure of a modern engine |
| Obsidian | GPL-3.0 | Cross-check on compact implementations |
| Caissa | **MIT** | The one permissively-licensed strong reference — code may be reused with attribution |
| Stockfish | GPL-3.0 | Consulted only to answer "what is the current best-known form of technique X" |

**Not read:** Motor — the repository has no LICENSE file, so all rights are reserved regardless of being public.
