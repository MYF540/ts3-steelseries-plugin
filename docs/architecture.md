# Architektur

## Randbedingung, aus der alles folgt

Es gibt **genau einen Prozess**: den TeamSpeak-3-Client. Das Plugin ist eine DLL, die in
diesen Prozess geladen wird. Kein Daemon, kein Dienst, kein Tray-Programm.

Daraus folgen drei Invarianten, die den Rest des Entwurfs bestimmen:

1. **Ein Absturz im Plugin reißt TeamSpeak mit.** Jede Exception muss an der ABI-Grenze
   gefangen werden; keine Exception darf durch eine `ts3plugin_*`-Funktion nach außen laufen.
2. **TS3-Callbacks dürfen nicht blockieren.** Sie laufen auf einem Client-Thread, der auch
   Audio bedient. Ein HTTP-Request im Callback bedeutet im besten Fall Ruckeln, im
   schlechtesten einen eingefrorenen Client. Also: Callback schreibt nur State und weckt
   einen Worker.
3. **Wir sind Gast im Prozess.** Kein Qt (das gehört dem Client), keine globalen Locale-
   oder CRT-Änderungen, kein eigener Message-Loop im Client-Thread.

## Schichten

```
src/plugin/     ABI-Grenze   -- dünn, kennt TS3, fängt alle Exceptions
src/core/       Zustand      -- ClientState, StateStore, Worker
src/widgets/    Features     -- modulare Anzeige-Bausteine (hier wächst das Projekt)
src/render/     Darstellung  -- Composer, Frame, 1bpp-Bitmap
src/gamesense/  Transport    -- CoreProps, WinHTTP, Session-Lifecycle
src/config/     Einstellung  -- JSON-Datei + Win32-Dialog
src/util/       Basis        -- Logging, UTF-8/UTF-16
```

Die Abhängigkeiten zeigen strikt nach unten. Insbesondere: **`widgets/`, `render/`, `core/`
und `gamesense/` kennen die TS3-Header nicht.** Nur `src/plugin/` bindet `ts3_functions.h`
ein. Das hält die interessanten Teile in Unit-Tests testbar, ohne einen laufenden
TeamSpeak-Client.

## Threading-Modell

Zwei Threads, klar getrennte Zuständigkeiten:

```
TS3-Client-Thread                        Worker-Thread (uns gehörend)
-----------------                        ----------------------------
ts3plugin_onTalkStatusChangeEvent(...)
  |
  +- Namen/Channel SOFORT über
  |  ts3Functions.* auflösen
  |  (siehe Invariante unten)
  |
  +- StateStore::apply(mutation)
  |    +- lock, ClientState mutieren
  |    +- condition_variable.notify_one() --->  aufgeweckt (oder Timeout)
  |                                               |
  +- return (Mikrosekunden)                       +- snapshot = StateStore::snapshot()
                                                  |    (Kopie unter Lock, dann Lock weg)
                                                  |
                                                  +- frame = Composer::compose(snapshot, config)
                                                  |
                                                  +- frame != lastFrame?
                                                  |    +- GameSenseSession::sendFrame(frame)
                                                  |
                                                  +- sonst Heartbeat fällig?
                                                       +- GameSenseSession::heartbeat()
```

### Invariante: Der Worker-Thread ruft niemals `ts3Functions` auf

TS3-Callbacks liefern IDs, keine Namen. `onTalkStatusChangeEvent` gibt eine `clientID`, den
Nickname muss man per `getClientVariableAsString(..., CLIENT_NICKNAME, ...)` nachschlagen.
Diese Auflösung passiert **im Callback, auf dem TS3-Thread**, und in den `ClientState`
wandert nur noch ein fertiger `std::string`.

Das kostet ein paar Mikrosekunden im Callback und kauft dafür: keine Frage nach
Thread-Sicherheit der TS3-API, keine Lebensdauer-Probleme mit Client-IDs, die zwischen
Event und Rendering ungültig werden, und einen Worker, der rein auf eigenen Daten arbeitet.

