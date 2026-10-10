## UTF-8: conversion of Shift_JIS data from older versions, pages in several
## character sets, the source view, filters and passwords from older versions.

param([string]$BinDir)

$TitleJa = "`u{65E5}`u{672C}`u{8A9E}`u{306E}`u{30BF}`u{30A4}`u{30C8}`u{30EB}"                 # same as test_server.py
$TitleUtf8 = "$TitleJa `u{1F600} `u{4E2D}`u{6587} `u{D55C}`u{AD6D}`u{C5B4}"
$PageJa = "`u{65E5}`u{672C}`u{8A9E}`u{306E}`u{30DA}`u{30FC}`u{30B8}"                          # a Japanese item name
$FolderJa = "`u{30D5}`u{30A9}`u{30EB}`u{30C0}"
$PassJa = "`u{30D1}`u{30B9}`u{30EF}`u{30FC}`u{30C9}"

function Get-LegacyPassword([string]$Plain) { -join ($script:Sjis.GetBytes($Plain) | ForEach-Object { '{0:x2}' -f ($_ -bxor 0xFF) }) }

function Read-Utf8([string]$Path) { $script:Enc.GetString([IO.File]::ReadAllBytes($Path)) }

Test-Case "Shift_JIS data of older versions is converted to UTF-8 on the first start" {
  $e = New-TestEnv "utf8-migrate" $BinDir
  $sub = Join-Path $e.Data $FolderJa
  New-Item -ItemType Directory -Force $sub | Out-Null
  $items = $script:Sjis.GetBytes((ItemLine $PageJa (Url "/a")) + "`r`n")
  $subItems = $script:Sjis.GetBytes((ItemLine "sub-$PageJa" (Url "/b")) + "`r`n")
  $userIni = $script:Sjis.GetBytes("# $PageJa`r`n[GENERAL]`r`n")
  $wwwcIni = $script:Sjis.GetBytes("[HTTP]`r`nBrowserPath=C:\$FolderJa\browser.exe`r`n")
  [IO.File]::WriteAllBytes($e.ItemFile, $items)
  [IO.File]::WriteAllBytes((Join-Path $sub "Item.dat"), $subItems)
  [IO.File]::WriteAllBytes((Join-Path $e.Data "user.ini"), $userIni)
  [IO.File]::WriteAllBytes((Join-Path $e.Bin "wwwc.ini"), $wwwcIni)

  [void](Start-Wwwc $e)
  Assert-True ((Get-ListTitles $e) -contains $PageJa) "the Japanese item name is shown ($((Get-ListTitles $e) -join ', '))"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"

  Assert-Equal $PageJa @(Get-SavedTitles $e)[0] "Item.dat is UTF-8"
  Assert-True ((Read-Utf8 (Join-Path $sub "Item.dat")) -like "sub-$PageJa`t*") "Item.dat in a subfolder is UTF-8"
  Assert-True ((Read-Utf8 (Join-Path $e.Data "user.ini")) -like "# $PageJa*") "user.ini is UTF-8"
  Assert-True ((Read-Utf8 (Join-Path $e.Data "user.ini")) -match "CharSetUTF8=1") "user.ini records the conversion"
  Assert-True ((Read-Utf8 (Join-Path $e.Bin "wwwc.ini")) -match [regex]::Escape("BrowserPath=C:\$FolderJa\browser.exe")) "wwwc.ini is UTF-8"
  Assert-True ((Read-Utf8 (Join-Path $e.Bin "general.ini")) -match "CharSetUTF8=1") "general.ini records the conversion"

  $backup = "$($e.Data)_sjis_backup"
  Assert-True (Test-Path (Join-Path $backup "Item.dat")) "Item.dat is backed up"
  if (Test-Path (Join-Path $backup "Item.dat")) {
    Assert-True ([Linq.Enumerable]::SequenceEqual([byte[]][IO.File]::ReadAllBytes((Join-Path $backup "Item.dat")), [byte[]]$items)) "backup has the original bytes"
  }
  Assert-True (Test-Path (Join-Path $backup "$FolderJa\Item.dat")) "Item.dat in a subfolder is backed up"
  Assert-True (Test-Path (Join-Path $e.Bin "settings_sjis_backup\wwwc.ini")) "wwwc.ini is backed up"

  # a second start must not convert again
  [void](Start-Wwwc $e)
  Assert-True ((Get-ListTitles $e) -contains $PageJa) "the item name is still right after the second start"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code (second start)"
  Assert-Equal $PageJa @(Get-SavedTitles $e)[0] "Item.dat is unchanged after the second start"
}

