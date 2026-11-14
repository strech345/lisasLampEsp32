#ifndef LAMP_STATE_H
#define LAMP_STATE_H

#include "types.h"

void initLampState(LampState* pLampState, LampState* pLastNormalState, bool* pWarningsEnabled,
                   FullConfig* pAppConfig, WiFiTestTracker* pWifiTracker, uint32_t* pPendingFlash);
void checkAndApplyColorMode(const FullConfig& config);
void checkLampState();
void updateLed();
void onShortPress();

#endif // LAMP_STATE_H
