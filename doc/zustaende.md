# Lampenzustände

_Stand: November 2026 · Code: `src/lamp_state.cpp`, Enum `LampState` in `include/types.h`_

---

## Zustände

| Zustand | Wert | Bedeutung | LEDs beim Eintritt |
|---|---|---|---|
| `LAMP_STATE_DEFAULT` | 0 | Normalbetrieb | Nutzerfarbe, -animation und -helligkeit (0 = aus) |
| `LAMP_STATE_ALARM` | 1 | Wecker läuft | Nutzerfarbe; Helligkeit steigt in den ersten 70 % der Dauer von 1 auf 7 |
| `LAMP_STATE_GOOD_NIGHT` | 2 | Einschlafen | Nutzerfarbe; Helligkeit sinkt über die Dauer von der Nutzer-Helligkeit auf 0 |
| `LAMP_STATE_ERROR` | 3 | kurzer Hinweis: Fehler | rot |
| `LAMP_STATE_SUCCESS` | 4 | kurzer Hinweis: OK | grün |
| `LAMP_STATE_WARNING` | 5 | kurzer Hinweis: Warnung | gelb |
| `LAMP_STATE_SLEEP` | 6 | Lampe aus, bis zur Bedienung | aus |
| `LAMP_STATE_CELEBRATION` | 7 | Feier-Modus | Regenbogen, Helligkeit 7 |
| `LAMP_STATE_SYS_ERROR` | 8 | Systemfehler: keine gültige Uhrzeit | ganze Lampe rot, Nutzer-Helligkeit |
| `LAMP_STATE_SYS_WARNING` | 9 | Systemwarnung: Zeit veraltet | ganze Lampe gelb, Nutzer-Helligkeit |

Kurze Hinweise nutzen die Nutzer-Helligkeit, bei ausgeschalteter Lampe Stufe 7 (damit sie sichtbar sind).
Wecker- und Gute-Nacht-Helligkeit werden alle 5 s nachgeführt.

---

## Priorität (`checkLampState()`)

```
1. Kurzer Hinweis      — merkt sich den Zustand davor und stellt ihn danach wieder her
2. Wecker              — endet der Wecker → Sleep
3. Gute Nacht          — endet die Dauer → Sleep
4. Feier-Modus
5. Sleep               — bleibt, bis die Lampe bedient wird
6. Systemwarnung       — nur wenn „Warnungen“ an UND Helligkeit > 0
7. Normalbetrieb
```

Eine ausgeschaltete Lampe (Sleep oder Helligkeit 0) wird nie durch eine Systemwarnung eingeschaltet.
Ein Hinweis (z. B. beim Speichern oder langen Tastendruck) unterbricht einen Zustand nur kurz: Aus Sleep
wird danach wieder Sleep.

---

## Übergänge

| Ereignis | Neuer Zustand | Ort |
|---|---|---|
| Aufwecken per Taster/Drehen | Normalbetrieb, Helligkeit 1 | `main.cpp` |
| Aufwecken per Timer | Sleep | `main.cpp` |
| Wecker-Zeit erreicht (bis 10 min verspätet, gültige Uhrzeit) | Wecker | `alarm.cpp` |
| Wecker-Dauer abgelaufen | Sleep | `lamp_state.cpp` |
| Kurzer Druck im Normalbetrieb | Gute Nacht | `lamp_state.cpp` (`onShortPress`) |
| Gute-Nacht-Dauer abgelaufen | Sleep | `good_night.cpp` |
| Feiertag ab 7:00 | Feier-Modus | `celebration.cpp` |
| `startAlert(…)` | Hinweis | `alerts.cpp` |
| Hinweis abgelaufen oder kurzer Druck | Zustand vor dem Hinweis | `lamp_state.cpp` |
| Warnungen an, Lampe an, Uhrzeit vor 2020 | Systemfehler | `lamp_state.cpp` |
| Warnungen an, Lampe an, letzte Zeitquelle > 24 h (bzw. 30 min nach Start keine) | Systemwarnung | `lamp_state.cpp` |

Eine Konfigurationsänderung über die Weboberfläche beendet einen laufenden Wecker nur, wenn genau dieser
Wecker deaktiviert oder zeitlich geändert wurde.

---

## Bedienung je Zustand

| Zustand | Kurzer Druck | Drehen |
|---|---|---|
| Normalbetrieb | → Gute Nacht (grüner Blitz) | Helligkeit ändern |
| Sleep | → Normalbetrieb (grüner Blitz) | → Normalbetrieb mit neuer Helligkeit |
| Wecker | Wecker stoppen → Normalbetrieb (roter Blitz) | Helligkeit (wird vom Weckverlauf überschrieben) |
| Gute Nacht | beenden → Normalbetrieb (roter Blitz) | beenden → Normalbetrieb mit neuer Helligkeit |
| Feier-Modus | für heute beenden → Normalbetrieb (roter Blitz) | Helligkeit ändern |
| Hinweis | Hinweis beenden → vorheriger Zustand | Helligkeit ändern |
| Systemwarnung/-fehler | → Gute Nacht | → Normalbetrieb mit neuer Helligkeit |

Langer Druck schaltet in jedem Zustand das WLAN mit Hotspot ein (kurzer gelber Hinweis).
