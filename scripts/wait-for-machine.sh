#!/usr/bin/env bash
# Block until no match is running on this machine, then return.
#
#   bash scripts/wait-for-machine.sh && TC=300+0 bash scripts/gauntlet.sh 80 ./rogatia-p8a
#
# Two matches on one box distort each other's timing and both results become
# worthless, so a queued run has to wait rather than start beside the current
# one.  testqueue.sh carries its own copy of this check; this file is for
# chaining anything else -- a second gauntlet, a different time control --
# behind whatever is running now.
#
# Poll, deliberately.  There is no parent-child relationship to wait(1) on: the
# run being waited for was started by a different shell, usually a detached one.
#
# ponytail: no timeout.  A gauntlet legitimately runs for hours and a wrapper
# that gives up early is worse than one that waits, because it starts the second
# match anyway.  Kill it if you change your mind.
set -u

INTERVAL=${INTERVAL:-60}

running() {
	pgrep -x fastchess >/dev/null 2>&1 && return 0
	pgrep -x fastchess.exe >/dev/null 2>&1 && return 0
	# Windows: pgrep sees nothing useful, so ask the OS directly.
	command -v tasklist >/dev/null 2>&1 &&
		tasklist 2>/dev/null | grep -qi 'fastchess' && return 0
	return 1
}

announced=0
while running; do
	if [ $announced -eq 0 ]; then
		printf '[%s] a match is running; waiting for the machine\n' \
			"$(date '+%Y-%m-%d %H:%M:%S')"
		announced=1
	fi
	sleep "$INTERVAL"
done

[ $announced -eq 1 ] && printf '[%s] machine free\n' "$(date '+%Y-%m-%d %H:%M:%S')"
exit 0
