#Requires -Version 7
# (Compress-Archive of Windows PowerShell 5.1 writes the zip entries with
# backslashes, which other unzip tools take as part of the file name)
<#
.SYNOPSIS
  Builds the distribution zip of WWWC for one platform.

.DESCRIPTION
  Collects WWWC.exe, wwwc.dll, the user documentation from WWWC\ and the
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

$m = Select-String -Path (Join-Path $root "version.h") -Pattern '#define\s+WWWC_VERSION_STR\s+"([^"]+)"'
if (-not $m) { throw "WWWC_VERSION_STR not found in version.h" }
$version = $m.Matches[0].Groups[1].Value

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
foreach ($f in "readme.txt", "readme_en.txt", "wwwc.html", "wwwc_en.html", "ini_sample.txt", "filter_sample.txt") {
  Copy-Item (Join-Path $root "WWWC\$f") $stage
}
Copy-Item (Join-Path $root "WWWC\manual") $stage -Recurse
Copy-Item (Join-Path $root "CHANGELOG.md") $stage

# win32-darkmodelib (MPL-2.0, parts MIT) and the code it is derived from
$dm = Join-Path $root "third_party\darkmodelib"
Copy-Item (Join-Path $dm "LICENSE.md") (Join-Path $stage "licenses\darkmodelib-LICENSE.md")
Copy-Item (Join-Path $dm "LICENSE-MIT.md") (Join-Path $stage "licenses\darkmodelib-LICENSE-MIT.md")
Get-ChildItem (Join-Path $dm "docs") -Filter "LICENSE-*.md" | ForEach-Object {
  Copy-Item $_.FullName (Join-Path $stage "licenses\darkmodelib-$($_.Name)")
}
# the source and version of the third-party code (stops when darkmodelib is
# not a checkout of its own or has local changes)
& (Join-Path $PSScriptRoot "third-party-notices.ps1") -Version $version -OutFile (Join-Path $stage "licenses\THIRD-PARTY-NOTICES.txt")

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host $zip
