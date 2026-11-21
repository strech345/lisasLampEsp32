// Live-Systemdaten als JSON — Implementierung
#include "system_info.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "clock_sync.h"

// Wandelt den Reset-Grund in den von der UI erwarteten Text um
static const char* resetReasonToString(esp_reset_reason_t reason) {
    switch(reason) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_EXT: return "ext";
    default: return "unknown";
    }
}

// Rundet auf eine Nachkommastelle
static float round1(float value) {
    return roundf(value * 10.0f) / 10.0f;
}

// Berechnet die CPU-Last in Prozent seit dem vorigen Aufruf; -1 wenn nicht messbar
static float measureCpuLoad() {
    // Zählerstände des vorigen Aufrufs (0 = Start, daher beim ersten Mal "seit Start")
    static uint32_t prevIdle = 0;
    static uint32_t prevTotal = 0;

    uint32_t idle = (uint32_t)ulTaskGetIdleRunTimeCounter();
    uint32_t total = (uint32_t)portGET_RUN_TIME_COUNTER_VALUE();

    // Vorzeichenlose Subtraktion bleibt auch beim 32-Bit-Überlauf (~71 min) korrekt
    uint32_t idleDelta = idle - prevIdle;
    uint32_t totalDelta = total - prevTotal;
    prevIdle = idle;
    prevTotal = total;

    if(totalDelta == 0) return -1.0f;
    float load = 100.0f * (1.0f - (float)idleDelta / (float)totalDelta);
    if(load < 0.0f) load = 0.0f;
    if(load > 100.0f) load = 100.0f;
    return round1(load);
}

// Füllt das Array mit den am Hotspot (SoftAP) verbundenen Geräten: { ip, rssi, you }.
// Leeres Array, wenn der AP aus ist oder niemand verbunden ist. MAC-Adressen werden nicht ausgegeben.
static void fillApClients(JsonArray arr, IPAddress requesterIp) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    if(esp_wifi_get_mode(&mode) != ESP_OK) return;
    if(mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) return; // ohne AP gibt es keine Stationsliste

    wifi_sta_list_t staList;
    if(esp_wifi_ap_get_sta_list(&staList) != ESP_OK || staList.num <= 0) return;

    // MACs in das Pair-Array kopieren; der DHCP-Server trägt dort die IPs ein
    esp_netif_pair_mac_ip_t pairs[ESP_WIFI_MAX_CONN_NUM];
    int n = staList.num < ESP_WIFI_MAX_CONN_NUM ? staList.num : ESP_WIFI_MAX_CONN_NUM;
    for(int i = 0; i < n; i++) {
        memcpy(pairs[i].mac, staList.sta[i].mac, 6);
        pairs[i].ip.addr = 0;
    }
    esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    bool haveIps = apNetif && esp_netif_dhcps_get_clients_by_mac(apNetif, n, pairs) == ESP_OK;

    for(int i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        // IP-Adresse (lwIP-Byteorder wie IPAddress(uint32_t)); leer, wenn unbekannt
        uint32_t ipRaw = haveIps ? pairs[i].ip.addr : 0;
        o["ip"] = ipRaw ? IPAddress(ipRaw).toString() : String("");
        o["rssi"] = staList.sta[i].rssi;
        o["you"] = ipRaw != 0 && IPAddress(ipRaw) == requesterIp;
    }
}

String getSystemInfoJson(IPAddress requesterIp) {
    // Ändert sich zur Laufzeit nicht: einmal ermitteln (getSketchSize liest sonst den Flash)
    static bool staticReady = false;
    static uint32_t sketchSize = 0, sketchFree = 0, flashSize = 0;
    static const char* resetReason = "unknown";
    if(!staticReady) {
        sketchSize = ESP.getSketchSize();
        sketchFree = ESP.getFreeSketchSpace();
        flashSize = ESP.getFlashChipSize();
        resetReason = resetReasonToString(esp_reset_reason());
        staticReady = true;
    }

    JsonDocument doc;
    doc["uptimeMs"] = millis();
    doc["cpuLoad"] = measureCpuLoad();
    doc["heapTotal"] = ESP.getHeapSize();
    doc["heapFree"] = ESP.getFreeHeap();
    doc["heapMinFree"] = ESP.getMinFreeHeap();
    doc["heapMaxAlloc"] = ESP.getMaxAllocHeap();
    doc["fsTotal"] = LittleFS.totalBytes();
    doc["fsUsed"] = LittleFS.usedBytes();
    doc["sketchSize"] = sketchSize;
    doc["sketchFree"] = sketchFree;
    doc["flashSize"] = flashSize;
    doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0; // nur als Client sinnvoll
    fillApClients(doc["apClients"].to<JsonArray>(), requesterIp);
    doc["tempC"] = round1(temperatureRead());
    doc["resetReason"] = resetReason;
    fillClockSyncJson(doc["clock"].to<JsonObject>()); // Systemzeit und Uhr-Abgleiche

    String out;
    serializeJson(doc, out);
    return out;
}
