// Store für die Systemeinstellungen (WLAN/Hotspot) inkl. Laden und Speichern über lib/api.js.
import { get, writable } from "svelte/store";
import { getJson, postJson } from "../lib/api.js";
import { messageStore } from "./messageStore.js";
import { t } from "../lib/i18n.js";

// Name der Lampe (Titel der Seite); wird beim Verbinden und nach dem Speichern aktualisiert
export const lampName = writable("Liza");
// Party-Tage [{month, day}] (Ballons auf der Hauptseite); Monat 1–12
export const celebrations = writable([]);

// Lädt Name und Party-Tage der Lampe (für Titel und Ballons)
export async function loadLampName() {
  try {
    const s = await getJson("/get_system_config");
    if (s.lampName) lampName.set(s.lampName);
    if (Array.isArray(s.celebrations)) celebrations.set(s.celebrations);
  } catch (e) {
    // Titel behält den bisherigen Namen
  }
}

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
        // Nur WLAN-Felder übernehmen (Änderungsanzeige der WLAN-Sektion)
        const merged = { ...defaultSystemSettings };
        for (const key of Object.keys(defaultSystemSettings)) if (key in settings) merged[key] = settings[key];
        originalSystemSettings = JSON.parse(JSON.stringify(merged));
        systemStoreData.set(merged);
      } catch (error) {
        messageStore.show("error", get(t)("system.loadFailed", { msg: error.message }));
      }
    },

    // Sendet die Systemeinstellungen; true bei Erfolg. Maske/leer = unverändert (Firmware-Regel).
    async post() {
      try {
        const currentSettings = get(systemStoreData);
        // Nur WLAN-Felder senden: Schalter, Persönliches und Debug haben eigene Speicherwege, sonst
        // würde ein zwischenzeitlich geänderter Wert mit dem alten Stand überschrieben
        const payload = {};
        for (const key of Object.keys(defaultSystemSettings)) payload[key] = currentSettings[key];
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
        messageStore.show("error", get(t)("system.saveFailed", { msg: error.message }));
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
