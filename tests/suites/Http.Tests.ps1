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

Test-Case "proxy set in the item properties is used" {
  $e = New-TestEnv "http-itemproxy" $BinDir
  # Option2: NOPROXY=0;;SETPROXY=1;;PROXY;;PORT;;... (the host does not exist: only the proxy can answer)
  Set-Items $e @(ItemLine "viaproxy" "http://wwwc-item-proxy.invalid/x" "0;;1;;127.0.0.1;;$($script:ServerPort);;0;;;;;;;;0")
  $m = Get-ServerMark
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  Assert-True (Wait-Until { (Get-ServerLog $m) -match "^GET http://wwwc-item-proxy.invalid/x " } 15) "request went through the item's proxy ($(Get-ServerLog $m))"
  Start-Sleep 1
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-Equal "" (Get-SavedItem $e "viaproxy").ErrStatus "no error"
}

Test-Case "Windows proxy settings" {
  $e = New-TestEnv "http-sysproxy" $BinDir
  # Proxy=2: Windows settings. With no proxy configured for loopback the request goes
  # directly, and the proxy password must not be sent to the site
  [IO.File]::WriteAllText((Join-Path $e.Bin "wwwc.ini"),
    "[PROXY]`r`nProxy=2`r`npServer=192.0.2.1`r`npPort=8080`r`npNoCache=0`r`npUsePass=1`r`npUser=pu`r`npPass=$(-join ([Text.Encoding]::ASCII.GetBytes('pp') | ForEach-Object { '{0:x2}' -f ($_ -bxor 0xFF) }))`r`n", $script:Enc)
  Set-Items $e @(ItemLine "page" (Url "/sysproxy"))
  $m = Get-ServerMark
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  Assert-True (Wait-Until { (Get-ServerLog $m) -match "^GET /sysproxy " } 15) "page was checked ($(Get-ServerLog $m))"
  Assert-True ((Get-ServerLog $m) -match "^GET /sysproxy auth=None proxy_auth=None") "no proxy credentials were sent to the site"
  Start-Sleep 1
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

$IDC_BUTTON_EDIT = 1011   # WWWC/resource.h: protocol page "Properties"

Test-Case "HTTP settings: choose the Windows proxy settings" {
  $e = New-TestEnv "http-proxyui" $BinDir
  Set-Items $e @(ItemLine "page" (Url "/target"))
  [void](Start-Wwwc $e)
  Send-Command $e 40090                                         # ID_MENUITEM_OPTION
  [void](Wait-Until { $script:opt = Get-Dialogs $e | Select-Object -First 1; $null -ne $script:opt } 10)
  $opt = $script:opt
  [void][WwwcWin]::SendMessage($opt.Handle, 0x0400 + 101, [IntPtr]2, [IntPtr]::Zero)          # PSM_SETCURSEL: protocol page
  Start-Sleep -Milliseconds 500
  $page = [WwwcWin]::SendMessage($opt.Handle, 0x0400 + 118, [IntPtr]::Zero, [IntPtr]::Zero)
  [WwwcList]::Select([WwwcWin]::GetDlgItem($page, 1007), 0)                                    # IDC_LIST_PROTOCOL: HTTP
  [void][WwwcWin]::PostMessage($page, 0x0111, [IntPtr]$IDC_BUTTON_EDIT, [IntPtr]::Zero)
  [void](Wait-Until { $script:http = Get-Dialogs $e | Where-Object { $_.Handle -ne $opt.Handle } | Select-Object -First 1; $null -ne $script:http } 10)
  $http = $script:http
  Assert-True ($null -ne $http) "HTTP settings opened"
  if ($http) {
    $hp = [WwwcWin]::SendMessage($http.Handle, 0x0400 + 118, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][WwwcWin]::SendMessage([WwwcWin]::GetDlgItem($hp, 1050), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)   # BM_CLICK: Windows settings
    Assert-True (-not [WwwcWin]::IsWindowEnabled([WwwcWin]::GetDlgItem($hp, 1002))) "the proxy server field is disabled"
    [void][WwwcWin]::PostMessage([WwwcWin]::GetDlgItem($http.Handle, 1), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    [void](Wait-Until { -not [WwwcWin]::IsWindow($http.Handle) } 5)
  }
  [void][WwwcWin]::PostMessage([WwwcWin]::GetDlgItem($opt.Handle, 1), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
  [void](Wait-Until { -not [WwwcWin]::IsWindow($opt.Handle) } 5)
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-True ((Get-Content (Join-Path $e.Bin "wwwc.ini")) -contains "Proxy=2") "wwwc.ini has Proxy=2"
}


Test-Case "HTTP/1.1 chunked body is not decoded twice" {
  $e = New-TestEnv "http-chunked" $BinDir
  # Force GET, size comparison only, and an MD5 check to read the complete body.
  $fields = (ItemLine "chunked" (Url "/chunked")).Split("`t")
  $fields[2] = "8"
  $fields[9] = (@("2", "1", "0", "0", "0", "", "", "", "1") -join ";;")
  $fields[15] = "41390559c58a863397c1d503bc92a264"
  Set-Items $e @($fields -join "`t")
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  [void](Wait-Until { (Get-ServerLog) -match "^GET /chunked " } 10)
  Start-Sleep 2
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $item = Get-SavedItem $e "chunked"
  Assert-Equal "8" $item.Size "decoded body length excludes the header and NUL"
  Assert-Equal "" $item.ErrStatus "chunked request succeeded"
  $fields = ($script:Enc.GetString([IO.File]::ReadAllBytes($e.ItemFile))).TrimEnd("`r", "`n").Split("`t")
  Assert-Equal "41390559c58a863397c1d503bc92a264" $fields[15] "digest includes the first body line"
}
