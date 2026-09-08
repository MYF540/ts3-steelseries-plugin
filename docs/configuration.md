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
  "language": "auto",

  "display": {
    "max_lines": 3,
    "chars_with_icon": 12,
    "chars_without_icon": 16,
    "hold_ms": 6000,
    "max_talker_lines": 3,
    "hide_self_in_talkers": false,
    "hide_channel_while_talking": true
  },

  "thresholds": {
    "ping_ms": 150,
    "packet_loss_percent": 2.0
  },

  "logging": {
    "level": "info"
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

### `language`

`"auto"` (Standard), `"de"` oder `"en"`. `auto` folgt der Windows-Anzeigesprache:
Deutsch auf einem deutschen Windows, sonst Englisch.

Betrifft beides — die Texte auf dem Display *und* den Einstellungsdialog. Der
Sprachwechsel greift beim Speichern sofort; nur der bereits geöffnete Dialog behält
seine Beschriftungen bis zum nächsten Öffnen.

Übersetzt wird über eine Tabelle in `src/util/i18n.cpp`. Ein `static_assert` verknüpft
sie mit dem `Str`-Enum: Wer eine ID ergänzt und die Übersetzung vergisst, bekommt einen
Compile-Fehler statt einer leeren Zeile auf dem Display.

### `thresholds`

Ab wann `connection_quality` sich meldet.

| Schlüssel | Standard | Grenzen |
|---|---|---|
| `ping_ms` | 150 | 20 – 2000 |
| `packet_loss_percent` | 2.0 | 0.1 – 100 |

Konfigurierbar, weil „schlecht" von der Leitung abhängt und davon, wie bereitwillig man
sich unterbrechen lässt. Werte außerhalb der Grenzen werden beim Laden **begrenzt, nicht
abgelehnt** — ein unsinniger Wert soll das Plugin nicht anhalten, und ihn stillschweigend
zu ignorieren würde ratlos machen.

### `logging.level`

`"debug"`, `"info"` (Standard), `"warn"` oder `"error"`.

`debug` ist zum Nachstellen eines Problems gedacht, nicht für den Dauerbetrieb: Jede
Zeile wird sofort auf die Platte geschrieben, damit der letzte Eintrag vor einem Absturz
noch dasteht. Das ist genau dann Gold wert und sonst überflüssige Schreiblast.

Ziel ist `%APPDATA%\TS3Client\plugins\ts3_steelseries\ts3_steelseries.log`; zusätzlich
geht alles ins TeamSpeak-Clientlog.

### `duration_ms`

Wie lange das Ereignis dieses Widgets auf dem Display bleibt. **Beim Laden auf
1000–60000 ms begrenzt.**

Die Obergrenze ist kein willkürlicher Rundungswert: Eine „Dauer" von Stunden wäre in
Wirklichkeit eine Dauerbelegung des Displays — genau das, was
[ADR 0006](decisions/0006-event-driven-screen-ownership.md) und
[ADR 0007](decisions/0007-transient-vs-persistent.md) verhindern sollen. Die Untergrenze
sorgt dafür, dass eine Meldung überhaupt lesbar ist.

Bei `talking_while_muted` ist der Wert wirkungslos: Diese Anzeige gilt, solange die
Bedingung zutrifft, nicht für eine feste Zeit.

Bei **`talkers`** ist der Wert das **Nachleuchten**: wie lange ein Name nach dem
Verstummen stehen bleibt. Ohne das verschwindet ein Name bei jedem kurzen „ja" sofort
wieder, und in einem lebhaften Channel kommt die Anzeige nie zur Ruhe. Standard 3 s.

### `buddies`

Liste von **`CLIENT_UNIQUE_IDENTIFIER`**-Werten (nicht Nicknames — die ändern sich, die
Identität nicht).

Diese eigene Liste ist eine Notwendigkeit, kein Entwurfsgeschmack: **TeamSpeaks eigene
Freunde-/Feinde-Verwaltung ist clientintern und wird Plugins überhaupt nicht angeboten.**
Eine Suche über die gesamten SDK-Header findet keinen einzigen Enum-Wert und keine
Funktion dafür.

Solange die Liste leer ist, meldet `server_join` nichts. Das ist Absicht: Auf einem
gut besuchten Server wäre jede Verbindung eine Displayübernahme.

Zwei Wege, jemanden aufzunehmen:

- **Rechtsklick im Client** → *Plugins → TS3 SteelSeries OLED → Als Buddy merken*.
  Bequem, funktioniert aber nur bei Leuten, die gerade sichtbar sind.
- **UID im Dialog eintippen** und *Hinzufügen*. Das deckt alle anderen ab — und wer
  offline ist, ist genau die Person, für die eine „kommt online"-Meldung gedacht ist.

### `display.hold_ms`

Nachlaufzeit: Wie lange der Schirm nach dem letzten Inhalt noch belegt bleibt, bevor er
an SteelSeries GG zurückgeht. Zu kurz wirkt hektisch, zu lang nähert sich einer
Dauerbelegung an. 6 s ist ein Startwert, kein Messergebnis.

Dass überhaupt freigegeben wird, ist gemessen begründet: Bei paralleler GameSense-App
(NowPlaying, CS2) **wechselt** GG zwischen den Anzeigen und flackert sichtbar. Eine
Dauerbelegung würde die Musikanzeige zerhacken — ausführlich in
[ADR 0006](decisions/0006-event-driven-screen-ownership.md).

### `display.max_lines`

Standard 3 — das ist die auf der Arctis-Nova-Pro-Basisstation gemessene Zeilenzahl.
Gebunden wurden fünf Zeilen, dargestellt drei; horizontal wurde nichts abgeschnitten.

### `display.hide_channel_while_talking`

Blendet die Channel-Zeile aus, solange jemand spricht. **Standard `true`.**

In dem Moment zählen die Namen; wo man selbst ist, weiß man ohnehin. Nur echtes Sprechen
zählt, nicht das Nachleuchten der Liste — käme der Channel in der Sekunde zurück, in der
der Letzte verstummt, wäre genau das Zucken wieder da, das das Nachleuchten beseitigt.

### `display.max_talker_lines`

Wie viele der Zeilen die Sprecherliste belegen darf. **Standard 3**, also alle: Da die
Channel-Zeile beim Reden ohnehin beiseitetritt, konkurriert nichts mehr um den Platz.

Auf 1–3 begrenzt, zusätzlich nie größer als `max_lines`. Kleiner setzen, wer bei vielen
gleichzeitigen Sprechern lieber eine ruhigere Anzeige hat.

### `display.hide_self_in_talkers`

Blendet dich selbst aus der Sprecherliste aus. Standard `false`.

**Betrifft ausdrücklich nicht** die Warnung beim Sprechen ins stumme Mikrofon. Die ist
eine eigene Anzeige, handelt per Definition von dir, und sie zu unterdrücken würde die
nützlichste Meldung entfernen, die dieses Display überhaupt hat.

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

## Nicht konfigurierbar

Zwei GameSense-Werte stehen fest in `src/core/worker.h` bzw. `src/gamesense/session.h`,
weil an ihnen niemand ohne Not drehen sollte:

- **`minUpdateInterval` (120 ms)** — Untergrenze zwischen zwei `/game_event`-Requests.
  Ereignisse dazwischen werden zusammengefasst und als *ein* Frame gesendet. Das ist
  keine Optimierung, sondern eine Schutzmaßnahme gegen
  [Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66).
- **`deviceType` (`"screened"`)** — das generische, mit dem auch GGs eigene Spielpakete
  binden. Auflösungsspezifische Werte gibt es nur für ältere Geräte;
  `"screened-128x64"` existiert nicht, und ein falscher Typ scheitert **lautlos** (siehe
  [gamesense-notes.md](gamesense-notes.md)).

Sollte eine künftige GG-Version das nötig machen, sind beides Einzeiler — bis dahin sind
sie ein Angebot, sich das Display kaputtzukonfigurieren.

## Der Dialog

Zwei Wege dorthin:

- *Extras → Optionen → Addons →* Plugin auswählen *→ Einstellungen*
  (über `ts3plugin_offersConfigure` / `ts3plugin_configure`)
- Menüband *Plugins → TS3 SteelSeries OLED → Einstellungen*
  (globaler Menüeintrag über `ts3plugin_initMenus`)

Reiner Win32-Dialog aus `resources/plugin.rc`, kein Qt (siehe
[ADR 0001](decisions/0001-single-process-native-plugin.md)).

```
+---------------------------------------------------------------+
| TS3 SteelSeries OLED – Einstellungen                          |
|                                                               |
| Anzeigen (Reihenfolge = Priorität):                           |
| +------------------------------------------+  [ Nach oben  ]  |
| | [x] Wer spricht                    5 s   |  [ Nach unten ]  |
| | [x] Stumm gesprochen               5 s   |                  |
| | [x] Angestupst                     8 s   |  Dauer (1-60 s): |
| | [x] Ping / Paketverlust            5 s   |  [  8 ] [Setzen] |
| | [ ] Channel                        4 s   |                  |
| +------------------------------------------+                  |
|                                                               |
| Buddys (per Rechtsklick im Client):                           |
| +------------------------------------------+  [ Entfernen  ]  |
| | 8Vd6mLc0AbC...                           |                  |
| +------------------------------------------+                  |
|                                                               |
| GameSense verbunden (127.0.0.1:58558)   [Speichern] [Schließen]|
+---------------------------------------------------------------+
```

Die Liste wird **zur Laufzeit aus der Widget-Registry** gefüllt, nicht aus einer
Konstanten im Dialogcode. Ein neues Widget erscheint dadurch automatisch — das ist die
Hälfte des Erweiterbarkeitsversprechens aus [widgets.md](widgets.md).

Die Statuszeile ist Absicht: „Nichts passiert auf dem Display" hat mehrere mögliche
Ursachen (GG läuft nicht, Gerät nicht gefunden, Bind abgelehnt), und ohne diese Anzeige
sucht man im Log statt im Dialog.

### Warum der Dialog einen eigenen Thread bekommt

`ts3plugin_configure` wird von TeamSpeak auf einem eigens dafür erzeugten Thread
aufgerufen — dort wäre eine modale Schleife unbedenklich. Der Menüeintrag kommt aber
über `ts3plugin_onMenuItemEvent` auf dem **UI-Thread des Clients** an, und eine modale
Schleife würde TeamSpeak dort einfrieren.

Statt zweier Pfade, die man verwechseln kann, öffnen beide Einstiege denselben eigenen
Thread. `ConfigDialog::shutdown()` schließt das Fenster und wartet auf den Thread, bevor
die DLL entladen wird — sonst liefe er in bereits freigegebenem Code weiter.

### Buddys aufnehmen

Rechtsklick auf einen Nutzer im Client → *Plugins → TS3 SteelSeries OLED →
Als Buddy merken*. Gespeichert wird der `CLIENT_UNIQUE_IDENTIFIER`, nicht der Nickname —
so übersteht der Eintrag eine Umbenennung. Entfernen geht im Dialog.

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
