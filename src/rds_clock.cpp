// RDS-Uhrzeit-Fallback — Implementierung
// Steuert RDA5807M-FM-Chip per I2C, scannt das Band nach einem empfangbaren Sender
// und wertet RDS-Group-4A (Clocktime & Date) aus um die Systemzeit zu setzen.
#include "rds_clock.h"
#include "store.h"
#include "debug_utils.h"
#include "system_utils.h"
#include "event_log.h"
#include <Wire.h>
#include <WiFi.h>
#include <sys/time.h>
#include <time.h>
#include <ArduinoJson.h>

// GPIO-Belegung ESP32-C3: 1(CLK), 2(DT), 3(SW), 7(LED), 8(STATUS_LED), 9(BOOT)
// Passe diese Pins an deine Hardware an:
#define RDS_POWER_PIN 6  // GPIO 6: steuert Modul-Stromversorgung (HIGH = an)
#define RDS_SDA_PIN   4  // GPIO 4: I2C SDA
#define RDS_SCL_PIN   5  // GPIO 5: I2C SCL

#define RDA5807M_ADDR  0x11  // Random-access I2C-Adresse

#define FM_START_10KHZ  8750
#define FM_END_10KHZ   10800
#define FM_STEP_10KHZ     10

#define RSSI_THRESHOLD   25
#define CT_WAIT_MS    35000
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

// Schreibt ein 16-Bit-Register des RDA5807M per I2C (Big-Endian)
static void writeReg(uint8_t reg, uint16_t val) {
    Wire.beginTransmission(RDA5807M_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(val >> 8));
    Wire.write((uint8_t)(val & 0xFF));
    Wire.endTransmission();
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

// Initialisiert den RDA5807M: Soft-Reset, RDS aktivieren, 50-kHz-Suchlaufschritt
static void chipInit() {
    writeReg(REG_CONFIG, 0xD001 | (1 << 3) | (1 << 2));
    delay(50);
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

// Analysiert RDS-Blöcke B/C/D auf Group 4A (Clocktime): dekodiert MJD-Datum,
// Stunden, Minuten und UTC-Offset und setzt die Systemzeit wenn plausibel
static bool tryParseCt(uint16_t blockB, uint16_t blockC, uint16_t blockD) {
    if (((blockB >> 12) & 0x0F) != 4 || ((blockB >> 11) & 0x01) != 0) return false;

    unsigned long mjd = ((unsigned long)(blockB & 0x3) << 15) | (blockC >> 1);
    uint8_t hours    = (uint8_t)(((blockC & 0x1) << 4) | (blockD >> 12));
    uint8_t minutes  = (blockD >> 6) & 0x3F;
    int8_t  offset   = (int8_t)(blockD & 0x1F);
    if (blockD & 0x20) offset = -offset;

    if (hours > 23 || minutes > 59) return false;

    long unixDay = (long)mjd - 40587L;
    if (unixDay < 0) return false;
    time_t unixUtc = (time_t)((long long)unixDay * 86400LL
                              + hours * 3600L + minutes * 60L
                              - (long)offset * 1800L);

    struct tm* t = gmtime(&unixUtc);
    if (t == nullptr || t->tm_year + 1900 < 2021) return false;

    struct timeval tv = { unixUtc, 0 };
    settimeofday(&tv, nullptr);
    return true;
}

// Liest RDS-Blöcke aus und versucht einen CT-Parse; gibt true zurück wenn Zeit gesetzt wurde
static bool checkRds() {
    if (!isRdsReady()) return false;
    return tryParseCt(readReg(REG_RDSB), readReg(REG_RDSC), readReg(REG_RDSD));
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
void initRdsClock(const SystemSettings* settings, WiFiTestTracker* tracker) {
    g_settings = settings;
    g_tracker  = tracker;

    pinMode(RDS_POWER_PIN, OUTPUT);
    digitalWrite(RDS_POWER_PIN, LOW);

    Wire.begin(RDS_SDA_PIN, RDS_SCL_PIN);

    uint16_t ch = settings->rdsLastChannel;
    serialPrint(ch > 0
        ? ("RDS: Init, letzter Kanal " + String(ch / 10.0f, 1) + " MHz")
        : String("RDS: Init, kein gespeicherter Kanal"));
}

// Zustandsmaschine: schaltet Modul ein, scannt Kanäle, wartet auf RDS-CT-Paket;
// stoppt sobald Uhrzeit gesetzt oder WiFi-Verbindung vorhanden ist
void rdsClockLoop() {
    if (g_settings == nullptr || !g_settings->rdsClockEnabled) {
        if (g_state != RDS_STATE_OFF && g_state != RDS_STATE_DONE) {
            digitalWrite(RDS_POWER_PIN, LOW);
            g_state = RDS_STATE_OFF;
        }
        return;
    }

    if (g_tracker != nullptr && (g_tracker->clockSynced || g_tracker->staConfigValid)) {
        if (g_state != RDS_STATE_OFF && g_state != RDS_STATE_DONE) {
            digitalWrite(RDS_POWER_PIN, LOW);
            g_state = RDS_STATE_OFF;
            logEvent("RDS: Modul aus");
        }
        return;
    }

    bool staActive = WiFi.getMode() == WIFI_MODE_APSTA;
    if (staActive) return;

    switch (g_state) {
    case RDS_STATE_OFF:
        digitalWrite(RDS_POWER_PIN, HIGH);
        g_stateMs = millis();
        g_state = RDS_STATE_POWERING;
        logEvent("RDS: Scan gestartet");
        break;

    case RDS_STATE_POWERING:
        if (millis() - g_stateMs < POWER_UP_MS) break;
        chipInit();
        g_currentCh = (g_settings->rdsLastChannel > 0)
                      ? g_settings->rdsLastChannel
                      : FM_START_10KHZ;
        g_scanStartCh = g_currentCh;
        chipTune(g_currentCh);
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
                digitalWrite(RDS_POWER_PIN, LOW);
                g_state = RDS_STATE_NO_SIGNAL;
                logEvent("RDS: kein Signal gefunden");
            } else {
                g_currentCh = next;
                chipTune(g_currentCh);
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
            snprintf(msg, sizeof(msg), "RDS: Zeit gesetzt %.1fMHz", g_currentCh / 10.0f);
            logEvent(msg);
            g_lastSyncMs = millis();
            digitalWrite(RDS_POWER_PIN, LOW);
            g_state = RDS_STATE_DONE;
            logEvent("RDS: Modul aus");
            break;
        }
        if (millis() - g_stateMs >= CT_WAIT_MS) {
            uint16_t next = nextChannel(g_currentCh);
            if (next == g_scanStartCh) {
                digitalWrite(RDS_POWER_PIN, LOW);
                g_state = RDS_STATE_NO_SIGNAL;
                logEvent("RDS: kein Signal gefunden");
            } else {
                g_currentCh = next;
                chipTune(g_currentCh);
                g_stateMs = millis();
                g_state = RDS_STATE_TUNING;
            }
        }
        break;
    }

    case RDS_STATE_DONE:
    case RDS_STATE_NO_SIGNAL:
        break;
    }
}

// Gibt RDS-Status als JSON zurück: Kanal, RSSI, Zustand (scanning/locked/done/…) und Zeit seit letzter Sync
String getRdsStatusJson() {
    JsonDocument doc;
    doc["enabled"] = g_settings != nullptr && g_settings->rdsClockEnabled;
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
