// LED-Steuerung — Implementierung
// WS2812FX auf PIN_LED_STRIP (RGBW, Anzahl aus SystemSettings); Status-LED auf PIN_STATUS_LED (invertierte Logik).
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

// Startlänge = Maximum; die tatsächliche Anzahl setzt ledInit()/setLedCount() per setLength()
WS2812FX ws2812fx = WS2812FX(MAX_LED_COUNT, PIN_LED_STRIP, NEO_GRBW + NEO_KHZ800);
static uint8_t s_ledCount = DEFAULT_LED_COUNT; // aktuell angesteuerte Anzahl

// --- Flammen-Effekt (eigener WS2812FX-Modus) ---
// Jedes Pixel flackert unabhängig: zufällige Ziel-Intensität, weich angenähert, gelegentlich ein
// tieferer Einbruch. Die Farbe wird aus der Grundfarbe abgeleitet: Rot sinkt linear mit der
// Intensität, Grün/Blau quadratisch, Weiß kubisch → dunkle Phasen werden röter (Glut), helle
// gelblicher. So passt die Flamme zu jeder Lichtfarbe (Kerzenlicht = Kerze, Blau = Gasflamme).
#define FLAME_FRAME_MS 30 // Bildrate des Effekts
static uint8_t s_flameMode = FX_MODE_STATIC;       // von setCustomMode() vergebene Modus-Nummer
static uint8_t s_flameLevel[MAX_LED_COUNT];        // aktuelle Intensität je Pixel (0–255)
static uint8_t s_flameTarget[MAX_LED_COUNT];       // Ziel-Intensität je Pixel

// Skaliert einen Farbkanal mit der Intensität f (0–255), potenziert (1 = linear, 2 = quadratisch …)
static uint8_t flameChannel(uint8_t c, uint8_t f, uint8_t power) {
    uint32_t v = c;
    for(uint8_t i = 0; i < power; i++)
        v = v * f / 255;
    return (uint8_t)v;
}

// Ein Bild des Flammen-Effekts; Rückgabe = Wartezeit bis zum nächsten Bild (ms)
static uint16_t flameEffect() {
    uint32_t base = ws2812fx.getSegment()->colors[0]; // 0xWWRRGGBB
    uint8_t w = base >> 24, r = base >> 16, g = base >> 8, b = base;
    uint16_t len = ws2812fx.getLength();
    for(uint16_t n = 0; n < len && n < MAX_LED_COUNT; n++) {
        uint8_t cur = s_flameLevel[n], tgt = s_flameTarget[n];
        if(abs((int)cur - (int)tgt) < 6) {
            // Neues Ziel: meist hell (150–255), selten ein Einbruch (70–140) für lebendiges Flackern
            tgt = (ws2812fx.random8() < 25) ? 70 + ws2812fx.random8(70) : 150 + ws2812fx.random8(106);
            s_flameTarget[n] = tgt;
        }
        cur = cur + ((int)tgt - (int)cur) / 4; // weich annähern
        s_flameLevel[n] = cur;
        uint32_t c = ((uint32_t)flameChannel(w, cur, 3) << 24) | ((uint32_t)flameChannel(r, cur, 1) << 16)
            | ((uint32_t)flameChannel(g, cur, 2) << 8) | flameChannel(b, cur, 2);
        ws2812fx.setPixelColor(n, c);
    }
    return FLAME_FRAME_MS;
}

// Interne Hilfsfunktion — setzt den PWM-Helligkeitswert (0–255)
static void applyBrightness(uint8_t brightness) {
    ws2812fx.setBrightness(brightness);
}

// Initialisiert LEDs (ledCount Pixel) mit Startfarbe und startet den Animationsmotor
void ledInit(uint8_t ledCount) {
    statusLedInit();
    ws2812fx.init();
    // Erst alle möglichen Pixel löschen (falls vorher mehr LEDs aktiv waren), dann Länge setzen
    ws2812fx.clear();
    ws2812fx.show();
    s_ledCount = ledCount;
    ws2812fx.setLength(ledCount);
    s_flameMode = ws2812fx.setCustomMode(F("Flamme"), flameEffect);
    for(uint8_t n = 0; n < MAX_LED_COUNT; n++)
        s_flameLevel[n] = s_flameTarget[n] = 200;
    ws2812fx.setSpeed(200);
    ws2812fx.start();
    ws2812fx.setColor(0xFFC896);
}

