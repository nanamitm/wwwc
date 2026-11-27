## Features without tests so far: FILE protocol, delete and undo, find, update
## message window, dark mode and the scheduled check.

param([string]$BinDir)

$CMD_DELETE = 40010         # ID_MENUITEM_DELETE
$CMD_UNDO = 40119           # ID_MENUITEM_UNDO
$CMD_FIND = 40111           # ID_MENUITEM_SERACH
$ID_MENUITEM_ABOUT = 40038
$IDC_COMBO_FINDSTRING = 1116
$WM_GETDARKMODE = 0x0400 + 16

# Titles of all items saved in the data folder and its subfolders: @{ folder = titles }
function Read-Utf8Text([string]$Path) { $script:Enc.GetString([IO.File]::ReadAllBytes($Path)) }

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
    # the button area at the bottom of a message box (version information)
    Send-Command $e $ID_MENUITEM_ABOUT
    $shown = Wait-Until { (Get-Dialogs $e).Count -gt 0 } 10
    Assert-True $shown "version information is shown (DarkMode=$mode)"
    if ($shown) {
      Start-Sleep -Milliseconds 500
      $d = (Get-Dialogs $e)[0].Handle
      $wr = New-Object WwwcWin+RECT; [void][WwwcWin]::GetWindowRect($d, [ref]$wr)
      $lum = Get-PixelLuminance $d 20 ($wr.B - $wr.T - 20)
      if ($mode -eq 1) { Assert-True ($lum -lt 80) "message box button area is dark (luminance $lum)" }
      else { Assert-True ($lum -gt 180) "message box button area is light (luminance $lum)" }
      [void][WwwcWin]::PostMessage($d, 0x0111, [IntPtr]2, [IntPtr]::Zero)   # the OK button of MB_OK is IDCANCEL
      [void](Wait-Until { (Get-Dialogs $e).Count -eq 0 } 5)
    }
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

