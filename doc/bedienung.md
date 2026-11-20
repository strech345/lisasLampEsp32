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

## Drehknopf gedrückt halten und drehen

- Wechselt das **Licht-Set** (nächstes/vorheriges, rundum), die Helligkeit bleibt.
- Ist die Lampe aus (Sleep oder Helligkeit 0), geht sie auf Stufe 1 an. Gute Nacht, Sleep und
  Systemwarnung wechseln in den Normalbetrieb; ein laufender Wecker übernimmt das neue Set.
- Wurde gedreht, löst das Loslassen weder einen kurzen Druck noch den Hotspot aus.

## Drehknopf kurz drücken (höchstens 0,5 s, ohne Drehen)

Länger gehalten, aber vor 3 s losgelassen, löst keine Aktion aus.

Die Wirkung hängt vom aktuellen Zustand ab (Details: [zustaende.md](zustaende.md)):

| Zustand | Wirkung | Rückmeldung |
|---|---|---|
| Kurzer Hinweis aktiv | Hinweis beenden, vorheriger Zustand | — |
| Sleep (Lampe aus) | Lampe einschalten | grüner Blitz |
| Wecker läuft | Wecker stoppen | roter Blitz |
| Gute Nacht läuft | Gute Nacht beenden | roter Blitz |
| Feier-Modus läuft | Feier-Modus beenden (für heute) | roter Blitz |
| Normalbetrieb | **Gute-Nacht-Modus starten** | grüner Blitz |

## Drehknopf lang drücken (3 s halten, ohne Drehen)

- Hotspot einschalten (Weboberfläche erreichbar); er startet noch während des Haltens nach 3 s.
  Das ist der **einzige** Weg zum Hotspot — beim
  Start verbindet sich die Lampe nur mit dem Heim-WLAN.
- Ist die Lampe gerade mit dem Heim-WLAN verbunden, kommt der Hotspot dazu (beides gleichzeitig).
- Kurzer gelber Blitz als Bestätigung, danach läuft ein **blauer Komet** über die Pixel, bis sich ein
  Gerät verbindet. Dann kehrt die Lampe in ihren vorherigen Zustand zurück (aus bleibt aus).
- Die Animation hat Vorrang vor einem Wecker; der läuft im Hintergrund weiter.
- **Abbrechen:** Kurzer Druck während der Animation schaltet Hotspot und Animation aus, sonst nichts.
  Drehen bricht ebenfalls ab und ändert zusätzlich die Helligkeit. Ein verbundenes Heimnetz bleibt.

## Licht-Sets (Hauptseite der Weboberfläche)

Ein Set ist eine Kombination aus Lichtfarbe und Animation. Es gibt bis zu **6 Sets**; eines ist aktiv
(in der Weboberfläche auswählen oder am Drehknopf: gedrückt halten + drehen). Sets lassen sich
hinzufügen, ändern und löschen (mindestens eines bleibt).

| Standard-Set | Lichtfarbe | Animation |
|---|---|---|
| 1 | Neutralweiß | Statisch |
| 2 (aktiv) | Warmweiß | Atmen |
| 3 | Kerzenlicht | Flamme |

| Lichtfarbe | Ton |
|---|---|
| Kerzenlicht | ca. 2200 K, fast nur Rot/Bernstein |
| Warmweiß | ca. 2700 K |
| Neutralweiß | ca. 4000 K, nur die weiße LED |
| Kaltweiß | ca. 6000 K |
| Eigene Farbe | frei wählbar (ohne Boost auf Stufe 7) |

| Animation | Wirkung |
|---|---|
| Statisch | gleichmäßiges Licht |
| Atmen | langsames Auf- und Abschwellen |
| Flamme | jedes Pixel flackert unabhängig; dunkle Phasen werden röter (Glut). Die Farbe kommt aus der Lichtfarbe — Kerzenlicht = Kerze, Blau = Gasflamme |

## Wecker (Hauptseite der Weboberfläche)

Bis zu **16 Wecker**, je Zeile: Ein/Aus, Wochentag bzw. Datum, Uhrzeit, Löschen.

- **Wecker hinzufügen:** neuer wöchentlicher Wecker (Montag 7:00); beim Maximum ausgegraut.
- **Einmaliger Termin:** in der Wochentag-Auswahl den letzten Eintrag **„Datum“** wählen. Statt des
  Wochentags erscheint ein Datumsfeld, vorbelegt mit dem heutigen Datum der Lampe (ohne gültige
  Uhrzeit: 1.1.2026). Zurück zu einem Wochentag geht es danach nicht mehr.
- Ein Termin feuert nur an diesem Tag; danach bleibt er blass als „vorbei“ in der Liste, bis man ihn
  löscht.

## Einstellungen in der Weboberfläche

Die Weboberfläche prüft alle 10 s die Verbindung zur Lampe (`/ping`). Ist sie nicht erreichbar, erscheint
„Verbindung zur Lampe verloren“ und **alle Felder und Knöpfe sind gesperrt** (nur ⚙ und „Zurück“ bleiben
bedienbar); kommt die Verbindung zurück, wird die Konfiguration neu geladen.

Unter **Einstellungen → System** lässt sich die **Anzahl der LEDs** (1–16, Standard 4) einstellen;
die Änderung wirkt sofort. Weitere Einstellungen: WLAN, Energie & Uhr, Warnungen, Ereignisprotokoll.

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
| Wecker-Zeit erreicht (bis 10 min verspätet) | Wecker startet: Farbe des aktiven Sets, Helligkeit steigt von 1 auf 7 |
| Wecker-Dauer abgelaufen | Lampe geht aus (Sleep) |
| Gute-Nacht-Dauer abgelaufen | Lampe geht aus (Sleep) |
| 1.1., 8.8., 2.9., 24.12. ab 7:00 | Feier-Modus (Regenbogen) |
