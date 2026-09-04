# ADR 0005 — USB-HID statt GameSense als Display-Transport

**Status:** **verworfen** (2026-09-05) — die Prämisse war falsch.
[ADR 0002](0002-gamesense-over-usb-hid.md) bleibt in Kraft.

> **Warum das Dokument trotzdem bleibt.** Zwei Dinge darin sind unabhängig von der
> verworfenen Schlussfolgerung wertvoll: das gemessene Ergebnis der HID-Zugriffsprobe
> (Abschnitt 5) und die vollständige Protokollbeschreibung. Sollte der GameSense-Weg an
> seinen Einschränkungen scheitern — zu grobe Textzeilen, Issue #66, kein Bitmap —,
> ist der Rückfall hier bereits durchgerechnet.

## Warum die Entscheidung verworfen wurde

Diese ADR schloss aus GGs Datenbank, das OLED der Nova-Pro-Basisstation sei über
GameSense nicht ansprechbar. **Das war falsch.** Die Basisstation zeigt nachweislich
App-Inhalte: NowPlaying stellt Titel, Interpret und Fortschrittsbalken dar, die
Counter-Strike-2-App rotiert durch Match-Statistiken.

Der Fehlschluss lag in Befund 3 unten: Die Schlüssel `<gerät>_oled_display_sequence`
gehören zum Mechanismus für **gespeicherte OLED-Sequenzen auf Tastaturen und Mäusen**,
nicht zum Screen-Handler-Pfad des SDK. Aus ihrem Fehlen für Arctis-Geräte folgt nichts
über deren Eignung als Screen-Ziel.

Die tatsächliche Ursache des leeren Displays war schlicht ein falscher `device-type`:
`screened-128x64` existiert nicht, richtig ist das generische `screened`.

## Ursprüngliche Befunde

*(unverändert dokumentiert; Befund 3 trägt die Schlussfolgerung nicht, Befund 5 bleibt
als Messergebnis gültig)*

### 1. `screened-128x64` existiert im GameSense-Gerätekatalog nicht

`doc/api/standard-zones.md` des SDK kennt nur:

| Typ | Gerät |
|---|---|
| `screened-128x36` | Rival 700 / 710 |
| `screened-128x40` | Apex 7 / Apex Pro |
| `screened-128x48` | Arctis **Pro** Wireless |
| `screened-128x52` | GameDAC / Arctis Pro |

Kein 128x64, keine Nova-Pro-Generation. Beschrieben sind nur Geräte der
**Vorgängergeneration**.

### 2. Ein nicht passender `device-type` schlägt lautlos fehl

Das erklärt den beobachteten Zustand vollständig: GG nimmt den Handler an und speichert
ihn, unabhängig davon, ob irgendein angeschlossenes Gerät auf den `device-type` passt.
Es gibt keine Fehlermeldung und keinen Log-Eintrag. HTTP 200 heißt „gespeichert", nicht
„zugestellt".

Damit ist auch die naheliegende Vermutung entkräftet, es müssten in der GG-Oberfläche
noch Events konfiguriert werden: GGs eigene Spielpakete binden ihre Handler über
denselben Aufruf und brauchen keine manuelle Freischaltung.

### 3. GGs eigene Datenbank kennt für Arctis-Geräte keine Display-Fähigkeit

Auswertung von `C:\ProgramData\SteelSeries\GG\apps\engine\db\database.db`:

- Spielgesteuerte OLED-Inhalte laufen über Schlüssel `<gerät>_oled_display_sequence`.
  Vorhanden sind genau drei: `apex_2022` (Tastatur), `macho` (Tastatur — steht im
  Kontext von `key_codes`, `hid`, `lock_key_color`), `rival_700` (Maus).
- **Kein einziger Arctis-Eintrag**, insbesondere keiner für die Nova Pro.
- Die sechs `arctis_nova_pro_wireless_tx`-Einträge haben `oled_brightness`
  (eine Geräteeinstellung), aber **keine** `display_sequence`-Fähigkeit.
- Verwendete `device-type`-Werte in GGs Paketen: `screened`,
  `screened-2-lines`, `screened-3-lines-or-more` — durchweg auflösungsfrei.

### 4. Die Existenz der Community-Projekte stützt den Befund