Test-Case "scheduled check at a set time runs once" {
  $e = New-TestEnv "feat-autotime" $BinDir
  Set-Items $e @(ItemLine "attime" (Url "/attime"))
  # AUTOTIME_DAY (3) at a minute that the once-a-minute timer reaches after its first or second tick
  $at = (Get-Date).AddSeconds(90)
  [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"), "[AUTOCHECK]`r`nCnt=1`r`ntype-0=3`r`nflag-0=0`r`nh-0=$($at.Hour)`r`nm-0=$($at.Minute)`r`n[UPMSG]`r`nUPSnd=0`r`nUPMsg=0`r`n", $script:Enc)
  # archiving (on by default) fetches an updated page once more: count only the check's request
  [IO.File]::WriteAllText((Join-Path $e.Bin "wwwc.ini"), "[ARCHIVE]`r`nEnable=0`r`n", $script:Enc)
  $m = Get-ServerMark
  [void](Start-Wwwc $e)
  Assert-True (Wait-Until { (Get-ServerLog $m) -match "^GET /attime " } 200) "the item is checked at $($at.ToString('HH:mm'))"
  # the next timer tick is within the same or the next minute: it must not start the check again
  Start-Sleep 65
  Assert-Equal 1 @((Get-ServerLog $m) -match "^GET /attime ").Count "checked once ($(Get-ServerLog $m))"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "DPI change (simulated with WM_DPICHANGED)" {
  $e = New-TestEnv "feat-dpi" $BinDir
  Set-Items $e @((ItemLine "page" (Url "/target")), (ItemLine "other" (Url "/other")))
  [void](Start-Wwwc $e)
  $tree = [WwwcWin]::GetDlgItem($e.Window, 1114); $lv = Get-ListHandle $e   # WWWC_TREE (the toolbar is rebuilt: get it each time)
  $measure = {
    [pscustomobject]@{
      Row = [int][WwwcWin]::SendMessage($tree, 0x111C, [IntPtr]::Zero, [IntPtr]::Zero)                    # TVM_GETITEMHEIGHT
      Button = ([int64][WwwcWin]::SendMessage([WwwcWin]::GetDlgItem($e.Window, 102), 0x0400 + 58, [IntPtr]::Zero, [IntPtr]::Zero)) -band 0xFFFF  # TB_GETBUTTONSIZE (width)
      Column = [int][WwwcWin]::SendMessage($lv, 0x1000 + 29, [IntPtr]0, [IntPtr]::Zero)                    # LVM_GETCOLUMNWIDTH
      CutImage = [int][WwwcWin]::SendMessage([WwwcWin]::GetDlgItem($e.Window, 102), 0x0400 + 44, [IntPtr]40055, [IntPtr]::Zero)  # TB_GETBITMAP (ID_MENUITEM_CUT)
    }
  }
  # Establish a 96 DPI baseline even when the desktop starts at 125% or 150%.
  $r = New-Object WwwcWin+RECT; [void][WwwcWin]::GetWindowRect($e.Window, [ref]$r)
  [WwwcList]::SendDpiChanged($e.Window, 96, $r.L, $r.T, $r.R, $r.B)
  Start-Sleep -Milliseconds 200
  $before = & $measure
  [WwwcList]::SendDpiChanged($e.Window, 144, $r.L, $r.T, $r.L + [int](($r.R - $r.L) * 1.5), $r.T + [int](($r.B - $r.T) * 1.5))
  Start-Sleep 1
  $high = & $measure
  if ($env:WWWC_DPI_SHOT) {
    $wr = New-Object WwwcWin+RECT; [void][WwwcWin]::GetWindowRect($e.Window, [ref]$wr)
    $bmp = New-Object Drawing.Bitmap ($wr.R - $wr.L), ($wr.B - $wr.T); $g = [Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
    [void][WwwcWin]::PrintWindow($e.Window, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose(); $bmp.Save($env:WWWC_DPI_SHOT); $bmp.Dispose()
  }
  Assert-True ($high.Row -ge $before.Row * 1.3) "tree rows are taller at 144 DPI ($($before.Row) -> $($high.Row))"
  Assert-True ($high.Button -ge $before.Button * 1.3) "toolbar buttons are larger at 144 DPI ($($before.Button) -> $($high.Button))"
  Assert-True ([Math]::Abs($high.Column - $before.Column * 1.5) -le 2) "column width scales by 1.5 ($($before.Column) -> $($high.Column))"
  [WwwcList]::SendDpiChanged($e.Window, 96, $r.L, $r.T, $r.R, $r.B)
  Start-Sleep 1
  $back = & $measure
  Assert-Equal $before.Row $back.Row "tree row height back at 96 DPI"
  Assert-Equal $before.Button $back.Button "toolbar button size back at 96 DPI"
  Assert-True ([Math]::Abs($back.Column - $before.Column) -le 1) "column width back at 96 DPI ($($before.Column) -> $($back.Column))"
  # the toolbar is rebuilt twice: the standard images must keep their index
  Assert-Equal $before.CutImage $high.CutImage "image of the cut button at 144 DPI"
  Assert-Equal $before.CutImage $back.CutImage "image of the cut button back at 96 DPI"
  Assert-True (-not $e.Process.HasExited) "WWWC is running"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $ini = Read-Utf8Text (Join-Path $e.Data "user.ini")
  Assert-True ($ini -match "(?m)^TvIconSize=16\r?$") "icon size is saved at 96 DPI"
}


Test-Case "delete and undo an item with a clipboard record larger than 32 KiB" {
  $e = New-TestEnv "feat-longrecord" $BinDir
  $comment = "`u{3042}" * 12000
  $fields = (ItemLine "long comment" (Url "/longrecord")).Split("`t")
  $fields[11] = $comment
  Set-Items $e @($fields -join "`t")
  [void](Start-Wwwc $e)
  Select-ListItem $e "long comment"
  Send-Command $e $CMD_DELETE
  [void](Wait-Until { Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero } } 5)
  Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero } | ForEach-Object {
    [void][WwwcWin]::PostMessage($_.Handle, 0x0111, [IntPtr]6, [IntPtr]::Zero)
  }
  Assert-True (Wait-Until { -not ((Get-ListTitles $e) -contains "long comment") } 5) "long item moved to recycle bin"
  Send-Command $e $CMD_UNDO
  Assert-True (Wait-Until { (Get-ListTitles $e) -contains "long comment" } 5) "undo restored the long item"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $saved = (Read-Utf8Text $e.ItemFile).TrimEnd("`r", "`n").Split("`t")
  Assert-Equal $comment $saved[11] "the complete comment is preserved"
}


Test-Case "copy and paste an item whose drop file path exceeds MAX_PATH" {
  $e = New-TestEnv "feat-longtitle" $BinDir
  # 300 Japanese characters = 900 bytes of UTF-8: the HTTP DLL builds TempDir\~WWWC_Clip_Files\<title>.url
  # (about 950 bytes) in the caller's buffer, which was 536 bytes before
  $title = "`u{3042}" * 300
  Set-Items $e @(ItemLine $title (Url "/longtitle"))
  [void](Start-Wwwc $e)
  # the list text read back is shorter than the title: select it by the text shown
  [void](Wait-Until { @(Get-ListTitles $e | Where-Object { $_ -like "`u{3042}*" }).Count -gt 0 } 5)
  Select-ListItem $e (Get-ListTitles $e | Where-Object { $_ -like "`u{3042}*" } | Select-Object -First 1)
  Send-Command $e 40056                                         # ID_MENUITEM_COPY
  Start-Sleep 1
  Assert-True (-not $e.Process.HasExited) "WWWC is running after copy (exit code $(if ($e.Process.HasExited) { '0x{0:X8}' -f $e.Process.ExitCode }))"
  if (-not $e.Process.HasExited) {
    Assert-Equal (Url "/longtitle") ((Get-Clipboard -Raw) -replace "`r?`n$", "") "clipboard text is the URL"
    Send-Command $e 40060                                       # ID_MENUITEM_PASTE
    Assert-True (Wait-Until { @(Get-ListTitles $e).Count -ge 2 } 5) "pasted copy appears ($(@(Get-ListTitles $e).Count) items)"
    Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  }
}

## Item management: cut while checking, undo after a folder is deleted, duplicate items

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class WwwcKeys {
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  // DeleteItem reads the Shift key with GetAsyncKeyState (Shift+Delete deletes without the recycle bin)
  public static void Shift(bool down) { keybd_event(0x10, 0, down ? 0u : 2u, UIntPtr.Zero); }
}
"@

$CMD_CHECK = 40003          # ID_MENUITEM_CHECK
$CMD_CUT = 40055            # ID_MENUITEM_CUT
$CMD_PASTE = 40060          # ID_MENUITEM_PASTE
$ID_TREE = 1114             # WWWC_TREE

function Get-TreeHandle($Env) { [WwwcWin]::GetDlgItem($Env.Window, $ID_TREE) }

# Select the root folder in the tree (TVM_GETNEXTITEM TVGN_ROOT, TVM_SELECTITEM TVGN_CARET)
function Select-TreeRoot($Env, [string]$Marker) {
  $tv = Get-TreeHandle $Env
  $root = [WwwcWin]::SendMessage($tv, 0x110A, [IntPtr]0, [IntPtr]::Zero)
  [void][WwwcWin]::SendMessage($tv, 0x110B, [IntPtr]9, $root)
  if (-not (Wait-Until { (Get-ListTitles $Env) -contains $Marker } 5)) { throw "the root folder does not list '$Marker'" }
}

# Select the child folder of the root whose list contains $Marker
function Select-TreeFolder($Env, [string]$Marker) {
  $tv = Get-TreeHandle $Env
  $root = [WwwcWin]::SendMessage($tv, 0x110A, [IntPtr]0, [IntPtr]::Zero)
  $c = [WwwcWin]::SendMessage($tv, 0x110A, [IntPtr]4, $root)             # TVGN_CHILD
  while ($c -ne [IntPtr]::Zero) {
    [void][WwwcWin]::SendMessage($tv, 0x110B, [IntPtr]9, $c)
    if (Wait-Until { (Get-ListTitles $Env) -contains $Marker } 2) { return }
    $c = [WwwcWin]::SendMessage($tv, 0x110A, [IntPtr]1, $c)              # TVGN_NEXT
  }
  throw "no folder lists '$Marker'"
}

# Answer Yes in the confirmation message box
function Confirm-Yes($Env) {
  Assert-True (Wait-Until { Get-Dialogs $Env | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero } } 5) "confirmation shown"
  Get-Dialogs $Env | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero } | ForEach-Object {
    [void][WwwcWin]::PostMessage($_.Handle, 0x0111, [IntPtr]6, [IntPtr]::Zero)
  }
}

