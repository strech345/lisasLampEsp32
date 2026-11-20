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

// Misst ob der externe 32-kHz-Quarz schwingt (Anlauf-Pulse + Kalibrierung, ca. 0,3–0,5 s).
// Gültig wenn die gemessene Frequenz zwischen 32.000 und 33.000 Hz liegt.
static bool probeCrystal() {
    rtc_clk_32k_bootstrap(200); // Anlauf-Pulse: lädt den Schwingkreis an
    delay(300);                 // Quarz braucht ~200–300 ms zum Einschwingen
    uint32_t cal = rtc_clk_cal(RTC_CAL_32K_XTAL, 1000);
    s_measuredFreqHz = calcFreqFromCal(cal);
    serialPrint("RTC: Kalibrierung cal=" + String(cal) + ", freq=" + String(s_measuredFreqHz) + " Hz");
    return cal > 0 && s_measuredFreqHz >= 32000 && s_measuredFreqHz <= 33000;
}

// Initialisiert den RTC-Takt. Beim Einschalten (nicht nach Deep Sleep) wird immer gemessen, ob ein
// Quarz vorhanden ist (rtcCrystalAvailable) — auch wenn er deaktiviert ist, damit die Weboberfläche
// ihn nur anbietet, wenn er da ist. Ist er aktiviert und vorhanden, wird er zur Taktquelle.
// Schreibt in den Flash nur, wenn sich rtcCrystalAvailable geändert hat.
void initRtcCrystal(SystemSettings* settings, bool fromDeepSleep) {
    g_settings = settings;
    serialPrint("RTC-Takt beim Start: " + String(rtc_clk_slow_freq_get_hz()) + " Hz");
    const bool previousAvailable = settings->rtcCrystalAvailable; // Stand vor diesem Boot

    if(!settings->rtcCrystalEnabled) {
        if(!fromDeepSleep) {
            // Nur prüfen ob vorhanden, Taktquelle bleibt der RC-Oszillator; Schwingkreis wieder aus
            settings->rtcCrystalAvailable = probeCrystal();
            rtc_clk_32k_enable(false);
            serialPrint(settings->rtcCrystalAvailable ? "RTC: Quarz vorhanden (deaktiviert)"
                                                      : "RTC: kein Quarz erkannt");
        }
        if(settings->rtcCrystalAvailable != previousAvailable)
            saveSystemSettings(*settings);
        return;
    }

    serialPrint("RTC: Starte 32kHz-Quarz-Initialisierung...");
    bool valid = probeCrystal();
    if(valid) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_XTAL32K);
#else
        rtc_clk_slow_freq_set(RTC_SLOW_FREQ_32K_XTAL);
#endif
        logEvent("RTC: 32 kHz crystal active");
        serialPrint("RTC: 32 kHz crystal active (" + String(s_measuredFreqHz) + " Hz)");
    } else {
        // Quarz fehlt oder schwingt nicht → sicher auf RC-Oszillator zurückfallen
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_RC_SLOW);
#else
        rtc_clk_slow_freq_set(RTC_SLOW_FREQ_8MD256);
#endif
        rtc_clk_32k_enable(false);
        logEvent("RTC: no crystal, using RC oscillator");
        serialPrint("RTC: Quarz nicht erkannt, bleibe auf RC-Oszi");
    }
    settings->rtcCrystalAvailable = valid;

    // Nur bei geändertem Status speichern (schont den Flash, kein Schreiben bei jedem Boot)
    if(settings->rtcCrystalAvailable != previousAvailable)
        saveSystemSettings(*settings);
}

// Liefert true, wenn der externe 32-kHz-Quarz aktiviert und vorhanden ist (= RTC-Taktquelle)
bool isRtcCrystalActive() {
    return g_settings != nullptr && g_settings->rtcCrystalEnabled && g_settings->rtcCrystalAvailable;
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
