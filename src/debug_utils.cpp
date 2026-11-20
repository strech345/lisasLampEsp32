// Debug-Ausgaben — Implementierung
#include "debug_utils.h"
#include <Arduino.h>

// DEBUG kommt aus den build_flags in platformio.ini (-DDEBUG)

void serialPrint(const String& text) {
#ifdef DEBUG
    Serial.println(text);
#endif
}

// Startet die serielle Ausgabe (nur mit -DDEBUG); bei USB-CDC kurz auf den Host warten
void debugInit() {
#ifdef DEBUG
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0); // nie blockieren wenn kein USB-Host verbunden
    // Nur bei USB-CDC: warten bis der Host bereit ist (max. 2 Sekunden); bei UART sinnlos
    unsigned long startWait = millis();
    while(!Serial && millis() - startWait < 2000) {
        delay(10);
    }
    if(Serial)
        delay(500); // kurze Pause nur wenn Monitor wirklich verbunden
#endif
    Serial.println("\n\n----------------------------------------");
    Serial.println("   ESP32-C3 Lizas Lampe startet");
    Serial.println("----------------------------------------");
#endif
}
