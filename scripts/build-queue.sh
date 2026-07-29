#!/usr/bin/env bash
# Build the SPRT binaries for one half of the queue, from a clean tree each time.
#
#   bash scripts/build-queue.sh a        # first half + the shared base
#   bash scripts/build-queue.sh b
#
# The queue itself deliberately builds nothing -- a build failure four hours
# into an unattended run is the worst possible time to find one -- so this runs
# first and gates every binary before the queue is started.
#
# Every dev branch in the queue shares merge-base a422c32, which is why one
# `rogatia-base` serves them all. Do not pair a dev binary with a base built
# from a different commit: the test then measures the difference between the
# two commits as well as the patch.
set -u

cd "$(dirname "$0")/.." || exit 1

HALF=${1:-a}
NET=$PWD/nets/rogatia-p8a.nnue

if [ ! -f "$NET" ]; then
	echo "FATAL: no net at $NET -- nets are gitignored, copy it over first" >&2
	exit 1
fi
echo "net: $(sha256sum "$NET" 2>/dev/null | cut -c1-16) $(stat -c%s "$NET") bytes"

# branch-or-commit : output binary name
BASE="a422c32:rogatia-base"
HALF_A="origin/phase7-dodeeper:rogatia-dd
origin/phase7-rule50tt:rogatia-r50tt
origin/phase7-dblext2:rogatia-dblx2
origin/phase7-hygiene:rogatia-hyg
origin/phase7-histage2:rogatia-age2"
HALF_B="origin/phase7-timeman3:rogatia-tm3
origin/phase7-conthist2:rogatia-ch6b
origin/phase7-capthist3:rogatia-capt3
origin/phase7-corrplexity2:rogatia-cplx2"

case "$HALF" in
	a) LIST="$BASE
$HALF_A" ;;
	b) LIST="$BASE
$HALF_B" ;;
	*) echo "usage: $0 [a|b]" >&2; exit 2 ;;
esac

start_branch=$(git rev-parse --abbrev-ref HEAD)
git fetch -q origin || { echo "FATAL: fetch failed" >&2; exit 1; }

failed=0
echo "$LIST" | while IFS=: read -r ref out; do
	[ -z "$ref" ] && continue
	printf '\n=== %s -> %s ===\n' "$ref" "$out"

	if ! git checkout -q --detach "$ref" 2>/dev/null; then
		echo "FAILED: cannot check out $ref"
		failed=1
		continue
	fi

	# Wipe the objects. Switching branches can move a source file's mtime
	# BACKWARDS, and make then keeps a stale .o -- which links a binary that is
	# neither branch and benches like neither.
	rm -rf build

	if ! make -s CXX=g++ EXE="$out" EVALFILE="$NET" >/dev/null 2>&1; then
		echo "FAILED: build error"
		failed=1
		continue
	fi

	echo "  $(./"$out" bench 2>/dev/null | tail -1)"
done

git checkout -q "$start_branch"
echo
echo "back on $start_branch"
ls -la rogatia-* 2>/dev/null | awk '{print "  " $5, $NF}'
exit $failed
