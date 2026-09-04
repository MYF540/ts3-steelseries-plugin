# ADR 0002 — GameSense-SDK statt direktem USB-HID-Zugriff

**Status:** akzeptiert (2026-09-05) — **durch Phase 0 bestätigt**

> **Nachtrag aus Phase 0.** `tools/gamesense-probe.ps1` zeigt mit
> `"device-type": "screened"` Text auf der Arctis-Nova-Pro-Basisstation an. Der
> Vorbehalt unten ist damit aufgelöst.
>
> Der erste Durchlauf blieb leer, weil er `screened-128x64` verwendete — **einen Typ,
> den es nicht gibt**. Auflösungsspezifische Werte existieren nur für die
> Vorgängergeneration; für die Nova Pro ist das generische `screened` richtig, mit dem
> auch GGs eigene Spielpakete binden.
>
> Zwischenzeitlich galt diese ADR aufgrund einer Fehlinterpretation von GGs Datenbank
> als gefallen. [ADR 0005](0005-usb-hid-display-transport.md) hält den USB-HID-Weg
> seitdem als durchgerechneten Rückfall fest, ist aber nicht in Kraft.

## Kontext

Das OLED der Arctis-Nova-Pro-Basisstation (128x64, monochrom) lässt sich auf zwei
grundverschiedene Arten beschreiben.

### Weg A — GameSense-SDK

HTTP-POSTs an SteelSeries GG auf `127.0.0.1`. Adresse steht in `coreProps.json`.
Offiziell dokumentiert, von SteelSeries vorgesehen.

### Weg B — direkter USB-HID-Zugriff

Am GG-Ökosystem vorbei direkt auf das Gerät schreiben. Das Protokoll ist durch
[ggoled](https://github.com/JerwuQu/ggoled) und
[arctis-nova-oled](https://github.com/Piipperi/arctis-nova-oled) reverse-engineered:

- VID `0x1038`, PID `0x12e0` (Nova Pro Wireless), Interface `4`
- Report-ID `0x06`, Zeichenbefehl `0x93` mit x / y / Breite / Höhe, danach Bitmap
- Bitpackung **spaltenweise**, `(x * padded_h + y) / 8`
- `0x95` gibt das Display an die GG-Oberfläche zurück, `0x85` setzt die Helligkeit
- 128x64, bis zu 60 fps, GG muss nicht laufen

## Entscheidung

**Weg A, GameSense-SDK.** Ausdrückliche Wahl des Nutzers, gestützt durch die Argumente
unten.

## Begründung

- **Offiziell und stabil.** Reverse-engineerte USB-Protokolle brechen bei
  Firmware-Updates. Ein OLED-Gimmick, das nach einem GG-Update stumm ausfällt, ist
  schlimmer als eines, das es nie gab.
- **Kein Kampf um das Gerät.** GG greift permanent auf die Basisstation zu. Weg B
  bedeutet, GG dieses HID-Interface streitig zu machen — mit unklarem Ausgang, wenn
  beide gleichzeitig laufen. Und GG *läuft*, weil es EQ, ChatMix und Akkuanzeige
  liefert, die niemand aufgeben will.
- **Weniger eigener Code.** Weg A kommt in Phase 1 ganz ohne Font, Layouter und
  Bitmap-Renderer aus: `has-text` plus `context-frame-key`, GG zeichnet. Weg B verlangt
  Pixelarbeit ab dem ersten Zeichen.
- **Sauberer Rückzug.** Bleiben die Heartbeats aus, gibt GG das Display nach 15 s von
  selbst frei — auch nach einem TeamSpeak-Absturz. Bei Weg B müssten wir das selbst
  garantieren, und ein Absturz ließe das Display in unserem letzten Bild stehen.

## Konsequenzen

- **GG muss laufen.** Ohne GG kein Display. Für dieses Projekt in Ordnung, weil GG auf
  einem Nova-Pro-System praktisch immer läuft.
- **Wir erben Issue #66.** [GameSense Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66)
  meldet zerstörte Displayinhalte bei parallelen Schreibern, offen und ungefixt. Was auf
  unserer Seite liegt, mildern wir (ein Sender-Thread, seriell, rate-limitiert,
  Coalescing); fremde GameSense-Anwendungen bleiben ein Restrisiko, das in die README
  gehört.
- **GG bestimmt das Timing.** Wann ein Frame erscheint und wie lange, entscheidet die
  Engine. Animationen im Millisekundenbereich sind so nicht machbar — für Statusanzeigen
  irrelevant.
- **Schriftbild gehört GG.** Weniger Kontrolle über Aussehen und Zeilenumbrüche.
  Phase 5 kann darauf mit `image-data` antworten.

## Offene Bestätigung — das Kill-Kriterium

Die GameSense-Doku sagt **nirgends explizit** zu, dass das OLED der
Nova-Pro-*Basisstation* über `screened-128x64` ansprechbar ist. Die vorhandene Doku
zielt auf Geräte wie Rival-Mäuse und Apex-Tastaturen; Issue #66 zeigt, dass gerade die
Arctis-Displays am SDK Probleme machen.

**Diese Entscheidung steht daher unter Vorbehalt, bis Phase 0 sie belegt hat.**
`tools/gamesense-probe.ps1` beantwortet die Frage ohne eine Zeile Plugin-Code.

Erscheint dort nichts auf dem Display, wird diese ADR durch eine Folge-ADR ersetzt und
Weg B neu bewertet — und zwar **bevor** Phase 1 beginnt, nicht danach.

## Falle bei einem späteren Wechsel

Die beiden Wege packen Bitmaps **unterschiedlich**:

| | Packung |
|---|---|
| GameSense `image-data` | zeilenweise, MSB zuerst, Ursprung oben links |
| USB-HID (ggoled) | spaltenweise, `(x * padded_h + y) / 8` |

Referenzcode von ggoled lässt sich also nicht direkt übernehmen. Deshalb liegt die
Packroutine in `src/render/bitmap.cpp` isoliert hinter einer Funktion und nicht verstreut
im Rendering-Code.
