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

# Balanced openings, deliberately, and not the sharp book the SPRT script uses.
# A rating is only meaningful against the runs it is compared with, and the
# current 3195 figure was measured on this book.  Changing it here silently
# makes the next number incomparable with the last one.
BOOK=${BOOK:-$BOOK_BALANCED}

# name : CCRL Blitz (40/4) rating : that rating's own error bar.
# 1-CPU 64-bit entries, list read 2026-07-28 from
# https://computerchess.org.uk/404/rating_list_all.html
#
# Replaced at Phase 6. The original set (toad 1776, goldfish 2252, blunder
# 2664) is saturated: the engine scored 99.4%, 96.0% and 95.4% against them,
# and a score that lopsided converts to nothing usable. These three bracket
# the engine instead of sitting under it.
#
# Caveat worth knowing: three versions of ONE engine share a playing style, so
# they are not fully independent anchors the way three different engines would
# be. They were chosen because Zahak publishes a Linux binary for every version
# and its versions happen to span the band. Add a different family before
# treating the number as settled.
ANCHORS="zahak-7.1:2972:18 zahak-8.0:3160:16 zahak-9.0:3292:12"

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
