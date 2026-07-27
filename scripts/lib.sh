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
BOOK=${BOOK:-$ROOT/books/8moves_v3.epd}
