<script>
  import { onMount, onDestroy } from "svelte";
  import AlarmTable from "./components/AlarmTable.svelte";
  import ColorPicker from "./components/ColorPicker.svelte";
  import MessageAlert from "./components/MessageAlert.svelte";
  import SystemSettings from "./components/SystemSettings.svelte";
  import { configStore } from "./stores/configStore.js";
  import { systemStore } from "./stores/systemStore.js";
  import { messageStore } from "./stores/messageStore.js";

  $: goodNightDuration = $configStore.goodNightDuration || 30;
  $: alarmDuration = $configStore.alarmDuration || 30;

  function handleGoodNightChange(event) {
    const value = parseInt(event.target.value);
    if (!isNaN(value)) {
      configStore.updateDuration("goodNightDuration", value);
    }
  }

  function handleAlarmChange(event) {
    const value = parseInt(event.target.value);
    if (!isNaN(value)) {
      configStore.updateDuration("alarmDuration", value);
    }
  }

  let showSystemModal = false;
  let isOffline = false;
  let pingInterval;

  async function checkConnectivity() {
    try {
      const response = await fetch("/ping", { signal: AbortSignal.timeout(5000) });
      
      if (response.ok) {
        if (isOffline) {
          isOffline = false;
          messageStore.hide();
          console.log("Connected to lamp again.");
          // Reload data if we just came back online
          configStore.load();
          systemStore.load();
        }
      } else {
        throw new Error("Ping failed");
      }
    } catch (error) {
      if (!isOffline) {
        isOffline = true;
        messageStore.showPersistent("error", "Verbindung zur Lampe verloren...");
        console.error("Lamp is offline:", error);
      }
    }
  }

  onMount(() => {
    configStore.load();
    systemStore.load();
    
    // Start periodic connectivity check every second
    pingInterval = setInterval(checkConnectivity, 10000);
  });

  onDestroy(() => {
    if (pingInterval) {
      clearInterval(pingInterval);
    }
  });

  function openSystemModal() {
    showSystemModal = true;
  }
</script>

<main class="container">
  <div class="main-wrapper">
    <button
      class="btn-smallblue square btn-icon system"
      on:click={openSystemModal}><span class="gear-icon">⚙︎</span></button
    >
    <header class="app-header">
      <h1>{$systemStore.internalSSID || "Lisa's Lampe"}</h1>
    </header>
    <p class="muted" style="display: block;">
      Viel Spaß mit deiner smarten Lampe ;-)!
    </p>

    <MessageAlert />

    <section aria-labelledby="section-general">
      <h3 id="section-general">Color</h3>
      <ColorPicker />
    </section>

    <section aria-labelledby="section-durations">
      <h3 id="section-durations">Durations</h3>
      <div class="duration-container">
        <div class="duration-item">
          <label for="goodNightDuration">Good Night:</label>
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
          <label for="alarmDuration">Alarm:</label>
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
      <h3 id="section-alarms">Alarms</h3>
      <AlarmTable />
    </section>

    {#if showSystemModal}
      <SystemSettings bind:show={showSystemModal} />
    {/if}
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
