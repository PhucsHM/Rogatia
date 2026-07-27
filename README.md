# Rogatia

A UCI chess engine written from scratch in C++20, optimized for blitz time controls (5+0 and faster).

**Status: early development.** Phase 1 (board representation, move generation, perft) — see [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Goals

- **3200+ CCRL**, by way of a modern alpha-beta search and a self-trained NNUE evaluation.
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
| Sliding attacks | Black magic bitboards, with a PEXT path behind `__BMI2__` |
| Move generation | Staged pseudo-legal with a legality filter |
| State | Make/unmake with an explicit undo stack |
| Evaluation | PSQT now; NNUE from Phase 6 |
| Testing | [fastchess](https://github.com/Disservin/fastchess) SPRT, then OpenBench |
| NNUE trainer | [bullet](https://github.com/jw1912/bullet) |

Five independent Zobrist key sets (main, pawn, non-pawn, major, minor) are maintained from the start — correction history needs them later, and retrofitting them through make/unmake is painful.

## Acknowledgements

Built on techniques documented by the [Chess Programming Wiki](https://www.chessprogramming.org/) and the wider engine programming community. Algorithms are implemented from understanding rather than adapted from other engines' source; per-feature source notes are kept in [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## License

[GPL-3.0](LICENSE).
