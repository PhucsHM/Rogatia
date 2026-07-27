#!/usr/bin/env bash
# Rogatia against the CCRL-rated anchor engines in tools/engines/, one match per
# opponent. Prints the score and an Elo estimate anchored on each opponent's
# published rating. This is what substantiates an Elo *number* -- an SPRT only
# ever tells you whether one build beats another.
#
#   scripts/gauntlet.sh [games-per-opponent] [engine]
#   scripts/gauntlet.sh 400 ./rogatia-base
#
# Env overrides: TC, CONCURRENCY, BOOK, HASH.
set -eu
. "$(dirname "$0")/lib.sh"

GAMES=${1:-400}
ENGINE=$(bin "${2:-$ROOT/rogatia-base}")

# name : CCRL Blitz (40/4) rating : that rating's own error bar.
# 1-CPU 64-bit entries, list read 2026-07-27 from
# https://computerchess.org.uk/ccrl/404/cgi/compare_engines.cgi
# None of these three versions appear on CCRL 40/15.
ANCHORS="toad-1.0.0:1776:18 goldfish-2.1.1:2252:16 blunder-8.5.5:2664:11"

OUT=$ROOT/sprt-results/gauntlet-$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"

for a in $ANCHORS; do
	name=${a%%:*}
	echo "== $name ($GAMES games, tc=$TC, concurrency=$CONCURRENCY)"
	"$FASTCHESS" \
		-engine cmd="$ENGINE" name=Rogatia \
		-engine cmd="$(bin "$ROOT/tools/engines/$name")" name="$name" \
		-each tc="$TC" option.Hash="$HASH" option.Threads=1 \
		-openings file="$BOOK" format=epd order=random \
		-rounds $(( GAMES / 2 )) -games 2 -repeat \
		-concurrency "$CONCURRENCY" -recover \
		-pgnout file="$OUT/$name.pgn" > "$OUT/$name.log" 2>&1
done

echo
printf "%-16s %5s %6s %5s %5s %5s %7s   %s\n" opponent CCRL games W L D score "Rogatia (anchored)"
for a in $ANCHORS; do
	set -- $(echo "$a" | tr : ' ')
	awk -v name="$1" -v ccrl="$2" -v ccrlerr="$3" '
		/^Elo:/   { split($0, e, /[ ,]+/); elo = e[2]; err = e[4] }
		/^Games:/ { games = $2+0; w = $4+0; l = $6+0; d = $8+0
		            # +0 forces numeric: sub() leaves a string, and a string
		            # compare would call "44.58" < "5" true.
		            pct = $(NF-1); sub(/\(/, "", pct); pct += 0 }
		END {
			printf "%-16s %5d %6d %5d %5d %5d %6.1f%%   ", name, ccrl, games, w, l, d, pct
			# Saturated matches (0% or 100%) give elo = +/-inf and no usable
			# anchor. Report the score, refuse to invent a rating.
			if (elo ~ /nan|inf/ || err ~ /nan|inf/ || pct < 5 || pct > 95)
				print "saturated -- no usable anchor"
			else
				printf "%.0f +/- %.0f  (diff %+.1f +/- %.1f)\n",
				       ccrl + elo, sqrt(err*err + ccrlerr*ccrlerr), elo, err
		}
	' "$OUT/${a%%:*}.log"
done
echo "(logs and PGNs in $OUT)"
