<script>
  import { onMount } from "svelte";
  import { messageStore } from "../stores/messageStore.js";
  import { systemStore } from "../stores/systemStore.js";
  import EventLog from "./EventLog.svelte";

  let showLogs = false;

  export let show = false;
  $: hasChanges = $systemStore && systemStore.hasChanges();

  let warningsEnabled = true;
  let deepSleepEnabled = true;

  onMount(() => {
    if (show) {
      systemStore.load();
      loadWarnings();
      loadDeepSleep();
    }
  });

  async function loadWarnings() {
    try {
      const res = await fetch("/get_warnings");
      if (res.ok) {
        const data = await res.json();
        warningsEnabled = data.enabled;
      }
    } catch {}
  }

  async function handleWarningsToggle() {
    try {
      await fetch("/set_warnings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled: warningsEnabled }),
      });
    } catch {}
  }

  async function loadDeepSleep() {
    try {
      const res = await fetch("/get_deep_sleep");
      if (res.ok) {
        const data = await res.json();
        deepSleepEnabled = data.enabled;
      }
    } catch {}
  }

  async function handleDeepSleepToggle() {
    try {
      await fetch("/set_deep_sleep", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled: deepSleepEnabled }),
      });
    } catch {}
  }

  let statusInfo = null;

  async function handleSave() {
    const success = await systemStore.post();
    if (success) {
      messageStore.show(
        "success",
        "System settings saved! Please wait a moment and reconnect to the WiFi network."
      );
    show = false;
    }
  }

  function handleCancel() {
    show = false;
  }

  function handleFieldChange(field, event) {
    systemStore.updateField(field, event.target.value);
  }

  let restarting = false;

  async function handleRestart() {
    if (!confirm("ESP32 wirklich neu starten?")) return;
    restarting = true;
    try {
      await fetch("/restart", { method: "POST" });
    } catch {
      // Device reboots before response arrives — expected.
    }
    messageStore.show("success", "ESP32 startet neu…");
    show = false;
  }

  let firmwareFiles = null;
  let otaUploading = false;

  async function handleOtaUpload() {
    if (!firmwareFiles || firmwareFiles.length === 0) return;
    const formData = new FormData();
    formData.append("firmware", firmwareFiles[0]);
    otaUploading = true;
    try {
      const res = await fetch("/ota", { method: "POST", body: formData });
      const data = await res.json();
      if (data.status === "ok") {
        messageStore.show("success", "Firmware uploaded! Device is rebooting...");
        show = false;
      } else {
        messageStore.show("error", "Update failed: " + (data.message || "unknown error"));
      }
    } catch {
      // Device may reboot before the response arrives — treat as success.
      messageStore.show("success", "Firmware uploaded! Device is rebooting...");
      show = false;
    } finally {
      otaUploading = false;
    }
  }

  async function loadStatusInfo() {
    try {
      const response = await fetch("/get_status");
      if (response.ok) {
        const rawStatus = await response.json();

        // Format time since last test from backend milliseconds
        const timeSinceLastTest = rawStatus.timeSinceLastSucceededTestMs;

        let lastTestTime;
        if (timeSinceLastTest < 60000) {
          lastTestTime = Math.floor(timeSinceLastTest / 1000) + " seconds ago";
        } else if (timeSinceLastTest < 3600000) {
          lastTestTime = Math.floor(timeSinceLastTest / 60000) + " minutes ago";
        } else {
          lastTestTime = Math.floor(timeSinceLastTest / 3600000) + " hours ago";
        }

        // Map WiFi test result enum values (from types.h)
        const WIFI_TEST_SUCCESS = 0;
        const WIFI_TEST_CONNECTION_FAILED = 1;
        const WIFI_TEST_NTP_FAILED = 2;

        let lastTestResultStr;
        switch (rawStatus.lastTestResult) {
          case WIFI_TEST_SUCCESS:
            lastTestResultStr = "success";
            break;
          case WIFI_TEST_CONNECTION_FAILED:
            lastTestResultStr = "connection_failed";
            break;
          case WIFI_TEST_NTP_FAILED:
            lastTestResultStr = "ntp_failed";
            break;
          default:
            lastTestResultStr = "unknown";
            break;
        }

        // Format systemTime (epoch seconds) to local date/time string
        let formattedSystemTime = "";
        if (rawStatus.systemTime && !isNaN(rawStatus.systemTime)) {
          const date = new Date(rawStatus.systemTime * 1000);
          formattedSystemTime = date.toLocaleString();
        }

        statusInfo = {
          ...rawStatus,
          lastTestTime,
          lastTestResult: lastTestResultStr,
          systemTime: formattedSystemTime,
        };
      }
    } catch (error) {
      console.error("Failed to load status:", error);
    }
  }

  // Load system settings, status and warnings when modal opens
  $: if (show) {
    systemStore.load();
    loadStatusInfo();
    loadWarnings();
    loadDeepSleep();
  }
