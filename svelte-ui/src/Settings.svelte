<!-- Einstellungsseite: WLAN/Hotspot, Energie & Uhr, Warnungen, System-Status und Event-Log (Accordion). -->
<script>
  import { onMount } from "svelte";
  import { messageStore } from "./stores/messageStore.js";
  import { systemStore } from "./stores/systemStore.js";
  import { getJson, postJson } from "./lib/api.js";
  import EventLog from "./components/EventLog.svelte";
  import MessageAlert from "./components/MessageAlert.svelte";

  const MASK = "******"; // Platzhalter, mit dem das Gerät gespeicherte Passwörter zurückliefert

  let openSection = null; // Accordion: nur eine Sektion offen
  let pwFocus = { internalPW: false, externalPW: false, externalPW2: false };

  // Öffnet/schließt eine Sektion
  function toggleSection(name) {
    openSection = openSection === name ? null : name;
  }

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
  let statusInfo = null;
  let restarting = false;

  // Prop: Navigation zurück zur Hauptseite
  export let onNavigateBack;

  // Zeigt einen Fehler als Meldung an
  function showError(prefix, e) {
    messageStore.show("error", `${prefix}: ${e.message}`);
  }

  // Komponente wird nur gerendert, wenn der Dialog offen ist → einmal laden.
  onMount(() => {
    systemStore.load();
    loadStatusInfo();
    loadWarnings();
    loadDeepSleep();
    loadTsfStatus();
    loadRdsStatus();
    loadRtcCrystalStatus();
    // Beim Verlassen Intervall aufräumen
    return () => stopRdsPolling();
  });

  // Startet das RDS-Status-Polling (nie doppelt)
  function startRdsPolling() {
    if (rdsRefreshInterval) return;
    rdsRefreshInterval = setInterval(loadRdsStatus, 10000);
  }

  // Stoppt das RDS-Status-Polling
  function stopRdsPolling() {
    if (rdsRefreshInterval) {
      clearInterval(rdsRefreshInterval);
      rdsRefreshInterval = null;
    }
  }

  // Lädt den Zustand des Warnungs-Schalters
  async function loadWarnings() {
    try {
      warningsEnabled = (await getJson("/get_warnings")).enabled;
    } catch (e) {
      showError("Warnungs-Einstellung konnte nicht geladen werden", e);
    }
  }

  // Speichert den Warnungs-Schalter
  async function handleWarningsToggle() {
    try {
      await postJson("/set_warnings", { enabled: warningsEnabled });
    } catch (e) {
      showError("Speichern fehlgeschlagen", e);
      loadWarnings();
    }
  }

  // Lädt den Zustand des Deep-Sleep-Schalters
  async function loadDeepSleep() {
    try {
      deepSleepEnabled = (await getJson("/get_deep_sleep")).enabled;
    } catch (e) {
      showError("Deep-Sleep-Einstellung konnte nicht geladen werden", e);
    }
  }

  // Speichert den Deep-Sleep-Schalter
  async function handleDeepSleepToggle() {
    try {
      await postJson("/set_deep_sleep", { enabled: deepSleepEnabled });
    } catch (e) {
      showError("Speichern fehlgeschlagen", e);
      loadDeepSleep();
    }
  }

  // Lädt Zustand und Status des TSF-Zeitfallbacks
  async function loadTsfStatus() {
    try {
      const data = await getJson("/get_tsf_fallback");
      tsfFallbackEnabled = data.enabled;
      tsfStatus = data;
    } catch (e) {
      showError("TSF-Status konnte nicht geladen werden", e);
    }
  }

  // Speichert den TSF-Schalter
  async function handleTsfToggle() {
    try {
      await postJson("/set_tsf_fallback", { enabled: tsfFallbackEnabled });
    } catch (e) {
      showError("Speichern fehlgeschlagen", e);
      loadTsfStatus();
    }
  }

  // Löst einen TSF-Snapshot aus und lädt den Status nach 3 s neu
  async function handleTsfSnapshot() {
    try {
      await postJson("/trigger_tsf_snapshot");
      setTimeout(loadTsfStatus, 3000);
    } catch (e) {
      showError("Snapshot fehlgeschlagen", e);
    }
  }

  // Lädt Zustand und Status des externen RTC-Quarzes
  async function loadRtcCrystalStatus() {
    try {
      const data = await getJson("/get_rtc_crystal");
      rtcCrystalEnabled = data.enabled;
      rtcCrystalStatus = data;
    } catch (e) {
      showError("RTC-Quarz-Status konnte nicht geladen werden", e);
    }
  }

  // Merkt, dass der Quarz-Schalter geändert wurde (Übernahme erst per Button, braucht Neustart)
  function handleRtcCrystalToggle() {
    rtcCrystalChanged = true;
  }

  // Speichert die Quarz-Einstellung; das Gerät startet neu
  async function applyRtcCrystal() {
    try {
      await postJson("/set_rtc_crystal", { enabled: rtcCrystalEnabled });
      messageStore.show("success", "RTC-Quarz-Einstellung gespeichert. ESP32 startet neu…");
      setTimeout(() => location.reload(), 6000);
    } catch (e) {
      showError("Speichern fehlgeschlagen", e);
    }
  }

  // Lädt Zustand und Status der RDS-Uhr; startet das Polling, wenn RDS aktiv ist
  async function loadRdsStatus() {
    try {
      const data = await getJson("/get_rds_clock");
      rdsClockEnabled = data.enabled;
      rdsStatus = data;
      if (rdsClockEnabled) startRdsPolling();
      else stopRdsPolling();
    } catch (e) {
      // Bei Polling-Fehlern nicht dauernd Meldungen zeigen
      if (!rdsRefreshInterval) showError("RDS-Status konnte nicht geladen werden", e);
    }
  }

  // Schaltet RDS ein/aus und startet bzw. stoppt das Polling
  async function handleRdsToggle() {
    try {
      await postJson("/set_rds_clock", { enabled: rdsClockEnabled });
      if (rdsClockEnabled) startRdsPolling();
      else stopRdsPolling();
    } catch (e) {
      showError("Speichern fehlgeschlagen", e);
      loadRdsStatus();
    }
  }

  // Formatiert eine Zeitspanne in ms als "5 s" / "3 min" / "2 h"
  function formatAgo(ms) {
    if (!ms) return "—";
    if (ms < 60000) return Math.floor(ms / 1000) + " s";
    if (ms < 3600000) return Math.floor(ms / 60000) + " min";
    return Math.floor(ms / 3600000) + " h";
  }

  // Länge eines Strings in UTF-8-Bytes (SSID/Passwort-Grenzen gelten in Bytes)
  function byteLength(str) {
    return new TextEncoder().encode(str).length;
  }

  // Prüft die Systemeinstellungen wie die Firmware; gibt Fehlertext oder null zurück
  function validateSystemSettings(s) {
    if (byteLength(s.internalSSID) < 1 || byteLength(s.internalSSID) > 32) {
      return "Hotspot-SSID: Der Name muss 1 bis 32 Zeichen lang sein.";
    }
    if (byteLength(s.externalSSID) > 32 || byteLength(s.externalSSID2) > 32) {
      return "Externes WLAN: Die SSID darf höchstens 32 Zeichen lang sein.";
    }
    const pwChecks = [
      ["internalPW", "clearInternalPW", "Hotspot-Passwort"],
      ["externalPW", "clearExternalPW", "Passwort des externen WLANs"],
      ["externalPW2", "clearExternalPW2", "Fallback-Passwort"],
    ];
    for (const [field, clear, label] of pwChecks) {
      const pw = s[field] || "";
      if (s[clear] || pw === "" || pw === MASK) continue; // entfernt, leer oder unverändert
      const len = byteLength(pw);
      if (len < 8 || len > 63) {
        return `${label}: Das Passwort muss leer sein oder 8 bis 63 Zeichen haben.`;
      }
    }
    return null;
  }

  // Speichert die Systemeinstellungen nach Prüfung; das Gerät startet danach den Hotspot neu
  async function handleSave() {
    const problem = validateSystemSettings($systemStore);
    if (problem) {
      messageStore.show("error", problem);
      return;
    }
    const success = await systemStore.post();
    if (success) {
      messageStore.show(
        "success",
        "Einstellungen gespeichert. Das Gerät startet den Hotspot neu – bitte kurz warten und das Handy neu mit dem WLAN verbinden."
      );
    }
  }

  // Übernimmt eine Texteingabe in den Store
  function handleFieldChange(field, event) {
    systemStore.updateField(field, event.target.value);
  }

  // Übernimmt die Checkbox "Passwort entfernen"
  function handleClearChange(field, event) {
    systemStore.updateField(field, event.target.checked);
  }

  // Setzt das Gerät nach Rückfrage auf Werkseinstellungen zurück
  async function handleFactoryReset() {
    if (!confirm("Werkseinstellungen wirklich zurücksetzen? Alle Einstellungen gehen verloren!")) return;
    restarting = true;
    try {
      await postJson("/factory_reset");
      messageStore.show("success", "Werkseinstellungen wiederhergestellt. ESP32 startet neu…");
    } catch (e) {
      // Netzwerkfehler (status 0) ist erwartbar, wenn das Gerät vor der Antwort neu startet
      if (e.status === 0) {
        messageStore.show("success", "Werkseinstellungen wiederhergestellt. ESP32 startet neu…");
      } else {
        restarting = false;
        showError("Zurücksetzen fehlgeschlagen", e);
      }
    }
  }

  // Lädt den Gerätestatus und bereitet ihn für die Anzeige auf
  async function loadStatusInfo() {
    try {
      const rawStatus = await getJson("/get_status");

      // Zeit seit dem letzten erfolgreichen Test aus ms formatieren
      const ms = rawStatus.timeSinceLastSucceededTestMs;
      let lastTestTime;
      if (ms < 60000) lastTestTime = "vor " + Math.floor(ms / 1000) + " Sekunden";
      else if (ms < 3600000) lastTestTime = "vor " + Math.floor(ms / 60000) + " Minuten";
      else lastTestTime = "vor " + Math.floor(ms / 3600000) + " Stunden";

      // Systemzeit (Epoch-Sekunden) als lokale Zeit
      let formattedSystemTime = "";
      if (rawStatus.systemTime && !isNaN(rawStatus.systemTime)) {
        formattedSystemTime = new Date(rawStatus.systemTime * 1000).toLocaleString();
      }

      statusInfo = { ...rawStatus, lastTestTime, systemTime: formattedSystemTime };
    } catch (e) {
      showError("Status konnte nicht geladen werden", e);
    }
  }

  // Startet eine NTP-Synchronisation
  async function handleNtpResync() {
    try {
      await postJson("/trigger_ntp_sync");
      messageStore.show("success", "NTP-Synchronisation gestartet…");
    } catch (e) {
      showError("NTP-Synchronisation fehlgeschlagen", e);
    }
  }
