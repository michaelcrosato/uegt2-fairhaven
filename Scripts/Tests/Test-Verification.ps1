<#
.SYNOPSIS
Checks verification scripts against simulated process failures; no Unreal needed.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceScripts = Split-Path -Parent $PSScriptRoot
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $tempRoot ('UEGT2-ScriptTests-' + [guid]::NewGuid().ToString('N'))
$fixture = @{ CaseCount = 0; NativeCaseCount = 0 }

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

function New-Report([string[]] $States = @('Success')) {
    $tests = @($States | ForEach-Object { @{ fullTestPath = "UEGT2.Fixture.$_"; state = $_ } })
    return @{
        succeeded = @($States | Where-Object { $_ -eq 'Success' }).Count
        succeededWithWarnings = 0
        failed = @($States | Where-Object { $_ -eq 'Fail' }).Count
        notRun = @($States | Where-Object { $_ -eq 'NotRun' }).Count
        inProcess = @($States | Where-Object { $_ -eq 'InProcess' }).Count
        tests = $tests
    }
}

# These functions shadow process operations only inside the fixture scripts.
# Real processes and the project's Saved/ directories are never touched.
function Start-Process {
    param($FilePath, $ArgumentList, [switch] $PassThru, $WindowStyle,
        $RedirectStandardOutput, $RedirectStandardError)
    $fixture.launchedPath = $FilePath
    $fixture.launchedArguments = $ArgumentList
    $logDir = Join-Path $fixture.caseDirectory 'Saved\Logs'
    $reportDir = Join-Path $fixture.caseDirectory 'Saved\TestReports'
    if ($fixture.caseOptions.ContainsKey('Report')) {
        $fixture.caseOptions.Report | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath (Join-Path $reportDir 'index.json')
    }
    if ($fixture.caseOptions.ContainsKey('Log')) {
        Set-Content -LiteralPath (Join-Path $logDir $fixture.logName) -Value $fixture.caseOptions.Log
    }
    if ($fixture.caseOptions.ContainsKey('Shots')) {
        for ($i = 1; $i -le $fixture.caseOptions.Shots; $i++) {
            Set-Content -LiteralPath (Join-Path $fixture.shotDirectory ('{0:00}_Fixture.png' -f $i)) -Value 'PNG fixture'
        }
    }
    $process = [pscustomobject]@{
        Id = 12345
        Handle = 1
        ExitCode = if ($fixture.caseOptions.ContainsKey('ExitCode')) { $fixture.caseOptions.ExitCode } else { 0 }
        Completes = -not $fixture.caseOptions.ContainsKey('Timeout')
        HasExited = -not $fixture.caseOptions.ContainsKey('Timeout')
    }
    $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { param($Milliseconds) return $this.Completes }
    if ($PassThru) { return $process }
}

function Stop-Process {
    param($Id, [switch] $Force)
    Assert-True ($Id -eq 12345) 'The timeout must stop only the process it launched.'
    $fixture.stopped = $true
}

function taskkill.exe {
    Assert-True (($args -join ' ') -eq '/PID 12345 /T /F') 'Packaging must stop only its own process tree.'
    $fixture.stopped = $true
}

