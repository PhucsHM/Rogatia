#!/usr/bin/env bash
# Run queued SPRTs one after another, unattended.
#
#   nohup bash scripts/testqueue.sh > sprt-results/queue-runner.log 2>&1 &
#
# A verdict takes hours and there is one machine, so tests run back to back.
# nohup detaches it, so it outlives the terminal or agent session that started
# it.  Progress goes to sprt-results/queue-summary.log.
#
# **This is bash and not PowerShell for a measured reason.**  Launching
# fastchess through PowerShell's Start-Process with -RedirectStandardOutput
# makes every engine fail startup -- "Engine didn't respond to uciok after
# startup", fatal, seconds in -- because that redirection disturbs the handles
# fastchess hands to its own child processes.  The identical command with a
# plain shell redirect runs clean.  Measured back to back on the same binaries:
# Start-Process 0 games and a fatal, shell redirect 12 games and no error.
#
# It builds nothing.  The Makefile needs a POSIX shell, and a build failure
# four hours into an unattended run is the worst possible time to find one, so
# every binary is built and gated before it joins the queue.
#
# Resumable: finished tests are appended to sprt-results/queue-state, so a
# reboot picks up where it left off instead of repeating four hours.

set -u
cd "$(dirname "$0")/.." || exit 1

OUT=sprt-results
STATE=$OUT/queue-state
SUMMARY=$OUT/queue-summary.log
mkdir -p "$OUT"

TC=8+0.08
HASH=16
CONCURRENCY=8        # physical cores only: SMT siblings distort timing at 8+0.08
BOOK=books/UHO_Lichess_4852_v1.epd
FASTCHESS=./tools/fastchess.exe

# name | dev | base | elo0 | elo1 | extra args for dev only
#
# Order is deliberate.  syzygy first because it was 72% of the way to a verdict
# when it was stopped and is the most likely to pay; repetition next because the
# syzygy result pushed games INTO the bucket it addresses (three-fold draws from
# a winning position went 69 -> 88) and because it is the one that can lose --
# it searches ~40% more nodes where it applies, so learn that before banking a
# free win.  A test where both sides are the same binary and only an option
# differs is the cleanest comparison there is: nothing else can explain it.
QUEUE=(
  "syzygy|rogatia-tb|rogatia-tb|0|5|option.SyzygyPath=C:/Users/minhp/syzygy/3-4-5"
  "repetition|rogatia-rep|rogatia-base|0|5|"
  "rule50|rogatia-r50|rogatia-base|0|5|"
)

log() { printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" | tee -a "$SUMMARY"; }

done_already() { [ -f "$STATE" ] && grep -qxF "$1" "$STATE"; }

# Never start a second match beside a running one -- two matches on one box
# distort each other's timing and both results become worthless.
wait_for_free_machine() {
    local announced=0
    while pgrep -f fastchess >/dev/null 2>&1 || tasklist 2>/dev/null | grep -qi fastchess; do
        [ $announced -eq 0 ] && { log "a match is already running; waiting for the machine"; announced=1; }
        sleep 60
    done
    [ $announced -eq 1 ] && log "machine free"
    return 0
}

log '================ queue start ================'

for entry in "${QUEUE[@]}"; do
    IFS='|' read -r name dev base elo0 elo1 extra <<< "$entry"

    if done_already "$name"; then
        log "$name : already finished, skipping"
        continue
    fi
    if [ ! -f "./$dev.exe" ] || [ ! -f "./$base.exe" ]; then
        log "$name : SKIPPED -- missing $dev.exe or $base.exe. Build and gate it, then rerun."
        continue
    fi

    wait_for_free_machine

    log "$name : starting -- $dev vs $base, tc=$TC, bounds [$elo0, $elo1]"

    started=0
    for attempt in 1 2; do
        # Retry once.  A freshly built .exe is scanned on its first execution,
        # and sixteen launching at once can blow fastchess's uciok deadline.
        # The second attempt runs against warm binaries.
        # shellcheck disable=SC2086
        $FASTCHESS \
            -engine cmd="./$dev" name=dev $extra \
            -engine cmd="./$base" name=base \
            -each tc="$TC" option.Hash="$HASH" option.Threads=1 \
            -openings file="$BOOK" format=epd order=random \
            -sprt elo0="$elo0" elo1="$elo1" alpha=0.05 beta=0.05 model=normalized \
            -rounds 100000 -games 2 -repeat \
            -concurrency "$CONCURRENCY" -recover \
            -pgnout file="$OUT/$name.pgn" > "$OUT/$name.log" 2>&1

        started=$(grep -c 'Started game' "$OUT/$name.log" 2>/dev/null || echo 0)
        [ "$started" -gt 8 ] && break
        [ $attempt -eq 1 ] && {
            log "$name : only $started games started -- retrying once with warm binaries"
            sleep 20
        }
    done

    # A run that barely started did not fail to reach a verdict, it failed to
    # run.  Recording it as done would silently drop the test from the queue.
    if [ "$started" -le 8 ]; then
        log "$name : FAILED TO START -- $started games, NOT marked done"
        log "$name :   $(grep -iE 'fatal|error' "$OUT/$name.log" 2>/dev/null | head -1)"
        continue
    fi

    verdict=$(grep -E 'SPRT .* completed' "$OUT/$name.log" 2>/dev/null | tail -1)
    log "$name : finished -- ${verdict:-no SPRT verdict line (stopped early?)}"
    log "$name :   $(grep -E '^Games:' "$OUT/$name.log" 2>/dev/null | tail -1)"
    log "$name :   $(grep -E '^Elo:'   "$OUT/$name.log" 2>/dev/null | tail -1)"

    echo "$name" >> "$STATE"
done

log '================ queue drained ================'
