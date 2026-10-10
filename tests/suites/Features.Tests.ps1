## Features without tests so far: FILE protocol, delete and undo, find, update
## message window, dark mode and the scheduled check.

param([string]$BinDir)

$CMD_DELETE = 40010         # ID_MENUITEM_DELETE
$CMD_UNDO = 40119           # ID_MENUITEM_UNDO
$CMD_FIND = 40111           # ID_MENUITEM_SERACH
$IDC_COMBO_FINDSTRING = 1116
$WM_GETDARKMODE = 0x0400 + 16

# Titles of all items saved in the data folder and its subfolders: @{ folder = titles }
function Get-AllSavedTitles($Env) {
  $r = @{}
  foreach ($f in Get-ChildItem $Env.Data -Recurse -Filter "Item.dat") {
    $r[$f.Directory.FullName.Substring($Env.Data.Length)] = @($script:Enc.GetString([IO.File]::ReadAllBytes($f.FullName)) -split "`r`n" | Where-Object { $_ } | ForEach-Object { ($_ -split "`t")[0] })
  }
  $r
}

Test-Case "FILE protocol detects a changed local file" {
  $e = New-TestEnv "feat-file" $BinDir
  $dir = Join-Path $e.Dir "`u{30D5}`u{30A1}`u{30A4}`u{30EB}"      # a Japanese folder name
  New-Item -ItemType Directory -Force $dir | Out-Null
  $file = Join-Path $dir "watched.txt"
  Set-Content -Path $file -Value "first" -NoNewline -Encoding ascii
  Set-Items $e @(ItemLine "local" "file:///$file")
  # the FILE protocol of wwwc.dll is not registered by default
  [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"),
    "[PROTOCOL]`r`nProtocolCnt=2`r`ntitle-0=HTTP`r`nDLL-0=wwwc.dll`r`nFuncHeader-0=HTTP_`r`nUpIconIndex-0=-1`r`ntitle-1=FILE`r`nDLL-1=wwwc.dll`r`nFuncHeader-1=FILE_`r`nUpIconIndex-1=-1`r`n[UPMSG]`r`nUPSnd=0`r`nUPMsg=0`r`n", $script:Enc)
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK; Start-Sleep 3
  Assert-Equal 0 (Stop-Wwwc $e) "exit code (first check)"
  Assert-Equal "5" (Get-SavedItem $e "local").Size "size of the file"
  # the second check after a change marks the item as updated
  Add-Content -Path $file -Value " and more" -NoNewline -Encoding ascii
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK; Start-Sleep 3
  Assert-Equal 0 (Stop-Wwwc $e) "exit code (second check)"
  $i = Get-SavedItem $e "local"
  # an updated item has "*" after the changed value
  Assert-Equal "14*" $i.Size "new size with the update mark"
  Assert-True (($i.Status -band 1) -ne 0) "item is marked as updated (status $($i.Status))"
}

Test-Case "delete an item and undo" {
  $e = New-TestEnv "feat-delete" $BinDir
  Set-Items $e @((ItemLine "keep" (Url "/keep")), (ItemLine "remove" (Url "/remove")))
  [void](Start-Wwwc $e)
  Select-ListItem $e "remove"
  Send-Command $e $CMD_DELETE
  # answer the confirmation, if any
  [void](Wait-Until { -not ((Get-ListTitles $e) -contains "remove") -or (Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero }) } 5)
  Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero } | ForEach-Object { [void][WwwcWin]::PostMessage($_.Handle, 0x0111, [IntPtr]6, [IntPtr]::Zero) }
  Assert-True (Wait-Until { -not ((Get-ListTitles $e) -contains "remove") } 5) "item is removed from the list"
  Send-Command $e $CMD_UNDO
  Assert-True (Wait-Until { (Get-ListTitles $e) -contains "remove" } 5) "undo brings the item back"
  Select-ListItem $e "remove"
  Send-Command $e $CMD_DELETE
  [void](Wait-Until { (Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero }) -or -not ((Get-ListTitles $e) -contains "remove") } 5)
  Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero } | ForEach-Object { [void][WwwcWin]::PostMessage($_.Handle, 0x0111, [IntPtr]6, [IntPtr]::Zero) }
  [void](Wait-Until { -not ((Get-ListTitles $e) -contains "remove") } 5)
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $saved = Get-AllSavedTitles $e
  Assert-Equal "keep" ($saved[""] -join ",") "root folder keeps the other item"
  $recycled = @($saved.Keys | Where-Object { $_ -ne "" -and $saved[$_] -contains "remove" })
  Assert-True ($recycled.Count -eq 1) "deleted item is in the recycle bin folder ($(($saved.Keys | ForEach-Object { "$_=$($saved[$_] -join '/')" }) -join '; '))"
}

