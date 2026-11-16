<script>
  import { onMount } from "svelte";
  import { messageStore } from "../stores/messageStore.js";
  import { systemStore } from "../stores/systemStore.js";
  import EventLog from "./EventLog.svelte";

  let showLogs = false;
  let openSection = null; // Accordion: nur eine Sektion offen

  function toggleSection(name) {
    openSection = openSection === name ? null : name;
  }

  export let show = false;
  $: hasChanges = $systemStore && systemStore.hasChanges();

  let warningsEnabled = true;
  let deepSleepEnabled = true;
  let tsfFallbackEnabled = true;
  let tsfStatus = null;
  let rdsClockEnabled = false;
  let rdsStatus = null;
  let rdsRefreshInterval = null;
  let rtcCrystalEnabled = false;
  let rtcCrystalStatus = null;
  let rtcCrystalChanged = false;
  let authTokenCount = 0;
  let articleEl;

  // Pull-to-Refresh im Dialog verhindern (overscroll-behavior wird in manchen WebViews ignoriert)
  function preventDialogPullRefresh(e) {
    const el = articleEl;
    if (!el) return;
    const dy = e.touches[0].clientY - (el._ptr_startY || e.touches[0].clientY);
    if (el.scrollTop === 0 && dy > 0) e.preventDefault();
    else if (el.scrollTop + el.clientHeight >= el.scrollHeight && dy < 0) e.preventDefault();
  }
  function trackDialogTouch(e) {
    if (articleEl) articleEl._ptr_startY = e.touches[0].clientY;
  }

  onMount(() => {
    if (show) {
      systemStore.load();
      loadWarnings();
      loadDeepSleep();
    }
    return () => clearInterval(rdsRefreshInterval);
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

  async function loadTsfStatus() {
    try {
      const res = await fetch("/get_tsf_fallback");
      if (res.ok) {
        const data = await res.json();
        tsfFallbackEnabled = data.enabled;
        tsfStatus = data;
      }
    } catch {}
  }

  async function handleTsfToggle() {
    try {
      await fetch("/set_tsf_fallback", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled: tsfFallbackEnabled }),
      });
    } catch {}
  }

  async function handleTsfSnapshot() {
    try {
      await fetch("/trigger_tsf_snapshot", { method: "POST" });
      setTimeout(loadTsfStatus, 3000);
    } catch {}
  }

  async function loadRtcCrystalStatus() {
    try {
      const res = await fetch("/get_rtc_crystal");
      if (res.ok) {
        const data = await res.json();
        rtcCrystalEnabled = data.enabled;
        rtcCrystalStatus = data;
      }
    } catch {}
  }

  async function handleRtcCrystalToggle() {
    rtcCrystalChanged = true;
  }

  async function applyRtcCrystal() {
    try {
      await fetch("/set_rtc_crystal", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled: rtcCrystalEnabled }),
      });
      messageStore.show("success", "RTC-Quarz-Einstellung gespeichert. ESP32 startet neu…");
      show = false;
      setTimeout(() => location.reload(), 6000);
    } catch {}
  }

  async function loadRdsStatus() {
    try {
      const res = await fetch("/get_rds_clock");
      if (res.ok) {
        const data = await res.json();
        rdsClockEnabled = data.enabled;
        rdsStatus = data;
      }
    } catch {}
  }

  async function handleRdsToggle() {
    try {
      await fetch("/set_rds_clock", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled: rdsClockEnabled }),
      });
      if (rdsClockEnabled) {
        rdsRefreshInterval = setInterval(loadRdsStatus, 10000);
      } else {
        clearInterval(rdsRefreshInterval);
      }
    } catch {}
  }

  function formatAgo(ms) {
    if (!ms) return "—";
    if (ms < 60000) return Math.floor(ms / 1000) + "s";
    if (ms < 3600000) return Math.floor(ms / 60000) + " min";
    return Math.floor(ms / 3600000) + " h";
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

  async function handleFactoryReset() {
    if (!confirm("Werkseinstellungen wirklich zurücksetzen? Alle Einstellungen gehen verloren!")) return;
    restarting = true;
    try {
      await fetch("/factory_reset", { method: "POST" });
    } catch {
      // Device reboots before response arrives — expected.
    }
    messageStore.show("success", "Werkseinstellungen wiederhergestellt. ESP32 startet neu…");
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

  async function handleNtpResync() {
    try {
      await fetch("/trigger_ntp_sync", { method: "POST" });
      messageStore.show("success", "NTP-Synchronisation gestartet…");
    } catch {}
  }

  async function loadAuthStatus() {
    try {
      const res = await fetch("/get_auth_status");
      if (res.ok) {
        const data = await res.json();
        authTokenCount = data.tokenCount ?? 0;
      }
    } catch {}
  }

  // Load system settings, status and warnings when modal opens
  $: if (show) {
    systemStore.load();
    loadStatusInfo();
    loadWarnings();
    loadDeepSleep();
    loadTsfStatus();
    loadRdsStatus();
    loadRtcCrystalStatus();
    loadAuthStatus();
    // Non-passive Listener nach DOM-Update registrieren
    setTimeout(() => {
      if (articleEl) {
        articleEl.addEventListener('touchstart', trackDialogTouch, {passive: true});
        articleEl.addEventListener('touchmove', preventDialogPullRefresh, {passive: false});
      }
    }, 0);
  } else if (articleEl) {
    articleEl.removeEventListener('touchstart', trackDialogTouch);
    articleEl.removeEventListener('touchmove', preventDialogPullRefresh);
  }
</script>

{#if show}
  <dialog open>
    <article bind:this={articleEl}>
      <header>
        <button aria-label="Close" class="close" on:click={handleCancel}></button>
        <h2>System Settings</h2>
      </header>

      <!-- Accordion: WLAN -->
      <div class="acc-item">
        <button class="acc-head" type="button" on:click={() => toggleSection("wifi")}>
          <span class="acc-icon">{openSection === "wifi" ? "▾" : "▸"}</span>
          WLAN
          {#if hasChanges}<span class="acc-badge">●</span>{/if}
        </button>
        {#if openSection === "wifi"}
          <div class="acc-body">
            <form on:submit|preventDefault={handleSave}>
              <label for="internalSSID">Hotspot SSID</label>
              <input type="text" id="internalSSID" value={$systemStore.internalSSID}
                on:input={(e) => handleFieldChange("internalSSID", e)} />

              <label for="internalPW">Hotspot Passwort</label>
              <input type="password" id="internalPW" value={$systemStore.internalPW}
                on:input={(e) => handleFieldChange("internalPW", e)} />

              <label for="externalSSID">Externes WLAN SSID</label>
              <input type="text" id="externalSSID" value={$systemStore.externalSSID}
                on:input={(e) => handleFieldChange("externalSSID", e)} />

              <label for="externalPW">Externes WLAN Passwort</label>
              <input type="password" id="externalPW" value={$systemStore.externalPW}
                on:input={(e) => handleFieldChange("externalPW", e)} />

              <label for="externalSSID2">Fallback SSID <span class="optional">(optional)</span></label>
              <input type="text" id="externalSSID2" value={$systemStore.externalSSID2}
                placeholder="optional" on:input={(e) => handleFieldChange("externalSSID2", e)} />

              <label for="externalPW2">Fallback Passwort</label>
              <input type="password" id="externalPW2" value={$systemStore.externalPW2}
                placeholder="optional" on:input={(e) => handleFieldChange("externalPW2", e)} />

              <div class="acc-actions">
                <button class="btn-small btn-blue" type="button" on:click={handleSave}
                  disabled={!hasChanges}>Speichern</button>
              </div>
            </form>
          </div>
        {/if}
      </div>

      <!-- Accordion: Sicherheit -->
      <div class="acc-item">
        <button class="acc-head" type="button" on:click={() => toggleSection("security")}>
          <span class="acc-icon">{openSection === "security" ? "▾" : "▸"}</span>
          Sicherheit
        </button>
        {#if openSection === "security"}
          <div class="acc-body">
            <label for="webPassword">Web-Passwort <span class="optional">(optional)</span></label>
            <input type="password" id="webPassword" value={$systemStore.webPassword}
              placeholder={$systemStore.webPasswordSet
                ? "••••••••  (leer lassen um beizubehalten)"
                : "Leer = kein Schutz"}
              on:input={(e) => handleFieldChange("webPassword", e)} />
            <small class="web-pw-hint">
              {#if $systemStore.webPasswordSet}
                Schutz aktiv · {authTokenCount} Gerät{authTokenCount === 1 ? "" : "e"} angemeldet
              {:else}
                Leer lassen um den Schutz zu deaktivieren
              {/if}
            </small>
            <div class="acc-actions">
              <button class="btn-small btn-blue" type="button" on:click={handleSave}
                disabled={!hasChanges}>Speichern</button>
            </div>
          </div>
        {/if}
      </div>

      <!-- Accordion: Energie & Uhr -->
      <div class="acc-item">
        <button class="acc-head" type="button" on:click={() => toggleSection("clock")}>
          <span class="acc-icon">{openSection === "clock" ? "▾" : "▸"}</span>
          Energie &amp; Uhr
        </button>
        {#if openSection === "clock"}
          <div class="acc-body">
            <section class="warnings-section">
              <label class="warnings-label">
                <input type="checkbox" bind:checked={deepSleepEnabled} on:change={handleDeepSleepToggle} />
                Deep sleep
              </label>
              <small>ESP32 schläft zwischen Alarmen. Wacht 5 min + Drift-Puffer vor jedem Event auf.</small>
            </section>

            <section class="warnings-section">
              <label class="warnings-label">
                <input type="checkbox" bind:checked={rtcCrystalEnabled} on:change={handleRtcCrystalToggle} />
                Externer 32kHz RTC-Quarz
              </label>
              <small>Genauerer Schlaf-Timer (GPIO0/1 = XTAL_32K). Änderung erfordert Neustart.</small>
              {#if rtcCrystalStatus}
                <div class="fallback-status">
                  {#if rtcCrystalStatus.available}
                    <span style="color: #22c55e;">Quarz erkannt ✓</span>
                    {#if rtcCrystalStatus.freqHz > 0}<span>· {rtcCrystalStatus.freqHz} Hz</span>{/if}
                  {:else if rtcCrystalEnabled}
                    <span>Wird beim nächsten Start geprüft</span>
                  {:else}
                    <span>Quarz nicht gefunden</span>
                  {/if}
                </div>
              {/if}
              {#if rtcCrystalChanged}
                <div class="fallback-status">
                  <small style="color: #f59e0b;">Änderung erfordert Neustart</small>
                  <button class="btn-tiny" type="button" on:click={applyRtcCrystal}>Jetzt anwenden &amp; Neustart</button>
                </div>
              {/if}
            </section>

            <section class="warnings-section">
              <label class="warnings-label">
                <input type="checkbox" bind:checked={tsfFallbackEnabled} on:change={handleTsfToggle} />
                TSF-Zeitfallback
              </label>
              <small>Nutzt WiFi-Beacon-Timestamps zur Zeitschätzung wenn kein NTP erreichbar ist.</small>
              {#if tsfFallbackEnabled && tsfStatus}
                <div class="fallback-status">
                  <span>{tsfStatus.storedAps} APs gespeichert</span>
                  {#if tsfStatus.lastSnapshotMs > 0}<span>· Snapshot vor {formatAgo(tsfStatus.lastSnapshotMs)}</span>{/if}
                  {#if tsfStatus.lastCalcResult !== "none"}
                    <span>· {tsfStatus.lastCalcResult === "success"
                      ? `✓ ${tsfStatus.lastCalcAps} APs, ±${tsfStatus.lastCalcDeviationMs}ms`
                      : `✗ ${tsfStatus.lastCalcResult}`}</span>
                  {/if}
                  {#if tsfStatus.minScore !== undefined}<span>· Score: {tsfStatus.minScore}–{tsfStatus.maxScore}</span>{/if}
                  <button class="btn-tiny" type="button" on:click={handleTsfSnapshot}>Snapshot</button>
                </div>
              {/if}
            </section>

            <section class="warnings-section">
              <label class="warnings-label">
                <input type="checkbox" bind:checked={rdsClockEnabled} on:change={handleRdsToggle} />
                RDS-Zeitfallback (RDA5807M)
              </label>
              <small>FM-Modul empfängt RDS-Zeitstempel (CT) wenn kein NTP erreichbar ist.</small>
              {#if rdsClockEnabled && rdsStatus}
                <div class="fallback-status">
                  {#if rdsStatus.status !== "off"}
                    <span>Kanal: {rdsStatus.channelMhz} MHz</span>
                    <span>· {rdsStatus.status}</span>
                    {#if rdsStatus.status === "locked" || rdsStatus.status === "scanning"}
                      <span>· RSSI {rdsStatus.rssi}</span>
                    {/if}
                  {:else}
                    <span>Bereit (inaktiv)</span>
                  {/if}
                </div>
              {/if}
            </section>

            <div class="acc-actions">
              <button class="btn-small btn-blue" type="button" on:click={handleNtpResync}>
                Uhr jetzt synchronisieren
              </button>
            </div>
          </div>
        {/if}
      </div>

      <!-- Accordion: Warnungen -->
      <div class="acc-item">
        <button class="acc-head" type="button" on:click={() => toggleSection("warnings")}>
          <span class="acc-icon">{openSection === "warnings" ? "▾" : "▸"}</span>
          Warnungen
        </button>
        {#if openSection === "warnings"}
          <div class="acc-body">
            <section class="warnings-section">
              <label class="warnings-label">
                <input type="checkbox" bind:checked={warningsEnabled} on:change={handleWarningsToggle} />
                LED-Warnungen aktiv
              </label>
              <small>Pixel 0 blinkt rot (keine Zeit) oder gelb (veraltete NTP-Sync).</small>
            </section>
          </div>
        {/if}
      </div>

      <!-- Accordion: System -->
      <div class="acc-item">
        <button class="acc-head" type="button" on:click={() => toggleSection("system")}>
          <span class="acc-icon">{openSection === "system" ? "▾" : "▸"}</span>
          System
        </button>
        {#if openSection === "system"}
          <div class="acc-body">
            {#if statusInfo}
              <div class="status-info">
                {#if statusInfo.currentTime}
                  <div class="status-line">
                    <span class="status-label">Aktuelle Zeit:</span>
                    <span class="status-value">{statusInfo.currentTime}</span>
                  </div>
                {/if}
                <div class="status-line">
                  <span class="status-label">System Time:</span>
                  <span class="status-value">{statusInfo.systemTime}</span>
                </div>
                <div class="status-line">
                  <span class="status-label">NTP synced:</span>
                  <span class="status-value {statusInfo.clockSynced ? 'success' : 'connection_failed'}">
                    {statusInfo.clockSynced ? "Ja" : "Nein"}
                  </span>
                </div>
                <div class="status-line">
                  <span class="status-label">Letzter Test:</span>
                  <span class="status-value">{statusInfo.lastTestTime}</span>
                </div>
                <div class="status-line">
                  <span class="status-label">STA verbunden:</span>
                  <span class="status-value {statusInfo.staConfigValid ? 'success' : 'connection_failed'}">
                    {statusInfo.staConfigValid ? "Ja" : "Nein"}
                  </span>
                </div>
              </div>
            {/if}

            <section class="log-section">
              <button class="log-toggle" type="button" on:click={() => (showLogs = !showLogs)}>
                {showLogs ? "▾" : "▸"} Event Log
              </button>
              {#if showLogs}<EventLog />{/if}
            </section>

            <div class="acc-actions">
              <button class="btn-small btn-danger" type="button" disabled={restarting} on:click={handleFactoryReset}>
                {restarting ? "Wird zurückgesetzt…" : "Werkseinstellungen"}
              </button>
            </div>
          </div>
        {/if}
      </div>

      <!-- Accordion: Firmware -->
      <div class="acc-item">
        <button class="acc-head" type="button" on:click={() => toggleSection("firmware")}>
          <span class="acc-icon">{openSection === "firmware" ? "▾" : "▸"}</span>
          Firmware Update
        </button>
        {#if openSection === "firmware"}
          <div class="acc-body">
            <div class="ota-row">
              <input type="file" accept=".bin" on:change={(e) => (firmwareFiles = e.target.files)} />
              <button class="btn-small btn-blue" type="button"
                disabled={!firmwareFiles || otaUploading} on:click={handleOtaUpload}>
                {otaUploading ? "Uploading…" : "Upload"}
              </button>
            </div>
          </div>
        {/if}
      </div>

      <footer>
        <div class="footer-right">
          <button class="btn-small btn-blue" type="button" on:click={handleCancel}>Schließen</button>
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
    justify-content: flex-end;
    align-items: center;
    gap: 1rem;
    padding-top: 0.5rem;
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

  .fallback-status {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.25rem 0.5rem;
    margin-top: 0.35rem;
    font-size: 0.75rem;
    color: var(--secondary-text-color, #888);
  }

  .btn-tiny {
    background: none;
    border: 1px solid currentColor;
    border-radius: 4px;
    cursor: pointer;
    font-size: 0.7rem;
    padding: 1px 6px;
    color: inherit;
    line-height: 1.4;
  }

  .btn-tiny:hover {
    opacity: 0.7;
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

  /* Accordion */
  .acc-item {
    border-bottom: 1px solid var(--pico-muted-border-color, #333);
  }

  .acc-head {
    width: 100%;
    background: none;
    border: none;
    padding: 0.65rem 0;
    text-align: left;
    cursor: pointer;
    font-size: 0.9rem;
    font-weight: 600;
    color: inherit;
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }

  .acc-head:hover {
    opacity: 0.8;
  }

  .acc-icon {
    font-size: 0.75rem;
    width: 1rem;
    flex-shrink: 0;
  }

  .acc-badge {
    margin-left: auto;
    color: #f59e0b;
    font-size: 0.7rem;
  }

  .acc-body {
    padding: 0.25rem 0 0.75rem 1.4rem;
  }

  .acc-actions {
    margin-top: 0.75rem;
    display: flex;
    gap: 0.5rem;
    flex-wrap: wrap;
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

  .web-pw-hint {
    display: block;
    margin-top: -0.75rem;
    margin-bottom: 1rem;
    font-size: 0.75rem;
    color: var(--secondary-text-color, #888);
  }

  .btn-link {
    background: none;
    border: none;
    padding: 0;
    cursor: pointer;
    font-size: inherit;
    color: inherit;
    text-decoration: underline;
  }
</style>
