#ifndef STATE_H
#define STATE_H

#include "types.h"
#include <Arduino.h>
#include <AiEsp32RotaryEncoder.h>

extern AiEsp32RotaryEncoder rotaryEncoder;
extern int mode;
extern RANGE modeRanges[];
extern int NUMBER_OF_MODES;
extern unsigned long lastInteractionMs;
extern volatile bool encoderChanged;
extern volatile bool buttonChanged;

extern int stateMode;
extern unsigned long stateModeStartTime;

extern RGB overrideColor;
extern bool colorOverrideActive;

void restoreState();
void saveState();
void setMode(int _mode);
int validateValue(int mode, int value, int def);

void setColorOverride(const RGB& color);
void setColorOverrideActive(bool active);

#endif // STATE_H
