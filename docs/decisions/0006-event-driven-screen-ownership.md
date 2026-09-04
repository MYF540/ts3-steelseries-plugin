# ADR 0006 — Der Schirm wird ereignisgesteuert belegt, nicht dauerhaft gehalten

**Status:** akzeptiert (2026-09-05)

## Kontext

Der naheliegende Entwurf für eine Statusanzeige ist, den Schirm dauerhaft zu halten:
Heartbeat alle 8 s, immer ein aktueller Frame, das Display gehört uns, solange TeamSpeak
läuft. Genau so war [architecture.md](../architecture.md) ursprünglich angelegt.

Phase 0 hat zwei Messungen ergeben, die diesen Entwurf unhaltbar machen — und eine
dritte, die die Alternative erst ermöglicht.

### Messung 7 — bei Konkurrenz wechselt GG, und es flackert

Läuft parallel eine andere GameSense-App mit Screen-Handler (NowPlaying bei laufender
Musik, die Counter-Strike-2-App im Match), dann **wechselt** die Anzeige zwischen den
Apps, mit **sichtbarem Flackern**.

GG arbitriert also nicht nach Priorität mit einem klaren Gewinner, sondern schaltet um.
Ein dauerhaft gehaltener TeamSpeak-Status würde damit die Musikanzeige des Nutzers
dauerhaft zerhacken — und die eigene gleich mit. Für ein Zubehörprojekt ist das ein
Ausschlusskriterium: Es macht ein funktionierendes Feature des Nutzers kaputt.

### Messung 4 — die Freigabe funktioniert, und zwar sofort

Beide Wege geben den Schirm zuverlässig an GG zurück:

- Heartbeat einstellen → nach dem Deinitialize-Timer (15 s) fällt die Anzeige zurück.
- `POST /remove_game` → **sofortige** Rückgabe.

Das ist die Voraussetzung für den Gegenentwurf. Ohne verlässliche Freigabe bliebe nur
"halten oder gar nichts".

### Messung 6 — drei Zeilen, nichts wird abgeschnitten

Reicht für Mute-Status, Sprecher und Channel. Kein Grund, den Schirm für mehr Inhalt
länger zu belegen.

## Entscheidung

Das Plugin belegt den Schirm **nur, solange es etwas zu sagen hat**, und gibt ihn
danach aktiv per `remove_game` frei.

„Etwas zu sagen" fällt direkt aus dem bestehenden Widget-Modell heraus und braucht
keinen neuen Mechanismus: Liefern **alle** aktivierten Widgets `nullopt`, ist der
komponierte Frame leer — und ein leerer Frame bedeutet Freigabe.

```
Zustandsänderung
  └─ Composer::compose(...)
       ├─ Frame nicht leer
       │    ├─ Session noch nicht registriert? -> game_metadata + bind_game_event
       │    ├─ game_event senden
       │    └─ Haltefrist neu starten (display.hold_ms)
       │
       └─ Frame leer und Haltefrist abgelaufen
            └─ remove_game  -> Schirm gehört wieder GG
```

### Folge für das `connection`-Widget

In [widgets.md](../widgets.md) stand für `connection` bisher „`nullopt` wenn: nie". Das
ist damit hinfällig — ein Widget, das immer etwas liefert, hielte den Schirm für immer
und höbe diese ADR auf.

Neue Regel: `connection` meldet sich nur bei **Übergängen und Problemen** — verbinde
gerade, getrennt, Verbindung verloren. Der Dauerzustand „verbunden, alles ruhig" ist
`nullopt`. Das ist ohnehin die bessere Anzeige: Dass die Verbindung steht, sieht man
daran, dass niemand sich beschwert.

Dieselbe Prüfung gilt für jedes künftige Widget. In [widgets.md](../widgets.md) ist
`nullopt` deshalb ab sofort nicht nur „spare eine Zeile", sondern „gib den Schirm frei".

## Konsequenzen

- **Kein Flackerkrieg.** Wer Musik hört, behält NowPlaying, solange in TeamSpeak nichts
  passiert. Fängt jemand an zu reden, gewinnt für ein paar Sekunden TeamSpeak.
- **Weniger Requests.** Der Heartbeat läuft nur während der Haltefrist statt rund um die
  Uhr. Nebenbei sinkt die Wahrscheinlichkeit, in
  [Issue #66](https://github.com/SteelSeries/gamesense-sdk/issues/66) zu laufen.
- **Session-Zustand kommt hinzu.** Der Worker muss registriert/nicht-registriert
  unterscheiden und beim Wiederbelegen `game_metadata` + `bind_game_event` erneut
  senden. Das ist echte zusätzliche Komplexität in `gamesense/session.cpp`, aber eine
  überschaubare Zustandsmaschine mit drei Zuständen.
- **Das Flackern verschwindet nicht ganz.** Während unserer Haltefrist wechselt GG
  weiterhin mit einer anderen aktiven App. Kürzer statt dauerhaft ist eine Milderung,
  keine Lösung. Gehört in die README.
- **Umschaltbar.** Wer die Dauerbelegung will, bekommt sie über `display.mode`; siehe
  [configuration.md](../configuration.md). Der Default ist ereignisgesteuert.

## Verworfene Alternativen

**Dauerbelegung mit langsamerer Bildrate.** Reduziert das Flackern nicht — das entsteht
durch GGs Umschalten zwischen Apps, nicht durch unsere Frequenz.

**Nur bei Änderung senden, aber Session dauerhaft halten.** Der Schirm gehört uns dann
weiterhin, GG wechselt weiterhin. Der Heartbeat ist das Problem, nicht der Frame.

**Auf eine Prioritätseinstellung in GG verlassen.** Nicht in unserer Hand, nicht
dokumentiert und je nach GG-Version unterschiedlich. Ein Entwurf, der nur mit der
richtigen Fremdeinstellung funktioniert, ist keiner.
