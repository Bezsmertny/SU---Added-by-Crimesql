$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$results = [System.Collections.Generic.List[object]]::new()
$system = Join-Path $env:SystemRoot 'System32'

# Export the exact C++-generated operation scripts and parse them, never invoke them.
& './build/Release/game_tests.exe' '--export-scripts' 'build/maintenance-plans'
if ($LASTEXITCODE -ne 0) { throw 'Could not export generated maintenance scripts.' }
$plans = @(Get-ChildItem -LiteralPath 'build/maintenance-plans' -Filter '*.ps1')
if ($plans.Count -ne 7) { throw 'Expected seven generated maintenance scripts.' }
foreach ($plan in $plans) {
    $tokens = $null; $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($plan.FullName, [ref]$tokens, [ref]$parseErrors) | Out-Null
    if ($parseErrors.Count -ne 0) { throw "Generated script failed parsing: $($plan.Name): $parseErrors" }
}
$results.Add(@{ Tool = 'Seven exact generated maintenance scripts'; Result = 'parsed; not executed' })

# Only version/help/status requests: no scans, repairs or reboot.
$version = & (Join-Path $system 'WindowsPowerShell\v1.0\powershell.exe') -NoLogo -NoProfile -NonInteractive -Command 'Write-Output $PSVersionTable.PSVersion.Major; exit 0'
if ($LASTEXITCODE -ne 0 -or [int]$version -lt 5) { throw 'Windows PowerShell failed the read-only smoke check.' }
$results.Add(@{ Tool = 'Windows PowerShell -NoProfile'; Result = 'passed'; Major = [int]$version })

& (Join-Path $system 'cmd.exe') /d /c 'exit 0'
if ($LASTEXITCODE -ne 0) { throw 'CMD /d failed.' }
$results.Add(@{ Tool = 'CMD /d'; Result = 'passed' })

$sfc = (& (Join-Path $system 'sfc.exe') '/?' 2>&1 | Out-String)
$sfc = $sfc -replace '\x00', ''
if ($sfc -notmatch 'VERIFYONLY' -or $sfc -notmatch 'SCANNOW') { throw 'SFC help does not expose the expected switches.' }
$results.Add(@{ Tool = 'SFC help'; Result = 'passed' })

$dism = (& (Join-Path $system 'dism.exe') '/Online' '/Cleanup-Image' '/?' 2>&1 | Out-String)
$dism = $dism -replace '\x00', ''
if ($dism -notmatch 'RestoreHealth') { throw 'DISM help does not expose RestoreHealth.' }
$results.Add(@{ Tool = 'DISM cleanup-image help'; Result = 'passed' })

$winre = (& (Join-Path $system 'reagentc.exe') '/info' 2>&1 | Out-String)
$winreCode = $LASTEXITCODE
if ([string]::IsNullOrWhiteSpace($winre)) { throw 'reagentc /info produced no status or error information.' }
$results.Add(@{ Tool = 'reagentc /info'; Result = 'status queried'; ExitCode = $winreCode; WinREBootTested = $false })

foreach ($cmdlet in @('Start-MpScan', 'Start-MpWDOScan')) {
    $available = $null -ne (Get-Command $cmdlet -ErrorAction SilentlyContinue)
    $results.Add(@{ Tool = $cmdlet; Result = $(if ($available) { 'available; not executed' } else { 'unavailable on runner; not executed' }) })
}

$results | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 'build/windows-readonly-smoke.json'
$results | Format-Table Tool, Result -AutoSize
if ($env:GITHUB_STEP_SUMMARY) {
    'Read-only Windows checks passed. Scans, SFC/DISM repairs, WinRE boot and offline Defender reboot were not executed.' | Add-Content $env:GITHUB_STEP_SUMMARY
}
