## Item tools of the HTTP plugin: header view, source view, get title.

param([string]$BinDir)

$IDC_EDIT_VIEW = 100

# The header/source view window and the length of its text
function Wait-ViewWindow($Env) {
  $script:view = $null
  [void](Wait-Until {
      $script:view = Get-Dialogs $Env | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, $IDC_EDIT_VIEW) -ne [IntPtr]::Zero } | Select-Object -First 1
      $null -ne $script:view } 10)
  if (-not $script:view) { return $null }
  $len = [int][WwwcWin]::SendMessage([WwwcWin]::GetDlgItem($script:view.Handle, $IDC_EDIT_VIEW), 0x000E, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_GETTEXTLENGTH
  [void][WwwcWin]::PostMessage($script:view.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 500
  $len
}

Test-Case "header view" {
  $e = New-TestEnv "tool-header" $BinDir
  Set-Items $e @(ItemLine "page" (Url "/target"))
  [void](Start-Wwwc $e)
  Select-ListItem $e "page"; Send-Command $e ($CMD_ACTION + 3)
  $len = Wait-ViewWindow $e
  Assert-True ($len -gt 50) "header view shows the response header (length $len)"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "source view through a redirect" {
  $e = New-TestEnv "tool-source" $BinDir
  Set-Items $e @(ItemLine "moved" (Url "/r302"))
  $m = Get-ServerMark
  [void](Start-Wwwc $e)
  Select-ListItem $e "moved"; Send-Command $e ($CMD_ACTION + 4)
  $len = Wait-ViewWindow $e
  Assert-True ((Get-ServerLog $m) -match "^GET /target ") "redirect was followed"
  Assert-Equal 67 $len "source view shows the page"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "get title" {
  $e = New-TestEnv "tool-title" $BinDir
  Set-Items $e @(ItemLine "untitled" (Url "/target"))
  [void](Start-Wwwc $e)
  Select-ListItem $e "untitled"; Send-Command $e ($CMD_ACTION + 5)
  Assert-True (Wait-Until { (Get-ListTitles $e) -contains "target page" } 10) "item is renamed to the page title"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-True ($null -ne (Get-SavedItem $e "target page")) "new title is saved"
}

Test-Case "cancel source view while receiving shows the partial source" {
  $e = New-TestEnv "tool-cancel" $BinDir
  Set-Items $e @(ItemLine "drip" (Url "/drip-source"))
  [void](Start-Wwwc $e)
  Select-ListItem $e "drip"; Send-Command $e ($CMD_ACTION + 4)
  Start-Sleep 4
  Send-Command $e $CMD_CHECKEND
  $len = Wait-ViewWindow $e
  Assert-True ($len -gt 100) "partial source is shown (length $len)"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

# Tool DLL functions of wwwc.dll registered in user.ini ([TOOLS], see WWWC/Ini.c)
function Set-Tools($Env, [object[]]$Tools) {
  $lines = @("[TOOLS]", "Cnt=$($Tools.Count)")
  for ($i = 0; $i -lt $Tools.Count; $i++) {
    $t = $Tools[$i]
    $lines += "title-$i=$($t.Title)", "FileName-$i=$(Join-Path $Env.Bin 'wwwc.dll')", "Action-$i=$($t.Action)",
      "MenuIndex-$i=-1", "Protocol-$i=$($t.Protocol)", "CommandLine-$i=", "func-$i=$($t.Func)", "Ctrl-$i=0", "Key-$i="
  }
  [IO.File]::WriteAllText((Join-Path $Env.Data "user.ini"), ($lines -join "`r`n") + "`r`n", $script:Enc)
}
$CMD_TOOL = 54000    # ID_MENU_TOOL_ACTION + index (item menu)
$CMD_WTOOL = 55000   # ID_WMENU_TOOL_ACTION + index (window menu)

Test-Case "open URL tool passes the URL to the configured browser at once" {
  $e = New-TestEnv "tool-openurl" $BinDir
  # a "browser" that records its arguments
  # in a folder with a Japanese name: wwwc.ini (UTF-8) is read with GetPrivateProfileStringA
  $dir = Join-Path $e.Dir "`u{30D6}`u{30E9}`u{30A6}`u{30B6}"
  New-Item -ItemType Directory -Force $dir | Out-Null
  $record = Join-Path $e.Dir "opened.txt"
  Set-Content -Path (Join-Path $dir "browser.cmd") -Value "@echo %*>> `"$record`"" -Encoding ascii
  [IO.File]::WriteAllText((Join-Path $e.Bin "wwwc.ini"), "[HTTP]`r`nBrowserPath=$(Join-Path $dir 'browser.cmd')`r`n", $script:Enc)
  Set-Tools $e @([pscustomobject]@{ Title = "open"; Action = 1; Protocol = "HTTP"; Func = "DDE_OpenURL" })
  Set-Items $e @(ItemLine "page" "http://example.invalid/a b?x=1&y=2")
  [void](Start-Wwwc $e)
  Select-ListItem $e "page"
  # DDE tried four browser names (about 2 seconds each) before. Starting the
  # first process can be slow on a CI runner, so a slow start is measured again
  $times = @()
  for ($try = 0; $try -lt 2; $try++) {
    Remove-Item $record -ErrorAction SilentlyContinue
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Send-Command $e $CMD_TOOL
    $opened = Wait-Until { (Test-Path $record) -and (Get-Content $record -Raw) } 20
    $times += $sw.Elapsed.TotalSeconds
    if (-not $opened -or $times[-1] -lt 4) { break }
  }
  Assert-True $opened "browser was started"
  if ($opened) {
    Assert-Equal '"http://example.invalid/a b?x=1&y=2"' (Get-Content $record -Raw).Trim() "browser argument"
    Assert-True ($times[-1] -lt 4) ("browser started after {0} s" -f (($times | ForEach-Object { $_.ToString("N1") }) -join " s, "))
  }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "removed browser info tools explain that they are not available" {
  $e = New-TestEnv "tool-browserinfo" $BinDir
  Set-Tools $e @([pscustomobject]@{ Title = "info"; Action = 2; Protocol = ""; Func = "GetBrowserInfo" })
  Set-Items $e @(ItemLine "page" (Url "/target"))
  [void](Start-Wwwc $e)
  Send-Command $e $CMD_WTOOL
  Assert-True (Wait-Until { (Get-Dialogs $e).Count -gt 0 } 10) "a message is shown"
  Assert-True (-not $e.Process.HasExited) "WWWC is running"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}
