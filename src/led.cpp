// LED-Steuerung — Implementierung
// WS2812FX auf GPIO 10 (5 RGBW-Pixel); Status-LED auf GPIO 8 (invertierte Logik).
#include <Arduino.h>
#include <WS2812FX.h>
#include "debug_utils.h"
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

#define LED_PIN      10
#define LED_COUNT    5
#define STATUS_LED_PIN 8

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

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
        // Flash-Zeit abgelaufen → gespeicherten Zustand zurückschreiben
        s_flashEndMs = 0;
        ws2812fx.resetSegments();
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

    ws2812fx.resetSegments();
    ws2812fx.setMode(FX_MODE_STATIC);
    ws2812fx.setColor(color);
    // Flash auch dann sichtbar machen wenn die Lampe gerade dunkel ist
    uint8_t b = s_preFlashState.brightness >= 64 ? s_preFlashState.brightness : 160;
    ws2812fx.setBrightness(b);
    if(!ws2812fx.isRunning()) ws2812fx.start();

    s_flashEndMs = millis() + durationMs;
}

void setBrightness(uint8_t brightness) {
    serialPrint("Setting brightness to " + String(brightness));
    ws2812fx.setBrightness(brightness);
}

// Setzt die Farbe nur wenn sie sich geändert hat (vermeidet unnötige Schreibvorgänge)
void setLedColor(uint32_t color) {
    const uint32_t current = ws2812fx.getColor();
    if(color == current)
        return;
    ws2812fx.setColor(color);
}

void setAnimationMode(uint8_t mode) {
    if(mode == 0) {
        ws2812fx.setMode(FX_MODE_STATIC);
    } else if(mode == 1) {
        ws2812fx.setMode(FX_MODE_BREATH);
    } else {
        ws2812fx.setMode(FX_MODE_STATIC);
    }
}

void setAnimationSpeed(uint16_t speed) {
    serialPrint("Setting animation speed to " + String(speed));
    ws2812fx.setSpeed(speed);
}

// Setzt Helligkeit über eine Stufe 0–7 (0 = aus, 7 = voll)
// Die Stufenwerte sind nichtlinear damit kleine Stufen sichtbar sind
void setBrightnessLevel(uint8_t level) {
    if(level == 0) {
        if(ws2812fx.isRunning()) {
            serialPrint("Stopping WS2812FX (brightness 0)");
            ws2812fx.stop();
        }
    } else {
        if(!ws2812fx.isRunning()) {
            serialPrint("Starting WS2812FX (brightness > 0)");
            ws2812fx.start();
        }
    }
    // Stufe → PWM-Wert: 0→0, 1→32, 2→64, 3→96, 4→128, 5→160, 6→200, 7→255
    static const uint8_t brightnessMap[8] = {0, 32, 64, 96, 128, 160, 200, 255};
    if(level > 7)
        level = 7;
    serialPrint("Setting brightness level to " + String(level) + " (mapped to " + String(brightnessMap[level]) + ")");
    setBrightness(brightnessMap[level]);
}

// Startet den Regenbogen-Effekt für den Feier-Modus
void startCelebrationEffect() {
    ws2812fx.setMode(FX_MODE_RAINBOW);
    ws2812fx.setSpeed(1000);
    setBrightnessLevel(7);
}

// Lässt WARNING_PIXEL in blinkColor blinken; restliche Pixel bleiben statisch in der aktuellen Farbe
void startSinglePixelBlink(uint32_t blinkColor) {
    uint32_t normalColor = ws2812fx.getColor();
    ws2812fx.setSegment(0, WARNING_PIXEL, WARNING_PIXEL, FX_MODE_BLINK, blinkColor, 500);
    if (WARNING_PIXEL + 1 <= LED_COUNT - 1) {
        ws2812fx.setSegment(1, WARNING_PIXEL + 1, LED_COUNT - 1, FX_MODE_STATIC, normalColor, 1000);
    }
}

// Beendet den Einzelpixel-Blink und setzt alle Pixel auf ein einheitliches Segment zurück
void stopSinglePixelBlink() {
    ws2812fx.resetSegments();
}

void statusLedInit() {
    pinMode(STATUS_LED_PIN, OUTPUT);
}

// Status-LED an: LOW = an (invertierte Logik auf dem Board)
void statusLedOn() {
    digitalWrite(STATUS_LED_PIN, LOW);
}

void statusLedOff() {
    digitalWrite(STATUS_LED_PIN, HIGH);
}

// Blinkt die Status-LED wenn apActive=true (500 ms an/aus), sonst LED aus.
// Muss regelmäßig aus loop() aufgerufen werden.
void statusLedTick(bool apActive) {
    static unsigned long lastToggleMs = 0;
    static bool ledState = false;
    if(!apActive) {
        if(ledState) { statusLedOff(); ledState = false; }
        return;
    }
    unsigned long now = millis();
    if(now - lastToggleMs >= 500) {
        lastToggleMs = now;
        ledState = !ledState;
        ledState ? statusLedOn() : statusLedOff();
    }
}
