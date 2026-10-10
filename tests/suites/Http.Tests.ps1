## HTTP checks against the local test server: results, redirects, errors,
## timeout, cancel and exit while checking.

param([string]$BinDir)

Test-Case "check updates size and date" {
  $e = New-TestEnv "http-basic" $BinDir
  Set-Items $e @(ItemLine "page" (Url "/target"))
  $m = Get-ServerMark
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  Assert-True (Wait-Until { (Get-ServerLog $m) -match "^GET /target " } 15) "server received GET /target"
  Start-Sleep 2
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $i = Get-SavedItem $e "page"
  Assert-Equal "67" $i.Size "size"
  Assert-True ($i.Date -ne "") "date is set"
  Assert-Equal "" $i.ErrStatus "no error"
}

Test-Case "redirects (302 relative, 301 absolute)" {
  $e = New-TestEnv "http-redirect" $BinDir
  Set-Items $e @((ItemLine "r302" (Url "/r302")), (ItemLine "r301" (Url "/r301" "127.0.0.2")))
  $m = Get-ServerMark
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  Assert-True (Wait-Until { @((Get-ServerLog $m) -match "^GET /target ").Count -ge 2 } 20) "both redirects followed to /target"
  Start-Sleep 2
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  foreach ($t in "r302", "r301") {
    $i = Get-SavedItem $e $t
    Assert-Equal "67" $i.Size "$t size"
    Assert-Equal "" $i.ErrStatus "$t no error"
  }
}

Test-Case "connection error is stored on the item" {
  $e = New-TestEnv "http-error" $BinDir
  Set-Items $e @(ItemLine "refused" "http://127.0.0.1:1/x")
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  Start-Sleep 6
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $i = Get-SavedItem $e "refused"
  Assert-True (($i.Status -band 2) -ne 0) "status has ST_ERROR (status $($i.Status))"
  Assert-True ($i.ErrStatus -match "WinHttpSendRequest") "error text names the failed call ('$($i.ErrStatus)')"
}

Test-Case "timeout" {
  $e = New-TestEnv "http-timeout" $BinDir
  [IO.File]::WriteAllText((Join-Path $e.Bin "wwwc.ini"), "[CHECK]`r`nTimeOut=3`r`n", $script:Enc)
  Set-Items $e @(ItemLine "slow" (Url "/slow-timeout"))
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  Start-Sleep 8
  Assert-True (-not $e.Process.HasExited) "WWWC is running"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-Equal 4 (Get-SavedItem $e "slow").Status "status is ST_TIMEOUT"
}

Test-Case "cancel while requests are blocked does not leak handles" {
  $e = New-TestEnv "http-cancel" $BinDir
  Set-Items $e @(1..4 | ForEach-Object { ItemLine "slow$_" (Url "/slow$_" "127.0.0.$($_ + 1)") })
  [void](Start-Wwwc $e)
  $rounds = { for ($i = 0; $i -lt 20; $i++) { Send-Command $e $CMD_ALLCHECK; Start-Sleep -Milliseconds (200 + 30 * $i); Send-Command $e $CMD_CHECKEND; Start-Sleep -Milliseconds 300 } }
  & $rounds; Start-Sleep 2; $h1 = Get-HandleCount $e
  & $rounds; Start-Sleep 2; $h2 = Get-HandleCount $e
  Assert-True (-not $e.Process.HasExited) "WWWC is running"
  # one leaked handle per request (80 requests) was the old behavior
  Assert-True (($h2 - $h1) -lt 60) "handle count after 20 more rounds: $h1 -> $h2"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "exit while receiving" {
  $e = New-TestEnv "http-exit" $BinDir
  Set-Items $e @((ItemLine "drip1" (Url "/drip1")), (ItemLine "drip2" (Url "/drip2" "127.0.0.2")))
  [void](Start-Wwwc $e)
  for ($i = 0; $i -lt 3; $i++) { Send-Command $e $CMD_ALLCHECK; Start-Sleep 2; Send-Command $e $CMD_CHECKEND; Start-Sleep -Milliseconds 500 }
  Send-Command $e $CMD_ALLCHECK; Start-Sleep 2
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}
