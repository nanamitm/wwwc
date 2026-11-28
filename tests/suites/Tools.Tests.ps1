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
  Select-ListItem $e "page"; Send-Command $e ($CMD_ACTION + 4)
  $len = Wait-ViewWindow $e
  Assert-True ($len -gt 50) "header view shows the response header (length $len)"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "source view through a redirect" {
  $e = New-TestEnv "tool-source" $BinDir
  Set-Items $e @(ItemLine "moved" (Url "/r302"))
  $m = Get-ServerMark
  [void](Start-Wwwc $e)
  Select-ListItem $e "moved"; Send-Command $e ($CMD_ACTION + 5)
  $len = Wait-ViewWindow $e
  Assert-True ((Get-ServerLog $m) -match "^GET /target ") "redirect was followed"
  Assert-Equal 67 $len "source view shows the page"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "get title" {
  $e = New-TestEnv "tool-title" $BinDir
  Set-Items $e @(ItemLine "untitled" (Url "/target"))
  [void](Start-Wwwc $e)
  Select-ListItem $e "untitled"; Send-Command $e ($CMD_ACTION + 6)
  Assert-True (Wait-Until { (Get-ListTitles $e) -contains "target page" } 10) "item is renamed to the page title"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-True ($null -ne (Get-SavedItem $e "target page")) "new title is saved"
}

Test-Case "cancel source view while receiving shows the partial source" {
  $e = New-TestEnv "tool-cancel" $BinDir
  Set-Items $e @(ItemLine "drip" (Url "/drip-source"))
  [void](Start-Wwwc $e)
  Select-ListItem $e "drip"; Send-Command $e ($CMD_ACTION + 5)
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

# Browser information is available again through the upstream UI Automation
# implementation. Its real-browser interaction requires a manual desktop check.

Test-Case "open URL tool: a URL with a quote is not passed to the browser" {
  $e = New-TestEnv "tool-openurl-quote" $BinDir
  $record = Join-Path $e.Dir "opened.txt"
  Set-Content -Path (Join-Path $e.Dir "browser.cmd") -Value "@echo %*>> `"$record`"" -Encoding ascii
  [IO.File]::WriteAllText((Join-Path $e.Bin "wwwc.ini"), "[HTTP]`r`nBrowserPath=$(Join-Path $e.Dir 'browser.cmd')`r`n", $script:Enc)
  Set-Tools $e @([pscustomobject]@{ Title = "open"; Action = 1; Protocol = "HTTP"; Func = "DDE_OpenURL" })
  # a quote would close the quoted argument and pass "--evil" as a browser option
  Set-Items $e @(ItemLine "page" 'http://example.invalid/a" --evil "b')
  [void](Start-Wwwc $e)
  Select-ListItem $e "page"
  Send-Command $e $CMD_TOOL
  Assert-True (Wait-Until { (Get-Dialogs $e).Count -gt 0 } 10) "a message is shown"
  Start-Sleep 2
  Assert-True (-not (Test-Path $record)) "the browser was not started"
  # the message that the URL is not opened is expected
  Assert-Equal 0 (Stop-Wwwc $e -AllowMessageBox) "exit code"
}

# A program tool (cmd.exe) registered in user.ini. Values are written in quotes:
# profile_get_string removes one quote at each end
function Set-CmdTool($Env, [string]$CommandLine) {
  $lines = @("[TOOLS]", "Cnt=1", "title-0=`"run`"", "FileName-0=`"$env:ComSpec`"", "Action-0=1", "MenuIndex-0=-1",
    "Protocol-0=`"HTTP`"", "CommandLine-0=`"$CommandLine`"", "func-0=`"`"", "Ctrl-0=0", "Key-0=`"`"")
  [IO.File]::WriteAllText((Join-Path $Env.Data "user.ini"), ($lines -join "`r`n") + "`r`n", $script:Enc)
}

Test-Case "a tool command line with a long comment is passed in full" {
  $e = New-TestEnv "tool-longcmd" $BinDir
  $record = Join-Path $e.Dir "comment.txt"
  # the command line was built in a 1024-byte stack buffer without a length check
  $comment = "c" * 3000
  Set-CmdTool $e "/c echo %m>`"$record`""
  $fields = (ItemLine "long" (Url "/target")).Split("`t")
  $fields[11] = $comment
  Set-Items $e @($fields -join "`t")
  [void](Start-Wwwc $e)
  Select-ListItem $e "long"
  Send-Command $e $CMD_TOOL
  Assert-True (Wait-Until { (Test-Path $record) -and (Get-Content $record -Raw) } 10) "the tool was started"
  Assert-True (-not $e.Process.HasExited) "WWWC is running (exit code $(if ($e.Process.HasExited) { '0x{0:X8}' -f $e.Process.ExitCode }))"
  if (Test-Path $record) { Assert-Equal $comment (Get-Content $record -Raw).Trim() "the whole comment is passed" }
  if (-not $e.Process.HasExited) { Assert-Equal 0 (Stop-Wwwc $e) "exit code" }
}

Test-Case "a quote in the item title does not end the quoted tool argument" {
  $e = New-TestEnv "tool-quote" $BinDir
  $record = Join-Path $e.Dir "title.txt"
  $evil = Join-Path $e.Dir "injected.txt"
  Set-CmdTool $e "/c echo `"%t`">`"$record`""
  # a title that closes the quotes and adds a command for cmd.exe
  Set-Items $e @(ItemLine "x`" & echo injected>`"$evil`" & `"y" (Url "/target"))
  [void](Start-Wwwc $e)
  [void](Wait-Until { @(Get-ListTitles $e | Where-Object { $_ -like "x*" }).Count -gt 0 } 5)
  Select-ListItem $e (Get-ListTitles $e | Where-Object { $_ -like "x*" } | Select-Object -First 1)
  Send-Command $e $CMD_TOOL
  Assert-True (Wait-Until { (Test-Path $record) -and (Get-Content $record -Raw) } 10) "the tool was started"
  Start-Sleep 1
  Assert-True (-not (Test-Path $evil)) "the title did not add a command"
  if (Test-Path $record) { Assert-True ((Get-Content $record -Raw) -like "*x'*") "the title is passed with the quote replaced ($(Get-Content $record -Raw))" }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "item properties opened from the update message survive a check that ends meanwhile" {
  $e = New-TestEnv "tool-upprop" $BinDir
  # /grow is one byte longer on every request: every check finds an update, and
  # the update message replaces (frees) the listed item that the properties use
  # the stored size differs from the page: the first check is an update too
  $fields = (ItemLine "growing" (Url "/grow-upprop")).Split("`t"); $fields[2] = "1"
  Set-Items $e @($fields -join "`t")
  [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"), "[UPMSG]`r`nUPSnd=0`r`n", $script:Enc)
  [IO.File]::WriteAllText((Join-Path $e.Bin "wwwc.ini"), "[ARCHIVE]`r`nEnable=0`r`n", $script:Enc)
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  [void](Wait-Until { $script:up = Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, $script:ID_LIST) -ne [IntPtr]::Zero } | Select-Object -First 1; $null -ne $script:up } 15)
  $up = $script:up
  Assert-True ($null -ne $up) "update message window opened"
  if ($up) {
    $uplv = [WwwcWin]::GetDlgItem($up.Handle, $script:ID_LIST)
    [WwwcList]::Select($uplv, 0)
    [void][WwwcWin]::PostMessage($up.Handle, 0x0111, [IntPtr]$CMD_PROP, [IntPtr]::Zero)
    [void](Wait-Until { $script:sheet = Get-Dialogs $e | Where-Object { $_.Title -like "growing*" } | Select-Object -First 1; $null -ne $script:sheet } 10)
    $sheet = $script:sheet
    Assert-True ($null -ne $sheet) "item property sheet opened from the update message"
    if ($sheet) {
      # check again while the properties are open: the item is updated again
      $m = Get-ServerMark
      Send-Command $e $CMD_ALLCHECK
      Assert-True (Wait-Until { (Get-ServerLog $m) -match "^GET /grow-upprop " } 10) "the second check ran"
      Start-Sleep 2
      [void][WwwcWin]::PostMessage([WwwcWin]::GetDlgItem($sheet.Handle, 1), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)   # OK
      Assert-True (Wait-Until { -not [WwwcWin]::IsWindow($sheet.Handle) } 5) "property sheet closed"
      Start-Sleep 1
      $titles = @(for ($i = 0; $i -lt [WwwcList]::Count($uplv); $i++) { [WwwcList]::GetText($uplv, $i, 0) })
      Assert-True ($titles -contains "growing") "the update message lists the item after the properties are closed ($($titles -join ', '))"
    }
  }
  Assert-True (-not $e.Process.HasExited) "WWWC is running (exit code $(if ($e.Process.HasExited) { '0x{0:X8}' -f $e.Process.ExitCode }))"
  if (-not $e.Process.HasExited) { Assert-Equal 0 (Stop-Wwwc $e) "exit code" }
}
