#ifndef PINS_H
#define PINS_H
// Zentrale GPIO-Belegung der Lampe (ESP32-C3 Super Mini).
// Alle Module verwenden ausschließlich diese Konstanten — Pins nie direkt als Zahl im Code schreiben.
// Jeder Wert ist per #ifndef abgesichert und lässt sich für eine andere Hardware-Variante über
// build_flags in platformio.ini überschreiben, z. B.:  build_flags = -DPIN_LED_STRIP=5
// Verdrahtung und Hinweise: doc/pins.md

// --- KY-040 Drehencoder ---
// Hinweis: SW und CLK müssen RTC-fähige GPIOs (0–5) sein, sonst funktioniert der Deep-Sleep-Wakeup
// nicht (SW weckt per Tastendruck, CLK per Drehung).
#ifndef PIN_ENCODER_DT
#define PIN_ENCODER_DT 2 // Drehrichtung
#endif
#ifndef PIN_ENCODER_SW
#define PIN_ENCODER_SW 3 // Taster (gedrückt = LOW), Deep-Sleep-Wakeup
#endif
#ifndef PIN_ENCODER_CLK
#define PIN_ENCODER_CLK 4 // Takt, Deep-Sleep-Wakeup bei Drehung
#endif

// --- Onboard ---
#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED 8 // blaue Onboard-LED, invertierte Logik (LOW = an)
#endif
#ifndef PIN_BOOT_BUTTON
#define PIN_BOOT_BUTTON 9 // Boot-Taster (gedrückt = LOW), langer Druck = Werksreset
#endif

// --- WS2812B LED-Streifen ---
#ifndef PIN_LED_STRIP
#define PIN_LED_STRIP 10 // Datenleitung
#endif

// --- RDA5807M FM-Empfänger (RDS-Uhrzeit, optional) ---
#ifndef PIN_RDS_SDA
#define PIN_RDS_SDA 5 // I²C SDA
#endif
#ifndef PIN_RDS_SCL
#define PIN_RDS_SCL 6 // I²C SCL
#endif
#ifndef PIN_RDS_POWER
#define PIN_RDS_POWER 7 // Modul-Stromversorgung (HIGH = an)
#endif

// GPIO0/GPIO1 sind für den optionalen 32-kHz-Quarz reserviert (fest im Chip, nicht konfigurierbar).

#endif // PINS_H
