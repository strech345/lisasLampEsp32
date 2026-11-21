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

- Wechselt das **Szene** (nächstes/vorheriges, rundum), die Helligkeit bleibt.
- Ist die Lampe aus (Sleep oder Helligkeit 0), geht sie auf Stufe 1 an. Gute Nacht, Sleep und
  Systemwarnung wechseln in den Normalbetrieb; ein laufender Wecker übernimmt die neue Szene.
- Wurde gedreht, löst das Loslassen weder einen kurzen Druck noch den Hotspot aus.

## Drehknopf kurz drücken (höchstens 0,5 s, ohne Drehen)

Länger gehalten, aber vor 2 s losgelassen, löst keine Aktion aus.

Die Wirkung hängt vom aktuellen Zustand ab (Details: [zustaende.md](zustaende.md)):

| Zustand | Wirkung | Rückmeldung |
|---|---|---|
| Kurzer Hinweis aktiv | Hinweis beenden, vorheriger Zustand | — |
| Sleep (Lampe aus) | Lampe einschalten | grüner Blitz |
| Wecker läuft | Wecker stoppen | roter Blitz |
| Gute Nacht läuft | Gute Nacht beenden | roter Blitz |
| Feier-Modus läuft | Feier-Modus beenden (für heute) | roter Blitz |
| Normalbetrieb | **Gute-Nacht-Modus starten** | grüner Blitz |

## Drehknopf lang drücken (2 s halten, ohne Drehen)

- Hotspot einschalten (Weboberfläche erreichbar); er startet noch während des Haltens nach 2 s.
  Das ist der **einzige** Weg zum Hotspot — beim
  Start verbindet sich die Lampe nur mit dem Heim-WLAN.
- Hotspot und Heim-WLAN sind getrennt: Der lange Druck schaltet nur den Hotspot ein und verbindet
  nicht mit dem Heim-WLAN. Läuft gerade ein Zeitabgleich übers Heim-WLAN, kommt der Hotspot dazu
  (beides gleichzeitig); ebenso kann ein fälliger Abgleich bei laufendem Hotspot dazukommen.
- Kurzer gelber Blitz als Bestätigung, danach läuft ein **blauer Komet** über die Pixel, bis sich ein
  Gerät verbindet. Dann kehrt die Lampe in ihren vorherigen Zustand zurück (aus bleibt aus).
- Die Animation hat Vorrang vor einem Wecker; der läuft im Hintergrund weiter.
- **Abbrechen:** Kurzer Druck während der Animation schaltet Hotspot und Animation aus, sonst nichts.
  Drehen bricht ebenfalls ab und ändert zusätzlich die Helligkeit. Ein verbundenes Heimnetz bleibt.

## Szenen (Hauptseite der Weboberfläche)

Eine Szene ist eine Kombination aus Lichtfarbe und Animation. Es gibt bis zu **6 Szenen**; eine ist aktiv
(in der Weboberfläche auswählen oder am Drehknopf: gedrückt halten + drehen). Szenen lassen sich
hinzufügen, ändern und löschen (mindestens eine bleibt).

| Standard-Szene | Lichtfarbe | Animation |
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
| Eigene Farbe | frei wählbar: Eintrag „Eigene Farbe“ anklicken öffnet den Farbwähler; der Rand zeigt die Farbe (ohne Boost auf Stufe 7) |

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
- Ein Termin feuert nur an diesem Tag. Liegt er in der Vergangenheit, wird er blass angezeigt und
  lässt sich nicht einschalten; Datum und Uhrzeit bleiben bearbeitbar (in die Zukunft verschoben →
  wieder einschaltbar). Neuer Termin: Vorschlag Lampenuhr + 1 Stunde.

## Einstellungen in der Weboberfläche

**Sprache:** Unter **Einstellungen → Persönlich → Sprache / Language** zwischen Deutsch und Englisch wählen.
Die Wahl wird im Browser gespeichert (pro Gerät/Browser). Das Ereignisprotokoll schreibt die Firmware
einheitlich auf Englisch; bei Deutsch übersetzt die Weboberfläche die Meldungen
(`svelte-ui/src/lib/logTranslations.js`).

Die Weboberfläche prüft alle 10 s die Verbindung zur Lampe (`/ping`). Ist sie nicht erreichbar, erscheint
„Verbindung zur Lampe verloren“ und **alle Felder und Knöpfe sind gesperrt** (nur ⚙ und „Zurück“ bleiben
bedienbar); kommt die Verbindung zurück, wird die Konfiguration neu geladen.

Bereiche der Einstellungen:

