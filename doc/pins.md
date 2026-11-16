# ESP32-C3 GPIO-Belegung — LisasLamp

_Stand: November 2026_

---

## Pin-Übersicht

```
                    ┌──────────────────────┐
                    │       ESP32-C3       │
          XTAL_32K_P┤ GPIO  0  [opt.]      │
          XTAL_32K_N┤ GPIO  1  [opt.]      │
          Encoder DT┤ GPIO  2              │
          Encoder SW┤ GPIO  3              │
         RDS I²C SDA┤ GPIO  4              │
         RDS I²C SCL┤ GPIO  5              │
          RDS Strom ┤ GPIO  6              │
          LED-Strip ┤ GPIO  7              │
          Status-LED┤ GPIO  8              │
          Boot-Taster┤ GPIO  9              │
         Encoder CLK┤ GPIO 10              │
              [frei]┤ GPIO 11              │
         Flash (SPI)┤ GPIO 12–17           │
           USB (CDC)┤ GPIO 18–19           │
                    └──────────────────────┘
```

---

## Detailtabelle

| GPIO | Funktion        | Modul              | Richtung | Anmerkungen                                           |
|-----:|-----------------|--------------------|---------:|-------------------------------------------------------|
|    0 | XTAL_32K_P      | RTC-Quarz (opt.)   | —        | Positive Seite des 32.768-kHz-Quarzes; frei wenn kein Quarz bestückt |
|    1 | XTAL_32K_N      | RTC-Quarz (opt.)   | —        | Negative Seite des 32.768-kHz-Quarzes; frei wenn kein Quarz bestückt |
|    2 | Encoder DT      | KY-040             | IN       | Drehrichtungssignal; Pullup intern                    |
|    3 | Encoder SW      | KY-040             | IN       | Taster-Signal; Pulldown im Deep-Sleep für GPIO-Wakeup |
|    4 | RDS I²C SDA     | RDA5807M           | BIDIR    | I²C-Datenleitung zum FM-Chip                          |
|    5 | RDS I²C SCL     | RDA5807M           | OUT      | I²C-Taktleitung zum FM-Chip                           |
|    6 | RDS Strom       | RDA5807M           | OUT      | HIGH = Modul eingeschaltet; LOW = ausgeschaltet       |
|    7 | LED-Strip       | WS2812FX / NeoPixel | OUT     | Datenleitung zum WS2812B-LED-Streifen                 |
|    8 | Status-LED      | Onboard-LED        | OUT      | Blaue Status-LED; an während boot/wifi aktiv          |
|    9 | Boot-Taster     | Boot-Button        | IN       | Interner Boot-Button; Pullup; langer Druck = Werksreset |
|   10 | Encoder CLK     | KY-040             | IN       | Taktsignal des Drehencoders; Pullup im Deep-Sleep für GPIO-Wakeup |
|   11 | —               | frei               | —        | Kein Peripheriegerät angeschlossen                    |
| 12–17 | Flash (SPI)    | internes Flash     | —        | Für internes SPI-Flash reserviert; nicht verwenden    |
| 18–19 | USB (CDC)      | USB-CDC            | BIDIR    | USB-Kommunikation (D−/D+); Serial-Monitor             |

---

## XTAL_32K — Externer Uhrenquarz (optional)

GPIO0 und GPIO1 sind beim ESP32-C3 als `XTAL_32K_P` (positive) und `XTAL_32K_N` (negative) Anschlüsse eines
externen **32.768-kHz-Uhrenquarzes** nutzbar. Dieser Quarz verbessert die Genauigkeit des RTC-Schlaftime
erheblich: Der interne RC-Oszillator driftet um ~1–2 % pro Tag, ein externer Quarz typischerweise nur
~20 ppm (~1,7 s/Tag).

Die Extension `src/rtc_crystal.cpp` aktiviert den Quarz beim Boot wenn `rtcCrystalEnabled = true` gesetzt
ist, prüft per Kalibrierungsmessung ob er tatsächlich schwingt (Erwartungsbereich 32.000–33.000 Hz), und
fällt sicher auf den internen RC-Oszillator zurück wenn kein Quarz bestückt ist.

**Warum GPIO0/1 frei halten?**
Solange kein Quarz bestückt ist, können GPIO0 und GPIO1 theoretisch als normale GPIOs genutzt werden.
Da der ESP32-C3 beim Anlegen der XTAL_32K-Funktion kurze Bootstrap-Pulse sendet, sollten dort keine
Peripheriegeräte angeschlossen werden, die auf kurze Pulse empfindlich reagieren.
Der Rotary-Encoder-CLK wurde deshalb von GPIO1 auf **GPIO10** verlegt.
