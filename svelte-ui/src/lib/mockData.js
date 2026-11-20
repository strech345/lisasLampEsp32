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
      } else if (options && options.method === "POST") {
        resolve({
          ok: true,
          text: () => Promise.resolve("Mock save successful"),
        });
      } else if (url.includes("/ping")) {
        resolve({ ok: true, text: () => Promise.resolve("pong") });
      } else if (url.includes("/get_status")) {
        resolve({ ok: true, json: () => Promise.resolve({ clockSynced: true, staConfigValid: false, timeSinceLastSucceededTestMs: 120000, systemTime: Math.floor(Date.now() / 1000) }) });
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
