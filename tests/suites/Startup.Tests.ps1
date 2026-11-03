## Startup without /d: exercise both the executable and the loaded HTTP DLL.
param([string]$BinDir)

function Start-DefaultWwwc($Env, [string]$LocalRoot) {
  $Env.Process = Start-Process (Join-Path $Env.Bin 'WWWC.exe') -WorkingDirectory $LocalRoot `
    -Environment @{ LOCALAPPDATA = $LocalRoot } -PassThru
  [void]$script:Started.Add($Env.Process)
  $ok = Wait-Until {
    $Env.Process.HasExited -or (Get-ProcessWindows $Env.Process.Id | Where-Object Class -eq 'WWWCWindowClass')
  } 15
  Assert-True ($ok -and -not $Env.Process.HasExited) 'main window appeared'
  if ($Env.Process.HasExited -or -not $ok) { return }
  $Env.Window = (Get-ProcessWindows $Env.Process.Id | Where-Object Class -eq 'WWWCWindowClass' | Select-Object -First 1).Handle
}

function Set-StartupData([string]$Root, [string]$Title) {
  $userDir = Join-Path $Root $env:USERNAME
  New-Item -ItemType Directory -Force $userDir | Out-Null
  [IO.File]::WriteAllText((Join-Path $Root 'general.ini'), "[GENERAL]`r`nCharSetUTF8=1`r`n", $script:Enc)
  [IO.File]::WriteAllText((Join-Path $userDir 'user.ini'), "[GENERAL]`r`nCharSetUTF8=1`r`n", $script:Enc)
  [IO.File]::WriteAllText((Join-Path $userDir 'Item.dat'), (ItemLine $Title (Url '/target')) + "`r`n", $script:Enc)
  [IO.File]::WriteAllText((Join-Path $Root 'wwwc.ini'), "[HTTP]`r`nTimeZone=7`r`n", $script:Enc)
}

Test-Case 'installed startup prioritizes existing data beside WWWC.exe over LocalAppData' {
  $e = New-TestEnv 'startup-local-first' $BinDir
  $localRoot = Join-Path $e.Dir 'local'
  $fallback = Join-Path $localRoot 'WWWC'
  Set-StartupData $e.Bin 'portable item'
  Set-StartupData $fallback 'fallback item'
  [IO.File]::WriteAllText((Join-Path $e.Bin 'wwwc_app.ini'), "[GENERAL]`r`nportable=0`r`n", $script:Enc)
  $untouched = (Get-FileHash (Join-Path $fallback 'wwwc.ini')).Hash
  Start-DefaultWwwc $e $localRoot
  Assert-True ((Get-ListTitles $e) -contains 'portable item') 'executable loads local user data'
  Assert-True ((Get-ListTitles $e) -notcontains 'fallback item') 'fallback data is not mixed in'
  Assert-Equal 0 (Stop-Wwwc $e) 'exit code'
  Assert-True ((Get-Content (Join-Path $e.Bin 'wwwc.ini')) -contains '[FORMAT]') 'HTTP DLL uses the same local directory'
  Assert-Equal $untouched (Get-FileHash (Join-Path $fallback 'wwwc.ini')).Hash 'fallback settings remain untouched'
}

Test-Case 'installed startup uses LocalAppData when no previous local data exists' {
  $e = New-TestEnv 'startup-fallback' $BinDir
  $localRoot = Join-Path $e.Dir 'local'
  $fallback = Join-Path $localRoot 'WWWC'
  Set-StartupData $fallback 'fallback item'
  [IO.File]::WriteAllText((Join-Path $e.Bin 'wwwc_app.ini'), "[GENERAL]`r`nportable=0`r`n", $script:Enc)
  Start-DefaultWwwc $e $localRoot
  Assert-True ((Get-ListTitles $e) -contains 'fallback item') 'executable loads fallback user data'
  Assert-Equal 0 (Stop-Wwwc $e) 'exit code'
  Assert-True ((Get-Content (Join-Path $fallback 'wwwc.ini')) -contains '[FORMAT]') 'HTTP DLL uses LocalAppData too'
  Assert-True (-not (Test-Path (Join-Path $e.Bin 'general.ini'))) 'no local settings are created'
  Assert-True (-not (Test-Path (Join-Path $e.Bin 'wwwc.ini'))) 'no local DLL settings are created'
}

Test-Case 'portable startup keeps using the executable directory' {
  $e = New-TestEnv 'startup-portable' $BinDir
  $localRoot = Join-Path $e.Dir 'local'
  Set-StartupData (Join-Path $localRoot 'WWWC') 'fallback item'
  [IO.File]::WriteAllText((Join-Path $e.Bin 'wwwc_app.ini'), "[GENERAL]`r`nportable=1`r`n", $script:Enc)
  Start-DefaultWwwc $e $localRoot
  Assert-True ((Get-ListTitles $e) -notcontains 'fallback item') 'fresh portable installation does not adopt AppData items'
  Assert-Equal 0 (Stop-Wwwc $e) 'exit code'
  Assert-True (Test-Path (Join-Path $e.Bin 'general.ini')) 'local settings created'
  Assert-True (Test-Path (Join-Path $e.Bin 'wwwc.ini')) 'HTTP DLL stays portable'
}
