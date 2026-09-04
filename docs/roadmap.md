# Roadmap

Jede Phase endet mit etwas, das man anschauen kann. Kein "erst alles bauen, dann testen".

---

## Phase 0 — Risiko messen, bevor irgendwas gebaut wird

> **Stand 2026-09-05: abgeschlossen.** Ergebnisse in
> [gamesense-notes.md](gamesense-notes.md).
>
> | | Ergebnis | Folge |
> |---|---|---|
> | device-type | `screened` trifft das Gerät | [ADR 0002](decisions/0002-gamesense-over-usb-hid.md) bestätigt |
> | Textzeilen | 3, kein Abschneiden | `display.max_lines` = 3 |
> | Bitmap | funktioniert nicht | Phase 5 gestrichen |
> | Freigabe | Timeout **und** `remove_game` geben frei | ermöglicht ADR 0006 |
> | App-Konkurrenz | Wechsel mit Flackern | [ADR 0006](decisions/0006-event-driven-screen-ownership.md): ereignisgesteuerte Belegung |
>
> Phase 1 ist freigegeben.

**Das größte Risiko ist nicht der Code, sondern die Frage, ob die Hardware überhaupt
mitspielt.** Die Arctis Nova Pro Wireless *Basisstation* ist nicht dasselbe Gerät wie ein
Rival-Maus-Display, und die GameSense-Doku sagt nirgends explizit zu, dass ihr OLED per SDK
beschreibbar ist. [Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66) legt
zusätzlich nahe, dass die Arctis-Displays am SDK zickig sind.

Deshalb zuerst ein Wegwerf-Skript, ganz ohne TeamSpeak: `tools/gamesense-probe.ps1`.

Zu klärende Fragen, in dieser Reihenfolge:

1. Existiert `coreProps.json` und antwortet die Adresse?
2. Welcher `device-type` erreicht das Gerät? Erwartet wird das generische `"screened"`;
   `"screened-128x64"` existiert nicht und schlägt lautlos fehl.
3. Erscheint ein `game_event` **sichtbar** auf dem OLED der Basisstation?
4. Wird die Anzeige nach Heartbeat-Stopp korrekt an die GG-Oberfläche zurückgegeben?
5. Funktioniert `image-data` (1-Bit-Bitmap) zusätzlich zu reinen Textzeilen?

**Gate:** Scheitert Frage 3, ist ADR 0002 (GameSense statt USB-HID) hinfällig und muss vor
Phase 1 neu entschieden werden. Der Aufwand bis hierher: ein Skript, kein Code.

**Ergebnis wird festgehalten in** `docs/decisions/0002-gamesense-over-usb-hid.md`.

---

## Phase 1 — Die DLL lädt

Ziel: TeamSpeak listet das Plugin, aktiviert es, und auf dem OLED steht ein fester Text.

- `src/plugin/plugin.cpp` mit allen Pflicht-Exports, `apiVersion` = 26
- Exception-Guard an jeder ABI-Funktion
- `src/util/log.cpp`: Datei-Log plus `ts3Functions.logMessage`
- `src/gamesense/core_props.cpp` + `http.cpp` (WinHTTP, synchron, ein Thread)
- `src/gamesense/session.cpp`: metadata -> bind -> event -> heartbeat -> remove
- Worker-Thread, der schlicht `"TeamSpeak"` anzeigt und am Leben hält
- `tools/install-dev.ps1` für die Schleife Bauen -> Kopieren -> TS3 neu starten

**Fertig wenn:** DLL laden, Text erscheint, TeamSpeak beenden, Display fällt sauber an GG
zurück — und das dreimal hintereinander ohne Neustart der Basisstation.

---

## Phase 2 — Echter Zustand

Ziel: Der `ClientState` bildet ab, was in TeamSpeak passiert. Noch keine hübsche Anzeige.

- `src/core/client_state.h`, `state_store.cpp` (Mutex + Snapshot + Versionszähler)
- `src/core/worker.cpp`: Coalescing, Rate-Limit, Heartbeat-Timer, Reconnect-Backoff
- **Besitz-Zustandsmaschine** `RELEASED -> OWNED -> RELEASED` nach
  [ADR 0006](decisions/0006-event-driven-screen-ownership.md): belegen, wenn der Frame
  nicht leer ist; nach `display.hold_ms` ohne Inhalt per `remove_game` freigeben. Beim
  Wiederbelegen müssen `game_metadata` und `bind_game_event` erneut gesendet werden.
