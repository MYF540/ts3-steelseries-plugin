# TeamSpeak-3-Plugin-SDK — recherchierte Fakten

Quelle: `third_party/ts3-pluginsdk` (Submodul,
[TeamSpeak-Systems/ts3client-pluginsdk](https://github.com/TeamSpeak-Systems/ts3client-pluginsdk)).
Zeilenangaben beziehen sich auf den eingecheckten Stand des Submoduls.

## Harte Randbedingungen

| Punkt | Wert |
|---|---|
| `PLUGIN_API_VERSION` | **26** (`src/plugin.c:38`) |
| Architektur | muss exakt zum Client passen — für TS3 3.6.x **x64** |
| Export-Mechanik | `__declspec(dllexport)` plus `extern "C"` — bereits in `src/plugin.h`, keine `.def`-Datei nötig |
| Speicherfreigabe | Das Plugin gibt eigenen Speicher über `ts3plugin_freeMemory` selbst frei. **Kein CRT-Mismatch**, das erlaubt eine normale MSVC-Runtime. |
| Vom Client allozierte Strings | Alles aus `get*VariableAsString` muss mit `ts3Functions.freeMemory` freigegeben werden — RAII-Wrapper in `ts3_context` |

Passt die API-Version nicht zum installierten Client, lädt TeamSpeak das Plugin
**wortlos nicht**. Beim Upgrade des Clients ist das die erste Verdächtige.

## Pflicht-Exports

Fehlt einer davon, wird das Plugin abgelehnt:

`ts3plugin_name`, `ts3plugin_version`, `ts3plugin_apiVersion`, `ts3plugin_author`,
`ts3plugin_description`, `ts3plugin_setFunctionPointers`, `ts3plugin_init`,
`ts3plugin_shutdown`

`ts3plugin_init` gibt **0 bei Erfolg** zurück.

## Genutzte Callbacks

Signaturen wörtlich aus `src/plugin.c`:

```c
void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int errorNumber);
void ts3plugin_onTalkStatusChangeEvent(uint64 schid, int status, int isReceivedWhisper, anyID clientID);
void ts3plugin_onClientSelfVariableUpdateEvent(uint64 schid, int flag, const char* oldValue, const char* newValue);
void ts3plugin_onClientMoveEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
                                 int visibility, const char* moveMessage);
void ts3plugin_onClientMoveMovedEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
                                      int visibility, anyID moverID, const char* moverName,
                                      const char* moverUniqueIdentifier, const char* moveMessage);
void ts3plugin_onUpdateChannelEditedEvent(uint64 schid, uint64 channelID, anyID invokerID,
                                          const char* invokerName, const char* invokerUniqueIdentifier);
void ts3plugin_currentServerConnectionChanged(uint64 schid);
```

Wichtig: Ein Nutzer kann den Channel auf **zwei** Wegen wechseln — selbst wechseln
(`onClientMoveEvent`) oder verschoben werden (`onClientMoveMovedEvent`). Wer nur den
ersten behandelt, verliert die Channel-Anzeige, sobald jemand geschoben wird. Ebenso
gibt es `onClientMoveTimeoutEvent` für Verbindungsabbrüche anderer Clients.

## Relevante Enums

```c
enum TalkStatus {                        // public_definitions.h
    STATUS_NOT_TALKING            = 0,
    STATUS_TALKING                = 1,
    STATUS_TALKING_WHILE_DISABLED = 2,   // nur für den eigenen Client
};
```

`STATUS_TALKING_WHILE_DISABLED` ist ein Geschenk für dieses Projekt: Es ist exakt der
Fall "du redest, aber dein Mikro ist stumm" — die nützlichste einzelne Information, die
so ein Display anzeigen kann.

```c
enum ConnectStatus {                     // public_definitions.h
    STATUS_DISCONNECTED = 0,
    STATUS_CONNECTING,
    STATUS_CONNECTED,
    STATUS_CONNECTION_ESTABLISHING,
    STATUS_CONNECTION_ESTABLISHED,       // erst hier sind Channels und Clients abfragbar
};
```

Servername und Channelliste **erst ab `STATUS_CONNECTION_ESTABLISHED`** abfragen —
vorher sind sie schlicht noch nicht da.

### Client-Eigenschaften

Aus `enum ClientProperties` (`public_definitions.h`):

| Konstante | Bedeutung |
|---|---|
| `CLIENT_NICKNAME` | Anzeigename |
| `CLIENT_INPUT_MUTED` | Mikrofon stumm |
| `CLIENT_OUTPUT_MUTED` | Lautsprecher stumm (impliziert Mikro-Mute) |
| `CLIENT_INPUT_HARDWARE` | Aufnahmegerät geöffnet |
| `CLIENT_OUTPUT_HARDWARE` | Wiedergabegerät geöffnet |
| `CLIENT_FLAG_TALKING` | spricht gerade |

`CLIENT_AWAY` liegt **nicht** hier, sondern in `enum ClientPropertiesRare`
(`public_rare_definitions.h:286`). Das ist eine leichte Stolperfalle: Beide Enums haben
eigene Wertebereiche, und der `flag`-Parameter von `onClientSelfVariableUpdateEvent`
bezieht sich auf `ClientProperties`. Für Away also zusätzlich über die Rare-Variante
abfragen.

Hinweis zur Semantik: `CLIENT_OUTPUT_MUTED` (Deaf) impliziert laut Header-Kommentar
bereits Mikrofon-Mute. Das Mute-Widget sollte deshalb "Deaf" anzeigen und nicht
redundant "Mic + Speaker stumm".

## Genutzte `ts3Functions`

```c
unsigned int (*getClientID)(uint64 schid, anyID* result);
unsigned int (*getChannelOfClient)(uint64 schid, anyID clientID, uint64* result);
unsigned int (*getClientVariableAsString)(uint64 schid, anyID clientID, size_t flag, char** result);
unsigned int (*getChannelVariableAsString)(uint64 schid, uint64 channelID, size_t flag, char** result);
unsigned int (*getServerVariableAsString)(uint64 schid, size_t flag, char** result);
unsigned int (*getChannelClientList)(uint64 schid, uint64 channelID, anyID** result);
uint64       (*getCurrentServerConnectionHandlerID)(void);
unsigned int (*logMessage)(const char* msg, enum LogLevel severity, const char* channel, uint64 logID);
unsigned int (*freeMemory)(void* pointer);
```

Alle geben `ERROR_ok` (0) bei Erfolg zurück; Rückgabewerte **immer** prüfen — beim
Verbindungsabbau schlagen sie fehl, statt gültige Daten zu liefern.

Wichtige Eigenschaften:

- `CHANNEL_NAME` ist `enum ChannelProperties`-Wert `0`.
- `VIRTUALSERVER_NAME` ist ein `enum VirtualServerProperties`-Wert.
- Längenlimits: Channelname 40 Zeichen, Servername 64 Zeichen. Auf 128 px passt
  ohnehin weniger — der Composer kürzt.

## Strings sind UTF-8

Die TS3-API liefert und erwartet UTF-8. GameSense-JSON ist ebenfalls UTF-8. Dazwischen
ist also **keine** Konvertierung nötig — das ist der bequeme Fall.

Konvertiert werden muss nur an zwei Stellen: Win32-Dialog (UTF-16, `src/util/strings`)
und Dateipfade. Deshalb wird auch die JSON-Serialisierung von nlohmann übernommen, statt
Strings selbst zusammenzukleben: Nicknames enthalten regelmäßig Zeichen, die escaped
werden müssen.

## Konfigurationsdialog

```c
PLUGINS_EXPORTDLL int  ts3plugin_offersConfigure();
PLUGINS_EXPORTDLL void ts3plugin_configure(void* handle, void* qParentWidget);
```

`qParentWidget` ist ein **Qt**-Widget-Zeiger. Wir nutzen Qt nicht und ignorieren ihn;
stattdessen liefert `ts3plugin_offersConfigure` den Wert
`PLUGIN_OFFERS_CONFIGURE_NEW_THREAD` (`plugin_definitions.h`), damit unser Win32-Dialog
auf einem eigenen Thread laufen darf und den Qt-Message-Loop des Clients nicht stört.

## Installation und Paketformat

Entwicklung: DLL nach `%APPDATA%\TS3Client\plugins\` kopieren, Client neu starten
(`tools/install-dev.ps1`).

Auslieferung: `.ts3_plugin` ist ein ZIP-Archiv mit `package.ini` in der Wurzel und der
DLL unter `plugins/` (`tools/package.ps1`, Vorlage in `resources/package.ini`).

## Debugging

TeamSpeak lädt das Plugin in den eigenen Prozess — also Visual Studio an `ts3client_win64.exe`
attachen. Weil ein Absturz den Client mitreißt, ist während der Entwicklung ein zweites
TS3-Profil ratsam.

Das eigene Datei-Log (`src/util/log`) ist meist schneller als der Debugger, weil viele
Fehler nur zur Laufzeit unter echten Netzwerk-Events auftreten.
