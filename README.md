# ts3-steelseries-plugin

TeamSpeak-3-Status auf dem OLED der SteelSeries Arctis Nova Pro Basisstation: wer gerade
spricht, ob dein Mikro stumm ist, wer den Channel betritt, wer dich anstupst.

Ein einzelnes natives TeamSpeak-Plugin. **Kein Hintergrunddienst, kein zusätzliches
Programm, kein Autostart-Eintrag.**

```
        ┌──────────────────────────────┐
        │  ♪   Anna                    │
        │      Bernd                   │
        │      Lobby 4/7               │
        └──────────────────────────────┘
             128 × 64, 3 Zeilen
```

*Sprache: Deutsch und Englisch, folgt standardmäßig der Windows-Anzeigesprache.*

---

## Was angezeigt wird

Zehn Bausteine, einzeln an- und abschaltbar, in frei wählbarer Reihenfolge:

| Anzeige | wann |
|---|---|
| **Wer spricht** | bis zu drei Namen, einer je Zeile, mit Sprech-Icon |
| **Stumm gesprochen** | du redest ins stumme Mikrofon — schlägt alles andere |
| **Angestupst** | wer, plus die Nachricht |
| **Ping / Paketverlust** | nur bei schlechten Werten (Schwellen einstellbar) |
| **Verbindungsstatus** | Verbinde…, Getrennt |
| **Neue Nachricht** | Absender und Anfang der Nachricht |
| **Buddy kommt online** | jemand von deiner Buddy-Liste betritt den Server |
| **Betritt Channel** | jemand kommt in deinen Channel |
| **Mute / Deaf** | kurz nach dem Umschalten |
| **Channel** | Name plus **aktiv/gesamt** — stumme, taube und abwesende Leute sind da, aber nicht ansprechbar |

### Der Schirm gehört dir, nicht dem Plugin

Das Display wird **nur belegt, wenn es etwas zu melden gibt**, und danach sofort an
SteelSeries GG zurückgegeben. Läuft nebenbei Musik, behältst du deine NowPlaying-Anzeige,
solange in TeamSpeak nichts passiert.

Das ist keine Sparsamkeit, sondern eine Notwendigkeit: Bei mehreren aktiven
GameSense-Apps wechselt GG zwischen den Anzeigen und flackert dabei sichtbar. Eine
Dauerbelegung würde deine Musikanzeige zerhacken. Ausführlich in
[ADR 0006](docs/decisions/0006-event-driven-screen-ownership.md) und
[ADR 0007](docs/decisions/0007-transient-vs-persistent.md).

---

## Voraussetzungen

| | |
|---|---|
| Betriebssystem | Windows |
| TeamSpeak | 3.6.x, **64 Bit** (Plugin-API 26) |
| Hardware | SteelSeries Arctis Nova Pro Wireless — Basisstation mit 128×64-OLED |
| Software | **SteelSeries GG muss laufen** — ihm gehört das Display |

> Andere SteelSeries-Geräte mit Bildschirm sind nicht ausgeschlossen: Das Plugin bindet
> den generischen Gerätetyp `screened`. Getestet ist ausschließlich die
> Nova-Pro-Basisstation.

---

## Installation

### Aus einem Release

