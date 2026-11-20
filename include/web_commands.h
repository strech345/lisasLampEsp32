#ifndef WEB_COMMANDS_H
#define WEB_COMMANDS_H
// Web-Aufträge: Webserver-Handler legen Zustandsänderungen als Auftrag ab, loop() führt sie aus.
// Details: doc/architektur.md (Tasks und Thread-Sicherheit).

#include "types.h"

// Merkt sich die Zeiger auf den globalen Zustand (einmal in setup() aufrufen)
void initWebCommands(FullConfig* appConfig, SystemSettings* systemSettings);

// Legt einen Auftrag ab (aus dem async_tcp-Task); kopiert die Daten, führt nichts aus.
// Passt zu GenericStateUpdateCallback und wird an initRouteHandlers() übergeben.
void queueWebCommand(StateChangeType type, void* data);

// Führt abgelegte Aufträge und verzögerte Aktionen (WLAN-Neustart, Neustart) aus — nur aus loop()
void processWebCommands();

#endif // WEB_COMMANDS_H
