// JSON-Serialisierung/-Deserialisierung — Implementierung
#include "json_utils.h"

String createConfigJson(const FullConfig& config) {
    JsonDocument doc;

    // Szenen und aktives Set
    JsonArray setsArray = doc["sets"].to<JsonArray>();
    for(int i = 0; i < config.setCount && i < MAX_LIGHT_SETS; ++i) {
        JsonObject s = setsArray.add<JsonObject>();
        s["colorMode"] = config.sets[i].colorMode;
        s["animationMode"] = config.sets[i].animationMode;
        JsonObject c = s["color"].to<JsonObject>();
        c["r"] = config.sets[i].color.r;
        c["g"] = config.sets[i].color.g;
        c["b"] = config.sets[i].color.b;
    }
    doc["activeSet"] = config.activeSet;
    doc["brightnessMode"] = config.brightnessMode;
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
        alarmObj["once"] = config.alarms[i].once;
        if(config.alarms[i].once) {
            // Datum im Format des HTML-Datumsfelds (JJJJ-MM-TT)
            char date[16]; // 10 Zeichen + Reserve (Compiler-Warnung bei Extremwerten)
            snprintf(date, sizeof(date), "%04u-%02u-%02u", config.alarms[i].onceYear, config.alarms[i].onceMonth,
                     config.alarms[i].onceDay);
            alarmObj["date"] = date;
        }
    }

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    return jsonResponse;
}

// Setzt die Fehlermeldung (falls gewünscht) und gibt false zurück
static bool fail(String* error, const char* msg) {
    if(error)
        *error = msg;
    return false;
}

// Liest eine Ganzzahl aus JSON; fehlt das Feld (oder ist es keine Zahl), bleibt `current`.
// Liefert false wenn der Wert außerhalb [minV, maxV] liegt.
static bool readIntRange(JsonVariantConst v, long current, long minV, long maxV, long& out) {
    out = v.is<long>() ? v.as<long>() : current;
    return out >= minV && out <= maxV;
}

