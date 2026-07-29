# Rogatia

A UCI chess engine written from scratch in C++20, aimed at blitz.

**Measured: 3379 +/- 20 CCRL Blitz.** 540 games against six CCRL-rated anchors
across four engine families, at `tc=120+1` — the control CCRL Blitz states for
itself.

That number has a real limit worth stating up front: **every anchor is rated
3160–3405, so none of them sits above the engine.** It shows Rogatia is around
3380; it cannot show what happens against stronger opposition, because it never
faced any.

| Opponent | CCRL Blitz | Games | W-L-D | Score | Implied |
|---|---|---|---|---|---|
| Zahak 8.0 | 3160 | 80 | 52-2-26 | 81.25% | 3415 +/- 72 |
| Zahak 9.0 | 3292 | 80 | 22-12-46 | 56.25% | 3336 +/- 50 |
| Zahak 10.0 | 3334 | 80 | 21-8-51 | 58.13% | 3391 +/- 43 |
| Smallbrain 6.0 | 3361 | 100 | 29-11-60 | 59.00% | 3424 +/- 45 |
| Clover 3.1 | 3399 | 100 | 11-33-56 | 39.00% | 3321 +/- 45 |
| Alexandria 3.5 | 3405 | 100 | 26-25-49 | 50.50% | 3408 +/- 52 |

The six implied values span 103 points. They agree on a band, not on a number.

## What is in it

**Search** — fail-soft PVS, iterative deepening, aspiration windows, quiescence
with SEE and delta pruning, a bucketed transposition table. Pruning: null move,
LMR, reverse futility, late move pruning, SEE pruning, futility, razoring
verified by quiescence, history pruning, internal iterative reduction. Singular
extensions. Correction history on the pawn and non-pawn keys. Node-based and
best-move-stability time management. Syzygy 3-4-5 probing, WDL in search and DTZ
at the root.

**Evaluation** — a self-trained NNUE, `(768 -> 256)x2 -> 1` with SCReLU and an
incremental accumulator, trained on 160 million positions from the engine's own
self-play. Tapered PSQT is the fallback when no net is loaded.

All 35 search constants are exposed as UCI options so a tuner can drive them
without a recompile.

Move generation is perft-exact: 37/37 checks, 626,461,214 nodes.

## Building

Requires a C++20 compiler and GNU make.

```bash
make                 # optimized native build -> ./rogatia
make release         # portable x86-64-v3 static build
make release-pext    # the same with PEXT -- only for Haswell+ or Zen 3+
make run-perft       # move generation correctness suite
make bench           # deterministic node-count fingerprint
```

`make release` deliberately does **not** use PEXT. BMI2 is part of `x86-64-v3`,
which Zen 1, Zen 2 and Excavator also satisfy, and PEXT is microcoded there.

The build honours `EXE=`, `CXX=` and `EVALFILE=` for
[OpenBench](https://github.com/AndyGrant/OpenBench) compatibility.

**The network is not in the repository.** `nets/` is gitignored, so a fresh
clone builds the PSQT fallback, which is roughly 360 Elo weaker. Build with
`make EVALFILE=/abs/path/to/net.nnue`.

## Usage

Rogatia speaks [UCI](https://www.chessprogramming.org/UCI) and runs in any
standard chess GUI.

**Keep the `.nnue` file next to the binary.** The release looks for its net
beside itself, so no setup is needed as long as the two stay together. If you
move the net elsewhere, point at it with the `EvalFile` UCI option.

If the net is not found the engine says so and plays on with the piece-square
fallback, which is roughly 360 Elo weaker — so if the strength looks wrong, that
is the first thing to check:

```
info string eval: FAILED to load ...
```

A quick check that it loaded correctly — this must print **4656884**:

```bash
./rogatia bench
```

## Design notes

| Area | Choice |
|---|---|
| Board | Bitboards, piece-centric, with a redundant mailbox for O(1) square lookup |
| Sliding attacks | Black magic bitboards, with an opt-in PEXT path |
| Move generation | Pseudo-legal in one pass, legality filter, selection-sort picker |
| State | Make/unmake with an explicit undo stack |
| Evaluation | NNUE `(768 -> 256)x2 -> 1`, SCReLU, incremental |
| Endgames | Syzygy 3-4-5 via vendored [Fathom](https://github.com/jdart1/Fathom) |
| Testing | [fastchess](https://github.com/Disservin/fastchess) SPRT |
| NNUE trainer | [bullet](https://github.com/jw1912/bullet) |

Five independent Zobrist key sets (main, pawn, non-pawn, major, minor) are
maintained from the start, because correction history needs them and
retrofitting them through make/unmake is painful.

`./rogatia bench` prints the same node count on every machine and every
`-march` level. Zobrist keys use a fixed seed for exactly that reason.

## How it is developed

Nothing that changes search behaviour is merged without an SPRT. Speed-only
changes are held to a different standard: the node count must be **bit-identical**,
so the gate is `bench` printing the same number rather than a better one.

Two findings from the current work, both from the engine's own games rather than
from reading the code:

- **It draws winning positions far more than its opponents do** — 35–73% of its
  draws came after reaching +1.00, against 0–18% for every opponent but one. Of
  317 drawn games, 109 held +1.00 for ten or more consecutive moves.
- **90% of those games ended with five or fewer pieces on the board**, inside
  the tablebase set already sitting on disk. It wins a piece, calls it +7, trades
  down, and reaches K+B vs K.

Full history is in [`CHANGELOG.md`](CHANGELOG.md). Plan in
[`docs/ROADMAP.md`](docs/ROADMAP.md), testing protocol in
[`docs/TESTING.md`](docs/TESTING.md), per-feature sources in
[`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## Goals

- **3500+ CCRL Blitz**, via a modern alpha-beta search and a self-trained NNUE.
  Neural evaluation inside alpha-beta rather than MCTS: the search runs on CPU,
  which is what blitz and the rating lists both reward.
- **Blitz-first.** Parameters tuned at the time control the engine plays, with
  real work on time management rather than a fixed fraction of the clock.
- **Own data only.** Every network is trained exclusively on Rogatia's own
  self-play games — no Leela data, no Stockfish data, no third-party nets.

## Acknowledgements

Built on techniques documented by the
[Chess Programming Wiki](https://www.chessprogramming.org/) and the wider engine
programming community. Per-feature source notes are in
[`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## License

[GPL-3.0](LICENSE).
