import { get, writable } from "svelte/store";
import { isMockEnabled, mockFetch } from "../lib/mockData.js";
import { messageStore } from "./messageStore.js";

const MAX_ALARMS = 10;

// Default config matching the original structure
const defaultConfig = {
  override_color: { r: 255, g: 255, b: 255 },
  colorMode: 1, // Default to Neutral White
  animationMode: 1,
  alarms: Array(MAX_ALARMS)
    .fill(null)
    .map(() => ({
      day: 0,
      hour: 0,
      minute: 0,
      active: false,
    })),
};

function createConfigStore() {
  const configStoreData = writable(defaultConfig);
  const { subscribe, set, update } = configStoreData;
  let originalConfig = JSON.parse(JSON.stringify(defaultConfig));

  return {
    subscribe,

    async load() {
      try {
        const fetchFn = isMockEnabled() ? mockFetch : fetch;
        const response = await fetchFn("/get_config");
        if (response.ok) {
          const config = await response.json();
          console.log("Fetched config:", config);
          originalConfig = JSON.parse(JSON.stringify(config));
          set(config);
        } else {
          messageStore.show(
            "error",
            `Error fetching configuration: ${response.statusText}`
          );
        }
      } catch (error) {
        console.error("Network error fetching configuration:", error);
        messageStore.show(
          "error",
          `Network error fetching configuration: ${error.message}`
        );
      }
    },

    async post() {
      try {
        const currentConfig = get(configStoreData);

        const fetchFn = isMockEnabled() ? mockFetch : fetch;
        const response = await fetchFn("/set_config", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(currentConfig),
        });

        if (response.ok) {
          originalConfig = JSON.parse(JSON.stringify(currentConfig));
          // Reload configuration from ESP32
          this.load();
        } else {
          const errorText = await response.text();
          messageStore.show(
            "error",
            `Error saving configuration: ${errorText}`
          );
          // Reload configuration from ESP32 even on error
          this.load();
        }
      } catch (error) {
        messageStore.show("error", `Network error: ${error.message}`);
        // Reload configuration from ESP32 even on error
        this.load();
      }
    },

    updateColor(r, g, b) {
      update((config) => ({
        ...config,
        override_color: { r, g, b },
      }));
      this.post();
    },

    /*  toggleColorOverride(active) {
      update((config) => ({
        ...config,
        colorMode: 1, // Always set to 1 as requested
      }));
      this.post();
    }, */

    setColorMode(modeId) {
      update((config) => ({
        ...config,
        colorMode: modeId,
      }));
      this.post();
    },

    setAnimationMode(modeId) {
      update((config) => ({
        ...config,
        animationMode: modeId,
      }));
      this.post();
    },

    addAlarm() {
      update((config) => {
        const alarms = [...config.alarms];
        for (let i = 0; i < MAX_ALARMS; i++) {
          if (alarms[i].day === 0) {
            alarms[i] = { day: 1, hour: 7, minute: 0, active: true };
            return { ...config, alarms };
          }
        }
        messageStore.show("error", "Maximum alarms reached");
        return config;
      });
      this.post();
    },

    updateAlarm(index, alarm) {
      update((config) => {
        const alarms = [...config.alarms];
        alarms[index] = alarm;
        return { ...config, alarms };
      });
      this.post();
    },

    removeAlarm(index) {
      update((config) => {
        const alarms = [...config.alarms];
        alarms[index] = { day: 0, hour: 0, minute: 0, active: false };
        return { ...config, alarms };
      });
      this.post();
    },

    hasChanges() {
      const currentConfig = get(configStoreData);
      return JSON.stringify(currentConfig) !== JSON.stringify(originalConfig);
    },
  };
}

export const configStore = createConfigStore();
