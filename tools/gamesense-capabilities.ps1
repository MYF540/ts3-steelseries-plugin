<#
.SYNOPSIS
    Phase 0, Teil 2: Was kann der Schirm, und wie verhaelt er sich?

.DESCRIPTION
    gamesense-probe.ps1 hat die Grundfrage beantwortet - "screened" erreicht das
    Display. Offen sind die vier Fragen, die das Design bestimmen:

      A  Wie viele Textzeilen werden tatsaechlich dargestellt?
         -> legt display.max_lines und das Zeilenbudget des Composers fest
      B  Funktioniert ein statisches image-data-Bitmap?
         -> entscheidet, ob Phase 5 (eigenes Rendering) ueberhaupt moeglich ist
      C  Funktioniert ein dynamisches Bitmap ueber den frame-Kontext?
         -> das SDK ist an dieser Stelle unklar, also messen statt raten
      D  Gibt GG den Schirm wieder frei, wenn wir aufhoeren?
         -> bestimmt, ob wir beim Shutdown aufraeumen muessen

    Zusaetzlich eine gefuehrte Beobachtung zur Konkurrenz mit anderen Apps
    (NowPlaying, CS2). Die laesst sich nicht automatisieren, ist aber die
    Frage mit der groessten Wirkung auf den Entwurf: Wenn eine dauerhafte
    Statusanzeige die Musikanzeige verdraengt, will man das vorher wissen.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\gamesense-capabilities.ps1

.EXAMPLE
    # Nur einen Abschnitt fahren:
    .\tools\gamesense-capabilities.ps1 -Only A
#>

[CmdletBinding()]
param(
    [ValidateSet('A', 'B', 'C', 'D', 'E', 'alle')]
    [string]$Only = 'alle',

    [string]$Game        = 'TS3_OLED_PROBE',
    [string]$DeviceType  = 'screened',
    [int]   $HoldSeconds = 6
)

$ErrorActionPreference = 'Stop'
$results = [ordered]@{}

function Ask([string]$Question) {
    while ($true) {
        $a = Read-Host "  >> $Question [j/n]"
        if ($a -match '^(j|y)') { return $true }
        if ($a -match '^n')     { return $false }
    }
}
function AskText([string]$Question) { return (Read-Host "  >> $Question") }
function Note([string]$Key, $Value) {
    $results[$Key] = $Value
    Write-Host "  = $Key : $Value" -ForegroundColor Green
}
function Want([string]$Section) { return ($Only -eq 'alle' -or $Only -eq $Section) }

# --- Verbindung ------------------------------------------------------------
$corePropsPath = Join-Path $env:PROGRAMDATA 'SteelSeries\SteelSeries Engine 3\coreProps.json'
if (-not (Test-Path $corePropsPath)) { Write-Host "SteelSeries GG laeuft nicht." -ForegroundColor Red; exit 1 }
$base = 'http://' + (Get-Content $corePropsPath -Raw | ConvertFrom-Json).address

function Invoke-GS([string]$Endpoint, $Body) {
    Invoke-RestMethod -Uri "$base/$Endpoint" -Method Post -ContentType 'application/json' `
                      -Body ($Body | ConvertTo-Json -Depth 12 -Compress) -TimeoutSec 8
}

Invoke-GS 'game_metadata' @{
    game = $Game; game_display_name = 'TS3 OLED Probe'
    developer = 'ts3-steelseries-plugin'; deinitialize_timer_length_ms = 15000
} | Out-Null
Write-Host "Verbunden mit $base`n" -ForegroundColor DarkGray

function Send-Lines([string]$EventName, [string[]]$Keys, [hashtable]$Frame, [int]$Seconds) {
    $lineDefs = @()
    foreach ($k in $Keys) {
        $lineDefs += @{ 'has-text' = $true; 'context-frame-key' = $k }
    }
    Invoke-GS 'bind_game_event' @{
        game = $Game; event = $EventName; value_optional = $true
        handlers = @(@{
            'device-type' = $DeviceType; zone = 'one'; mode = 'screen'
            datas = @(@{ lines = $lineDefs })
        })
    } | Out-Null

    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        try { Invoke-GS 'game_event' @{ game = $Game; event = $EventName; data = @{ frame = $Frame } } | Out-Null } catch { }
        Start-Sleep -Milliseconds 700
    }
}

