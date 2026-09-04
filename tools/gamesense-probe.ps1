<#
.SYNOPSIS
    Phase-0-Pruefung: Laesst sich das OLED der Arctis-Basisstation ueber das
    GameSense-SDK beschreiben?

.DESCRIPTION
    Testet der Reihe nach ALLE plausiblen device-type-Werte. Das ist noetig, weil
    ein bind_game_event auch dann HTTP 200 liefert, wenn der device-type auf gar
    kein angeschlossenes Geraet passt: GG speichert den Handler klaglos und zeigt
    die App sogar in der Oberflaeche an - nur passiert nichts.

    Genau dieser Fall trat beim ersten Durchlauf mit "screened-128x64" ein.
    Nach dem offiziellen Geraetekatalog (doc/api/standard-zones.md) existiert
    dieser Typ ueberhaupt nicht; dokumentiert sind nur:

        screened-128x36  Rival 700 / 710
        screened-128x40  Apex 7 / Apex Pro
        screened-128x48  Arctis Pro Wireless      (Vorgaengergeneration)
        screened-128x52  GameDAC / Arctis Pro

    Der aussichtsreichste Wert ist deshalb das generische "screened": Es passt
    laut Doku auf jedes Geraet mit Schirm, und GGs eigene Spielpakete binden
    genau so. Eine Auswertung von GG\apps\engine\db\database.db findet als
    Screen-Typen ausschliesslich "screened", "screened-2-lines" und
    "screened-3-lines-or-more" - durchweg auflosungsfrei.

    Dass die Basisstation App-Inhalte darstellen KANN, ist belegt: NowPlaying
    zeigt Titel, Interpret und Fortschrittsbalken, die Counter-Strike-2-App
    rotiert durch Match-Statistiken. Es geht hier also nur noch darum, den
    richtigen device-type zu finden.

    Nur die Augen entscheiden: HTTP 200 heisst "angenommen", nicht "sichtbar".
    Also auf die Basisstation schauen, nicht auf die Konsole.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\gamesense-probe.ps1

.EXAMPLE
    # Nur einen bestimmten Typ pruefen:
    .\tools\gamesense-probe.ps1 -DeviceTypes screened
#>

[CmdletBinding()]
param(
    [string[]]$DeviceTypes = @(
        'screened',
        'screened-3-lines-or-more',
        'screened-2-lines',
        'screened-128x64',
        'screened-128x48',
        'screened-128x52'
    ),
    [string]$Game        = 'TS3_OLED_PROBE',
    [int]   $HoldSeconds = 7
)

$ErrorActionPreference = 'Stop'

function Ask([string]$Question) {
    while ($true) {
        $a = Read-Host "  >> $Question [j/n]"
        if ($a -match '^(j|y)') { return $true }
        if ($a -match '^n')     { return $false }
    }
}

# --- Server finden ---------------------------------------------------------
Write-Host "`n=== GameSense-Server ===" -ForegroundColor Cyan

$corePropsPath = Join-Path $env:PROGRAMDATA 'SteelSeries\SteelSeries Engine 3\coreProps.json'
if (-not (Test-Path $corePropsPath)) {
    Write-Host "  coreProps.json nicht gefunden. Laeuft SteelSeries GG?" -ForegroundColor Red
    exit 1
}
$address = (Get-Content $corePropsPath -Raw | ConvertFrom-Json).address
$base = "http://$address"
Write-Host "  Adresse: $address" -ForegroundColor Green

