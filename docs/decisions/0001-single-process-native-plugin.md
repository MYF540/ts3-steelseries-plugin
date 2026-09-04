# ADR 0001 — Alles in einem nativen Plugin, kein zweiter Prozess

**Status:** akzeptiert (2026-09-05)

## Kontext

Zwei Systeme müssen verbunden werden: TeamSpeak 3 (liefert Events) und SteelSeries
GameSense (nimmt Display-Inhalte entgegen). Die naheliegende Aufteilung wäre ein
schlankes TS3-Plugin plus ein Hintergrunddienst, der das Rendering und die
GameSense-Kommunikation übernimmt.

## Entscheidung

Alles läuft in **einer** DLL im TeamSpeak-Prozess. Kein Dienst, kein Tray-Programm,
kein Autostart-Eintrag.

Damit ist **C++** gesetzt: Die TS3-Plugin-Schnittstelle ist eine C-ABI, die im
Client-Prozess bedient werden muss. Für die verbleibenden Aufgaben genügen
Windows-Bordmittel — WinHTTP für die HTTP-Requests an `127.0.0.1`, die Win32-Dialog-API
für die Konfiguration. Einzige externe Abhängigkeit ist
[nlohmann/json](https://github.com/nlohmann/json), header-only, über CMake FetchContent.

## Begründung

Ausdrückliche Vorgabe des Nutzers: *"es soll kein zusätzliches programm bzw dienst
werden"*. Die technischen Argumente stützen das:

- **Ein Lebenszyklus.** TeamSpeak startet, das Plugin startet. TeamSpeak endet, das
  Display fällt an GG zurück. Ein separater Dienst müsste erkennen, ob TS3 überhaupt
  läuft, und bräuchte eigene Start-/Stopp-/Update-Behandlung.
- **Keine IPC.** Named Pipe oder localhost-Socket wären zusätzlicher Code mit eigenen
  Fehlerfällen (Pipe kaputt, Port belegt, Reconnect-Logik) — für null Mehrwert, weil
  beide Seiten ohnehin auf demselben Rechner desselben Nutzers laufen.
- **Eine Installation.** `.ts3_plugin` doppelklicken, fertig. Kein Installer, keine
  Firewall-Freigabe, kein "warum hat sich da was in den Autostart eingetragen".

## Konsequenzen

Der Preis ist real und wird in [architecture.md](../architecture.md) über konkrete
Invarianten bezahlt:

- **Wir teilen den Adressraum mit TeamSpeak.** Jeder Absturz reißt den Client mit.
  Gegenmaßnahme: Exception-Guard an jeder ABI-Funktion, keine Ausnahme.
- **Wir teilen die Threads.** Ein blockierender Callback stört den Audiopfad.
  Gegenmaßnahme: eigener Worker-Thread, Callbacks schreiben nur State.
- **Anzeige nur bei laufendem TeamSpeak.** Ein Dienst könnte auch sonst etwas anzeigen
  — das ist aber nicht das Ziel dieses Projekts.
- **C++ statt einer bequemeren Sprache.** Rust könnte eine `cdylib` mit C-ABI liefern,
  aber der Gewinn wiegt die Reibung an einer C-Schnittstelle mit rund 100 Callbacks
  nicht auf, für die es fertige C-Header und Beispielcode gibt.

## Verworfene Alternative

**TS3-Plugin plus Rust-/C#-Daemon über Named Pipe.** Wäre angenehmer zu entwickeln und
zu debuggen (Renderer ohne TeamSpeak startbar, Absturz reißt den Client nicht mit).
Scheitert an der Vorgabe "kein zusätzliches Programm" — und diese Vorgabe ist
nachvollziehbar: Ein Headset-Display rechtfertigt keinen dauerhaft laufenden Dienst.
