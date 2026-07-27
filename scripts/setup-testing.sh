#!/usr/bin/env bash
# Fetch the testing harness: fastchess, the OpenBench opening books, and the
# CCRL-rated reference engines. Everything lands in tools/ and books/, both of
# which are gitignored -- this script is the only record of where they came from.
#
#   scripts/setup-testing.sh
#
# Linux x86-64 (developed against CachyOS). Needs curl, tar and unzip, all of
# which are in the Arch base install. Re-running is safe: it skips what exists.
# ponytail: no checksums. These are GitHub release URLs over TLS; if that is
# compromised a checksum in the same repo does not help.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/tools/engines" "$ROOT/books"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

get() { echo "-> $2"; curl -sSL -o "$1" "$2"; }

# --- fastchess -------------------------------------------------------------
if [ ! -x "$ROOT/tools/fastchess" ]; then
	get "$TMP/fc.tar" https://github.com/Disservin/fastchess/releases/download/v1.8.2-alpha/fastchess-linux-x86-64.tar
	tar -xf "$TMP/fc.tar" -C "$TMP"
	mv "$TMP/fastchess-linux-x86-64/fastchess" "$ROOT/tools/fastchess"
	chmod +x "$ROOT/tools/fastchess"
fi

# --- opening books ---------------------------------------------------------
# 8moves_v3 is balanced and correct under ~2800; UHO is the biased book used
# above it. Both from OpenBench's Books/ manifest.
for b in 8moves_v3 UHO_Lichess_4852_v1; do
	[ -f "$ROOT/books/$b.epd" ] && continue
	get "$TMP/$b.zip" "https://raw.githubusercontent.com/AndyGrant/openbench-books/master/$b.epd.zip"
	unzip -qo "$TMP/$b.zip" -d "$ROOT/books"
done

# --- reference engines -----------------------------------------------------
# CCRL Blitz (40/4) ratings, read 2026-07-27. Kept in sync with the ANCHORS
# line in scripts/gauntlet.sh. None of these versions is on CCRL 40/15.
#   Toad 1.0.0     1776 +/- 18
#   Goldfish 2.1.1 2252 +/- 16
#   Blunder 8.5.5  2664 +/- 11
E=$ROOT/tools/engines

if [ ! -x "$E/toad-1.0.0" ]; then
	get "$TMP/toad.tar.gz" https://github.com/dannyhammer/toad/releases/download/v1.0.0/toad_v1.0.0_x86_64-unknown-linux-musl.tar.gz
	tar -xzf "$TMP/toad.tar.gz" -C "$TMP"
	mv "$(find "$TMP" -type f -name 'toad*' ! -name '*.tar.gz')" "$E/toad-1.0.0"
fi

if [ ! -x "$E/goldfish-2.1.1" ]; then
	get "$E/goldfish-2.1.1" https://github.com/bsamseth/goldfish/releases/download/v2.1.1/goldfish-x86_64-linux-gnu
fi

if [ ! -x "$E/blunder-8.5.5" ]; then
	get "$TMP/blunder.zip" https://github.com/algerbrex/blunder/releases/download/v8.5.5/blunder-8.5.5.zip
	unzip -qo "$TMP/blunder.zip" -d "$TMP"
	mv "$TMP/blunder-8.5.5/linux/blunder-8.5.5-avx2" "$E/blunder-8.5.5"
fi

chmod +x "$E"/*

echo
echo "ready:"
ls -1 "$ROOT/tools/fastchess" "$E"/* "$ROOT/books"/*.epd
echo
echo "next: make && scripts/gauntlet.sh 240 ./rogatia"
