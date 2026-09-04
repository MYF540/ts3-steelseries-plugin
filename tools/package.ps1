<#
.SYNOPSIS
    Baut das .ts3_plugin-Installationsarchiv.

.DESCRIPTION
    Ein .ts3_plugin ist ein ZIP-Archiv mit package.ini in der Wurzel und der DLL
    unter plugins/. Doppelklick installiert es im TeamSpeak-Client.

.EXAMPLE
    .\tools\package.ps1 -Preset release
#>

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'release'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

$config = if ($Preset -eq 'debug') { 'Debug' } else { 'RelWithDebInfo' }
$dll = Join-Path $repo "build\$Preset\bin\$config\ts3_steelseries.dll"
if (-not (Test-Path $dll)) {
    $dll = Join-Path $repo "build\$Preset\bin\ts3_steelseries.dll"
}
if (-not (Test-Path $dll)) {
    throw "DLL nicht gefunden. Zuerst bauen:  cmake --build --preset $Preset"
}

$ini = Join-Path $repo 'resources\package.ini'
if (-not (Test-Path $ini)) { throw "resources\package.ini fehlt" }

$version = ([regex]::Match((Get-Content $ini -Raw), '(?m)^Version\s*=\s*(.+)$')).Groups[1].Value.Trim()
if (-not $version) { throw "Version konnte aus package.ini nicht gelesen werden" }

$stage = Join-Path $env:TEMP "ts3ss-pkg-$([guid]::NewGuid())"
$dist  = Join-Path $repo 'dist'
New-Item -ItemType Directory -Path (Join-Path $stage 'plugins') -Force | Out-Null
New-Item -ItemType Directory -Path $dist -Force | Out-Null

Copy-Item $ini  (Join-Path $stage 'package.ini')
Copy-Item $dll  (Join-Path $stage 'plugins\ts3_steelseries.dll')

$out = Join-Path $dist "ts3-steelseries-plugin-$version.ts3_plugin"
if (Test-Path $out) { Remove-Item $out -Force }

# -DestinationPath erzeugt ein .zip; die Endung wird danach umbenannt, weil
# Compress-Archive keine freie Endung akzeptiert.
$tmpZip = "$out.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $tmpZip -Force
Move-Item $tmpZip $out
Remove-Item $stage -Recurse -Force

Write-Host "Paket erstellt: $out" -ForegroundColor Green
