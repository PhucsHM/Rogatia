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

# Sourced only for TB_OPT -- the Syzygy option every match script must pass.
# The settings below deliberately override lib.sh's: this queue pins its own
# time control and concurrency and does not want the shared defaults.
. "$(dirname "$0")/lib.sh"

OUT=sprt-results
STATE=$OUT/queue-state
SUMMARY=$OUT/queue-summary.log
mkdir -p "$OUT"

TC=8+0.08
HASH=16
# Physical cores only: SMT siblings distort timing at 8+0.08.  Overridable,
# because the two boxes differ -- the laptop has 10 physical cores and the home
# box 8, so a hardcoded 8 oversubscribes the smaller one.
CONCURRENCY=${CONCURRENCY:-8}
BOOK=books/UHO_Lichess_4852_v1.epd
# FASTCHESS comes from lib.sh, which picks .exe only when one exists. This used
# to be hardcoded to tools/fastchess.exe, which cannot run on the Linux box.

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
# answer that [0, 5] would not give.  Retunes now: capthist and rule50.
#
# ORDER IS BY MEASURED ELO PER MACHINE-HOUR, not by the roadmap.  Research on
# 2026-07-29 established that the techniques left in this phase are worth +2 to
# +5 each and that the reference SPRTs needed 15,000 to 130,000 games apiece --
# so the order has to put the biggest expected effects first, because those are
# the only ones one machine can actually resolve.
#
# dodeeper leads: Alexandria measured +8.68 +/- 5.41 for it and this engine did
# not have it at all.  dblext is second because its negative extensions shrink
# the tree 13.6% at equal depth, which is the half that pays for singular
# extensions' +28% cost.
#
# NOT queued, deliberately:
#   ttpv     parked -- the exemption costs 12-58% more nodes here depending on
#            constants, and the flat form already measured ~0.  Stockfish pays
#            for it with a compensating term this search does not have.
#   checkext no modern engine keeps check extensions; Stormphrax has a commit
#            removing them.  Needs rewriting as an LMR term plus an SEE-gated
#            ordering bonus before it is worth a slot.
#   probcut  Berserk measured -0.15 +/- 4.40 at 8+0.08 and +4.34 +/- 3.00 at
#            40+0.4.  It is an LTC feature; run it at the phase gate instead.
QUEUE=(
  "dodeeper|rogatia-dd|rogatia-base|0|5|"        # +8.68 measured, best Elo/hour we lack
  "rule50tt|rogatia-r50tt|rogatia-base|0|5|"     # TT guard ONLY, taper split off
  "dblext|rogatia-dblx2|rogatia-base|0|5|"       # negative extensions: 13.6% smaller tree
  "hygiene|rogatia-hyg|rogatia-base|0|5|"        # four defect fixes, bundled
  "histage|rogatia-age2|rogatia-base|0|5|"       # ~+5 measured (Berserk)
  "timeman|rogatia-tm3|rogatia-base|0|5|"        # node fraction + best-move stability
  "conthist|rogatia-ch6b|rogatia-base|0|5|"      # +2.81 measured
  "capthist|rogatia-capt3|rogatia-base|0|3|"     # retune: +2.80 measured, needs the tight band
  "corrplexity|rogatia-cplx2|rogatia-base|0|5|"  # merged in Stockfish at STC and LTC
)

# rule50c was SPLIT on 2026-07-29 and only half of it is queued.
#
# It bundled two unrelated changes. The eval taper (`v - v*rule50/199`) fires
# from counter 0, so at an ordinary counter of 20 it is a ~10% haircut on every
# score -- and all 33 margins in tunable.h are calibrated to the undamped scale,
# so it silently re-tunes the whole search's pruning rather than fixing anything
# about the fifty-move rule. The TT guard is independent and has a clean
# mechanism: the fifty-move counter is not in the Zobrist key, so one entry
# serves the same position at counter 3 and at counter 93, and near the draw
# those are not the same position.
#
# Bundled, a null result at [0, 3] costs 30,000 games and cannot say which half
# was wrong. The guard alone is a real question, so it gets [0, 5]. The taper
# stays parked on `phase7-rule50c`.

