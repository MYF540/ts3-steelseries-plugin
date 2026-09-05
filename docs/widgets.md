# Widgets — ein Feature hinzufügen

Dieses Dokument ist der Vertrag für die Erweiterbarkeit. Wenn "noch etwas anzeigen"
mehr kostet als *eine neue Datei plus eine Zeile*, ist der Entwurf kaputt und gehört
repariert, nicht umgangen.

## Was ein Widget ist

Ein Widget beantwortet genau eine Frage: **"Was möchte ich, gegeben den aktuellen
Zustand, auf dem Display sehen?"**

Es kennt weder GameSense noch HTTP, weder TeamSpeak-Header noch Bitmaps. Es bekommt
einen `ClientState` und gibt Zeilen zurück. Das macht jedes Widget ohne laufenden
TeamSpeak-Client und ohne angeschlossenes Headset testbar.

## Die Schnittstelle

`src/widgets/widget.h`:

```cpp
// GameSense-Icon-IDs aus doc/api/event-icons.md. Nur die hier relevanten.
enum class Icon : int {
    None       = 0,
    Clock      = 15,
    Muted      = 19,
    Talking    = 20,
    Connect    = 21,
    Disconnect = 22,
};

struct WidgetOutput {
    std::vector<std::string> lines;

    // Wunsch-Icon. Es gibt nur EINES pro Frame -- siehe unten, wer gewinnt.
    Icon icon = Icon::None;

    // Höher gewinnt, wenn mehr Widgets liefern als Zeilen frei sind.
    // Normal 0. Für Dringendes (z. B. "du sprichst ins stumme Mikro") höher.
    int priority = 0;

    // Rechtfertigt das, dem Nutzer die Anzeige wegzunehmen?
    //
    //   true  = etwas ist gerade passiert (jemand spricht, Poke, Mute umgeschaltet)
    //   false = etwas ist gerade wahr (du bist stumm, der Channel heißt X)
    //
    // Nur ein true kann den Schirm beanspruchen. Der Default false ist die sichere
    // Wahl: Wer nichts angibt, kann das Display nicht dauerhaft belegen.
    bool demandsScreen = false;
};

struct RenderContext {
    int maxLines;         // wie viele Zeilen der Composer noch vergeben kann
    int maxCharsPerLine;  // harte Grenze -- was darüber steht, ist weg (kein Bildlauf)
};

class IWidget {
public:
    virtual ~IWidget() = default;

    virtual std::string_view id() const = 0;
    virtual std::string_view displayName() const = 0;

    virtual std::optional<WidgetOutput> render(const ClientState& state,
                                               const RenderContext& ctx) const = 0;
};
```

### Zu `id()`

Der Wert landet in `config.json` und identifiziert die Nutzerauswahl. **Er darf sich
nie ändern**, sonst verlieren bestehende Konfigurationen ihre Einstellung. Kleingeschrieben,
`snake_case`, ohne Versionsnummer: `talkers`, nicht `Talkers2`.

`displayName()` ist der Text im Konfigurationsdialog und darf sich jederzeit ändern.

### Zu `nullopt`

`nullopt` heißt "ich habe gerade nichts beizutragen" und ist der **Normalfall**, nicht
der Fehlerfall. Das Talker-Widget liefert `nullopt`, wenn niemand redet. Das
Mute-Widget liefert `nullopt`, wenn nichts stumm ist.

Genau das erlaubt dem Nutzer, mehr Widgets zu aktivieren, als gleichzeitig auf drei
Zeilen passen: Meistens schweigt die Mehrheit, und der Platz geht an das, was gerade
relevant ist.

> **Die wichtigere Frage ist aber `demandsScreen`.** Solange kein einziges Widget es auf
> `true` setzt, bleibt der Frame leer und das Display gehört SteelSeries GG — auch wenn
> Widgets Zeilen geliefert haben.
>
> Genau daran ist die erste Fassung gescheitert: Das Mute-Widget meldete korrekt und
> dauerhaft „Mikro aus" und hielt damit den Schirm, solange der Nutzer stumm war —
> potenziell stundenlang, mit genau dem Geflacker gegen NowPlaying, das
> [ADR 0006](decisions/0006-event-driven-screen-ownership.md) verhindern sollte.
> [ADR 0007](decisions/0007-transient-vs-persistent.md) trennt deshalb Ereignis und
> Dauerzustand.
>
> **Faustregel:** Frage nicht „was könnte ich anzeigen?", sondern „ist das gerade
> *passiert* — und lohnt es, dem Nutzer dafür seine Musikanzeige wegzunehmen?".