| Bereich | Inhalt |
|---|---|
| Persönlich | Sprache, Name der Lampe, Party-Tage |
| WLAN | Hotspot (inkl. Laufzeit), externes WLAN, Fallback |
| Synchronisation | Zeitabgleich übers Heimnetz (Intervalle nach Erfolg/Fehlschlag); externer 32-kHz-Quarz (hält die Uhr zwischen den Abgleichen genau, Umschalten mit Neustart); TSF-Zeitfallback mit gespeicherten APs, letztem Snapshot, Genauigkeit beim letzten Snapshot (max. Abweichung gegenüber NTP seit dem vorigen Snapshot), letzter Berechnung und Knopf **Snapshot jetzt erstellen**; RDS-Zeitfallback; letzter Abgleich (NTP/RDS) und Knopf **Uhr jetzt synchronisieren** — verbindet das Heimnetz, fragt bis 60 s nach und meldet Erfolg oder Fehlschlag, das Datum aktualisiert sich |
| LEDs | **Anzahl der LEDs** (1–16, Standard 4), LED-Warnungen, Status-LED auf der Platine |
| System | Deep Sleep, Ereignisprotokoll, serielle Logs (siehe unten), Werksreset |
| Systemmonitor | Verlauf und Kennzahlen (siehe unten) |
| Ereignisprotokoll | letzte 200 Ereignisse |

**Speichern ohne Knopf:** Zahlen- und Textfelder (Name, Party-Tage, Hotspot-Laufzeit, Sync-Intervalle,
LED-Anzahl) werden gespeichert, sobald man das Feld verlässt — wie die Dauer-Felder der Startseite.
Schalter, Monatsauswahl sowie Party-Tag hinzufügen/entfernen speichern sofort. Ungültige Eingaben werden
gemeldet und auf den gespeicherten Wert zurückgesetzt. Nur die WLAN-Zugangsdaten haben Haken (siehe unten).

Der **Systemmonitor** zeigt (alle 5 s aktualisiert, solange er offen ist) CPU-Last, RAM-Belegung und WLAN-Signalqualität zum Router sowie vom eigenen Gerät zum Hotspot (je in %, Lücke wenn nicht verbunden bzw. über den Router verbunden) als Verlauf der letzten 30 Minuten. Darunter stehen die Kennzahlen in Gruppen:

| Gruppe | Inhalt |
|---|---|
| Uhr | Systemzeit zum Zeitpunkt der letzten Abfrage (alle 5 s), letzter **absoluter** Abgleich (NTP oder RDS, mit Quelle), letzter **relativer** Abgleich (TSF über WLAN-Beacons), letzter NTP-Versuch (erfolgreich/fehlgeschlagen), nächster NTP-Abgleich |
| WLAN | Heimnetz (verbunden/getrennt — getrennt ist der Normalfall, es wird nur zum Abgleich verbunden), Signal zum Router, Geräte am Hotspot (IP und Signalstärke, das eigene fett mit „(du)“) |
| Speicher | RAM frei/gesamt, RAM-Minimum, größter freier Block, Dateisystem, Firmware, Flash-Chip |
| System | CPU-Last, Temperatur, Laufzeit, letzter Neustartgrund |

Die Abgleich-Zeitpunkte überstehen den Deep Sleep, nach Reset/Stromausfall steht dort „noch nie“. Der Verlauf liegt nur im Browser und ist nach einem Seiten-Reload leer.

**WLAN:** `******` im Passwortfeld steht für das gespeicherte Passwort. Wer das Feld leert, löscht das
Passwort beim Speichern (Hotspot dann offen). Die Sektion ist in die Rubriken **Hotspot**, **Externes WLAN** und **Externes WLAN Fallback** gegliedert, je eine Zeile SSID und Passwort. Einen allgemeinen Speichern-Knopf gibt es nicht: Jede Zeilengruppe speichert über ihren eigenen **Haken** (`CheckButton.svelte`).
Ein **voller** Haken heißt „so gespeichert“, ein **gestrichelter** „ungespeicherte Änderung“.

| Knopf | Wirkung |
|---|---|
| Lupe (hinter SSID, externe WLANs) | Lampe sucht WLANs in der Nähe; Auswahlliste mit Signalstärke und 🔒, Auswahl übernimmt die SSID |
| Haken (hinter Passwort, externe WLANs) | Lampe verbindet sich testweise mit SSID + Passwort aus den Feldern (leeres Passwort = offenes Netz); Meldung: erfolgreich, Passwort falsch, Netz nicht gefunden oder Zeitüberschreitung. **Bei Erfolg** und ungespeicherten Änderungen wird dieses WLAN gleich gespeichert („erfolgreich – gespeichert“) und ein neuer Zeitabgleich startet. Leere SSID + Haken = WLAN entfernen (ohne Test) |
| Haken (hinter Passwort, Hotspot) | speichert Name und Passwort des Hotspots (testen lässt er sich nicht); bei Änderung startet der Hotspot neu, das Handy muss sich neu verbinden |

