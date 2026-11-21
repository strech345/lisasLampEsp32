# TSF-Uhrzeit: Uhr stellen ohne Internet

_Stand: November 2026_ · Code: `src/tsf_clock.cpp`, Auslöser in `src/wifi_controller.cpp`

Fällt das Internet aus, kann die Lampe die Uhrzeit nicht per NTP holen. Die eigene Uhr (RTC) läuft
weiter, driftet aber — ohne Quarz etwa 1,5 % (≈ 11 min in 12 h). Der **TSF-Fallback** stellt die Uhr
dann aus den Funksignalen der WLANs in der Nähe, ganz ohne Verbindung zu ihnen. Diese Datei erklärt die
Logik Schritt für Schritt und rechnet ein Beispiel über mehrere Tage durch.

---

## 1. Grundidee in einem Satz

Jeder WLAN-Access-Point (AP) sendet etwa zehnmal pro Sekunde ein **Beacon** mit seinem **TSF-Zähler**
(Timing Synchronization Function): einer Uhr in Mikrosekunden, die seit dem Einschalten des APs stur
hochzählt. Kennt die Lampe zu **einem** Zeitpunkt die echte Uhrzeit **und** den Zählerstand eines APs,
kann sie später aus dem neuen Zählerstand die Uhrzeit zurückrechnen:

```
Uhrzeit jetzt = Uhrzeit damals + (Zähler jetzt − Zähler damals)
```

---

## 2. Begriffe

| Begriff | Bedeutung |
|---|---|
| **Beacon-Erfassung** | 2 s lang alle Beacons auf dem aktuellen Funkkanal mitschneiden (Promiscuous-Modus), je AP ein Zählerstand |
| **Bezugspunkt** | pro AP gespeichert: Zählerstand + Uhrzeit zum selben Moment (`TsfEntry`, bis 16 APs, im Flash) |
| **Snapshot** | Beacon-Erfassung direkt nach einem **erfolgreichen** NTP-Abgleich → neue Bezugspunkte aus echter Zeit |
| **Fallback** | Beacon-Erfassung nach einem **gescheiterten** NTP-Versuch → Uhrzeit aus den Bezugspunkten berechnen |
| **Generation** | Herkunft eines Bezugspunkts: **0** = Uhrzeit aus NTP; **n** = aus einer TSF-Berechnung (neu basiert), höchstens eine Stufe über den dabei verwendeten Bezügen. Nur zur Information |
| **Gang (Drift)** | wie viel schneller (+) oder langsamer (−) der Zähler eines APs gegenüber echter Zeit läuft, in ppm (Millionstel). Quarze von APs liegen typisch bei ±5…±25 ppm |
| **Messpunkt** | ein NTP-genauer Bezugspunkt, ab dem der Gang des APs gemessen wird |
| **Gangkorrektur** | bekannte Gangabweichung aus der Zählerdifferenz herausrechnen |
| **Neu basieren** | einem AP einen neuen Bezugspunkt geben, wenn der alte unbrauchbar ist (Neustart, Ausreißer) |

---

## 3. Wann passiert was?

Bei **jedem** Versuch, die Uhr per NTP zu stellen (automatisch nach dem Sync-Intervall, beim Start, per
„Uhr jetzt synchronisieren“), hängt sich eine Beacon-Erfassung an:

```
NTP-Versuch über das Heimnetz
 ├─ Erfolg ──► Snapshot:  1. Genauigkeitstest (TSF gegen NTP)
 │                        2. Gang messen (falls Messpunkt ≥ 2 h alt)
 │                        3. Bezugspunkte erneuern (Generation 0)
 │                        → danach Heimnetz aus
 └─ Fehlschlag ──► Fallback: Uhrzeit aus den Bezugspunkten berechnen,
   (Router nicht           neu gestartete APs / Ausreißer neu basieren
    erreichbar oder        → danach Heimnetz aus
    1 min ohne NTP)
```

Das Funkteil bleibt dafür nach dem Fehlschlag noch ~2 s an (`syncFailed()` → `requestTsfFallback()` →
`dropStaAfterFail()`). Ob der Hotspot läuft, spielt keine Rolle.

