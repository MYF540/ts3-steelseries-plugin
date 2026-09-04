<#
.SYNOPSIS
    Prueft, ob der USB-HID-Weg auf das OLED der Basisstation gangbar ist.

.DESCRIPTION
    Gegenprobe zu gamesense-probe.ps1. Beantwortet die Vorfragen, bevor
    HID-Code im Plugin entsteht:

      1. Ist die Basisstation ueberhaupt als HID-Geraet da (VID 1038, PID 12E0)?
      2. Existiert das benoetigte Interface (MI_04)?
      3. Laesst sich der Geraetepfad mit gemeinsamem Zugriff oeffnen,
         WAEHREND SteelSeries GG laeuft?

    Punkt 3 ist die eigentliche Frage. GG haelt die Basisstation permanent
    offen; wenn Windows uns daneben keinen Schreibzugriff gewaehrt, muesste der
    Nutzer GG beenden - und damit EQ, ChatMix und Akkuanzeige aufgeben. Das
    waere ein Ausschlusskriterium, ueber das der Nutzer entscheiden muss.

    Es wird NICHT auf das Display geschrieben. Das Skript oeffnet nur ein
    Handle und schliesst es wieder.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\hid-probe.ps1
#>

[CmdletBinding()]
param(
    [string]$Vid = '1038',
    # 12E0 = Nova Pro Wireless, 12E5 = Xbox, 225D = Xbox White,
    # 12CB/12CD = Nova Pro Wired, 2244 = Nova Elite
    [string[]]$Pids = @('12E0', '12E5', '225D', '12CB', '12CD', '2244')
)

$ErrorActionPreference = 'Stop'

Write-Host "`n=== 1. SteelSeries-HID-Geraete im System ===" -ForegroundColor Cyan

$all = Get-PnpDevice -Class HIDClass -ErrorAction SilentlyContinue |
       Where-Object { $_.InstanceId -match "VID_$Vid" }

if (-not $all) {
    Write-Host "  Kein SteelSeries-HID-Geraet gefunden (VID_$Vid)." -ForegroundColor Red
    Write-Host "  Basisstation angeschlossen und eingeschaltet?" -ForegroundColor Yellow
    exit 1
}

$all | Select-Object Status, FriendlyName, InstanceId |
    Format-Table -AutoSize | Out-String -Width 200 | Write-Host

Write-Host "=== 2. Zielgeraet und Interface MI_04 ===" -ForegroundColor Cyan

$target = $null
foreach ($p in $Pids) {
    $match = $all | Where-Object { $_.InstanceId -match "PID_$p" }
    if ($match) {
        Write-Host "  PID_$p gefunden ($($match.Count) Interface(s))" -ForegroundColor Green
        $mi04 = $match | Where-Object { $_.InstanceId -match 'MI_04' }
        if ($mi04) {
            @($mi04) | ForEach-Object { Write-Host "  -> MI_04: $($_.InstanceId)" -ForegroundColor Green }
            # MI_04 hat mehrere Collections (COL01/COL02). Laut ggoled traegt eine
            # davon die OLED-Reports (Report-ID 0x06), die andere Geraeteinfos.
            # Welche welche ist, sagt uns nur der Zugriffsversuch - also alle behalten.
            $targets = @($mi04) | Where-Object { $_.InstanceId -like 'HID\*' }
            if (-not $targets) { $targets = @($mi04) }
            $target = $targets[0]
            break
        } else {
            Write-Host "  -> MI_04 NICHT vorhanden. Vorhandene Interfaces:" -ForegroundColor Yellow
            $match | ForEach-Object { "     $($_.InstanceId)" }
        }
    }
}

if (-not $target) {
    Write-Host "`n  Kein passendes Interface gefunden." -ForegroundColor Red
    Write-Host "  Die oben gelistete InstanceId-Tabelle in docs/decisions/0005-*.md eintragen." -ForegroundColor Yellow
    exit 1
}

Write-Host "`n=== 3. Zugriff bei laufendem GG ===" -ForegroundColor Cyan

$gg = Get-Process -Name 'SteelSeriesGG*' -ErrorAction SilentlyContinue
if ($gg) {
    Write-Host "  SteelSeries GG laeuft ($($gg.Count) Prozess(e)) - genau der interessante Fall." -ForegroundColor Yellow
} else {
    Write-Host "  ACHTUNG: GG laeuft gerade NICHT. Der Test sagt dann wenig aus," -ForegroundColor Yellow
    Write-Host "  denn die Frage ist ja, ob wir NEBEN GG schreiben duerfen." -ForegroundColor Yellow
}

