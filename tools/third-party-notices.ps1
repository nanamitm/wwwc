#Requires -Version 5.1
<#
.SYNOPSIS
  Writes THIRD-PARTY-NOTICES.txt for a WWWC package or the installer.

.DESCRIPTION
  Names the third-party code in WWWC, its license and where its source code
  is (the MPL 2.0 requires telling recipients of the executable how to get
  the source). The darkmodelib commit is read from its submodule; the script
  stops when the submodule is not a checkout of its own or has local changes,
  so that the notice does not state a wrong version or "not modified".
  Used by tools\make-package.ps1 (zip) and Installer\prepare.ps1 (installer).

.EXAMPLE
  tools\third-party-notices.ps1 -Version 1.5.2 -OutFile dist\wwwc\licenses\THIRD-PARTY-NOTICES.txt
#>
param(
  [Parameter(Mandatory)][string]$Version,
  [Parameter(Mandatory)][string]$OutFile
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$dm = Join-Path $root "third_party\darkmodelib"

# the submodule must be a checkout of its own: in a plain folder git would
# walk up and report the commit of WWWC itself
$top = git -C $dm rev-parse --show-toplevel 2>$null
if ($LASTEXITCODE -ne 0 -or -not $top -or
    [IO.Path]::GetFullPath($top.Trim()).TrimEnd('\', '/') -ne [IO.Path]::GetFullPath($dm).TrimEnd('\', '/')) {
  throw "third_party\darkmodelib is not a git checkout (run git submodule update --init)"
}
$commit = git -C $dm rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or -not $commit) { throw "could not read the darkmodelib commit" }
$commit = $commit.Trim()
$changes = git -C $dm status --porcelain
if ($LASTEXITCODE -ne 0) { throw "could not read the darkmodelib status" }
if ($changes) {
  throw "third_party\darkmodelib has local changes; the notice says the code is not modified:`n$($changes -join "`n")"
}

$text = @"
Third-party software in WWWC $Version

win32-darkmodelib
  Used for the dark mode. Licensed under the Mozilla Public License 2.0
  (some files under the MIT License); see darkmodelib-*.md in this folder.
  Source code: https://github.com/ozone10/win32-darkmodelib
  Version used: commit $commit (not modified)

RSA Data Security, Inc. MD5 Message-Digest Algorithm
  Used for the MD5 check. See the notice at the end of readme.txt.
"@ -replace "`r`n", "`n" -replace "`n", "`r`n"

New-Item -ItemType Directory -Force (Split-Path -Parent $OutFile) | Out-Null
[IO.File]::WriteAllText($OutFile, $text + "`r`n", (New-Object System.Text.UTF8Encoding $false))