function Write-ItemFile([string]$Dir, [string[]]$Lines) {
  New-Item -ItemType Directory -Force $Dir | Out-Null
  [IO.File]::WriteAllBytes((Join-Path $Dir "Item.dat"), $script:Enc.GetBytes(($Lines -join "`r`n") + "`r`n"))
}

# Item.dat lines of every folder below the data folder except the root
function Get-OtherItemLines($Env) {
  @(Get-ChildItem $Env.Data -Recurse -Filter "Item.dat" | Where-Object { $_.Directory.FullName -ne $Env.Data } |
    ForEach-Object { (Read-Utf8Text $_.FullName) -split "`r`n" | Where-Object { $_ } })
}

Test-Case "cutting an item while it is checked and pasting it into another folder cancels its check" {
  $e = New-TestEnv "feat-cutcheck" $BinDir
  Set-Items $e @(ItemLine "checking" (Url "/slow-cutcheck"))
  $sub = Join-Path $e.Data "sub"
  Write-ItemFile $sub @(ItemLine "marker" (Url "/marker"))
  $m = Get-ServerMark
  [void](Start-Wwwc $e)
  Select-ListItem $e "checking"
  Send-Command $e $CMD_CHECK
  Assert-True (Wait-Until { (Get-ServerLog $m) -match "^GET /slow-cutcheck " } 10) "the check started"
  Select-ListItem $e "checking"
  Send-Command $e $CMD_CUT
  Start-Sleep -Milliseconds 500
  Select-TreeFolder $e "marker"
  Select-ListItem $e "marker"
  Send-Command $e $CMD_PASTE
  Assert-True (Wait-Until { (Get-ListTitles $e) -contains "checking" } 5) "the item is pasted into the other folder"
  # the check timer (TimeoutItem) runs every second and used to read the freed item
  Start-Sleep 3
  Assert-True (-not $e.Process.HasExited) "WWWC is running (exit code $(if ($e.Process.HasExited) { '0x{0:X8}' -f $e.Process.ExitCode }))"
  if (-not $e.Process.HasExited) { Assert-Equal 0 (Stop-Wwwc $e) "exit code" }
  Assert-True ((Read-Utf8Text (Join-Path $sub "Item.dat")) -match "(?m)^checking`t") "the item is saved in the other folder"
  Assert-True (-not ((Read-Utf8Text $e.ItemFile) -match "(?m)^checking`t")) "the item is removed from the first folder"
}

