// Debug-Ausgaben — Implementierung
// Serielle Logs gehen wahlweise über UART0 (RX/TX, z. B. FTDI-Adapter) oder über den USB-Anschluss
// (s_usbSerial); beides wird beim Start geöffnet, umgeschaltet wird zur Laufzeit (Einstellung Debug).
#include "debug_utils.h"
#include <Arduino.h>
#include <HWCDC.h>

// DEBUG kommt aus den build_flags in platformio.ini (-DDEBUG)

// Eigene Instanz der USB-Schnittstelle: die Arduino-Core legt s_usbSerial nur bei
// ARDUINO_USB_CDC_ON_BOOT=1 an, hier ist Serial aber UART0
static HWCDC s_usbSerial;

static bool s_serialEnabled = true; // serielle Logs an/aus
static bool s_useUsb = false;       // true = USB statt UART

// Gibt text auf der gewählten seriellen Schnittstelle aus (wenn Logs aktiviert sind)
void serialPrint(const String& text) {
#ifdef DEBUG
    if(!s_serialEnabled)
        return;
    if(s_useUsb)
        s_usbSerial.println(text);
    else
        Serial.println(text);
#endif
}

// Schaltet serielle Logs an/aus und wählt die Schnittstelle (USB oder UART)
void setSerialLogging(bool enabled, bool useUsb) {
    s_serialEnabled = enabled;
    s_useUsb = useUsb;
}

// Startet beide seriellen Schnittstellen (nur mit -DDEBUG); USB blockiert nie, wenn kein Host da ist
void debugInit() {
#ifdef DEBUG
    Serial.begin(115200);
    s_usbSerial.setTxTimeoutMs(0); // nie blockieren wenn kein USB-Host verbunden
    s_usbSerial.begin();
    Serial.println("\n\n----------------------------------------");
    Serial.println("   ESP32-C3 Lampe startet");
    Serial.println("----------------------------------------");
#endif
}