# --- A: Zeilenzahl ---------------------------------------------------------
if (Want 'A') {
    Write-Host "=== A. Wie viele Zeilen werden dargestellt? ===" -ForegroundColor Cyan
    Write-Host "  Es werden 5 nummerierte Zeilen gebunden. Zaehle, wie viele ankommen." -ForegroundColor Yellow

    $keys  = 1..5 | ForEach-Object { "k$_" }
    $frame = @{}
    1..5 | ForEach-Object { $frame["k$_"] = "Zeile $_" }

    Send-Lines 'CAPLINES' $keys $frame ($HoldSeconds + 4)
    Note 'Sichtbare Zeilen' (AskText 'Wie viele der 5 Zeilen waren lesbar?')
    Note 'Zeilen abgeschnitten?' (AskText 'Wurde Text horizontal abgeschnitten? Bei welcher Laenge etwa?')
}

# --- B: statisches Bitmap --------------------------------------------------
# Zeilenweise gepackt, MSB zuerst -> 128*64/8 = 1024 Byte.
# Schachbrett aus 8x8-Bloecken: eine falsche Packreihenfolge faellt sofort auf,
# weil dann Streifen statt Karos erscheinen.
function New-Checkerboard {
    $b = New-Object byte[] 1024
    for ($i = 0; $i -lt 1024; $i++) {
        $row     = [math]::Floor($i / 16)
        $colByte = $i % 16
        $b[$i]   = if (((([math]::Floor($row / 8)) + $colByte) % 2) -eq 0) { 0xFF } else { 0x00 }
    }
    return $b
}

if (Want 'B') {
    Write-Host "`n=== B. Statisches Bitmap (image-data im Handler) ===" -ForegroundColor Cyan
    $bmp = New-Checkerboard
    try {
        Invoke-GS 'bind_game_event' @{
            game = $Game; event = 'CAPBMP'; value_optional = $true
            handlers = @(@{
                'device-type' = $DeviceType; zone = 'one'; mode = 'screen'
                datas = @(@{ 'has-text' = $false; 'image-data' = $bmp })
            })
        } | Out-Null
        $d = (Get-Date).AddSeconds($HoldSeconds)
        while ((Get-Date) -lt $d) {
            try { Invoke-GS 'game_event' @{ game = $Game; event = 'CAPBMP'; data = @{ frame = @{} } } | Out-Null } catch { }
            Start-Sleep -Milliseconds 700
        }
        Note 'Statisches Bitmap' $(if (Ask 'Schachbrettmuster sichtbar?') { 'ja' } else { 'nein' })
        Note 'Bitmap-Orientierung' (AskText 'Karos (=korrekt), Streifen (=falsche Packung), oder etwas anderes?')
    }
    catch { Note 'Statisches Bitmap' "Fehler: $($_.Exception.Message)" }
}

