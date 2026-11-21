#ifndef BUTTON_H
#define BUTTON_H
// Drehencoder- und Tastersteuerung: entprellt KY-040-Encoder und Boot-Button,
// unterscheidet kurzen und langen Druck und leitet Ereignisse an Callbacks weiter.

#include <functional>
#include <Arduino.h>
#include "types.h"

// Ereignistypen des Drehencoders
enum class RotaryEncoderEventType {
    ShortClick,      // Kurzer Tastendruck
    LongClick,       // Langer Tastendruck (>1s)
    Rotate,          // Drehbewegung; value = +/- Schritte
    ShortBootClick,  // Kurzer Druck beim Boot (Wakeup aus Deep Sleep)
    LongBootClick,   // Langer Druck beim Boot
};

// Callback-Typ: wird bei jedem Encoder-Ereignis aufgerufen
using RotaryEncoderCallback =
    std::function<void(RotaryEncoderEventType, int16_t value)>;

// Initialisiert den Encoder mit Startwert und registriert den Callback
void initRotaryEncoder(byte value, RotaryEncoderCallback callback);
void rotary_loop(); // Muss in loop() aufgerufen werden

// Initialisiert die Button-Callbacks mit Zeigern auf den globalen Lampenzustand
void initButtonCallbacks(FullConfig* pAppConfig, LampState* pLampState, WiFiTestTracker* pWifiTracker);
void myRotaryEncoderCallback(RotaryEncoderEventType eventType, int16_t value);

#endif // BUTTON_H
