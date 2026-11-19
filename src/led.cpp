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

// Interne Hilfsfunktion — setzt Helligkeit mit Log
static void applyBrightness(uint8_t brightness) {
    serialPrint("Setting brightness to " + String(brightness));
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

void setLedColor(uint32_t color) {
    if(color == ws2812fx.getColor()) return;
    ws2812fx.setColor(color);
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
// 1–7 = an (start falls nötig + trigger + service).
void setBrightnessLevel(uint8_t level) {
    // Stufe → PWM-Wert: 0→0, 1→32, 2→64, 3→96, 4→128, 5→160, 6→200, 7→255
    static const uint8_t brightnessMap[8] = {0, 32, 64, 96, 128, 160, 200, 255};
    if(level > 7) level = 7;
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

// Blinkt die Status-LED immer: langsam (2000ms an / 4000ms aus) wenn kein WLAN aktiv,
// schnell (200ms an / 300ms aus) wenn WLAN aktiv. Muss aus loop() aufgerufen werden.
void statusLedTick(bool wifiActive) {
    static unsigned long lastToggleMs = 0;
    static bool ledState = false;
    unsigned long onTime  = wifiActive ? 200UL : 2000UL;
    unsigned long offTime = wifiActive ? 300UL : 4000UL;
    unsigned long interval = ledState ? onTime : offTime;
    unsigned long now = millis();
    if(now - lastToggleMs >= interval) {
        lastToggleMs = now;
        ledState = !ledState;
        ledState ? statusLedOn() : statusLedOff();
    }
}