## Ein Widget hinzufügen

### 1. Datei anlegen

`src/widgets/ping.cpp` — Header nur, wenn andere den Typ brauchen; in der Regel reicht
die `.cpp`.

```cpp
#include "widgets/widget.h"
#include "widgets/registry.h"

namespace ts3ss {
namespace {

class PingWidget final : public IWidget {
public:
    std::string_view id() const override { return "ping"; }
    std::string_view displayName() const override { return "Ping zum Server"; }

    std::optional<WidgetOutput> render(const ClientState& state,
                                        const RenderContext&) const override {
        if (!state.connected || state.pingMs < 0)
            return std::nullopt;

        WidgetOutput out;
        out.lines = { std::to_string(state.pingMs) + " ms" };
        out.icon  = Icon::None;   // die 32 Icon-Pixel lieber für Dringenderes
        return out;
    }
};

TS3SS_REGISTER_WIDGET(PingWidget);

}  // namespace
}  // namespace ts3ss
```

Die anonyme `namespace`-Klammer ist Absicht: Widget-Klassen sind nie von außen sichtbar,
der einzige Zugang ist die Registry.

### 2. Fertig

CMake globbt `src/*.cpp` mit `CONFIGURE_DEPENDS`, die Datei wird also automatisch gebaut.
`TS3SS_REGISTER_WIDGET` trägt das Widget beim Programmstart in die Registry ein, und der
Konfigurationsdialog listet alles, was in der Registry steht. Weder Composer noch Dialog
noch Session müssen angefasst werden.

Braucht das Widget ein neues Datenfeld — wie `pingMs` oben —, kommen zwei Stellen dazu:
das Feld in `ClientState` und die Stelle in `src/plugin/`, die es befüllt. Das ist
unvermeidbar; alles andere bleibt unberührt.

## Wie der Composer entscheidet

```
1. Widgets in konfigurierter Reihenfolge holen
     (deaktivierte und unbekannte IDs überspringen)
2. Für jedes render() aufrufen; nullopt -> überspringen
3. Sortieren nach:  demandsScreen  ->  priority  ->  Nutzerreihenfolge
4. Icon vom obersten Beitrag übernehmen, der eines wünscht
5. Ohne Icon noch einmal rendern -- dann sind 16 statt 12 Zeichen frei
6. Zeilen füllen bis max_lines (3)
7. Hat NIEMAND demandsScreen gesetzt: leerer Frame -> Schirm freigeben
```

### Punkt 3: Wer den Schirm holt, führt

`demandsScreen` sortiert **vor** `priority`. Das ist nicht kosmetisch: Ohne diesen
Schlüssel nimmt ein bloß mitfahrendes Zustands-Widget dem Ereignis die oberste Zeile
weg, das die Anzeige überhaupt ausgelöst hat.

Genau das trat auf und wurde von einem Test gefangen: Beim Stummschalten stand
`"Lobby 3"` über `"Mikro aus"` — allein weil `channel_info` zufällig früher registriert
ist und beide Priorität 0 haben.

`priority` regelt danach die Dringlichkeit unter den Ereignissen: „Du sprichst ins
stumme Mikro" (Priorität 100) schlägt alles, auch wenn der Nutzer das Widget nach unten
sortiert hat.

### Punkt 4: Es gibt nur ein Icon

GameSense setzt `icon-id` auf den **Frame**, nicht auf die Zeile. Drei Zeilen aus drei
Widgets teilen sich also ein einziges Icon.

Die Regel ist deshalb: **Das oberste beitragende Widget, das ein Icon wünscht, bekommt
es.** Da Sortierung nach `priority` bereits stattgefunden hat, gewinnt damit
automatisch das Dringlichste — spricht jemand, steht das Talking-Icon da, auch wenn
darunter noch der Channel steht.

Ein Widget darf `Icon::None` liefern und trotzdem Zeilen beitragen. Das ist der
Normalfall für `channel_info`: Es hat nichts Symbolisches zu sagen und überlässt die
32 Pixel gern jemandem, der etwas davon hat.

### Punkt 7: Kürzen ist Pflicht

**Es gibt keinen Bildlauf.** Gemessen mit Alphabet-Lineal und einem absichtlich viel zu
langen Text: In keinem Fall lief etwas durch, alles wurde abgeschnitten.

