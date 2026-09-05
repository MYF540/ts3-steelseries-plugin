# Hinweise zu Fremdsoftware / Third-party notices

`ts3-steelseries-plugin` selbst steht unter der MIT-Lizenz (siehe `LICENSE`).
Dieses Dokument listet fremde Bestandteile.

---

## In der ausgelieferten DLL enthalten

### nlohmann/json 3.11.3

Header-only und damit einkompiliert. Die MIT-Lizenz verlangt, dass dieser Hinweis jeder
Weitergabe beiliegt — deshalb liegt diese Datei auch im `.ts3_plugin`-Paket.

<https://github.com/nlohmann/json>

```
MIT License

Copyright (c) 2013-2022 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Nur zum Bauen benötigt, nicht ausgeliefert

### TeamSpeak 3 Client Plugin SDK

**Copyright © TeamSpeak Systems GmbH. All rights reserved.**

Keine Open-Source-Lizenz. Das SDK liefert die Header, gegen die dieses Plugin
kompiliert wird.

Es liegt deshalb als **Git-Submodul** unter `third_party/ts3-pluginsdk` und wird von
diesem Repository **nicht mitverteilt** — beim Klonen holt es `git submodule update`
direkt von TeamSpeak. Die ausgelieferte DLL enthält Deklarationen aus diesen Headern
(Funktionszeiger-Struktur, Enums, Konstanten), wie es für ein Client-Plugin unvermeidbar
und von TeamSpeak vorgesehen ist.

<https://github.com/TeamSpeak-Systems/ts3client-pluginsdk>

### doctest 2.4.11

MIT-Lizenz, Copyright © 2016–2023 Viktor Kirilov. Nur für die Unit-Tests; nicht Teil der
DLL und nicht Teil des Pakets.

<https://github.com/doctest/doctest>

---

## Nicht enthalten, nur angesprochen

Das **SteelSeries GameSense SDK** wird nicht eingebunden. Die Kommunikation läuft über
dessen dokumentierte lokale HTTP-Schnittstelle; es wird weder Code noch Header davon
verwendet.

<https://github.com/SteelSeries/gamesense-sdk>
