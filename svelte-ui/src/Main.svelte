<!-- Hauptseite: Farbe, Gute-Nacht-/Wecker-Dauer und Alarmliste. -->
<script>
  import { onMount } from "svelte";
  import { online } from "./stores/connectionStore.js";
  import AlarmTable from "./components/AlarmTable.svelte";
  import LightSets from "./components/LightSets.svelte";
  import MessageAlert from "./components/MessageAlert.svelte";
  import { configStore } from "./stores/configStore.js";
  import { lampName, celebrations } from "./stores/systemStore.js";
  import { messageStore } from "./stores/messageStore.js";
  import Snowfall from "./components/Snowfall.svelte";
  import Balloons from "./components/Balloons.svelte";
  import { t } from "./lib/i18n.js";

  const now = new Date();
  const month = now.getMonth(); // 0-11
  const day = now.getDate();
  
  // Party-Tag (Ballons): Liste kommt von der Lampe, Monat dort 1–12
  $: isCelebration = $celebrations.some((c) => c.month === month + 1 && c.day === day);
  
  // Winter (Schneefall): 15. Dez. bis 15. Feb. (Monate 0-basiert: 11 = Dez, 0 = Jan, 1 = Feb)
  const isWinter = (month === 11 && day >= 15) || // 15.–31. Dezember
                   (month === 0) ||               // Januar
                   (month === 1 && day <= 15);    // 1.–15. Februar

  $: goodNightDuration = $configStore.goodNightDuration || 30;
  $: alarmDuration = $configStore.alarmDuration || 30;

  // Dauer muss eine ganze Zahl von 1 bis 1440 Minuten sein, sonst Meldung und kein Senden
  function validDuration(raw, label, inputEl) {
    const value = Number(raw);
    if (raw === "" || !Number.isInteger(value) || value < 1 || value > 1440) {
      messageStore.show("error", $t("main.durationInvalid", { label }));
      // Anzeige auf den gespeicherten Wert zurücksetzen
      if (inputEl) inputEl.value = inputEl.id === "goodNightDuration" ? goodNightDuration : alarmDuration;
      return null;
    }
    return value;
  }

  // Gute-Nacht-Dauer geändert
  function handleGoodNightChange(event) {
    const value = validDuration(event.target.value, $t("main.goodNightLabel"), event.target);
    if (value !== null) configStore.updateDuration("goodNightDuration", value);
  }

  // Wecker-Dauer geändert
  function handleAlarmChange(event) {
    const value = validDuration(event.target.value, $t("main.alarmLabel"), event.target);
    if (value !== null) configStore.updateDuration("alarmDuration", value);
  }

  onMount(() => {
    configStore.load();
  });

  // Prop: Navigation zur Einstellungsseite
  export let onNavigateToSettings;
</script>

