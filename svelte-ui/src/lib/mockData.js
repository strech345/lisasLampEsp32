// Mock data for development/testing
// To enable mocking, add ?mock=true to the URL

export const mockConfig = {
  override_color: { r: 120, g: 80, b: 200 },
  colorMode: 3, // Always set to custom mode
  alarms: [
    { day: 1, hour: 7, minute: 30, active: true },
    { day: 2, hour: 8, minute: 0, active: true },
    { day: 5, hour: 6, minute: 45, active: false },
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
  internalSSID: "Lisa_Lamp_Mock",
  internalPW: "mock_password",
  externalSSID: "Home_WiFi_Mock",
  externalPW: "home_password_123",
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
      } else if (
        url.includes("/set_config") ||
        url.includes("/set_system_config")
      ) {
        resolve({
          ok: true,
          text: () => Promise.resolve("Mock save successful"),
        });
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
