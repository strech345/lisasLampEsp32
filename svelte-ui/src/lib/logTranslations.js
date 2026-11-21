// Übersetzung der Ereignisprotokoll-Meldungen der Firmware (immer Englisch) ins Deutsche.
// Die Tabelle enthält exakte Texte und Muster mit Platzhaltern ($1, $2 …); unbekannte Meldungen
// bleiben unverändert. Jede logEvent()-Meldung in src/*.cpp muss hier abgedeckt sein.

// Zustandsnamen der Lampe (lampStateName() in lamp_state.cpp)
const STATE_NAMES = {
  "Default": "Standard",
  "Alarm": "Wecker",
  "Good Night": "Gute Nacht",
  "Error": "Fehler",
  "Success": "Erfolg",
  "Warning": "Warnung",
  "Sleep": "Schlaf",
  "Celebration": "Feier",
  "SysError: no time": "Systemfehler: keine Zeit",
  "SysWarning: stale NTP": "Systemwarnung: NTP veraltet",
  "AP waiting": "Hotspot wartet",
  "Unknown": "Unbekannt",
};

// Alarmtypen (alertTypeName() in alerts.cpp)
const ALERT_NAMES = { "Error": "Fehler", "Warning": "Warnung", "OK": "OK", "None": "Keine" };

// Exakte Meldungen
const EXACT = {
  "Boot": "Start",
  "WiFi started (long press)": "WLAN gestartet (langer Druck)",
  "Factory reset triggered": "Werkseinstellungen ausgelöst",
  "RTC: 32 kHz crystal active": "RTC: 32-kHz-Quarz aktiv",
  "RTC: no crystal, using RC oscillator": "RTC: Quarz nicht erkannt, RC-Oszillator",
  "RTC: setting changed, restarting": "RTC: Einstellung geändert, Neustart",
  "TSF: snapshot discarded, time invalid": "TSF: Snapshot verworfen, Uhrzeit ungültig",
  "TSF: fallback has no data": "TSF: Fallback ohne Daten",
  "TSF: invalid reference time": "TSF: ungültige Referenzzeit",
  "TSF: scan start failed": "TSF: Scan-Start fehlgeschlagen",
  "Hotspot off: 10 min idle": "Hotspot aus: 10 min ohne Bedienung",
  "Hotspot off: idle timeout": "Hotspot aus: Laufzeit ohne Bedienung abgelaufen",
  "Hotspot off (user action)": "Hotspot aus (Bedienung)",
  "NTP stale": "NTP veraltet",
  "AP activated (upgrade)": "Hotspot aktiviert (Upgrade)",
  "WiFi started (AP+STA)": "WLAN gestartet (Hotspot+STA)",
  "WiFi started (STA sync)": "WLAN gestartet (STA-Sync)",
  "Hotspot started": "Hotspot gestartet",
  "Hotspot started (STA stays)": "Hotspot gestartet (Heimnetz bleibt)",
  "STA started (sync, hotspot stays)": "Heimnetz für Zeitabgleich verbunden, Hotspot bleibt",
  "Hotspot off: no client": "Hotspot aus: 1 min kein Gerät",
  "Hotspot restart (settings)": "Hotspot neu gestartet (Einstellungen)",
  "STA restart (settings)": "Heimnetz neu verbunden (Einstellungen)",
  "STA off: sync timeout": "Heimnetz getrennt: Abgleich nach 1 min ohne Erfolg",
  "STA failed: sync timeout": "Abgleich fehlgeschlagen: 1 min ohne Erfolg → TSF rechnet",
  "STA failed: max retries": "Abgleich fehlgeschlagen: Heimnetz nicht erreichbar → TSF rechnet",
  "STA off after failed sync": "Heimnetz nach fehlgeschlagenem Abgleich getrennt",
  "WiFi stopped": "WLAN gestoppt",
  "STA disconnected": "STA getrennt",
  "STA off after NTP, hotspot stays": "Heimnetz nach NTP getrennt, Hotspot bleibt",
  "STA off after NTP": "Heimnetz nach NTP getrennt, WLAN aus",
  "STA connected, NTP sync": "STA verbunden, NTP-Sync",
  "NTP sync: no reference time": "NTP-Sync: ohne Referenzzeit",
  "NTP sync successful": "NTP-Sync erfolgreich",
  "NTP sync triggered via web": "NTP-Sync über Web ausgelöst",
  "RDS: module off": "RDS: Modul aus",
  "RDS: scan started": "RDS: Scan gestartet",
  "RDS: module not responding (I2C)": "RDS: Modul antwortet nicht (I2C)",
  "RDS: no signal found": "RDS: kein Signal gefunden",
  "Config updated via WiFi": "Konfiguration per WLAN geändert",
  "Debug settings changed": "Debug-Einstellungen geändert",
  "System settings updated via WiFi": "Systemeinstellungen per WLAN geändert",
  "Warnings enabled": "Warnungen aktiviert",
  "Warnings disabled": "Warnungen deaktiviert",
  "Deep sleep enabled": "Deep Sleep aktiviert",
  "Deep sleep disabled": "Deep Sleep deaktiviert",
  "TSF fallback enabled": "TSF-Fallback aktiviert",
  "TSF fallback disabled": "TSF-Fallback deaktiviert",
  "RDS clock enabled": "RDS-Uhr aktiviert",
  "RDS clock disabled": "RDS-Uhr deaktiviert",
  "Factory reset via web": "Werkseinstellungen über Web",
  "Restart via web": "Neustart über Web",
};

