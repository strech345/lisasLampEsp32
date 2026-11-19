// Store für die Systemeinstellungen (WLAN/Hotspot) inkl. Laden und Speichern über lib/api.js.
import { get, writable } from "svelte/store";
import { getJson, postJson } from "../lib/api.js";
import { messageStore } from "./messageStore.js";

const defaultSystemSettings = {
  internalSSID: "",
  internalPW: "",
  externalSSID: "",
  externalPW: "",
  externalSSID2: "",
  externalPW2: "",
  // Optionale "Passwort entfernen"-Flags (werden nur beim Speichern gesendet)
  clearInternalPW: false,
  clearExternalPW: false,
  clearExternalPW2: false,
};

function createSystemStore() {
  const systemStoreData = writable(defaultSystemSettings);
  let originalSystemSettings = JSON.parse(
    JSON.stringify(defaultSystemSettings)
  );

  return {
    subscribe: systemStoreData.subscribe,

    // Lädt die Systemeinstellungen vom Gerät
    async load() {
      try {
        const settings = await getJson("/get_system_config");
        const merged = { ...defaultSystemSettings, ...settings };
        originalSystemSettings = JSON.parse(JSON.stringify(merged));
        systemStoreData.set(merged);
      } catch (error) {
        messageStore.show("error", `Systemeinstellungen konnten nicht geladen werden: ${error.message}`);
      }
    },

    // Sendet die Systemeinstellungen; true bei Erfolg. Maske/leer = unverändert (Firmware-Regel).
    async post() {
      try {
        const currentSettings = get(systemStoreData);
        // Warn-/Sleep-Schalter haben eigene Endpunkte → nicht mitsenden, sonst würde ein
        // zwischenzeitlich umgeschalteter Wert mit dem alten Stand überschrieben
        const { warningsEnabled, deepSleepEnabled, ...payload } = currentSettings;
        await postJson("/set_system_config", payload);
        // Nach dem Speichern: Clear-Flags zurücksetzen, Passwörter wie vom Gerät maskiert zeigen
        const saved = { ...currentSettings };
        for (const [pw, clear] of [["internalPW", "clearInternalPW"], ["externalPW", "clearExternalPW"], ["externalPW2", "clearExternalPW2"]]) {
          if (saved[clear]) saved[pw] = "";
          else if (saved[pw]) saved[pw] = "******";
          saved[clear] = false;
        }
        systemStoreData.set(saved);
        originalSystemSettings = JSON.parse(JSON.stringify(saved));
        return true;
      } catch (error) {
        messageStore.show("error", `Speichern fehlgeschlagen: ${error.message}`);
        return false;
      }
    },

    updateField(field, value) {
      systemStoreData.update((settings) => ({
        ...settings,
        [field]: value,
      }));
    },

    hasChanges() {
      let currentSettings = get(systemStoreData);
      return (
        JSON.stringify(currentSettings) !==
        JSON.stringify(originalSystemSettings)
      );
    },
  };
}

export const systemStore = createSystemStore();