function Invoke-ScriptCase {
    param([string] $Name, [string] $ScriptName, [hashtable] $Options,
        [string] $FailurePattern = '', [hashtable] $Arguments = @{}, [switch] $FromCaseDirectory)
    $fixture.caseCount++
    $fixture.caseDirectory = Join-Path $fixtureRoot ('case {0:00}' -f $fixture.caseCount)
    $scripts = Join-Path $fixture.caseDirectory 'Scripts'
    $logs = Join-Path $fixture.caseDirectory 'Saved\Logs'
    $reports = Join-Path $fixture.caseDirectory 'Saved\TestReports'
    $fixture.shotDirectory = Join-Path $fixture.caseDirectory 'capture output'
    $oldPackage = Join-Path $fixture.caseDirectory 'LocalBuilds\Old\Binaries\Win64'
    $newPackage = Join-Path $fixture.caseDirectory 'LocalBuilds\New\Binaries\Win64'
    New-Item -ItemType Directory -Path $scripts, $logs, $reports, $fixture.shotDirectory, $oldPackage, $newPackage -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceScripts $ScriptName) -Destination $scripts
    Set-Content -LiteralPath (Join-Path $scripts 'Resolve-Engine.ps1') -Value 'param($EngineRoot); Join-Path (Split-Path -Parent $PSScriptRoot) ''fixture engine & tools'''
    Set-Content -LiteralPath (Join-Path $oldPackage 'UEGT2.exe') -Value 'old'
    Set-Content -LiteralPath (Join-Path $newPackage 'UEGT2.exe') -Value 'new'
    (Get-Item -LiteralPath (Join-Path $oldPackage 'UEGT2.exe')).LastWriteTime = (Get-Date).AddDays(-1)
    $fixture.caseOptions = $Options
    $fixture.stopped = $false
    $fixture.launchedPath = ''
    $fixture.launchedArguments = @()
    $fixture.logName = switch ($ScriptName) {
        'Test.ps1' { 'Test.log' }
        'Smoke-Packaged.ps1' { 'SmokePackaged.log' }
        'Fly-Soak.ps1' { 'FlySoak.log' }
        'Build-Content.ps1' { 'ContentBuild.log' }
        'Screenshot-Tour.ps1' { 'ScreenshotTour.log' }
        'Package.ps1' { 'Package.log' }
    }
    if ($Options.ContainsKey('StaleReport')) {
        New-Report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reports 'index.json')
    }
    if ($Options.ContainsKey('StaleLog')) {
        Set-Content -LiteralPath (Join-Path $logs $fixture.logName) -Value $Options.StaleLog
    }
    if ($ScriptName -eq 'Test.ps1') { $Arguments.SkipBuild = $true }
    if ($ScriptName -eq 'Package.ps1' -and -not $Arguments.ContainsKey('OutputDirectory')) {
        $Arguments.OutputDirectory = $newPackage + '\'
    }
    if ($ScriptName -eq 'Screenshot-Tour.ps1') {
        if (-not $Arguments.ContainsKey('OutputDirectory')) {
            $Arguments.OutputDirectory = $fixture.shotDirectory
        }
        Set-Content -LiteralPath (Join-Path $fixture.shotDirectory '01_Previous.png') -Value 'stale'
        Set-Content -LiteralPath (Join-Path $fixture.shotDirectory 'reference.png') -Value 'unrelated image'
        New-Item -ItemType Directory -Path (Join-Path $fixture.shotDirectory 'notes') | Out-Null
        Set-Content -LiteralPath (Join-Path $fixture.shotDirectory 'notes\review.txt') -Value 'keep me'
    }
    if ($ScriptName -in @('Smoke-Packaged.ps1', 'Fly-Soak.ps1', 'Screenshot-Tour.ps1') -and
        -not $Options.ContainsKey('AmbiguousPackage') -and
        -not $Arguments.ContainsKey('PackagedExecutable')) {
        # The normal cases name the intended archive explicitly. The separate
        # ambiguity cases below prove that omitting it fails closed.
        $Arguments.PackagedExecutable = Join-Path $newPackage 'UEGT2.exe'
    }
    $failure = $null
    $processDirectory = [Environment]::CurrentDirectory
    try {
        if ($FromCaseDirectory) { Push-Location -LiteralPath $fixture.caseDirectory }
        try {
            # Keep even an incorrect process-relative write inside the fixture,
            # while making it distinct from the intended PowerShell location.
            if ($FromCaseDirectory) { [Environment]::CurrentDirectory = $fixtureRoot }
            & (Join-Path $scripts $ScriptName) @Arguments 6>$null
        } finally {
            if ($FromCaseDirectory) {
                [Environment]::CurrentDirectory = $processDirectory
                Pop-Location
            }
        }
    }
    catch { $failure = $_.Exception.Message }
    if ($FailurePattern) {
        Assert-True ($null -ne $failure -and $failure -match $FailurePattern) "${Name}: expected '$FailurePattern', got '$failure'."
    } else {
        Assert-True ($null -eq $failure) "${Name}: unexpected failure '$failure'."
    }
    if ($Options.ContainsKey('Timeout')) { Assert-True $fixture.stopped "${Name}: timeout left the process running." }
    if ($null -eq $failure -and $ScriptName -in @('Smoke-Packaged.ps1', 'Fly-Soak.ps1', 'Screenshot-Tour.ps1')) {
        Assert-True ($fixture.launchedPath -eq (Join-Path $newPackage 'UEGT2.exe')) "${Name}: did not use the requested packaged binary."
    }
    if ($null -eq $failure -and $ScriptName -eq 'Screenshot-Tour.ps1') {
        Assert-True (Test-Path -LiteralPath (Join-Path $fixture.shotDirectory 'reference.png')) "${Name}: removed an unrelated image."
        Assert-True (Test-Path -LiteralPath (Join-Path $fixture.shotDirectory 'notes\review.txt')) "${Name}: removed unrelated documents."
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $fixture.shotDirectory '01_Previous.png'))) "${Name}: retained stale capture evidence."
        Assert-True (($fixture.launchedArguments -join ' ') -match '-UEGT2Capture="[^"\r\n]+capture output"') "${Name}: capture path with spaces is not quoted."
    }
    if ($ScriptName -eq 'Package.ps1') {
        $packageArguments = $fixture.launchedArguments -join ' '
        Assert-True ($packageArguments -match '(?:^| )-AdditionalCookerOptions=-SkipZenStore(?: |$)') "${Name}: local cook-store option not forwarded."
        Assert-True ($packageArguments -notmatch '(?:^| )-nozenstore(?: |$)') "${Name}: ineffective nozenstore option retained."
        Assert-True ($packageArguments -match '(?:^| )-UbtArgs=-WaitMutex(?: |$)') "${Name}: UBT mutex wait not forwarded."
    }
    Write-Host "PASS $Name"
}