### Invariante: Genau ein Schreiber auf das Display

[GameSense Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66) berichtet von
zerstörten OLED-Inhalten, wenn mehrere Quellen gleichzeitig an ein Arctis-Display schreiben
— bis hin zum nötigen Hard-Reboot der Basisstation. Der Fehler ist offen.

Was wir davon kontrollieren können, kontrollieren wir: **alle HTTP-Requests an GameSense
laufen ausschließlich auf dem Worker-Thread**, seriell, nie überlappend. Zusätzlich ein
Rate-Limit (`min_update_interval_ms`, Default 120 ms) mit Coalescing — zehn Talk-Events in
50 ms ergeben einen einzigen Frame, nicht zehn.

Gegen fremde Schreiber (Discord-Plugin, andere GameSense-Spiele) hilft das nicht. Das ist
ein dokumentiertes Restrisiko, kein auf unserer Seite lösbares Problem.

## Datenmodell

`ClientState` ist ein reiner Wert-Typ ohne TS3-Bezug — kopierbar, vergleichbar, testbar:

```cpp
struct TalkerInfo {
    std::string name;
    bool        whispering = false;
    std::chrono::steady_clock::time_point since;
};

struct ClientState {
    // Verbindung
    bool        connected = false;
    std::string serverName;

    // Eigener Channel
    std::string channelName;
    int         channelClientCount = 0;

    // Eigener Status
    std::string ownNickname;
    bool        inputMuted  = false;   // Mikrofon stumm
    bool        outputMuted = false;   // Kopfhörer stumm (Deaf)
    bool        away        = false;

    // Wer redet gerade im eigenen Channel
    std::vector<TalkerInfo> talkers;
};
```

### Mehrere Server-Tabs

TeamSpeak erlaubt mehrere gleichzeitige Verbindungen. Angezeigt wird immer der **aktuell im
Client ausgewählte Tab**; `ts3plugin_currentServerConnectionChanged` schaltet um. Events von
anderen `serverConnectionHandlerID`s werden verworfen, statt den State zu mischen.
Begründung in [ADR 0004](decisions/0004-single-active-server-tab.md).

## Widget-Modell

Die Anzeige besteht aus austauschbaren Bausteinen. Ein Widget kennt den `ClientState` und
liefert Zeilen; es weiß nichts über GameSense, HTTP oder Bitmaps.

```cpp
class IWidget {
public:
    virtual ~IWidget() = default;

    // Stabil und maschinenlesbar -- Schlüssel in der Config-Datei. Nie ändern.
    virtual std::string_view id() const = 0;

    // Für den Konfigurationsdialog.
    virtual std::string_view displayName() const = 0;

    // nullopt = "ich habe gerade nichts zu sagen", Zeile wird übersprungen.
    virtual std::optional<WidgetOutput> render(const ClientState&, const RenderContext&) const = 0;
};
```

Neues Feature = neue Datei in `src/widgets/` plus eine Registrierungszeile. Kein Anfassen
von Composer, Session oder Dialog. Das ist die Modularität, die im Auftrag gefordert war —
ausführlich in [widgets.md](widgets.md).

Der `Composer` nimmt die im Config aktivierten Widgets **in konfigurierter Reihenfolge**,
sammelt ihre Ausgaben und füllt damit die drei verfügbaren Zeilen. Widgets, die
`nullopt` liefern, verbrauchen keinen Platz — so kann der Nutzer mehr Widgets aktivieren als
gleichzeitig auf den Schirm passen, und es wird jeweils das Relevante gezeigt.

Zwei Eigenheiten des Geräts prägen den Composer:

