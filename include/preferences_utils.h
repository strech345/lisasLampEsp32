#ifndef PREFERENCES_UTILS_H
#define PREFERENCES_UTILS_H

#include <Preferences.h>
#include <string.h> // for memcmp

// Typsichere Wrapper für die Preferences-Bibliothek.
// Schreibt nur wenn sich der Wert geändert hat — schont den Flash-Speicher (begrenzte Schreibzyklen).

// --- Schreiben ---

// Speichert einen beliebigen Datentyp (Struct, Array) als Byte-Blob
template<typename T>
inline void putValue(Preferences& prefs, const char* key, const T& value) {
    T existingValue;
    if (prefs.getBytes(key, &existingValue, sizeof(T)) != sizeof(T) || memcmp(&existingValue, &value, sizeof(T)) != 0) {
        prefs.putBytes(key, &value, sizeof(T));
    }
}

// Speichert einen Integer (nur schreiben wenn geändert)
inline void putValue(Preferences& prefs, const char* key, int32_t value) {
    if (prefs.getInt(key, ~value) != value) {
        prefs.putInt(key, value);
    }
}

// Speichert einen Boolean (nur schreiben wenn geändert)
inline void putValue(Preferences& prefs, const char* key, bool value) {
    if (prefs.getBool(key, !value) != value) {
        prefs.putBool(key, value);
    }
}


// --- Lesen ---

// Liest einen beliebigen Datentyp; gibt defaultValue zurück wenn Schlüssel nicht existiert
template<typename T>
inline T getValue(Preferences& prefs, const char* key, const T& defaultValue) {
    T value;
    if (prefs.getBytes(key, &value, sizeof(T)) == sizeof(T)) {
        return value;
    }
    return defaultValue;
}

// Liest einen Integer
inline int32_t getValue(Preferences& prefs, const char* key, int32_t defaultValue) {
    return prefs.getInt(key, defaultValue);
}

// Liest einen Boolean
inline bool getValue(Preferences& prefs, const char* key, bool defaultValue) {
    return prefs.getBool(key, defaultValue);
}

#endif // PREFERENCES_UTILS_H
