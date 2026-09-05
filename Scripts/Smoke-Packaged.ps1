<#
.SYNOPSIS
Boots the packaged build and proves it is actually playable.

.DESCRIPTION
Injects real movement input through Enhanced Input and checks the player
character moved. This is the check that catches "the world renders perfectly and
no input is bound", which is what a missing DefaultInputComponentClass causes.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1440)]
    [int] $TimeoutMinutes = 10,
    [double] $Delay = 12.0,
    [string] $PackagedExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
$log = Join-Path $logDir 'SmokePackaged.log'
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }

$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', '-ResX=1280', '-ResY=720',
    '-unattended', '-nosplash', '-nopause', '-nosound',
    '-UEGT2SmokeWalk', "-UEGT2CaptureDelay=$Delay",
    "-abslog=`"$log`""
)

Write-Host "Running packaged smoke: $($exe.FullName)"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir 'SmokePackaged.stdout.log')
# Retain the handle so Windows PowerShell can read ExitCode after redirection.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    Stop-Process -Id $process.Id -Force
    throw "Packaged smoke did not finish within $TimeoutMinutes minutes. Inspect $log."
}
if ($process.ExitCode -ne 0) {
    throw "Packaged smoke failed with exit code $($process.ExitCode). Inspect $log."
}

if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
    throw "Packaged smoke produced no log at $log."
}
$text = Get-Content -LiteralPath $log -Raw

Select-String -LiteralPath $log -Pattern 'LogUEGT2' |
    Select-Object -Last 14 |
    ForEach-Object { Write-Host ("  " + ($_.Line -replace '^\[[^\]]+\]\[\s*\d+\]', '')) }

$problems = @()
if ($text -match 'UEGT2_SMOKE_WALK_FAILED') { $problems += 'the player did not move (input is not bound)' }
if (-not ($text -match 'UEGT2_SMOKE_WALK_COMPLETE')) { $problems += 'the smoke never completed' }
if ($text -match 'Enhanced Input component missing') { $problems += 'Enhanced Input component missing' }
if ($text -match 'invalid ShaderMap') { $problems += 'a material has no valid shader map' }

if ($problems.Count -gt 0) {
    throw ("Packaged smoke FAILED: " + ($problems -join '; ') + ". Log: $log")
}

Write-Host 'Packaged smoke passed: the build boots, renders, and the player walks.' -ForegroundColor Green