</script>

{#if show}
  <dialog open>
    <article>
      <header>
        <button aria-label="Close" class="close" on:click={handleCancel}
        ></button>
        <h2>System Settings</h2>
      </header>
      <form on:submit|preventDefault={handleSave}>
        <label for="internalSSID">Internal SSID</label>
        <input
          type="text"
          id="internalSSID"
          value={$systemStore.internalSSID}
          on:input={(e) => handleFieldChange("internalSSID", e)}
        />

        <label for="internalPW">Internal Password</label>
        <input
          type="password"
          id="internalPW"
          value={$systemStore.internalPW}
          on:input={(e) => handleFieldChange("internalPW", e)}
        />

        <label for="externalSSID">External SSID</label>
        <input
          type="text"
          id="externalSSID"
          value={$systemStore.externalSSID}
          on:input={(e) => handleFieldChange("externalSSID", e)}
        />

        <label for="externalPW">External Password</label>
        <input
          type="password"
          id="externalPW"
          value={$systemStore.externalPW}
          on:input={(e) => handleFieldChange("externalPW", e)}
        />

        <label for="externalSSID2">External SSID 2 <span class="optional">(Fallback)</span></label>
        <input
          type="text"
          id="externalSSID2"
          value={$systemStore.externalSSID2}
          placeholder="optional"
          on:input={(e) => handleFieldChange("externalSSID2", e)}
        />

        <label for="externalPW2">External Password 2</label>
        <input
          type="password"
          id="externalPW2"
          value={$systemStore.externalPW2}
          placeholder="optional"
          on:input={(e) => handleFieldChange("externalPW2", e)}
        />
      </form>

      <hr />
      <section class="warnings-section">
        <label class="warnings-label">
          <input
            type="checkbox"
            bind:checked={deepSleepEnabled}
            on:change={handleDeepSleepToggle}
          />
          Deep sleep enabled
        </label>
        <small>ESP32 schläft zwischen Alarmen. Wacht 5 min + Drift-Puffer vor jedem Event auf.</small>
      </section>

      <section class="warnings-section">
        <label class="warnings-label">
          <input
            type="checkbox"
            bind:checked={warningsEnabled}
            on:change={handleWarningsToggle}
          />
          LED warnings enabled
        </label>
        <small>When active, pixel 0 blinks red (no time) or yellow (stale NTP sync).</small>
      </section>

      <hr />
      <section class="restart-section">
        <button
          class="btn-small btn-danger"
          type="button"
          disabled={restarting}
          on:click={handleRestart}
        >
          {restarting ? "Startet neu…" : "ESP32 neu starten"}
        </button>
      </section>

      <hr />
      <section class="log-section">
        <button
          class="log-toggle"
          type="button"
          on:click={() => (showLogs = !showLogs)}
        >
          {showLogs ? "▾" : "▸"} Event Log
        </button>
        {#if showLogs}
          <EventLog />
        {/if}
      </section>

      <hr />
      <section class="ota-section">
        <h3>Firmware Update</h3>
        <div class="ota-row">
          <input
            type="file"
            accept=".bin"
            on:change={(e) => (firmwareFiles = e.target.files)}
          />
          <button
            class="btn-small btn-blue"
            type="button"
            disabled={!firmwareFiles || otaUploading}
            on:click={handleOtaUpload}
          >
            {otaUploading ? "Uploading…" : "Upload"}
          </button>
        </div>
      </section>

      <footer>
        <div class="footer-left">
          {#if statusInfo}
            <div class="status-info">
              <div class="status-line">
                <span class="status-label">Last Test:</span>
                <span class="status-value">{statusInfo.lastTestTime}</span>
              </div>
              {#if statusInfo.currentTime}
                <div class="status-line">
                  <span class="status-label">Current Time:</span>
                  <span class="status-value">{statusInfo.currentTime}</span>
                </div>
              {/if}
              <div class="status-line">
                <span class="status-label">STA Config Valid:</span>
                <span
                  class="status-value {statusInfo.staConfigValid
                    ? 'success'
                    : 'connection_failed'}"
                >
                  {statusInfo.staConfigValid ? "Yes" : "No"}
                </span>
              </div>
              <div class="status-line">
                <span class="status-label">System Time:</span>
                <span class="status-value">{statusInfo.systemTime}</span>
              </div>
              <div class="status-line">
                <span class="status-label">Clock Synced:</span>
                <span
                  class="status-value {statusInfo.clockSynced
                    ? 'success'
                    : 'connection_failed'}"
                >
                  {statusInfo.clockSynced ? "Yes" : "No"}
                </span>
              </div>
            </div>
          {/if}
        </div>
        <div class="footer-right">
          <button
            class="btn-small btn-blue"
            type="button"
            on:click={handleCancel}>Cancel</button
          >
          <button
            class="btn-small btn-blue"
            type="button"
            on:click={handleSave}
            disabled={!hasChanges}>Save</button
          >
        </div>
      </footer>
    </article>
  </dialog>
{/if}

<style>
  dialog {
    max-width: 500px;
  }

  .close {
    background: none;
    border: none;
    font-size: 1.5rem;
    cursor: pointer;
    float: right;
  }

  .close::before {
    content: "×";
  }

  form {
    margin-bottom: 1rem;
  }

  .optional {
    font-size: 0.75rem;
    font-weight: normal;
    color: var(--pico-muted-color, #888);
  }

  form > label {
    margin-top: 1rem;
    margin-bottom: 0.5rem;
    display: block;
  }

  form > input {
    margin-bottom: 1rem;
  }

  footer {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 1rem;
  }

  .footer-left {
    display: flex;
    align-items: center;
    gap: 1rem;
  }

  .footer-right {
    display: flex;
    gap: 1rem;
  }

  .status-info {
    font-size: 0.75rem;
    color: var(--secondary-text-color);
  }

  .status-line {
    display: flex;
    justify-content: space-between;
    gap: 0.5rem;
    margin-bottom: 0.25rem;
  }

  .status-label {
    font-weight: 500;
    min-width: 80px;
  }

  .status-value {
    color: var(--text-color);
  }

  .status-value.success {
    color: #22c55e;
  }

  .status-value.connection_failed {
    color: #ef4444;
  }

  .warnings-section {
    margin-bottom: 0.5rem;
  }

  .warnings-label {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    font-weight: 500;
    cursor: pointer;
  }

  .warnings-section small {
    display: block;
    margin-top: 0.25rem;
    font-size: 0.75rem;
    color: var(--secondary-text-color, #888);
  }

  .restart-section {
    margin-bottom: 0.25rem;
  }

  :global(.btn-danger) {
    background-color: #dc2626 !important;
    border-color: #dc2626 !important;
    color: white !important;
  }

  :global(.btn-danger:hover) {
    background-color: #b91c1c !important;
    border-color: #b91c1c !important;
  }

  :global(.btn-danger:disabled) {
    background-color: #fca5a5 !important;
    border-color: #fca5a5 !important;
    cursor: not-allowed !important;
  }

  .log-section {
    margin-bottom: 0.5rem;
  }

  .log-toggle {
    background: none;
    border: none;
    cursor: pointer;
    font-size: 0.85rem;
    font-weight: 600;
    padding: 0;
    color: inherit;
  }

  .ota-section h3 {
    font-size: 0.9rem;
    margin-bottom: 0.5rem;
  }

  .ota-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .ota-row input[type="file"] {
    flex: 1;
    font-size: 0.85rem;
  }
</style>
