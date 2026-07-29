# Rogatia

A UCI chess engine written from scratch in C++20, optimized for blitz time controls (5+0 and faster).

**Status: Phase 7 of 9, search build-out.**

Move generation is perft-exact (37/37, 626,461,214 nodes). The search is a
fail-soft PVS with iterative deepening, aspiration windows, a bucketed
transposition table, the full low-depth pruning set (null move, LMR, RFP, LMP,
SEE pruning, futility, razoring, history pruning, internal iterative reduction),
singular extensions, and correction history on the pawn and non-pawn keys.
Evaluation is a **self-trained NNUE**, `(768 -> 256)x2 -> 1` with SCReLU and an
incremental accumulator, trained on 112 million of the engine's own self-play
positions. All 33 search constants are exposed as UCI options.

Latest measurement: **3379 +/- 20 CCRL Blitz**, from a 540-game gauntlet against
six CCRL-rated anchors across four engine families, run at `tc=120+1` — the
control CCRL Blitz states for itself. The six implied values span 103 points and
agree on the band rather than on the number.

Not yet worth publishing: every anchor is rated 3160-3405, so none of them sits
above the engine. The measurement can show the engine is around 3380; it cannot
show what happens against stronger opposition, because it never faced any.

Current work is aimed by measurement rather than by a feature list. Replaying the
gauntlet showed that **19% of all games were positions the engine evaluated as
winning and then drew** — three separate causes, three separate fixes, each on
its own branch under its own test. One passed and merged, one was too small to
resolve, one lost and is being retuned. See [`CHANGELOG.md`](CHANGELOG.md),
[`docs/ROADMAP.md`](docs/ROADMAP.md) and [`docs/TESTING.md`](docs/TESTING.md).

## Goals

- **3500+ CCRL**, by way of a modern alpha-beta search and a self-trained NNUE evaluation. Neural evaluation inside alpha-beta, rather than MCTS — the search runs on CPU, which is what blitz and the rating lists both reward.
- **Blitz-first.** Search parameters tuned at the time control the engine is meant to play, with real effort spent on time management rather than treating it as a fixed fraction of the clock.
- **Clean provenance.** Every network is trained exclusively on Rogatia's own self-play games. No Leela data, no Stockfish data, no third-party nets.

## Building

Requires a C++20 compiler (clang recommended) and GNU make.

```bash
make                # optimized native build -> ./rogatia
make run-perft      # move generation correctness suite
make bench          # deterministic node-count fingerprint
```

The build honours `EXE=`, `CXX=`, and `EVALFILE=` for [OpenBench](https://github.com/AndyGrant/OpenBench) compatibility.

## Usage

Rogatia speaks [UCI](https://www.chessprogramming.org/UCI) and runs in any standard chess GUI (Cute Chess, Arena, BanksiaGUI, En Croissant).

## Design notes

| Area | Choice |
|---|---|
| Board | Bitboards, piece-centric, with a redundant mailbox for O(1) square lookup |
| Sliding attacks | Black magic bitboards, with an opt-in PEXT path (`make release-pext`) |
| Move generation | Pseudo-legal, generated in one pass, with a legality filter and a selection-sort picker |
| State | Make/unmake with an explicit undo stack |
| Evaluation | NNUE `(768 -> 256)x2 -> 1`, SCReLU, incremental; tapered PSQT as the no-net fallback |
| Endgames | Syzygy 3-4-5 via vendored [Fathom](https://github.com/jdart1/Fathom): WDL in search, DTZ at the root |
| Testing | [fastchess](https://github.com/Disservin/fastchess) SPRT, then OpenBench |
| NNUE trainer | [bullet](https://github.com/jw1912/bullet) |

Five independent Zobrist key sets (main, pawn, non-pawn, major, minor) are maintained from the start — correction history needs them later, and retrofitting them through make/unmake is painful.

## Acknowledgements

Built on techniques documented by the [Chess Programming Wiki](https://www.chessprogramming.org/) and the wider engine programming community. Algorithms are implemented from understanding rather than adapted from other engines' source; per-feature source notes are kept in [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## License

[GPL-3.0](LICENSE).
