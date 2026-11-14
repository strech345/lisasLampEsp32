# Bedienelemente — LisasLamp ESP32-C3

Das Gerät hat zwei physische Eingaben:

- **Drehencoder (KY-040)** — Drehen + Druckknopf (SW-Pin)
- **Boot-Button** — GPIO 9, separater Taster

---

## Drehencoder — Drehen

| Richtung | Aktion |
|----------|--------|
| Rechts (clockwise) | Helligkeit erhöhen (Stufe 0–7) |
| Links (counter-clockwise) | Helligkeit verringern (Stufe 0–7) |

- Stufe 0 = Lampe aus
- Stufe 7 = maximale Helligkeit
- Wert wird sofort gespeichert (debounced, 5 s)
- Falls die Lampe im Good-Night-Modus ist: Modus wird beendet, Lampe zurück auf Normal

---

## Drehencoder — Druckknopf (kurzer Druck, >50 ms)

Das Verhalten hängt vom aktuellen Lampenzustand ab:

| Aktueller Zustand | Aktion |
|-------------------|--------|
| Alert aktiv (Fehler / Warnung / OK-Signal) | Alert löschen, zurück zum vorherigen Zustand |
| Sleep (Lampe aus) | Lampe einschalten (→ Default) |
| Alarm läuft | Alarm stoppen (→ Default) |
| Good-Night-Modus läuft | Good-Night stoppen (→ Default) |
| Celebration läuft | Celebration stoppen (→ Default) |
| **Normalbetrieb (Default)** | **Good-Night-Modus starten** |

---

## Drehencoder — Druckknopf (langer Druck, >1000 ms)

| Aktion |
|--------|
| WiFi einschalten (falls aus) |
| Orange-Warnsignal für 5 s (visuelles Feedback) |

Typischer Anwendungsfall: WiFi nach automatischem Abschalten manuell reaktivieren, um die Weboberfläche zu erreichen.

---

## Boot-Button (kurzer Druck, >50 ms)

Aktuell ohne Funktion (Callback leer, reserviert für zukünftige Nutzung).

---

## Boot-Button (langer Druck, >1000 ms)

| Aktion |
|--------|
| **Werkszustand wiederherstellen** |
| FullConfig auf Defaults zurücksetzen |
| SystemSettings auf Defaults zurücksetzen |
| ESP32 startet neu |

⚠️ Alle gespeicherten Einstellungen (Alarme, Farbe, WLAN-Zugangsdaten) gehen verloren.

---

## WiFi-Verhalten (automatisch, kein Tastendruck)

| Bedingung | Aktion |
|-----------|--------|
| Kein Client verbunden, 60 s vergangen | WiFi abschalten |
| WiFi aus, 5 h vergangen | WiFi automatisch wieder einschalten |
| STA (externes WLAN) verloren, 3 Fehlversuche | Fallback auf AP-only |
| AP-only, 5 min vergangen, externer SSID konfiguriert | Erneuter STA-Verbindungsversuch |

---

## Pin-Belegung Eingaben

| Signal | GPIO |
|--------|------|
| Encoder CLK (A) | GPIO 1 |
| Encoder DT (B) | GPIO 2 |
| Encoder SW (Druckknopf) | GPIO 3 |
| Boot-Button | GPIO 9 |
