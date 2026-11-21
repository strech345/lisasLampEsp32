<!-- Einstellungsseite: Persönliches, WLAN/Hotspot, Energie & Uhr, Warnungen, System, Debug und Event-Log (Accordion). -->
<script>
  import { onMount } from "svelte";
  import { messageStore } from "./stores/messageStore.js";
  import CheckButton from "./components/CheckButton.svelte";
  import { WIFI_KEYS, systemStore, lampName, celebrations } from "./stores/systemStore.js";
  import { getJson, postJson } from "./lib/api.js";
  import SystemMonitor from "./components/SystemMonitor.svelte";
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

  // WLAN-Sektion und Rubrik Synchronisation speichern getrennt (je eigene Felder, eigener Knopf)
  $: hasChanges = $systemStore && systemStore.hasChanges(WIFI_KEYS);


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
  let restarting = false;

  // Persönliches: Name der Lampe und Party-Tage (eigener Speichern-Knopf)
  const MAX_CELEBRATIONS = 8;   // muss zur Firmware passen (include/types.h)
  const NAME_MAX_LEN = 20;      // muss zur Firmware passen (LAMP_NAME_MAX_LEN)
  let personalName = "";
  let personalDays = [];        // [{month, day}], Monat 1–12
  let personalOriginal = "";    // gespeicherter Stand als JSON (Änderungsanzeige)

  // Debug-Schalter (wirken sofort)
  let statusLedEnabled = true;
  let serialLogEnabled = true;
  let eventLogEnabled = true;
  let logToUsb = false;

  // Prop: Navigation zurück zur Hauptseite
  export let onNavigateBack;

  // Zeigt einen Fehler als Meldung an
  function showError(prefix, e) {
    messageStore.show("error", `${prefix}: ${e.message}`);
  }

  // Komponente wird nur gerendert, wenn der Dialog offen ist → einmal laden.
  onMount(() => {
    systemStore.load();
    loadSwitches();
    loadTsfStatus();
    loadClockInfo();
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

  // Setzt einen Schalter der Lampe (warnings, deepSleep, tsfFallback, rdsClock, rtcCrystal,
  // statusLed, serialLog, eventLog, logUsb)
  function setOption(option, enabled) {
    return postJson("/set_option", { option, enabled });
  }

  // Lädt Schalter, LED-Anzahl, Persönliches und Debug (stehen in den Systemeinstellungen)
  async function loadSwitches() {
    try {
      const s = await getJson("/get_system_config");
      warningsEnabled = s.warningsEnabled;
      deepSleepEnabled = s.deepSleepEnabled;
      if (s.ledCount) ledCount = s.ledCount;
      statusLedEnabled = s.statusLedEnabled ?? true;
      serialLogEnabled = s.serialLogEnabled ?? true;
      eventLogEnabled = s.eventLogEnabled ?? true;
      logToUsb = s.logToUsb ?? false;
      personalName = s.lampName || "";
      personalDays = (s.celebrations || []).map((c) => ({ month: c.month, day: c.day }));
      personalOriginal = JSON.stringify({ n: personalName, d: personalDays });
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

  // Speichert einen Debug-Schalter; bei Fehler Stand neu laden
  async function handleDebugToggle(option, enabled) {
    try {
      await setOption(option, enabled);
    } catch (e) {
      showError($t("set.err.saveFailed"), e);
      loadSwitches();
    }
  }

  // Tage im Monat (Februar mit 29, damit der 29.2. als Party-Tag möglich ist)
  function daysInMonth(month) {
    return new Date(2024, month, 0).getDate();
  }

  // Fügt einen Party-Tag hinzu (heute als Vorschlag)
  function addPartyDay() {
    if (personalDays.length >= MAX_CELEBRATIONS) return;
    const now = new Date();
    personalDays = [...personalDays, { month: now.getMonth() + 1, day: now.getDate() }];
    savePersonal();
  }

  // Entfernt einen Party-Tag
  function removePartyDay(index) {
    personalDays = personalDays.filter((_, i) => i !== index);
    savePersonal();
  }

  // Monat geändert: Tag ggf. auf das Monatsende kürzen
  function handleMonthChange(index) {
    const d = personalDays[index];
    if (d.day > daysInMonth(d.month)) d.day = daysInMonth(d.month);
    personalDays = personalDays;
    savePersonal();
  }

  // Monatsnamen in der gewählten Sprache
  $: monthNames = Array.from({ length: 12 }, (_, i) =>
    new Date(2024, i, 1).toLocaleString(localeFor($lang), { month: "long" }));

  // Speichert Name und Party-Tage sofort nach jeder Änderung (Feld verlassen, Monat gewählt, Tag
  // hinzugefügt/entfernt) — wie die Dauer-Felder der Startseite, ohne Erfolgsmeldung. Ungültig → Meldung
  // und gespeicherten Stand wiederherstellen. Kein WLAN-Neustart.
  async function savePersonal() {
    const name = personalName.trim();
    const restore = () => {
      const o = JSON.parse(personalOriginal);
      personalName = o.n;
      personalDays = o.d;
    };
    if (name.length < 1 || byteLength(name) > NAME_MAX_LEN) {
      messageStore.show("error", $t("set.val.lampName", { max: NAME_MAX_LEN }));
      restore();
      return;
    }
    for (const d of personalDays) {
      if (!Number.isInteger(d.day) || d.day < 1 || d.day > daysInMonth(d.month)) {
        messageStore.show("error", $t("set.val.partyDay"));
        restore();
        return;
      }
    }
    const state = JSON.stringify({ n: name, d: personalDays });
    if (state === personalOriginal) return; // nichts geändert
    try {
      await postJson("/set_system_config", { lampName: name, celebrations: personalDays });
      personalName = name;
      personalOriginal = state;
      lampName.set(name);
      celebrations.set(personalDays.map((d) => ({ ...d })));
    } catch (e) {
      showError($t("set.err.saveFailed"), e);
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

  // Löst einen TSF-Snapshot aus (dauert ca. 2 s) und fragt nach, bis er da ist; dann steht der neue
  // Zeitpunkt in der Anzeige. Kommt nach 10 s keiner, Meldung (z. B. WLAN aus → kein Empfang möglich).
  let tsfBusy = false;
  async function handleTsfSnapshot() {
    tsfBusy = true;
    const start = Date.now();
    try {
      await postJson("/trigger_tsf_snapshot");
      let done = false;
      while (!done && Date.now() - start < 10000) {
        await sleep(1000);
        await loadTsfStatus();
        // lastSnapshotMs = ms seit dem Snapshot: kleiner als die Zeit seit dem Klick → neuer Snapshot
        done = tsfStatus && tsfStatus.lastSnapshotMs > 0 && tsfStatus.lastSnapshotMs < Date.now() - start;
      }
      messageStore.show(done ? "success" : "error", $t(done ? "set.clock.tsfDone" : "set.clock.tsfFailed"));
    } catch (e) {
      showError($t("set.err.snapshot"), e);
    }
    tsfBusy = false;
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

  // Gang in ppb als ppm mit Vorzeichen und einer Nachkommastelle, z. B. 12400 → "+12.4"
  const fmtPpb = (ppb) => (ppb >= 0 ? "+" : "−") + (Math.abs(ppb) / 1000).toFixed(1);

  // Zeitspanne in Sekunden kurz: "45 min", "5 h 10 min", "3 Tage"
  function fmtSpan(sec) {
    const m = Math.round((sec || 0) / 60);
    if (m < 60) return `${m} min`;
    if (m < 2880) return `${Math.floor(m / 60)} h${m % 60 ? ` ${m % 60} min` : ""}`;
    return $t("set.clock.days", { n: Math.round(m / 1440) });
  }

  // Länge eines Strings in UTF-8-Bytes (SSID/Passwort-Grenzen gelten in Bytes)
  function byteLength(str) {
    return new TextEncoder().encode(str).length;
  }

  // Prüft ein Passwortfeld beim Tippen: leer (= kein Passwort) und Maske sind ok, sonst 8–63 Bytes.
  // Gibt den Hinweistext oder "" zurück.
  function pwProblem(pw) {
    if (!pw || pw === MASK) return "";
    const len = byteLength(pw);
    if (len < 8) return $t("set.wifi.pwShort", { n: len });
    if (len > 63) return $t("set.wifi.pwLong");
    return "";
  }

  // Prüft Minutenfelder ([Feld, min, max, Bezeichnung]) wie die Firmware (json_utils.cpp); Fehlertext oder null
  function checkMinutes(s, checks) {
    for (const [field, min, max, label] of checks) {
      const v = s[field];
      if (!Number.isInteger(v) || v < min || v > max) {
        return $t("set.val.minutes", { label, min, max });
      }
    }
    return null;
  }

  // --- WLAN-Sektion: jede Zeilengruppe hat ihren eigenen Haken (voll = gespeichert, gestrichelt = Änderung) ---
  const AP_KEYS = ["internalSSID", "internalPW"];
  const EXT_KEYS = { 1: ["externalSSID", "externalPW"], 2: ["externalSSID2", "externalPW2"] };
  $: dirtyAp = $systemStore && systemStore.hasChanges(AP_KEYS);
  $: dirty1 = $systemStore && systemStore.hasChanges(EXT_KEYS[1]);
  $: dirty2 = $systemStore && systemStore.hasChanges(EXT_KEYS[2]);
  let apBusy = false;

  // Springt zum Passwortfeld, wenn dort ein Fehler steht (der Text steht schon unter dem Feld); true = Fehler
  function focusPwProblem(field) {
    if (!pwProblem($systemStore[field])) return false;
    const el = document.getElementById(field);
    el?.scrollIntoView({ block: "center", behavior: "smooth" });
    el?.focus();
    return true;
  }

  // Hotspot-Haken: Name/Passwort prüfen und speichern (testen lässt sich der eigene Hotspot nicht).
  // Bei Änderung startet die Lampe den Hotspot neu → Handy muss sich neu verbinden.
  async function saveHotspot() {
    if (focusPwProblem("internalPW")) return;
    const len = byteLength($systemStore.internalSSID);
    if (len < 1 || len > 32) {
      messageStore.show("error", $t("set.val.hotspotSsid"));
      return;
    }
    if (!dirtyAp) {
      messageStore.show("success", $t("set.wifi.alreadySaved"));
      return;
    }
    apBusy = true;
    if (await systemStore.post(AP_KEYS)) messageStore.show("success", $t("set.msg.savedApRestart"));
    apBusy = false;
  }

  // Minutenfeld beim Verlassen speichern (wie die Dauer-Felder der Startseite): ungültig → Meldung und
  // gespeicherten Wert wieder anzeigen; gültig → sofort senden, ohne Erfolgsmeldung
  async function saveMinutes(field, min, max, label, event) {
    const raw = event.target.value;
    const value = Number(raw);
    if (raw === "" || !Number.isInteger(value) || value < min || value > max) {
      messageStore.show("error", $t("set.val.minutes", { label, min, max }));
      systemStore.revertField(field);
      event.target.value = $systemStore[field];
      return;
    }
    systemStore.updateField(field, value);
    await systemStore.post([field]);
  }

  // Haken eines externen WLANs (1 = Haupt, 2 = Fallback): Verbindung testen; bei Erfolg und ungespeicherten
  // Änderungen gleich speichern. Leere SSID mit Änderung = Netz entfernen (ohne Test).
  async function checkExternal(slot) {
    const [ssidField, pwField] = EXT_KEYS[slot];
    const dirty = slot === 1 ? dirty1 : dirty2;
    if (focusPwProblem(pwField)) return;
    if (byteLength($systemStore[ssidField]) > 32) {
      messageStore.show("error", $t("set.val.extSsid"));
      return;
    }
    if (!$systemStore[ssidField]) {
      if (dirty && (await systemStore.post(EXT_KEYS[slot]))) {
        testSlot = slot;
        testError = "";
        testResult = "removed";
      }
      return;
    }
    await startTest(slot, ssidField, pwField);
    if (testResult === "ok" && dirty && (await systemStore.post(EXT_KEYS[slot]))) {
      testResult = "okSaved";
    }
  }

  // Übernimmt eine Texteingabe in den Store
  function handleFieldChange(field, event) {
    systemStore.updateField(field, event.target.value);
  }

  // --- WLAN finden / testen (Lampe sucht bzw. verbindet sich testweise; Ergebnis per Abfrage) ---
  let scanBusy = false;
  let scanSlot = 0;          // 1 = externes WLAN, 2 = Fallback; dort erscheint die Auswahl
  let scanResults = null;    // [{ssid, rssi, secure}] oder null
  let testBusy = false;
  let testSlot = 0;
  let testResult = "";       // running | ok | wrong_password | not_found | timeout | failed | error
  let testError = "";        // Fehlertext der Lampe (z. B. ungültige Eingabe), erscheint unter dem Feld

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  // Fragt einen Status-Endpunkt ab, bis er fertig ist; Verbindungsfehler zwischendurch sind normal
  // (beim Test kann der Hotspot kurz den Kanal wechseln)
  async function pollUntilDone(url, maxMs) {
    const end = Date.now() + maxMs;
    while (Date.now() < end) {
      await sleep(1000);
      try {
        const data = await getJson(url);
        if (data.status === "done" || data.status === "failed") return data;
      } catch (e) {
        // weiter versuchen
      }
    }
    return null;
  }

  // Startet die WLAN-Suche und zeigt die gefundenen Netze unter dem SSID-Feld von slot
  async function startScan(slot) {
    scanSlot = slot;
    scanResults = null;
    scanBusy = true;
    try {
      await postJson("/trigger_wifi_scan");
      const data = await pollUntilDone("/get_wifi_scan", 25000);
      if (!data || data.status === "failed") {
        messageStore.show("error", $t("set.wifi.scanFailed"));
        scanSlot = 0;
      } else {
        scanResults = data.networks || [];
      }
    } catch (e) {
      showError($t("set.wifi.scanFailed"), e);
      scanSlot = 0;
    }
    scanBusy = false;
  }

  // Übernimmt das gewählte Netz als SSID und schließt die Auswahl
  function pickNetwork(field, event) {
    if (!event.target.value) return;
    systemStore.updateField(field, event.target.value);
    scanSlot = 0;
    scanResults = null;
  }

  // Signalstärke als Balken
  function signalText(rssi) {
    return rssi >= -55 ? "▂▄▆█" : rssi >= -67 ? "▂▄▆" : rssi >= -78 ? "▂▄" : "▂";
  }

  // Testet SSID + Passwort (Werte aus den Feldern, auch ungespeichert)
  async function startTest(slot, ssidField, pwField) {
    testSlot = slot;
    testResult = "running";
    testError = "";
    testBusy = true;
    try {
      await postJson("/test_wifi", { slot, ssid: $systemStore[ssidField], password: $systemStore[pwField] || "" });
      const data = await pollUntilDone("/get_wifi_test", 30000);
      testResult = data ? data.result : "error";
    } catch (e) {
      testResult = "error";
      testError = e.message;
    }
    testBusy = false;
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

  // --- Uhr-Abgleich: Stand aus /get_system_info (Objekt clock, siehe clock_sync.h) ---
  let clockInfo = null; // { now, ntpAt, rdsAt, tsfAt, lastSyncOk, staActive, nextSyncInMs }
  let syncBusy = false;

  // Lädt den Stand der Uhr-Abgleiche; Fehler still (Verbindung kann beim Abgleich kurz wackeln)
  async function loadClockInfo() {
    try {
      const d = await getJson("/get_system_info");
      if (d.clock) clockInfo = d.clock;
    } catch (e) {
      // nächster Versuch beim nächsten Aufruf
    }
  }

  // Text „Letzter Abgleich: 20.11. 21:21 (NTP)“ aus dem jüngeren der echten Zeitquellen NTP und RDS
  function syncInfoText(c, _t) {
    if (!c) return "";
    if (c.nextSyncInMs === -1 && !c.ntpAt && !c.rdsAt) return $t("set.clock.noHomeWifi");
    const at = Math.max(c.ntpAt || 0, c.rdsAt || 0);
    if (!at) return $t("set.clock.lastSyncNever");
    const src = (c.ntpAt || 0) >= (c.rdsAt || 0) ? "NTP" : "RDS";
    return $t("set.clock.lastSync", { at: formatAt(Math.max(0, (c.now - at) * 1000)), src });
  }

  // „Uhr jetzt synchronisieren“: Lampe verbindet das Heimnetz und fragt NTP. Danach alle 2 s nachfragen
  // (bis 60 s): neuer NTP-Zeitpunkt = Erfolg; Heimnetz wieder aus ohne neuen Zeitpunkt = Fehlschlag.
  async function handleNtpResync() {
    syncBusy = true;
    await loadClockInfo();
    const before = clockInfo ? clockInfo.ntpAt || 0 : 0;
    const start = Date.now();
    try {
      await postJson("/trigger_ntp_sync");
      let result = null;
      while (!result && Date.now() - start < 60000) {
        await sleep(2000);
        await loadClockInfo();
        if (!clockInfo) continue;
        if ((clockInfo.ntpAt || 0) > before) result = "ok";
        else if (!clockInfo.staActive && Date.now() - start > 6000) result = "failed";
      }
      messageStore.show(result === "ok" ? "success" : "error",
        $t(result === "ok" ? "set.clock.syncOk" : "set.clock.syncFailed"));
      if (tsfFallbackEnabled) loadTsfStatus(); // nach NTP: neuer Snapshot, Snapshot-Knopf wieder frei
    } catch (e) {
      showError($t("set.err.ntp"), e);
    }
    syncBusy = false;
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
  <!-- Accordion: Persönlich (Sprache, Name der Lampe, Party-Tage) -->
  <div class="acc-item" class:open={openSection === "personal"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("personal")}>
      <span class="acc-icon">{openSection === "personal" ? "▾" : "▸"}</span>
      {$t("set.acc.personal")}
    </button>
    {#if openSection === "personal"}
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
        <div class="group">
          <label for="lampName">{$t("set.personal.name")}</label>
          <input type="text" id="lampName" maxlength={NAME_MAX_LEN} bind:value={personalName} on:change={savePersonal} />
        </div>

        <div class="group">
          <span class="group-title">{$t("set.personal.days")}</span>
          <small class="hint">{$t("set.personal.daysHint")}</small>
          {#each personalDays as d, i}
            <div class="party-row">
              <input type="number" min="1" max={daysInMonth(d.month)} bind:value={d.day}
                on:change={savePersonal} aria-label={$t("set.personal.day")} />
              <select bind:value={d.month} on:change={() => handleMonthChange(i)} aria-label={$t("set.personal.month")}>
                {#each monthNames as m, mi}<option value={mi + 1}>{m}</option>{/each}
              </select>
              <button type="button" class="icon-btn" title={$t("set.personal.remove")}
                aria-label={$t("set.personal.remove")} on:click={() => removePartyDay(i)}>
                <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M5 5 L19 19 M19 5 L5 19" stroke="currentColor" stroke-width="3.2" stroke-linecap="round" fill="none" /></svg>
              </button>
            </div>
          {:else}
            <small class="hint">{$t("set.personal.none")}</small>
          {/each}
          <button type="button" class="btn-small btn-blue add-day" disabled={personalDays.length >= MAX_CELEBRATIONS}
            on:click={addPartyDay}>{$t("set.personal.add")}</button>
        </div>
        </fieldset>
      </div>
    {/if}
  </div>

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
        <form on:submit|preventDefault>
          <div class="group">
            <div class="rubric">{$t("set.wifi.secHotspot")}</div>
            <div class="wrow">
              <label for="internalSSID">{$t("set.wifi.ssid")}</label>
              <input type="text" id="internalSSID" value={$systemStore.internalSSID}
                on:input={(e) => handleFieldChange("internalSSID", e)} />
              <span></span>

              <label for="internalPW">{$t("set.wifi.pw")}</label>
              <input autocomplete="off" type={pwFocus.internalPW ? "text" : "password"} id="internalPW" value={$systemStore.internalPW}
                on:focus={() => pwFocus.internalPW = true} on:blur={() => pwFocus.internalPW = false}
                on:input={(e) => handleFieldChange("internalPW", e)} />
              <CheckButton title={$t("set.wifi.saveHotspot")} dirty={dirtyAp} busy={apBusy}
                disabled={apBusy || !!pwProblem($systemStore.internalPW)} on:click={saveHotspot} />
              {#if pwProblem($systemStore.internalPW)}<small class="test-result bad wrow-under">{pwProblem($systemStore.internalPW)}</small>{/if}

              <!-- Laufzeit im selben Raster wie SSID/Passwort; „min“ steht in der Spalte der Haken -->
              <label for="apTimeoutMin">{$t("set.wifi.apTimeout")}</label>
              <input type="number" id="apTimeoutMin" min="0" max="1440" step="1" value={$systemStore.apTimeoutMin}
                on:change={(e) => saveMinutes("apTimeoutMin", 0, 1440, $t("set.wifi.apTimeout"), e)} />
              <span class="unit wunit">{$t("main.min")}</span>
            </div>
            <small class="nrow-hint">{$t("set.wifi.apTimeoutHint")}</small>
          </div>

          <div class="group">
            <div class="rubric">{$t("set.wifi.secExt")}</div>
            <div class="wrow">
              <label for="externalSSID">{$t("set.wifi.ssid")}</label>
              <input type="text" id="externalSSID" value={$systemStore.externalSSID}
                on:input={(e) => handleFieldChange("externalSSID", e)} />
              <button class="icon-btn" type="button" title={$t("set.wifi.find")} aria-label={$t("set.wifi.find")}
                disabled={scanBusy || testBusy} on:click={() => startScan(1)}>
                {#if scanBusy && scanSlot === 1}<span class="spin"></span>{:else}<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6" stroke="currentColor" stroke-width="2.8" fill="none" /><path d="M15 15 L20 20" stroke="currentColor" stroke-width="3.2" stroke-linecap="round" /></svg>{/if}
              </button>
              {#if scanSlot === 1 && !scanBusy && scanResults}
                <select class="scan-list wrow-under" on:change={(e) => pickNetwork("externalSSID", e)}>
                  <option value="">{scanResults.length ? $t("set.wifi.pick", { n: scanResults.length }) : $t("set.wifi.noneFound")}</option>
                  {#each scanResults as net}
                    <option value={net.ssid}>{net.ssid} · {signalText(net.rssi)}{net.secure ? " 🔒" : ""}</option>
                  {/each}
                </select>
              {/if}

              <label for="externalPW">{$t("set.wifi.pw")}</label>
              <input autocomplete="off" type={pwFocus.externalPW ? "text" : "password"} id="externalPW" value={$systemStore.externalPW}
                on:focus={() => pwFocus.externalPW = true} on:blur={() => pwFocus.externalPW = false}
                on:input={(e) => handleFieldChange("externalPW", e)} />
              <CheckButton title={$t("set.wifi.test")} dirty={dirty1} busy={testBusy && testSlot === 1}
                disabled={scanBusy || testBusy || (!$systemStore.externalSSID && !dirty1) || !!pwProblem($systemStore.externalPW)}
                on:click={() => checkExternal(1)} />
              {#if pwProblem($systemStore.externalPW)}<small class="test-result bad wrow-under">{pwProblem($systemStore.externalPW)}</small>{/if}
              {#if testSlot === 1 && testResult}
                <small class="test-result wrow-under {['ok', 'okSaved', 'removed'].includes(testResult) ? 'ok' : testResult === 'running' ? '' : 'bad'}">{testError || $t("set.wifi.result." + testResult)}</small>
              {/if}
            </div>
          </div>

          <div class="group">
            <div class="rubric">{$t("set.wifi.secFb")} <span class="optional">{$t("set.wifi.optionalParen")}</span></div>
            <div class="wrow">
              <label for="externalSSID2">{$t("set.wifi.ssid")}</label>
              <input type="text" id="externalSSID2" value={$systemStore.externalSSID2} placeholder={$t("set.wifi.optional")}
                on:input={(e) => handleFieldChange("externalSSID2", e)} />
              <button class="icon-btn" type="button" title={$t("set.wifi.find")} aria-label={$t("set.wifi.find")}
                disabled={scanBusy || testBusy} on:click={() => startScan(2)}>
                {#if scanBusy && scanSlot === 2}<span class="spin"></span>{:else}<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6" stroke="currentColor" stroke-width="2.8" fill="none" /><path d="M15 15 L20 20" stroke="currentColor" stroke-width="3.2" stroke-linecap="round" /></svg>{/if}
              </button>
              {#if scanSlot === 2 && !scanBusy && scanResults}
                <select class="scan-list wrow-under" on:change={(e) => pickNetwork("externalSSID2", e)}>
                  <option value="">{scanResults.length ? $t("set.wifi.pick", { n: scanResults.length }) : $t("set.wifi.noneFound")}</option>
                  {#each scanResults as net}
                    <option value={net.ssid}>{net.ssid} · {signalText(net.rssi)}{net.secure ? " 🔒" : ""}</option>
                  {/each}
                </select>
              {/if}

              <label for="externalPW2">{$t("set.wifi.pw")}</label>
              <input autocomplete="off" type={pwFocus.externalPW2 ? "text" : "password"} id="externalPW2" value={$systemStore.externalPW2} placeholder={$t("set.wifi.optional")}
                on:focus={() => pwFocus.externalPW2 = true} on:blur={() => pwFocus.externalPW2 = false}
                on:input={(e) => handleFieldChange("externalPW2", e)} />
              <CheckButton title={$t("set.wifi.test")} dirty={dirty2} busy={testBusy && testSlot === 2}
                disabled={scanBusy || testBusy || (!$systemStore.externalSSID2 && !dirty2) || !!pwProblem($systemStore.externalPW2)}
                on:click={() => checkExternal(2)} />
              {#if pwProblem($systemStore.externalPW2)}<small class="test-result bad wrow-under">{pwProblem($systemStore.externalPW2)}</small>{/if}
              {#if testSlot === 2 && testResult}
                <small class="test-result wrow-under {['ok', 'okSaved', 'removed'].includes(testResult) ? 'ok' : testResult === 'running' ? '' : 'bad'}">{testError || $t("set.wifi.result." + testResult)}</small>
              {/if}
            </div>
          </div>

          <small class="hint">{$t("set.wifi.hint")}</small>
        </form>
        </fieldset>
      </div>
    {/if}
  </div>

  <!-- Accordion: Synchronisation (NTP-Intervalle, 32-kHz-Quarz, TSF-/RDS-Fallback, Abgleich von Hand) -->
  <div class="acc-item" class:open={openSection === "sync"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("sync")}>
      <span class="acc-icon">{openSection === "sync" ? "▾" : "▸"}</span>
      {$t("set.acc.sync")}
    </button>
    {#if openSection === "sync"}
      <div class="acc-body">
        <fieldset class="plain" disabled={!$online}>
        <!-- Zeitabgleich übers Heimnetz (NTP): Intervalle nach Erfolg / Fehlschlag, speichern beim Verlassen -->
        <div class="group">
          <div class="nrow nrow-unit">
            <label for="syncIntervalMin">{$t("set.wifi.syncInterval")}</label>
            <input type="number" id="syncIntervalMin" min="5" max="720" step="1" value={$systemStore.syncIntervalMin}
              on:change={(e) => saveMinutes("syncIntervalMin", 5, 720, $t("set.wifi.syncInterval"), e)} />
            <span class="unit">{$t("main.min")}</span>

            <label for="syncRetryMin">{$t("set.wifi.syncRetry")}</label>
            <input type="number" id="syncRetryMin" min="5" max="720" step="1" value={$systemStore.syncRetryMin}
              on:change={(e) => saveMinutes("syncRetryMin", 5, 720, $t("set.wifi.syncRetry"), e)} />
            <span class="unit">{$t("main.min")}</span>
          </div>
        </div>

        <!-- 32-kHz-Quarz: hält die Uhr zwischen den Abgleichen genau (v. a. im Deep Sleep) -->
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
            <!-- Stand: gespeicherte APs, letzter Snapshot, letzte Berechnung mit größter Abweichung -->
            <small>
              {$t("set.clock.tsfStored", { n: tsfStatus.storedAps ?? 0, ntp: tsfStatus.storedNtpAps ?? 0, drift: tsfStatus.storedDriftAps ?? 0 })} ·
              {tsfStatus.lastSnapshotMs > 0 ? $t("set.clock.tsfSnapshotAt", { at: formatAt(tsfStatus.lastSnapshotMs) }) : $t("set.clock.tsfNoSnapshot")}
            </small>
            <!-- Gangverlauf je AP (aus dem Flash): zeigt, ob die Router-Zähler stabil laufen oder springen -->
            {#if tsfStatus.aps && tsfStatus.aps.length}
              <details class="tsf-aps">
                <summary>{$t("set.clock.tsfApsTitle", { n: tsfStatus.aps.length })}</summary>
                <table>
                  <thead><tr><th>AP</th><th>{$t("set.clock.tsfApsHist")}</th><th>{$t("set.clock.tsfApsUsed")}</th></tr></thead>
                  <tbody>
                    {#each tsfStatus.aps as ap}
                      <tr>
                        <td>…{ap.id}{ap.gen > 0 ? ` (${$t("set.clock.tsfApsGen", { g: ap.gen })})` : ""}</td>
                        <td>{ap.hist && ap.hist.length ? ap.hist.map(fmtPpb).join(" · ") : "–"}</td>
                        <td>{ap.drift !== undefined ? fmtPpb(ap.drift) : "–"}</td>
                      </tr>
                    {/each}
                  </tbody>
                </table>
                <small>{$t("set.clock.tsfApsHint")}</small>
              </details>
            {/if}
            <!-- Genauigkeitstest beim Snapshot: TSF-Zeit gegen die per NTP gestellte Uhr -->
            <small>
              {tsfStatus.checkAps > 0
                ? $t("set.clock.tsfCheck", { dev: tsfStatus.checkMaxDevMs, span: fmtSpan(tsfStatus.checkSpanS), aps: tsfStatus.checkAps })
                : $t("set.clock.tsfNoCheck")}
            </small>
            {#if tsfStatus.lastCalcResult && tsfStatus.lastCalcResult !== "none"}
              <small>
                {tsfStatus.lastCalcResult === "success"
                  ? $t("set.clock.tsfOk", { aps: tsfStatus.lastCalcAps, dev: tsfStatus.lastCalcMaxDevMs ?? tsfStatus.lastCalcDeviationMs,
                      src: tsfStatus.lastCalcGen > 0 ? $t("set.clock.tsfSrcDerived", { g: tsfStatus.lastCalcGen }) : $t("set.clock.tsfSrcNtp") })
                  : $t("set.clock.tsfFail", { reason: statusText("tsf", tsfStatus.lastCalcResult) })}
              </small>
            {/if}
            <div class="acc-actions">
              <button class="btn-small btn-blue" type="button" disabled={tsfBusy || tsfStatus.snapshotAllowed === false}
                on:click={handleTsfSnapshot}>
                {tsfBusy ? $t("set.clock.tsfRunning") : $t("set.clock.tsfButton")}
              </button>
            </div>
            {#if tsfStatus.snapshotAllowed === false}<small>{$t("set.clock.tsfNeedNtp")}</small>{/if}
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

        <!-- Letzter absoluter Abgleich (NTP/RDS) und Abgleich von Hand mit Rückmeldung -->
        <div class="warnings-section group">
          <small>{syncInfoText(clockInfo, $t)}</small>
          <div class="acc-actions">
            <button class="btn-small btn-blue" type="button" disabled={syncBusy || clockInfo?.nextSyncInMs === -1}
              on:click={handleNtpResync}>
              {syncBusy ? $t("set.clock.syncRunning") : $t("set.clock.syncNow")}
            </button>
          </div>
        </div>
        </fieldset>
      </div>
    {/if}
  </div>

  <!-- Accordion: LEDs (Anzahl LEDs, Warnungen, Status-LED) -->
  <div class="acc-item" class:open={openSection === "leds"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("leds")}>
      <span class="acc-icon">{openSection === "leds" ? "▾" : "▸"}</span>
      {$t("set.acc.leds")}
    </button>
    {#if openSection === "leds"}
      <div class="acc-body">
        <fieldset class="plain" disabled={!$online}>
        <!-- Wie die anderen Zahlenfelder: Bezeichnung links, Feld rechts; speichert beim Verlassen -->
        <div class="group">
          <div class="nrow">
            <label for="ledCount">{$t("set.sys.ledCount")}</label>
            <input id="ledCount" type="number" min="1" max={MAX_LED_COUNT} value={ledCount}
              on:change={handleLedCountChange} />
          </div>
        </div>

        <section class="warnings-section group">
          <label class="warnings-label">
            <input type="checkbox" bind:checked={warningsEnabled} on:change={handleWarningsToggle} />
            {$t("set.warn.label")}
          </label>
          <small>{$t("set.warn.hint")}</small>
        </section>

        <section class="warnings-section group">
          <label class="warnings-label">
            <input type="checkbox" bind:checked={statusLedEnabled}
              on:change={() => handleDebugToggle("statusLed", statusLedEnabled)} />
            {$t("set.debug.statusLed")}
          </label>
          <small>{$t("set.debug.statusLedHint")}</small>
        </section>
        </fieldset>
      </div>
    {/if}
  </div>

  <!-- Accordion: System (Deep Sleep, Ereignisprotokoll, serielle Logs, Werksreset) -->
  <div class="acc-item" class:open={openSection === "system"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("system")}>
      <span class="acc-icon">{openSection === "system" ? "▾" : "▸"}</span>
      {$t("set.acc.system")}
    </button>
    {#if openSection === "system"}
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
            <input type="checkbox" bind:checked={eventLogEnabled}
              on:change={() => handleDebugToggle("eventLog", eventLogEnabled)} />
            {$t("set.debug.eventLog")}
          </label>
          <small>{$t("set.debug.eventLogHint")}</small>
        </section>

        <section class="warnings-section group">
          <label class="warnings-label">
            <input type="checkbox" bind:checked={serialLogEnabled}
              on:change={() => handleDebugToggle("serialLog", serialLogEnabled)} />
            {$t("set.debug.serialLog")}
          </label>
          <small>{$t("set.debug.serialLogHint")}</small>
          <label for="logOutput" class="sub-label">{$t("set.debug.output")}</label>
          <select id="logOutput" disabled={!serialLogEnabled} value={logToUsb ? "usb" : "uart"}
            on:change={(e) => { logToUsb = e.target.value === "usb"; handleDebugToggle("logUsb", logToUsb); }}>
            <option value="uart">{$t("set.debug.uart")}</option>
            <option value="usb">{$t("set.debug.usb")}</option>
          </select>
          <small>{$t("set.debug.outputHint")}</small>
        </section>

        <div class="acc-actions group">
          <button class="btn-small btn-danger" type="button" disabled={restarting} on:click={handleFactoryReset}>
            {restarting ? $t("set.sys.resetting") : $t("set.sys.factory")}
          </button>
        </div>
        </fieldset>
      </div>
    {/if}
  </div>

  <!-- Accordion: Systemmonitor -->
  <div class="acc-item" class:open={openSection === "sysmon"}>
    <button class="acc-head" type="button" on:click={() => toggleSection("sysmon")}>
      <span class="acc-icon">{openSection === "sysmon" ? "▾" : "▸"}</span>
      {$t("set.acc.sysmon")}
    </button>
    {#if openSection === "sysmon"}
      <div class="acc-body">
        <SystemMonitor />
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
    font-size: 1.15rem;
    font-weight: 600;
    color: inherit;
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }

  /* Kein Pico-Knopfstil am Rubrik-Kopf: kein grauer Hintergrund, kein Rand bei Hover/Fokus/Klick */
  .acc-head:is(:hover, :focus, :active) {
    background: none;
    border: none;
    box-shadow: none;
    color: inherit;
  }

  .acc-head:hover {
    opacity: 0.8;
  }

  /* Offene Rubrik: nur der Titel in der Primärfarbe (wechselt im Dark Mode selbst auf helleres Blau) */
  .acc-item.open > .acc-head,
  .acc-item.open > .acc-head:is(:hover, :focus, :active) {
    color: var(--pico-primary, #0172ad);
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


  /* Hinweistexte: überall wie unter den Schaltern (TSF, RDS, …) — klein, grau */
  .hint,
  .nrow-hint {
    display: block;
    margin-top: 0.25rem;
    font-size: 0.75rem;
    color: var(--secondary-text-color, #888);
  }
  /* Sprachauswahl steht außerhalb des fieldsets → Trennlinie nach unten statt über .group + .group */
  /* Eingabefeld mit quadratischem Icon-Knopf dahinter (Finden / Testen) */
  /* WLAN-Zeilen: Bezeichnung | Feld | Knopf (Finden/Testen) — alle Zeilen im selben Raster */
  .wrow {
    display: grid;
    grid-template-columns: 4.5rem minmax(0, 1fr) 2rem;
    gap: 0.5rem;
    align-items: center;
  }

  .wrow > label {
    margin: 0;
  }

  .wrow input,
  .wrow select {
    margin: 0;
  }

  /* Hinweise und Netzauswahl unter Feld + Knopf (nicht unter der Bezeichnung) */
  .wrow-under {
    grid-column: 2 / -1;
    margin-top: -0.25rem !important;
  }

  /* Rubrik innerhalb einer Sektion (Hotspot, Externes WLAN, …) */
  /* Minutenfelder: Bezeichnung links, schmales Zahlenfeld rechts */
  .nrow {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 5.5rem;
    gap: 0.5rem;
    align-items: center;
    margin-top: 0.5rem;
  }

  /* Minutenzeile: Bezeichnung | Feld | Einheit (wie die Dauer auf der Startseite) */
  .nrow.nrow-unit {
    grid-template-columns: minmax(0, 1fr) 5.5rem auto;
  }

  .nrow > label,
  .nrow input {
    margin: 0;
  }

  .rubric {
    font-weight: 600;
    margin-bottom: 0.5rem;
  }

  .scan-list {
    margin: 0;
  }

  .test-result {
    display: block;
    margin-top: 0.3rem;
    font-size: 0.75rem;
    color: var(--pico-muted-color, #888);
  }

  .test-result.ok {
    color: #22c55e;
  }

  .test-result.bad {
    color: #ef4444;
  }

  /* Drehender Kreis im Knopf, solange gesucht/getestet wird */
  .spin {
    width: 14px;
    height: 14px;
    border: 2px solid currentColor;
    border-right-color: transparent;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }

  @keyframes spin {
    to {
      transform: rotate(360deg);
    }
  }

  .lang-group {
    border-bottom: 1px solid var(--pico-muted-border-color, #ddd);
  }

  .group-title {
    display: block;
    font-weight: 500;
  }

  /* Party-Tag: Tag | Monat | Löschen in einer Zeile */
  .party-row {
    display: grid;
    grid-template-columns: 4.5rem minmax(0, 1fr) 2rem;
    gap: 0.5rem;
    align-items: center;
    margin-top: 0.5rem;
  }

  .party-row input,
  .party-row select {
    margin: 0;
    height: var(--control-h, 2.4rem);
  }

  .add-day {
    margin-top: 0.6rem;
  }

  /* Laufzeit im WLAN-Raster: schmal wie die anderen Zahlenfelder (5.5rem), rechts bündig mit SSID/Passwort */
  .wrow #apTimeoutMin {
    justify-self: end;
    width: 5.5rem;
  }

  /* „min“ in der Knopf-Spalte des WLAN-Rasters: mittig */
  .wunit {
    text-align: center;
    color: var(--pico-muted-color);
  }

  /* Gangverlauf je AP: kleine, aufklappbare Tabelle im Stil der Hinweistexte */
  .tsf-aps {
    margin-top: 0.35rem;
    font-size: 0.75rem;
    color: var(--secondary-text-color, #888);
  }

  .tsf-aps summary {
    cursor: pointer;
  }

  .tsf-aps table {
    width: 100%;
    table-layout: fixed; /* feste Spaltenbreiten: nie breiter als die Seite */
    margin: 0.35rem 0 0;
    font-size: 0.75rem;
  }

  .tsf-aps th,
  .tsf-aps td {
    padding: 0.15rem 0.25rem;
    color: inherit;
    background: none;
    overflow-wrap: anywhere;
    vertical-align: top;
  }

  .tsf-aps th {
    overflow-wrap: normal; /* Überschriften nur an Leerzeichen umbrechen */
  }

  .tsf-aps th:nth-child(1) { width: 30%; }
  .tsf-aps th:nth-child(3) { width: 24%; }

  /* Einheit „min“ hinter Minutenfeldern */
  .nrow .unit {
    color: var(--pico-muted-color);
  }

  .sub-label {
    display: block;
    margin: 0.6rem 0 0.35rem;
    font-size: 0.85rem;
  }

</style>