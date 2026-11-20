// RDS-Uhrzeit-Fallback — Implementierung
// Steuert RDA5807M-FM-Chip per I2C, scannt das Band nach einem empfangbaren Sender
// und wertet RDS-Group-4A (Clocktime & Date) aus um die Systemzeit zu setzen.
#include "rds_clock.h"
#include "pins.h"
#include "store.h"
#include "debug_utils.h"
#include "system_utils.h"
#include "event_log.h"
#include <Wire.h>
#include <esp_attr.h>
#include <WiFi.h>
#include <sys/time.h>
#include <time.h>
#include <ArduinoJson.h>

// Pins (SDA/SCL/Strom) kommen aus include/pins.h

#define RDA5807M_ADDR  0x11  // Random-access I2C-Adresse

#define FM_START_10KHZ  8750
#define FM_END_10KHZ   10800
#define FM_STEP_10KHZ     10

#define RSSI_THRESHOLD   25
#define CT_WAIT_MS    35000   // Wartezeit auf das erste CT-Paket pro Kanal
#define CT_WAIT_PAIR_MS 130000 // verlängerte Wartezeit, sobald ein erstes CT-Paket vorliegt
#define POWER_UP_MS     300

#define REG_CONFIG  0x02
#define REG_TUNE    0x03
#define REG_STATUS  0x0A
#define REG_SIGNAL  0x0B
#define REG_RDSA    0x0C
#define REG_RDSB    0x0D
#define REG_RDSC    0x0E
#define REG_RDSD    0x0F

enum RdsState {
    RDS_STATE_OFF,
    RDS_STATE_POWERING,
    RDS_STATE_TUNING,
    RDS_STATE_LOCKED,
    RDS_STATE_DONE,
    RDS_STATE_NO_SIGNAL
};

static const SystemSettings* g_settings = nullptr;
static WiFiTestTracker*       g_tracker  = nullptr;

static RdsState      g_state       = RDS_STATE_OFF;
static uint16_t      g_currentCh   = 0;
static uint16_t      g_scanStartCh = 0;
static unsigned long g_stateMs     = 0;
static unsigned long g_lastSyncMs  = 0;
static unsigned long g_idleSinceMs = 0;

// Modul vorhanden? Beim Einschalten per I2C geprüft; im RTC-Speicher, übersteht so den Deep Sleep
RTC_DATA_ATTR static bool s_modulePresent = false; // Zeitpunkt des Wechsels nach DONE/NO_SIGNAL

#define RDS_RETRY_NO_SIGNAL_MS (60UL * 60UL * 1000UL)       // nach "kein Signal": in 1 h erneut versuchen
#define RDS_RESYNC_AFTER_DONE_MS (24UL * 60UL * 60UL * 1000UL) // nach Erfolg: nach 24 h neu abgleichen

// Wechselt in einen Ruhezustand (DONE/NO_SIGNAL): Modul aus, Zeitpunkt für den nächsten Versuch merken
static void enterIdle(RdsState state) {
    digitalWrite(PIN_RDS_POWER, LOW);
    g_state = state;
    g_idleSinceMs = millis();
}

// Schreibt ein 16-Bit-Register des RDA5807M per I2C (Big-Endian)
static bool writeReg(uint8_t reg, uint16_t val) {
    Wire.beginTransmission(RDA5807M_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(val >> 8));
    Wire.write((uint8_t)(val & 0xFF));
    return Wire.endTransmission() == 0; // 0 = ACK vom Chip
}

// Liest ein 16-Bit-Register des RDA5807M per I2C (Big-Endian, Repeated-Start)
static uint16_t readReg(uint8_t reg) {
    Wire.beginTransmission(RDA5807M_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)RDA5807M_ADDR, (uint8_t)2);
    uint16_t val = 0;
    if (Wire.available()) val = (uint16_t)Wire.read() << 8;
    if (Wire.available()) val |= Wire.read();
    return val;
}

// Initialisiert den RDA5807M (REG 0x02): DHIZ|DMUTE|BASS|ENABLE (=0xD001) + RDS_EN (Bit 3) +
// NEW_METHOD (Bit 2, bessere Empfangsqualität). Kanalraster bleibt 100 kHz (REG 0x03).
// Gibt false zurück wenn der Chip nicht antwortet (Modul fehlt / I2C-Fehler).
static bool chipInit() {
    bool ok = writeReg(REG_CONFIG, 0xD001 | (1 << 3) | (1 << 2));
    delay(50);
    return ok;
}

