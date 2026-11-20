<!-- Einstellungsseite: WLAN/Hotspot, Energie & Uhr, Warnungen, System-Status und Event-Log (Accordion). -->
<script>
  import { onMount } from "svelte";
  import { messageStore } from "./stores/messageStore.js";
  import { systemStore } from "./stores/systemStore.js";
  import { getJson, postJson } from "./lib/api.js";
  import EventLog from "./components/EventLog.svelte";
  import { online } from "./stores/connectionStore.js";
  import MessageAlert from "./components/MessageAlert.svelte";
  import { t, lang, localeFor } from "./lib/i18n.js";

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
  let ledCount = 4;        // Anzahl der LEDs im Streifen
  const MAX_LED_COUNT = 16; // muss zur Firmware passen (include/types.h)
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
    loadSwitches();
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

  // Setzt einen Schalter der Lampe (warnings, deepSleep, tsfFallback, rdsClock, rtcCrystal)
  function setOption(option, enabled) {
    return postJson("/set_option", { option, enabled });
  }

  // Lädt die Schalter Warnungen und Deep Sleep (stehen in den Systemeinstellungen)
  async function loadSwitches() {
    try {
      const s = await getJson("/get_system_config");
      warningsEnabled = s.warningsEnabled;
      deepSleepEnabled = s.deepSleepEnabled;
      if (s.ledCount) ledCount = s.ledCount;
    } catch (e) {
      showError($t("set.err.loadSettings"), e);
    }
  }

  // Speichert die LED-Anzahl (wirkt sofort); ungültige Eingaben werden zurückgesetzt
  async function handleLedCountChange(event) {
    const value = parseInt(event.target.value, 10);
    if (!Number.isInteger(value) || value < 1 || value > MAX_LED_COUNT) {
      messageStore.show("error", $t("set.msg.ledRange", { max: MAX_LED_COUNT }));
      loadSwitches();
      return;
    }
    try {
      await postJson("/set_led_count", { count: value });
      ledCount = value;
      messageStore.show("success", $t("set.msg.ledSaved", { n: value }));
    } catch (e) {
      showError($t("set.err.saveFailed"), e);
      loadSwitches();
    }
  }

  // Speichert den Warnungs-Schalter
  async function handleWarningsToggle() {
    try {
      await setOption("warnings", warningsEnabled);
    } catch (e) {
      showError($t("set.err.saveFailed"), e);
      loadSwitches();
    }
  }

  // Speichert den Deep-Sleep-Schalter
  async function handleDeepSleepToggle() {
    try {
      await setOption("deepSleep", deepSleepEnabled);
    } catch (e) {
      showError($t("set.err.saveFailed"), e);
      loadSwitches();
    }
  }

  // Lädt Zustand und Status des TSF-Zeitfallbacks
  async function loadTsfStatus() {
    try {
      const data = await getJson("/get_tsf_fallback");
      tsfFallbackEnabled = data.enabled;
      tsfStatus = data;
    } catch (e) {
      showError($t("set.err.tsfStatus"), e);
    }
  }

  // Speichert den TSF-Schalter
  async function handleTsfToggle() {
    try {
      await setOption("tsfFallback", tsfFallbackEnabled);
    } catch (e) {
      showError($t("set.err.saveFailed"), e);
      loadTsfStatus();
    }
  }

  // Löst einen TSF-Snapshot aus und lädt den Status nach 3 s neu
  async function handleTsfSnapshot() {
    try {
      await postJson("/trigger_tsf_snapshot");
      setTimeout(loadTsfStatus, 3000);
    } catch (e) {
      showError($t("set.err.snapshot"), e);
    }
  }

  // Lädt Zustand und Status des externen RTC-Quarzes
  async function loadRtcCrystalStatus() {
    try {
      const data = await getJson("/get_rtc_crystal");
      rtcCrystalEnabled = data.enabled;
      rtcCrystalStatus = data;
    } catch (e) {
      showError($t("set.err.rtcStatus"), e);
    }
  }

  // Merkt, dass der Quarz-Schalter geändert wurde (Übernahme erst per Button, braucht Neustart)
  function handleRtcCrystalToggle() {
    rtcCrystalChanged = true;
  }

  // Speichert die Quarz-Einstellung; das Gerät startet neu
  async function applyRtcCrystal() {
    try {
      await setOption("rtcCrystal", rtcCrystalEnabled);
      messageStore.show("success", $t("set.msg.rtcSaved"));
      setTimeout(() => location.reload(), 6000);
    } catch (e) {
      showError($t("set.err.saveFailed"), e);
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
      if (!rdsRefreshInterval) showError($t("set.err.rdsStatus"), e);
    }
  }

  // Schaltet RDS ein/aus und startet bzw. stoppt das Polling
  async function handleRdsToggle() {
    try {
      await setOption("rdsClock", rdsClockEnabled);
      if (rdsClockEnabled) startRdsPolling();
      else stopRdsPolling();
    } catch (e) {
      showError($t("set.err.saveFailed"), e);
      loadRdsStatus();
    }
  }

  // Zeitpunkt aus "vor ms Millisekunden" (Angabe der Lampe) als "20.11. 14:32" — absolut statt
  // "vor X", damit die Anzeige nicht veraltet, solange die Seite offen ist
  function formatAt(msAgo) {
    if (!msAgo) return "—";
    const d = new Date(Date.now() - msAgo);
    const p = (n) => String(n).padStart(2, "0");
    const time = `${p(d.getHours())}:${p(d.getMinutes())}`;
    // Deutsch: Tag.Monat. — Englisch: Monat/Tag
    return $lang === "en" ? `${p(d.getMonth() + 1)}/${p(d.getDate())} ${time}` : `${p(d.getDate())}.${p(d.getMonth() + 1)}. ${time}`;
  }

  // Länge eines Strings in UTF-8-Bytes (SSID/Passwort-Grenzen gelten in Bytes)
  function byteLength(str) {
    return new TextEncoder().encode(str).length;
  }

  // Prüft die Systemeinstellungen wie die Firmware; gibt Fehlertext oder null zurück
  function validateSystemSettings(s) {
    if (byteLength(s.internalSSID) < 1 || byteLength(s.internalSSID) > 32) {
      return $t("set.val.hotspotSsid");
    }
    if (byteLength(s.externalSSID) > 32 || byteLength(s.externalSSID2) > 32) {
      return $t("set.val.extSsid");
    }
    const pwChecks = [
      ["internalPW", "clearInternalPW", $t("set.val.pwHotspot")],
      ["externalPW", "clearExternalPW", $t("set.val.pwExt")],
      ["externalPW2", "clearExternalPW2", $t("set.val.pwFallback")],
    ];
    for (const [field, clear, label] of pwChecks) {
      const pw = s[field] || "";
      if (s[clear] || pw === "" || pw === MASK) continue; // entfernt, leer oder unverändert
      const len = byteLength(pw);
      if (len < 8 || len > 63) {
        return $t("set.val.pw", { label });
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
      messageStore.show("success", $t("set.msg.saved"));
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
    if (!confirm($t("set.confirm.reset"))) return;
    restarting = true;
    try {
      await postJson("/factory_reset");
      messageStore.show("success", $t("set.msg.resetDone"));
    } catch (e) {
      // Netzwerkfehler (status 0) ist erwartbar, wenn das Gerät vor der Antwort neu startet
      if (e.status === 0) {
        messageStore.show("success", $t("set.msg.resetDone"));
      } else {
        restarting = false;
        showError($t("set.err.reset"), e);
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
      if (ms < 60000) lastTestTime = $t("set.sys.secAgo", { n: Math.floor(ms / 1000) });
      else if (ms < 3600000) lastTestTime = $t("set.sys.minAgo", { n: Math.floor(ms / 60000) });
      else lastTestTime = $t("set.sys.hourAgo", { n: Math.floor(ms / 3600000) });

      // Systemzeit (Epoch-Sekunden) als lokale Zeit
      let formattedSystemTime = "";
      if (rawStatus.systemTime && !isNaN(rawStatus.systemTime)) {
        formattedSystemTime = new Date(rawStatus.systemTime * 1000).toLocaleString(localeFor($lang));
      }

      statusInfo = { ...rawStatus, lastTestTime, systemTime: formattedSystemTime };
    } catch (e) {
      showError($t("set.err.status"), e);
    }
  }

  // Startet eine NTP-Synchronisation
  async function handleNtpResync() {
    try {
      await postJson("/trigger_ntp_sync");
      messageStore.show("success", $t("set.msg.ntpStarted"));
    } catch (e) {
      showError($t("set.err.ntp"), e);
    }
  }

  // Übersetzt einen Rohstatus der Firmware (z. B. "locked"); unbekannte Werte bleiben unverändert
  function statusText(kind, value) {
    const key = `status.${kind}.${value}`;
    const text = $t(key);
    return text === key ? value : text;
  }
</script>

<main class="container settings-container">
  <header>
    <h2>{$t("set.title")}</h2>
    <button class="btn-small btn-blue" on:click={onNavigateBack}>{$t("set.back")}</button>
  </header>

  <!-- Meldungen (Erfolg/Fehler) auch auf der Einstellungsseite anzeigen -->
  <MessageAlert />

  <!-- Ohne Verbindung zur Lampe sind alle Eingaben gesperrt (je Sektion ein fieldset); nur die
       Sprachauswahl liegt außerhalb und bleibt immer bedienbar -->
  <div class="acc-scroll">
  <!-- Accordion: WLAN -->
  <div class="acc-item" class:open={openSection === "wifi"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("wifi")}>
      <span class="acc-icon">{openSection === "wifi" ? "▾" : "▸"}</span>
      {$t("set.acc.wifi")}
      {#if hasChanges}<span class="acc-badge">●</span>{/if}
    </button>
    {#if openSection === "wifi"}
      <div class="acc-body">
        <fieldset class="plain" disabled={!$online}>
        <form on:submit|preventDefault={handleSave}>
          <div class="group">
            <label for="internalSSID">{$t("set.wifi.hotspotSsid")}</label>
            <input type="text" id="internalSSID" value={$systemStore.internalSSID}
              on:input={(e) => handleFieldChange("internalSSID", e)} />

            <label for="internalPW">{$t("set.wifi.hotspotPw")}</label>
            <input autocomplete="off" disabled={$systemStore.clearInternalPW} type={pwFocus.internalPW ? "text" : "password"} id="internalPW" value={$systemStore.internalPW}
              on:focus={() => pwFocus.internalPW = true} on:blur={() => pwFocus.internalPW = false}
              on:input={(e) => handleFieldChange("internalPW", e)} />
            <label class="clear-pw">
              <input type="checkbox" checked={$systemStore.clearInternalPW}
                on:change={(e) => handleClearChange("clearInternalPW", e)} />
              {$t("set.wifi.clearPw")}
            </label>
          </div>

          <div class="group">
            <label for="externalSSID">{$t("set.wifi.extSsid")}</label>
            <input type="text" id="externalSSID" value={$systemStore.externalSSID}
              on:input={(e) => handleFieldChange("externalSSID", e)} />

            <label for="externalPW">{$t("set.wifi.extPw")}</label>
            <input autocomplete="off" disabled={$systemStore.clearExternalPW} type={pwFocus.externalPW ? "text" : "password"} id="externalPW" value={$systemStore.externalPW}
              on:focus={() => pwFocus.externalPW = true} on:blur={() => pwFocus.externalPW = false}
              on:input={(e) => handleFieldChange("externalPW", e)} />
            <label class="clear-pw">
              <input type="checkbox" checked={$systemStore.clearExternalPW}
                on:change={(e) => handleClearChange("clearExternalPW", e)} />
              {$t("set.wifi.clearPw")}
            </label>
          </div>

          <div class="group">
            <label for="externalSSID2">{$t("set.wifi.fbSsid")} <span class="optional">{$t("set.wifi.optionalParen")}</span></label>
            <input type="text" id="externalSSID2" value={$systemStore.externalSSID2}
              placeholder={$t("set.wifi.optional")} on:input={(e) => handleFieldChange("externalSSID2", e)} />

            <label for="externalPW2">{$t("set.wifi.fbPw")}</label>
            <input autocomplete="off" disabled={$systemStore.clearExternalPW2} type={pwFocus.externalPW2 ? "text" : "password"} id="externalPW2" value={$systemStore.externalPW2}
              placeholder={$t("set.wifi.optional")} on:focus={() => pwFocus.externalPW2 = true} on:blur={() => pwFocus.externalPW2 = false}
              on:input={(e) => handleFieldChange("externalPW2", e)} />
            <label class="clear-pw">
              <input type="checkbox" checked={$systemStore.clearExternalPW2}
                on:change={(e) => handleClearChange("clearExternalPW2", e)} />
              {$t("set.wifi.clearPw")}
            </label>
          </div>

          <small class="hint">{$t("set.wifi.hint")}</small>

          <div class="acc-actions group">
            <button class="btn-small btn-blue" type="button" on:click={handleSave}
              disabled={!hasChanges}>{$t("set.wifi.save")}</button>
          </div>
        </form>
        </fieldset>
      </div>
    {/if}
  </div>

  <!-- Accordion: Energie & Uhr -->
  <div class="acc-item" class:open={openSection === "clock"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("clock")}>
      <span class="acc-icon">{openSection === "clock" ? "▾" : "▸"}</span>
      {$t("set.acc.clock")}
    </button>
    {#if openSection === "clock"}
      <div class="acc-body">
        <fieldset class="plain" disabled={!$online}>
        <section class="warnings-section group">
          <label class="warnings-label">
            <input type="checkbox" bind:checked={deepSleepEnabled} on:change={handleDeepSleepToggle} />
            {$t("set.clock.deepSleep")}
          </label>
          <small>{$t("set.clock.deepSleepHint")}</small>
        </section>

        <section class="warnings-section group">
          <label class="warnings-label">
            <!-- Nur aktivierbar, wenn beim Einschalten ein Quarz erkannt wurde (ausschalten geht immer) -->
            <input
              type="checkbox"
              bind:checked={rtcCrystalEnabled}
              disabled={!rtcCrystalEnabled && !(rtcCrystalStatus && rtcCrystalStatus.available)}
              on:change={handleRtcCrystalToggle}
            />
            {$t("set.clock.rtc")}
          </label>
          <small>{$t("set.clock.rtcHint")}</small>
          {#if rtcCrystalStatus}
            <div class="fallback-status">
              {#if rtcCrystalStatus.available}
                <span style="color: #22c55e;">{$t("set.clock.rtcFound")}</span>
                {#if rtcCrystalStatus.freqHz > 0}<span>· {rtcCrystalStatus.freqHz} Hz</span>{/if}
              {:else}
                <span>{$t("set.clock.rtcNone")}</span>
              {/if}
            </div>
          {/if}
          {#if rtcCrystalChanged}
            <div class="fallback-status">
              <small style="color: #f59e0b;">{$t("set.clock.rtcRestart")}</small>
              <button class="btn-tiny" type="button" on:click={applyRtcCrystal}>{$t("set.clock.rtcApply")}</button>
            </div>
          {/if}
        </section>

        <section class="warnings-section group">
          <label class="warnings-label">
            <input type="checkbox" bind:checked={tsfFallbackEnabled} on:change={handleTsfToggle} />
            {$t("set.clock.tsf")}
          </label>
          <small>{$t("set.clock.tsfHint")}</small>
          {#if tsfFallbackEnabled && tsfStatus}
            <div class="fallback-status">
              <span>{$t("set.clock.tsfStored", { n: tsfStatus.storedAps })}</span>
              {#if tsfStatus.lastSnapshotMs > 0}<span>{$t("set.clock.tsfSnapshotAt", { at: formatAt(tsfStatus.lastSnapshotMs) })}</span>{/if}
              {#if tsfStatus.lastCalcResult !== "none"}
                <span>· {tsfStatus.lastCalcResult === "success"
                  ? $t("set.clock.tsfOk", { aps: tsfStatus.lastCalcAps, dev: tsfStatus.lastCalcDeviationMs })
                  : `✗ ${statusText("tsf", tsfStatus.lastCalcResult)}`}</span>
              {/if}
              {#if tsfStatus.minScore !== undefined}<span>{$t("set.clock.tsfScore", { min: tsfStatus.minScore, max: tsfStatus.maxScore })}</span>{/if}
              <button class="btn-tiny" type="button" on:click={handleTsfSnapshot}>{$t("set.clock.tsfButton")}</button>
            </div>
            <small>
              {$t("set.clock.tsfScoreHelp")}
            </small>
          {/if}
        </section>

        <section class="warnings-section group">
          <label class="warnings-label">
            <!-- Nur aktivierbar, wenn das Modul beim Einschalten gefunden wurde (ausschalten geht immer) -->
            <input
              type="checkbox"
              bind:checked={rdsClockEnabled}
              disabled={!rdsClockEnabled && !(rdsStatus && rdsStatus.present)}
              on:change={handleRdsToggle}
            />
            {$t("set.clock.rds")}
          </label>
          <small>{$t("set.clock.rdsHint")}</small>
          {#if rdsStatus && !rdsStatus.present}
            <div class="fallback-status"><span>{$t("set.clock.rdsNone")}</span></div>
          {/if}
          {#if rdsClockEnabled && rdsStatus}
            <div class="fallback-status">
              {#if rdsStatus.status !== "off"}
                <span>{$t("set.clock.rdsChannel", { mhz: rdsStatus.channelMhz })}</span>
                <span>· {statusText("rds", rdsStatus.status)}</span>
                {#if rdsStatus.status === "locked" || rdsStatus.status === "scanning"}
                  <span>{$t("set.clock.rdsRssi", { rssi: rdsStatus.rssi })}</span>
                {/if}
                {#if rdsStatus.lastSyncMs > 0}<span>{$t("set.clock.rdsSet", { at: formatAt(rdsStatus.lastSyncMs) })}</span>{/if}
              {:else}
                <span>{$t("set.clock.rdsReady")}</span>
              {/if}
            </div>
          {/if}
        </section>

        <div class="acc-actions group">
          <button class="btn-small btn-blue" type="button" on:click={handleNtpResync}>
            {$t("set.clock.syncNow")}
          </button>
        </div>
        </fieldset>
      </div>
    {/if}
  </div>

  <!-- Accordion: Warnungen -->
  <div class="acc-item" class:open={openSection === "warnings"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("warnings")}>
      <span class="acc-icon">{openSection === "warnings" ? "▾" : "▸"}</span>
      {$t("set.acc.warnings")}
    </button>
    {#if openSection === "warnings"}
      <div class="acc-body">
        <fieldset class="plain" disabled={!$online}>
        <section class="warnings-section group">
          <label class="warnings-label">
            <input type="checkbox" bind:checked={warningsEnabled} on:change={handleWarningsToggle} />
            {$t("set.warn.label")}
          </label>
          <small>{$t("set.warn.hint")}</small>
        </section>
        </fieldset>
      </div>
    {/if}
  </div>

  <!-- Accordion: System -->
  <div class="acc-item" class:open={openSection === "system"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("system")}>
      <span class="acc-icon">{openSection === "system" ? "▾" : "▸"}</span>
      {$t("set.acc.system")}
    </button>
    {#if openSection === "system"}
      <div class="acc-body">
        <!-- Sprachauswahl: bewusst außerhalb des fieldsets, auch ohne Verbindung zur Lampe bedienbar -->
        <div class="group lang-group">
          <label for="langSelect">{$t("set.sys.language")}</label>
          <select id="langSelect" bind:value={$lang}>
            <option value="de">Deutsch</option>
            <option value="en">English</option>
          </select>
        </div>
        <fieldset class="plain" disabled={!$online}>
        {#if statusInfo}
          <div class="status-info group">
            {#if statusInfo.currentTime}
              <div class="status-line">
                <span class="status-label">{$t("set.sys.currentTime")}</span>
                <span class="status-value">{statusInfo.currentTime}</span>
              </div>
            {/if}
            <div class="status-line">
              <span class="status-label">{$t("set.sys.systemTime")}</span>
              <span class="status-value">{statusInfo.systemTime}</span>
            </div>
            <div class="status-line">
              <span class="status-label">{$t("set.sys.ntpSynced")}</span>
              <span class="status-value {statusInfo.clockSynced ? 'success' : 'connection_failed'}">
                {statusInfo.clockSynced ? $t("set.sys.yes") : $t("set.sys.no")}
              </span>
            </div>
            <div class="status-line">
              <span class="status-label">{$t("set.sys.lastTest")}</span>
              <span class="status-value">{statusInfo.lastTestTime}</span>
            </div>
            <div class="status-line">
              <span class="status-label">{$t("set.sys.staConnected")}</span>
              <span class="status-value {statusInfo.staConfigValid ? 'success' : 'connection_failed'}">
                {statusInfo.staConfigValid ? $t("set.sys.yes") : $t("set.sys.no")}
              </span>
            </div>
          </div>
        {/if}

        <div class="group led-count">
          <label for="ledCount">{$t("set.sys.ledCount")}</label>
          <input id="ledCount" type="number" min="1" max={MAX_LED_COUNT} value={ledCount}
            on:change={handleLedCountChange} />
          <small>{$t("set.sys.ledHint")}</small>
        </div>

        <div class="acc-actions group">
          <button class="btn-small btn-danger" type="button" disabled={restarting} on:click={handleFactoryReset}>
            {restarting ? $t("set.sys.resetting") : $t("set.sys.factory")}
          </button>
        </div>
        </fieldset>
      </div>
    {/if}
  </div>

  <!-- Accordion: Event Log -->
  <div class="acc-item" class:open={openSection === "log"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("log")}>
      <span class="acc-icon">{openSection === "log" ? "▾" : "▸"}</span>
      {$t("set.acc.log")}
    </button>
    {#if openSection === "log"}
      <div class="acc-body">
        <fieldset class="plain" disabled={!$online}>
        <EventLog />
        </fieldset>
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
  /* Sprachauswahl steht außerhalb des fieldsets → Trennlinie nach unten statt über .group + .group */
  .lang-group {
    border-bottom: 1px solid var(--pico-muted-border-color, #ddd);
  }

  .led-count input[type="number"] {
    width: 80px;
  }
</style>