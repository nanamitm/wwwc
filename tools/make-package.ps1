<#
.SYNOPSIS
  Builds the distribution zip of WWWC for one platform.

.DESCRIPTION
  Collects WWWC.exe, wwwc.dll, the user documentation from docs\ and the
  third-party licenses into wwwc-<version>-<platform>\ and zips it. The
  version is taken from version.h.

.EXAMPLE
  pwsh tools\make-package.ps1 -Platform x64 -BinDir x64\Release -OutDir dist
#>
param(
  [Parameter(Mandatory)][ValidateSet("x86", "x64")][string]$Platform,
  [Parameter(Mandatory)][string]$BinDir,
  [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot

$version = (Select-String -Path (Join-Path $root "version.h") -Pattern '#define\s+WWWC_VERSION_STR\s+"([^"]+)"').Matches[0].Groups[1].Value
if (-not $version) { throw "WWWC_VERSION_STR not found in version.h" }

$name = "wwwc-$version-$Platform"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage, (Join-Path $stage "licenses") | Out-Null

foreach ($f in "WWWC.exe", "wwwc.dll") {
  $src = Join-Path $BinDir $f
  if (-not (Test-Path $src)) { throw "$src not found (build Release $Platform first)" }
  Copy-Item $src $stage
}
foreach ($f in "readme.txt", "wwwc.html", "ini_sample.txt", "filter_sample.txt") {
  Copy-Item (Join-Path $root "docs\$f") $stage
}
Copy-Item (Join-Path $root "CHANGELOG.md") $stage

# win32-darkmodelib (MPL-2.0, parts MIT) and the code it is derived from
$dm = Join-Path $root "third_party\darkmodelib"
Copy-Item (Join-Path $dm "LICENSE.md") (Join-Path $stage "licenses\darkmodelib-LICENSE.md")
Copy-Item (Join-Path $dm "LICENSE-MIT.md") (Join-Path $stage "licenses\darkmodelib-LICENSE-MIT.md")
Get-ChildItem (Join-Path $dm "docs") -Filter "LICENSE-*.md" | ForEach-Object {
  Copy-Item $_.FullName (Join-Path $stage "licenses\darkmodelib-$($_.Name)")
}
$commit = (git -C $dm rev-parse HEAD).Trim()
@"
Third-party software in WWWC $version

win32-darkmodelib
  Used for the dark mode. Licensed under the Mozilla Public License 2.0
  (some files under the MIT License); see darkmodelib-*.md in this folder.
  Source code: https://github.com/ozone10/win32-darkmodelib
  Version used: commit $commit (not modified)

RSA Data Security, Inc. MD5 Message-Digest Algorithm
  Used for the MD5 check. See the notice at the end of readme.txt.
"@ | Set-Content -Path (Join-Path $stage "licenses\THIRD-PARTY-NOTICES.txt") -Encoding utf8

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host $zip
