// Store für die Lampen-Konfiguration (Farbe, Alarme, Dauer) inkl. Laden/Speichern über lib/api.js.
import { get, writable } from "svelte/store";
import { getJson, postJson } from "../lib/api.js";
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
  goodNightDuration: 30,
  alarmDuration: 30,
  animationSpeed: 200,
};

function createConfigStore() {
  const configStoreData = writable(defaultConfig);
  const { subscribe, set, update } = configStoreData;
  let originalConfig = JSON.parse(JSON.stringify(defaultConfig));

  return {
    subscribe,

    // Lädt die Konfiguration vom Gerät
    async load() {
      try {
        const config = await getJson("/get_config");
        originalConfig = JSON.parse(JSON.stringify(config));
        set(config);
      } catch (error) {
        messageStore.show("error", `Konfiguration konnte nicht geladen werden: ${error.message}`);
      }
    },

    // Sendet die Konfiguration (ohne brightnessMode – der Drehknopf ist Master) und lädt danach neu
    async post() {
      const currentConfig = get(configStoreData);
      const { brightnessMode, ...payload } = currentConfig;
      try {
        await postJson("/set_config", payload);
        originalConfig = JSON.parse(JSON.stringify(currentConfig));
      } catch (error) {
        messageStore.show("error", `Speichern fehlgeschlagen: ${error.message}`);
      }
      // Auch bei Fehler neu laden, damit die UI den Gerätezustand zeigt
      this.load();
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

    setAnimationSpeed(speed) {
      update((config) => ({
        ...config,
        animationSpeed: speed,
      }));
      this.post();
    },
    
    updateDuration(type, value) {
      update((config) => ({
        ...config,
        [type]: value,
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
        messageStore.show("error", "Maximale Anzahl Alarme erreicht");
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