Test-Case "undo after the folder it refers to was deleted does nothing" {
  $e = New-TestEnv "feat-undostale" $BinDir
  Set-Items $e @(ItemLine "top" (Url "/top"))
  $sub = Join-Path $e.Data "sub"
  Write-ItemFile $sub @(ItemLine "inner" (Url "/inner"))
  [void](Start-Wwwc $e)
  # delete the item of the folder (to the recycle bin): undo moves it back to the folder
  Select-TreeFolder $e "inner"
  Select-ListItem $e "inner"
  Send-Command $e $CMD_DELETE
  Confirm-Yes $e
  Assert-True (Wait-Until { -not ((Get-ListTitles $e) -contains "inner") } 5) "the item is moved to the recycle bin"
  # delete the folder itself with Shift+Delete (no undo information is recorded)
  Select-TreeRoot $e "top"
  Select-ListItem $e "sub"
  [WwwcKeys]::Shift($true)
  try {
    Send-Command $e $CMD_DELETE
    [void](Wait-Until { Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 6) -ne [IntPtr]::Zero } } 5)
  } finally { [WwwcKeys]::Shift($false) }
  Confirm-Yes $e
  Assert-True (Wait-Until { -not (Test-Path -LiteralPath $sub) } 5) "the folder is deleted"
  Assert-Equal 0 @(Get-ChildItem $e.Data -Recurse -Directory -Filter "sub").Count "Shift+Delete deletes the folder without the recycle bin"
  Send-Command $e $CMD_UNDO
  Start-Sleep 2
  Assert-True (-not $e.Process.HasExited) "WWWC is running (exit code $(if ($e.Process.HasExited) { '0x{0:X8}' -f $e.Process.ExitCode }))"
  if (-not $e.Process.HasExited) { Assert-Equal 0 (Stop-Wwwc $e) "exit code" }
  Assert-True (-not (Test-Path -LiteralPath $sub)) "the deleted folder is not created again"
  Assert-Equal @("top") @(Get-SavedTitles $e) "the root folder is unchanged"
  Assert-True (@(Get-OtherItemLines $e | Where-Object { $_ -like "inner`t*" }).Count -eq 1) "the item stays in the recycle bin"
}

