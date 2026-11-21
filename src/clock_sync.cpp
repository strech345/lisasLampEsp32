// Zeitpunkte der letzten Uhr-Abgleiche je Quelle (NTP, RDS, TSF) — Implementierung
#include "clock_sync.h"
#include <time.h>
#include <esp_attr.h>
#include "wifi_controller.h"
#include "store.h"

// Unix-Sekunden des letzten Abgleichs je Quelle (Index = ClockSyncSource); 0 = seit dem Einschalten noch nie.
// RTC_DATA_ATTR: bleibt im Deep Sleep erhalten.
RTC_DATA_ATTR static time_t s_lastSyncAt[3] = {0, 0, 0};

// Merkt sich die aktuelle (gerade gesetzte) Uhrzeit als Zeitpunkt des Abgleichs dieser Quelle und sichert
// sie im NVS (gilt für NTP, RDS und TSF gleichermaßen): Nach einem Stromausfall startet die Uhr dann mit
// dem Stand des letzten Abgleichs statt mit einem älteren.
void noteClockSync(ClockSyncSource source) {
    if(source > CLOCK_SYNC_TSF)
        return;
    time_t now = time(nullptr);
    s_lastSyncAt[source] = now;
    saveLastKnownTime(now);
}

// Mindestens ein Abgleich seit dem Einschalten (die Zeitpunkte liegen im RTC-Speicher)
bool clockSyncedSincePowerOn() {
    return s_lastSyncAt[CLOCK_SYNC_NTP] != 0 || s_lastSyncAt[CLOCK_SYNC_RDS] != 0 || s_lastSyncAt[CLOCK_SYNC_TSF] != 0;
}

// Zeitpunkt des letzten Abgleichs dieser Quelle (0 = noch nie)
time_t lastClockSyncAt(ClockSyncSource source) {
    return source <= CLOCK_SYNC_TSF ? s_lastSyncAt[source] : 0;
}

// Schreibt Systemzeit, Abgleich-Zeitpunkte und den Stand des nächsten NTP-Abgleichs ins JSON-Objekt
void fillClockSyncJson(JsonObject obj) {
    obj["now"] = (uint32_t)time(nullptr);
    obj["ntpAt"] = (uint32_t)s_lastSyncAt[CLOCK_SYNC_NTP];
    obj["rdsAt"] = (uint32_t)s_lastSyncAt[CLOCK_SYNC_RDS];
    obj["tsfAt"] = (uint32_t)s_lastSyncAt[CLOCK_SYNC_TSF];
    obj["lastSyncOk"] = lastSyncOk();
    obj["staActive"] = isStaActive();
    obj["nextSyncInMs"] = msUntilNextSync();
}
