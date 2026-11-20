// LED-Steuerung — Implementierung
// WS2812FX auf PIN_LED_STRIP (5 RGBW-Pixel); Status-LED auf PIN_STATUS_LED (invertierte Logik).
// Pins siehe include/pins.h.
#include <Arduino.h>
#include <WS2812FX.h>
#include "debug_utils.h"
#include "pins.h"
#include "led.h"

// Speichert den LED-Zustand vor einem Flash-Effekt
struct LedState {
    uint32_t color;
    uint8_t  brightness;
    uint8_t  mode;
    uint16_t speed;
};

static unsigned long s_flashEndMs = 0; // Zeitpunkt wann der Flash-Effekt endet (0 = kein Flash)
static LedState      s_preFlashState;  // Gespeicherter Zustand vor dem Flash

#define LED_COUNT    5

WS2812FX ws2812fx = WS2812FX(LED_COUNT, PIN_LED_STRIP, NEO_GRBW + NEO_KHZ800);

// Interne Hilfsfunktion — setzt den PWM-Helligkeitswert (0–255)
static void applyBrightness(uint8_t brightness) {
    ws2812fx.setBrightness(brightness);
}

// Initialisiert LEDs mit Startfarbe und startet den Animationsmotor
void ledInit() {
    statusLedInit();
    ws2812fx.init();
    ws2812fx.setSpeed(200);
    ws2812fx.start();
    ws2812fx.setColor(0xFFC896);
}

// Muss in der Hauptschleife aufgerufen werden: stellt nach einem Flash den alten Zustand wieder her
void ledUpdate() {
    if(s_flashEndMs > 0 && millis() >= s_flashEndMs) {
        s_flashEndMs = 0;
        ws2812fx.setMode(s_preFlashState.mode);
        ws2812fx.setColor(s_preFlashState.color);
        ws2812fx.setSpeed(s_preFlashState.speed);
        ws2812fx.setBrightness(s_preFlashState.brightness);
        if(s_preFlashState.brightness == 0) ws2812fx.stop();
        else if(!ws2812fx.isRunning()) ws2812fx.start();
    }
    ws2812fx.service();
}

// Blendet kurz alle LEDs in einer Farbe auf und stellt danach den vorherigen Zustand wieder her
void flashLeds(uint32_t color, unsigned long durationMs) {
    s_preFlashState.color      = ws2812fx.getColor();
    s_preFlashState.brightness = ws2812fx.getBrightness();
    s_preFlashState.mode       = ws2812fx.getMode();
    s_preFlashState.speed      = ws2812fx.getSpeed();

    ws2812fx.setMode(FX_MODE_STATIC);
    ws2812fx.setColor(color);
    // Flash auch dann sichtbar machen wenn die Lampe gerade dunkel ist
    uint8_t b = s_preFlashState.brightness >= 64 ? s_preFlashState.brightness : 160;
    applyBrightness(b);
    if(!ws2812fx.isRunning()) ws2812fx.start();

    s_flashEndMs = millis() + durationMs;
}

// Farbe für Stufe 1–6 und für Stufe 7 ("Boost": zusätzliche Farb-Chips für mehr Licht)
static uint32_t s_baseColor = 0;
static uint32_t s_boostColor = 0;
static uint8_t s_level = 0; // aktuelle Helligkeitsstufe 0–7

// Zeigt die zur aktuellen Stufe passende Farbe (Stufe 7 → Boost-Farbe)
static void applyColorForLevel() {
    uint32_t color = (s_level == 7) ? s_boostColor : s_baseColor;
    if(color != ws2812fx.getColor())
        ws2812fx.setColor(color);
}

// Setzt eine Farbe für alle Stufen (Hinweise, Warnungen)
void setLedColor(uint32_t color) {
    setLedColors(color, color);
}

