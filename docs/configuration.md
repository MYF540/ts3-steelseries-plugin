# Konfiguration

## Speicherort

```
%APPDATA%\TS3Client\plugins\ts3_steelseries\config.json
```

Unterhalb des TeamSpeak-Plugin-Verzeichnisses, weil die Einstellung zum Plugin gehört
und mit einem TS3-Profil zusammen wandert. Existiert die Datei nicht, wird sie beim
ersten Start mit Standardwerten angelegt.

## Schema

Die Datei wird beim ersten Start angelegt und enthält **jedes Widget, das der Build
kennt** — man muss also nichts von Hand ergänzen, um zu sehen, was es gibt.

```json
{
  "version": 1,

  "display": {
    "max_lines": 3,
    "chars_with_icon": 12,
    "chars_without_icon": 16,
    "hold_ms": 6000
  },

  "widgets": [
    { "id": "talkers",             "enabled": true, "duration_ms": 5000 },
    { "id": "talking_while_muted", "enabled": true, "duration_ms": 5000 },
    { "id": "poke",                "enabled": true, "duration_ms": 8000 },
    { "id": "chat_message",        "enabled": true, "duration_ms": 6000 },
    { "id": "server_join",         "enabled": true, "duration_ms": 6000 },
    { "id": "channel_join",        "enabled": true, "duration_ms": 5000 },
    { "id": "connection",          "enabled": true, "duration_ms": 5000 },
    { "id": "connection_quality",  "enabled": true, "duration_ms": 5000 },
    { "id": "mute_status",         "enabled": true, "duration_ms": 4000 },
    { "id": "channel_info",        "enabled": true, "duration_ms": 4000 }
  ],

  "buddies": []
}
```

### `duration_ms`

Wie lange das Ereignis dieses Widgets auf dem Display bleibt. **Beim Laden auf
1000–60000 ms begrenzt.**

Die Obergrenze ist kein willkürlicher Rundungswert: Eine „Dauer" von Stunden wäre in
Wirklichkeit eine Dauerbelegung des Displays — genau das, was
[ADR 0006](decisions/0006-event-driven-screen-ownership.md) und
[ADR 0007](decisions/0007-transient-vs-persistent.md) verhindern sollen. Die Untergrenze
sorgt dafür, dass eine Meldung überhaupt lesbar ist.

Bei `talkers` und `talking_while_muted` ist der Wert wirkungslos: Diese Widgets zeigen
an, solange tatsächlich jemand spricht, nicht für eine feste Zeit.

### `buddies`

Liste von **`CLIENT_UNIQUE_IDENTIFIER`**-Werten (nicht Nicknames — die ändern sich, die
Identität nicht).

Diese eigene Liste ist eine Notwendigkeit, kein Entwurfsgeschmack: **TeamSpeaks eigene
Freunde-/Feinde-Verwaltung ist clientintern und wird Plugins überhaupt nicht angeboten.**
Eine Suche über die gesamten SDK-Header findet keinen einzigen Enum-Wert und keine
Funktion dafür.

Solange die Liste leer ist, meldet `server_join` nichts. Das ist Absicht: Auf einem
gut besuchten Server wäre jede Verbindung eine Displayübernahme.

> **Offene Baustelle:** UIDs von Hand einzutragen ist zumutbar, aber unschön. Vorgesehen
> ist ein Kontextmenü-Eintrag („Als Buddy merken") über `ts3plugin_initMenus`, mit dem
> man jemanden im Client per Rechtsklick aufnimmt.

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