// Parst /set_config. Arbeitet auf der übergebenen Kopie der aktuellen Config: fehlende Felder behalten
// ihren Wert. brightnessMode wird bewusst NICHT übernommen (Drehknopf ist Master der Helligkeit).
// Bei Fehler bleibt `config` unverändert und `error` enthält eine deutsche Meldung.
bool parseConfigJson(const String& jsonString, FullConfig& config, String* error) {
    JsonDocument doc;

    DeserializationError err = deserializeJson(doc, jsonString);
    if(err) {
        return fail(error, "Ungültiges JSON.");
    }

    FullConfig tmp = config; // erst auf Kopie arbeiten, bei Fehler nichts übernehmen
    long v;

    // Szenen: fehlt "sets", bleiben die aktuellen; sonst wird die Liste komplett ersetzt (1–6)
    JsonArrayConst setsArray = doc["sets"].as<JsonArrayConst>();
    if(!setsArray.isNull()) {
        if(setsArray.size() < 1 || setsArray.size() > MAX_LIGHT_SETS)
            return fail(error, "Es muss 1–6 Sets geben.");
        LightSet sets[MAX_LIGHT_SETS];
        memset(sets, 0, sizeof(sets));
        int i = 0;
        for(JsonObjectConst s : setsArray) {
            if(!readIntRange(s["colorMode"], 1, 0, 4, v)) return fail(error, "Lichtfarbe (colorMode) muss 0–4 sein.");
            sets[i].colorMode = v;
            if(!readIntRange(s["animationMode"], 0, 0, 2, v)) return fail(error, "Animation muss 0–2 sein.");
            sets[i].animationMode = v;
            JsonObjectConst c = s["color"].as<JsonObjectConst>();
            if(!readIntRange(c["r"], 255, 0, 255, v)) return fail(error, "Rotwert muss 0–255 sein.");
            sets[i].color.r = v;
            if(!readIntRange(c["g"], 255, 0, 255, v)) return fail(error, "Grünwert muss 0–255 sein.");
            sets[i].color.g = v;
            if(!readIntRange(c["b"], 255, 0, 255, v)) return fail(error, "Blauwert muss 0–255 sein.");
            sets[i].color.b = v;
            i++;
        }
        memcpy(tmp.sets, sets, sizeof(tmp.sets));
        tmp.setCount = i;
    }
    // Aktives Set: muss auf ein vorhandenes Set zeigen (fehlt es, bleibt es, notfalls gekürzt)
    if(!readIntRange(doc["activeSet"], tmp.activeSet, 0, tmp.setCount - 1, v)) {
        if(doc["activeSet"].is<long>())
            return fail(error, "Aktives Set existiert nicht.");
        v = tmp.setCount - 1; // Liste wurde gekürzt → letztes Set
    }
    tmp.activeSet = v;
    if(!readIntRange(doc["goodNightDuration"], tmp.goodNightDuration, 1, 1440, v))
        return fail(error, "Gute-Nacht-Dauer muss 1–1440 Minuten sein.");
    tmp.goodNightDuration = v;
    if(!readIntRange(doc["alarmDuration"], tmp.alarmDuration, 1, 1440, v))
        return fail(error, "Weckdauer muss 1–1440 Minuten sein.");
    tmp.alarmDuration = v;
    if(!readIntRange(doc["animationSpeed"], tmp.animationSpeed, 10, 65535, v))
        return fail(error, "Animationsgeschwindigkeit muss 10–65535 sein.");
    tmp.animationSpeed = v;
    // brightnessMode aus dem JSON wird ignoriert (K8)

    JsonArrayConst alarmsArray = doc["alarms"].as<JsonArrayConst>();
    if(!alarmsArray.isNull()) {
        if(alarmsArray.size() > MAX_ALARMS) return fail(error, "Zu viele Wecker.");
        int i = 0;
        for(JsonObjectConst alarmObj : alarmsArray) {
            if(!readIntRange(alarmObj["day"], tmp.alarms[i].day, 0, 7, v)) return fail(error, "Wecker-Tag muss 0–7 sein.");
            tmp.alarms[i].day = v;
            if(!readIntRange(alarmObj["hour"], tmp.alarms[i].hour, 0, 23, v)) return fail(error, "Wecker-Stunde muss 0–23 sein.");
            tmp.alarms[i].hour = v;
            if(!readIntRange(alarmObj["minute"], tmp.alarms[i].minute, 0, 59, v)) return fail(error, "Wecker-Minute muss 0–59 sein.");
            tmp.alarms[i].minute = v;
            tmp.alarms[i].active = alarmObj["active"] | tmp.alarms[i].active;
            // Einmaliger Termin: "once": true + "date": "JJJJ-MM-TT"
            tmp.alarms[i].once = alarmObj["once"] | false;
            if(tmp.alarms[i].once) {
                const char* date = alarmObj["date"] | "";
                unsigned y = 0, m = 0, d = 0;
                if(sscanf(date, "%4u-%2u-%2u", &y, &m, &d) != 3 || y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1
                   || d > 31)
                    return fail(error, "Wecker-Datum ungültig (JJJJ-MM-TT).");
                tmp.alarms[i].onceYear = y;
                tmp.alarms[i].onceMonth = m;
                tmp.alarms[i].onceDay = d;
                tmp.alarms[i].day = 0; // Wochentag spielt bei einmaligen Weckern keine Rolle
            } else {
                tmp.alarms[i].onceYear = 0;
                tmp.alarms[i].onceMonth = 0;
                tmp.alarms[i].onceDay = 0;
            }
            i++;
        }
    }

    config = tmp;
    return true;
}

String createWiFiStatusJson(const WiFiStatus& status) {
    JsonDocument doc;

    if(status.currentTime.isEmpty()) {
        doc["currentTime"] = nullptr;
    } else {
        doc["currentTime"] = status.currentTime;
    }

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

    doc["internalSSID"]     = systemSettings.internalSSID;
    doc["internalPW"]       = strlen(systemSettings.internalPW) > 0 ? PASSWORD_MASK : "";
    doc["externalSSID"]     = systemSettings.externalSSID;
    doc["externalPW"]       = strlen(systemSettings.externalPW) > 0 ? PASSWORD_MASK : "";
    doc["externalSSID2"]    = systemSettings.externalSSID2;
    doc["externalPW2"]      = strlen(systemSettings.externalPW2) > 0 ? PASSWORD_MASK : "";
    doc["warningsEnabled"]  = systemSettings.warningsEnabled;
    doc["deepSleepEnabled"] = systemSettings.deepSleepEnabled;
    doc["tsfFallbackEnabled"] = systemSettings.tsfFallbackEnabled;
    doc["rdsClockEnabled"]  = systemSettings.rdsClockEnabled;
    doc["rtcCrystalEnabled"] = systemSettings.rtcCrystalEnabled;
    doc["ledCount"]         = systemSettings.ledCount;
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    return jsonResponse;
}

