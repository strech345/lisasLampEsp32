# GPIO-Belegung — LisasLamp (ESP32-C3 Super Mini)

_Stand: November 2026_

---

## Verdrahtungsdiagramm (Draufsicht)

```
                              ┌─── USB-C ───┐
                              │             │
    RDA5807M SDA ─── G5 ──────┤             ├──── 5V  ──────────── [5V Netzteil]
    RDA5807M SCL ─── G6 ──────┤             ├──── GND ──────────── [GND Sammelschiene]
    RDA5807M VCC ─── G7 ──────┤  ESP32-C3   ├──── 3V3 ──────────── [3V3]
   (Onboard LED) ─── G8 ──────┤  Super Mini ├──── G4  ── CLK ─┐
   (Boot-Taster) ─── G9 ──────┤             ├──── G3  ── SW  ─┤── KY-040 Encoder
    WS2812B Data ─── G10 ─────┤  ┌───────┐  ├──── G2  ── DT  ─┘
      (TX0 frei) ─── G20 ─────┤  │ Chip  │  ├──── G1  ──────────── XTAL_32K_N ─┐
      (RX0 frei) ─── G21 ─────┤  └───────┘  ├──── G0  ──────────── XTAL_32K_P ─┴─ 32k Quarz (opt.)
                              │             │
                              └─────────────┘

Stromversorgung der Peripherie:
  RDA5807M VCC/GND    ── über G6 geschaltet (LOW = stromlos)
  WS2812B  VCC        ── 5V direkt
  WS2812B  GND        ── GND
  KY-040   VCC        ── 3V3
  KY-040   GND        ── GND
```

---

## Detailtabelle

| GPIO | Header? | Funktion       | Modul              | Richtung | Anmerkungen                                               |
|-----:|:-------:|----------------|--------------------|:--------:|-----------------------------------------------------------|
|    0 | ✓       | XTAL_32K_P     | RTC-Quarz (opt.)   | —        | Pos. Quarz-Pin; Bootstrap-Pulse beim Aktivieren           |
|    1 | ✓       | XTAL_32K_N     | RTC-Quarz (opt.)   | —        | Neg. Quarz-Pin; kein anderes Gerät anschließen            |
|    2 | ✓       | Encoder DT     | KY-040             | IN       | Drehrichtung; interner Pullup                             |
|    3 | ✓       | Encoder SW     | KY-040             | IN       | Taster; Pulldown im Deep Sleep → GPIO-Wakeup ⚡           |
|    4 | ✓       | Encoder CLK    | KY-040             | IN       | Takt; Pullup im Deep Sleep → GPIO-Wakeup ⚡               |
|    5 | ✓       | RDS I²C SDA    | RDA5807M           | BIDIR    | I²C-Daten; 4,7 kΩ Pullup auf 3V3 empfohlen               |
|    6 | ✓       | RDS I²C SCL    | RDA5807M           | OUT      | I²C-Takt; 4,7 kΩ Pullup auf 3V3 empfohlen                |
|    7 | ✓       | RDS Strom      | RDA5807M           | OUT      | HIGH = Modul an; LOW = 0 mA (vollständig stromlos)        |
|    8 | ✓       | Status-LED     | Onboard (blau)     | OUT      | Invertierte Logik (LOW = an); beeinflusst LED wenn genutzt |
|    9 | ✓       | Boot-Taster    | Onboard            | IN       | Interner Pullup; langer Druck = Factory Reset              |
|   10 | ✓       | LED-Strip Data | WS2812B (5×GRBW)   | OUT      | NeoPixel-Datenleitung; 300–500 Ω Serienwiderstand empf.   |
|   20 | ✓       | TX0 (frei)     | UART0              | OUT      | Aktuell ungenutzt                                         |
|   21 | ✓       | RX0 (frei)     | UART0              | IN       | Aktuell ungenutzt                                         |
| 12–17| —       | Flash SPI      | internes Flash     | —        | Reserviert; niemals als GPIO verwenden                    |
| 18–19| —       | USB D−/D+      | USB-CDC            | BIDIR    | Serial-Monitor (Debug-Builds)                             |

---

## Empfohlene Schutzbeschaltung

| Bauteil          | Maßnahme                                      |
|------------------|-----------------------------------------------|
| WS2812B Data     | 300–500 Ω Serienwiderstand direkt am GPIO7    |
| RDA5807M I²C     | 4,7 kΩ Pullup-Widerstände SDA+SCL → 3V3      |
| RDA5807M Strom   | GPIO6 → NPN-Transistor oder P-Kanal-MOSFET   |
| 32kHz Quarz      | Quarz so nah wie möglich an GPIO0/GPIO1       |

---

## XTAL_32K — Externer 32.768-kHz-Uhrenquarz (optional)

| Taktquelle         | Typischer Drift       |
|--------------------|-----------------------|
| Interner RC-Oszi   | ~1–2 % / Tag          |
| Externer 32k-Quarz | ~20 ppm (~1,7 s/Tag)  |

Die Extension `rtc_crystal` prüft beim Boot per `rtc_clk_cal()` ob der Quarz schwingt
(Zielbereich 32.000–33.000 Hz) und fällt sicher auf den RC-Oszillator zurück wenn
kein Quarz bestückt ist. Der Encoder-CLK wurde deshalb von GPIO1 auf GPIO10 verlegt.
