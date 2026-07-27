#!/usr/bin/env bash
# SPRT a dev binary against the baseline. See docs/TESTING.md.
#
#   scripts/sprt.sh <dev> <base> [elo0] [elo1]
#   scripts/sprt.sh ./rogatia-dev ./rogatia-base          # [0, 10], the under-2800 band
#   CONCURRENCY=4 TC=20+0.2 scripts/sprt.sh ./a ./b 0 5
#
# Env overrides: TC, CONCURRENCY, BOOK, HASH, ROUNDS.
set -eu
. "$(dirname "$0")/lib.sh"

DEV=${1:?usage: sprt.sh <dev> <base> [elo0] [elo1]}
BASE=${2:?usage: sprt.sh <dev> <base> [elo0] [elo1]}
ELO0=${3:-0}
ELO1=${4:-10}

mkdir -p "$ROOT/sprt-results"
OUT=$ROOT/sprt-results/sprt-$(date +%Y%m%d-%H%M%S)

echo "dev=$DEV base=$BASE tc=$TC concurrency=$CONCURRENCY bounds=[$ELO0, $ELO1]"

"$FASTCHESS" \
	-engine cmd="$(bin "$DEV")"  name=dev \
	-engine cmd="$(bin "$BASE")" name=base \
	-each tc="$TC" option.Hash="$HASH" option.Threads=1 \
	-openings file="$BOOK" format=epd order=random \
	-sprt elo0="$ELO0" elo1="$ELO1" alpha=0.05 beta=0.05 model=normalized \
	-rounds "${ROUNDS:-100000}" -games 2 -repeat \
	-concurrency "$CONCURRENCY" -recover \
	-pgnout file="$OUT.pgn" 2>&1 | tee "$OUT.log"
