## Loading local data files (Item.dat, user.ini). No network access.

param([string]$BinDir)

function Test-Load([string]$Name, [byte[]]$ItemData, [string]$UserIni, [string[]]$Expected) {
  $e = New-TestEnv $Name $BinDir
  [IO.File]::WriteAllBytes($e.ItemFile, $ItemData)
  if ($UserIni) { [IO.File]::WriteAllText((Join-Path $e.Data "user.ini"), $UserIni, $script:Sjis) }
  $started = Start-Wwwc $e
  Assert-True $started ("WWWC exited at startup (exit code 0x{0:X8})" -f $e.Process.ExitCode)
  if (-not $started) { return }
  $titles = Get-ListTitles $e
  foreach ($t in $Expected) { Assert-True ($titles -contains $t) "item '$t' is shown (list: $($titles -join ', '))" }
  Assert-Equal 0 (Stop-Wwwc $e) "exit code"
  $saved = Get-SavedTitles $e
  Assert-Equal ($Expected -join ",") ($saved -join ",") "items saved to Item.dat"
}

$a = ItemLine "a" (Url "/a"); $b = ItemLine "b" (Url "/b"); $c = ItemLine "c" (Url "/c")

Test-Case "Item.dat whose last line has no line break" {
  Test-Load "parse-nonewline" ($script:Sjis.GetBytes("$a`r`n$b`r`n$c")) $null @("a", "b", "c")
}

Test-Case "Item.dat with CR-only line breaks" {
  Test-Load "parse-cr" ($script:Sjis.GetBytes("$a`r$b`r$c`r")) $null @("a", "b", "c")
}

Test-Case "Item.dat with a 4000-character Status field" {
  Test-Load "parse-longstatus" ($script:Sjis.GetBytes((ItemLine "a" (Url "/a") "0;;0;;;;;;0;;;;;;;;0" ("1" * 4000)) + "`r`n$b`r`n")) $null @("a", "b")
}

Test-Case "user.ini with a 4000-character comment line" {
  Test-Load "parse-inicomment" ($script:Sjis.GetBytes("$a`r`n")) ("#" + ("x" * 4000) + "`r`n[GENERAL]`r`n") @("a")
}

Test-Case "user.ini with a 4000-character key without '='" {
  Test-Load "parse-inikey" ($script:Sjis.GetBytes("$a`r`n")) ("[GENERAL]`r`n" + ("k" * 4000) + "`r`n") @("a")
}
