#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include "types.h"
#include <stdint.h>

// Initialisiert den Sleep-Manager mit Zeigern auf den globalen Zustand
void initSleepManager(bool* pDeepSleepEnabled, LampState* pLampState, FullConfig* pAppConfig);

// Prüft ob Deep Sleep eingeleitet werden soll (Lampe aus + kein Alarm bald)
void checkToGoSleep();

// Versetzt den ESP32 für sleepTimeMs Millisekunden in Deep Sleep
void enterDeepSleep(uint64_t sleepTimeMs);

#endif // SLEEP_MANAGER_H