// Ändert die Anzahl der LEDs sofort (1–MAX_LED_COUNT). Vorher wird der ganze Streifen gelöscht,
// sonst würden wegfallende Pixel mit der letzten Farbe weiterleuchten.
void setLedCount(uint8_t count) {
    if(count < 1 || count > MAX_LED_COUNT || count == s_ledCount)
        return;
    ws2812fx.setLength(MAX_LED_COUNT);
    ws2812fx.clear();
    ws2812fx.show();
    ws2812fx.setLength(count);
    s_ledCount = count;
    ws2812fx.trigger(); // Effekt mit neuer Länge sofort neu zeichnen
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

// Animationsmodus für Farben: 1 = Atmen, 2 = Flamme, sonst statisch. Setzt auch Effekt-Optionen
// zurück (z. B. die Schweif-Länge der Hotspot-Animation).
void setAnimationMode(uint8_t mode) {
    ws2812fx.setOptions(0, NO_OPTIONS);
    if(mode == 1) {
        ws2812fx.setMode(FX_MODE_BREATH);
    } else if(mode == 2) {
        ws2812fx.setMode(s_flameMode);
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

// Hotspot-Warte-Animation: blauer Lichtpunkt läuft von vorn nach hinten, Schweif wird nach hinten
// dunkler (WS2812FX-Komet). Ein Durchlauf über alle Pixel dauert AP_ANIM_CYCLE_MS.
#define AP_ANIM_COLOR 0x000060FF  // angenehmes Blau (0xWWRRGGBB)
#define AP_ANIM_CYCLE_MS 1200     // nicht zu schnell, nicht zu träge
#define AP_ANIM_LEVEL 5           // feste Helligkeitsstufe, damit sie auch bei ausgeschalteter Lampe sichtbar ist
void startApWaitingEffect() {
    setBrightnessLevel(AP_ANIM_LEVEL);
    setLedColor(AP_ANIM_COLOR);
    // Erst alle Pixel blau: der Komet blendet vorhandene Pixel nur langsam aus — sonst blieben sie
    // kurz in der alten Farbe stehen
    for(uint16_t n = 0; n < ws2812fx.getLength(); n++)
        ws2812fx.setPixelColor(n, AP_ANIM_COLOR);
    ws2812fx.show();
    ws2812fx.setMode(FX_MODE_COMET);
    ws2812fx.setSpeed(AP_ANIM_CYCLE_MS);
    ws2812fx.setOptions(0, FADE_MEDIUM); // Schweif über etwa 2–3 Pixel
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

#define STATUS_SLOW_ON_MS 2000   // WLAN aus: lang an ...
#define STATUS_SLOW_OFF_MS 4000  // ... und lang aus

// Status-LED (Bedeutung siehe wifiStatusBlinkCount()). Muss aus loop() aufgerufen werden.
// blinkCount 0: langsam blinken (2 s an, 4 s aus); 1–3: so oft kurz aufblinken, Pause, von vorn.
// Die Position im Muster ergibt sich direkt aus millis() — kein eigener Zustand nötig.
static bool s_statusLedEnabled = true; // Einstellung Debug

// Status-LED an/aus (aus = dauerhaft dunkel)
void setStatusLedEnabled(bool enabled) {
    s_statusLedEnabled = enabled;
    if(!enabled)
        statusLedOff();
}

void statusLedTick(uint8_t blinkCount) {
    static bool ledOn = false;
    if(!s_statusLedEnabled) {
        if(ledOn) {
            ledOn = false;
            statusLedOff();
        }
        return;
    }
    bool on;
    if(blinkCount == 0) {
        on = millis() % (STATUS_SLOW_ON_MS + STATUS_SLOW_OFF_MS) < STATUS_SLOW_ON_MS;
    } else {
        const unsigned long step = STATUS_BLINK_ON_MS + STATUS_BLINK_GAP_MS;
        const unsigned long blinkPart = (unsigned long)blinkCount * step;
        unsigned long pos = millis() % (blinkPart + STATUS_PAUSE_MS);
        on = pos < blinkPart && (pos % step) < STATUS_BLINK_ON_MS;
    }
    if(on != ledOn) { // nur bei Änderung schreiben
        ledOn = on;
        on ? statusLedOn() : statusLedOff();
    }
}