function Invoke-GS([string]$Endpoint, $Body) {
    $json = $Body | ConvertTo-Json -Depth 12 -Compress
    Invoke-RestMethod -Uri "$base/$Endpoint" -Method Post `
                      -ContentType 'application/json' -Body $json -TimeoutSec 5
}

Invoke-GS 'game_metadata' @{
    game                         = $Game
    game_display_name            = 'TS3 OLED Probe'
    developer                    = 'ts3-steelseries-plugin'
    deinitialize_timer_length_ms = 15000
} | Out-Null
Write-Host "  game_metadata angenommen" -ForegroundColor Green

# --- Matrix ----------------------------------------------------------------
Write-Host "`n=== Matrix ueber $($DeviceTypes.Count) device-type-Werte ===" -ForegroundColor Cyan
Write-Host "  Jeder Typ wird $HoldSeconds s lang gesendet. Basisstation im Blick behalten." -ForegroundColor Yellow

$results = @()
$i = 0

foreach ($dt in $DeviceTypes) {
    $i++
    $evt = "PROBE$i"
    Write-Host "`n  [$i/$($DeviceTypes.Count)] device-type = $dt" -ForegroundColor White

    $bindOk = $true; $bindErr = ''
    try {
        Invoke-GS 'bind_game_event' @{
            game           = $Game
            event          = $evt
            value_optional = $true
            handlers       = @(
                @{
                    'device-type' = $dt
                    zone          = 'one'
                    mode          = 'screen'
                    datas         = @(
                        @{
                            lines = @(
                                @{ 'has-text' = $true; 'context-frame-key' = 'l1'; bold = $true },
                                @{ 'has-text' = $true; 'context-frame-key' = 'l2' }
                            )
                        }
                    )
                }
            )
        } | Out-Null
    }
    catch { $bindOk = $false; $bindErr = $_.Exception.Message }

    if (-not $bindOk) {
        Write-Host "      bind ABGELEHNT: $bindErr" -ForegroundColor Red
        $results += [pscustomobject]@{ DeviceType = $dt; Bind = 'abgelehnt'; Sichtbar = '-'; Notiz = $bindErr }
        continue
    }
    Write-Host "      bind angenommen, sende Frames..." -ForegroundColor DarkGray

    $deadline = (Get-Date).AddSeconds($HoldSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            Invoke-GS 'game_event' @{
                game  = $Game
                event = $evt
                data  = @{ frame = @{ l1 = "TYP $i"; l2 = $dt } }
            } | Out-Null
        } catch { }
        Start-Sleep -Milliseconds 700
    }

    $seen = Ask "Stand 'TYP $i' auf dem Display?"
    $results += [pscustomobject]@{
        DeviceType = $dt
        Bind       = 'ok'
        Sichtbar   = $(if ($seen) { 'JA' } else { 'nein' })
        Notiz      = ''
    }
    if ($seen) {
        Write-Host "      TREFFER - dieser device-type funktioniert." -ForegroundColor Green
        break
    }
}

try { Invoke-GS 'remove_game' @{ game = $Game } | Out-Null } catch { }

# --- Fazit -----------------------------------------------------------------
Write-Host "`n=== Ergebnis ===" -ForegroundColor Cyan
$results | Format-Table -AutoSize | Out-String -Width 120 | Write-Host

$winner = $results | Where-Object Sichtbar -eq 'JA' | Select-Object -First 1
if ($winner) {
    Write-Host "ADR 0002 bestaetigt. Funktionierender device-type: $($winner.DeviceType)" -ForegroundColor Green
    Write-Host "In docs/gamesense-notes.md und config.json (gamesense.device_type) eintragen." -ForegroundColor Green
} else {
    Write-Host "Kein device-type erreicht das Display." -ForegroundColor Red
    Write-Host ""
    Write-Host "Das ist ueberraschend: NowPlaying und die CS2-App zeigen auf diesem" -ForegroundColor Yellow
    Write-Host "Geraet Inhalte an, das Display ist also grundsaetzlich erreichbar." -ForegroundColor Yellow
    Write-Host "Bevor die Architektur in Frage gestellt wird, zuerst pruefen:" -ForegroundColor Yellow
    Write-Host "  - Laeuft parallel eine App, die den Schirm belegt (NowPlaying, CS2)?" -ForegroundColor Yellow
    Write-Host "    GameSense priorisiert Apps; eine aktive kann uns verdraengen." -ForegroundColor Yellow
    Write-Host "  - Zeigt die Basisstation gerade ihre eigene UI (Lautstaerke/ChatMix)?" -ForegroundColor Yellow
    Write-Host "  - Ist die Probe-App in GG unter Engine/Apps deaktiviert?" -ForegroundColor Yellow
    Write-Host "  - Mit -HoldSeconds 20 erneut versuchen." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Erst wenn all das ausgeschlossen ist, ist der USB-HID-Rueckfall" -ForegroundColor DarkYellow
    Write-Host "(docs/decisions/0005-*.md, tools\hid-probe.ps1) das Thema." -ForegroundColor DarkYellow
}
Write-Host ""
