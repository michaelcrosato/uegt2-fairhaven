<#
.SYNOPSIS
Flies god mode around both settlements for several minutes and reports stalls.

.DESCRIPTION
The soak test for the things a screenshot cannot see. It turns on dev mode, god
mode and flight exactly as a player would, flies a circuit of the inhabited
viewpoints, and logs every second: the worst frame in that second, the object
count, the process memory, the world hour and how many inhabitants are near.

This exists because of a real bug it found. A* held a pointer into the cost map
across a write to that map; the dangling read let the parent links form a cycle,
and the walk back from the goal then allocated four gigabytes in eight seconds
before the allocator asserted. The game froze for twenty seconds and died. Every
automation test passed, the world built, the screenshots were clean - the only
way to see it was to fly for two minutes.

Flying is the trigger rather than a coincidence: it crosses the map fast enough
to take hundreds of inhabitants from Dormant to active in one pass, and every
one of them repaths on the spot.

.EXAMPLE
./Scripts/Fly-Soak.ps1
./Scripts/Fly-Soak.ps1 -Minutes 15 -Speed 20
./Scripts/Fly-Soak.ps1 -Collide          # keep collision on instead of noclip
#>
[CmdletBinding()]
param(
    [double] $Minutes = 8.0,
    [double] $Speed = 8.0,
    [double] $Altitude = 4000.0,
    [int] $ResX = 1920,
    [int] $ResY = 1080,
    [switch] $Collide,
    [ValidateRange(1, 1440)]
    [int] $TimeoutMinutes = 30,
    [string] $PackagedExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([double]::IsNaN($Minutes) -or [double]::IsInfinity($Minutes) -or $Minutes -le 0.0) {
    throw 'Minutes must be a finite positive number.'
}

$projectRoot = Split-Path -Parent $PSScriptRoot

if ($PackagedExecutable) {
    $resolvedExecutable = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PackagedExecutable)
    if (-not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf) -or
        $resolvedExecutable -notlike '*\Binaries\Win64\UEGT2.exe') {
        throw 'PackagedExecutable must name the real Binaries\Win64\UEGT2.exe of a packaged build.'
    }
    $exe = Get-Item -LiteralPath $resolvedExecutable
} else {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'LocalBuilds') -Recurse -File `
        -Filter 'UEGT2.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\Binaries\Win64\UEGT2.exe' })
    if ($candidates.Count -eq 0) { throw 'No packaged build found. Run ./Scripts/Package.ps1 first.' }
    if ($candidates.Count -ne 1) {
        throw 'Multiple packaged builds found. Select one with -PackagedExecutable.'
    }
    $exe = $candidates[0]
}

$logDir = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir 'FlySoak.log'
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }

$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$ResX", "-ResY=$ResY",
    '-unattended', '-nosplash', '-nopause',
    '-UEGT2SmokeFly', "-UEGT2SmokeMinutes=$Minutes", "-UEGT2SmokeSpeed=$Speed",
    "-UEGT2SmokeAltitude=$Altitude", '-UEGT2CaptureDelay=8',
    "-abslog=`"$log`""
)
if ($Collide) { $arguments += '-UEGT2SmokeCollide' }

Write-Host "Flying $Minutes minutes at ${Speed}x: $($exe.FullName)"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    Stop-Process -Id $process.Id -Force
    throw "Fly soak did not finish within $TimeoutMinutes minutes. Inspect $log."
}
if ($process.ExitCode -ne 0) {
    throw "Fly soak failed with exit code $($process.ExitCode). Inspect $log."
}

if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
    throw "Fly soak produced no log at $log."
}
$text = Get-Content -LiteralPath $log -Raw

# The per-second lines are the interesting output; print the stalls and the
# summary rather than four hundred healthy ones.
Select-String -LiteralPath $log -Pattern 'UEGT2_FLY_STALL|UEGT2_FLY_SOAK|stuck at|parent links cycle' |
    ForEach-Object { Write-Host ("  " + ($_.Line -replace '^\[[^\]]+\]\[\s*\d+\]', '')) }

$problems = @()
if ($text -match 'UEGT2_FLY_SOAK_FAILED') { $problems += 'the soak could not start' }
if (-not ($text -match 'UEGT2_FLY_SOAK_COMPLETE')) { $problems += 'the soak never completed - it hung or crashed' }
if ($text -match 'Hang detected') { $problems += 'a thread stopped answering' }
if ($text -match 'Critical error|Assertion failed') { $problems += 'the build crashed' }
if ($text -match 'parent links cycle') { $problems += 'the route network produced a parent-link cycle' }

if ($problems.Count -gt 0) {
    throw ("Fly soak FAILED: " + ($problems -join '; ') + ". Log: $log")
}

$stalls = ([regex]::Matches($text, 'UEGT2_FLY_STALL')).Count
Write-Host "Fly soak passed: flew $Minutes minutes with $stalls stalls. Log: $log" -ForegroundColor Green