Der Platz ist fest: rund 8 px je Zeichen, also **16 Zeichen ohne Icon, 12 mit Icon**
(das Icon belegt die 32 linkesten von 128 Pixeln). `RenderContext::maxCharsPerLine`
nennt den jeweils geltenden Wert.

Das ist die harte Grenze, an der Widgets entlangschreiben müssen:

| statt | besser |
|---|---|
| `"Anna Musterfrau spricht"` | `"Anna"` |
| `"Mikrofon stummgeschaltet"` | `"Mic aus"` |
| `"Channel: Lobby (4)"` | `"Lobby 4"` |

Der Composer kürzt zwar als Notbremse, aber ein Widget, das sich auf die Kürzung
verlässt, liefert unlesbaren Rumpf. `RenderContext` nennt die verfügbare Zeichenzahl —
benutze sie.

> Ganz früher stand hier „nicht kürzen, GG lässt durchlaufen". Das war aus dem Verhalten
> der NowPlaying-App geschlossen und ist für JSON-Texthandler widerlegt.

## Testen

Widgets sind reine Funktionen von `ClientState` nach `optional<WidgetOutput>` — also in
`tests/` direkt prüfbar:

```cpp
ClientState s;
s.connected  = true;
s.inputMuted = true;
CHECK(widget.render(s, ctx).has_value());
```

Für jedes Widget mindestens: der aktive Fall, der `nullopt`-Fall, und der Fall
"nicht verbunden".

## Die Widgets aus dem ursprünglichen Auftrag

| id | zeigt | Icon | Prio | beansprucht den Schirm |
|---|---|---|---|---|
| `talking_while_muted` | „MIKRO AUS! / du sprichst" | `Muted` | 100 | solange es zutrifft |
| `poke` | wer angestupst hat + Text | — | 50 | Ereignis (Default 8 s) |
| `connection_quality` | Ping hoch / Paketverlust | — | 45 | solange das Problem besteht |
| `connection` | Verbinde…, Getrennt | `Connect`/`Disconnect` | 40 | Ereignis (5 s) |
| `chat_message` | Absender + Anfang der Nachricht | — | 30 | Ereignis (6 s) |
| `server_join` | „Name / ist online" — nur Buddys | `Connect` | 25 | Ereignis (6 s) |
| `channel_join` | „Name / ist da" | `Connect` | 20 | Ereignis (5 s) |
| `talkers` | bis zu 3 Nicknames, einer je Zeile | `Talking` | 10 | solange jemand spricht |
| `mute_status` | Mikro aus / Ton aus / Abwesend | `Muted` | 0 | Ereignis (4 s) |
| `channel_info` | Channelname + Nutzerzahl | — | 0 | Ereignis (4 s) |

Alle Dauern sind pro Widget konfigurierbar (1–60 s), siehe
[configuration.md](configuration.md).

`connection_quality` meldet sich **nur bei schlechten Werten** (ab 150 ms Ping bzw. 2 %
Paketverlust). Ein dauerhaft eingeblendeter Ping wäre ein Zustand und keine Information:
Ein guter Ping sagt einem nichts, was man wissen musste.

`server_join` bleibt still, solange die Buddy-Liste leer ist — sonst wäre auf einem gut
besuchten Server jede Verbindung eine Displayübernahme.

Die letzten beiden sind die Zustands-Widgets: Sie zeigen dauerhaft etwas an, **fordern
den Schirm aber nur direkt nach der Änderung**. Danach fahren sie nur noch mit, wenn ein
anderes Widget die Anzeige ohnehin geholt hat — und genau dort sind sie nützlich, weil
man dann sieht, *wer* spricht und *wo*.

`connection` meldet bewusst nie den Dauerzustand „verbunden". Dass die Verbindung steht,
erkennt man daran, dass sich niemand beschwert.

`poke` und `chat_message` verzichten auf ein Icon: Es gibt keins, das passt, und die
32 Pixel sind für den Absendernamen wertvoller.

## Ideen für später

Kosten jeweils eine Datei, kein Umbau:

- `talking_while_muted` — Vollbildwarnung bei `STATUS_TALKING_WHILE_DISABLED`
- `clock` — Uhrzeit als Ruhebild, wenn sonst nichts anliegt
- `channel_roster` — alle im Channel, nicht nur die Sprechenden
- `poke` / `chat` — letzte private Nachricht (`onTextMessageEvent`)
- `whisper` — wer flüstert gerade (`isReceivedWhisper` liegt bereits im Callback an)
