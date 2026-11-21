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
  // Minuten: Hotspot-Laufzeit ohne Bedienung (0 = unbegrenzt), Zeitabgleich nach Erfolg / Fehlschlag
  apTimeoutMin: 10,
  syncIntervalMin: 300,
  syncRetryMin: 300,
};

// Feldgruppen mit eigenem Speichern-Knopf: WLAN-Sektion bzw. Rubrik Synchronisation (System)
export const WIFI_KEYS = ["internalSSID", "internalPW", "externalSSID", "externalPW", "externalSSID2", "externalPW2", "apTimeoutMin"];
export const SYNC_KEYS = ["syncIntervalMin", "syncRetryMin"];

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

    // Sendet die Felder `keys` (WIFI_KEYS oder SYNC_KEYS); true bei Erfolg. Fehlende Felder behält die
    // Lampe, ungespeicherte Eingaben der anderen Gruppe bleiben stehen. Passwort-Maske = unverändert,
    // leer = entfernen.
    async post(keys = Object.keys(defaultSystemSettings)) {
      try {
        const currentSettings = get(systemStoreData);
        // Schalter, Persönliches und Debug haben eigene Speicherwege, sonst würde ein zwischenzeitlich
        // geänderter Wert mit dem alten Stand überschrieben
        const payload = {};
        for (const key of keys) payload[key] = currentSettings[key];
        await postJson("/set_system_config", payload);
        // Nach dem Speichern Passwörter wie vom Gerät maskiert zeigen (leer = kein Passwort)
        const saved = { ...currentSettings };
        for (const pw of ["internalPW", "externalPW", "externalPW2"]) {
          if (keys.includes(pw) && saved[pw]) saved[pw] = "******";
        }
        systemStoreData.set(saved);
        for (const key of keys) originalSystemSettings[key] = saved[key];
        return true;
      } catch (error) {
        messageStore.show("error", get(t)("system.saveFailed", { msg: error.message }));
        return false;
      }
    },

    // Setzt ein Feld auf den zuletzt gespeicherten Wert zurück (z. B. nach ungültiger Eingabe)
    revertField(field) {
      systemStoreData.update((settings) => ({ ...settings, [field]: originalSystemSettings[field] }));
    },

    updateField(field, value) {
      systemStoreData.update((settings) => ({
        ...settings,
        [field]: value,
      }));
    },

    // true, wenn sich eines der Felder `keys` (Standard: alle) vom gespeicherten Stand unterscheidet
    hasChanges(keys = Object.keys(defaultSystemSettings)) {
      const cur = get(systemStoreData);
      return keys.some((k) => JSON.stringify(cur[k]) !== JSON.stringify(originalSystemSettings[k]));
    },
  };
}

export const systemStore = createSystemStore();
