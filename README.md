# Rogatia

A UCI chess engine written from scratch in C++20, aimed at blitz.

> **Development stopped 2026-07-30.** The author is starting over, writing a
> chess engine from scratch as a first programming project, without AI
> assistance. This repository stays up as a record; the v0.1 release works and
> the measurements below stand.
>
> **Built with AI assistance.** The author directed the project — what to build,
> how it was tested, and what the results meant — and the C++ was written with
> an LLM. This is stated here so nobody has to find it out later.
>
> **From 30 July 2026, other engines' source is no longer used as material for
> this project.** Where an author has asked that their repository be left alone,
> that is honoured; see [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## Strength

**3379 +/- 20 CCRL Blitz**, from 540 games at `tc=120+1` against six CCRL-rated
opponents.

| Opponent | CCRL Blitz | Games | W-L-D | Score | Implied |
|---|---|---|---|---|---|
| Zahak 8.0 | 3160 | 80 | 52-2-26 | 81.25% | 3415 +/- 72 |
| Zahak 9.0 | 3292 | 80 | 22-12-46 | 56.25% | 3336 +/- 50 |
| Zahak 10.0 | 3334 | 80 | 21-8-51 | 58.13% | 3391 +/- 43 |
| Smallbrain 6.0 | 3361 | 100 | 29-11-60 | 59.00% | 3424 +/- 45 |
| Clover 3.1 | 3399 | 100 | 11-33-56 | 39.00% | 3321 +/- 45 |
| Alexandria 3.5 | 3405 | 100 | 26-25-49 | 50.50% | 3408 +/- 52 |

Limits of the figure:

- Every opponent is rated 3160–3405. None is above the engine, so the result
  does not bound its strength from above.
- The six implied values span 103 points.
- Tested at blitz only. Long time controls are unmeasured.

## Download

Binaries and the network are in the
[latest release](https://github.com/PhucsHM/Rogatia/releases).

| File | For |
|---|---|
| `rogatia-*-windows-x86-64-v3.exe` | Windows |
| `rogatia-*-windows-x86-64-v3-pext.exe` | Windows, Intel Haswell+ or AMD Zen 3+ only |
| `rogatia-*-linux-x86-64-v3` | Linux, static |
| `rogatia-p8a.nnue` | Network |

PEXT is microcoded on Zen 1, Zen 2 and Excavator, where the PEXT build is
slower. The plain build runs everywhere.

## Usage

Rogatia speaks [UCI](https://www.chessprogramming.org/UCI) and runs in any
standard chess GUI.

Keep the `.nnue` next to the binary; the engine looks for its network beside
itself. To keep it elsewhere, set the `EvalFile` UCI option.

`./rogatia bench` prints **4656884** when the network is loaded and `6951633`
when it is not. Without a network the engine uses a piece-square fallback and is
about 360 Elo weaker; it reports the failure as:

```
info string eval: FAILED to load ...
```

### UCI options

| Option | Default | Range |
|---|---|---|
| `Hash` | 16 | 1–65536 MB |
| `Threads` | 1 | 1 |
| `Move Overhead` | 10 | 0–5000 ms |
| `SyzygyPath` | *(empty)* | path |
| `EvalFile` | *(build-time)* | path |

35 further options expose every search constant for tuning; see
[`src/tunable.h`](src/tunable.h).

## Features

**Search** — fail-soft PVS, iterative deepening, aspiration windows, quiescence
with SEE and delta pruning, bucketed transposition table with depth-preferred
aging replacement. Null move, LMR, reverse futility, late move pruning, SEE
pruning, futility, quiescence-verified razoring, history pruning, internal
iterative reduction. Singular extensions. Correction history on the pawn and
non-pawn keys. Node-count and best-move-stability time management. Syzygy 3-4-5,
WDL in search and DTZ at the root.

**Evaluation** — NNUE `(768 -> 256)x2 -> 1`, SCReLU, incremental accumulator,
trained on 160 million positions of the engine's own self-play. Tapered PSQT as
the no-network fallback.

**Move generation** — perft-exact: 37/37 checks, 626,461,214 nodes.

| Area | Choice |
|---|---|
| Board | Bitboards, piece-centric, with a redundant mailbox |
| Sliding attacks | Black magic bitboards, opt-in PEXT path |
| Move generation | Pseudo-legal in one pass, legality filter, selection-sort picker |
| State | Make/unmake with an undo stack |
| Zobrist | Five key sets: main, pawn, non-pawn, major, minor |
| Endgames | Syzygy via vendored [Fathom](https://github.com/jdart1/Fathom) |
| Testing | [fastchess](https://github.com/Disservin/fastchess) SPRT |
| NNUE trainer | [bullet](https://github.com/jw1912/bullet) |

`bench` is deterministic: the same node count on every machine and every
`-march` level.

## Building

Requires a C++20 compiler and GNU make.

```bash
make                 # native build -> ./rogatia
make release         # portable x86-64-v3 static build
make release-pext    # the same with PEXT
make run-perft       # move generation correctness suite
make bench           # node-count fingerprint
```

`EXE=`, `CXX=` and `EVALFILE=` are honoured for
[OpenBench](https://github.com/AndyGrant/OpenBench).

The network is not in the repository — `nets/` is gitignored. Download
`rogatia-p8a.nnue` from the release and build against it:

```bash
make EVALFILE=/abs/path/to/rogatia-p8a.nnue
```

## Development

Changes to search behaviour require a passing SPRT. Speed-only changes must
leave the node count bit-identical, so their gate is `bench` printing the same
number rather than a better one. Networks are trained exclusively on Rogatia's
own self-play games — no Leela data, no Stockfish data, no third-party networks.

Target is 3500+ CCRL Blitz.

[`CHANGELOG.md`](CHANGELOG.md) — development log.
[`docs/ROADMAP.md`](docs/ROADMAP.md) — plan.
[`docs/TESTING.md`](docs/TESTING.md) — testing protocol.
[`docs/PROVENANCE.md`](docs/PROVENANCE.md) — per-feature sources.

## Acknowledgements

Techniques documented by the
[Chess Programming Wiki](https://www.chessprogramming.org/) and the wider engine
programming community.

## License

[GPL-3.0](LICENSE).