**Snapshot von Hand** (Knopf in Einstellungen → Synchronisation) ist nur möglich, wenn der letzte
NTP-Erfolg **in der aktuellen Wachphase** höchstens 10 min her ist (gemessen mit `millis()`, das nach
jedem Deep Sleep neu beginnt). Sonst würde eine schon gedriftete Uhrzeit als „echter“ Generation-0-Bezug
gespeichert und alle späteren Rechnungen verfälschen (siehe Abschnitt 6.1).

---

## 4. Die Rechenwege

### 4.1 Snapshot (nach NTP-Erfolg)

Für jeden gehörten AP, in dieser Reihenfolge:

1. **Genauigkeitstest** — nur für schon gespeicherte APs mit Generation 0 und ohne Neustart:
   `Abweichung = berechnete TSF-Zeit − NTP-Zeit`. Angezeigt wird die **größte** Abweichung, wie lange
   der Bezug zurücklag und wie viele APs verglichen wurden.
2. **Gang messen** — liegt der Messpunkt ≥ 2 h zurück und hat der AP nicht neu gestartet:
   ```
   Gang = (Zählerlauf seit Messpunkt − echte Zeit seit Messpunkt) / echte Zeit seit Messpunkt
   neuer Gang = Mittel aus bisherigem und gemessenem Gang (beim ersten Mal: der gemessene)
   ```
   Werte über ±200 ppm sind unplausibel und werden verworfen. Unter 2 h wird nicht gemessen, weil
   NTP selbst um einige ms schwankt (5 ms auf 30 min wären schon ±3 ppm Messfehler).
3. **Bezugspunkt erneuern** — Zählerstand + NTP-Zeit, Generation 0. Unbekannte APs kommen dazu; ist
   die Liste voll (16), verdrängen sie den schwächsten Eintrag (Signalstärke + Laufzeit des APs).

### 4.2 Fallback (nach NTP-Fehlschlag)

1. Für jeden gehörten, gespeicherten AP:
   - **Zähler kleiner als im Bezug** → der AP wurde neu gestartet → später neu basieren.
   - sonst **Schätzung** mit Gangkorrektur:
     ```
     Zählerdifferenz   d = Zähler jetzt − Zähler im Bezug
     echte Zeitspanne  = d · (1 − Gang)          (Gang unbekannt → d)
     Schätzung         = Uhrzeit im Bezug + echte Zeitspanne
     ```
2. Weniger als 3 Schätzungen → **keine** Uhrzeit setzen (RTC läuft weiter), nichts neu basieren.
3. **Ausreißer** aussortieren: Schätzungen, die mehr als 30 s vom Median **aller** Schätzungen abliegen.
4. Bleiben mindestens 3 übrig, wird ihr **Median** die neue Uhrzeit. Angezeigt wird die größte
   Abweichung einer verwendeten Schätzung von diesem Median.
5. **Neu basieren** mit der gerade berechneten Uhrzeit:
   - neu gestartete APs: Generation = höchste verwendete + 1, **Gang bleibt** (gleicher Quarz),
     Messpunkt ungültig;
   - Ausreißer: ebenso, aber **Gang verworfen** (womöglich ein anderes Gerät, z. B. Router getauscht).
   Alle anderen Bezüge bleiben unverändert.

---

## 5. Durchgerechnetes Beispiel

Fünf APs A–E sind zu hören. Ihre Zähler laufen unterschiedlich falsch (das weiß die Lampe anfangs nicht):

| AP | wahrer Gang |
|---|---|
| A | +12 ppm |
| B | −8 ppm |
| C | +3 ppm |
| D | +20 ppm |
| E | −15 ppm |

Zur Einordnung: **1 ppm über 1 h = 3,6 ms**. +12 ppm über 5 h ergibt also 12 · 5 · 3,6 = **216 ms**.

### Schritt 1 — Mo 08:00, NTP erfolgreich → erster Snapshot

Die Liste ist leer. Alle fünf APs bekommen einen Bezugspunkt (Generation 0), Messpunkt = jetzt,
Gang noch **unbekannt**. Kein Genauigkeitstest (nichts zum Vergleichen).

| AP | Generation | Gang | Messpunkt |
|---|---|---|---|
| A–E | 0 | unbekannt | Mo 08:00 |

### Schritt 2 — Mo 13:00, NTP erfolgreich → Snapshot mit Gangmessung