Suche und Test dauern einige Sekunden (Test höchstens 15 s). Danach stellt die Lampe ihre gespeicherte
Verbindung wieder her. Während des Tests kann der Hotspot kurz den Kanal wechseln; das Handy verbindet
sich dann meist von selbst neu.

Zeiten in Minuten (gespeichert beim Verlassen des Feldes, ohne WLAN-Neustart; Hotspot-Laufzeit unter **WLAN → Hotspot**, die beiden Intervalle unter **Synchronisation**):

| Feld | Standard | Bereich | Bedeutung |
|---|---|---|---|
| Hotspot → Laufzeit (ohne Bedienung) | 10 | 0–1440 | danach schaltet sich der Hotspot aus; 0 = bleibt an, bis er per Drehknopf ausgeschaltet wird (kein Deep Sleep, solange er läuft) |
| Synchronisation → Nach Erfolg alle | 300 | 5–720 | so oft holt sich die Lampe die Uhrzeit aus dem Internet |
| Synchronisation → Nach Fehlschlag alle | 300 | 5–720 | so schnell versucht sie es erneut, wenn der letzte Abgleich nicht geklappt hat |

Im Deep Sleep wacht die Lampe spätestens zum nächsten Abgleich auf.

**Persönlich:** Name der Lampe (1–20 Zeichen, erscheint im Seitentitel, z. B. „Liza's Lampe“) und bis
zu 8 **Party-Tage** (Tag + Monat). An diesen Tagen startet um 7:00 der Feier-Modus und die Hauptseite
zeigt Ballons. Gespeichert wird beim Verlassen des Feldes bzw. sofort beim Hinzufügen/Entfernen; das WLAN wird dabei nicht neu gestartet. Standardwerte
(nach Werksreset) stehen in `include/secrets.h`.

**System** (jeder Schalter wirkt sofort und wird gespeichert; die Status-LED steht unter **LEDs**, blaue LED auf dem Board an/aus):

| Schalter | Wirkung |
|---|---|
| Ereignisprotokoll aufzeichnen | aus = keine neuen Einträge, vorhandene bleiben |
| Serielle Logs (Monitor) | Textausgabe für `pio device monitor` an/aus |
| Ausgabe über | **UART** (TX = GPIO21, RX = GPIO20, z. B. FTDI) oder **USB** (USB-CDC des ESP32-C3) |

USB-Logs gibt es nur, solange die Lampe wach ist: Im Deep Sleep ist der USB-Anschluss abgeschaltet.

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
| Hotspot an, 1 min kein Gerät verbunden (nicht bei Hotspot-Laufzeit 0) | Hotspot aus (ein laufender Zeitabgleich bleibt) |
| Zeitabgleich übers Heim-WLAN nach 1 min ohne Erfolg | Heim-WLAN getrennt (Hotspot bleibt) |
| Hotspot an, Laufzeit ohne Bedienung abgelaufen (Standard 10 min, 0 = nie) | Hotspot aus (auch mit verbundenem Gerät) |
| Sync-Intervall vergangen (Standard 5 h; nach Erfolg bzw. Fehlschlag getrennt einstellbar), egal ob der Hotspot läuft | kurz mit dem Heim-WLAN verbinden (Uhrzeit per NTP); ein laufender Hotspot bleibt an |
| Lampe aus (Sleep oder Helligkeit 0), WLAN aus, Deep Sleep aktiviert | Deep Sleep bis zum nächsten Wecker/WLAN-Sync (wacht ca. 5 min plus erwartete Uhrabweichung vorher auf) |
| Wecker in weniger als 5 min | kein Deep Sleep |
| Wecker-Zeit erreicht (bis 10 min verspätet) | Wecker startet: Farbe der aktiven Szene, Helligkeit steigt von 1 auf 7 |
| Wecker-Dauer abgelaufen | Lampe geht aus (Sleep) |
| Gute-Nacht-Dauer abgelaufen | Lampe geht aus (Sleep) |
| Party-Tag ab 7:00 (Standard 1.1., 8.8., 2.9., 24.12.; Einstellungen → Persönlich) | Feier-Modus (Regenbogen) |