// Trennungsgründe des WLAN-Treibers (wifi_err_reason_t, esp_wifi_types.h) — die häufigen im Klartext
const DISCONNECT_REASONS = {
  1: "unbekannt",
  2: "Anmeldung abgelaufen",
  3: "vom Router abgemeldet",
  4: "Zuordnung abgelaufen",
  8: "selbst getrennt",
  15: "Schlüsselaustausch-Zeitüberschreitung (Passwort falsch?)",
  23: "Anmeldung abgelehnt",
  200: "Router-Signal verloren",
  201: "Netz nicht gefunden",
  202: "Anmeldung fehlgeschlagen (Passwort?)",
  203: "Zuordnung fehlgeschlagen",
  204: "Schlüsselaustausch-Zeitüberschreitung (Passwort falsch?)",
  205: "Verbindung fehlgeschlagen",
  210: "Netz mit passender Verschlüsselung nicht gefunden",
  211: "Netz unter der Mindest-Sicherheit",
  212: "Netz zu schwach",
};

// Muster mit Platzhaltern: [Regex, Ersatz-Funktion oder -Text]
const PATTERNS = [
  [/^Alert start: (.*)$/, (m) => `Hinweis Start: ${ALERT_NAMES[m[1]] ?? m[1]}`],
  [/^Alert stop: (.*)$/, (m) => `Hinweis Ende: ${ALERT_NAMES[m[1]] ?? m[1]}`],
  [/^State: (.*) -> (.*)$/, (m) => `Zustand: ${STATE_NAMES[m[1]] ?? m[1]} -> ${STATE_NAMES[m[2]] ?? m[2]}`],
  [/^Scene (\d+)$/, "Szene $1"],
  [/^LED count: (\d+)$/, "LED-Anzahl: $1"],
  [/^STA connect \(fallback\): (.*)$/, "STA verbinden (Fallback): $1"],
  [/^STA connect: (.*)$/, "STA verbinden: $1"],
  [/^STA disconnected: reason (\d+)$/, (m) => `STA getrennt: ${DISCONNECT_REASONS[m[1]] ?? "Grund"} (${m[1]})`],
  [/^NTP sync: drift ([+-]?\d+)ms$/, "NTP-Sync: Abweichung $1ms"],
  [/^TSF: snapshot (\d+) new, (\d+) replaced, (\d+) total$/, "TSF: Snapshot $1 neu, $2 ersetzt, $3 gesamt"],
  [/^TSF: too few APs \((\d+)\)$/, "TSF: zu wenige APs ($1)"],
  [/^TSF: check (\d+) APs max \+-(\d+)ms$/, "TSF: Genauigkeit $1 APs, max. ±$2ms"],
  [/^TSF: drift (\S+) ([+-][\d.]+)ppm (\d+)h avg ([+-][\d.]+)$/, "TSF: Gang AP …$1 gemessen $2 ppm über $3 h, verwendet $4 ppm"],
  [/^TSF: (\d+) entries rebased$/, "TSF: $1 Einträge neu basiert"],
  [/^TSF: time set, (\d+) APs \+-(\d+)ms$/, "TSF: Zeit gesetzt, $1 APs ±$2ms"],
  [/^TSF: time set, (\d+) APs gen (\d+) \+-(\d+)ms$/, (m) => `TSF: Zeit gesetzt, ${m[1]} APs ${m[2] === "0" ? "(NTP-Bezug)" : `(abgeleitet, Stufe ${m[2]})`} max. ±${m[3]}ms`],
  [/^RDS: time set ([\d.]+)MHz$/, "RDS: Zeit gesetzt $1MHz"],
];

// Übersetzt eine Firmware-Meldung ins Deutsche; unbekannte Meldungen kommen unverändert zurück
export function translateEvent(msg) {
  if (typeof msg !== "string") return msg;
  if (msg in EXACT) return EXACT[msg];
  for (const [regex, replacement] of PATTERNS) {
    const m = msg.match(regex);
    if (m) return typeof replacement === "function" ? replacement(m) : msg.replace(regex, replacement);
  }
  return msg;
}