# Geraetepfad aus der InstanceId ableiten: \\?\hid#vid_1038&pid_12e0&mi_04#...#{guid}
$devIdKey = $target.InstanceId -replace '\\', '#'
$hidGuid  = '{4d1e55b2-f16f-11cf-88cb-001111000030}'

$container = (Get-PnpDeviceProperty -InstanceId $target.InstanceId `
                -KeyName 'DEVPKEY_Device_ContainerId' -ErrorAction SilentlyContinue).Data
Write-Host "  ContainerId: $container"

Add-Type -Namespace Ts3Ss -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr CreateFileW(
    string lpFileName, uint dwDesiredAccess, uint dwShareMode,
    System.IntPtr lpSecurityAttributes, uint dwCreationDisposition,
    uint dwFlagsAndAttributes, System.IntPtr hTemplateFile);

[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool CloseHandle(System.IntPtr hObject);
'@

$GENERIC_RW    = [uint32]3221225472  # GENERIC_READ | GENERIC_WRITE (0xC0000000 waere in PS5.1 ein negativer Int32)
$SHARE_RW      = [uint32]3           # FILE_SHARE_READ | FILE_SHARE_WRITE - neben GG
$OPEN_EXISTING = 3

# Interface-Pfade ueber die Registry der Geraeteschnittstellen holen.
# Der Schluesselname ist bereits der Geraetepfad in der Form
#   ##?#HID#VID_1038&PID_12E0&MI_04&Col02#7&166c7cf&0&0001#{4d1e55b2-...}
# also inklusive Interface-GUID - die darf NICHT noch einmal angehaengt werden.
$paths = @()
$ifRoot = "HKLM:\SYSTEM\CurrentControlSet\Control\DeviceClasses\$hidGuid"
if (Test-Path $ifRoot) {
    $keys = Get-ChildItem $ifRoot -ErrorAction SilentlyContinue
    foreach ($t in $targets) {
        $needle = ($t.InstanceId -replace '\\', '#').ToLower()
        foreach ($k in $keys) {
            if ($k.PSChildName.ToLower().Contains($needle)) {
                $paths += '\\?\' + ($k.PSChildName -replace '^##\?#', '')
            }
        }
    }
}
if (-not $paths) {
    $paths = @("\\?\$($devIdKey.ToLower())#$hidGuid")
    Write-Host "  (Pfad aus InstanceId abgeleitet - Registry lieferte keinen Treffer)" -ForegroundColor DarkGray
}

# Alle Collections testen statt beim ersten Treffer abzubrechen: welche der
# beiden die OLED-Reports traegt, laesst sich sonst nicht ablesen.
$ok = $false
$INVALID = [IntPtr]::new(-1)
foreach ($path in ($paths | Select-Object -Unique)) {
    $short = ($path -split '#')[1..2] -join '#'
    Write-Host "  $short" -ForegroundColor DarkGray

    $h = [Ts3Ss.Native]::CreateFileW($path, $GENERIC_RW, $SHARE_RW, [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
    if ($h -ne $INVALID) {
        [Ts3Ss.Native]::CloseHandle($h) | Out-Null
        Write-Host "      -> Lesen+Schreiben GEWAEHRT" -ForegroundColor Green
        $ok = $true
        continue
    }
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()

    # Fehler 5 (Zugriff verweigert) ist bei HID-Geraeten normal, sobald ein
    # anderer Prozess sie exklusiv haelt. Ein reines Lese-Handle genuegt fuer
    # eine Aussage nicht, zeigt aber, ob das Geraet grundsaetzlich erreichbar ist.
    $h2 = [Ts3Ss.Native]::CreateFileW($path, [uint32]0, $SHARE_RW, [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
    if ($h2 -ne $INVALID) {
        [Ts3Ss.Native]::CloseHandle($h2) | Out-Null
        Write-Host "      -> nur Metadaten-Handle (Schreiben abgelehnt, Win32 $err)" -ForegroundColor DarkYellow
    } else {
        Write-Host "      -> abgelehnt (Win32-Fehler $err)" -ForegroundColor DarkYellow
    }
}

Write-Host "`n=== Ergebnis ===" -ForegroundColor Cyan
if ($ok) {
    Write-Host "USB-HID-Weg ist gangbar - Zugriff auch neben laufendem GG." -ForegroundColor Green
    Write-Host "ADR 0005 kann auf 'akzeptiert' gesetzt werden." -ForegroundColor Green
} else {
    Write-Host "Kein Schreibzugriff auf das Interface." -ForegroundColor Red
    Write-Host "Vor einer Entscheidung mit beendetem GG erneut testen: Gelingt es dann," -ForegroundColor Yellow
    Write-Host "kostet der HID-Weg den Verzicht auf GG - das muss der Nutzer entscheiden." -ForegroundColor Yellow
}
Write-Host ""
