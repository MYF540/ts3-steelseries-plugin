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

# Die generierte Fassung, nicht die Vorlage: die Versionsnummer steht ausschliesslich
# in CMakeLists.txt und wird beim Konfigurieren eingesetzt. So kann die Version im
# Addon-Manager nicht von der der DLL abweichen.
$ini = Join-Path $repo "build\$Preset\generated\package.ini"
if (-not (Test-Path $ini)) {
    throw "Generierte package.ini fehlt: $ini`nZuerst konfigurieren:  cmake --preset $Preset"
}

$version = ([regex]::Match((Get-Content $ini -Raw), '(?m)^Version\s*=\s*(.+)$')).Groups[1].Value.Trim()
if (-not $version) { throw "Version konnte aus der generierten package.ini nicht gelesen werden" }

$dist = Join-Path $repo 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null

$out = Join-Path $dist "ts3-steelseries-plugin-$version.ts3_plugin"
if (Test-Path $out) { Remove-Item $out -Force }

# Bewusst NICHT Compress-Archive: das schreibt unter Windows PowerShell 5.1
# Backslashes in die Eintragsnamen ("plugins\ts3_steelseries.dll"). Die ZIP-Spezifikation
# verlangt Schraegstriche, und ob der TeamSpeak-Entpacker das verzeiht, ist nichts, was
# man in einem Release herausfinden moechte.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# Lizenztexte gehoeren ins Paket, nicht nur ins Repository: nlohmann/json ist
# header-only und damit in der DLL enthalten, und die MIT-Lizenz verlangt, dass ihr
# Hinweis jeder Weitergabe beiliegt. Sie liegen unter plugins/ts3_steelseries/ - dem
# ueblichen Ressourcenverzeichnis eines Plugins -, damit die Wurzel des Archivs genau
# das enthaelt, was TeamSpeak dort erwartet.
$extras = @(
    @{ Path = (Join-Path $repo 'LICENSE');                 Entry = 'plugins/ts3_steelseries/LICENSE.txt' }
    @{ Path = (Join-Path $repo 'THIRD-PARTY-NOTICES.md');  Entry = 'plugins/ts3_steelseries/THIRD-PARTY-NOTICES.md' }
)
foreach ($extra in $extras) {
    if (-not (Test-Path $extra.Path)) { throw "Lizenzdatei fehlt: $($extra.Path)" }
}

$archive = [System.IO.Compression.ZipFile]::Open($out, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    # Eintragsnamen explizit gesetzt, damit die Struktur unabhaengig vom lokalen
    # Dateisystem stimmt: package.ini in der Wurzel, DLL unter plugins/.
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $ini, 'package.ini',
        [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $dll, 'plugins/ts3_steelseries.dll',
        [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null

    foreach ($extra in $extras) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $extra.Path, $extra.Entry,
            [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally {
    $archive.Dispose()
}

Write-Host "Paket erstellt: $out" -ForegroundColor Green
Write-Host "Version $version" -ForegroundColor Green

# Gegenprobe: falsche Trennzeichen faenden hier auf, nicht erst beim Nutzer.
$check = [System.IO.Compression.ZipFile]::OpenRead($out)
try {
    foreach ($entry in $check.Entries) {
        if ($entry.FullName -match '\\') {
            throw "Eintrag enthaelt Backslash statt Schraegstrich: $($entry.FullName)"
        }
        Write-Host ("  {0,-32} {1,9:N0} Bytes" -f $entry.FullName, $entry.Length)
    }
}
finally {
    $check.Dispose()
}
