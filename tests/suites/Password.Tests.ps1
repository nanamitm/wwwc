## Stored passwords: migration of the old format to DPAPI and use in requests.

param([string]$BinDir)

# Old password format: each byte XOR 0xFF as two hex digits
function Get-LegacyPassword([string]$Plain) { -join ($script:Sjis.GetBytes($Plain) | ForEach-Object { '{0:x2}' -f ($_ -bxor 0xFF) }) }
function Get-PassField($Env, [string]$Title) { ((Get-SavedItem $Env $Title).Option2 -split ";;")[6] }

function Invoke-Check($Env, [string]$Pattern) {
  $m = Get-ServerMark
  [void](Start-Wwwc $Env); Send-Command $Env $CMD_ALLCHECK
  [void](Wait-Until { (Get-ServerLog $m) -match $Pattern } 15)
  Wait-CheckEnd $Env
  Assert-Equal 0 (Stop-Wwwc $Env) "exit code"
  Get-ServerLog $m
}

Test-Case "item password in the old format is migrated" {
  $e = New-TestEnv "pass-item" $BinDir
  Set-Items $e @(ItemLine "auth" (Url "/auth") "1;;0;;;;;;1;;tester;;$(Get-LegacyPassword 's3cret!');;;;0")
  $log = Invoke-Check $e "^GET /auth "
  Assert-True ($log -cmatch "auth='tester:s3cret!'") "first check sends the password ($log)"
  Assert-True ((Get-PassField $e "auth") -like "DP2:*") "password is stored encrypted"
  $log = Invoke-Check $e "^GET /auth "
  Assert-True ($log -cmatch "auth='tester:s3cret!'") "second check decrypts the password ($log)"
}

Test-Case "proxy password in the old format is migrated" {
  $e = New-TestEnv "pass-proxy" $BinDir
  $ini = Join-Path $e.Bin "wwwc.ini"
  [IO.File]::WriteAllText($ini, "[PROXY]`r`nProxy=1`r`npServer=127.0.0.1`r`npPort=$($script:ServerPort)`r`npNoCache=0`r`npUsePass=1`r`npUser=pu`r`npPass=$(Get-LegacyPassword 'pp@ss')`r`n", $script:Enc)
  Set-Items $e @(ItemLine "proxied" "http://wwwc-proxy-test.invalid/x")
  $log = Invoke-Check $e "wwwc-proxy-test"
  Assert-True ($log -cmatch "proxy_auth='pu:pp@ss'") "first check sends the proxy password ($log)"
  Assert-True ((Get-Content $ini) -match "^pPass=DP2:") "proxy password is stored encrypted"
  $log = Invoke-Check $e "wwwc-proxy-test"
  Assert-True ($log -cmatch "proxy_auth='pu:pp@ss'") "second check decrypts the proxy password ($log)"
}

Test-Case "item password is not sent to the server a redirect leads to" {
  $e = New-TestEnv "pass-redirect" $BinDir
  $opt = "1;;0;;;;;;1;;tester;;$(Get-LegacyPassword 's3cret!');;;;0"
  Set-Items $e @((ItemLine "cross" (Url "/rcross" "127.0.0.3") $opt), (ItemLine "same" (Url "/r302-auth" "127.0.0.2") $opt))
  $m = Get-ServerMark
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  [void](Wait-Until { @((Get-ServerLog $m) -match "^GET /target ").Count -ge 2 } 20)
  Wait-CheckEnd $e
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $log = Get-ServerLog $m
  Assert-True ($log -cmatch "^GET /rcross auth='tester:s3cret!'") "first server gets the password ($log)"
  Assert-True ($log -cmatch "^GET /r302-auth auth='tester:s3cret!'") "same-server item gets the password"
  Assert-Equal 1 @($log -cmatch "^GET /target auth=None").Count "the other server does not get the password ($log)"
  Assert-Equal 1 @($log -cmatch "^GET /target auth='tester:s3cret!'").Count "a redirect on the same server keeps the password"
  Assert-Equal (Url "/rcross" "127.0.0.3") (Get-SavedItem $e "cross").Url "check URL is kept"
}

Test-Case "proxy password for HTTPS goes to the proxy (CONNECT), not to the site" {
  $e = New-TestEnv "pass-proxy-https" $BinDir
  [IO.File]::WriteAllText((Join-Path $e.Bin "wwwc.ini"), "[PROXY]`r`nProxy=1`r`npServer=127.0.0.1`r`npPort=$($script:ServerPort)`r`npNoCache=0`r`npUsePass=1`r`npUser=pu`r`npPass=$(Get-LegacyPassword 'pp@ss')`r`n", $script:Enc)
  Set-Items $e @(ItemLine "tls" "https://wwwc-proxy-test.invalid/x")
  $log = Invoke-Check $e "^CONNECT .*proxy_auth='pu:pp@ss'"
  Assert-True ($log -cmatch "^CONNECT wwwc-proxy-test.invalid:443 auth=None proxy_auth='pu:pp@ss'") "CONNECT carries the proxy password ($log)"
}

Test-Case "password entered in the item properties" {
  $e = New-TestEnv "pass-dialog" $BinDir
  Set-Items $e @(ItemLine "auth" (Url "/auth") "1;;0;;;;;;1;;tester;;$(Get-LegacyPassword 's3cret!');;;;0")
  [void](Start-Wwwc $e)
  Select-ListItem $e "auth"
  Send-Command $e $CMD_PROP
  $sheet = $null
  [void](Wait-Until { $script:sheetWin = Get-Dialogs $e | Select-Object -First 1; $null -ne $script:sheetWin } 10)
  $sheet = $script:sheetWin
  Assert-True ($sheet -and $sheet.Title -like "auth*") "item property sheet opened ('$($sheet.Title)')"
  if ($sheet -and $sheet.Title -like "auth*") {
    [void][WwwcWin]::SendMessage($sheet.Handle, 0x0400 + 101, [IntPtr]2, [IntPtr]::Zero)          # PSM_SETCURSEL: option page
    Start-Sleep -Milliseconds 500
    $page = [WwwcWin]::SendMessage($sheet.Handle, 0x0400 + 118, [IntPtr]::Zero, [IntPtr]::Zero)   # PSM_GETCURRENTPAGEHWND
    [void][WwwcWin]::SendMessageText([WwwcWin]::GetDlgItem($page, 1006), 0x000C, [IntPtr]::Zero, "newuser")
    [void][WwwcWin]::SendMessageText([WwwcWin]::GetDlgItem($page, 1007), 0x000C, [IntPtr]::Zero, "n3w-p@ss")
    [void][WwwcWin]::PostMessage([WwwcWin]::GetDlgItem($sheet.Handle, 1), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)   # OK
    Assert-True (Wait-Until { -not [WwwcWin]::IsWindow($sheet.Handle) } 5) "property sheet closed"
    $m = Get-ServerMark
    Send-Command $e $CMD_ALLCHECK
    [void](Wait-Until { (Get-ServerLog $m) -match "^GET /auth " } 15)
    Assert-True ((Get-ServerLog $m) -cmatch "auth='newuser:n3w-p@ss'") "check sends the new password"
  }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-True ((Get-PassField $e "auth") -like "DP2:*") "new password is stored encrypted"
}
