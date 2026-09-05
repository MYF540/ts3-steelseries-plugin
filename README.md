# ts3-steelseries-plugin

TeamSpeak-3-Status auf dem OLED der SteelSeries Arctis Nova Pro Wireless Basisstation:
wer gerade spricht, ob das Mikro stumm ist, in welchem Channel man sitzt.

> **Status: Planung abgeschlossen, Phase 0 bestanden.** Struktur, Architektur und
> Roadmap stehen; Implementierungscode gibt es noch nicht.
>
> Der Machbarkeitsnachweis ist erbracht: Mit `"device-type": "screened"` erscheint Text
> auf der Basisstation. Damit ist [Phase 1](docs/roadmap.md#phase-1--die-dll-lädt)
> freigegeben.
>
> **Wichtig für alle, die hier ansetzen:** `screened-128x64` existiert nicht, und ein
> falscher `device-type` scheitert *lautlos* — HTTP 200, App erscheint in GG, Display
> bleibt leer. Details in [gamesense-notes.md](docs/gamesense-notes.md).

## Wie es funktioniert

Ein einzelnes natives TeamSpeak-3-Plugin (Windows, x64). Kein Hintergrunddienst, kein
zusätzliches Programm.

```
TeamSpeak 3 Client
  └─ ts3_steelseries.dll
       ├─ TS3-Callbacks  ──► ClientState (wer spricht, Mute, Channel, Verbindung)
       ├─ Widgets        ──► vom Nutzer auswählbare Anzeige-Bausteine
       └─ GameSense HTTP ──► SteelSeries GG ──► OLED der Basisstation
```

Die Anzeige besteht aus Widgets, die der Nutzer im Konfigurationsdialog an- und
abschalten und sortieren kann. Ein neues Feature kostet eine Datei in `src/widgets/` —
siehe [widgets.md](docs/widgets.md).

## Voraussetzungen

| | |
|---|---|
| Betriebssystem | Windows (WinHTTP + Win32-Dialog) |
| TeamSpeak | 3.6.x, 64 Bit — Plugin-API-Version **26** |
| Headset | SteelSeries Arctis Nova Pro Wireless (Basisstation mit 128x64-OLED) |
| Software | **SteelSeries GG muss laufen** — es besitzt das Display |
| Build | Visual Studio 2022 oder neuer, CMake ≥ 3.21 |

> **Hinweis zum Build-Setup:** CMake gehört nicht zur Standardinstallation von Visual
> Studio. Im Visual Studio Installer unter *Einzelne Komponenten* die
> **„C++-CMake-Tools für Windows"** ergänzen (Workload *Desktop-Entwicklung mit C++*).
> Ohne sie schlägt `cmake --preset` mit „command not found" fehl.

## Bauen

```powershell
git clone --recursive https://github.com/<user>/ts3-steelseries-plugin.git
cd ts3-steelseries-plugin

cmake --preset release
cmake --build --preset release

.\tools\install-dev.ps1 -Preset release -RestartTeamSpeak
```

Beim Klonen ohne `--recursive`: `git submodule update --init --recursive` nachholen —
das TeamSpeak-Plugin-SDK liegt als Submodul unter `third_party/`.

Solange `src/` noch leer ist, meldet CMake, dass nur das Gerüst konfiguriert wurde.
Das ist der erwartete Zustand vor Phase 1.

## Dokumentation

| Dokument | Inhalt |
|---|---|
| [architecture.md](docs/architecture.md) | Schichten, Threading-Modell, Datenmodell, Fehlerverhalten |
| [roadmap.md](docs/roadmap.md) | Phasen 0–6, jeweils mit Abnahmekriterium |
| [widgets.md](docs/widgets.md) | **Wie man ein Feature hinzufügt** |
| [configuration.md](docs/configuration.md) | Config-Schema und Dialog |
| [gamesense-notes.md](docs/gamesense-notes.md) | Recherchierte GameSense-Fakten, offene Punkte |
| [ts3-plugin-notes.md](docs/ts3-plugin-notes.md) | Recherchierte TS3-SDK-Fakten, Stolperfallen |
| [decisions/](docs/decisions/) | Architekturentscheidungen samt verworfener Alternativen |

## Projektstruktur

```
src/plugin/      TS3-ABI-Grenze -- die einzige Stelle, die TS3-Header kennt
src/core/        ClientState, StateStore, Worker-Thread
src/widgets/     Anzeige-Bausteine (hier wächst das Projekt)
src/render/      Composer, Frame, 1bpp-Bitmap
src/gamesense/   coreProps, WinHTTP, Session-Lifecycle
src/config/      JSON-Config + Win32-Dialog
src/util/        Logging, UTF-8/UTF-16
tools/           smoke-test (DLL ohne TeamSpeak testen), Phase-0-Proben,
                 Dev-Install, Paketbau
third_party/     TeamSpeak-Plugin-SDK (Submodul)
```

## Bekannte Einschränkungen

- **Bei parallel laufenden GameSense-Apps flackert das Display.** Zeigt gleichzeitig
  eine andere App etwas an (NowPlaying bei laufender Musik, die CS2-App im Match),
  wechselt SteelSeries GG zwischen den Anzeigen — sichtbar und nicht abstellbar.
  Deshalb belegt das Plugin den Schirm nur, solange es etwas zu melden hat, und gibt ihn
  danach sofort zurück ([ADR 0006](docs/decisions/0006-event-driven-screen-ownership.md)).
  Das verkürzt das Flackern auf wenige Sekunden, beseitigt es aber nicht.
- **Andere GameSense-Anwendungen können das Display stören.**
  [Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66) im SDK berichtet von
  zerstörten Displayinhalten, wenn mehrere Quellen gleichzeitig schreiben — offen und
  ungefixt. Wir senden seriell und rate-limitiert; gegen fremde Schreiber hilft das nicht.
- **Grafik ist nicht möglich, nur drei Textzeilen.** Beide Bitmap-Wege des SDK wurden
  geprüft und zeigen auf diesem Gerät nichts an.
- **Nur der aktive Server-Tab wird angezeigt.** Begründung in
  [ADR 0004](docs/decisions/0004-single-active-server-tab.md).
- **Ohne laufendes SteelSeries GG bleibt das Display leer.** Das Plugin wartet mit
  Backoff und verbindet sich, sobald GG startet.
- **Nach einem TeamSpeak-Update kann das Plugin wortlos nicht mehr laden.** Dann hat sich
  `PLUGIN_API_VERSION` geändert und ein Neubau ist nötig — erste Diagnosefrage bei
  „geht nicht mehr".

## Fehlersuche

| Symptom | Wahrscheinlichste Ursache |
|---|---|
| Plugin taucht in TS3 gar nicht auf | 32-Bit-Build, oder DLL im falschen Verzeichnis |
| Plugin gelistet, lässt sich nicht aktivieren | `PLUGIN_API_VERSION` passt nicht zum Client |
| Plugin aktiv, Display bleibt leer | GG läuft nicht, oder Gerätetyp wird abgelehnt — Statuszeile im Konfigurationsdialog prüfen |
| Display zeigt Pixelmüll | Issue #66, fremder Schreiber — Basisstation neu starten |

## Lizenz

Noch nicht festgelegt. Das TeamSpeak-Plugin-SDK unter `third_party/` hat eigene
Lizenzbedingungen.
