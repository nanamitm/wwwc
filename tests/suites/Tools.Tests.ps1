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