Test-Case "get title of pages in UTF-8, Shift_JIS, EUC-JP and ISO-2022-JP" {
  $e = New-TestEnv "utf8-titles" $BinDir
  $charsets = "utf8", "sjis", "euc", "jis"
  Set-Items $e @($charsets | ForEach-Object { ItemLine "t-$_" (Url "/title-$_") })
  [void](Start-Wwwc $e)
  foreach ($c in $charsets) {
    $expected = if ($c -eq "utf8") { $TitleUtf8 } else { $TitleJa }
    # several pages have the same title: count the items with it
    $before = @(Get-ListTitles $e | Where-Object { $_ -eq $expected }).Count
    Select-ListItem $e "t-$c"; Send-Command $e ($CMD_ACTION + 5)
    $ok = Wait-Until { -not ((Get-ListTitles $e) -contains "t-$c") } 10
    $after = @(Get-ListTitles $e | Where-Object { $_ -eq $expected }).Count
    Assert-True ($ok -and $after -eq $before + 1) "$c page title is '$expected' (list: $((Get-ListTitles $e) -join ', '))"
  }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-True ((Get-SavedTitles $e) -contains $TitleUtf8) "the UTF-8 title with emoji, Chinese and Korean is saved"
}

Test-Case "source view of a UTF-8 page" {
  $e = New-TestEnv "utf8-source" $BinDir
  Set-Items $e @(ItemLine "src" (Url "/title-utf8"))
  [void](Start-Wwwc $e)
  Select-ListItem $e "src"; Send-Command $e ($CMD_ACTION + 4)
  $view = $null
  [void](Wait-Until { $script:v = Get-Dialogs $e | Where-Object { [WwwcWin]::GetDlgItem($_.Handle, 100) -ne [IntPtr]::Zero } | Select-Object -First 1; $null -ne $script:v } 10)
  $view = $script:v
  Assert-True ($null -ne $view) "source view opened"
  if ($view) {
    $text = [WwwcWin]::GetTextW([WwwcWin]::GetDlgItem($view.Handle, 100))
    Assert-True ($text.Contains("<title>$TitleUtf8</title>")) "source view shows the characters ('$($text.Substring(0, [Math]::Min(80, $text.Length)))')"
  }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "filter on a Shift_JIS page that becomes longer in UTF-8" {
  $e = New-TestEnv "utf8-filter" $BinDir
  # D: remove <p>...</p>; the rest (3000 Japanese characters) grows from 6000 to 9000 bytes
  [IO.File]::WriteAllText((Join-Path $e.Bin "filter.txt"), "D`t$(Url '/filter-sjis')`t<p>`t</p>`r`n", $script:Enc)
  Set-Items $e @(ItemLine "filtered" (Url "/filter-sjis"))
  $m = Get-ServerMark
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  [void](Wait-Until { (Get-ServerLog $m) -match "^GET /filter-sjis " } 15)
  Start-Sleep 2
  Assert-True (-not $e.Process.HasExited) "WWWC is running"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $expected = $script:Enc.GetByteCount("<html><body><div>" + ("`u{3042}" * 3000) + "</div></body></html>")
  Assert-Equal "$expected" (Get-SavedItem $e "filtered").Size "size after the filter (UTF-8 bytes)"
}

Test-Case "Japanese passwords saved by older versions" {
  $e = New-TestEnv "utf8-password" $BinDir
  Add-Type -AssemblyName System.Security
  # 1.2.0 encrypted the Shift_JIS bytes of the password with DPAPI ("DP1:")
  $dp1 = "DP1:" + [Convert]::ToBase64String([Security.Cryptography.ProtectedData]::Protect(
      $script:Sjis.GetBytes($PassJa), [Text.Encoding]::ASCII.GetBytes("WWWC password"), [Security.Cryptography.DataProtectionScope]::LocalMachine))
  Set-Items $e @(
    (ItemLine "legacy" (Url "/auth-legacy") "1;;0;;;;;;1;;tester;;$(Get-LegacyPassword $PassJa);;;;0"),
    (ItemLine "dp1" (Url "/auth-dp1" "127.0.0.2") "1;;0;;;;;;1;;tester;;$dp1;;;;0"))
  $m = Get-ServerMark
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  [void](Wait-Until { @((Get-ServerLog $m) -match "^GET /auth-").Count -ge 2 } 15)
  Start-Sleep 1
  $log = Get-ServerLog $m
  Assert-True ($log -match [regex]::Escape("GET /auth-legacy auth='tester:$PassJa'")) "old format password ($log)"
  Assert-True ($log -match [regex]::Escape("GET /auth-dp1 auth='tester:$PassJa'")) "1.2.0 format password ($log)"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}

Test-Case "long Japanese folder and item names" {
  $e = New-TestEnv "utf8-long" $BinDir
  # three nested folders of 50 Japanese characters: about 450 bytes in UTF-8
  # (the folder path used to be limited to 240 bytes). Windows still limits a path to
  # MAX_PATH (260) characters, so the folders are not longer
  $name = -join (1..50 | ForEach-Object { [char](0x3042 + ($_ % 80)) })
  $deep = $e.Data
  foreach ($i in 1..3) { $deep = Join-Path $deep "$i$name"; New-Item -ItemType Directory -Force $deep | Out-Null }
  $longTitle = -join (1..200 | ForEach-Object { [char](0x4E00 + $_) })        # 200 characters, 600 bytes
  [IO.File]::WriteAllBytes((Join-Path $deep "Item.dat"), $script:Enc.GetBytes((ItemLine "deep" (Url "/deep-item")) + "`r`n"))
  Set-Items $e @(ItemLine $longTitle (Url "/long-title"))
  $m = Get-ServerMark
  [void](Start-Wwwc $e); Send-Command $e $CMD_ALLCHECK
  Assert-True (Wait-Until { @((Get-ServerLog $m) -match "^GET /(deep-item|long-title) ").Count -ge 2 } 20) "items in the deep folder and with the long name are checked ($(Get-ServerLog $m))"
  Start-Sleep 2
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $deepItem = (Read-Utf8 (Join-Path $deep "Item.dat")) -split "`t"
  Assert-Equal "67" $deepItem[2] "the item in the deep folder is saved with the check result"
  Assert-True ((Get-SavedTitles $e) -contains $longTitle) "the 200-character name is saved in full"
}

Test-Case "Japanese names in the find window and with a font chosen in the settings" {
  # list views convert ANSI text with the code page of the character set of their font:
  # the dialog font and a font chosen in the settings are Japanese (Shift_JIS)
  $e = New-TestEnv "utf8-listfont" $BinDir
  Set-Items $e @(ItemLine $PageJa (Url "/a"))
  [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"), "[WINDOW]`r`nListFontName=MS UI Gothic`r`nListFontSize=9`r`nListFontCharSet=128`r`n", $script:Enc)
  [void](Start-Wwwc $e)
  Assert-True ((Get-ListTitles $e) -contains $PageJa) "the Japanese item name is shown with the font (Shift_JIS character set) ($((Get-ListTitles $e) -join ', '))"
  Send-Command $e 40111                                                    # ID_MENUITEM_SERACH
  [void](Wait-Until { $script:fw = Get-Dialogs $e | Where-Object { [WwwcWin]::FindChildById($_.Handle, 1116) -ne [IntPtr]::Zero } | Select-Object -First 1; $null -ne $script:fw } 10)
  $fw = $script:fw
  Assert-True ($null -ne $fw) "find window opened"
  if ($fw) {
    [void][WwwcWin]::SendMessageTextW([WwwcWin]::FindChildById($fw.Handle, 1116), 0x000C, [IntPtr]::Zero, $PageJa)
    [void][WwwcWin]::PostMessage($fw.Handle, 0x0111, [IntPtr]1, [IntPtr]::Zero)       # IDOK: start
    $lv = [WwwcWin]::GetDlgItem($fw.Handle, $script:ID_LIST)
    Assert-True (Wait-Until { [WwwcList]::Count($lv) -ge 1 } 10) "the item is found"
    Assert-Equal $PageJa ([WwwcList]::GetText($lv, 0, 0)) "the Japanese item name in the find window"
  }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
}


Test-Case "failed Shift_JIS migration preserves data and retries after unlocking" {
  $e = New-TestEnv "utf8-migrate-locked" $BinDir
  $original = $script:Sjis.GetBytes((ItemLine $PageJa (Url "/a")) + "`r`n")
  [IO.File]::WriteAllBytes($e.ItemFile, $original)
  $held = [IO.File]::Open($e.ItemFile, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
  try {
    $e.Process = Start-Process (Join-Path $e.Bin "WWWC.exe") -ArgumentList "/d:`"$(Join-Path $e.Dir 'data')`"" -PassThru
    [void]$script:Started.Add($e.Process)
    Assert-True (Wait-Until { Get-ProcessWindows $e.Process.Id | Where-Object { $_.Class -eq "#32770" } } 10) "migration failure is reported"
    Assert-True (-not (Get-ProcessWindows $e.Process.Id | Where-Object Class -eq "WWWCWindowClass")) "failed migration does not load legacy data"
    Assert-True ([Linq.Enumerable]::SequenceEqual([byte[]][IO.File]::ReadAllBytes($e.ItemFile), [byte[]]$original)) "locked source is unchanged"
    $ini = Join-Path $e.Data "user.ini"
    Assert-True (-not (Test-Path $ini) -or -not ((Read-Utf8 $ini) -match "CharSetUTF8=1")) "failed migration is not marked complete"
    Get-ProcessWindows $e.Process.Id | Where-Object Class -eq "#32770" | ForEach-Object {
      [void][WwwcWin]::PostMessage($_.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    }
    Assert-True ($e.Process.WaitForExit(10000)) "failed startup exits"
  } finally { $held.Dispose() }
  [void](Start-Wwwc $e)
  Assert-True ((Get-ListTitles $e) -contains $PageJa) "retry converts the Japanese item"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code after retry"
  Assert-True ((Read-Utf8 (Join-Path $e.Data "user.ini")) -match "CharSetUTF8=1") "successful retry is marked complete"
  Assert-True ([Linq.Enumerable]::SequenceEqual([byte[]][IO.File]::ReadAllBytes("$($e.Data)_sjis_backup\Item.dat"), [byte[]]$original)) "retry preserves the original backup"
}


Test-Case "Unicode numeric HTML entities and invalid references in page titles" {
  $e = New-TestEnv "utf8-entities" $BinDir
  Set-Items $e @(ItemLine "entities" (Url "/title-entities"))
  [void](Start-Wwwc $e)
  Select-ListItem $e "entities"; Send-Command $e ($CMD_ACTION + 5)
  $expected = "`u{65E5}`u{672C} `u{1F600} `u{1F600} &#xD800; &#0; &#1114112; &#x;"
  Assert-True (Wait-Until { (Get-ListTitles $e) -contains $expected } 10) "numeric references decode to Unicode while invalid references stay literal"
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  Assert-True ((Get-SavedTitles $e) -contains $expected) "decoded Unicode title is saved"
}