</script>

<main class="container settings-container">
  <header>
    <h2>Systemeinstellungen</h2>
    <button class="btn-small btn-blue" on:click={onNavigateBack}>Zurück</button>
  </header>

  <!-- Meldungen (Erfolg/Fehler) auch auf der Einstellungsseite anzeigen -->
  <MessageAlert />

  <div class="acc-scroll">
  <!-- Accordion: WLAN -->
  <div class="acc-item" class:open={openSection === "wifi"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("wifi")}>
      <span class="acc-icon">{openSection === "wifi" ? "▾" : "▸"}</span>
      WLAN
      {#if hasChanges}<span class="acc-badge">●</span>{/if}
    </button>
    {#if openSection === "wifi"}
      <div class="acc-body">
        <form on:submit|preventDefault={handleSave}>
          <div class="group">
            <label for="internalSSID">Hotspot SSID</label>
            <input type="text" id="internalSSID" value={$systemStore.internalSSID}
              on:input={(e) => handleFieldChange("internalSSID", e)} />

            <label for="internalPW">Hotspot Passwort</label>
            <input autocomplete="off" disabled={$systemStore.clearInternalPW} type={pwFocus.internalPW ? "text" : "password"} id="internalPW" value={$systemStore.internalPW}
              on:focus={() => pwFocus.internalPW = true} on:blur={() => pwFocus.internalPW = false}
              on:input={(e) => handleFieldChange("internalPW", e)} />
            <label class="clear-pw">
              <input type="checkbox" checked={$systemStore.clearInternalPW}
                on:change={(e) => handleClearChange("clearInternalPW", e)} />
              Passwort entfernen
            </label>
          </div>

          <div class="group">
            <label for="externalSSID">Externes WLAN SSID</label>
            <input type="text" id="externalSSID" value={$systemStore.externalSSID}
              on:input={(e) => handleFieldChange("externalSSID", e)} />

            <label for="externalPW">Externes WLAN Passwort</label>
            <input autocomplete="off" disabled={$systemStore.clearExternalPW} type={pwFocus.externalPW ? "text" : "password"} id="externalPW" value={$systemStore.externalPW}
              on:focus={() => pwFocus.externalPW = true} on:blur={() => pwFocus.externalPW = false}
              on:input={(e) => handleFieldChange("externalPW", e)} />
            <label class="clear-pw">
              <input type="checkbox" checked={$systemStore.clearExternalPW}
                on:change={(e) => handleClearChange("clearExternalPW", e)} />
              Passwort entfernen
            </label>
          </div>

          <div class="group">
            <label for="externalSSID2">Fallback SSID <span class="optional">(optional)</span></label>
            <input type="text" id="externalSSID2" value={$systemStore.externalSSID2}
              placeholder="optional" on:input={(e) => handleFieldChange("externalSSID2", e)} />

            <label for="externalPW2">Fallback Passwort</label>
            <input autocomplete="off" disabled={$systemStore.clearExternalPW2} type={pwFocus.externalPW2 ? "text" : "password"} id="externalPW2" value={$systemStore.externalPW2}
              placeholder="optional" on:focus={() => pwFocus.externalPW2 = true} on:blur={() => pwFocus.externalPW2 = false}
              on:input={(e) => handleFieldChange("externalPW2", e)} />
            <label class="clear-pw">
              <input type="checkbox" checked={$systemStore.clearExternalPW2}
                on:change={(e) => handleClearChange("clearExternalPW2", e)} />
              Passwort entfernen
            </label>
          </div>

          <small class="hint">Hinweis: Nach dem Speichern startet das Gerät den Hotspot neu. Das Handy muss sich danach neu verbinden. SSID: 1–32 Zeichen, Passwörter: leer oder 8–63 Zeichen. Ein angezeigtes „******“ steht für ein gespeichertes, unverändertes Passwort.</small>

          <div class="acc-actions group">
            <button class="btn-small btn-blue" type="button" on:click={handleSave}
              disabled={!hasChanges}>Speichern</button>
          </div>
        </form>
      </div>
    {/if}
  </div>

  <!-- Accordion: Energie & Uhr -->
  <div class="acc-item" class:open={openSection === "clock"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("clock")}>
      <span class="acc-icon">{openSection === "clock" ? "▾" : "▸"}</span>
      Energie &amp; Uhr
    </button>
    {#if openSection === "clock"}
      <div class="acc-body">
        <section class="warnings-section group">
          <label class="warnings-label">
            <input type="checkbox" bind:checked={deepSleepEnabled} on:change={handleDeepSleepToggle} />
            Deep sleep
          </label>
          <small>ESP32 schläft zwischen Alarmen. Wacht 5 min + Drift-Puffer vor jedem Event auf.</small>
        </section>

        <section class="warnings-section group">
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

        <section class="warnings-section group">
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

        <section class="warnings-section group">
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

        <div class="acc-actions group">
          <button class="btn-small btn-blue" type="button" on:click={handleNtpResync}>
            Uhr jetzt synchronisieren
          </button>
        </div>
      </div>
    {/if}
  </div>

  <!-- Accordion: Warnungen -->
  <div class="acc-item" class:open={openSection === "warnings"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("warnings")}>
      <span class="acc-icon">{openSection === "warnings" ? "▾" : "▸"}</span>
      Warnungen
    </button>
    {#if openSection === "warnings"}
      <div class="acc-body">
        <section class="warnings-section group">
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
  <div class="acc-item" class:open={openSection === "system"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("system")}>
      <span class="acc-icon">{openSection === "system" ? "▾" : "▸"}</span>
      System
    </button>
    {#if openSection === "system"}
      <div class="acc-body">
        {#if statusInfo}
          <div class="status-info group">
            {#if statusInfo.currentTime}
              <div class="status-line">
                <span class="status-label">Aktuelle Zeit:</span>
                <span class="status-value">{statusInfo.currentTime}</span>
              </div>
            {/if}
            <div class="status-line">
              <span class="status-label">Systemzeit:</span>
              <span class="status-value">{statusInfo.systemTime}</span>
            </div>
            <div class="status-line">
              <span class="status-label">NTP synchronisiert:</span>
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

        <div class="acc-actions group">
          <button class="btn-small btn-danger" type="button" disabled={restarting} on:click={handleFactoryReset}>
            {restarting ? "Wird zurückgesetzt…" : "Werkseinstellungen"}
          </button>
        </div>
      </div>
    {/if}
  </div>

  <!-- Accordion: Event Log -->
  <div class="acc-item" class:open={openSection === "log"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("log")}>
      <span class="acc-icon">{openSection === "log" ? "▾" : "▸"}</span>
      Ereignisprotokoll
    </button>
    {#if openSection === "log"}
      <div class="acc-body">
        <EventLog />
      </div>
    {/if}
  </div>

  </div> <!-- /acc-scroll -->
</main>

<style>
  .settings-container {
    padding-bottom: 2rem;
  }

  header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1rem;
  }

  header h2 {
    margin: 0;
  }

  /* Header, Footer und alle Accordion-Köpfe fest;
     nur der Inhalt der offenen Section scrollt. */
  .acc-scroll {
    display: flex;
    flex-direction: column;
  }

  .acc-item {
    flex: none;
  }

  .acc-item.open {
    flex: 0 1 auto;
    min-height: 0;
    display: flex;
    flex-direction: column;
  }

  .acc-item.open > .acc-head {
    flex: none;
  }

  .acc-item.open > .acc-body {
    flex: 0 1 auto;
    min-height: 0;
    padding-right: 0.25rem; /* Platz für Fokus-Rahmen */
  }



  form {
    margin-bottom: 0;
  }

  /* Gliederung: Gruppen durch Linien getrennt (Breite = Textbreite) */
  .group {
    margin: 0 !important;
    padding: 0.75rem 0;
  }

  .group + .group {
    border-top: 1px solid var(--pico-muted-border-color, #ddd);
  }

  .optional {
    font-size: 0.75rem;
    font-weight: normal;
    color: var(--pico-muted-color, #888);
  }

  .group > label {
    margin-top: 0.75rem;
    margin-bottom: 0.35rem;
    display: block;
  }

  .group > label:first-child {
    margin-top: 0;
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

  .clear-pw {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    margin-top: 0.35rem;
    font-size: 0.75rem;
    color: var(--pico-muted-color, #888);
  }

  .hint {
    display: block;
    margin-top: 0.5rem;
    font-size: 0.75rem;
    color: var(--pico-muted-color, #888);
  }
</style>