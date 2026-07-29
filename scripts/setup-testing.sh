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

# Zahak 7.1 / 8.0 / 9.0 -- the Phase 6 anchor set, bracketing the engine at
# 2972 / 3160 / 3292 CCRL Blitz. One repo, Linux binary per version.
for zv in 7.1:7.1 8.0:8.0-avx 9.0:9.0-avx; do
	ztag=${zv%%:*}; zfile=${zv##*:}
	if [ ! -x "$E/zahak-$ztag" ]; then
		get "$E/zahak-$ztag" "https://github.com/amanjpro/zahak/releases/download/$ztag/zahak-linux-amd64-$zfile"
		chmod +x "$E/zahak-$ztag"
	fi
done

if [ ! -x "$E/toad-1.0.0" ]; then
	get "$TMP/toad.tar.gz" https://github.com/dannyhammer/toad/releases/download/v1.0.0/toad_v1.0.0_x86_64-unknown-linux-musl.tar.gz
	# The tarball is flat: toad, README.md, CHANGELOG.md. Take only the binary.
	tar -xzf "$TMP/toad.tar.gz" -C "$TMP" toad
	mv "$TMP/toad" "$E/toad-1.0.0"
fi

if [ ! -x "$E/goldfish-2.1.1" ]; then
	get "$E/goldfish-2.1.1" https://github.com/bsamseth/goldfish/releases/download/v2.1.1/goldfish-x86_64-linux-gnu
fi

if [ ! -x "$E/blunder-8.5.5" ]; then
	get "$TMP/blunder.zip" https://github.com/algerbrex/blunder/releases/download/v8.5.5/blunder-8.5.5.zip
	unzip -qo "$TMP/blunder.zip" -d "$TMP"
	# ponytail: -avx2 to match what the laptop already has. CCRL rates a
	# "64-bit" build without saying which; -avx2 may be slightly stronger than
	# the rated one, which would make Blunder's anchor read a little low.
	mv "$TMP/blunder-8.5.5/linux/blunder-8.5.5-avx2" "$E/blunder-8.5.5"
fi

chmod +x "$E"/*

echo
echo "ready:"
ls -1 "$ROOT/tools/fastchess" "$E"/* "$ROOT/books"/*.epd
echo
echo "next: make && scripts/gauntlet.sh 240 ./rogatia"

# Stormphrax (C++) and Viridithas (Rust) -- different families from Zahak, so
# the gauntlet is not three versions of one engine.  Both ship Linux binaries
# only for versions already past 3600; that is fine, a 15-20% score converts.
if [ ! -x "$E/stormphrax-5.0.0" ]; then
	get "$E/stormphrax-5.0.0" "https://github.com/Ciekce/Stormphrax/releases/download/v5.0.0/stormphrax-5.0.0-avx2"
	chmod +x "$E/stormphrax-5.0.0"
fi
if [ ! -x "$E/viridithas-15.0.0" ]; then
	get "$E/viridithas-15.0.0" "https://github.com/cosmobobak/viridithas/releases/download/v15.0.0/viridithas-15.0.0-linux-x86-64-v3"
	chmod +x "$E/viridithas-15.0.0"
fi
