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

# Ein laufender Client haelt die DLL gesperrt - Kopieren schlaegt dann mit einer
# wenig aussagekraeftigen Meldung fehl. Lieber vorher klar sagen, was los ist.
$ts3 = Get-Process -Name 'ts3client_win64' -ErrorAction SilentlyContinue
if ($ts3) {
    if ($RestartTeamSpeak) {
        Write-Host "TeamSpeak wird beendet..." -ForegroundColor Yellow
        $ts3 | Stop-Process
        $ts3 | Wait-Process -Timeout 15
        Start-Sleep -Milliseconds 500
    } else {
        throw "TeamSpeak laeuft und sperrt die DLL. Beenden, oder -RestartTeamSpeak nutzen."
    }
}

Copy-Item $dll $pluginDir -Force
Write-Host "Kopiert nach $pluginDir" -ForegroundColor Green

if ($RestartTeamSpeak -and $ts3) {
    Start-Process $ts3.Path
    Write-Host "TeamSpeak neu gestartet." -ForegroundColor Green
}

Write-Host "Aktivieren unter: Extras -> Optionen -> Addons"
