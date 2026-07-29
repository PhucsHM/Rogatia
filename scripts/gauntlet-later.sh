#!/usr/bin/env bash
# Queue a gauntlet at a given time control behind whatever is running now.
#
#   nohup bash scripts/gauntlet-later.sh 300+0 80 ./rogatia-p8a \
#         > gauntlet-5plus0.log 2>&1 < /dev/null &
#
# There is no per-time-control script and there should not be one.  lib.sh
# already reads TC from the environment, so a whole time control is one variable
# -- the 5+0 run on the home box was a hand-written inline heredoc that died at
# line 3 on an unterminated quote, wrote one line to its log, and left the
# benchmark looking like it was running for hours when nothing was.
#
# This file exists only to hold the WAIT and the detach, which is the part that
# cannot go on a command line safely.
#
#   $1  time control, in fastchess form   (300+0 = 5+0, 120+1 = 2+1)
#   $2  games per opponent                (default 80, matching the 2+1 run)
#   $3  engine to measure                 (default ./rogatia-p8a)
set -u

TC_ARG=${1:-}
if [ -z "$TC_ARG" ]; then
	echo "usage: $0 <tc> [games] [engine]   e.g. $0 300+0 80 ./rogatia-p8a" >&2
	exit 2
fi

GAMES=${2:-80}
ENGINE=${3:-./rogatia-p8a}

cd "$(dirname "$0")/.." || exit 1

printf '[%s] queued: tc=%s, %s games/opponent, engine %s\n' \
	"$(date '+%Y-%m-%d %H:%M:%S')" "$TC_ARG" "$GAMES" "$ENGINE"

bash scripts/wait-for-machine.sh || exit 1

printf '[%s] starting\n' "$(date '+%Y-%m-%d %H:%M:%S')"

# export, not a `TC=x exec ...` assignment prefix.  The prefix form does work --
# exec is a special builtin, so bash keeps the assignment and passes it on --
# but nobody reading it in a hurry can be sure, and being wrong here is silent:
# gauntlet.sh would fall back to lib.sh's 8+0.08 default and produce a
# perfectly normal-looking run at the wrong time control.
export TC=$TC_ARG
exec bash scripts/gauntlet.sh "$GAMES" "$ENGINE"
