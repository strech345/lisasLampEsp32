#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include "types.h"
#include <stdint.h>

void initSleepManager(bool* pDeepSleepEnabled, LampState* pLampState, FullConfig* pAppConfig);
void checkToGoSleep();
void enterDeepSleep(uint64_t sleepTimeMs);
void enterPowerSaveMode(uint64_t sleepTimeMs);

#endif // SLEEP_MANAGER_H