foreach ($deleted in @("111", "222")) {
  Test-Case "deleting one of two items with the same title and URL keeps the other ($deleted deleted)" {
    $e = New-TestEnv "feat-dup$deleted" $BinDir
    $lines = foreach ($size in "111", "222") { $f = (ItemLine "dup" (Url "/dup")).Split("`t"); $f[2] = $size; $f -join "`t" }
    Set-Items $e $lines
    [void](Start-Wwwc $e)
    Select-ListItem $e "dup"
    $lv = Get-ListHandle $e
    $row = -1
    for ($i = 0; $i -lt [WwwcList]::Count($lv); $i++) {
      for ($c = 0; $c -lt 5; $c++) { if ([WwwcList]::GetText($lv, $i, $c) -eq $deleted) { $row = $i } }
    }
    Assert-True ($row -ge 0) "the item with size $deleted is listed"
    if ($row -ge 0) {
      [WwwcList]::Select($lv, $row)
      Start-Sleep -Milliseconds 200
      Send-Command $e $CMD_DELETE
      Confirm-Yes $e
      Assert-True (Wait-Until { @(Get-ListTitles $e | Where-Object { $_ -eq "dup" }).Count -eq 1 } 5) "one item is left"
    }
    Assert-Equal 0 (Stop-Wwwc $e) "exit code"
    $kept = if ($deleted -eq "111") { "222" } else { "111" }
    $root = @((Read-Utf8Text $e.ItemFile) -split "`r`n" | Where-Object { $_ })
    Assert-Equal 1 $root.Count "one item is saved in the folder"
    Assert-Equal $kept ($root[0].Split("`t")[2]) "the item that was not deleted is kept"
    $bin = @(Get-OtherItemLines $e | Where-Object { $_ -like "dup`t*" })
    Assert-True ($bin.Count -eq 1 -and $bin[0].Split("`t")[2] -eq $deleted) "the deleted item is in the recycle bin ($($bin -join ' | '))"
  }
}

Test-Case "negative status values are saved as 0 within the item record" {
  $e = New-TestEnv "feat-negstatus" $BinDir
  # ItemSize counts one digit for Status and CheckSt; "-1" used to write past the save buffer
  $fields = (ItemLine "negative" (Url "/negative") -Status "-1").Split("`t")
  $fields[12] = "-1"
  Set-Items $e @(($fields -join "`t"), (ItemLine "after" (Url "/after")))
  [void](Start-Wwwc $e)
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $lines = @((Read-Utf8Text $e.ItemFile) -split "`r`n")
  Assert-Equal "" $lines[-1] "Item.dat ends with a line break"
  $saved = $lines[0].Split("`t")
  Assert-Equal 16 $saved.Count "the record keeps all 16 fields"
  Assert-Equal "0" $saved[4] "Status is saved as 0"
  Assert-Equal "0" $saved[12] "CheckSt is saved as 0"
  Assert-True ($lines[1] -like "after`t*") "the next item is saved intact"
  Assert-True (-not (Test-Path -LiteralPath (Join-Path $e.Data "Item.da~"))) "no temporary file is left"
}

Test-Case "a tab entered in the item title does not shift the saved fields" {
  $e = New-TestEnv "feat-tabtitle" $BinDir
  Set-Items $e @(ItemLine "tabbed" (Url "/tabbed"))
  [void](Start-Wwwc $e)
  Select-ListItem $e "tabbed"
  Send-Command $e $CMD_PROP
  [void](Wait-Until { $script:sheetWin = Get-Dialogs $e | Select-Object -First 1; $null -ne $script:sheetWin } 10)
  $sheet = $script:sheetWin
  Assert-True ($sheet -and $sheet.Title -like "tabbed*") "item property sheet opened ('$($sheet.Title)')"
  if ($sheet -and $sheet.Title -like "tabbed*") {
    $page = [WwwcWin]::SendMessage($sheet.Handle, 0x0400 + 118, [IntPtr]::Zero, [IntPtr]::Zero)   # PSM_GETCURRENTPAGEHWND
    # a single-line edit keeps a pasted tab
    [void][WwwcWin]::SendMessageText([WwwcWin]::GetDlgItem($page, 1000), 0x000C, [IntPtr]::Zero, "tab`tbed")   # IDC_EDIT_TITLE of wwwc.dll
    [void][WwwcWin]::PostMessage([WwwcWin]::GetDlgItem($sheet.Handle, 1), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)   # OK
    Assert-True (Wait-Until { -not [WwwcWin]::IsWindow($sheet.Handle) } 5) "property sheet closed"
  }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $saved = (Read-Utf8Text $e.ItemFile).TrimEnd("`r", "`n").Split("`t")
  Assert-Equal 16 $saved.Count "the record keeps all 16 fields"
  Assert-Equal "tab bed" $saved[0] "the tab is saved as a space"
  Assert-Equal (Url "/tabbed") $saved[1] "the URL stays in its field"
}

