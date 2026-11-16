#ifndef LAMP_STATE_H
#define LAMP_STATE_H
// Lampenzustands-Maschine: bestimmt den aktiven Betriebsmodus (Alarm, Gute Nacht,
// Alert, Sleep …) und steuert darüber die LED-Effekte.

#include "types.h"

// Initialisiert alle Zeiger auf den globalen Zustand (einmalig in setup() aufrufen)
void initLampState(LampState* pLampState, LampState* pLastNormalState, bool* pWarningsEnabled,
                   FullConfig* pAppConfig, WiFiTestTracker* pWifiTracker, uint32_t* pPendingFlash);

// Wendet den konfigurierten Farbmodus auf die LEDs an
void checkAndApplyColorMode(const FullConfig& config);

// Zustandsmaschine: bestimmt anhand aller aktiven Modi welcher LED-Effekt gilt
void checkLampState();

// Wendet den aktuellen Zustand auf die LEDs an (getrennt von checkLampState für bessere Kontrolle)
void updateLed();

// Kurzer Tastendruck: schaltet Lampe ein/aus oder beendet aktive Modi
void onShortPress();

#endif // LAMP_STATE_H
