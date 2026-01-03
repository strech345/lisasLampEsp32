import { get, writable } from "svelte/store";
import { isMockEnabled, mockFetch } from "../lib/mockData.js";
import { messageStore } from "./messageStore.js";

const defaultSystemSettings = {
  internalSSID: "",
  internalPW: "",
  externalSSID: "",
  externalPW: "",
};

function createSystemStore() {
  const systemStoreData = writable(defaultSystemSettings);
  let originalSystemSettings = JSON.parse(
    JSON.stringify(defaultSystemSettings)
  );

  return {
    subscribe: systemStoreData.subscribe,

    async load() {
      try {
        const fetchFn = isMockEnabled() ? mockFetch : fetch;
        const response = await fetchFn("/get_system_config");
        if (response.ok) {
          const settings = await response.json();
          originalSystemSettings = JSON.parse(JSON.stringify(settings));
          systemStoreData.set(settings);
        } else {
          messageStore.show("error", "Failed to load system settings.");
        }
      } catch (error) {
        messageStore.show("error", `Error: ${error.message}`);
      }
    },

    async post() {
      try {
        const currentSettings = get(systemStoreData);

        const fetchFn = isMockEnabled() ? mockFetch : fetch;
        const response = await fetchFn("/set_system_config", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(currentSettings),
        });

        if (response.ok) {
          originalSystemSettings = JSON.parse(JSON.stringify(currentSettings));
          //messageStore.show("success", "System settings saved!");
          return true;
        } else {
          const errorText = await response.text();
          messageStore.show(
            "error",
            `Failed to save system settings: ${errorText}`
          );
          return false;
        }
      } catch (error) {
        messageStore.show("error", `Error: ${error.message}`);
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
