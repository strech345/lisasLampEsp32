// Mock data for development/testing
// To enable mocking, add ?mock=true to the URL

export const mockConfig = {
  sets: [
    { colorMode: 1, animationMode: 0, color: { r: 255, g: 255, b: 255 } },
    { colorMode: 2, animationMode: 1, color: { r: 255, g: 255, b: 255 } },
    { colorMode: 4, animationMode: 2, color: { r: 255, g: 255, b: 255 } },
    { colorMode: 3, animationMode: 0, color: { r: 120, g: 80, b: 200 } },
  ],
  activeSet: 1,
  alarms: [
    { day: 1, hour: 7, minute: 30, active: true },
    { day: 2, hour: 8, minute: 0, active: true },
    { day: 5, hour: 6, minute: 45, active: false },
    { day: 0, hour: 9, minute: 15, active: true, once: true, date: "2026-12-24" },
    { day: 0, hour: 6, minute: 0, active: true, once: true, date: "2026-01-02" },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
    { day: 0, hour: 0, minute: 0, active: false },
  ],
  goodNightDuration: 30,
  alarmDuration: 30,
  animationSpeed: 200,
};

export const mockSystemSettings = {
  internalSSID: "Liza_Lamp_Mock",
  internalPW: "******",
  externalSSID: "Home_WiFi_Mock",
  externalPW: "******",
  externalSSID2: "",
  externalPW2: "",
  warningsEnabled: true,
  deepSleepEnabled: true,
  tsfFallbackEnabled: true,
  rdsClockEnabled: false,
  rtcCrystalEnabled: false,
  ledCount: 4,
  lampName: "Liza",
  celebrations: [{ month: 1, day: 1 }, { month: 8, day: 8 }, { month: 9, day: 2 }, { month: 12, day: 24 }],
  statusLedEnabled: true,
  serialLogEnabled: true,
  eventLogEnabled: true,
  logToUsb: false,
  apTimeoutMin: 10,
  syncIntervalMin: 300,
  syncRetryMin: 300,
};

// Check if mocking is enabled via URL parameter
export const isMockEnabled = () => {
  if (typeof window !== "undefined") {
    const params = new URLSearchParams(window.location.search);
    return params.get("mock") === "true";
  }
  return false;
};

// Mock fetch function
export const mockFetch = (url, options) => {
  console.log("🎭 Mock API call:", url, options);

  return new Promise((resolve) => {
    setTimeout(() => {
      if (url.includes("/get_config")) {
        resolve({
          ok: true,
          json: () => Promise.resolve(mockConfig),
        });
      } else if (url.includes("/get_system_config")) {
        resolve({
          ok: true,
          json: () => Promise.resolve(mockSystemSettings),
        });
      } else if (url.includes("/get_wifi_scan")) {
        resolve({ ok: true, json: () => Promise.resolve({ status: "done", networks: [
          { ssid: "Home_WiFi_Mock", rssi: -52, secure: true },
          { ssid: "Nachbar", rssi: -71, secure: true },
          { ssid: "Cafe offen", rssi: -84, secure: false },
        ] }) });
      } else if (url.includes("/get_tsf_fallback")) {
        resolve({ ok: true, json: () => Promise.resolve({ enabled: false, storedAps: 16, lastSnapshotMs: 3600000,
          lastCalcResult: "success", lastCalcAps: 5, lastCalcDeviationMs: 4, lastCalcMaxDevMs: 12,
          checkAps: 7, checkMaxDevMs: 38, checkSpanS: 18120,
          storedNtpAps: 12, storedDriftAps: 9, lastCalcGen: 0, snapshotAllowed: false,
          aps: [{ id: "3a:4f:c1", gen: 0, drift: 12200, hist: [12400, 12100, 13000, 11900] },
                { id: "9e:02:7b", gen: 0, drift: -8150, hist: [-8300, -8000] },
                { id: "51:aa:10", gen: 1, drift: 3000, hist: [3000] },
                { id: "c4:e9:84", gen: 0, hist: [] }] }) });
      } else if (url.includes("/get_wifi_test")) {
        resolve({ ok: true, json: () => Promise.resolve({ status: "done", result: "wrong_password", slot: 1 }) });
      } else if (options && options.method === "POST") {
        resolve({
          ok: true,
          text: () => Promise.resolve("Mock save successful"),
        });
      } else if (url.includes("/ping")) {
        resolve({ ok: true, text: () => Promise.resolve("pong") });
      } else if (url.includes("/get_status")) {
        resolve({ ok: true, json: () => Promise.resolve({ clockSynced: true, staConfigValid: false, staActive: false, lastSyncOk: true, everSynced: true, nextSyncInMs: 17880000, timeSinceLastSucceededTestMs: 120000, systemTime: Math.floor(Date.now() / 1000) }) });
      } else if (url.includes("/get_system_info")) {
        // Leicht schwankende Zufallswerte für den Systemmonitor
        const r = (a, b) => a + Math.random() * (b - a);
        resolve({ ok: true, json: () => Promise.resolve({
          uptimeMs: Math.floor(performance.now()) + 90061000, cpuLoad: r(5, 35),
          heapTotal: 320000, heapFree: Math.floor(r(150000, 190000)), heapMinFree: 120000, heapMaxAlloc: 110000,
          fsTotal: 1441792, fsUsed: Math.floor(r(400000, 420000)),
          sketchSize: 1100000, sketchFree: 900000, flashSize: 4194304,
          rssi: Math.floor(r(-70, -55)),
          apClients: [
            { ip: "192.168.4.2", rssi: Math.floor(r(-60, -50)), you: true },
            { ip: "192.168.4.3", rssi: Math.floor(r(-78, -68)), you: false },
          ], tempC: r(38, 44), resetReason: "poweron",
          clock: { now: Math.floor(Date.now() / 1000), ntpAt: Math.floor(Date.now() / 1000) - 1260, rdsAt: 0,
            tsfAt: Math.floor(Date.now() / 1000) - 7400, lastSyncOk: true, staActive: false, nextSyncInMs: 17880000 },
        }) });
      } else if (url.includes("/get_event_log")) {
        resolve({ ok: true, json: () => Promise.resolve({ uptimeMs: 60000, unixTime: 0, events: [] }) });
      } else if (/\/get_(tsf_fallback|rds_clock|rtc_crystal)/.test(url)) {
        resolve({ ok: true, json: () => Promise.resolve({ enabled: false, present: false, available: false }) });
      } else {
        resolve({
          ok: false,
          status: 404,
          statusText: "Mock endpoint not found",
        });
      }
    }, 200); // Simulate network delay
  });
};
