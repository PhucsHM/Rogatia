#!/usr/bin/env bash
# Launch a fleet of datagen workers, one process per thread.
#
#   scripts/datagen.sh [positions-per-worker] [workers] [nodes-per-move]
#   scripts/datagen.sh 10000000 16 5000
#
# The engine is single-threaded by design (Phase 7 owns SMP), so parallelism
# here is N independent processes with different seeds writing separate files.
# bullet reads a directory of .bin parts, so they never need concatenating.
#
# Unlike SPRT this is throughput work, not latency work, so it uses every
# logical core -- SMT siblings help here and hurt there.
set -eu
. "$(dirname "$0")/lib.sh"

POSITIONS=${1:-10000000}
WORKERS=${2:-$(nproc)}
NODES=${3:-5000}

ENGINE=$(bin "$ROOT/rogatia")
[ -x "$ENGINE" ] || { echo "build the engine first: make" >&2; exit 1; }

OUT=$ROOT/data/$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"

echo "$WORKERS workers x $POSITIONS positions @ ${NODES}n -> $OUT"

pids=""
for i in $(seq 1 "$WORKERS"); do
	# Seeds are spread far apart so two workers cannot walk the same openings.
	"$ENGINE" datagen "$OUT/part-$i.bin" "$POSITIONS" "$(( i * 7919 ))" "$NODES" \
		> /dev/null 2> "$OUT/part-$i.log" &
	pids="$pids $!"
done

trap 'kill $pids 2>/dev/null || true' INT TERM
wait

echo "done: $(du -sh "$OUT" | cut -f1) in $OUT"
ls -l "$OUT"/*.bin | awk '{s+=$5} END {printf "%d positions total\n", s/32}'
