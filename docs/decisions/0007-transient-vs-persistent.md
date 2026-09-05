# ADR 0007 — Nur Ereignisse dürfen den Schirm beanspruchen

**Status:** akzeptiert (2026-09-05)

Verfeinert [ADR 0006](0006-event-driven-screen-ownership.md).

## Kontext

ADR 0006 legte fest: Der Schirm wird belegt, solange Widgets etwas liefern, und danach
freigegeben. Als Signal dafür diente „alle Widgets liefern `nullopt`".

Im Test fiel auf, dass das nicht genügt. **Bei stummgeschaltetem Mikrofon oder
Kopfhörer wurde der Schirm nie freigegeben.** Das Mute-Widget lieferte korrekt und
dauerhaft „Mikro aus" — und hielt damit das Display, solange der Nutzer stumm war.
Also potenziell stundenlang, mit genau der Flackerkonkurrenz zu NowPlaying, die ADR 0006
vermeiden sollte.

Der Fehler steckte nicht in der Umsetzung, sondern im Modell: `nullopt` vermischt zwei
verschiedene Aussagen.

## Die Unterscheidung

Widget-Ausgaben zerfallen in zwei Sorten:

| | Beispiele | Dauer |
|---|---|---|
| **Ereignis** | jemand spricht, Poke, neue Nachricht, jemand betritt den Channel, Mute wurde *gerade umgeschaltet* | Sekunden |
| **Dauerzustand** | du *bist* stumm, Channelname, verbunden | Minuten bis Stunden |

Ein Ereignis rechtfertigt, dem Nutzer seine Musikanzeige wegzunehmen. Ein Dauerzustand
nicht — sonst gehörte das Display für immer uns.

## Entscheidung

`WidgetOutput` bekommt ein Feld:

```cpp
bool demandsScreen = false;
```

- **`true`** — „das ist gerade passiert, dafür lohnt sich der Schirm".
- **`false`** — „das ist gerade wahr; zeig es mit, falls der Schirm ohnehin belegt ist".

Der Composer liefert **nur dann einen nicht-leeren Frame, wenn mindestens ein Widget
`demandsScreen` setzt.** Dauerzustände füllen die verbleibenden Zeilen, können den
Schirm aber nie allein beanspruchen.

Damit gilt weiterhin die Regel aus ADR 0006 — leerer Frame heißt Freigabe —, nur ist
„leer" jetzt richtig definiert.

### Dauerzustände melden sich beim Umschalten

Dass Mute für die Anzeige unwichtig wäre, folgt daraus nicht. Der **Wechsel** ist ein
Ereignis: Stummschalten zeigt für ein paar Sekunden „Mikro aus" und verstummt dann.

Dafür führt `ClientState` Zeitstempel (`selfFlagsChangedAt`, `channelChangedAt`,
`connectionChangedAt`). Ein Widget setzt `demandsScreen` innerhalb eines kurzen Fensters
nach dem Wechsel und lässt es danach fallen.

Die Zeitstempel sind bewusst **nicht** Teil von `operator==`: Sie ändern sich immer
gemeinsam mit dem Wert, den sie beschreiben, und mitzuvergleichen ließe unveränderten
Zustand nach jedem Rebuild geändert aussehen.

## Konsequenzen

- **Der gemeldete Fehler ist strukturell behoben**, nicht als Sonderfall für Mute. Jedes
  künftige Zustands-Widget erbt das richtige Verhalten.
- **Widgets müssen eine Frage mehr beantworten.** Nicht nur „was zeige ich?", sondern
  „ist das ein Ereignis oder ein Zustand?". Die Voreinstellung `false` ist die sichere:
  Wer nichts angibt, kann den Schirm nicht kapern.
- **Der Zeitpunkt zählt, nicht nur der Wert.** `RenderContext` führt daher `now` mit,
  statt dass Widgets selbst die Uhr lesen — so bleiben sie in Tests deterministisch
  prüfbar.
- **Der Worker muss auch ohne Zustandsänderung ticken**, weil ein Fenster durch bloßen
  Zeitablauf endet. Das tut er ohnehin alle 120 ms.

## Verworfene Alternative

**Mute nach `hold_ms` einfach ausblenden, Sonderfall im Composer.** Hätte den gemeldeten
Fehler behoben und beim nächsten Zustands-Widget — Away, Verbindungsqualität,
Aufnahme-Anzeige — genauso wieder auftreten lassen. Der Fehler war allgemein, die Lösung
gehört es auch zu sein.