try {
    Invoke-ScriptCase 'fresh automation succeeds' 'Test.ps1' @{ Report = (New-Report) }
    Assert-True (($fixture.launchedArguments -join ' ') -match '-ExecCmds="Automation RunTests UEGT2; Quit"') 'Automation command must retain its argument quotes.'

    # Exercise Windows command-line parsing too; a mocked ArgumentList alone
    # cannot prove that spaces and the ExecCmds semicolon reach the child intact.
    $probeScript = Join-Path $fixtureRoot 'argument probe.ps1'
    $probeOutput = Join-Path $fixtureRoot 'argument output.json'
    Set-Content -LiteralPath $probeScript -Value 'param([string] $Output); @($args) | ConvertTo-Json | Set-Content -LiteralPath $Output; exit 7'
    $probeArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$probeScript`"", "`"$probeOutput`"") + $fixture.launchedArguments
    $probe = Microsoft.PowerShell.Management\Start-Process -FilePath 'powershell.exe' -ArgumentList $probeArgs -PassThru -WindowStyle Hidden
    try {
        Assert-True ($probe.WaitForExit(10000)) 'Native argument probe timed out.'
        Assert-True ($probe.ExitCode -eq 7) 'Native child exit code was lost.'
        $nativeArgs = Get-Content -LiteralPath $probeOutput -Raw | ConvertFrom-Json
        Assert-True ($nativeArgs[0] -eq (Join-Path $fixture.caseDirectory 'UEGT2.uproject')) 'Native project path was split at spaces.'
        Assert-True ($nativeArgs -contains '-ExecCmds=Automation RunTests UEGT2; Quit') 'Native automation command was split.'
        Assert-True ($nativeArgs -contains '-TestExit=Automation Test Queue Empty') 'Native exit marker was split.'
    } finally {
        if (-not $probe.HasExited) { Microsoft.PowerShell.Management\Stop-Process -Id $probe.Id -Force }
    }
    $fixture.nativeCaseCount++
    Write-Host 'PASS native automation argument quoting and exit code'

    Invoke-ScriptCase 'editor launch quotes project path' 'Open-Editor.ps1' @{}
    $probeArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$probeScript`"", "`"$probeOutput`"") + $fixture.launchedArguments
    $probe = Microsoft.PowerShell.Management\Start-Process -FilePath 'powershell.exe' -ArgumentList $probeArgs -PassThru -WindowStyle Hidden
    $null = $probe.Handle
    try {
        Assert-True ($probe.WaitForExit(10000)) 'Native editor argument probe timed out.'
        Assert-True ($probe.ExitCode -eq 7) 'Native editor argument probe exit code was lost.'
        $nativeArgs = @(Get-Content -LiteralPath $probeOutput -Raw | ConvertFrom-Json)
        Assert-True ($nativeArgs.Count -eq 1 -and $nativeArgs[0] -eq (Join-Path $fixture.caseDirectory 'UEGT2.uproject')) 'Native editor project path was split at spaces.'
    } finally {
        if (-not $probe.HasExited) { Microsoft.PowerShell.Management\Stop-Process -Id $probe.Id -Force }
    }
    $fixture.nativeCaseCount++
    Write-Host 'PASS native editor project argument quoting'
    Invoke-ScriptCase 'stale automation report is rejected' 'Test.ps1' @{ StaleReport = $true } 'produced no report'
    Invoke-ScriptCase 'editor crash overrides green report' 'Test.ps1' @{ Report = (New-Report); ExitCode = 7 } 'exit code 7'
    Invoke-ScriptCase 'automation timeout stops editor' 'Test.ps1' @{ Timeout = $true } 'exceeded'
    Invoke-ScriptCase 'empty test selection fails' 'Test.ps1' @{ Report = (New-Report -States @()) } 'failed or incomplete'
    Invoke-ScriptCase 'pending tests fail' 'Test.ps1' @{ Report = (New-Report -States @('Success', 'NotRun')) } 'failed or incomplete'
    Invoke-ScriptCase 'running tests fail' 'Test.ps1' @{ Report = (New-Report -States @('Success', 'InProcess')) } 'failed or incomplete'
    Invoke-ScriptCase 'failed tests fail' 'Test.ps1' @{ Report = (New-Report -States @('Success', 'Fail')) } 'failed or incomplete'
    $inconsistent = New-Report -States @('Success', 'Fail')
    $inconsistent.failed = 0
    Invoke-ScriptCase 'per-test failure overrides green summary' 'Test.ps1' @{ Report = $inconsistent } 'failed or incomplete'
    $warnings = New-Report
    $warnings.succeeded = 0
    $warnings.succeededWithWarnings = 1
    Invoke-ScriptCase 'successful test with warnings is counted' 'Test.ps1' @{ Report = $warnings }

    $walk = 'UEGT2_SMOKE_WALK_COMPLETE distance=400'
    Invoke-ScriptCase 'walk smoke succeeds' 'Smoke-Packaged.ps1' @{ Log = $walk }
    Invoke-ScriptCase 'ambiguous walk package requires explicit path' 'Smoke-Packaged.ps1' @{ AmbiguousPackage = $true } 'Multiple packaged builds'
    Invoke-ScriptCase 'stale walk log is rejected' 'Smoke-Packaged.ps1' @{ StaleLog = $walk } 'produced no log'
    Invoke-ScriptCase 'walk crash overrides completion' 'Smoke-Packaged.ps1' @{ Log = $walk; ExitCode = 3 } 'exit code 3'
    Invoke-ScriptCase 'non-positive fly duration is rejected' 'Fly-Soak.ps1' @{} 'finite positive number' -Arguments @{ Minutes = 0 }
    Invoke-ScriptCase 'fly crash overrides completion' 'Fly-Soak.ps1' @{ Log = 'UEGT2_FLY_SOAK_COMPLETE'; ExitCode = 4 } 'exit code 4'
    Invoke-ScriptCase 'fly soak succeeds' 'Fly-Soak.ps1' @{ Log = 'UEGT2_FLY_SOAK_COMPLETE' }
    Invoke-ScriptCase 'content crash overrides success marker' 'Build-Content.ps1' @{ Log = 'UEGT2_CONTENT_BUILD_SUCCEEDED'; ExitCode = 5 } 'exit code 5'
    Invoke-ScriptCase 'content success remains accepted' 'Build-Content.ps1' @{ Log = 'UEGT2_CONTENT_BUILD_SUCCEEDED' }

    foreach ($archiveRoot in @('', '.\LocalBuilds\New\Binaries\Win64\', 'C:\', '\\fixture-server\share name\')) {
        if ($archiveRoot.StartsWith('.\')) {
            Invoke-ScriptCase 'package relative output follows PowerShell location' 'Package.ps1' @{ Log = 'BUILD SUCCESSFUL' } -Arguments @{ OutputDirectory = $archiveRoot } -FromCaseDirectory
            $expectedArchive = (Join-Path $fixture.caseDirectory 'LocalBuilds\New\Binaries\Win64').Replace('\', '/') + '/'
        } elseif ($archiveRoot) {
            # Fail before output enumeration: these paths test parsing only,
            # and no files are read from or written to the drive/network root.
            Invoke-ScriptCase "package root path $archiveRoot" 'Package.ps1' @{ Log = 'BUILD SUCCESSFUL'; ExitCode = 8 } 'exit code 8' -Arguments @{ OutputDirectory = $archiveRoot }
            $expectedArchive = $archiveRoot.Replace('\', '/')
        } else {
            Invoke-ScriptCase 'packaging succeeds with trailing output separator' 'Package.ps1' @{ Log = 'BUILD SUCCESSFUL' }
            $expectedArchive = (Join-Path $fixture.caseDirectory 'LocalBuilds\New\Binaries\Win64').Replace('\', '/') + '/'
        }
        $batchDir = Join-Path $fixture.caseDirectory 'fixture engine & tools\Engine\Build\BatchFiles'
        New-Item -ItemType Directory -Path $batchDir -Force | Out-Null
        $batchArguments = Join-Path $fixtureRoot 'batch arguments.json'
        # Forward %* into a native child as UAT does. Echoing the command line
        # alone cannot detect a trailing backslash swallowing a closing quote.
        Set-Content -LiteralPath (Join-Path $batchDir 'RunUAT.bat') -Value "@echo off`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$probeScript`" `"$batchArguments`" %*`r`necho UAT_STDERR 1>&2`r`nexit /b 7" -Encoding Ascii
        $batchOutput = Join-Path $fixtureRoot 'batch output.log'
        $probe = Microsoft.PowerShell.Management\Start-Process -FilePath $env:ComSpec -ArgumentList $fixture.launchedArguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $batchOutput
        $null = $probe.Handle
        try {
            Assert-True ($probe.WaitForExit(10000)) 'Native batch probe timed out.'
            Assert-True ($probe.ExitCode -eq 7) 'Batch exit code was lost.'
            $batchText = Get-Content -LiteralPath $batchOutput -Raw
            $nativeArgs = Get-Content -LiteralPath $batchArguments -Raw | ConvertFrom-Json
            Assert-True ($nativeArgs -contains ('-project=' + (Join-Path $fixture.caseDirectory 'UEGT2.uproject'))) 'Batch project path lost quoting.'
            Assert-True ($nativeArgs -contains ('-archivedirectory=' + $expectedArchive)) 'Batch archive path lost its trailing separator or quote boundary.'
            Assert-True ($nativeArgs -contains '-AdditionalCookerOptions=-SkipZenStore') 'Native cooker option was split or lost.'
            Assert-True ($nativeArgs -notcontains '-nozenstore') 'Native package invocation retained ineffective nozenstore.'
            Assert-True ($nativeArgs -contains '-UbtArgs=-WaitMutex') 'Native UBT mutex forwarding was split or lost.'
            Assert-True ($batchText.Contains('UAT_STDERR')) 'Batch stderr did not reach the package log.'
        } finally {
            if (-not $probe.HasExited) { Microsoft.PowerShell.Management\Stop-Process -Id $probe.Id -Force }
        }
        $fixture.nativeCaseCount++
        Write-Host "PASS native batch quoting, stderr and exit code: $expectedArchive"
    }
    Invoke-ScriptCase 'packaging timeout stops its process tree' 'Package.ps1' @{ Timeout = $true } 'exceeded'
    Invoke-ScriptCase 'package crash overrides successful log' 'Package.ps1' @{ Log = 'BUILD SUCCESSFUL'; ExitCode = 8 } 'exit code 8'
    Invoke-ScriptCase 'package missing success marker fails' 'Package.ps1' @{ Log = 'Cook aborted' } 'did not report success'
    Invoke-ScriptCase 'package shader failure rejects successful cook' 'Package.ps1' @{ Log = "LogInit: Display: Failed to compile Material M_Broken`nBUILD SUCCESSFUL" } 'materials that do not compile'

    $tour = "Capture tour requested: 2 viewpoints`nUEGT2_CAPTURE_TOUR_COMPLETE (2 viewpoints)"
    Invoke-ScriptCase 'complete screenshot tour preserves unrelated files' 'Screenshot-Tour.ps1' @{ Log = $tour; Shots = 2 }
    Invoke-ScriptCase 'ambiguous screenshot package requires explicit path' 'Screenshot-Tour.ps1' @{ AmbiguousPackage = $true } 'Multiple packaged builds'
    Invoke-ScriptCase 'capture relative output follows PowerShell location' 'Screenshot-Tour.ps1' @{ Log = $tour; Shots = 2 } -Arguments @{ OutputDirectory = 'capture output' } -FromCaseDirectory
    Assert-True ($fixture.launchedArguments -contains ('-UEGT2Capture="' + $fixture.shotDirectory.Replace('\', '/') + '"')) 'Capture output did not resolve against the requested PowerShell location.'
    Invoke-ScriptCase 'partial screenshot tour fails' 'Screenshot-Tour.ps1' @{ Log = $tour; Shots = 1 } 'expected 2'
    Invoke-ScriptCase 'stale or unrelated PNG cannot pass' 'Screenshot-Tour.ps1' @{ Log = $tour; Shots = 0 } 'produced 0 screenshots'
    Invoke-ScriptCase 'screenshots without completion fail' 'Screenshot-Tour.ps1' @{ Log = 'Capture tour requested: 2 viewpoints'; Shots = 2 } 'did not complete'
    Invoke-ScriptCase 'screenshot crash overrides completed images' 'Screenshot-Tour.ps1' @{ Log = $tour; Shots = 2; ExitCode = 6 } 'exit code 6'
    Invoke-ScriptCase 'menu uses its screen count' 'Screenshot-Tour.ps1' @{ Log = "$tour`nMenu capture: 3 screens"; Shots = 3 } -Arguments @{ Menu = $true }
    Invoke-ScriptCase 'life interaction failure rejects screenshots' 'Screenshot-Tour.ps1' @{ Log = "$tour`nAmenity capture: 2 stops`nAmenity capture 01: NOT USED"; Shots = 2 } 'failed or did not complete'
    Invoke-ScriptCase 'missing life amenity rejects reduced tour' 'Screenshot-Tour.ps1' @{ Log = "$tour`nAmenity capture: no Food anywhere in the world.`nAmenity capture: 2 stops"; Shots = 2 } 'failed or did not complete'
    Write-Host "Verification scripts: $($fixture.caseCount) simulated cases and $($fixture.nativeCaseCount) native command-line checks passed."
} finally {
    # Verify the exact absolute fixture path before recursive removal.
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $tempPrefix = $tempRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixture.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolvedFixture) -notmatch '^UEGT2-ScriptTests-[0-9a-f]{32}$') {
        throw "Refusing to remove unexpected fixture path: $resolvedFixture"
    }
    if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
}
