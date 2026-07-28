# Launch the SPRT queue detached, so it outlives whatever started it.
#
#   powershell -ExecutionPolicy Bypass -File scripts/queue-start.ps1
#
# The queue runs for hours per test.  Started as a child of a terminal or an
# agent session it dies with that session; started this way it is its own
# process and survives the terminal closing, the agent disconnecting, and the
# lid shutting (the power scheme is set to ignore the lid -- see the README
# note in testqueue.ps1).
#
# Safe to run twice: testqueue.ps1 waits for a free machine and skips tests
# already recorded in queue-state.json, so a second copy idles rather than
# starting a competing match.

$Root = Split-Path -Parent $PSScriptRoot
$q    = Join-Path $Root 'scripts\testqueue.ps1'
$log  = Join-Path $Root 'sprt-results\queue-runner.log'

New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null

$p = Start-Process -FilePath 'powershell.exe' `
        -ArgumentList '-ExecutionPolicy','Bypass','-NonInteractive','-File',"`"$q`"" `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err"

"queue running detached as pid $($p.Id)"
"  progress : sprt-results\queue-summary.log"
"  raw      : sprt-results\queue-runner.log"
"  stop it  : Stop-Process -Id $($p.Id)"