**1. Genauigkeitstest** (Bezüge 5 h alt, ohne Gangkorrektur, weil der Gang noch unbekannt ist):

| AP | TSF-Zeit liegt daneben |
|---|---|
| A | +216 ms |
| B | −144 ms |
| C | +54 ms |
| D | +360 ms |
| E | −270 ms |

→ Anzeige „Genauigkeit beim letzten Snapshot: max. Abweichung ±360 ms nach 5 h (5 APs)“.

**2. Gang messen:** Die Messpunkte sind 5 h alt (≥ 2 h). Beispiel A: Zähler lief 5 h + 216 ms, also
216 ms / 18 000 s = **+12 ppm**. Ebenso B −8, C +3, D +20, E −15.

**3. Bezüge erneuern:** alle Generation 0 mit Zeit 13:00, Messpunkt 13:00.

| AP | Generation | Gang | Messpunkt |
|---|---|---|---|
| A | 0 | +12 ppm | Mo 13:00 |
| B | 0 | −8 ppm | Mo 13:00 |
| C | 0 | +3 ppm | Mo 13:00 |
| D | 0 | +20 ppm | Mo 13:00 |
| E | 0 | −15 ppm | Mo 13:00 |

### Schritt 3 — Mo 18:00, Internet weg → NTP scheitert → erster Fallback

Inzwischen ist einiges passiert:
- **C** wurde neu gestartet (Stromausfall beim Nachbarn) → sein Zähler ist kleiner als im Bezug.
- **E** wurde durch einen neuen Router mit gleicher Kennung ersetzt → sein Zähler passt zu nichts.
- Die Quarze von A und D sind wärmer geworden und laufen leicht anders als gemessen
  (A real +12,4 statt +12 ppm, D +19,5 statt +20). Diese **Restabweichung** kann keine Korrektur kennen.

Schätzungen (5 h seit dem Bezug, Gangkorrektur angewendet), angegeben als Fehler gegenüber der wahren Zeit:

| AP | Rechnung | Ergebnis |
|---|---|---|
| A | Rest +0,4 ppm · 5 h | +7 ms |
| B | Rest 0 ppm | 0 ms |
| C | Zähler < Bezug | **Neustart** → neu basieren |
| D | Rest −0,5 ppm · 5 h | −9 ms |
| E | passt zu nichts | +3 h |

- Median aller vier Schätzungen (−9, 0, +7 ms, +3 h) = +7 ms → **E** liegt 3 h davon weg (> 30 s) →
  **Ausreißer**.
- Übrig: D −9, B 0, A +7 → 3 Schätzungen, Median **0 ms** → **Uhr gestellt**, Fehler 0 ms.
  Anzeige: „Letzte Berechnung: ✓ 3 APs mit NTP-Bezug, max. Abweichung ±9 ms“.
- **Neu basieren** mit dieser Uhrzeit (18:00):

| AP | Generation | Gang | Messpunkt | Grund |
|---|---|---|---|---|
| A, B, D | 0 (unverändert) | wie bisher | Mo 13:00 | verwendet, Bezug bleibt |
| C | **1** | +3 ppm (bleibt) | ungültig | Neustart, gleicher Quarz |
| E | **1** | **unbekannt** | ungültig | Ausreißer, evtl. anderes Gerät |

Ohne Gangkorrektur hätte A hier +216 ms, D +360 ms geliefert — der Median läge bei +216 ms daneben.

### Schritt 4 — Di 07:00, Internet immer noch weg → zweiter Fallback

Bezüge von A, B, D sind jetzt **18 h** alt (seit Mo 13:00), von C und E **13 h** (seit Mo 18:00).
Restabweichungen: A +0,4, B −0,3, D −0,5, C +0,2 ppm; E (neuer Router) läuft −0,4 ppm, Gang ist aber
unbekannt — sein Zähler wird also nicht korrigiert, hier zufällig nahe an richtig.

| AP | Generation | Rechnung | Fehler |
|---|---|---|---|
| A | 0 | +0,4 ppm · 18 h | +26 ms |
| B | 0 | −0,3 ppm · 18 h | −19 ms |
| C | 1 | 0 ms (Fehler aus Schritt 3) + 0,2 ppm · 13 h | +9 ms |
| D | 0 | −0,5 ppm · 18 h | −32 ms |
| E | 1 | 0 ms (aus Schritt 3) − 0,4 ppm · 13 h | −19 ms |

