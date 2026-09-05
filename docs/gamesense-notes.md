# GameSense — recherchierte Fakten

Quelle: [SteelSeries/gamesense-sdk](https://github.com/SteelSeries/gamesense-sdk),
insbesondere `doc/api/sending-game-events.md` und `doc/api/json-handlers-screen.md`.

> **Statuskennzeichnung:** Alles hier ist aus der SDK-Doku übernommen. Was Phase 0
> tatsächlich ergeben hat, steht in der Tabelle.

| # | Annahme | Ergebnis (Phase 0, 2026-09-05) |
|---|---|---|
| 1 | `coreProps.json` vorhanden, Adresse antwortet | **bestätigt** |
| 2 | `screened-128x64` wird als device-type akzeptiert | **widerlegt** — der Typ existiert nicht; korrekt ist `screened` |
| 3 | Frames erscheinen sichtbar auf der Nova-Pro-Basisstation | **bestätigt** mit `screened` |
| 4 | Display fällt nach Heartbeat-Stopp an GG zurück | **bestätigt** — und `remove_game` gibt sofort frei |
| 5 | `image-data` (1bpp-Bitmap) funktioniert | **nein** — weder statisch noch dynamisch |
| 6 | Nutzbare Zeilenzahl | **3 Zeilen**, kein horizontales Abschneiden |
| 7 | Verhalten bei konkurrierenden Apps | **Wechsel mit sichtbarem Flackern** |

Phase 0 ist damit abgeschlossen. Zwei Ergebnisse haben den Entwurf verändert:
Punkt 5 kippt den Bitmap-Pfad, Punkt 7 führt zu
[ADR 0006](decisions/0006-event-driven-screen-ownership.md).

## Bestätigter Aufruf

Dieser Payload hat auf der Arctis-Nova-Pro-Basisstation nachweislich Text angezeigt —
zwei Zeilen, über Frame-Kontext gefüllt:

```json
POST /bind_game_event
{
  "game": "TS3_OLED_PROBE", "event": "PROBE1", "value_optional": true,
  "handlers": [{
    "device-type": "screened", "zone": "one", "mode": "screen",
    "datas": [{ "lines": [
      { "has-text": true, "context-frame-key": "l1", "bold": true },
      { "has-text": true, "context-frame-key": "l2" }
    ]}]
  }]
}

POST /game_event
{ "game": "TS3_OLED_PROBE", "event": "PROBE1",
  "data": { "frame": { "l1": "TYP 1", "l2": "screened" } } }
```

Damit sind gleich vier Dinge belegt: `screened` trifft das Gerät, mehrzeilige Frames
funktionieren, `context-frame-key` wird aufgelöst, und `value_optional: true` genügt —
ein numerischer `value` ist nicht nötig.

## Den `device-type` nicht an der Auflösung festmachen

Der erste Probe-Durchlauf nutzte `screened-128x64` und zeigte nichts an. Der Grund ist
banal: **diesen Typ gibt es nicht.** `doc/api/standard-zones.md` listet nur
`screened-128x36` (Rival 700), `screened-128x40` (Apex), `screened-128x48` (Arctis Pro
Wireless) und `screened-128x52` (GameDAC) — alles Vorgängergeneration, kein Eintrag für
die Nova Pro.

**Das heißt aber nicht, dass das Display unerreichbar wäre.** Die Nova-Pro-Basisstation
zeigt nachweislich App-Inhalte: NowPlaying stellt Titel, Interpret und einen
Fortschrittsbalken dar, die Counter-Strike-2-App rotiert durch Match-Statistiken.

Der richtige Wert ist der **generische** `"screened"` — er passt laut Doku auf jedes
Gerät mit Schirm, unabhängig von der Auflösung. Genau so binden GGs eigene Spielpakete:
Eine Auswertung von `GG\apps\engine\db\database.db` findet als Screen-Typen
ausschließlich `screened`, `screened-2-lines` und `screened-3-lines-or-more` —
auflösungsfrei, ohne eine einzige Pixelangabe.

> **Verworfene Fehlspur, damit sie niemand wiederholt:** Dieselbe Datenbank enthält
> Schlüssel der Form `<gerät>_oled_display_sequence`, und die gibt es nur für
> `apex_2022`, `macho` (beides Tastaturen) und `rival_700`. Daraus lässt sich *nicht*
> schließen, dass Arctis-Geräte keine Screen-Ziele sind: Das ist der Mechanismus für
> gespeicherte OLED-Sequenzen auf Tastaturen und Mäusen, nicht der Screen-Handler-Pfad
> des SDK. Die beiden Wege haben nichts miteinander zu tun.

### Die eigentliche Falle: lautloses Scheitern

Ein `device-type`, auf den kein angeschlossenes Gerät passt, schlägt **ohne jede
Rückmeldung** fehl. GG speichert den Handler, quittiert mit HTTP 200 und zeigt die App
sogar in der Oberfläche an. Weder Fehlermeldung noch Log-Eintrag.

Deshalb prüft `tools/gamesense-probe.ps1` eine ganze Matrix von Typen und fragt nach
jedem, ob etwas zu sehen war: Der Rückgabewert des Servers ist als Erfolgssignal
wertlos.

## Server finden

Windows: `%PROGRAMDATA%\SteelSeries\SteelSeries Engine 3\coreProps.json`

```json
{ "address": "127.0.0.1:51248" }
```

Der Port wechselt zwischen GG-Starts. Also **bei jedem `ts3plugin_init` neu lesen**, nicht
cachen. Fehlt die Datei, läuft GG nicht — das ist ein Normalzustand, kein Fehler.

## Endpunkte

Alle als `POST` mit `Content-Type: application/json` an `http://<address>/<endpoint>`.

| Endpunkt | Zweck |
|---|---|
| `/game_metadata` | Spiel anmelden, Anzeigename, Deinitialize-Timer |
| `/bind_game_event` | Event **inklusive Handler** definieren (das brauchen wir) |
| `/register_game_event` | Event ohne Handler (Nutzer konfiguriert in GG) — nicht genutzt |
| `/game_event` | Zustandsupdate senden |
| `/multiple_game_events` | Mehrere Events gebündelt (Engine 3.15.4+) |
| `/game_heartbeat` | Am Leben halten, ohne Zustandsänderung |
| `/remove_game` | Beim Shutdown abmelden |
| `/supports_multiple_game_events` | `GET`, 200 = unterstützt |

## Namensregeln

Spiel- und Eventnamen dürfen **nur** `A-Z`, `0-9`, `-` und `_` enthalten. Kleinbuchstaben
sind nicht erlaubt.

Für dieses Projekt festgelegt:

- Spiel: `TS3_OLED`
- Event: `STATUS`

## Lebenszyklus und Heartbeat

`deinitialize_timer_length_ms` ist auf 1000–60000 ms begrenzt, Default 15000. Bleibt länger
als dieser Zeitraum jeder Request aus, deinitialisiert GG das Spiel und gibt das Display
frei.

Wir setzen 15000 und senden alle **8 s** einen Heartbeat — genug Reserve für einen
verschluckten Request, ohne GG unnötig zu belasten.

Der Deinitialize-Mechanismus ist gleichzeitig unsere Aufräumhilfe: Stürzt TeamSpeak ab,
bevor `/remove_game` gesendet wurde, gibt GG das Display nach 15 s von selbst frei.

## Metadaten anmelden

```json
POST /game_metadata
{
  "game": "TS3_OLED",
  "game_display_name": "TeamSpeak 3",
  "developer": "ts3-steelseries-plugin",
  "deinitialize_timer_length_ms": 15000
}
```

## Screen-Handler binden

Für Textzeilen (Phase 1). `value_optional: true` ist hier **zwingend** — wir steuern die
Anzeige über `frame`-Kontext, nicht über einen Zahlenwert:

```json
POST /bind_game_event
{
  "game": "TS3_OLED",
  "event": "STATUS",
  "value_optional": true,
  "handlers": [
    {
      "device-type": "screened",
      "zone": "one",
      "mode": "screen",
      "datas": [
        {
          "lines": [
            { "has-text": true, "context-frame-key": "line1", "bold": true },
            { "has-text": true, "context-frame-key": "line2" },
            { "has-text": true, "context-frame-key": "line3" }
          ]
        }
      ]
    }
  ]
}
```

device-type-Werte laut Doku: `"screened"` (jedes Gerät mit Schirm) oder
`"screened-BREITExHÖHE"`. **Für die Nova-Pro-Basisstation ist `"screened"` der richtige
Wert** — der auflösungsspezifische Katalog endet bei der Vorgängergeneration.

Folge für den Composer: Er darf sich nicht auf 128x64 verlassen, sondern bekommt die
Maße über `RenderContext`. Bei reinen Textzeilen ist das ohnehin unkritisch, weil GG das
Layout übernimmt; erst der Bitmap-Pfad (Phase 5) bräuchte eine feste Auflösung.

## Icons

`icon-id` steht auf dem **Frame** (dem Objekt in `datas`), nicht auf einer einzelnen
Zeile. Laut Doku belegt das Icon die **32 linkesten Pixel**, der Text bekommt den Rest.

```json
"datas": [ { "has-text": true, "suffix": "stuff", "icon-id": 16 } ]
```

Die Liste aus `doc/api/event-icons.md` enthält vier Icons, die für dieses Projekt wie
gemacht sind:

| ID | Icon | Verwendung |
|---|---|---|
| **19** | Muted | Mikro stumm / Deaf |
| **20** | Talking | jemand spricht |
| **21** | Connect | verbunden |
| **22** | Disconnect | getrennt |
| 15 | Clock | ggf. Ruhebild |
| 0 | kein Icon | volle Textbreite |

Weitere vorhandene, hier nicht einschlägige: 1–14 (Spielwerte), 16 Lightning,
17 Item, 18 @, 23 Music, 24 Play, 25 Pause, 27–29 CPU/GPU/RAM, 30–43 MOBA-Werte.

**Zwei Folgen für den Entwurf:**

1. **Ein Icon pro Frame, nicht pro Zeile.** Zeigt ein Frame drei Zeilen aus drei
   Widgets, gibt es trotzdem nur ein Icon. Der Composer muss also entscheiden, welches
   — siehe [widgets.md](widgets.md).
2. **Mit Icon steht weniger Textbreite zur Verfügung.** 32 der 128 Pixel sind weg. Wo
   Platz wichtiger ist als Symbolik, ist `icon-id: 0` richtig.

## Es gibt keinen Bildlauf — der Composer muss kürzen

**Gemessen mit `gamesense-capabilities.ps1 -Only F`:**

| Layout | lesbar bis | Zeichen | Bildlauf |
|---|---|---|---|
| ohne Icon | `P` (angeschnitten) | **~15–16** | nein |
| mit Icon | `L` | **12** | nein |
| sehr langer Text | — | — | nein |
| 3 Zeilen mit Icon *(Phase 1)* | — | **~11–12** | nein |

### Kein Bildlauf. Nirgends.

Auch ein absichtlich viel zu langer Text lief nicht durch — er wurde abgeschnitten.

Die Bildlauf-Beobachtung an der NowPlaying-App gilt also **nicht** für JSON-Texthandler.
NowPlaying erreicht das offenbar anders (GoLisp-Handler, `arg`-Ausdrücke oder eine
Mehrbild-Sequenz). Für unseren Weg heißt das schlicht: **Was nicht passt, ist weg.**

Damit ist die Regel eindeutig: **Der Composer muss kürzen**, und Widgets müssen von sich
aus kurz fassen. Eine frühere Fassung dieses Dokuments behauptete das Gegenteil — das war
aus dem Verhalten einer fremden App geschlossen und ist widerlegt.

### Rund 8 px je Zeichen, überall gleich

Die Zahlen sind über beide Messungen hinweg konsistent:

- ohne Icon: 128 px / ~16 Zeichen = **8 px je Zeichen**
- mit Icon: 96 px / 12 Zeichen = **8 px je Zeichen**

Die Schriftgröße ist also fest; das Icon kostet exakt die dokumentierten 32 px, was
**vier Zeichen** entspricht. Als Faustformel für den Composer:

```
Zeichen je Zeile = 16 ohne Icon,  12 mit Icon
```

> **Kurz verfolgte Fehlspur:** Zwischenzeitlich sah es so aus, als passten ohne Icon nur
> sechs Zeichen — was auf eine layoutabhängige Schriftgröße hingedeutet hätte. Ursache
> war eine Fehlablesung des Lineals: Das abgeschnittene `P` sah aus wie ein `F`. Wer
> diesen Test wiederholt, sollte beim letzten Zeichen genau hinsehen.

### Nicht zu verwechseln mit `wrap`

`wrap` ist laut Doku die Anzahl **zusätzlicher Umbruchzeilen** für eine Textzeile
(Default 0), kein Bildlauf. Bei drei verfügbaren Zeilen verbraucht `wrap: 1` zwei davon.

## Mehrbild-Rotation

`length-millis` steuert die Standzeit eines Frames, `repeats` das Wiederholen der
Sequenz. So rotiert die Counter-Strike-2-App durch ihre Statistiken — in GGs Datenbank
nachweisbar:

```json
{"has-text":true,"prefix":"Round ","icon-id":15,"length-millis":3000}
{"has-text":true,"prefix":"Team Score: ","length-millis":3000,"repeats":0}
```

Damit ist belegt, dass Mehrbild-Sequenzen auf diesem Gerät funktionieren — und dass
**jedes Bild ein eigenes Icon** haben kann.

Für dieses Projekt vorerst nicht genutzt: Bei „wer spricht gerade" ist Unmittelbarkeit
wichtiger als Informationsmenge. Wer drei Sekunden auf das richtige Bild warten muss,
verpasst die Antwort. Als spätere Option (`display.layout: "rotate"`) bleibt es offen.

## Zustand senden

```json
POST /game_event
{
  "game": "TS3_OLED",
  "event": "STATUS",
  "data": {
    "frame": {
      "line1": "Lobby",
      "line2": "Anna spricht",
      "line3": "MIC AUS"
    }
  }
}
```

Die Schlüssel unter `frame` sind frei wählbar; sie müssen nur zu den
`context-frame-key`-Werten im Handler passen.

## Bitmap: funktioniert auf diesem Gerät nicht

Gemessen, nicht vermutet. Beide dokumentierten Wege wurden mit einem 1024-Byte-Schachbrett
(128x64, 1 bpp, zeilenweise, MSB zuerst) geprüft:

| Variante | Handler | Ergebnis |
|---|---|---|
| statisch | `{ "has-text": false, "image-data": [...] }` | **GG-Oberfläche blieb stehen** |
| dynamisch | `{ "has-text": false, "context-frame-key": "image-data-128x64" }`, Bytes im Frame | **Display schwarz** |

Der Unterschied ist die eigentliche Information:

- **Statisch** übernahm GG den Schirm gar nicht erst — der Handler wurde verworfen.
- **Dynamisch** übernahm GG den Schirm sehr wohl (die GG-Oberfläche verschwand), zeichnete
  aber nichts. Der Handler war also formal gültig, nur die Bilddaten kamen nicht an.

Das zweite Ergebnis ist der interessantere Ansatzpunkt, falls der Bitmap-Pfad je wieder
gebraucht wird: Der Weg ist nicht grundsätzlich versperrt, es passt nur etwas an den
Daten nicht. Denkbare Ursachen, keine davon geprüft:

- Die für GameSense maßgebliche Auflösung ist nicht 128x64, womit der Schlüsselname
  `image-data-128x64` ins Leere zeigt. Der offizielle Katalog kennt für Arctis-Geräte
  nur 128x48 und 128x52 — die Nova Pro fehlt dort ganz.
- Der Handler verweist womöglich anders auf den Frame-Schlüssel; die SDK-Doku sagt dazu
  nichts Eindeutiges.

**Konsequenz für die Roadmap:** Phase 5 (eigenes Pixel-Rendering) ist blockiert und wird
nicht weiterverfolgt. Das ist verkraftbar, weil drei Textzeilen für Mute-Status,
Sprecher und Channel ausreichen — GG rendert die Schrift, und laut Messung wird nichts
abgeschnitten.

### Bitreihenfolge: Falle beim Vergleich mit ggoled

Der direkte USB-HID-Weg ([ggoled](https://github.com/JerwuQu/ggoled)) packt **spaltenweise**
mit `(x * padded_h + y) / 8`. GameSense packt **zeilenweise**. Wer Referenzcode von dort
übernimmt, muss die Packroutine austauschen — sonst erscheint transponierter Pixelmatsch.

## Bekanntes Risiko: Issue #66

[Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66) — "Arctis Pro Wireless
OLED problems", offen, ungefixt:

- Gleichzeitige Requests aus mehreren Quellen können den Display-Inhalt zerstören
  ("scrambled"), teils ist ein Hard-Reboot der Basisstation nötig.
- Der Melder half sich, indem er das Discord-Plugin deaktivierte.
- Das SDK hat **keine** eingebaute Warteschlange für ein beschäftigtes Gerät.

Unsere Gegenmaßnahmen (siehe [architecture.md](architecture.md)):

- Genau ein Thread sendet, streng seriell, nie überlappend.
- Rate-Limit mit Coalescing, damit Event-Bursts nicht zu Request-Bursts werden.
- Kein Frame-Versand, wenn sich der Inhalt nicht geändert hat.

Was wir **nicht** verhindern können: andere GameSense-Anwendungen, die parallel auf
dasselbe Gerät schreiben. Gehört in die README als bekannte Einschränkung.
