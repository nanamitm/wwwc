## Upstream archiving remains functional with this fork's extended Option2 layout.
param([string]$BinDir)

Test-Case 'upstream archive stores the checked page beside the user data folder' {
  $e = New-TestEnv 'archive-page' $BinDir
  Set-Items $e @(ItemLine 'archived page' (Url '/target'))
  [void](Start-Wwwc $e)
  Send-Command $e $CMD_ALLCHECK
  $archive = $e.Data + '.archive'
  $saved = Wait-Until {
    @(Get-ChildItem -LiteralPath $archive -Filter '*.html' -File -Recurse -ErrorAction SilentlyContinue).Count -ge 2
  } 15
  Assert-True $saved 'archive page and index were written'
  if ($saved) {
    $pages = @(Get-ChildItem -LiteralPath $archive -Filter '*.html' -File -Recurse | Where-Object Name -ne 'index.html')
    Assert-True (@($pages | Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -match 'target page' }).Count -gt 0) 'archived HTML contains the checked page'
  }
  Assert-Equal 0 (Stop-Wwwc $e) 'exit code'
  Assert-Equal '' (Get-SavedItem $e 'archived page').ErrStatus 'check succeeded'
}

Test-Case 'per-item archive disable keeps its field separate from the certificate exception' {
  $e = New-TestEnv 'archive-disabled' $BinDir
  $options = [string[]](@('0') * 15)
  $options[8] = '1'  # legacy fork: ignore certificate errors
  $options[9] = '1'  # upstream extension: do not archive
  Set-Items $e @(ItemLine 'not archived' (Url '/target') ($options -join ';;'))
  [void](Start-Wwwc $e)
  $mark = Get-ServerMark
  Send-Command $e $CMD_ALLCHECK
  Assert-True (Wait-Until { (Get-ServerLog $mark) -match '^GET /target ' } 10) 'page was checked'
  Start-Sleep 2
  Assert-Equal 0 (Stop-Wwwc $e) 'exit code'
  Assert-True (-not (Test-Path ($e.Data + '.archive'))) 'disabled item did not create an archive'
  $saved = Get-SavedItem $e 'not archived'
  Assert-Equal '' $saved.ErrStatus 'check succeeded'
  $savedOptions = $saved.Option2 -split ';;'
  Assert-Equal '1' $savedOptions[8] 'legacy certificate exception preserved'
  Assert-Equal '1' $savedOptions[9] 'archive disable preserved'
}