// Setzt Normal- und Boost-Farbe (0xWWRRGGBB); die Boost-Farbe gilt nur auf Stufe 7
void setLedColors(uint32_t baseColor, uint32_t boostColor) {
    s_baseColor = baseColor;
    s_boostColor = boostColor;
    applyColorForLevel();
}

void setAnimationMode(uint8_t mode) {
    if(mode == 1) {
        ws2812fx.setMode(FX_MODE_BREATH);
    } else {
        ws2812fx.setMode(FX_MODE_STATIC);
    }
}

void setAnimationSpeed(uint16_t speed) {
    serialPrint("Setting animation speed to " + String(speed));
    ws2812fx.setSpeed(speed);
}

// Setzt Helligkeit über Stufe 0–7 und zeigt das Ergebnis sofort an.
// 0 = aus (Strip wird explizit mit 0 beschrieben, damit WS2812 nicht im letzten Zustand bleiben),
// 1–7 = an (start falls nötig + trigger + service); Stufe 7 zeigt zusätzlich die Boost-Farbe.
void setBrightnessLevel(uint8_t level) {
    // Stufe → PWM-Wert, geometrisch gestuft (Faktor ~1,53 je Stufe): Das Auge empfindet Helligkeit
    // logarithmisch, so wirkt jeder Schritt gleich groß (linear wären die oberen Stufen kaum spürbar)
    static const uint8_t brightnessMap[8] = {0, 20, 31, 47, 71, 108, 166, 255};
    if(level > 7) level = 7;
    s_level = level;
    applyColorForLevel();
    uint8_t bv = brightnessMap[level];
    serialPrint("Setting brightness level to " + String(level) + " (mapped to " + String(bv) + ")");

    if(level == 0) {
        applyBrightness(0);
        if(ws2812fx.isRunning()) ws2812fx.stop(); // stop() löscht den Strip bereits ...
        ws2812fx.strip_off();                     // ... und hier sicherheitshalber immer: clear() + show()
    } else {
        if(!ws2812fx.isRunning()) ws2812fx.start();
        applyBrightness(bv);
        ws2812fx.trigger();
        ws2812fx.service();
    }
}

// Startet den Regenbogen-Effekt für den Feier-Modus
void startCelebrationEffect() {
    ws2812fx.setMode(FX_MODE_RAINBOW);
    ws2812fx.setSpeed(1000);
    setBrightnessLevel(7);
}

void statusLedInit() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    statusLedOff(); // invertierte Logik: HIGH = aus, sonst leuchtet LED sofort dauerhaft
}

// Status-LED an: LOW = an (invertierte Logik auf dem Board)
void statusLedOn() {
    digitalWrite(PIN_STATUS_LED, LOW);
}

void statusLedOff() {
    digitalWrite(PIN_STATUS_LED, HIGH);
}

// Zeitraster des Status-Blinkmusters (ms)
#define STATUS_BLINK_ON_MS 100   // Dauer eines Aufblinkens
#define STATUS_BLINK_GAP_MS 200  // Abstand zwischen zwei Blinks
#define STATUS_PAUSE_MS 2000     // Pause nach der Blinkfolge

// Blinkt die Status-LED blinkCount-mal kurz auf, macht eine Pause und beginnt von vorn
// (Bedeutung siehe wifiStatusBlinkCount()). Muss aus loop() aufgerufen werden.
// Die Position im Muster ergibt sich direkt aus millis() — kein eigener Zustand nötig.
void statusLedTick(uint8_t blinkCount) {
    static bool ledOn = false;
    const unsigned long step = STATUS_BLINK_ON_MS + STATUS_BLINK_GAP_MS;
    const unsigned long blinkPart = (unsigned long)blinkCount * step;
    unsigned long pos = millis() % (blinkPart + STATUS_PAUSE_MS);
    bool on = pos < blinkPart && (pos % step) < STATUS_BLINK_ON_MS;
    if(on != ledOn) { // nur bei Änderung schreiben
        ledOn = on;
        on ? statusLedOn() : statusLedOff();
    }
}
