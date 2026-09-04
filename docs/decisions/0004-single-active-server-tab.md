# ADR 0004 — Nur der aktive Server-Tab wird angezeigt

**Status:** akzeptiert (2026-09-05)

## Kontext

TeamSpeak 3 erlaubt mehrere gleichzeitige Serververbindungen, jede in einem eigenen Tab
mit eigener `serverConnectionHandlerID` (kurz `schid`). Alle Plugin-Callbacks bekommen
diese ID als ersten Parameter.

Es gibt also mehr Zustand, als auf 128x64 Pixel passt. Irgendetwas muss ausgewählt
werden.

## Entscheidung

Der `ClientState` bildet **genau eine** Verbindung ab: die, deren Tab im Client gerade
im Vordergrund ist. `ts3plugin_currentServerConnectionChanged(schid)` schaltet um;
Events mit einer anderen `schid` werden verworfen.

Beim Umschalten wird der State **vollständig neu aufgebaut**, statt inkrementell
weitergeschrieben: eigener Client, Channel, Channelmitglieder, Sprecherliste. Sonst
blieben Sprecher aus dem alten Tab stehen.

## Begründung

- **Es entspricht der Erwartung.** Wer den Tab wechselt, schaut auf diesen Server. Dass
  das Display mitgeht, ist die naheliegende Lesart.
- **"Wer redet gerade" ist sonst nicht beantwortbar.** Reden Personen in zwei Servern
  gleichzeitig, ergäbe eine gemischte Liste ohne Serverzuordnung Unsinn — und für die
  Zuordnung fehlt der Platz.
- **Es hält den State klein.** Eine `ClientState`-Instanz statt einer Map über `schid`.
  Der Composer bekommt einen Wert, keine Auswahllogik.
- **Ein einzelner Sonderfall bleibt handhabbar.** Die Alternative — alle Verbindungen
  verfolgen — bringt sofort Folgefragen: Welcher Server gewinnt bei Konflikt? Was zeigt
  ein stiller Tab? Wie zeigt man Mute, wenn es pro Verbindung verschieden ist?

## Konsequenzen

- **Der zweite Server ist unsichtbar**, auch wenn dort jemand spricht. Bewusste
  Einschränkung, gehört in die README.
- **Beim Tabwechsel ein vollständiger Rebuild.** Kostet einige TS3-Abfragen
  (`getChannelClientList` und je Client den Nickname) auf dem Client-Thread. Passiert
  nur bei einer Nutzeraktion, ist also unkritisch — aber der Grund, warum
  `ts3_context` diese Abfragen gebündelt an einer Stelle hält.
- **`schid`-Prüfung gehört in jeden Callback.** Wird sie in einem vergessen, verunreinigt
  ein Hintergrund-Tab die Anzeige. Das ist die wahrscheinlichste Fehlerquelle dieses
  Entwurfs, deshalb: die Prüfung passiert **einmal zentral** beim Eintritt in die
  gemeinsame Dispatch-Funktion, nicht per Copy-Paste in jedem Handler.
- **Umkehrbar.** Sollte sich Mehrserverbetrieb doch als nötig erweisen, wird aus dem
  einen `ClientState` eine Map und der Composer bekommt eine Auswahlregel. Widgets
  müssten nicht angefasst werden, da sie ohnehin einen einzelnen `ClientState` bekommen.