Test-Case "a cancelled Windows shutdown does not close WWWC" {
  $e = New-TestEnv "feat-endsession" $BinDir
  Set-Items $e @(ItemLine "page" (Url "/target"))
  [void](Start-Wwwc $e)
  # WM_ENDSESSION with wParam FALSE: another application cancelled the shutdown
  [void][WwwcWin]::SendMessage($e.Window, 0x0016, [IntPtr]::Zero, [IntPtr]::Zero)
  Start-Sleep 2
  Assert-True (-not $e.Process.HasExited) "WWWC is still running"
  Assert-True ([WwwcWin]::IsWindow($e.Window)) "the main window still exists"
  if (-not $e.Process.HasExited) { Assert-Equal 0 (Stop-Wwwc $e) "exit code" }
}

Test-Case "the folder path column in the main list shows the path" {
  $e = New-TestEnv "feat-pathcol" $BinDir
  Set-Items $e @(ItemLine "page" (Url "/target"))
  # column 11 (folder path) works in the update message and find windows; the
  # main list read the start of the item as a string for it and crashed
  [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"), "[LISTVIEW]`r`nLvStyle=1`r`nLvColumn=`"2,3,11,`"`r`n", $script:Enc)
  [void](Start-Wwwc $e)
  Start-Sleep 1
  Assert-True (-not $e.Process.HasExited) "WWWC is running (exit code $(if ($e.Process.HasExited) { '0x{0:X8}' -f $e.Process.ExitCode }))"
  if (-not $e.Process.HasExited) {
    $lv = Get-ListHandle $e
    $row = [array]::IndexOf(@(Get-ListTitles $e), "page")
    Assert-True ($row -ge 0) "the item is listed"
    if ($row -ge 0) {
      $path = [WwwcList]::GetText($lv, $row, 3)
      Assert-True ($path -like "\*") "the path column shows the folder path ('$path')"
    }
    Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  }
}

Test-Case "FILE protocol compares full 64-bit sizes on sparse files" {
  $e = New-TestEnv "feat-file64" $BinDir
  $file = Join-Path $e.Dir "large.bin"
  [WwwcWin]::CreateSparseFile($file, 2147483655)
  $fields = (ItemLine "large file" "file:///$file").Split("`t")
  $fields[2] = "2147483655"
  $fields[9] = "1;;0"    # FILE: ignore date, compare size
  Set-Items $e @($fields -join "`t")
  [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"),
    "[PROTOCOL]`r`nProtocolCnt=2`r`ntitle-0=HTTP`r`nDLL-0=wwwc.dll`r`nFuncHeader-0=HTTP_`r`nUpIconIndex-0=-1`r`ntitle-1=FILE`r`nDLL-1=wwwc.dll`r`nFuncHeader-1=FILE_`r`nUpIconIndex-1=-1`r`n[UPMSG]`r`nUPSnd=0`r`nUPMsg=0`r`n", $script:Enc)
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK; Start-Sleep 2
  Assert-Equal 0 (Stop-Wwwc $e) "exit code (above 2 GiB)"
  $first = Get-SavedItem $e "large file"
  Assert-Equal "2147483655" $first.Size "size above 2 GiB is positive"
  Assert-True (($first.Status -band 1) -eq 0) "unchanged size is not marked updated"
  $stream = [IO.File]::OpenWrite($file)
  try { $stream.SetLength(6442450951) } finally { $stream.Dispose() }
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK; Start-Sleep 2
  Assert-Equal 0 (Stop-Wwwc $e) "exit code (above 4 GiB)"
  $second = Get-SavedItem $e "large file"
  Assert-Equal "6442450951" $second.Size.TrimEnd("*") "upper 32 bits are preserved"
  Assert-True (($second.Status -band 1) -ne 0) "a 4 GiB size increase is detected without date comparison"
}
