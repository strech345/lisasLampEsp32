#ifndef PREFERENCES_UTILS_H
#define PREFERENCES_UTILS_H

#include <Preferences.h>
#include <string.h> // for memcmp

// This header provides generic, type-safe wrapper functions for the Preferences library.
// The `putValue` functions automatically check if the data has changed before writing to flash,
// making them more efficient and reducing flash wear.

// --- SETTERS (PUT) ---

/**
 * @brief Generic template to save a data blob (like a struct or array) to Preferences.
 */
template<typename T>
inline void putValue(Preferences& prefs, const char* key, const T& value) {
    // Check if the key exists and if the existing data is the same as the new data.
    T existingValue;
    if (prefs.getBytes(key, &existingValue, sizeof(T)) != sizeof(T) || memcmp(&existingValue, &value, sizeof(T)) != 0) {
        // If it doesn't exist or is different, write the new data.
        prefs.putBytes(key, &value, sizeof(T));
    }
}

/**
 * @brief Overload for saving an integer.
 */
inline void putValue(Preferences& prefs, const char* key, int32_t value) {
    if (prefs.getInt(key, ~value) != value) { // Use inverted value as an unlikely default for the check
        prefs.putInt(key, value);
    }
}

/**
 * @brief Overload for saving a boolean.
 */
inline void putValue(Preferences& prefs, const char* key, bool value) {
    if (prefs.getBool(key, !value) != value) { // Use inverted value for the check
        prefs.putBool(key, value);
    }
}


// --- GETTERS (GET) ---

/**
 * @brief Generic template to retrieve a data blob (like a struct or array) from Preferences.
 *        NOTE: This returns by value, so it is not efficient for very large structs or arrays.
 */
template<typename T>
inline T getValue(Preferences& prefs, const char* key, const T& defaultValue) {
    T value;
    if (prefs.getBytes(key, &value, sizeof(T)) == sizeof(T)) {
        return value;
    }
    return defaultValue;
}

/**
 * @brief Overload for retrieving an integer.
 */
inline int32_t getValue(Preferences& prefs, const char* key, int32_t defaultValue) {
    return prefs.getInt(key, defaultValue);
}

/**
 * @brief Overload for retrieving a boolean.
 */
inline bool getValue(Preferences& prefs, const char* key, bool defaultValue) {
    return prefs.getBool(key, defaultValue);
}

#endif // PREFERENCES_UTILS_H
