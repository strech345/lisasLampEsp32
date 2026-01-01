#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "types.h" // For RGB and Alarm structs, and MAX_ALARMS

/**
 * @brief Creates a JSON string containing override RGB color and alarms data.
 *
 * @param overrides_rgb A const reference to the RGB struct for override color.
 * @param alarms_ptr A const pointer to the array of Alarm structs.
 * @return A String containing the JSON representation of the configuration.
 */
String createConfigJson(const FullConfig& config);

/**
 * @brief Parses a JSON string to extract override RGB color and alarms data.
 *
 * @param json_string The JSON string to parse.
 * @param overrides_rgb A reference to the RGB struct to populate with override color.
 * @param alarms_ptr A pointer to the array of Alarm structs to populate.
 * @return True if parsing is successful, false otherwise.
 */
bool parseConfigJson(const String& jsonString, FullConfig& config);

String createSystemConfigJson(const SystemSettings& systemSettings);
bool parseSystemConfigJson(const String& jsonString, SystemSettings& systemSettings);

String createWiFiStatusJson(const WiFiStatus& status);

#endif // JSON_UTILS_H