[ggoled](https://github.com/JerwuQu/ggoled) und
[arctis-nova-oled](https://github.com/Piipperi/arctis-nova-oled) existieren genau
deshalb: um dieses Display anzusteuern, **ohne** GG. Gäbe es einen SDK-Weg, wären beide
überflüssig.

### 5. Der HID-Weg ist offen — auch neben laufendem GG

`tools/hid-probe.ps1`, ausgeführt bei 9 laufenden GG-Prozessen:

```
HID\VID_1038&PID_12E0&MI_04&COL02\7&166C7CF&0&0001   Lesen+Schreiben GEWAEHRT
HID\VID_1038&PID_12E0&MI_04&COL01\7&166C7CF&0&0000   Lesen+Schreiben GEWAEHRT
```

Beide Collections des Interface MI_04 lassen sich mit
`FILE_SHARE_READ | FILE_SHARE_WRITE` öffnen. **GG muss also nicht beendet werden** —
das war die größte Sorge gegenüber dem HID-Weg, und sie hat sich nicht bestätigt.

## Entscheidung

Display-Transport wird direktes USB-HID nach dem von
[ggoled](https://github.com/JerwuQu/ggoled) dokumentierten Protokoll:

| | |
|---|---|
| VID / PID | `0x1038` / `0x12E0` (Nova Pro Wireless) |
| Interface | MI_04, Collection mit den OLED-Reports |
| Report-ID | `0x06` |
| Zeichnen | `0x93`, danach x, y, Breite, Höhe (auf Vielfaches von 8 gepolstert), Bitmap |
| Bitpackung | **spaltenweise**, `(x * padded_h + y) / 8` |
| Helligkeit | `0x85`, Wert `0x01`–`0x0A` (Feature-Report) |
| Rückgabe an GG | `0x95` |
| Auflösung | 128 × 64, 1 bpp |

Zugriff über Windows-Bordmittel (`setupapi` + `hid.dll`), also weiterhin **keine externe
Abhängigkeit** — [ADR 0001](0001-single-process-native-plugin.md) bleibt unangetastet.

## Konsequenzen

### Was besser wird

- **Volle Pixelkontrolle.** Eigene Schrift, eigene Icons, eigenes Layout auf 128x64.
- **Deterministisches Timing.** Kein Heartbeat, kein Deinitialize-Timer, keine Engine
  dazwischen.
- **Issue #66 entfällt** als Fehlerbild aus *unserer* Richtung.
- **GG wird nicht mehr vorausgesetzt.** Angenehmer Nebeneffekt, kein Ziel.

### Was teurer wird

- **Bitmap-Rendering ist ab sofort Pflicht, nicht optional.** Das bisherige Phase 5
  (eigener Composer, eingebetteter Bitmap-Font, Icons) rückt in Phase 1. Das ist der
  größte Aufwandszuwachs dieser Entscheidung.
- **`src/gamesense/` entfällt** und wird zu `src/display/` (HID-Transport). CoreProps,
  WinHTTP und der Session-Lebenszyklus fallen weg — der Ersatz ist kleiner, aber
  hardwarenäher.
- **Aufräumen wird unsere Pflicht.** Bisher gab GG das Display nach 15 s von selbst
  frei. Jetzt müssen wir in `ts3plugin_shutdown` selbst `0x95` senden. Und weil ein
  TeamSpeak-Absturz das verhindert, bleibt dann unser letztes Bild stehen, bis der
  Nutzer die Basisstation anfasst. Das ist eine echte Verschlechterung und gehört in
  die README.
- **Reverse-engineertes Protokoll.** Ein Firmware- oder GG-Update kann es brechen.
  Deshalb liegen alle Konstanten an einer Stelle in `src/display/nova_pro_protocol.h`,
  nicht verstreut.

### Offenes Risiko: Konkurrenz mit der GG-Oberfläche

Die Basisstation zeigt im Normalbetrieb GGs eigene UI — Lautstärke, ChatMix,
Akkustand — und GG schreibt dafür auf dasselbe Interface. Wenn wir dauerhaft
dagegenschreiben, ist unklar, wer gewinnt und wie das aussieht: Bedient der Nutzer das
Rad, während wir zeichnen, kann es flackern oder die Lautstärkeanzeige verschlucken.

Das ist die wichtigste noch offene Frage. Sie lässt sich erst mit einem echten
Schreibversuch beantworten, nicht durch Lesen. Sinnvolle Gegenmaßnahmen, falls es
stört:

- Nur bei tatsächlicher Zustandsänderung zeichnen, nicht in einer festen Bildrate.
- Nach dem Zeichnen eine Weile ruhig bleiben, damit GG-UI-Ereignisse durchkommen.
- Über die Info-Collection (die zweite MI_04-Collection) prüfen, ob sich GG-Aktivität
  erkennen lässt.

## Vorbehalt

Diese ADR wird erst **akzeptiert**, wenn `tools/gamesense-probe.ps1` in der
Matrix-Fassung bestätigt, dass **kein** `device-type` das Display erreicht — insbesondere
nicht das generische `screened`, mit dem GGs eigene Spielpakete arbeiten.

Erreicht ein Typ doch das Display, bleibt ADR 0002 in Kraft: Der GameSense-Weg ist dann
trotz seiner Einschränkungen vorzuziehen, weil er offiziell ist und uns die Pflicht zum
Aufräumen abnimmt.
