# ADR 0003 — Natives Plugin als Event-Quelle, nicht ClientQuery

**Status:** akzeptiert (2026-09-05)

## Kontext

TeamSpeak 3 bietet zwei Wege, an Ereignisse wie "jemand spricht" oder "Mikro stumm"
heranzukommen.

### Weg A — natives Plugin

C-Callbacks des Plugin-SDK im Client-Prozess: `onTalkStatusChangeEvent`,
`onClientSelfVariableUpdateEvent`, `onClientMoveEvent`, `onConnectStatusChangeEvent`.

### Weg B — ClientQuery

Das mitgelieferte ClientQuery-Plugin öffnet eine Telnet-Schnittstelle auf
`127.0.0.1:25639`. Über `clientnotifyregister` liefert es unter anderem
`notifytalkstatuschange`, `notifyclientupdated` und `notifyclientmoved` — dieselben
Ereignisse, als Textprotokoll. Seit TS3 3.1 ist ein API-Key nötig (*Extras -> Optionen
-> Addons -> ClientQuery -> Einstellungen*).

## Entscheidung

**Weg A, natives Plugin.** Ausdrückliche Wahl des Nutzers.

## Begründung

- **Konsequenz aus [ADR 0001](0001-single-process-native-plugin.md).** Wir bauen ohnehin
  eine DLL im TS3-Prozess. Von dort aus über eine Telnet-Verbindung zurück in denselben
  Client zu sprechen, wäre absurd — die Callbacks liegen bereits an.
- **Kein Setup.** Keine ClientQuery-Aktivierung, kein API-Key zum Kopieren. Plugin
  installieren, aktivieren, fertig. Bei ClientQuery wäre der Key ein dauerhafter
  Supportfall.
- **Strukturierte Daten.** Typisierte Callback-Parameter statt Textprotokoll mit
  eigenem Escaping (ClientQuery kodiert Leerzeichen als `\s`, Schrägstriche als `\/`
  usw. — genau die Sorte Parser, die bei ungewöhnlichen Nicknames bricht).
- **Vollständigere Ereignisse.** `STATUS_TALKING_WHILE_DISABLED` etwa unterscheidet
  "spricht" von "spricht ins stumme Mikro" — die nützlichste Einzelinformation für dieses
  Display.
- **Kein Verbindungszustand zu pflegen.** Weg B bräuchte Reconnect-Logik, wenn
  ClientQuery neu startet, plus Behandlung eines belegten Ports.

## Konsequenzen

- **C und die Plugin-ABI.** Kein Ausweichen auf eine bequemere Sprache. Siehe ADR 0001.
- **Bindung an `PLUGIN_API_VERSION`** (aktuell 26). Erhöht TeamSpeak die Version, lädt
  das Plugin **wortlos nicht mehr** und muss neu gebaut werden. Gehört in die README als
  erste Diagnosefrage bei "geht nicht mehr".
- **Fehler treffen den Client.** Ein ClientQuery-Client hätte im schlimmsten Fall sich
  selbst abgeschossen; wir schießen TeamSpeak ab. Daher der Exception-Guard an jeder
  ABI-Funktion.
- **Entwicklungsschleife ist zäher.** Jede Änderung heißt: bauen, DLL kopieren, TS3 neu
  starten. Gegenmaßnahme: `tools/install-dev.ps1`, und die gesamte interessante Logik
  (State, Widgets, Composer) hängt bewusst *nicht* an TS3-Headern, ist also in `tests/`
  ohne Client prüfbar.

## Verworfene Alternative

**ClientQuery zuerst, natives Plugin später** — hinter einer gemeinsamen
`IEventSource`-Schnittstelle. Hätte deutlich schneller zu einem laufenden Gesamtsystem
geführt, weil man den Renderer ohne C-ABI und ohne TS3-Neustart entwickeln kann.

Verworfen, weil die Abstraktion dauerhaft Kosten verursacht, während der Nutzen einmalig
ist: Sobald das native Plugin existiert, ist der ClientQuery-Pfad toter Code, der
mitgepflegt werden will. Und da ADR 0001 die DLL ohnehin erzwingt, wäre ClientQuery ein
Umweg zum selben Ziel.
