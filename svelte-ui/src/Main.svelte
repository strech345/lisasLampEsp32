<!-- Hauptseite: Farbe, Gute-Nacht-/Wecker-Dauer und Alarmliste. -->
<script>
  import { onMount, onDestroy } from "svelte";
  import AlarmTable from "./components/AlarmTable.svelte";
  import ColorPicker from "./components/ColorPicker.svelte";
  import MessageAlert from "./components/MessageAlert.svelte";
  import { configStore } from "./stores/configStore.js";
  import { messageStore } from "./stores/messageStore.js";
  import Snowfall from "./components/Snowfall.svelte";
  import Balloons from "./components/Balloons.svelte";

  const now = new Date();
  const month = now.getMonth(); // 0-11
  const day = now.getDate();
  
  const isCelebration = (month === 0 && day === 1) || // Jan 1
                         (month === 7 && day === 8) || // Aug 8
                         (month === 8 && day === 2) || // Sep 2 
                         (month === 11 && day === 24); // 24. Dez (getMonth ist 0-basiert)
  
  // Winter: 15. Nov. bis 15. Feb. (Monate 0-basiert: 10 = Nov, 11 = Dez, 0 = Jan, 1 = Feb)
  const isWinter = (month === 10 && day >= 15) || // 15.-30. Nov
                   (month === 11) ||              // Dezember
                   (month === 0) ||               // Januar
                   (month === 1 && day <= 15);    // 1.-15. Feb

  $: goodNightDuration = $configStore.goodNightDuration || 30;
  $: alarmDuration = $configStore.alarmDuration || 30;

  // Dauer muss eine ganze Zahl von 1 bis 1440 Minuten sein, sonst Meldung und kein Senden
  function validDuration(raw, label, inputEl) {
    const value = Number(raw);
    if (raw === "" || !Number.isInteger(value) || value < 1 || value > 1440) {
      messageStore.show("error", `${label}: Bitte eine ganze Zahl von 1 bis 1440 Minuten eingeben.`);
      // Anzeige auf den gespeicherten Wert zurücksetzen
      if (inputEl) inputEl.value = inputEl.id === "goodNightDuration" ? goodNightDuration : alarmDuration;
      return null;
    }
    return value;
  }

  // Gute-Nacht-Dauer geändert
  function handleGoodNightChange(event) {
    const value = validDuration(event.target.value, "Gute-Nacht-Dauer", event.target);
    if (value !== null) configStore.updateDuration("goodNightDuration", value);
  }

  // Wecker-Dauer geändert
  function handleAlarmChange(event) {
    const value = validDuration(event.target.value, "Wecker-Dauer", event.target);
    if (value !== null) configStore.updateDuration("alarmDuration", value);
  }

  let isOffline = false;
  let pingInterval;

  async function checkConnectivity() {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 5000);

    try {
      const response = await fetch("/ping", { signal: controller.signal });
      clearTimeout(timeoutId);
      
      if (response.ok) {
        if (isOffline) {
          isOffline = false;
          messageStore.hide();
          console.log("Wieder mit der Lampe verbunden.");
          // Daten neu laden, wenn die Verbindung wieder da ist
          configStore.load();
        }
      } else {
        throw new Error("Ping failed");
      }
    } catch (error) {
      if (!isOffline) {
        isOffline = true;
        messageStore.showPersistent("error", "Verbindung zur Lampe verloren...");
        console.error("Lampe nicht erreichbar:", error);
      }
    }
  }

  onMount(() => {
    configStore.load();

    // Verbindung alle 10 s prüfen
    pingInterval = setInterval(checkConnectivity, 10000);
  });

  onDestroy(() => {
    if (pingInterval) {
      clearInterval(pingInterval);
    }
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
    <img src="/meerkat.avif" alt="Meerkat" class="meerkat-img" />
    <header class="app-header">
      <h1>Lisa's Lampe</h1>
    </header>
    <p class="muted" style="display: block;">
      Viel Spaß mit deiner smarten Lampe ;-)!
    </p>

    <MessageAlert />

    <section aria-labelledby="section-general">
      <h3 id="section-general">Farbe</h3>
      <ColorPicker />
    </section>

    <section aria-labelledby="section-durations">
      <h3 id="section-durations">Dauer (Minuten)</h3>
      <div class="duration-container">
        <div class="duration-item">
          <label for="goodNightDuration">Gute Nacht:</label>
          <input
            type="number"
            id="goodNightDuration"
            value={goodNightDuration}
            on:change={handleGoodNightChange}
            min="1"
            max="1440"
          />
        </div>
        <div class="duration-item">
          <label for="alarmDuration">Wecker:</label>
          <input
            type="number"
            id="alarmDuration"
            value={alarmDuration}
            on:change={handleAlarmChange}
            min="1"
            max="1440"
          />
        </div>
      </div>
    </section>

    <section aria-labelledby="section-alarms">
      <h3 id="section-alarms">Wecker</h3>
      <AlarmTable />
    </section>
  </div>
</main>

<style>
  :global(.btn-blue:hover) {
    background-color: #0052a3 !important;
    border-color: #0052a3 !important;
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

  :global(:root) {
    --pico-form-element-spacing-vertical: 0.35rem;
    --pico-form-element-spacing-horizontal: 0.75rem;
    --pico-font-size: 1rem;
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

  .duration-container {
    display: flex;
    gap: 1rem;
    flex-direction: column;
  }

  .duration-item {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }

  .duration-item label {
    margin-bottom: 0;
    white-space: nowrap;
  }

  .duration-item input[type="number"] {
    width: 80px;
  }
</style>