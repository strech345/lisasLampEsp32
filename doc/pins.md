# GPIO-Belegung — LisasLamp (ESP32-C3 Super Mini)

_Stand: November 2026_

---

## Board-Diagramm

Der ESP32-C3 Super Mini hat zwei Pinreihen mit je 8 Pins (USB-Anschluss oben).
GPIO8 und GPIO9 sind **Onboard-Komponenten** (LED / Boot-Taster) ohne Header-Pin.

```
         USB
    ┌────┤├────┐
5V ─┤          ├─ GND
GND─┤          ├─ GPIO21  (TX)
3V3─┤          ├─ GPIO20  (RX)
 G4─┤          ├─ GPIO10  → Encoder CLK
 G5─┤          ├─ GPIO3   → Encoder SW  ⚡Wakeup
 G6─┤          ├─ GPIO2   → Encoder DT
 G7─┤          ├─ GPIO1   → XTAL_32K_N (RTC-Quarz opt.)
 G8─┤  [LED]   ├─ GPIO0   → XTAL_32K_P (RTC-Quarz opt.)
    └──────────┘
       [BTN]
       GPIO9 (Boot)

Legende:
  G4 = GPIO4   G5 = GPIO5   G6 = GPIO6   G7 = GPIO7   G8 = GPIO8
  [LED] = Onboard blaue LED (GPIO8, kein Header-Pin)
  [BTN] = Onboard Boot-Taster (GPIO9, kein Header-Pin)
  ⚡    = GPIO-Wakeup aus Deep Sleep
```

---

## Detailtabelle

| GPIO | Header? | Funktion        | Modul              | Richtung | Anmerkungen                                                  |
|-----:|:-------:|-----------------|--------------------|:--------:|--------------------------------------------------------------|
|    0 | ✓       | XTAL_32K_P      | RTC-Quarz (opt.)   | —        | Pos. Seite des 32.768-kHz-Quarzes; ungenutzt wenn kein Quarz |
|    1 | ✓       | XTAL_32K_N      | RTC-Quarz (opt.)   | —        | Neg. Seite des 32.768-kHz-Quarzes; ungenutzt wenn kein Quarz |
|    2 | ✓       | Encoder DT      | KY-040             | IN       | Drehrichtungssignal; interner Pullup                         |
|    3 | ✓       | Encoder SW      | KY-040             | IN       | Taster; Pulldown im Deep Sleep für GPIO-Wakeup               |
|    4 | ✓       | RDS I²C SDA     | RDA5807M           | BIDIR    | I²C-Datenleitung zum FM-Chip                                 |
|    5 | ✓       | RDS I²C SCL     | RDA5807M           | OUT      | I²C-Taktleitung zum FM-Chip                                  |
|    6 | ✓       | RDS Strom       | RDA5807M           | OUT      | HIGH = Modul an; LOW = stromlos (0 mA)                       |
|    7 | ✓       | LED-Strip       | WS2812FX           | OUT      | Datenleitung zu 5× WS2812B (GRBW)                           |
|    8 | —       | Status-LED      | Onboard (blau)     | OUT      | Kein Header-Pin; an = Boot/WiFi aktiv; invertierte Logik     |
|    9 | —       | Boot-Taster     | Onboard            | IN       | Kein Header-Pin; Pullup; langer Druck = Werksreset           |
|   10 | ✓       | Encoder CLK     | KY-040             | IN       | Taktsignal; Pullup im Deep Sleep für GPIO-Wakeup             |
| 11   | —       | —               | —                  | —        | Nicht am Header; intern nicht verwendet                      |
| 12–17| —       | Flash (SPI)     | internes Flash     | —        | Für SPI-Flash reserviert; niemals verwenden                  |
| 18–19| —       | USB (CDC)       | USB-CDC            | BIDIR    | USB D−/D+; Serial-Monitor bei Debug-Builds                   |
| 20   | ✓       | TX0 (frei)      | UART0              | OUT      | Header-Pin; aktuell ungenutzt                                |
| 21   | ✓       | RX0 (frei)      | UART0              | IN       | Header-Pin; aktuell ungenutzt                                |

---

## Freie Header-Pins

| GPIO | Anmerkung |
|-----:|-----------|
|   20 | TX0 — für UART-Debug oder zukünftige Peripherie nutzbar |
|   21 | RX0 — für UART-Debug oder zukünftige Peripherie nutzbar |

---

## XTAL_32K — Externer 32.768-kHz-Uhrenquarz (optional)

GPIO0 (`XTAL_32K_P`) und GPIO1 (`XTAL_32K_N`) sind die Anschlüsse für einen externen
Uhrenquarz. Dieser verbessert die RTC-Genauigkeit erheblich:

| Taktquelle        | Typischer Drift  |
|-------------------|-----------------|
| Interner RC-Oszi  | ~1–2 % / Tag    |
| Externer 32k-Quarz| ~20 ppm (~1,7 s/Tag) |

Die Extension `rtc_crystal` aktiviert den Quarz beim Boot, prüft per Kalibrierung
ob er schwingt (Zielbereich 32.000–33.000 Hz), und fällt sicher auf den internen
RC-Oszillator zurück wenn kein Quarz bestückt ist.

**Hinweis:** Der ESP32-C3 sendet beim Aktivieren kurze Bootstrap-Pulse auf GPIO0/1.
Deshalb wurden GPIO0 und GPIO1 für keine andere Funktion genutzt und der
Encoder-CLK von GPIO1 auf **GPIO10** verlegt.