# --- C: dynamisches Bitmap -------------------------------------------------
if (Want 'C') {
    Write-Host "`n=== C. Dynamisches Bitmap (image-data-128x64 im frame) ===" -ForegroundColor Cyan
    Write-Host "  Laut SDK-Doku gibt es Frame-Schluessel nach dem Muster image-data-BxH." -ForegroundColor DarkGray
    Write-Host "  Wie der Handler darauf verweist, sagt die Doku nicht - daher dieser Test." -ForegroundColor DarkGray

    $bmp = New-Checkerboard
    # Invertiert, damit es sich sichtbar von Test B unterscheidet
    for ($i = 0; $i -lt 1024; $i++) { $bmp[$i] = $bmp[$i] -bxor 0xFF }

    try {
        Invoke-GS 'bind_game_event' @{
            game = $Game; event = 'CAPDYN'; value_optional = $true
            handlers = @(@{
                'device-type' = $DeviceType; zone = 'one'; mode = 'screen'
                datas = @(@{ 'has-text' = $false; 'context-frame-key' = 'image-data-128x64' })
            })
        } | Out-Null
        $d = (Get-Date).AddSeconds($HoldSeconds)
        while ((Get-Date) -lt $d) {
            try {
                Invoke-GS 'game_event' @{
                    game = $Game; event = 'CAPDYN'
                    data = @{ frame = @{ 'image-data-128x64' = $bmp } }
                } | Out-Null
            } catch { }
            Start-Sleep -Milliseconds 700
        }
        Note 'Dynamisches Bitmap' $(if (Ask 'Invertiertes Schachbrett sichtbar?') { 'ja' } else { 'nein' })
    }
    catch { Note 'Dynamisches Bitmap' "Fehler: $($_.Exception.Message)" }
}

# --- D: Freigabe -----------------------------------------------------------
if (Want 'D') {
    Write-Host "`n=== D. Gibt GG den Schirm wieder frei? ===" -ForegroundColor Cyan

    Send-Lines 'CAPREL' @('r1') @{ r1 = 'HALTE SCHIRM' } 4

    Write-Host "  D1: Heartbeat gestoppt, warte 18 s auf den Deinitialize-Timer (15 s)..." -ForegroundColor Yellow
    Start-Sleep -Seconds 18
    Note 'Freigabe per Timeout' $(if (Ask 'Zeigt das Display wieder die normale GG-Oberflaeche?') { 'ja' } else { 'NEIN' })

    Send-Lines 'CAPREL' @('r1') @{ r1 = 'NOCHMAL' } 4
    Write-Host "  D2: jetzt explizit remove_game..." -ForegroundColor Yellow
    Invoke-GS 'remove_game' @{ game = $Game } | Out-Null
    Start-Sleep -Seconds 3
    Note 'Freigabe per remove_game' $(if (Ask 'Sofort wieder GG-Oberflaeche?') { 'ja' } else { 'NEIN' })

    Invoke-GS 'game_metadata' @{
        game = $Game; game_display_name = 'TS3 OLED Probe'
        developer = 'ts3-steelseries-plugin'; deinitialize_timer_length_ms = 15000
    } | Out-Null
}

# --- E: Konkurrenz ---------------------------------------------------------
if (Want 'E') {
    Write-Host "`n=== E. Konkurrenz mit anderen Apps ===" -ForegroundColor Cyan
    Write-Host "  Die wichtigste Entwurfsfrage: Wenn TeamSpeak dauerhaft eine" -ForegroundColor Yellow
    Write-Host "  Statusanzeige haelt, verdraengt das dann NowPlaying - oder umgekehrt?" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Bitte JETZT Musik starten, sodass NowPlaying etwas anzeigen wuerde." -ForegroundColor Yellow
    Read-Host "  [Enter], sobald Musik laeuft"

    Send-Lines 'CAPCONF' @('c1','c2') @{ c1 = 'TS3 STATUS'; c2 = 'haelt den Schirm' } 15

    Note 'Wer gewinnt' (AskText 'Was war zu sehen: nur TS3, nur NowPlaying, oder Wechsel?')
    Note 'Flackern' $(if (Ask 'Hat das Display sichtbar geflackert oder gezuckt?') { 'ja' } else { 'nein' })
}

# --- Aufraeumen ------------------------------------------------------------
try { Invoke-GS 'remove_game' @{ game = $Game } | Out-Null } catch { }

Write-Host "`n=== Ergebnis ===" -ForegroundColor Cyan
$results.GetEnumerator() | ForEach-Object { "{0,-26} {1}" -f $_.Key, $_.Value }
Write-Host "`nIn die Statustabelle von docs/gamesense-notes.md eintragen." -ForegroundColor Yellow
Write-Host ""
