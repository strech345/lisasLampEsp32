#ifndef LAMP_STATE_H
#define LAMP_STATE_H
// Lampenzustands-Maschine: bestimmt den aktiven Betriebsmodus (Alarm, Gute Nacht,
// Alert, Sleep …) und steuert darüber die LED-Effekte.

#include "types.h"

// Initialisiert alle Zeiger auf den globalen Zustand (einmalig in setup() aufrufen)
void initLampState(LampState* pLampState, bool* pWarningsEnabled, FullConfig* pAppConfig,
                   WiFiTestTracker* pWifiTracker);

// Wendet den konfigurierten Farbmodus auf die LEDs an
void checkAndApplyColorMode(const FullConfig& config);

// Zustandsmaschine: bestimmt anhand aller aktiven Modi welcher LED-Effekt gilt
void checkLampState();

// Wendet den aktuellen Zustand auf die LEDs an (getrennt von checkLampState für bessere Kontrolle)
void updateLed();

// Kurzer Tastendruck: schaltet Lampe ein/aus oder beendet aktive Modi
void onShortPress();

// Bricht den wartenden Hotspot bei einer Bedienung (Drücken/Drehen) ab: Animation und Hotspot aus;
// sonst ohne Wirkung
bool dismissApAnimation(); // true wenn abgebrochen wurde

// Wechselt das Szene um delta Schritte (zyklisch); Drehen bei gedrückter Taste
void switchLightSet(int delta);

#endif // LAMP_STATE_H