Test-Case "find items by name" {
  $e = New-TestEnv "feat-find" $BinDir
  $sub = Join-Path $e.Data "sub"
  New-Item -ItemType Directory -Force $sub | Out-Null
  [IO.File]::WriteAllBytes((Join-Path $sub "Item.dat"), $script:Enc.GetBytes((ItemLine "apple pie" (Url "/c")) + "`r`n"))
  Set-Items $e @((ItemLine "apple" (Url "/a")), (ItemLine "banana" (Url "/b")))
  [void](Start-Wwwc $e)
  Send-Command $e $CMD_FIND
  [void](Wait-Until { $script:fw = Get-Dialogs $e | Where-Object { [WwwcWin]::FindChildById($_.Handle, $IDC_COMBO_FINDSTRING) -ne [IntPtr]::Zero } | Select-Object -First 1; $null -ne $script:fw } 10)
  $fw = $script:fw
  Assert-True ($null -ne $fw) "find window opened"
  if ($fw) {
    [void][WwwcWin]::SendMessageText([WwwcWin]::FindChildById($fw.Handle, $IDC_COMBO_FINDSTRING), 0x000C, [IntPtr]::Zero, "apple")
    [void][WwwcWin]::PostMessage($fw.Handle, 0x0111, [IntPtr]1, [IntPtr]::Zero)       # IDOK: start
    $lv = [WwwcWin]::GetDlgItem($fw.Handle, $script:ID_LIST)
    Assert-True (Wait-Until { [WwwcList]::Count($lv) -ge 2 } 10) "results are listed"
    $found = @(for ($i = 0; $i -lt [WwwcList]::Count($lv); $i++) { [WwwcList]::GetText($lv, $i, 0) }) | Sort-Object
    Assert-Equal "apple,apple pie" ($found -join ",") "matching items in all folders"
  }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "update message lists the updated item" {
  $e = New-TestEnv "feat-upmsg" $BinDir
  # the item had another size before: this check finds an update
  Set-Items $e @("updated`t$(Url '/target')`t10`t`t0`t`t`t`t`t2`t0;;0;;;;;;0;;;;;;;;0`t`t0`t`t`t")
  [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"), "[UPMSG]`r`nUPSnd=0`r`n", $script:Enc)
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  [void](Wait-Until { $script:up = Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, $script:ID_LIST) -ne [IntPtr]::Zero } | Select-Object -First 1; $null -ne $script:up } 15)
  $up = $script:up
  Assert-True ($null -ne $up) "update message window opened"
  if ($up) {
    $lv = [WwwcWin]::GetDlgItem($up.Handle, $script:ID_LIST)
    $titles = @(for ($i = 0; $i -lt [WwwcList]::Count($lv); $i++) { [WwwcList]::GetText($lv, $i, 0) })
    Assert-True ($titles -contains "updated") "the updated item is listed ($($titles -join ', '))"
  }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-True (((Get-SavedItem $e "updated").Status -band 1) -ne 0) "item is saved as updated"
}

Test-Case "dark mode on and off" {
  foreach ($mode in 1, 0) {
    $e = New-TestEnv "feat-dark$mode" $BinDir
    Set-Items $e @(ItemLine "page" (Url "/target"))
    [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"), "[GENERAL]`r`nDarkMode=$mode`r`n", $script:Enc)
    [void](Start-Wwwc $e); Start-Sleep 1
    $dark = [int][WwwcWin]::SendMessage($e.Window, $WM_GETDARKMODE, [IntPtr]::Zero, [IntPtr]::Zero)
    Assert-Equal $mode $dark "WM_GETDARKMODE with DarkMode=$mode"
    # empty area at the bottom of the list
    $lv = Get-ListHandle $e
    $r = New-Object WwwcWin+RECT; [void][WwwcWin]::GetClientRect($lv, [ref]$r)
    $lum = Get-PixelLuminance $lv ($r.R - 20) ($r.B - 10)
    if ($mode -eq 1) { Assert-True ($lum -lt 80) "list background is dark (luminance $lum)" }
    else { Assert-True ($lum -gt 180) "list background is light (luminance $lum)" }
    Assert-Equal 0 (Stop-Wwwc $e) "exit code (DarkMode=$mode)"
  }
}

Test-Case "scheduled check (every minute)" {
  $e = New-TestEnv "feat-autocheck" $BinDir
  Set-Items $e @(ItemLine "scheduled" (Url "/scheduled"))
  # AUTOTIME_MIN (1) every 1 minute; the timer runs once a minute. Checks every N
  # minutes are disabled unless NoMinuteCheck=0
  [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"), "[CHECK]`r`nNoMinuteCheck=0`r`n[AUTOCHECK]`r`nCnt=1`r`ntype-0=1`r`nflag-0=0`r`nm-0=1`r`n[UPMSG]`r`nUPSnd=0`r`nUPMsg=0`r`n", $script:Enc)
  $m = Get-ServerMark
  [void](Start-Wwwc $e)
  Assert-True (Wait-Until { (Get-ServerLog $m) -match "^GET /scheduled " } 90) "the item is checked without a command"
  Start-Sleep 1
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}
