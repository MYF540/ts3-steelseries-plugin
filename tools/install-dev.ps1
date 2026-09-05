<#
.SYNOPSIS
    Kopiert die gebaute DLL ins TeamSpeak-Plugin-Verzeichnis.

.DESCRIPTION
    Die Entwicklungsschleife bei einem In-Process-Plugin ist zaeh: bauen, kopieren,
    TeamSpeak neu starten. Dieses Skript uebernimmt die mittleren beiden Schritte
    und weist darauf hin, wenn TS3 die DLL noch gesperrt haelt.

.EXAMPLE
    .\tools\install-dev.ps1
    .\tools\install-dev.ps1 -Preset release -RestartTeamSpeak
#>

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'debug',

    [switch]$RestartTeamSpeak
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

$pluginDir = Join-Path $env:APPDATA 'TS3Client\plugins'
if (-not (Test-Path $pluginDir)) {
    throw "TeamSpeak-Plugin-Verzeichnis nicht gefunden: $pluginDir"
}

# Ein laufender Client sperrt die DLL nur, wenn das Plugin darin bereits GELADEN ist.
# Beim Erstinstallieren gibt es keine Sperre, also wird hier die Datei selbst gepruft
# statt bloss die Anwesenheit des Prozesses - sonst verlangt das Skript ohne Not, dass
# der Nutzer sein laufendes TeamSpeak beendet.
$target = Join-Path $pluginDir (Split-Path $dll -Leaf)

function Test-FileLocked([string]$Path) {
    if (-not (Test-Path $Path)) { return $false }
    try {
        $fs = [IO.File]::Open($Path, 'Open', 'Write', 'None')
        $fs.Close()
        return $false
    } catch { return $true }
}

$ts3 = Get-Process -Name 'ts3client_win64' -ErrorAction SilentlyContinue

if (Test-FileLocked $target) {
    if ($RestartTeamSpeak -and $ts3) {
        Write-Host "DLL ist gesperrt, TeamSpeak wird beendet..." -ForegroundColor Yellow
        $ts3 | Stop-Process
        $ts3 | Wait-Process -Timeout 15
        Start-Sleep -Milliseconds 500
    } else {
        throw "Das geladene Plugin sperrt $target.`nTeamSpeak beenden, oder -RestartTeamSpeak nutzen."
    }
}

Copy-Item $dll $target -Force
Write-Host "Kopiert nach $target" -ForegroundColor Green

if ($RestartTeamSpeak -and $ts3 -and $ts3.HasExited) {
    Start-Process $ts3.Path
    Write-Host "TeamSpeak neu gestartet." -ForegroundColor Green
} elseif ($ts3) {
    Write-Host "TeamSpeak laeuft noch - fuer einen Neustart ist es selbst zustaendig." -ForegroundColor Yellow
    Write-Host "Plugins werden nur beim Start eingelesen." -ForegroundColor Yellow
}

Write-Host "Aktivieren unter: Extras -> Optionen -> Addons"
