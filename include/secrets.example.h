#ifndef SECRETS_H
#define SECRETS_H
// Vorlage für die Zugangsdaten der Lampe (eingecheckt).
// Eigene Werte: diese Datei nach include/secrets.h kopieren und dort anpassen — secrets.h steht in
// .gitignore und landet nie im Repository. Fehlt secrets.h, wird automatisch diese Vorlage verwendet.
//
// Die Werte sind STANDARDWERTE: Sie gelten nach der Erstinstallation und nach einem Werksreset.
// Ein bereits eingerichtetes Gerät behält seine gespeicherten Einstellungen (Änderung über die
// Weboberfläche).
//
// Regeln (werden beim Kompilieren geprüft, siehe src/store.cpp):
//   - SSIDs höchstens 32 Zeichen, Hotspot-SSID nicht leer
//   - Passwörter leer oder 8–63 Zeichen, nur ASCII (WPA2 erlaubt keine Umlaute)

// Eigener Hotspot der Lampe
#define SECRET_AP_SSID "Lizas-Lamp"
#define SECRET_AP_PASSWORD "Erdmaennchen"

// Externes WLAN (für NTP-Uhrzeit); leer = keins
#define SECRET_EXT_SSID ""
#define SECRET_EXT_PASSWORD ""

// Fallback-WLAN (optional); leer = keins
#define SECRET_EXT_SSID2 ""
#define SECRET_EXT_PASSWORD2 ""

#endif // SECRETS_H
