#include "json_utils.h"

String createConfigJson(const FullConfig& config) {
    JsonDocument doc;

    JsonObject colorObj = doc["override_color"].to<JsonObject>();
    colorObj["r"] = config.color.r;
    colorObj["g"] = config.color.g;
    colorObj["b"] = config.color.b;

    doc["colorMode"] = config.colorMode;
    doc["brightnessMode"] = config.brightnessMode;
    doc["animationMode"] = config.animationMode;
    doc["goodNightDuration"] = config.goodNightDuration;
    doc["alarmDuration"] = config.alarmDuration;
    doc["animationSpeed"] = config.animationSpeed;

    JsonArray alarmsArray = doc["alarms"].to<JsonArray>();
    for(int i = 0; i < MAX_ALARMS; ++i) {
        JsonObject alarmObj = alarmsArray.add<JsonObject>();
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
    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, jsonString);
    if(error) {
        return false;
    }

    JsonObject colorObj = doc["override_color"];
    if(!colorObj.isNull()) {
        config.color.r = colorObj["r"] | 0;
        config.color.g = colorObj["g"] | 0;
        config.color.b = colorObj["b"] | 0;
    } else {
        config.color.r = 0;
        config.color.g = 0;
        config.color.b = 0;
    }

    config.colorMode      = doc["colorMode"]          | 1;
    config.brightnessMode = doc["brightnessMode"]      | 7;
    config.animationMode  = doc["animationMode"]       | 1;
    config.goodNightDuration = doc["goodNightDuration"]| 30;
    config.alarmDuration  = doc["alarmDuration"]       | 30;
    config.animationSpeed = doc["animationSpeed"]      | 200;

    JsonArray alarmsArray = doc["alarms"].as<JsonArray>();
    if(!alarmsArray.isNull()) {
        int i = 0;
        for(JsonObject alarmObj : alarmsArray) {
            if(i < MAX_ALARMS) {
                config.alarms[i].day    = alarmObj["day"]    | 0;
                config.alarms[i].hour   = alarmObj["hour"]   | 0;
                config.alarms[i].minute = alarmObj["minute"] | 0;
                config.alarms[i].active = alarmObj["active"] | false;
                i++;
            }
        }
    }

    return true;
}

String createWiFiStatusJson(const WiFiStatus& status) {
    JsonDocument doc;

    if(status.currentTime.isEmpty()) {
        doc["currentTime"] = nullptr;
    } else {
        doc["currentTime"] = status.currentTime;
    }

    doc["lastTestResult"]               = (int)status.lastTestResult;
    doc["timeSinceLastTestMs"]          = status.timeSinceLastTestMs;
    doc["timeSinceLastSucceededTestMs"] = status.timeSinceLastSucceededTestMs;
    doc["lastStaConnectionTime"]        = status.lastStaConnectionTime;
    doc["systemTime"]                   = status.systemTime;
    doc["clockSynced"]                  = status.clockSynced;
    doc["staConfigValid"]               = status.staConfigValid;

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

String createSystemConfigJson(const SystemSettings& systemSettings) {
    JsonDocument doc;

    doc["internalSSID"] = systemSettings.internalSSID;
    doc["internalPW"]   = strlen(systemSettings.internalPW) > 0 ? PASSWORD_MASK : "";
    doc["externalSSID"] = systemSettings.externalSSID;
    doc["externalPW"]   = strlen(systemSettings.externalPW) > 0 ? PASSWORD_MASK : "";

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    return jsonResponse;
}

bool parseSystemConfigJson(const String& jsonString, SystemSettings& systemSettings) {
    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, jsonString);
    if(error) {
        return false;
    }

    strlcpy(systemSettings.internalSSID, doc["internalSSID"] | "", sizeof(systemSettings.internalSSID));

    const char* internalPW = doc["internalPW"] | "";
    if(strlen(internalPW) > 0 && strcmp(internalPW, PASSWORD_MASK) != 0) {
        strlcpy(systemSettings.internalPW, internalPW, sizeof(systemSettings.internalPW));
    }

    strlcpy(systemSettings.externalSSID, doc["externalSSID"] | "", sizeof(systemSettings.externalSSID));

    const char* externalPW = doc["externalPW"] | "";
    if(strlen(externalPW) > 0 && strcmp(externalPW, PASSWORD_MASK) != 0) {
        strlcpy(systemSettings.externalPW, externalPW, sizeof(systemSettings.externalPW));
    }

    return true;
}