// Stimmt den Chip auf die angegebene Frequenz (in 10kHz-Einheiten) ab
static void chipTune(uint16_t freq_10kHz) {
    uint16_t chan = (freq_10kHz - FM_START_10KHZ) / FM_STEP_10KHZ;
    writeReg(REG_TUNE, (chan << 6) | (1 << 4));
}

// Gibt den RSSI-Wert des aktuellen Kanals zurück (Bits 15:9 von REG_SIGNAL)
static uint8_t getRssi() {
    return (uint8_t)(readReg(REG_SIGNAL) >> 9);
}

// Gibt true zurück wenn RDS-Bit RDSR (Bit 15 in REG_STATUS) gesetzt ist
static bool isRdsReady() {
    return (readReg(REG_STATUS) & 0x8000) != 0;
}

// Dekodiert RDS-Blöcke B/C/D als Group 4A (Clocktime) und liefert die UTC-Zeit.
// Gibt false zurück wenn es keine 4A-Gruppe ist oder die Werte unplausibel sind.
// Hinweis: RDS-CT überträgt Stunde/Minute/Datum in UTC; der lokale Offset (Bits 5:0 von Block D,
// in 30-min-Schritten) ist nur Zusatzinfo und wird NICHT mehr abgezogen (die Zeitzone kommt aus TZ).
static bool decodeCt(uint16_t blockB, uint16_t blockC, uint16_t blockD, time_t* outUtc) {
    if (((blockB >> 12) & 0x0F) != 4 || ((blockB >> 11) & 0x01) != 0) return false;

    unsigned long mjd = ((unsigned long)(blockB & 0x3) << 15) | (blockC >> 1);
    uint8_t hours    = (uint8_t)(((blockC & 0x1) << 4) | (blockD >> 12));
    uint8_t minutes  = (blockD >> 6) & 0x3F;

    if (hours > 23 || minutes > 59) return false;

    long unixDay = (long)mjd - 40587L; // MJD 40587 = 1970-01-01
    if (unixDay < 0) return false;
    time_t unixUtc = (time_t)((long long)unixDay * 86400LL + hours * 3600L + minutes * 60L);

    struct tm t;
    if (gmtime_r(&unixUtc, &t) == nullptr || t.tm_year + 1900 < 2021) return false;

    *outUtc = unixUtc;
    return true;
}

// Letztes plausibles CT-Paket (für die Zwei-Paket-Prüfung); Zustand wird beim Kanalwechsel verworfen
static bool   g_haveLastCt   = false;
static time_t g_lastCtUtc    = 0;
static unsigned long g_lastCtMs = 0;

// Verwirft ein gemerktes CT-Paket (z.B. bei Kanalwechsel oder Neustart des Scans)
static void resetCtPlausibility() {
    g_haveLastCt = false;
}

// Prüft Blockfehler und CT-Plausibilität und setzt erst dann die Systemzeit.
// RDA5807M REG 0x0B (Datenblatt): Bits 3:2 = BLERA (Fehler Block A), Bits 1:0 = BLERB (Fehler Block B);
// 0 = fehlerfrei, 1-2 = korrigiert, 3 = nicht korrigierbar. Für Block C/D hat der Chip keine eigenen
// Bits, daher verlangen wir zwei CT-Pakete nacheinander, deren Zeitdifferenz zur verstrichenen
// Echtzeit passt (Toleranz 3 s); ein Bitfehler in C/D fällt dadurch praktisch immer auf.
static bool tryParseCt(uint16_t blockB, uint16_t blockC, uint16_t blockD, uint16_t regSignal) {
    uint8_t blera = (regSignal >> 2) & 0x03;
    uint8_t blerb = regSignal & 0x03;
    if (blera != 0 || blerb != 0) return false; // nur fehlerfreie Blöcke A und B akzeptieren

    time_t utc;
    if (!decodeCt(blockB, blockC, blockD, &utc)) return false;

    unsigned long nowMs = millis();
    if (!g_haveLastCt) {
        // Erstes Paket: nur merken, noch nichts setzen
        g_haveLastCt = true;
        g_lastCtUtc  = utc;
        g_lastCtMs   = nowMs;
        return false;
    }

    // CT wird einmal pro Minute gesendet: Zeitdifferenz muss der gemessenen Echtzeit entsprechen
    long deltaCt   = (long)(utc - g_lastCtUtc);
    long deltaReal = (long)((nowMs - g_lastCtMs) / 1000UL);
    if (deltaCt < 50 || deltaCt > 70 || labs(deltaCt - deltaReal) > 3) {
        // Passt nicht: aktuelles Paket als neue Referenz nehmen
        g_lastCtUtc = utc;
        g_lastCtMs  = nowMs;
        return false;
    }

    struct timeval tv = { utc, 0 };
    settimeofday(&tv, nullptr);
    // Zeit gilt jetzt als gültig → keine "Zeit veraltet"-Warnung; clockSynced (= NTP) bleibt unverändert
    if (g_tracker != nullptr) g_tracker->lastSucceededTestTime = millis();
    return true;
}

