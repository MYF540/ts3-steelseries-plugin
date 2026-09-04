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

    // Höher gewinnt, wenn mehr Widgets liefern als Zeilen frei sind.
    // Normal 0. Für Dringendes (z. B. "du sprichst ins stumme Mikro") höher.
    int priority = 0;

    // Wunsch-Icon. Es gibt nur EINES pro Frame -- siehe unten, wer gewinnt.
    Icon icon = Icon::None;
};

struct RenderContext {
    int widthPx;    // 128 auf der Nova-Pro-Basisstation
    int heightPx;   // 64
    int maxLines;   // wie viele Textzeilen der Composer noch vergeben kann
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

> **`nullopt` gibt den Schirm frei.** Seit [ADR 0006](decisions/0006-event-driven-screen-ownership.md)
> hat `nullopt` eine zweite, gewichtigere Bedeutung: Liefern **alle** aktivierten Widgets
> `nullopt`, gibt das Plugin das Display an SteelSeries GG zurück.
>
> Ein Widget, das immer etwas liefert, hält den Schirm damit für immer — und zerhackt
> die Anzeige anderer Apps, weil GG bei Konkurrenz sichtbar zwischen ihnen wechselt.
>
> **Faustregel beim Schreiben eines Widgets:** Frage nicht „was könnte ich anzeigen?",
> sondern „lohnt es sich, dem Nutzer dafür gerade seine Musikanzeige wegzunehmen?".
> Im Zweifel `nullopt`.

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
1. Widgets aus der Config in konfigurierter Reihenfolge holen
     (deaktivierte und unbekannte IDs überspringen)
2. Für jedes: render() mit dem restlichen Zeilenbudget aufrufen
3. nullopt -> überspringen, kein Platzverbrauch
4. Ausgaben nach priority absteigend stabil sortieren
     (stabil: bei gleicher priority bleibt die Nutzerreihenfolge erhalten)
5. Zeilen füllen, bis das Budget erschöpft ist (max_lines, Default 3)
6. Icon des ERSTEN beitragenden Widgets mit icon != None übernehmen
7. Nicht kürzen -- GG lässt zu lange Zeilen von selbst durchlaufen
8. Ist gar nichts zusammengekommen: leerer Frame -> Schirm freigeben
```

Punkt 4 ist der Grund, warum `priority` existiert: Die Nutzerreihenfolge gilt, *außer*
etwas ist wirklich dringend. "Du sprichst, aber dein Mikro ist stumm"
(`STATUS_TALKING_WHILE_DISABLED`) soll durchschlagen, auch wenn der Nutzer das
Mute-Widget nach unten sortiert hat.

### Punkt 6: Es gibt nur ein Icon

GameSense setzt `icon-id` auf den **Frame**, nicht auf die Zeile. Drei Zeilen aus drei
Widgets teilen sich also ein einziges Icon.

Die Regel ist deshalb: **Das oberste beitragende Widget, das ein Icon wünscht, bekommt
es.** Da Sortierung nach `priority` bereits stattgefunden hat, gewinnt damit
automatisch das Dringlichste — spricht jemand, steht das Talking-Icon da, auch wenn
darunter noch der Channel steht.

Ein Widget darf `Icon::None` liefern und trotzdem Zeilen beitragen. Das ist der
Normalfall für `channel_info`: Es hat nichts Symbolisches zu sagen und überlässt die
32 Pixel gern jemandem, der etwas davon hat.

### Punkt 7: Nicht kürzen

Text, der nicht in die Zeile passt, lässt GG von selbst durchlaufen (beobachtet an der
NowPlaying-App). Ein hart auf 18 Zeichen abgeschnittener Channelname wäre schlechter als
einer, der scrollt. Kürzung bleibt Notbremse für absurde Längen, nicht Regelfall.

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

| id | displayName | zeigt | Icon | `nullopt` wenn |
|---|---|---|---|---|
| `connection` | Verbindungsstatus | Verbinde…, Getrennt, Verbindung verloren | `Connect` / `Disconnect` | verbunden und ruhig |
| `channel_info` | Channel | Channelname + Nutzerzahl | `None` | nicht verbunden, oder seit `hold_ms` unverändert |
| `talkers` | Wer spricht | Nicknames der Sprechenden im eigenen Channel | `Talking` | niemand spricht |
| `mute_status` | Mute / Deaf | Mikro stumm, Deaf, Away | `Muted` | nichts davon aktiv |

`connection` zeigt bewusst **nur Übergänge und Probleme**, nicht den Dauerzustand
„verbunden": Sonst hielte es den Schirm permanent und höbe ADR 0006 auf. Dass die
Verbindung steht, erkennt man daran, dass sich niemand beschwert.

`channel_info` ist der Grenzfall: Der Channelname ist ein Dauerzustand, aber ein
Channelwechsel ist ein Ereignis. Deshalb meldet es sich beim Wechsel und verstummt
danach — sonst wäre der Schirm nach jedem Channelwechsel für immer belegt.

## Ideen für später

Kosten jeweils eine Datei, kein Umbau:

- `talking_while_muted` — Vollbildwarnung bei `STATUS_TALKING_WHILE_DISABLED`
- `clock` — Uhrzeit als Ruhebild, wenn sonst nichts anliegt
- `channel_roster` — alle im Channel, nicht nur die Sprechenden
- `poke` / `chat` — letzte private Nachricht (`onTextMessageEvent`)
- `whisper` — wer flüstert gerade (`isReceivedWhisper` liegt bereits im Callback an)
