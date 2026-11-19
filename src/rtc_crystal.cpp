// RTC-Quarz-Extension — Implementierung
// Schaltet beim Boot auf den externen 32.768-kHz-Uhrenquarz um (GPIO0/GPIO1).
// Prüft via Kalibrierungsmessung ob der Quarz tatsächlich schwingt; fällt sonst
// sicher auf den internen RC-Oszillator zurück. Nur einmalig beim Start aufrufbar.
#include "rtc_crystal.h"
#include "store.h"
#include "event_log.h"
#include "debug_utils.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <soc/rtc.h>
#include <esp_idf_version.h>

// Zeiger auf SystemSettings für Statusabfragen nach der Initialisierung
static SystemSettings* g_settings = nullptr;

// Gemessene Frequenz des Quarzes beim letzten Boot (0 = nicht gemessen)
static uint32_t s_measuredFreqHz = 0;

// Berechnet Frequenz aus dem IDF-Kalibrierungswert
// cal enthält die Taktperiode in Einheiten von 1/(2^19) Sekunden
static uint32_t calcFreqFromCal(uint32_t cal) {
    if(cal == 0) return 0;
    return (uint32_t)(1000000ULL * (1ULL << 19) / cal);
}

// Initialisiert den RTC-Takt: testet den externen Quarz und stellt die Taktquelle entsprechend ein.
// Schreibt in den Flash nur, wenn sich rtcCrystalAvailable gegenüber dem gespeicherten Stand geändert hat.
void initRtcCrystal(SystemSettings* settings) {
    g_settings = settings;
    const bool previousAvailable = settings->rtcCrystalAvailable; // Stand vor diesem Boot

    if(!settings->rtcCrystalEnabled) {
        settings->rtcCrystalAvailable = false;
        if(previousAvailable)
            saveSystemSettings(*settings); // Status hat sich geändert → einmalig speichern
        serialPrint("RTC: Quarz deaktiviert, bleibe auf RC-Oszi");
        return;
    }

    serialPrint("RTC: Starte 32kHz-Quarz-Initialisierung...");

    // Anlauf-Pulse: lädt den Schwingkreis an
    rtc_clk_32k_bootstrap(200);
    delay(300); // Quarz braucht ~200-300ms zum Einschaukeln

    // Kalibrierungsmessung: misst die Taktperiode des 32k-Quarzes
    uint32_t cal = rtc_clk_cal(RTC_CAL_32K_XTAL, 1000);
    s_measuredFreqHz = calcFreqFromCal(cal);

    serialPrint("RTC: Kalibrierung cal=" + String(cal) + ", freq=" + String(s_measuredFreqHz) + " Hz");

    // Quarz gilt als gültig wenn Frequenz im Bereich 32000-33000 Hz liegt
    bool valid = (cal > 0) && (s_measuredFreqHz >= 32000) && (s_measuredFreqHz <= 33000);

    if(valid) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_XTAL32K);
#else
        rtc_clk_slow_freq_set(RTC_SLOW_FREQ_32K_XTAL);
#endif
        settings->rtcCrystalAvailable = true;
        logEvent("RTC: 32kHz Quarz aktiv");
        serialPrint("RTC: 32kHz Quarz aktiv (" + String(s_measuredFreqHz) + " Hz)");
    } else {
        // Quarz fehlt oder schwingt nicht → sicher auf RC-Oszillator zurückfallen
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_RC_SLOW);
#else
        rtc_clk_slow_freq_set(RTC_SLOW_FREQ_8MD256);
#endif
        settings->rtcCrystalAvailable = false;
        logEvent("RTC: Quarz nicht erkannt, RC-Oszi");
        serialPrint("RTC: Quarz nicht erkannt (cal=" + String(cal) + "), bleibe auf RC-Oszi");
    }

    // Nur bei geändertem Status speichern (schont den Flash, kein Schreiben bei jedem Boot)
    if(settings->rtcCrystalAvailable != previousAvailable)
        saveSystemSettings(*settings);
}

// Liefert true, wenn der externe 32-kHz-Quarz beim Boot erkannt und als RTC-Takt aktiviert wurde
bool isRtcCrystalActive() {
    return g_settings != nullptr && g_settings->rtcCrystalAvailable;
}

// Liefert den Quarz-Status als JSON für das Frontend
String getRtcCrystalStatusJson() {
    JsonDocument doc;
    doc["enabled"]   = g_settings != nullptr && g_settings->rtcCrystalEnabled;
    doc["available"] = g_settings != nullptr && g_settings->rtcCrystalAvailable;
    doc["freqHz"]    = s_measuredFreqHz;
    String out;
    serializeJson(doc, out);
    return out;
}
