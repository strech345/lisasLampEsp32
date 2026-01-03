#include "json_utils.h"

String createConfigJson(const FullConfig& config) {
    StaticJsonDocument<2048> doc; // Adjust size if more data or complex structures are added

    // Add RGB override color
    JsonObject colorObj = doc.createNestedObject("override_color");
    colorObj["r"] = config.color.r;
    colorObj["g"] = config.color.g;
    colorObj["b"] = config.color.b;

    // Add color mode
    // doc["selectedColorMode"] = 1; // Always active for frontend compatibility
    doc["colorMode"] = config.colorMode;
    doc["animationMode"] = config.animationMode;
    doc["goodNightDuration"] = config.goodNightDuration;
    doc["alarmDuration"] = config.alarmDuration;
    doc["animationSpeed"] = config.animationSpeed;

    // Add alarms array
    JsonArray alarmsArray = doc.createNestedArray("alarms");
    for(int i = 0; i < MAX_ALARMS; ++i) {
        JsonObject alarmObj = alarmsArray.createNestedObject();
        alarmObj["day"] = config.alarms[i].day;
        alarmObj["hour"] = config.alarms[i].hour;
        alarmObj["minute"] = config.alarms[i].minute;
        alarmObj["active"] = config.alarms[i].active;
    }

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    return jsonResponse;
}

bool parseConfigJson(const String& jsonString, FullConfig& config) {
    StaticJsonDocument<2048> doc; // Adjust size if more data or complex structures are added

    DeserializationError error = deserializeJson(doc, jsonString);
    if(error) {
        // Handle deserialization error (e.g., print to serial)
        // For now, just return false. Consider adding a serialPrint if needed
        // and accessible.
        return false;
    }

    // Parse RGB override color
    JsonObject colorObj = doc["override_color"];
    if(!colorObj.isNull()) {
        config.color.r = colorObj["r"] | 0; // Use | 0 for default if key missing
        config.color.g = colorObj["g"] | 0;
        config.color.b = colorObj["b"] | 0;
    } else {
        config.color.r = 0; // Use | 0 for default if key missing
        config.color.g = 0;
        config.color.b = 0;
    }

    // Parse color mode
    if(doc.containsKey("colorMode")) {
        config.colorMode = doc["colorMode"];
    } else {
        config.colorMode = 1; // Default to Neutral White if not present
    }
    if(doc.containsKey("animationMode")) {
        config.animationMode = doc["animationMode"];
    } else {
        config.animationMode = 1; // Default to Neutral White if not present
    }

    if(doc.containsKey("goodNightDuration")) {
        config.goodNightDuration = doc["goodNightDuration"];
    } else {
        config.goodNightDuration = 30; // Default 30 min
    }

    if(doc.containsKey("alarmDuration")) {
        config.alarmDuration = doc["alarmDuration"];
    } else {
        config.alarmDuration = 30; // Default 30 min
    }

    if(doc.containsKey("animationSpeed")) {
        config.animationSpeed = doc["animationSpeed"];
    } else {
        config.animationSpeed = 200; // Default speed
    }

    // Parse alarms array
    JsonArray alarmsArray = doc["alarms"].as<JsonArray>();
    if(!alarmsArray.isNull()) {
        int i = 0;
        for(JsonObject alarmObj : alarmsArray) {
            if(i < MAX_ALARMS) {
                config.alarms[i].day = alarmObj["day"] | 0;
                config.alarms[i].hour = alarmObj["hour"] | 0;
                config.alarms[i].minute = alarmObj["minute"] | 0;
                config.alarms[i].active = alarmObj["active"] | false;
                i++;
            }
        }
    } else {
        // Handle missing alarms array if needed, or assume defaults are fine
    }

    return true;
}

String createWiFiStatusJson(const WiFiStatus& status) {
    StaticJsonDocument<512> doc;

    if(status.currentTime.isEmpty()) {
        doc["currentTime"] = nullptr;
    } else {
        doc["currentTime"] = status.currentTime;
    }

    doc["lastTestResult"] = (int)status.lastTestResult;
    doc["timeSinceLastTestMs"] = status.timeSinceLastTestMs;
    doc["timeSinceLastSucceededTestMs"] = status.timeSinceLastSucceededTestMs;
    doc["lastStaConnectionTime"] = status.lastStaConnectionTime;
    doc["systemTime"] = status.systemTime;
    doc["clockSynced"] = status.clockSynced;
    doc["staConfigValid"] = status.staConfigValid;

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

String createSystemConfigJson(const SystemSettings& systemSettings) {
    StaticJsonDocument<512> doc;

    doc["internalSSID"] = systemSettings.internalSSID;
    doc["internalPW"] = strlen(systemSettings.internalPW) > 0 ? PASSWORD_MASK : "";
    doc["externalSSID"] = systemSettings.externalSSID;
    doc["externalPW"] = strlen(systemSettings.externalPW) > 0 ? PASSWORD_MASK : "";

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    return jsonResponse;
}

bool parseSystemConfigJson(const String& jsonString, SystemSettings& systemSettings) {
    StaticJsonDocument<512> doc;

    DeserializationError error = deserializeJson(doc, jsonString);
    if(error) {
        return false;
    }

    strlcpy(systemSettings.internalSSID, doc["internalSSID"] | "", sizeof(systemSettings.internalSSID));

    if(doc.containsKey("internalPW")) {
        const char* internalPW = doc["internalPW"] | "";
        if(strlen(internalPW) > 0 && strcmp(internalPW, PASSWORD_MASK) != 0) {
            strlcpy(systemSettings.internalPW, internalPW, sizeof(systemSettings.internalPW));
        }
    }

    if(doc.containsKey("externalSSID")) {
        strlcpy(systemSettings.externalSSID, doc["externalSSID"] | "", sizeof(systemSettings.externalSSID));
    }

    if(doc.containsKey("externalPW")) {
        const char* externalPW = doc["externalPW"] | "";
        if(strlen(externalPW) > 0 && strcmp(externalPW, PASSWORD_MASK) != 0) {
            strlcpy(systemSettings.externalPW, externalPW, sizeof(systemSettings.externalPW));
        }
    }

    return true;
}
