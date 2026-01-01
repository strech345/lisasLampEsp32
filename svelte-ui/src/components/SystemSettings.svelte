<script>
  import { onMount } from "svelte";
  import { messageStore } from "../stores/messageStore.js";
  import { systemStore } from "../stores/systemStore.js";

  export let show = false;
  $: hasChanges = $systemStore && systemStore.hasChanges();

  onMount(() => {
    if (show) {
      systemStore.load();
    }
  });

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

  // Load system settings and status when modal opens
  $: if (show) {
    systemStore.load();
    loadStatusInfo();
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
          type="text"
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
          type="text"
          id="externalPW"
          value={$systemStore.externalPW}
          on:input={(e) => handleFieldChange("externalPW", e)}
        />
      </form>
      <footer>
        <div class="footer-left">
          {#if statusInfo}
            <div class="status-info">
              <div class="status-line">
                <span class="status-label">WiFi Status:</span>
                <span class="status-value {statusInfo.lastTestResult}">
                  {#if statusInfo.lastTestResult === "success"}
                    Connected & Synced
                  {:else if statusInfo.lastTestResult === "connection_failed"}
                    Connection Failed
                  {:else if statusInfo.lastTestResult === "ntp_failed"}
                    Connected, No Time Sync
                  {:else}
                    Unknown
                  {/if}
                </span>
              </div>
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

  .status-value.connection_failed,
  .status-value.ntp_failed {
    color: #ef4444;
  }
</style>
