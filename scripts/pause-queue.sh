#!/usr/bin/env bash
# Stop the running queue cleanly, keeping every game played.
#
#   bash scripts/pause-queue.sh          # then move the machine
#   HALF=b bash scripts/testqueue.sh &   # ... and start it again afterwards
#
# **Stop before you move the laptop. Do not just close the lid.**
#
# fastchess measures the clocks in wall time. If the machine suspends
# mid-match, both engines suspend with it, and on wake fastchess sees minutes
# of elapsed time against a 8+0.08 budget and scores every game in flight as a
# loss on time. Those are not recoverable -- the snapshot cannot undo a
# recorded result, only preserve what came before it.
#
# So a clean stop costs at most the last 15 seconds. A suspend costs up to
# eight corrupted games AND poisons the test with false time losses.
set -u

cd "$(dirname "$0")/.." || exit 1

resume_dir=sprt-results

# The queue's own monitor snapshots every 15s, but take one more here: this is
# the moment that matters, and config.json is still the current match's.
if [ -f config.json ]; then
	for f in "$resume_dir"/*.config.json; do
		[ -e "$f" ] || continue
	done
	# Which test is running? The queue names its resume file after it, and the
	# newest one is the live match.
	live=$(ls -t "$resume_dir"/*.config.json 2>/dev/null | head -1)
	if [ -n "$live" ]; then
		cp -f config.json "$live.tmp" 2>/dev/null
		# Last NON-WHITESPACE byte. fastchess writes CRLF, so the file ends
		# `}\r\n` and a bare `tail -c 1` returns the newline -- which command
		# substitution strips to nothing, failing the test for every file.
		if [ "$(tail -c 64 "$live.tmp" 2>/dev/null | tr -d '[:space:]' | tail -c 1)" = "}" ]; then
			mv -f "$live.tmp" "$live"
			echo "snapshot updated: $live"
		else
			rm -f "$live.tmp"
			echo "config.json was mid-write; kept the previous snapshot: $live"
		fi
	fi
fi

# Order matters. Stop the QUEUE first so it does not start the next test the
# moment the current one dies, then the harness, then any engine left behind.
# Killing fastchess alone leaves its engine children running at full CPU --
# that happened on 2026-07-28 and three orphans sat at 90% until noticed.
stopped=0
for p in $(ps -ef 2>/dev/null | awk '$0 ~ /bash scripts\/testqueue\.sh/ && $0 !~ /awk/ {print $2}'); do
	kill "$p" 2>/dev/null && stopped=$((stopped + 1))
done
[ "$stopped" -gt 0 ] && echo "stopped $stopped queue process(es)"
sleep 2

if command -v taskkill >/dev/null 2>&1; then
	taskkill //F //IM fastchess.exe >/dev/null 2>&1
	sleep 2
	for e in $(tasklist //NH 2>/dev/null | awk '/^rogatia-/ {print $1}' | sort -u); do
		taskkill //F //IM "$e" >/dev/null 2>&1
	done
else
	pkill -x fastchess 2>/dev/null
	sleep 2
	# Match the PROCESS NAME, never the command line.  `pkill -f '[r]ogatia-'`
	# looks safe because the bracket stops it matching itself, but it still
	# matches any OTHER shell whose command line happens to contain the string --
	# including an ssh session that names a rogatia binary. That killed the
	# caller mid-script on 2026-07-30 and the work it was about to do never ran,
	# silently. `comm` is the executable name and cannot collide that way.
	for p in $(ps -eo pid=,comm= | awk '$2 ~ /^rogatia-/ {print $1}'); do
		kill -9 "$p" 2>/dev/null
	done
fi

sleep 2
left=$(ps -ef 2>/dev/null | grep -cE 'fastches[s]|rogatia-[a-z0-9]' || true)
echo "remaining match processes: ${left:-0}"
echo
echo "Games are kept. Restart with:"
echo "  HALF=b nohup bash scripts/testqueue.sh >> sprt-results/queue-runner-b.log 2>&1 &"
