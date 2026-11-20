# Bedienung

_Stand: November 2026_

Die Lampe hat zwei Bedienelemente: den **Drehknopf** (KY-040, drehen und drücken) und die
**Boot-Taste** auf dem ESP32-Board. Alles Weitere läuft über die Weboberfläche
(Hotspot `Lizas-Lamp`, siehe [README](../README.md)).

---

## Drehknopf drehen

| Richtung | Wirkung |
|---|---|
| Rechts | Helligkeit eine Stufe heller (0–7) |
| Links | Helligkeit eine Stufe dunkler; Stufe 0 = aus |

- Die Stufen sind an das Auge angepasst (jede etwa 1,5-mal heller als die vorige); Stufe 1 eignet
  sich als Nachtlicht.
- **Stufe 7 = Boost:** Zusätzlich zur weißen LED schalten Farb-Chips dazu, deutlich heller, der Ton
  verschiebt sich etwas. Gilt für Kerzenlicht, Warm-, Neutral- und Kaltweiß (nicht für eigene Farben).
- Jede Rastung ist genau eine Stufe. Die Helligkeit wird nach 5 s ohne weitere Änderung gespeichert
  (schont den Flash; vor dem Deep Sleep wird sofort gespeichert).
- Drehen heißt „Lampe benutzen“: Aus **Gute Nacht**, **Sleep** und einer **Systemwarnung** wechselt
  die Lampe in den Normalbetrieb. Ein laufender **Wecker** und kurze Hinweise bleiben unverändert.
- Die Weboberfläche ändert die Helligkeit nie, der Drehknopf ist dafür allein zuständig.

## Drehknopf kurz drücken

Die Wirkung hängt vom aktuellen Zustand ab (Details: [zustaende.md](zustaende.md)):

| Zustand | Wirkung | Rückmeldung |
|---|---|---|
| Kurzer Hinweis aktiv | Hinweis beenden, vorheriger Zustand | — |
| Sleep (Lampe aus) | Lampe einschalten | grüner Blitz |
| Wecker läuft | Wecker stoppen | roter Blitz |
| Gute Nacht läuft | Gute Nacht beenden | roter Blitz |
| Feier-Modus läuft | Feier-Modus beenden (für heute) | roter Blitz |
| Normalbetrieb | **Gute-Nacht-Modus starten** | grüner Blitz |

## Drehknopf lang drücken (≥ 1 s)

- Hotspot einschalten (Weboberfläche erreichbar). Das ist der **einzige** Weg zum Hotspot — beim
  Start verbindet sich die Lampe nur mit dem Heim-WLAN.
- Ist die Lampe gerade mit dem Heim-WLAN verbunden, kommt der Hotspot dazu (beides gleichzeitig).
- Kurzer gelber Blitz als Bestätigung; danach ist die Lampe wieder im vorherigen Zustand.

## Boot-Taste

| Druck | Wirkung |
|---|---|
| kurz | ohne Funktion |
| **lang (≥ 1 s)** | **Werksreset**: alle Einstellungen auf Standard, danach Neustart |

Nach dem Werksreset gelten die Standard-Zugangsdaten (Hotspot `Lizas-Lamp`, Passwort aus
`include/secrets.h` bzw. `Erdmaennchen`). Farbe, Wecker und WLAN-Daten sind zurückgesetzt.

---

## Aufwecken aus dem Deep Sleep

- **Drücken** des Drehknopfs weckt die Lampe; sie leuchtet sofort (ca. 60 ms) mit Helligkeit 1.
- **Drehen** weckt ebenfalls, sofern der Knopf beim Einschlafen in einer Rastung stand.
- Beim Aufwachen per Timer (Wecker, WLAN-Sync) bleibt die Lampe aus.

## Status-LED (blau, auf dem Board)

| Blinkmuster | Bedeutung |
|---|---|
| langsam (2 s an, 4 s aus) | WLAN aus |
| 1× kurz, Pause | nur Heimnetz |
| 2× kurz, Pause | nur Hotspot (Weboberfläche erreichbar) |
| 3× kurz, Pause | Heimnetz und Hotspot |
| aus | Deep Sleep |

Kurzes Blinken: 0,1 s an, 0,2 s Abstand, danach 2 s Pause (`statusLedTick()` in `src/led.cpp`).

---

## Automatisches Verhalten

| Bedingung | Aktion |
|---|---|
| WLAN an, 1 min kein Gerät im Hotspot | WLAN aus |
| Hotspot an, 10 min keine Bedienung der Weboberfläche | WLAN aus (auch mit verbundenem Gerät) |
| WLAN aus, 5 h vergangen | WLAN kurz ohne Hotspot an (Uhrzeit per NTP) |
| Lampe aus (Sleep oder Helligkeit 0), WLAN aus, Deep Sleep aktiviert | Deep Sleep bis zum nächsten Wecker/WLAN-Sync (wacht ca. 5 min plus erwartete Uhrabweichung vorher auf) |
| Wecker in weniger als 5 min | kein Deep Sleep |
| Wecker-Zeit erreicht (bis 10 min verspätet) | Wecker startet: Nutzerfarbe, Helligkeit steigt von 1 auf 7 |
| Wecker-Dauer abgelaufen | Lampe geht aus (Sleep) |
| Gute-Nacht-Dauer abgelaufen | Lampe geht aus (Sleep) |
| 1.1., 8.8., 2.9., 24.12. ab 7:00 | Feier-Modus (Regenbogen) |