// Liest RDS-Blöcke aus und versucht einen CT-Parse; gibt true zurück wenn Zeit gesetzt wurde
static bool checkRds() {
    if (!isRdsReady()) return false;
    uint16_t sig = readReg(REG_SIGNAL);
    return tryParseCt(readReg(REG_RDSB), readReg(REG_RDSC), readReg(REG_RDSD), sig);
}

// Gibt den nächsten Kanal im Scan zurück; rollt vom Ende des Bandes zurück zum Anfang
static uint16_t nextChannel(uint16_t ch) {
    ch += FM_STEP_10KHZ;
    if (ch > FM_END_10KHZ) ch = FM_START_10KHZ;
    return ch;
}

// Speichert den aktuellen Kanal in SystemSettings damit beim nächsten Start direkt dort begonnen wird
static void saveChannel(uint16_t ch) {
    if (g_settings == nullptr) return;
    const_cast<SystemSettings*>(g_settings)->rdsLastChannel = ch;
    saveSystemSettings(*g_settings);
}

// Initialisiert I2C-Bus, Power-Pin (LOW = aus) und gibt den letzten bekannten Kanal aus
void initRdsClock(const SystemSettings* settings, WiFiTestTracker* tracker, bool fromDeepSleep) {
    g_settings = settings;
    g_tracker  = tracker;

    pinMode(PIN_RDS_POWER, OUTPUT);
    digitalWrite(PIN_RDS_POWER, LOW);

    Wire.begin(PIN_RDS_SDA, PIN_RDS_SCL);

    if(!fromDeepSleep) {
        // Modul kurz einschalten und fragen ob es auf seiner I2C-Adresse antwortet (ACK)
        digitalWrite(PIN_RDS_POWER, HIGH);
        delay(POWER_UP_MS);
        Wire.beginTransmission(RDA5807M_ADDR);
        s_modulePresent = Wire.endTransmission() == 0;
        digitalWrite(PIN_RDS_POWER, LOW);
        serialPrint(s_modulePresent ? "RDS: Modul gefunden" : "RDS: kein Modul gefunden");
    }

    uint16_t ch = settings->rdsLastChannel;
    serialPrint(ch > 0
        ? ("RDS: Init, letzter Kanal " + String(ch / 10.0f, 1) + " MHz")
        : String("RDS: Init, kein gespeicherter Kanal"));
}