- Keine Ausreißer. Sortiert: −32, −19, −19, +9, +26 → Median **−19 ms** → Uhr gestellt, 19 ms zu früh.
- Größte Abweichung vom Median: A, +26 − (−19) = **±45 ms**.
  Anzeige: „✓ 5 APs mit abgeleitetem Bezug (Stufe 1), max. Abweichung ±45 ms“.
- Nichts neu zu basieren.

**Warum sich hier nichts aufaddiert:** C und E tragen den Fehler aus Schritt 3 (0 ms) weiter — aber nur
diesen einen, nicht etwa den aus Schritt 4. Die Rechnung startet jedes Mal neu von den gespeicherten
Bezügen, nie von der zuletzt per TSF gesetzten Uhrzeit. Und der Median sorgt dafür, dass ein einzelner
abgeleiteter Bezug das Ergebnis nicht verschiebt. Was wächst, ist nur die Restabweichung mal Zeit seit
dem Bezug.

Zum Vergleich **ohne Gangkorrektur**: A +778, B −518, C +140, D +1 296, E −702 ms → Median +140 ms,
größte Abweichung ±1 156 ms.

### Schritt 5 — Di 12:00, Internet wieder da → Snapshot

1. **Genauigkeitstest** (nur Generation 0: A, B, D; Bezüge 23 h alt, mit Gangkorrektur):
   A +33 ms, B −25 ms, D −41 ms → „max. Abweichung ±41 ms nach 23 h (3 APs)“.
2. **Gang nachmessen** (Messpunkt Mo 13:00, 23 h ≥ 2 h): A misst +12,4 → neu (12 + 12,4)/2 = **+12,2**;
   B −8,3 → **−8,15**; D +19,5 → **+19,75**. C und E haben keinen gültigen Messpunkt → Gang bleibt
   (C +3, E unbekannt), Messpunkt startet jetzt.
3. **Bezüge erneuern:** alle fünf wieder Generation 0, Zeit 12:00.

| AP | Generation | Gang | Messpunkt |
|---|---|---|---|
| A | 0 | +12,2 ppm | Di 12:00 |
| B | 0 | −8,15 ppm | Di 12:00 |
| C | 0 | +3 ppm | Di 12:00 |
| D | 0 | +19,75 ppm | Di 12:00 |
| E | 0 | unbekannt | Di 12:00 — wird beim nächsten Snapshot (≥ 2 h) gemessen |

Damit ist alles wieder auf NTP verankert; eine neue Ausfallphase beginnt mit frischen Bezügen.

---

## 6. Fehlerbetrachtung

| Fehlerquelle | Größe | Wird sie größer, je öfter TSF rechnet? |
|---|---|---|
| Gang der AP-Quarze (ohne Korrektur) | 5–25 ppm → 0,4–2 s pro Tag | nein, wächst nur mit der Zeit seit dem Bezug |
| Restabweichung nach Gangkorrektur (Temperatur) | typisch < 1 ppm → < 90 ms pro Tag | nein, ebenso |
| Neu basierte Bezüge | Rechenfehler der Berechnung, die sie erzeugt hat | nein: jede Berechnung rechnet absolut, der Median dämpft |
| Snapshot mit ungenauer Uhrzeit | beliebig | — verhindert (nur bis 10 min nach NTP) |

### 6.1 Rolle der eigenen Uhr der Lampe

Für die Gangkorrektur spielt es **keine Rolle**, wie schnell oder langsam die eigene Uhr läuft:

- **Gangmessung:** Beide Endpunkte sind NTP-Zeiten (jeder Snapshot folgt direkt auf einen
  NTP-Abgleich). Was die eigene Uhr dazwischen tut — auch 1,5 % RC-Drift im Deep Sleep — geht nicht ein.
- **Erfassung und Fallback:** Die eigene Uhr überbrückt nur die Zeit vom Empfang eines Beacons bis
  „jetzt“ (höchstens 2 s). Wach läuft sie über den Hauptquarz (40 MHz, ≈ ±20 ppm) → < 0,1 ms.