# ---- half selection: two machines, one queue file -----------------------
#
# The boxes must never run the SAME test -- fastchess has no distributed mode
# and merging two PGN sets breaks SPRT's sequential stopping rule. Split the
# list instead and let each machine drain its own half.
#
#   HALF=a bash scripts/testqueue.sh     # first half
#   HALF=b bash scripts/testqueue.sh     # second half
#   bash scripts/testqueue.sh            # everything, one machine
HALF=${HALF:-}
if [ -n "$HALF" ]; then
    _mid=$(( (${#QUEUE[@]} + 1) / 2 ))
    case "$HALF" in
        a) QUEUE=("${QUEUE[@]:0:$_mid}") ;;
        b) QUEUE=("${QUEUE[@]:$_mid}") ;;
        *) echo "HALF must be 'a' or 'b'" >&2; exit 2 ;;
    esac
    STATE=$OUT/queue-state-$HALF
    SUMMARY=$OUT/queue-summary-$HALF.log
fi

log() { printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" | tee -a "$SUMMARY"; }

done_already() { [ -f "$STATE" ] && grep -qxF "$1" "$STATE"; }

# Never start a second match beside a running one -- two matches on one box
# distort each other's timing and both results become worthless.
wait_for_free_machine() {
    local announced=0
    # pgrep -x, matching the PROCESS NAME, never -f.  -f matches the whole
    # command line, so any shell that merely MENTIONS fastchess counts as a
    # running match -- including an ssh session checking on this very queue.
    # That is not hypothetical: it deadlocked half A here on 2026-07-29, and the
    # queue sat waiting for a machine that was already free.
    while pgrep -x fastchess >/dev/null 2>&1 || tasklist 2>/dev/null | grep -qi 'fastchess\.exe'; do
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
    # bin() picks the .exe only where one exists, so this runs on both boxes.
    # The check used to be hardcoded to "$dev.exe", which on Linux is never
    # there -- every test would have logged SKIPPED and the queue would have
    # drained in seconds having run nothing.
    devbin=$(bin "./$dev")
    basebin=$(bin "./$base")
    if [ ! -x "$devbin" ] || [ ! -x "$basebin" ]; then
        log "$name : SKIPPED -- missing $devbin or $basebin. Build and gate it, then rerun."
        continue
    fi

    wait_for_free_machine

    # Both binaries, every test. A patch measured against a base that failed to
    # load tablebases is measuring the tablebases, not the patch.
    if ! verify_tb "$devbin" || ! verify_tb "$basebin"; then
        log "$name : SKIPPED -- tablebases did not load; fix that before testing"
        continue
    fi

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
    # 2.8x more games than [0,5].
    #
    # RAISED 4,000 -> 20,000 and 12,000 -> 30,000 on 2026-07-29.  The old limits
    # were manufacturing false negatives.  The reference SPRTs for the techniques
    # in this queue needed 15,000 to 130,000 games; capture history was recorded
    # here as "rejected twice" on runs of 400 and about 1,500 games, and both
    # were null results, not rejections.  A stall limit below the sample size the
    # effect needs does not save machine time, it spends it on an answer that
    # cannot be right.
    #
    # 20,000 games is ~18 hours at ~1,100 games/hour and 30,000 is ~27.  That is
    # the honest ceiling for one machine, and it is still short of what a +2
    # patch needs.  That gap is what OpenBench exists to close.  See
    # docs/TESTING.md.
    if [ "$elo1" = "3" ]; then STALL_GAMES=30000; else STALL_GAMES=20000; fi
    STALL_LLR=0.6
    # Take a resume snapshot, but NEVER overwrite a good one with a bad one.
    #
    # fastchess rewrites config.json continuously while the match runs, so a
    # copy taken mid-write is truncated JSON.  The old code copied straight over
    # the snapshot, which meant an unlucky moment destroyed the only record of
    # the games played -- and the resume would silently start from zero.  That
    # is the exact failure that cost a whole syzygy run on 2026-07-28.
    #
    # Copy to a temp, check it, and only then promote.  The check is the last
    # NON-WHITESPACE byte being `}`: no dependency on python or jq, and a
    # truncated write essentially never ends that way.
    #
    # The whitespace strip is load-bearing.  fastchess writes config.json with
    # CRLF line endings, so the file ends `}\r\n` -- and `$(tail -c 1 ...)`
    # returns the newline, which command substitution then strips to the empty
    # string.  Comparing that against `}` fails for EVERY file, valid or not, so
    # the first version of this guard silently rejected every snapshot and
    # turned the resume mechanism off completely.  It looked like it was
    # working: the snapshot file was still there, just never updated.
    snapshot() {
        [ -f config.json ] || return 0
        cp -f config.json "$resume.tmp" 2>/dev/null || return 0
        if [ "$(tail -c 64 "$resume.tmp" 2>/dev/null | tr -d '[:space:]' | tail -c 1)" = "}" ]; then
            mv -f "$resume.tmp" "$resume" 2>/dev/null
        else
            rm -f "$resume.tmp" 2>/dev/null
        fi
    }

    monitor() {
        local pid=$1 lf=$2       # not `log`: that is the function name
        local tick=0
        while kill -0 "$pid" 2>/dev/null; do
            # Every 15s, not every 60s.  This interval IS the cost of stopping:
            # whatever has been played since the last good snapshot is lost, and
            # the laptop gets carried around mid-queue.
            snapshot
            sleep 15
            tick=$((tick + 1))
            [ $((tick % 4)) -eq 0 ] || continue
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
            "$FASTCHESS" -config file=config.json >> "$OUT/$name.log" 2>&1 &
        else
            # shellcheck disable=SC2086
            "$FASTCHESS" \
                -engine cmd="$devbin" name=dev $extra \
                -engine cmd="$basebin" name=base \
                -each tc="$TC" option.Hash="$HASH" option.Threads=1 $TB_OPT \
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

        # One last snapshot after the match ends. The monitor died with it, so
        # without this the resume file is up to 15 seconds stale -- and if the
        # match ended because someone stopped it before moving the machine,
        # those are exactly the games worth keeping.
        snapshot

        # NO `|| echo 0` here. grep -c already prints 0 when it matches
        # nothing, and EXITS 1 doing so -- so the fallback fired too and the
        # variable became a TWO-LINE string, zero then zero. Every later [ -le ]
        # test then failed with "integer expected", including the guard that
        # stops a test being recorded as done -- so a queue that ran no games
        # at all marked all five finished and drained in twenty seconds.
        started=$(grep -c 'Started game' "$OUT/$name.log" 2>/dev/null)
        started=${started:-0}
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