<main class="container">
  {#if isCelebration}
    <Balloons />
  {/if}
  {#if isWinter}
    <Snowfall />
  {/if}
  <div class="main-wrapper">
    <button
      class="btn-smallblue square btn-icon system"
      on:click={onNavigateToSettings}><span class="gear-icon">⚙︎</span></button
    >
    <img src="/meerkat.avif" alt={$t("main.meerkatAlt")} class="meerkat-img" />
    <header class="app-header">
      <h1>{$t("main.title", { name: $lampName })}</h1>
    </header>
    <p class="muted" style="display: block;">
      {$t("main.welcome")}
    </p>

    <MessageAlert />

    <!-- Ohne Verbindung zur Lampe sind alle Eingaben gesperrt (fieldset sperrt alle Felder/Knöpfe) -->
    <fieldset class="plain" disabled={!$online}>


    <section aria-labelledby="section-alarms">
      <h3 id="section-alarms">{$t("main.alarms")}</h3>
      <AlarmTable />
    </section>

    <section aria-labelledby="section-general">
      <h3 id="section-general">{$t("main.lightSets")}</h3>
      <LightSets />
    </section>

    <section aria-labelledby="section-durations">
      <h3 id="section-durations">{$t("main.durations")}</h3>
      <!-- Raster: Bezeichnung | Feld | Einheit → Felder stehen genau untereinander -->
      <div class="duration-grid">
        <label for="goodNightDuration">{$t("main.goodNight")}</label>
        <input
          type="number"
          id="goodNightDuration"
          value={goodNightDuration}
          on:change={handleGoodNightChange}
          min="1"
          max="1440"
        />
        <span class="unit">{$t("main.min")}</span>

        <label for="alarmDuration">{$t("main.alarm")}</label>
        <input
          type="number"
          id="alarmDuration"
          value={alarmDuration}
          on:change={handleAlarmChange}
          min="1"
          max="1440"
        />
        <span class="unit">{$t("main.min")}</span>
      </div>
    </section>
    </fieldset>
  </div>
</main>

<style>
  :global(.btn-blue:hover) {
    background-color: #0052a3 !important;
    border-color: #0052a3 !important;
  }

  /* Fieldset nur als Sperr-Container, ohne eigene Optik */
  :global(fieldset.plain) {
    border: 0;
    padding: 0;
    margin: 0;
    min-width: 0;
  }

  /* Gesperrte Icon-Knöpfe (z. B. Löschen ohne Verbindung) blass */
  :global(.icon-btn:disabled) {
    opacity: 0.45;
    cursor: not-allowed;
  }

  :global(.btn-blue:disabled) {
    background-color: #cccccc !important;
    border-color: #cccccc !important;
    color: #666666 !important;
    cursor: not-allowed !important;
  }

  :global(.btn-icon) {
    padding: 0.5rem !important;
    display: flex !important;
    align-items: center !important;
    justify-content: center !important;
  }

  @media (max-width: 420px) {
    :global(:root) {
      --row-gap: 0.25rem;
      --row-pad: 0.25rem 0.3rem;
    }
  }

  :global(:root) {
    --pico-form-element-spacing-vertical: 0.35rem;
    --pico-form-element-spacing-horizontal: 0.75rem;
    --pico-font-size: 1rem;
    --control-h: 2.4rem; /* einheitliche Feldhöhe in Licht-Sets, Weckern und Dauer */
    /* Gemeinsames Zeilenraster (Sets, Wecker, Dauer): Vorspalte | Feld | Feld | Knopf/Einheit */
    --row-cols: 2.5rem minmax(0, 1fr) minmax(0, 1fr) 2rem;
    --row-gap: 0.35rem;
    --row-pad: 0.3rem 0.4rem;
  }

  /* Quadratischer Icon-Knopf (z. B. Löschen) — gleiche Größe und Farbe wie der Einstellungs-Knopf */
  :global(.icon-btn) {
    flex: 0 0 auto;
    width: 2rem !important;
    height: 2rem !important;
    min-height: 2rem !important;
    padding: 0 !important;
    margin: 0 !important;
    display: inline-flex !important;
    align-items: center;
    justify-content: center;
    background-color: #0172ad !important;
    border-color: #0172ad !important;
    color: #fff !important;
    border-radius: 4px;
  }

  /* Alle Eingabefelder der Zeilen gleich hoch (Zeitfelder sind sonst höher als Auswahllisten) */
  :global(.set-row select),
  :global(.set-row input[type="color"]),
  :global(.alarm-row select),
  :global(.alarm-row input[type="time"]),
  :global(.alarm-row input[type="date"]),
  :global(.duration-grid input[type="number"]) {
    height: var(--control-h) !important;
    min-height: var(--control-h) !important;
    box-sizing: border-box;
    padding-top: 0 !important;
    padding-bottom: 0 !important;
    line-height: calc(var(--control-h) - 2px);
  }

  .app-header {
    position: relative;
    text-align: center;
    margin-bottom: 2rem;
  }

  .meerkat-img {
    height: 50px;
    width: auto;
        position: absolute !important;
    top: 0 !important;
    left: 0 !important;
    z-index: 10;
  }

  .main-wrapper {
    position: relative !important;
  }

  button.system {
    position: absolute !important;
    top: 0 !important;
    right: 0 !important;
    z-index: 10;
    color: white !important;
    font-size: 1.5rem;
    margin-bottom: 2px;
  }

  :global(button.blue) {
    background-color: #0066cc !important;
    border-color: #0066cc !important;
    color: white !important;
  }

  :global(button.square) {
    width: 2rem !important;
    height: 2rem !important;
  }

  /* Override Pico CSS margin-bottom for form elements */
  :global(input:not([type="checkbox"], [type="radio"])),
  :global(select),
  :global(textarea),
  :global(button) {
    margin-bottom: 0 !important;
  }

  .container {
    padding-top: var(--pico-spacing);
  }

  /* Mehr Abstand zwischen Abschnittstitel und dem Inhalt darüber */
  section > h3 {
    margin-top: 2rem;
    margin-bottom: 0.75rem;
  }

  /* Gleiches Raster wie die Wecker-Zeilen: Bezeichnung über Vorspalte + 1. Feldspalte, das Eingabe-
     feld steht genau unter der Uhrzeit der Wecker, "min" in der letzten Spalte */
  .duration-grid {
    display: grid;
    grid-template-columns: var(--row-cols);
    align-items: center;
    gap: 0.5rem var(--row-gap);
    padding: var(--row-pad);
  }

  .duration-grid label {
    grid-column: 1 / 3;
    margin-bottom: 0;
    white-space: nowrap;
  }

  .duration-grid input[type="number"] {
    width: 100%;
    margin: 0 !important;
  }

  .unit {
    opacity: 0.75;
  }
</style>