- **Einzige Stelle, an der sie zählt:** ein Snapshot *nicht* direkt nach NTP. Wach bleiben in 10 min
  höchstens ≈ 12 ms Fehler (deshalb die 10-min-Grenze); nach einem Deep Sleep wären es Sekunden
  (deshalb „in der aktuellen Wachphase“).

### 6.2 Simulation

Eine Simulation (8 APs, 7 Tage ohne NTP, alle 5 h eine Berechnung, zufällige Neustarts) ergab ohne
Gangkorrektur ≈ 2,6 s Fehler nach 7 Tagen, mit Gangkorrektur ≈ 1,1 s. Der Fehler wächst mit der Zeit
seit dem letzten NTP, nicht mit der Zahl der Berechnungen. Die Rechenfunktionen `estimateUnixUs()` und
`updateDrift()` wurden zusätzlich auf dem PC mit dem Original-Code getestet: ein AP mit +12 ppm wird nach
5 h exakt als 12 000 ppb erkannt; 24 h später liegt die TSF-Zeit ohne Korrektur 1 036 ms, mit 0 ms daneben.

---

## 7. Grenzen

- **Nur der aktuelle Funkkanal:** Die Erfassung wechselt den Kanal nicht. Hört die Lampe weniger als
  3 gespeicherte APs, setzt sie keine Uhrzeit.
- **Gang erst nach zwei Snapshots** mit mindestens 2 h Abstand bekannt; vorher rechnet sie ohne Korrektur.
- **Uhrzeit-Auflösung:** NTP selbst schwankt um einige ms; genauer als das kann auch TSF nicht werden.
- **Liste im Flash** (NVS-Schlüssel `tsfData4`): übersteht Neustarts, Deep Sleep und Stromausfall. Bei
  Formatänderungen der Firmware beginnt sie neu.
- **Springende Router-Zähler:** Normalerweise läuft der TSF-Zähler eines Routers frei (nur seine
  Systemuhr gleicht er per NTP ab). Mesh-Repeater oder Treiber können ihn aber verstellen; dann wird der
  Gang mal zu hoch, mal zu niedrig gemessen. Mittelwert und Median dämpfen das nur. Zur Beobachtung
  speichert jeder Bezugspunkt seine **letzten 4 Gangmessungen** (`drift_hist`, im Flash) — sichtbar unter
  Einstellungen → Synchronisation → TSF → „Gangmessungen je Access Point“. Springen die Werte eines APs,
  wäre der nächste Schritt: Gang nur verwenden, wenn die letzten zwei Messungen bis auf ±2 ppm übereinstimmen.

## 7.1 Gangmessungen beobachten

Jede Gangmessung landet (a) im Bezugspunkt im Flash und (b) als Zeile im Ereignisprotokoll (nur RAM, geht
bei Deep Sleep/Neustart verloren):

```
TSF: drift 3a:4f:c1 +12.40ppm 5h avg +12.20
           └ BSSID   └ gemessen └ Dauer └ gemittelt (verwendet)
```

Neustart eines APs: Verlauf bleibt (gleicher Quarz). Ausreißer: Verlauf wird geleert (womöglich anderes Gerät).

---

## 8. Wo im Code

| Aufgabe | Funktion |
|---|---|
| Erfassung starten/stoppen | `startCapture()`, `stopCapture()`, `beaconRxCallback()` |
| Snapshot verarbeiten | `processSnapshot()` → `checkAgainstSystemTime()`, `updateDrift()` |
| Fallback rechnen | `processFallback()` |
| TSF-Zeit eines Bezugs mit Gangkorrektur | `estimateUnixUs()` |
| Auslöser nach NTP-Erfolg / -Fehlschlag | `onNtpSynced()` / `requestTsfFallback()` (aus `wifi_controller.cpp`) |
| Snapshot von Hand erlaubt? | `isTsfSnapshotAllowed()` |
| Status für die Weboberfläche | `getTsfStatusJson()` → `/get_tsf_fallback` |

Protokoll-Meldungen: „TSF: Genauigkeit N APs, max. ±X ms“ (Test), „TSF: Snapshot …“ (neue Bezüge),
„TSF: Zeit gesetzt, N APs (NTP-Bezug | abgeleitet, Stufe n) max. ±X ms“ (Fallback), „TSF: zu wenige APs“,
„TSF: Gang AP …xx:xx:xx gemessen … ppm über … h, verwendet … ppm“ (Gangmessung).