// Prüft/übernimmt ein Passwortfeld: leer oder Maske = unverändert, sonst muss die Länge 8–63 sein (WPA).
// `clear` (clearXxxPW) leert das Passwort explizit. Gibt false bei ungültiger Länge zurück.
static bool applyPassword(JsonVariantConst value, bool clear, char* dst, size_t dstSize) {
    const char* src = value.is<const char*>() ? value.as<const char*>() : "";
    if(src[0] != '\0' && strcmp(src, PASSWORD_MASK) != 0) {
        size_t len = strlen(src);
        if(len < 8 || len > 63 || len >= dstSize)
            return false;
        strlcpy(dst, src, dstSize);
    }
    if(clear)
        dst[0] = '\0'; // explizites Entfernen gewinnt gegenüber einem mitgesendeten Wert
    return true;
}

// Übernimmt eine SSID: fehlt das Feld, bleibt der Wert. Länge muss minLen–32 sein.
static bool applySsid(JsonVariantConst value, char* dst, size_t dstSize, size_t minLen) {
    if(!value.is<const char*>())
        return true;
    const char* src = value.as<const char*>();
    size_t len = strlen(src);
    if(len < minLen || len >= dstSize)
        return false;
    strlcpy(dst, src, dstSize);
    return true;
}

// Parst /set_system_config auf einer Kopie; Felder die fehlen behalten ihren Wert.
// Regeln: AP-SSID 1–32 Zeichen; AP-Passwort leer(=unverändert) oder 8–63; externe SSID 0–32,
// externe Passwörter leer(=unverändert) oder 8–63.
// Optionale Boolean-Felder zum Entfernen von Passwörtern: "clearInternalPW", "clearExternalPW", "clearExternalPW2".
bool parseSystemConfigJson(const String& jsonString, SystemSettings& systemSettings, String* error) {
    JsonDocument doc;

    DeserializationError err = deserializeJson(doc, jsonString);
    if(err) {
        return fail(error, "Ungültiges JSON.");
    }

    SystemSettings tmp = systemSettings;

    if(!applySsid(doc["internalSSID"], tmp.internalSSID, sizeof(tmp.internalSSID), 1))
        return fail(error, "Hotspot-Name muss 1–32 Zeichen lang sein.");
    if(!applyPassword(doc["internalPW"], doc["clearInternalPW"] | false, tmp.internalPW, sizeof(tmp.internalPW)))
        return fail(error, "Hotspot-Passwort muss leer oder 8–63 Zeichen lang sein.");

    if(!applySsid(doc["externalSSID"], tmp.externalSSID, sizeof(tmp.externalSSID), 0))
        return fail(error, "WLAN-Name (extern) darf höchstens 32 Zeichen lang sein.");
    if(!applyPassword(doc["externalPW"], doc["clearExternalPW"] | false, tmp.externalPW, sizeof(tmp.externalPW)))
        return fail(error, "WLAN-Passwort (extern) muss leer oder 8–63 Zeichen lang sein.");

    if(!applySsid(doc["externalSSID2"], tmp.externalSSID2, sizeof(tmp.externalSSID2), 0))
        return fail(error, "WLAN-Name 2 darf höchstens 32 Zeichen lang sein.");
    if(!applyPassword(doc["externalPW2"], doc["clearExternalPW2"] | false, tmp.externalPW2, sizeof(tmp.externalPW2)))
        return fail(error, "WLAN-Passwort 2 muss leer oder 8–63 Zeichen lang sein.");

    tmp.warningsEnabled  = doc["warningsEnabled"]  | tmp.warningsEnabled;
    tmp.deepSleepEnabled = doc["deepSleepEnabled"] | tmp.deepSleepEnabled;

    systemSettings = tmp;
    return true;
}
