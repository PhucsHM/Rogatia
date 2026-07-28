# Rogatia -- run queued SPRTs one after another, unattended.
#
# One box means one test at a time and a verdict takes hours, so the queue has
# to outlive the session that started it -- closing the terminal or losing the
# agent connection must not stop it.  Start it detached with
# scripts/queue-start.ps1 and it keeps going.
#
#   powershell -ExecutionPolicy Bypass -File scripts/testqueue.ps1
#
# It does NOT try to be clever about the lid.  Closing the lid puts the machine
# to sleep, deliberately: eight engines at full load inside a closed laptop is a
# thermal problem, and sleeping is the safe failure.  Idle sleep is disabled, so
# a test with the lid open runs for as long as it needs.  Stop the queue by hand
# before carrying the machine anywhere -- a suspend mid-match costs the games in
# flight, which the gap watch below records so a bad verdict is never mistaken
# for a clean one.
#
# Binaries are NOT built here.  Building needs a POSIX shell for the Makefile,
# and a build failure four hours in is the worst possible time to find out --
# so every binary is built and gated before it reaches the queue, and this
# script only plays games.  A missing binary is logged and skipped, never
# guessed at.
#
# Resumable: sprt-results/queue-state.json records finished tests by name, so a
# reboot mid-queue picks up where it left off instead of re-running a verdict
# that already cost four hours.

$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$Out       = Join-Path $Root 'sprt-results'
$StateFile = Join-Path $Out  'queue-state.json'
$Summary   = Join-Path $Out  'queue-summary.log'
New-Item -ItemType Directory -Force -Path $Out | Out-Null

# ---------------------------------------------------------------- the queue --
# Order matters and it is not arbitrary.  repetition goes first because the
# syzygy result pushed games INTO the bucket it addresses (3-fold draws from a
# winning position went 69 -> 88), and because it is the one carrying real risk:
# it searches ~40% more nodes in the positions it touches, so it can lose.
# Learn that early rather than after a free win has been banked.
$Queue = @(
    @{ Name = 'repetition'; Dev = 'rogatia-rep'; Base = 'rogatia-base'; Elo0 = 0; Elo1 = 5 }
    @{ Name = 'rule50';     Dev = 'rogatia-r50'; Base = 'rogatia-base'; Elo0 = 0; Elo1 = 5 }
)

$TC          = '8+0.08'
$Hash        = 16
$Concurrency = 8          # physical cores only: SMT siblings distort timing at 8+0.08
$Book        = 'books/UHO_Lichess_4852_v1.epd'
$FastChess   = 'tools/fastchess.exe'

function Log($msg) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Write-Output $line
    Add-Content -Path $Summary -Value $line -Encoding utf8
}

# Keep the machine awake for as long as this script lives.  The power scheme is
# already set to ignore the lid, but this also covers a scheme change or a
# different profile becoming active.  ES_CONTINUOUS | ES_SYSTEM_REQUIRED.
try {
    Add-Type -Name Power -Namespace Win32 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
    # 0x80000001, written as a decimal literal on purpose: PowerShell types
    # 0x80000000 as a signed int, so `0x80000000 -bor 1` is negative and the
    # UInt32 marshal throws.
    [Win32.Power]::SetThreadExecutionState([uint32]2147483649) | Out-Null
    Log 'keep-awake requested (ES_CONTINUOUS | ES_SYSTEM_REQUIRED)'
} catch {
    Log "keep-awake unavailable: $($_.Exception.Message)"
}

function Get-Done {
    if (Test-Path $StateFile) {
        try { return @((Get-Content $StateFile -Raw | ConvertFrom-Json).done) } catch { return @() }
    }
    return @()
}

function Set-Done($names) {
    @{ done = @($names) } | ConvertTo-Json | Set-Content -Path $StateFile -Encoding utf8
}

# Blocks until no fastchess is running, so the queue never starts a second
# match beside one already in progress -- two matches on one box distort each
# other's timing and both results become worthless.
function Wait-ForFreeMachine {
    $announced = $false
    while (Get-Process -Name fastchess -ErrorAction SilentlyContinue) {
        if (-not $announced) {
            Log 'a match is already running; waiting for the machine'
            $announced = $true
        }
        Start-Sleep -Seconds 60
    }
    if ($announced) { Log 'machine free' }
}

# Polls the running match and flags wall-clock jumps.  A suspend/resume makes
# every engine clock jump at once, so the games in flight lose on time through
# no fault of the patch.  A few such games in several thousand is noise, but it
# has to be visible in the log or a bad verdict looks clean.
function Wait-WithGapWatch($proc, $name) {
    $last = Get-Date
    while (-not $proc.HasExited) {
        Start-Sleep -Seconds 30
        $now = Get-Date
        $gap = ($now - $last).TotalSeconds
        if ($gap -gt 150) {
            Log ("$name : WALL-CLOCK GAP of {0:N0}s -- machine likely suspended. Games in flight will have lost on time; treat a marginal verdict with suspicion." -f $gap)
        }
        $last = $now
    }
}

Log '================ queue start ================'

foreach ($t in $Queue) {
    $done = Get-Done
    if ($done -contains $t.Name) {
        Log "$($t.Name) : already finished, skipping"
        continue
    }

    $dev  = Join-Path $Root ($t.Dev  + '.exe')
    $base = Join-Path $Root ($t.Base + '.exe')
    if (-not (Test-Path $dev) -or -not (Test-Path $base)) {
        Log "$($t.Name) : SKIPPED -- missing $($t.Dev).exe or $($t.Base).exe. Build and gate it, then rerun."
        continue
    }

    Wait-ForFreeMachine

    $log = Join-Path $Out "$($t.Name).log"
    $pgn = Join-Path $Out "$($t.Name).pgn"

    Log "$($t.Name) : starting -- $($t.Dev) vs $($t.Base), tc=$TC, bounds [$($t.Elo0), $($t.Elo1)]"

    $args = @(
        '-engine', "cmd=./$($t.Dev)",  'name=dev'
        '-engine', "cmd=./$($t.Base)", 'name=base'
        '-each', "tc=$TC", "option.Hash=$Hash", 'option.Threads=1'
        '-openings', "file=$Book", 'format=epd', 'order=random'
        '-sprt', "elo0=$($t.Elo0)", "elo1=$($t.Elo1)", 'alpha=0.05', 'beta=0.05', 'model=normalized'
        '-rounds', '100000', '-games', '2', '-repeat'
        '-concurrency', $Concurrency, '-recover'
        '-pgnout', "file=$pgn"
    )

    $proc = Start-Process -FilePath (Join-Path $Root $FastChess) -ArgumentList $args `
                          -WorkingDirectory $Root -NoNewWindow -PassThru `
                          -RedirectStandardOutput $log -RedirectStandardError "$log.err"

    Wait-WithGapWatch $proc $t.Name

    $verdict = (Select-String -Path $log -Pattern 'SPRT .* completed' -ErrorAction SilentlyContinue |
                Select-Object -Last 1).Line
    $elo     = (Select-String -Path $log -Pattern '^Elo:' -ErrorAction SilentlyContinue |
                Select-Object -Last 1).Line
    $games   = (Select-String -Path $log -Pattern '^Games:' -ErrorAction SilentlyContinue |
                Select-Object -Last 1).Line

    Log "$($t.Name) : finished -- $(if ($verdict) { $verdict } else { 'no SPRT verdict line (stopped early?)' })"
    if ($games) { Log "$($t.Name) :   $games" }
    if ($elo)   { Log "$($t.Name) :   $elo" }

    Set-Done (@(Get-Done) + $t.Name)
}

Log '================ queue drained ================'