- Callbacks in `src/plugin/`:
  - `onConnectStatusChangeEvent` -> connected, serverName
  - `onClientSelfVariableUpdateEvent` -> inputMuted, outputMuted, away
  - `onTalkStatusChangeEvent` -> talkers (nur eigener Channel)
  - `onClientMoveEvent` / `onClientMoveMovedEvent` -> Channelwechsel, Nutzerzahl
  - `onUpdateChannelEditedEvent` -> Channel-Umbenennung
  - `currentServerConnectionChanged` -> aktiver Tab
- `src/plugin/ts3_context.cpp`: alle ID-nach-Name-Auflösungen an einer Stelle

**Fertig wenn:** Das Log zeigt bei jeder Aktion in TS3 den korrekten neuen State. Reden,
Muten, Channel wechseln, Server trennen — jeweils genau ein State-Update.

---

## Phase 3 — Widgets

Ziel: Die vier gewünschten Anzeigen, über die Registry entkoppelt.

- `src/widgets/widget.h` — `IWidget`, `WidgetOutput`, `RenderContext`, `enum class Icon`
- `src/widgets/registry.cpp` — Selbstregistrierung
- `src/render/composer.cpp` — Reihenfolge, Zeilenbudget (3), Icon-Auswahl
  (oberstes beitragendes Widget mit Icon-Wunsch); **kein** hartes Kürzen, GG scrollt
- Die Widgets:
  - `connection` — Verbindungsstatus / Servername
  - `channel_info` — Channelname + Nutzerzahl
  - `talkers` — wer redet gerade
  - `mute_status` — Mute / Deaf / Away
- Unit-Tests für Composer und Widgets gegen synthetische `ClientState`-Werte

**Fertig wenn:** Alle vier Widgets zeigen zur Laufzeit korrekt an, und die Tests laufen
grün ohne TeamSpeak.

---

## Phase 4 — Konfiguration durch den Nutzer

Ziel: Der Nutzer wählt aus, was angezeigt wird — die zweite ausdrückliche Anforderung.

- `src/config/config.cpp` — JSON in `%APPDATA%\TS3Client\plugins\ts3_steelseries\config.json`,
  Schema und Migration siehe [configuration.md](configuration.md)
- `ts3plugin_offersConfigure` -> `PLUGIN_OFFERS_CONFIGURE_NEW_THREAD`
- `src/config/config_dialog.cpp` — Win32-Dialog aus `resources/plugin.rc`:
  Liste aller registrierten Widgets mit Checkbox, Hoch/Runter für die Reihenfolge
- Hot-Reload: Speichern weckt den Worker, kein TS3-Neustart nötig
- Unbekannte Widget-IDs in der Config werden ignoriert statt zu crashen

**Fertig wenn:** Widgets lassen sich im Dialog an- und abschalten und umsortieren, und das
Display folgt sofort.

---

## Phase 5 — Eigenes Bitmap-Rendering *(gestrichen)*

**Blockiert durch Phase 0.** Beide dokumentierten Bitmap-Wege wurden geprüft und zeigten
nichts: statisches `image-data` ließ die GG-Oberfläche stehen, dynamisches über
`image-data-128x64` übernahm zwar den Schirm, zeichnete aber nur Schwarz. Einzelheiten
in [gamesense-notes.md](gamesense-notes.md#bitmap-funktioniert-auf-diesem-gerät-nicht).

Kein Verlust: Drei Textzeilen ohne Abschneiden reichen für den geplanten Inhalt, und GG
rendert die Schrift.

Falls es je wieder aufgegriffen wird, ist der dynamische Weg der Ansatzpunkt — er war
formal gültig, nur die Bilddaten kamen nicht an.

---

## Phase 6 — Ausliefern

- `tools/package.ps1` baut das `.ts3_plugin`-Archiv aus `resources/package.ini` + DLL
- README mit Installationsanleitung und der Voraussetzung "SteelSeries GG läuft"
- GitHub-Release mit der gebauten DLL

---

## Bewusst nicht im Scope

| Punkt | Warum nicht |
|---|---|
| Linux / macOS | WinHTTP und Win32-Dialog sind Windows-gebunden; TS3+GG-Kombination dort ohnehin selten |
| Andere SteelSeries-Displays | Layout ist auf 128x64 zugeschnitten. Der Composer bekommt die Maße über `RenderContext`, eine spätere Erweiterung ist also nicht verbaut |
| Direkter USB-HID-Zugriff | Bewusst verworfen, siehe [ADR 0002](decisions/0002-gamesense-over-usb-hid.md) |
| ClientQuery als Event-Quelle | Bewusst verworfen, siehe [ADR 0003](decisions/0003-native-plugin-as-event-source.md) |
