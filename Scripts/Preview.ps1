<#
.SYNOPSIS
The visual iteration loop: build code, rebuild content, package, capture a tour.

.DESCRIPTION
This is the fastest way to see the effect of a change. Skip stages you did not
touch to keep it quick.

.EXAMPLE
./Scripts/Preview.ps1                                   # everything
./Scripts/Preview.ps1 -Stages lighting -SkipCode        # lighting tweak only
./Scripts/Preview.ps1 -Only TownSquare                  # one viewpoint
#>
[CmdletBinding()]
param(
    [string[]] $Stages = @('all'),
    [switch] $SkipCode,
    [switch] $SkipContent,
    [switch] $SkipPackage,
    [string] $Only,
    [int] $ResX = 1920,
    [int] $ResY = 1080,
    [double] $Delay = 12.0,
    [double] $Hold = 2.0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot

if (-not $SkipCode) {
    & (Join-Path $root 'Build.ps1') -Target Both -Configuration Development |
        Select-String -Pattern 'Build succeeded|error' | ForEach-Object { Write-Host $_ }
}
if (-not $SkipContent) {
    & (Join-Path $root 'Build-Content.ps1') -Stages $Stages
}
if (-not $SkipPackage) {
    & (Join-Path $root 'Package.ps1') -Configuration Development | Select-Object -Last 2
}

$tourArgs = @{ ResX = $ResX; ResY = $ResY; Delay = $Delay; Hold = $Hold }
if ($Only) { $tourArgs['Only'] = $Only }
# Package.ps1 writes the selected configuration to this stable archive path.
# Pass it explicitly because LocalBuilds may also contain older archives.
$tourArgs['PackagedExecutable'] = Join-Path $root '..\LocalBuilds\Windows-Development\UEGT2\Binaries\Win64\UEGT2.exe'
& (Join-Path $root 'Screenshot-Tour.ps1') @tourArgs | Select-Object -Last 16