1. `ts3-steelseries-plugin-<version>.ts3_plugin` von der
   [Releases-Seite](https://github.com/MYF540/ts3-steelseries-plugin/releases) laden.
2. **Doppelklick.** TeamSpeak fragt nach und installiert selbst.
3. TeamSpeak neu starten.
4. *Extras → Optionen → Addons* — Plugin aktivieren.

### Von Hand

Falls der Doppelklick nicht greift, ist `.ts3_plugin` schlicht ein ZIP-Archiv:

1. Umbenennen in `.zip` und entpacken.
2. `plugins\ts3_steelseries.dll` nach `%APPDATA%\TS3Client\plugins\` kopieren.
3. TeamSpeak neu starten und unter *Addons* aktivieren.

### Prüfen, ob es läuft

Öffne die Einstellungen (siehe unten). Unten links steht entweder
**„GameSense verbunden (127.0.0.1:…)"** oder **„SteelSeries GG läuft nicht"**. Das
beantwortet die häufigste Frage sofort, ohne ins Log zu schauen.

---

## Einstellungen

Zwei Wege:

- *Extras → Optionen → Addons →* Plugin auswählen *→ Einstellungen*
- Menüband *Plugins → TS3 SteelSeries OLED → Einstellungen*

Einstellbar sind: welche Anzeigen aktiv sind und in welcher Reihenfolge, die Dauer je
Anzeige (1–60 s), die Warnschwellen für Ping und Paketverlust, die Buddy-Liste und die
Sprache. **Speichern wirkt sofort — kein TeamSpeak-Neustart.**

**Buddys** nimmst du per Rechtsklick auf einen Nutzer auf (*Plugins → TS3 SteelSeries
OLED → Als Buddy merken*) oder trägst die UID im Dialog ein. Gespeichert wird die
`CLIENT_UNIQUE_IDENTIFIER`, nicht der Nickname — der Eintrag übersteht also eine
Umbenennung.

Alles landet in `%APPDATA%\TS3Client\plugins\ts3_steelseries\config.json` und kann auch
direkt bearbeitet werden; das Schema steht in [configuration.md](docs/configuration.md).

---

## Grenzen

Ehrlichkeitshalber vorweg — das meiste davon ist gemessen, nicht vermutet:

**Drei Zeilen, rund 16 Zeichen (12 mit Icon).** Der Font ist fest, ein Icon kostet die
32 linkesten Pixel und damit vier Zeichen.

**Es gibt keinen Bildlauf.** Zu langer Text wird abgeschnitten, nicht durchgescrollt —
mit einem absichtlich viel zu langen Text nachgemessen. Lange Nicknames werden deshalb
gekürzt (`EinSehrLangerN.`). Dass die NowPlaying-App scrollen kann, hilft nicht: Sie
erreicht das offenbar über einen anderen Mechanismus als den dokumentierten
JSON-Texthandler.

**Keine Grafik.** Beide dokumentierten Bitmap-Wege des SDK wurden geprüft und zeigen auf
diesem Gerät nichts an. Nur Text.

**Flackern bei paralleler GameSense-App.** Zeigt gleichzeitig NowPlaying oder die
CS2-App etwas an, wechselt GG zwischen den Anzeigen. Das Plugin hält den Schirm deshalb
nur kurz — das verkürzt das Flackern auf Sekunden, beseitigt es aber nicht.

**Nur der aktive Server-Tab.** Bei mehreren Verbindungen zählt der Tab im Vordergrund
([ADR 0004](docs/decisions/0004-single-active-server-tab.md)).

**Eigene Buddy-Liste.** TeamSpeaks Freunde-/Feinde-Verwaltung ist clientintern und wird
Plugins überhaupt nicht angeboten — eine Suche über die gesamten SDK-Header findet
weder Funktion noch Enum-Wert. Deshalb die separate Liste.

**Ohne SteelSeries GG bleibt das Display leer.** Das Plugin wartet mit Backoff und
verbindet sich, sobald GG startet.

**Nach einem TeamSpeak-Update kann das Plugin wortlos verschwinden.** Dann hat sich
`PLUGIN_API_VERSION` geändert und es braucht einen neuen Build — die erste
Diagnosefrage bei „geht nicht mehr".

**Fremde GameSense-Apps können das Display zerstören.**
[Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66) im SDK, offen und
ungefixt. Wir senden seriell und rate-limitiert; gegen andere Schreiber hilft das nicht.
Abhilfe: Basisstation kurz stromlos machen.

---

## Selbst bauen

### Benötigt

- **Visual Studio 2022 oder neuer** mit der Workload *Desktop-Entwicklung mit C++*
- Darin zusätzlich, unter *Einzelne Komponenten*:
  - **Windows 11 SDK**
  - **C++-CMake-Tools für Windows**

> Beide fehlen in manchen Standardinstallationen der Workload. Ohne Windows SDK
> kompiliert gar nichts, ohne CMake-Tools gibt es keine `cmake.exe`. CMake geht
> alternativ per `winget install Kitware.CMake`.

### Bauen

```powershell
git clone --recursive https://github.com/MYF540/ts3-steelseries-plugin.git
cd ts3-steelseries-plugin

cmake --preset release
cmake --build --preset release
```

Ohne `--recursive` geklont? `git submodule update --init --recursive` nachholen — das
TeamSpeak-Plugin-SDK hängt als Submodul unter `third_party/`.

Das Ergebnis liegt in `build\release\bin\RelWithDebInfo\ts3_steelseries.dll`.

Externe Abhängigkeiten holt CMake selbst
([nlohmann/json](https://github.com/nlohmann/json), für Tests
[doctest](https://github.com/doctest/doctest)); beim ersten Konfigurieren wird also
Netzzugang gebraucht.

### Tests

```powershell
ctest --preset debug
```

24 Unit-Tests über Composer, Widgets und Konfiguration — ohne laufenden TeamSpeak und
ohne angeschlossenes Headset.

### Paket bauen

```powershell
.\tools\package.ps1 -Preset release
```

Legt `dist\ts3-steelseries-plugin-<version>.ts3_plugin` an. Die Versionsnummer steht
ausschließlich in `CMakeLists.txt` und wird überall eingesetzt.

### Entwicklungsschleife

| Werkzeug | Zweck |
|---|---|
| `tools\install-dev.ps1` | DLL ins TeamSpeak-Plugin-Verzeichnis kopieren |
| `tools\smoke-test.ps1` | DLL laden und init/shutdown fahren — **ohne TeamSpeak** |
| `tools\smoke-test.ps1 -Configure` | zusätzlich den Einstellungsdialog öffnen |
| `tools\gamesense-probe.ps1` | prüfen, welcher GameSense-Gerätetyp das Display erreicht |
| `tools\gamesense-capabilities.ps1` | Zeilenzahl, Bitmap, Freigabeverhalten, App-Konkurrenz |

Der Smoke-Test ist der Grund, warum die Schleife erträglich ist: Alles unterhalb der
ABI-Schicht lässt sich prüfen, ohne TeamSpeak jedes Mal neu zu starten.

---

## Fehlersuche

| Symptom | wahrscheinlichste Ursache |
|---|---|
| Plugin taucht in TS3 gar nicht auf | 32-Bit-Build, oder DLL im falschen Verzeichnis |
| Gelistet, lässt sich nicht aktivieren | `PLUGIN_API_VERSION` passt nicht zum Client |
| Aktiv, Display bleibt leer | GG läuft nicht — Statuszeile im Einstellungsdialog prüfen |
| Nichts erscheint, obwohl was passiert | Anzeige im Dialog deaktiviert, oder eine andere GameSense-App hat den Schirm |
| Pixelmüll | Issue #66, fremder Schreiber — Basisstation neu starten |
| Buddy meldet sich nicht | Buddy-Liste leer, oder die UID passt nicht (Nicknames zählen nicht) |

Log: `%APPDATA%\TS3Client\plugins\ts3_steelseries\ts3_steelseries.log`. Für mehr Details
in `config.json` unter `logging.level` auf `"debug"` stellen.

---

## Wie es funktioniert

```
TeamSpeak 3 Client
  └─ ts3_steelseries.dll
       ├─ TS3-Callbacks  ──► ClientState   (wer spricht, Mute, Channel, Ereignisse)
       ├─ Widgets        ──► Composer      (was ist einen Frame wert?)
       └─ GameSense HTTP ──► SteelSeries GG ──► OLED
```

TS3-Callbacks laufen auf einem Client-Thread, der auch Audio bedient — sie schreiben
deshalb nur Zustand und wecken einen eigenen Worker-Thread, der die Netzarbeit macht.

| Dokument | Inhalt |
|---|---|
| [architecture.md](docs/architecture.md) | Schichten, Threading, Datenmodell, Fehlerverhalten |
| [widgets.md](docs/widgets.md) | **Wie man eine Anzeige hinzufügt** |
| [configuration.md](docs/configuration.md) | Config-Schema und Dialog |
| [gamesense-notes.md](docs/gamesense-notes.md) | Gemessene GameSense-Fakten und Fallstricke |
| [ts3-plugin-notes.md](docs/ts3-plugin-notes.md) | TS3-SDK-Fakten und Fallstricke |
| [decisions/](docs/decisions/) | Architekturentscheidungen samt verworfener Alternativen |
| [roadmap.md](docs/roadmap.md) | Entstehungsgeschichte, inklusive der Sackgassen |

Eine neue Anzeige kostet eine Datei in `src/widgets/` plus eine Registrierungszeile —
Composer, Dialog und Konfiguration greifen sie automatisch auf.

---

## Lizenz

Noch nicht festgelegt. Ohne Lizenzangabe gilt in den meisten Rechtsordnungen „alle
Rechte vorbehalten" — für ein öffentliches Repository vermutlich nicht gewollt.

Das TeamSpeak-Plugin-SDK unter `third_party/` hat eigene Lizenzbedingungen.
