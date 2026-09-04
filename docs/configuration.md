# Konfiguration

## Speicherort

```
%APPDATA%\TS3Client\plugins\ts3_steelseries\config.json
```

Unterhalb des TeamSpeak-Plugin-Verzeichnisses, weil die Einstellung zum Plugin gehört
und mit einem TS3-Profil zusammen wandert. Existiert die Datei nicht, wird sie beim
ersten Start mit Standardwerten angelegt.

## Schema

```json
{
  "version": 1,

  "gamesense": {
    "game": "TS3_OLED",
    "game_display_name": "TeamSpeak 3",
    "deinitialize_timer_ms": 15000,
    "heartbeat_interval_ms": 8000,
    "min_update_interval_ms": 120,
    "device_type": "screened"
  },

  "display": {
    "mode": "events",
    "hold_ms": 6000,
    "max_lines": 3,
    "widgets": [
      { "id": "talkers",      "enabled": true  },
      { "id": "mute_status",  "enabled": true  },
      { "id": "channel_info", "enabled": true  },
      { "id": "connection",   "enabled": false }
    ]
  },

  "logging": {
    "level": "info",
    "to_file": true
  }
}
```

### `display.mode` und `display.hold_ms`

`"events"` (Default) belegt den Schirm nur, solange mindestens ein Widget etwas
liefert, und gibt ihn danach per `remove_game` frei. `"always"` hält ihn dauerhaft,
solange TeamSpeak läuft.

Der Default ist gemessen begründet, nicht geschmacklich: Bei paralleler GameSense-App
(NowPlaying, CS2) **wechselt** GG zwischen den Anzeigen und flackert dabei sichtbar. Eine
Dauerbelegung zerhackt damit die Musikanzeige des Nutzers. Ausführlich in
[ADR 0006](decisions/0006-event-driven-screen-ownership.md).

`hold_ms` ist die Nachlaufzeit: Wie lange die Anzeige stehen bleibt, nachdem das letzte
Widget verstummt ist. Zu kurz wirkt hektisch, zu lang nähert sich der Dauerbelegung an.
6 s ist ein Startwert, kein Messergebnis.

### `display.max_lines`

Standard 3 — das ist die auf der Arctis-Nova-Pro-Basisstation gemessene Zeilenzahl.
Gebunden wurden fünf Zeilen, dargestellt drei; horizontal wurde nichts abgeschnitten.

### `display.widgets`

Das ist die Nutzerauswahl aus dem Auftrag. **Die Reihenfolge im Array ist die
Anzeigereihenfolge** — kein separates `order`-Feld, das mit der Array-Position aus dem
Tritt geraten kann.

Regeln beim Laden, jede davon ein potenzieller Absturz, wenn man sie vergisst:

- **Unbekannte `id`** (Widget entfernt oder Config aus neuerer Version): überspringen,
  Warnung ins Log. Kein Fehler, kein Abbruch.
- **Fehlende `id`** (neues Widget, alte Config): ans Ende anhängen, `enabled` auf den
  Standardwert des Widgets. So erscheinen neue Features, ohne dass jemand die Config
  löschen muss.
- **Doppelte `id`**: erstes Vorkommen gewinnt, Rest verwerfen.

### `gamesense.min_update_interval_ms`

Untergrenze zwischen zwei `/game_event`-Requests. Events, die währenddessen eintreffen,
werden zusammengefasst und beim nächsten Tick als *ein* Frame gesendet.

Das ist keine Optimierung, sondern eine Schutzmaßnahme gegen
[Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66) — siehe
[gamesense-notes.md](gamesense-notes.md). Nicht ohne Not verkleinern.

### `gamesense.device_type`

Default ist das generische `"screened"`, das auf jedes Gerät mit Schirm passt und mit
dem auch GGs eigene Spielpakete binden. Auflösungsspezifische Werte wie
`"screened-128x48"` existieren nur für ältere Geräte — `"screened-128x64"` gibt es
nicht, und ein nicht passender Typ scheitert **lautlos** (siehe
[gamesense-notes.md](gamesense-notes.md)).

Der Wert bleibt trotzdem konfigurierbar: Wenn eine künftige GG-Version einen
spezifischeren Typ einführt oder ein anderes Gerät angebunden werden soll, spart das
einen neuen Build.

## Der Dialog

Erreichbar über *Extras -> Optionen -> Addons -> ts3-steelseries -> Einstellungen*.
Win32-Dialog aus `resources/plugin.rc`, kein Qt (siehe
[ts3-plugin-notes.md](ts3-plugin-notes.md)).

```
+----------------------------------------------+
|  TeamSpeak 3 -> Arctis OLED                  |
|                                              |
|  Anzeigen:                          [ Hoch ] |
|  +----------------------------------+ [Runter]|
|  | [x] Wer spricht                  |        |
|  | [x] Mute / Deaf                  |        |
|  | [x] Channel                      |        |
|  | [ ] Verbindungsstatus            |        |
|  +----------------------------------+        |
|                                              |
|  Maximale Zeilen: [ 3 ]                      |
|                                              |
|  Status: GameSense verbunden (Port 51248)    |
|                                              |
|              [ Übernehmen ]   [ Schließen ]  |
+----------------------------------------------+
```

Die Liste wird **zur Laufzeit aus der Widget-Registry** gefüllt, nicht aus einer
Konstanten im Dialogcode. Ein neues Widget erscheint dadurch automatisch — das ist die
Hälfte des Erweiterbarkeitsversprechens aus [widgets.md](widgets.md).

Die Statuszeile ist Absicht: "Nichts passiert auf dem Display" hat mehrere mögliche
Ursachen (GG läuft nicht, Gerät nicht gefunden, Bind abgelehnt), und ohne diese Anzeige
sucht man im Log statt im Dialog.

## Hot-Reload

*Übernehmen* schreibt die Datei und weckt den Worker. Kein TeamSpeak-Neustart.

Die Config wird über einen `shared_ptr<const Config>` weitergereicht: Der Dialog-Thread
baut eine neue Instanz und tauscht den Zeiger atomar; der Worker nimmt zu Beginn jedes
Ticks eine Kopie des Zeigers. Damit braucht der Rendering-Pfad kein Lock, und ein
laufender Frame arbeitet die alte Config zu Ende, statt mittendrin zu wechseln.

## Migration

`version` ist eine Ganzzahl. Bei einer inkompatiblen Änderung wird hochgezählt und in
`config.cpp` eine Migrationsfunktion ergänzt. Ist `version` **höher** als bekannt, wird
die Datei nicht überschrieben, sondern mit Defaults gearbeitet und geloggt — sonst
zerstört ein Downgrade die Einstellungen.
