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
# **Bounds are per test, not global.**  A first test of a new technique uses
# [0, 5]; a retune of a patch that already failed uses [0, 3].
#
# The reason is what each test is asking.  A new technique can plausibly be
# worth 10-40 Elo, and [0, 5] resolves that in ~2-3 hours.  A retune is asking a
# narrower question -- the patch already measured at or below zero once, so the
# honest expectation is a few Elo, which sits in the middle of [0, 5] where SPRT
# is slowest and closest to a coin flip.  [0, 3] makes that same patch the design
# point and accepts H1 about 95% of the time.
#
# [0, 3] costs roughly 2.8x more games, so it is spent only where it buys an
# answer that [0, 5] would not give.  Retunes now: capthist (rejected twice),
# rule50b (rejected at threshold 20).
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
  "ttpv|rogatia-ttpv|rogatia-base|0|5|"
  "checkext|rogatia-chkext|rogatia-base|0|5|"
  "corrplexity|rogatia-cplx|rogatia-base|0|5|"
  "capthist|rogatia-capt|rogatia-base|0|3|"     # retune: rejected twice
  "rule50b|rogatia-r50b|rogatia-base|0|3|"      # retune: rejected at threshold 20
  "conthist|rogatia-ch6|rogatia-base|0|5|"
  "histage|rogatia-age|rogatia-base|0|5|"
  "dblext|rogatia-dblx|rogatia-base|0|5|"
  "probcut|rogatia-pc|rogatia-base|0|5|"
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

    # fastchess writes ./config.json continuously, carrying the full running
    # state -- W/L/D and the pentanomial pair counts -- and can restart from it
    # with `-config file=...`.  That is the difference between a stop costing
    # twenty seconds and costing the whole test: on 2026-07-28 a syzygy run was
    # killed at 2,322 games and restarted from zero for exactly this reason.
    #
    # It is one shared file in the repo root, so the next test overwrites it.
    # Snapshot it under this test's name while the match runs; only one match
    # runs at a time, so the file always belongs to the current test.
    resume=$OUT/$name.config.json

    # Snapshot the resume state, and abandon a test that will never finish.
    #
    # An SPRT concludes when the LLR reaches +-2.94.  It does that quickly when
    # the true effect is outside the bounds and never when it sits between them:
    # the repetition patch reached 3,260 games at LLR 0.08, which projects to
    # roughly 120,000 games -- 135 hours of the only test machine -- for a
    # number already known to be about +2.  Past STALL_GAMES with the LLR still
    # inside +-STALL_LLR, the answer is "too small to resolve at these bounds",
    # and that answer is already in hand.  Stop and keep the machine.
    #
    # Deliberately NOT an early abort on a losing result: SPRT rejects a real
    # loss fast on its own, and second-guessing it would throw away verdicts
    # that were about to arrive.
    # The limit follows the bounds, because the two cannot be set apart.
    #
    # Games to a verdict scale about 1/(elo1 - elo0)^2, so [0,3] costs roughly
    # 2.8x more games than [0,5].  A [0,3] test under a 4,000-game limit aborts
    # before its LLR can leave +-0.6, which parks the exact 2-4 Elo patches the
    # tighter bounds exist to resolve.  A [0,5] test under a 12,000-game limit
    # burns ~7 extra hours on a question already answered.
    #
    # 4,000 games is ~3.5 hours at ~1,100 games/hour; 12,000 is ~11 hours.  That
    # 11 hours is the honest ceiling for one machine -- resolving a +2 patch
    # reliably needs more games than this box supplies, and that is what
    # OpenBench is for.  See docs/TESTING.md.
    if [ "$elo1" = "3" ]; then STALL_GAMES=12000; else STALL_GAMES=4000; fi
    STALL_LLR=0.6
    monitor() {
        local pid=$1 lf=$2       # not `log`: that is the function name
        while kill -0 "$pid" 2>/dev/null; do
            [ -f config.json ] && cp -f config.json "$resume" 2>/dev/null
            sleep 60
            local g llr
            g=$(grep -E '^Games:' "$lf" 2>/dev/null | tail -1 | grep -oE '[0-9]+' | head -1)
            llr=$(grep -E '^LLR:' "$lf" 2>/dev/null | tail -1 | awk '{print $2}')
            if [ -z "$g" ] || [ -z "$llr" ]; then continue; fi
            if [ "$g" -ge "$STALL_GAMES" ] \
               && awk -v l="$llr" -v t="$STALL_LLR" 'BEGIN{exit !(l<t && l>-t)}'; then
                log "$name : STALLED -- $g games, LLR $llr still inside +-$STALL_LLR."
                log "$name :   Effect is too small to resolve at these bounds; stopping."
                cp -f config.json "$OUT/$name-STALLED-$g.config.json" 2>/dev/null
                kill "$pid" 2>/dev/null
                return
            fi
        done
    }

    started=0
    for attempt in 1 2; do
        # Retry once.  A freshly built .exe is scanned on its first execution,
        # and sixteen launching at once can blow fastchess's uciok deadline.
        # The second attempt runs against warm binaries.
        if [ -f "$resume" ]; then
            log "$name : resuming from $(basename "$resume") -- earlier games are kept"
            cp -f "$resume" config.json
            $FASTCHESS -config file=config.json >> "$OUT/$name.log" 2>&1 &
        else
            # shellcheck disable=SC2086
            $FASTCHESS \
                -engine cmd="./$dev" name=dev $extra \
                -engine cmd="./$base" name=base \
                -each tc="$TC" option.Hash="$HASH" option.Threads=1 \
                -openings file="$BOOK" format=epd order=random \
                -sprt elo0="$elo0" elo1="$elo1" alpha=0.05 beta=0.05 model=normalized \
                -rounds 100000 -games 2 -repeat \
                -concurrency "$CONCURRENCY" -recover \
                -pgnout file="$OUT/$name.pgn" > "$OUT/$name.log" 2>&1 &
        fi
        fc_pid=$!
        monitor "$fc_pid" "$OUT/$name.log" &
        mon_pid=$!
        wait "$fc_pid"
        kill "$mon_pid" 2>/dev/null

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
    rm -f "$resume"          # finished: nothing left to resume from
done

log '================ queue drained ================'
