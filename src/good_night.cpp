// Gute-Nacht-Modus — Implementierung
#include <stdint.h>

#include "good_night.h"
#include <Arduino.h>
#include <debug_utils.h>

static bool goodNightModeActive = false;
static unsigned long goodNightStartTimeMs = 0;

// Startet den Gute-Nacht-Modus und merkt den Startzeitpunkt
void activateGoodNightMode() {
    goodNightModeActive = true;
    goodNightStartTimeMs = millis();
    serialPrint("Good night mode started");
}

void stopGoodNightMode() {
    goodNightModeActive = false;
    goodNightStartTimeMs = 0;
}

bool isGoodNightModeActive() {
    return goodNightModeActive;
}

// Berechnet die interpolierte Helligkeit: von startBrightness gleichmäßig auf 0 über die konfigurierte Dauer
byte getGoodNightBrightness(byte startBrightness, uint16_t durationMinutes) {
    if(!goodNightModeActive) {
        return 0;
    }
    unsigned long durationMs = (unsigned long)durationMinutes * 60 * 1000;
    unsigned long elapsed = millis() - goodNightStartTimeMs;
    if(elapsed >= durationMs) {
        return 0; // Dauer überschritten → komplett aus
    }
    float progress = (float)elapsed / (float)durationMs;
    byte result = startBrightness - (byte)(progress * startBrightness);
    return result;
}

// Prüft alle 10 Sekunden ob die konfigurierte Dauer abgelaufen ist und stoppt den Modus dann
void checkGoodNightMode(uint16_t durationMinutes) {
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    if(currentTime - lastCheck < 10000) {
        return;
    }
    lastCheck = currentTime;

    unsigned long durationMs = (unsigned long)durationMinutes * 60 * 1000;
    if(goodNightModeActive && (millis() - goodNightStartTimeMs > durationMs)) {
        stopGoodNightMode();
    }
}