- **Ein Icon pro Frame, nicht pro Zeile.** GameSense setzt `icon-id` auf den Frame und
  reserviert dafür die 32 linkesten Pixel. Der Composer übernimmt daher das Icon des
  obersten beitragenden Widgets, das eines wünscht — nach `priority`-Sortierung also
  automatisch das Dringlichste. Die Icons 19 (Muted), 20 (Talking), 21 (Connect) und
  22 (Disconnect) passen exakt auf diesen Anwendungsfall.
- **Nicht kürzen.** GG lässt zu lange Zeilen von selbst durchlaufen (an der
  NowPlaying-App beobachtet). Ein abgeschnittener Channelname wäre schlechter als ein
  scrollender.

## GameSense-Anbindung

Ablauf über die Lebensdauer des Plugins:

| Zeitpunkt | Aktion |
|---|---|
| `ts3plugin_init` | `coreProps.json` lesen -> Adresse; Worker starten |
| Worker-Start | `POST /game_metadata`, dann `POST /bind_game_event` (Screen-Handler) |
| Zustandsänderung | `POST /game_event` mit `data.frame` |
| Leerlauf | `POST /game_heartbeat` alle ~8 s |
| `ts3plugin_shutdown` | `POST /remove_game`, Worker sauber joinen |

Die HTTP-Details und die genaue JSON-Form stehen in [gamesense-notes.md](gamesense-notes.md).

### Wer den Schirm besitzt

Das Display wird **nicht dauerhaft gehalten**. Phase 0 hat gemessen, dass GG bei
konkurrierenden Apps zwischen den Anzeigen wechselt und dabei sichtbar flackert — eine
Dauerbelegung würde also die Musikanzeige des Nutzers zerhacken. Begründung in
[ADR 0006](decisions/0006-event-driven-screen-ownership.md).

Der Worker führt daher eine kleine Zustandsmaschine:

```
        Frame nicht leer                Haltefrist abgelaufen
RELEASED ───────────────► OWNED ──────────────────────────────► RELEASED
   │                        │                                       │
   │ game_metadata          │ game_event je Änderung                │ remove_game
   │ bind_game_event        │ game_heartbeat alle 8 s               │ (sofortige Freigabe)
```

„Frame leer" heißt: **alle** aktivierten Widgets haben `nullopt` geliefert. Es braucht
also keinen zusätzlichen Mechanismus — das Widget-Modell trägt die Entscheidung bereits.

### Nur Textzeilen, kein Bitmap

GameSense kann grundsätzlich beides: von der Engine gerenderte Textzeilen (`has-text` +
`context-frame-key`) oder ein geliefertes 1-Bit-Bitmap (`image-data`). Auf diesem Gerät
funktioniert **nur der Textweg** — beide Bitmap-Varianten wurden in Phase 0 geprüft und
zeigten nichts (siehe [gamesense-notes.md](gamesense-notes.md)).

Das ist verkraftbar: Gemessene drei Zeilen ohne horizontales Abschneiden reichen für
Mute-Status, Sprecher und Channel. `Frame` bleibt trotzdem eine Variante aus `TextLines`
und `Bitmap` — die Trennung kostet nichts und hält die Tür offen, falls sich der
Bitmap-Pfad doch noch klären lässt.

## Fehlerverhalten

Das Plugin ist Zubehör. Es darf TeamSpeak unter keinen Umständen beeinträchtigen:

- **SteelSeries GG läuft nicht** -> `coreProps.json` fehlt. Kein Fehler-Popup; geloggt,
  Worker geht in einen Retry mit Backoff (10 s -> 60 s) und findet GG, sobald es startet.
- **HTTP-Fehler** -> geloggt, nächster Tick versucht es erneut. Nach wiederholtem Scheitern
  wird die Session neu aufgebaut (`game_metadata` + `bind_game_event`), denn GG vergisst
  Spiele nach dem Deinitialize-Timeout.
- **Exception irgendwo** -> am ABI-Rand gefangen, geloggt, Plugin schaltet sich still ab,
  TeamSpeak läuft weiter.
