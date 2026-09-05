<#
.SYNOPSIS
Runs Fairhaven headlessly and captures a PNG at each tour viewpoint.

.DESCRIPTION
This is the visual regression check for the world. Screenshots land in
Saved/Screenshots/Tour by default. The viewpoint list lives in
Source/UEGT2/Private/Diagnostics/UEGT2CaptureSubsystem.cpp.

.EXAMPLE
./Scripts/Screenshot-Tour.ps1
./Scripts/Screenshot-Tour.ps1 -Only TownSquare -ResX 2560 -ResY 1440
./Scripts/Screenshot-Tour.ps1 -Menu -OutputDirectory Saved/Screenshots/Menu
./Scripts/Screenshot-Tour.ps1 -ExtraArgs '-UEGT2Time=22.5 -UEGT2Weather=storm'
#>
[CmdletBinding()]
param(
    [string] $OutputDirectory,
    [string] $Only,
    [int] $ResX = 1920,
    [int] $ResY = 1080,
    [double] $Delay = 7.0,
    [double] $Hold = 1.8,
    [ValidateRange(1, 1440)]
    [int] $TimeoutMinutes = 20,
    [switch] $Menu,
    # Passed straight through to the game. -UEGT2Time=<hours> and
    # -UEGT2Weather=<name> render a specific sky; both freeze the day/night
    # cycle so the shot stays reproducible.
    [string] $ExtraArgs,
    [string] $PackagedExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot

# Use the packaged build. An uncooked game binary cannot start, because the
# global shader library only exists after a cook.
# Prefer the real binary under Binaries\Win64 over the staged launcher stub in
# the archive root: Windows Application Control blocks the stub.
if ($PackagedExecutable) {
    $resolvedExecutable = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PackagedExecutable)
    if (-not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf) -or
        $resolvedExecutable -notlike '*\Binaries\Win64\UEGT2.exe') {
        throw 'PackagedExecutable must name the real Binaries\Win64\UEGT2.exe of a packaged build.'
    }
    $gameExe = (Get-Item -LiteralPath $resolvedExecutable).FullName
} else {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'LocalBuilds') -Recurse -File `
        -Filter 'UEGT2.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\Binaries\Win64\UEGT2.exe' })
    if ($candidates.Count -eq 0) {
        throw 'No packaged build found. Run ./Scripts/Package.ps1 first.'
    }
    if ($candidates.Count -ne 1) {
        throw 'Multiple packaged builds found. Select one with -PackagedExecutable.'
    }
    $gameExe = $candidates[0].FullName
}
Write-Host "Using packaged build: $gameExe"

if (-not $OutputDirectory) {
    $folder = if ($Menu) { 'Menu' } else { 'Tour' }
    $OutputDirectory = Join-Path $projectRoot "Saved\Screenshots\$folder"
}
# Relative paths follow PowerShell's current location, which can differ from
# the process working directory after Set-Location.
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
# A caller may choose a directory containing other work. Clear only the files
# named by our capture subsystem, and count the same set after this run.
$captureFilePattern = '^(?:\d{2,}|menu_\d{2,}|talk_\d{2,}|life_\d{2,})_.+\.png$'
Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.png' -File |
    Where-Object { $_.Name -match $captureFilePattern } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

$logDir = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir 'ScreenshotTour.log'
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }

# No .uproject argument: passing one makes a packaged build try to load
# uncooked project content through a Zen storage server and fail to start.
$arguments = @(
    '-RenderOffscreen',
    '-Windowed',
    # -ForceRes is required: without it the offscreen window falls back to a
    # smaller size and the screenshots come out at the wrong resolution.
    '-ForceRes',
    "-ResX=$ResX",
    "-ResY=$ResY",
    '-unattended', '-nosplash', '-nopause', '-nosound',
    "-UEGT2Capture=`"$($OutputDirectory.Replace('\','/'))`"",
    "-UEGT2CaptureDelay=$Delay",
    "-UEGT2CaptureHold=$Hold",
    "-abslog=`"$log`""
)
if ($Only) { $arguments += "-UEGT2CaptureOnly=$Only" }
if ($Menu) { $arguments += '-UEGT2CaptureMenu' }
if ($ExtraArgs) { $arguments += ($ExtraArgs -split '\s+' | Where-Object { $_ }) }

Write-Host "Capturing tour at ${ResX}x${ResY} -> $OutputDirectory"
$process = Start-Process -FilePath $gameExe -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir 'ScreenshotTour.stdout.log') `
    -RedirectStandardError (Join-Path $logDir 'ScreenshotTour.stderr.log')
# Retain the handle so Windows PowerShell can read ExitCode after redirection.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    Stop-Process -Id $process.Id -Force
    throw "Screenshot tour exceeded $TimeoutMinutes minutes. Inspect $log."
}
if ($process.ExitCode -ne 0) {
    throw "Screenshot tour failed with exit code $($process.ExitCode). Inspect $log."
}

if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
    throw "Screenshot tour produced no log at $log."
}
Select-String -LiteralPath $log -Pattern 'LogUEGT2Diag|LogUEGT2:|Error:' |
    Select-Object -Last 30 |
    ForEach-Object { Write-Host ("  " + ($_.Line -replace '^\[[^\]]+\]\[\s*\d+\]', '')) }
$text = Get-Content -LiteralPath $log -Raw
if ($text -notmatch 'UEGT2_CAPTURE_TOUR_COMPLETE' -or
    $text -match 'LogUEGT2Diag: Error:|Critical error|Assertion failed|Amenity capture \d+:.*(?:NOT FOUND BY THE PROBE|NOT USED)|Amenity capture: no .+ anywhere') {
    throw "Screenshot tour failed or did not complete. Inspect $log."
}

# FinishTour is also used on early exits, so the marker alone is insufficient.
# The mode-specific plans give the expected output count (dialogue has opening
# and answered states); a missed screenshot callback must fail validation too.
$expected = 0
if ($text -match 'Amenity capture: (\d+) stops') { $expected = [int] $Matches[1] }
elseif ($text -match 'Dialogue capture: talking to') { $expected = 2 }
elseif ($text -match 'Menu capture: (\d+) screens') { $expected = [int] $Matches[1] }
elseif ($text -match 'Capture tour requested: (\d+) viewpoints') { $expected = [int] $Matches[1] }

$shots = @(Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.png' -File |
    Where-Object { $_.Name -match $captureFilePattern })
if ($expected -lt 1 -or $shots.Count -ne $expected -or @($shots | Where-Object { $_.Length -eq 0 }).Count -gt 0) {
    throw "Screenshot tour produced $($shots.Count) screenshots; expected $expected complete images. Inspect $log."
}
Write-Host "Captured $($shots.Count) screenshots in $OutputDirectory"
$shots | ForEach-Object { Write-Host ("  {0}  ({1:N0} KB)" -f $_.Name, ($_.Length / 1KB)) }
