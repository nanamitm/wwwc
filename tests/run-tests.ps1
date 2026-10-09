<#
.SYNOPSIS
  Runs the WWWC tests against a built WWWC.exe / wwwc.dll.

.DESCRIPTION
  Each test copies WWWC to its own folder under %TEMP%\wwwc-tests, writes the
  data files, drives WWWC with window messages and checks the result.
  HTTP tests use a local server (tests\server\test_server.py) that listens only
  on 127.0.0.1 - 127.0.0.5. Python 3 is required.

  WWWC windows appear while the tests run; they need an interactive desktop
  session, but no mouse or keyboard input.

.EXAMPLE
  pwsh tests\run-tests.ps1 -BinDir x64\Release
  pwsh tests\run-tests.ps1 -BinDir Release -Suite Http,Password
#>
param(
  [string]$BinDir = (Join-Path $PSScriptRoot "..\x64\Release"),
  [string[]]$Suite = @("Parse", "Http", "Password", "Tools"),
  [int]$Port = 18769
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\WwwcTest.ps1")

$BinDir = (Resolve-Path $BinDir).Path
# "pwsh -File run-tests.ps1 -Suite A,B" passes one string
$Suite = @($Suite | ForEach-Object { $_ -split "," } | Where-Object { $_ })
foreach ($s in $Suite) {
  if (-not (Test-Path (Join-Path $PSScriptRoot "suites\$s.Tests.ps1"))) { throw "unknown suite: $s" }
}
foreach ($f in "WWWC.exe", "wwwc.dll") {
  if (-not (Test-Path (Join-Path $BinDir $f))) { throw "$f not found in $BinDir (build first)" }
}
Write-Host "WWWC: $BinDir"

$ErrorActionPreference = "Continue"
Start-TestServer $Port
try {
  foreach ($s in $Suite) {
    Write-Host "== $s"
    . (Join-Path $PSScriptRoot "suites\$s.Tests.ps1") -BinDir $BinDir
  }
} finally {
  Stop-AllWwwc
  Stop-TestServer
}

$failed = @($script:Results | Where-Object { -not $_.Passed })
Write-Host ""
Write-Host "$($script:Results.Count - $failed.Count) passed, $($failed.Count) failed"
if ($env:GITHUB_STEP_SUMMARY) {
  $lines = @("### Tests ($(Split-Path -Leaf (Split-Path $BinDir)) $(Split-Path -Leaf $BinDir))", "", "| Test | Result |", "|---|---|")
  $lines += $script:Results | ForEach-Object { "| $($_.Name) | $(if ($_.Passed) { 'pass' } else { 'FAIL: ' + ($_.Errors -join '; ') }) |" }
  Add-Content -Path $env:GITHUB_STEP_SUMMARY -Value ($lines -join "`n")
}
exit $(if ($failed.Count -eq 0 -and $script:Results.Count -gt 0) { 0 } else { 1 })
