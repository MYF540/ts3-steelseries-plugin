<#
.SYNOPSIS
    Laedt die Plugin-DLL direkt und ruft ts3plugin_init/shutdown auf - ohne TeamSpeak.

.DESCRIPTION
    Die Entwicklungsschleife eines In-Process-Plugins ist zaeh: bauen, kopieren,
    TeamSpeak neu starten, verbinden, ausprobieren. Fuer alles unterhalb der
    ABI-Schicht ist das unnoetig.

    Dieses Skript laedt die DLL in den PowerShell-Prozess und ruft die beiden
    Lebenszyklus-Funktionen auf. Damit laufen Worker, GameSense-Session, WinHTTP
    und die Bildschirm-Zustandsmaschine real - nur eben ohne Client.

    Was NICHT geprueft wird: alles, was TS3Functions braucht. Da beim direkten
    Laden kein ts3plugin_setFunctionPointers aufgerufen wird, bleibt der
    Funktionszeiger-Block genullt. Der Log-Sink prueft darauf und schweigt; die
    Ausgabe landet in der Logdatei.

    Die DLL wird vor dem Laden in %TEMP% kopiert, sonst sperrt der PowerShell-
    Prozess die Builddatei und der naechste Build schlaegt fehl.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\smoke-test.ps1

.EXAMPLE
    .\tools\smoke-test.ps1 -Preset release -HoldSeconds 20
#>

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'debug',

    [int]$HoldSeconds = 10
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

$config = if ($Preset -eq 'debug') { 'Debug' } else { 'RelWithDebInfo' }
$dll = Join-Path $repo "build\$Preset\bin\$config\ts3_steelseries.dll"
if (-not (Test-Path $dll)) {
    throw "DLL nicht gefunden: $dll`nZuerst bauen:  cmake --build --preset $Preset"
}

# Aus dem Buildverzeichnis herauskopieren, damit der naechste Build nicht auf einer
# gesperrten Datei scheitert.
$staged = Join-Path $env:TEMP ("ts3ss-smoke-{0}.dll" -f ([guid]::NewGuid().ToString('N').Substring(0, 8)))
Copy-Item $dll $staged

$logFile = Join-Path $env:APPDATA 'TS3Client\plugins\ts3_steelseries\ts3_steelseries.log'
if (Test-Path $logFile) { Remove-Item $logFile -Force }

# Die Delegate-Bindung liegt komplett in C#: PowerShell 5.1 kennt keine Syntax fuer
# generische Methodenaufrufe, ein ::Bind[T](...) waere ein Parserfehler.
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class Ts3SsAbi {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr LoadLibraryW(string path);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool FreeLibrary(IntPtr module);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int    IntFn();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void   VoidFn();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr StrFn();

    private static IntPtr _module = IntPtr.Zero;

    public static void Load(string path) {
        _module = LoadLibraryW(path);
        if (_module == IntPtr.Zero)
            throw new Exception("LoadLibrary fehlgeschlagen, Win32-Fehler " + Marshal.GetLastWin32Error());
    }

    public static void Unload() {
        if (_module != IntPtr.Zero) { FreeLibrary(_module); _module = IntPtr.Zero; }
    }

    private static Delegate Bind(string name, Type type) {
        IntPtr p = GetProcAddress(_module, name);
        if (p == IntPtr.Zero) throw new Exception("Export nicht gefunden: " + name);
        return Marshal.GetDelegateForFunctionPointer(p, type);
    }

    private static string CallStr(string name) {
        return Marshal.PtrToStringAnsi(((StrFn)Bind(name, typeof(StrFn)))());
    }

    public static string Name()       { return CallStr("ts3plugin_name"); }
    public static string Version()    { return CallStr("ts3plugin_version"); }
    public static int    ApiVersion() { return ((IntFn)Bind("ts3plugin_apiVersion", typeof(IntFn)))(); }
    public static int    Init()       { return ((IntFn)Bind("ts3plugin_init", typeof(IntFn)))(); }
    public static void   Shutdown()   { ((VoidFn)Bind("ts3plugin_shutdown", typeof(VoidFn)))(); }
}
'@

Write-Host "`n=== DLL laden ===" -ForegroundColor Cyan
[Ts3SsAbi]::Load($staged)
Write-Host "  geladen: $staged" -ForegroundColor Green

try {
    Write-Host "  Name        : $([Ts3SsAbi]::Name())"
    Write-Host "  Version     : $([Ts3SsAbi]::Version())"
    Write-Host "  API-Version : $([Ts3SsAbi]::ApiVersion())  (TeamSpeak 3.6.x erwartet 26)"

    Write-Host "`n=== ts3plugin_init ===" -ForegroundColor Cyan
    $rc = [Ts3SsAbi]::Init()
    if ($rc -eq 0) { Write-Host "  Rueckgabe 0 (Erfolg)" -ForegroundColor Green }
    else { Write-Host "  Rueckgabe $rc (FEHLER)" -ForegroundColor Red }

    Write-Host "`n  Halte $HoldSeconds s. JETZT auf die Basisstation schauen." -ForegroundColor Yellow
    Start-Sleep -Seconds $HoldSeconds

    Write-Host "`n=== ts3plugin_shutdown ===" -ForegroundColor Cyan
    [Ts3SsAbi]::Shutdown()
    Write-Host "  zurueckgekehrt" -ForegroundColor Green
    Start-Sleep -Seconds 2
}
finally {
    [Ts3SsAbi]::Unload()
    Remove-Item $staged -Force -ErrorAction SilentlyContinue
}

Write-Host "`n=== Logdatei ===" -ForegroundColor Cyan
if (Test-Path $logFile) {
    Get-Content $logFile | ForEach-Object {
        $color = if ($_ -match '\[ERROR\]') { 'Red' } elseif ($_ -match '\[WARN') { 'Yellow' } else { 'Gray' }
        Write-Host "  $_" -ForegroundColor $color
    }
} else {
    Write-Host "  Keine Logdatei unter $logFile" -ForegroundColor Red
}
Write-Host ""
