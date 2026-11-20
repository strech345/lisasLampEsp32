// Store für die Lampen-Konfiguration (Licht-Sets, Alarme, Dauer) inkl. Laden/Speichern über lib/api.js.
import { get, writable } from "svelte/store";
import { getJson, postJson } from "../lib/api.js";
import { messageStore } from "./messageStore.js";

const MAX_ALARMS = 10;
export const MAX_LIGHT_SETS = 6; // muss zur Firmware passen (include/types.h)

// Neues Set beim Hinzufügen: Neutralweiß, statisch
const NEW_SET = { colorMode: 1, animationMode: 0, color: { r: 255, g: 255, b: 255 } };

// Startwerte bis die Konfiguration vom Gerät geladen ist (entsprechen den Firmware-Standardwerten)
const defaultConfig = {
  sets: [
    { colorMode: 1, animationMode: 0, color: { r: 255, g: 255, b: 255 } },
    { colorMode: 2, animationMode: 1, color: { r: 255, g: 255, b: 255 } },
    { colorMode: 4, animationMode: 2, color: { r: 255, g: 255, b: 255 } },
  ],
  activeSet: 1,
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

    // Macht Set i zum aktiven Set
    setActiveSet(i) {
      update((config) => ({ ...config, activeSet: i }));
      this.post();
    },

    // Ändert Felder von Set i (z. B. { colorMode: 3 } oder { color: {r,g,b} })
    updateSet(i, patch) {
      update((config) => {
        const sets = config.sets.map((s, idx) => (idx === i ? { ...s, ...patch } : s));
        return { ...config, sets };
      });
      this.post();
    },

    // Hängt ein neues Set an (max. MAX_LIGHT_SETS)
    addSet() {
      update((config) => {
        if (config.sets.length >= MAX_LIGHT_SETS) return config;
        return { ...config, sets: [...config.sets, { ...NEW_SET, color: { ...NEW_SET.color } }] };
      });
      this.post();
    },

    // Löscht Set i (mindestens eines bleibt); das aktive Set rückt bei Bedarf nach
    removeSet(i) {
      update((config) => {
        if (config.sets.length <= 1) return config;
        const sets = config.sets.filter((_, idx) => idx !== i);
        let activeSet = config.activeSet;
        if (i < activeSet || activeSet >= sets.length) activeSet = Math.max(0, activeSet - 1);
        return { ...config, sets, activeSet };
      });
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
