# Shared settings for the testing scripts. Sourced, not run.
# Every path derives from the repo root so this works on both machines.

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

FASTCHESS=$ROOT/tools/fastchess
[ -x "$FASTCHESS.exe" ] && FASTCHESS=$FASTCHESS.exe

# Windows builds carry a .exe; Linux ones do not. Callers pass the bare name.
bin() { if [ -x "$1.exe" ]; then echo "$1.exe"; else echo "$1"; fi; }

# Physical cores, never logical. SMT siblings distort timing at 8+0.08 and
# cause spurious losses on time.
physical_cores() {
	if command -v lscpu >/dev/null 2>&1; then
		lscpu -p=Core,Socket | grep -v '^#' | sort -u | wc -l
	else
		# ponytail: Windows has no lscpu; assume 2-way SMT, true on both boxes.
		echo $(( ${NUMBER_OF_PROCESSORS:-4} / 2 ))
	fi
}

# Leave a couple of cores for the OS and the harness itself.
CONCURRENCY=${CONCURRENCY:-$(( $(physical_cores) - 2 ))}
[ "$CONCURRENCY" -lt 1 ] && CONCURRENCY=1

TC=${TC:-8+0.08}
HASH=${HASH:-16}

# ---------------------------------------------------------------- Syzygy ----
#
# **Every match script must pass this or it measures a weaker engine.**
#
# Syzygy probing is merged and measured at +24.07 +/- 9.41. It is also DORMANT
# until `SyzygyPath` is set: the engine defaults the option to <empty> and
# main.cpp reads no environment variable of its own -- only datagen.cpp does,
# for datagen. From the merge until 2026-07-29 no script passed the option, so
# the feature was inert in every game the engine played, including the whole
# 3379 gauntlet. It was found from the PGNs: 90% of the winning positions the
# engine drew ended with five or fewer pieces on the board, inside this very
# table set.
#
# The two boxes keep the set in different places, so probe for it rather than
# hardcoding either.
for _tb in "$HOME/syzygy/3-4-5" "/c/Users/minhp/syzygy/3-4-5" "C:/Users/minhp/syzygy/3-4-5"; do
	if [ -d "$_tb" ]; then
		# CONVERT THE PATH ON WINDOWS.  The engine is a native Windows binary
		# and Fathom calls the Win32 file API, which does not understand an
		# MSYS path: `/c/Users/...` is rejected outright while
		# `C:/Users/...` loads.  Verified against the engine, both forms:
		#   /c/Users/minhp/syzygy/3-4-5  -> "no tablebases, probing stays off"
		#   C:/Users/minhp/syzygy/3-4-5  -> "5-piece tablebases"
		# The directory test above passes for BOTH, so without this the script
		# would hand over a path the engine silently refuses and every match
		# would run with no tablebases while the setup looked correct.
		if command -v cygpath >/dev/null 2>&1; then
			SYZYGY_DEFAULT=$(cygpath -m "$_tb")
		else
			SYZYGY_DEFAULT=$_tb
		fi
		break
	fi
done
SYZYGY=${SYZYGY:-${SYZYGY_DEFAULT:-}}

# An INCOMPLETE set is worse than none. The engine builds -DNDEBUG, so Fathom's
# own asserts are gone and a truncated file reads as garbage. datagen.sh checks
# the count for exactly this reason; so does this.
TB_OPT=""
if [ -n "$SYZYGY" ]; then
	_n=$(ls "$SYZYGY" 2>/dev/null | wc -l)
	if [ "$_n" -ge 290 ]; then
		TB_OPT="option.SyzygyPath=$SYZYGY"
	else
		echo "WARNING: $SYZYGY holds $_n files, expected 290 -- running WITHOUT tablebases" >&2
	fi
else
	echo "WARNING: no Syzygy 3-4-5 found -- running WITHOUT tablebases, which is ~24 Elo" >&2
fi

# Ask the ENGINE, not the filesystem.  A readable directory proves nothing: the
# path form has to survive Fathom's own file API too, and when it does not the
# engine says so on a line no one reads and then plays on without tablebases.
# Call this once before a match and let it fail loudly instead.
verify_tb() {
	[ -z "$TB_OPT" ] && return 0
	local out
	out=$(printf 'setoption name SyzygyPath value %s\nquit\n' "$SYZYGY" | "$1" 2>&1 | grep -i 'info string syzygy')
	case "$out" in
		*"-piece tablebases"*) echo "  tablebases: ${out#*syzygy: }" ;;
		*) echo "FATAL: $1 rejected SyzygyPath=$SYZYGY" >&2
		   echo "       engine said: ${out:-<nothing>}" >&2
		   return 1 ;;
	esac
}

# Two books, because the two harnesses want opposite things and there is no one
# default that is right for both.
#
#   balanced  -- normal positions.  What a rating measurement needs, and what
#                produced the current 3195 figure, so the gauntlet keeps using
#                it or the next number is not comparable with the last one.
#   sharp     -- unbalanced openings.  Above ~2800 the draw rate on a balanced
#                book swamps a 3-Elo patch; UHO raises the decisive rate and an
#                SPRT resolves in far fewer games.  docs/TESTING.md has the
#                crossover.
#
# Each script picks its own; BOOK= in the environment still overrides either.
BOOK_BALANCED=$ROOT/books/8moves_v3.epd
BOOK_SHARP=$ROOT/books/UHO_Lichess_4852_v1.epd