// Zustandsmaschine: schaltet Modul ein, scannt Kanäle, wartet auf RDS-CT-Paket;
// stoppt sobald Uhrzeit gesetzt oder WiFi-Verbindung vorhanden ist
void rdsClockLoop() {
    // Ohne Modul (oder ausgeschaltet) nichts tun
    if (g_settings == nullptr || !g_settings->rdsClockEnabled || !s_modulePresent) {
        if (g_state != RDS_STATE_OFF && g_state != RDS_STATE_DONE) {
            digitalWrite(PIN_RDS_POWER, LOW);
            g_state = RDS_STATE_OFF;
        }
        return;
    }

    if (g_tracker != nullptr && (g_tracker->clockSynced || g_tracker->staConfigValid)) {
        if (g_state != RDS_STATE_OFF && g_state != RDS_STATE_DONE) {
            digitalWrite(PIN_RDS_POWER, LOW);
            g_state = RDS_STATE_OFF;
            logEvent("RDS: module off");
        }
        return;
    }

    bool staActive = WiFi.getMode() == WIFI_MODE_APSTA;
    if (staActive) return;

    switch (g_state) {
    case RDS_STATE_OFF:
        digitalWrite(PIN_RDS_POWER, HIGH);
        g_stateMs = millis();
        g_state = RDS_STATE_POWERING;
        logEvent("RDS: scan started");
        break;

    case RDS_STATE_POWERING:
        if (millis() - g_stateMs < POWER_UP_MS) break;
        if (!chipInit()) {
            logEvent("RDS: module not responding (I2C)");
            enterIdle(RDS_STATE_NO_SIGNAL);
            break;
        }
        g_currentCh = (g_settings->rdsLastChannel > 0)
                      ? g_settings->rdsLastChannel
                      : FM_START_10KHZ;
        g_scanStartCh = g_currentCh;
        chipTune(g_currentCh);
        resetCtPlausibility(); // neuer Kanal: altes CT-Paket verwerfen
        g_stateMs = millis();
        g_state = RDS_STATE_TUNING;
        break;

    case RDS_STATE_TUNING: {
        if (millis() - g_stateMs < 500) break;
        uint8_t rssi = getRssi();
        char msg[52];
        snprintf(msg, sizeof(msg), "RDS: Kanal %.1f MHz, RSSI %d", g_currentCh / 10.0f, rssi);
        serialPrint(msg);
        if (rssi >= RSSI_THRESHOLD) {
            saveChannel(g_currentCh);
            g_stateMs = millis();
            g_state = RDS_STATE_LOCKED;
        } else {
            uint16_t next = nextChannel(g_currentCh);
            if (next == g_scanStartCh) {
                enterIdle(RDS_STATE_NO_SIGNAL);
                logEvent("RDS: no signal found");
            } else {
                g_currentCh = next;
                chipTune(g_currentCh);
                resetCtPlausibility(); // neuer Kanal: altes CT-Paket verwerfen
                g_stateMs = millis();
            }
        }
        break;
    }

    case RDS_STATE_LOCKED: {
        static unsigned long lastRdsCheckMs = 0;
        if (!isTimeForAction(&lastRdsCheckMs, 100)) break;

        if (checkRds()) {
            char msg[52];
            snprintf(msg, sizeof(msg), "RDS: time set %.1fMHz", g_currentCh / 10.0f);
            logEvent(msg);
            g_lastSyncMs = millis();
            enterIdle(RDS_STATE_DONE);
            logEvent("RDS: module off");
            break;
        }
        // Mit gemerktem ersten CT-Paket auf das zweite warten (CT kommt ~1x pro Minute)
        if (millis() - g_stateMs >= (g_haveLastCt ? CT_WAIT_PAIR_MS : CT_WAIT_MS)) {
            uint16_t next = nextChannel(g_currentCh);
            if (next == g_scanStartCh) {
                enterIdle(RDS_STATE_NO_SIGNAL);
                logEvent("RDS: no signal found");
            } else {
                g_currentCh = next;
                chipTune(g_currentCh);
                resetCtPlausibility(); // neuer Kanal: altes CT-Paket verwerfen
                g_stateMs = millis();
                g_state = RDS_STATE_TUNING;
            }
        }
        break;
    }

    case RDS_STATE_DONE:
        // Erneut abgleichen, damit die Uhr bei dauerhaft wacher Lampe ohne WLAN nicht wegdriftet
        if (millis() - g_idleSinceMs >= RDS_RESYNC_AFTER_DONE_MS) g_state = RDS_STATE_OFF;
        break;
    case RDS_STATE_NO_SIGNAL:
        // Später erneut versuchen (Empfang kann sich ändern)
        if (millis() - g_idleSinceMs >= RDS_RETRY_NO_SIGNAL_MS) g_state = RDS_STATE_OFF;
        break;
    }
}

// Gibt RDS-Status als JSON zurück: Kanal, RSSI, Zustand (scanning/locked/done/…) und Zeit seit letzter Sync
String getRdsStatusJson() {
    JsonDocument doc;
    doc["enabled"] = g_settings != nullptr && g_settings->rdsClockEnabled;
    doc["present"] = s_modulePresent; // Modul beim Einschalten per I2C gefunden
    doc["channel"] = g_currentCh;

    char mhzBuf[8];
    snprintf(mhzBuf, sizeof(mhzBuf), "%.1f", g_currentCh / 10.0f);
    doc["channelMhz"] = mhzBuf;

    doc["rssi"] = (g_state == RDS_STATE_LOCKED || g_state == RDS_STATE_TUNING)
                  ? (int)getRssi() : 0;

    const char* statusStr;
    switch (g_state) {
    case RDS_STATE_POWERING:
    case RDS_STATE_TUNING:    statusStr = "scanning";  break;
    case RDS_STATE_LOCKED:    statusStr = "locked";    break;
    case RDS_STATE_NO_SIGNAL: statusStr = "no_signal"; break;
    case RDS_STATE_DONE:      statusStr = "done";      break;
    default:                  statusStr = "off";       break;
    }
    doc["status"] = statusStr;
    doc["lastSyncMs"] = g_lastSyncMs > 0 ? (unsigned long)(millis() - g_lastSyncMs) : 0;

    String out;
    serializeJson(doc, out);
    return out;
}
