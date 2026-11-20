<!-- Alarmliste: Tabelle der Wecker mit Aktiv-Schalter, Wochentag, Uhrzeit und Löschen. -->
<script>
  import { configStore } from '../stores/configStore.js';

  const dayNames = {
    1: "Montag",
    2: "Dienstag", 
    3: "Mittwoch",
    4: "Donnerstag",
    5: "Freitag",
    6: "Samstag",
    7: "Sonntag"
  };

  function pad2(n) {
    return n.toString().padStart(2, '0');
  }

  function handleDayChange(index, event) {
    const day = parseInt(event.target.value);
    const alarm = { ...$configStore.alarms[index], day };
    configStore.updateAlarm(index, alarm);
  }

  function handleTimeChange(index, event) {
    const [hour, minute] = event.target.value.split(':').map(x => parseInt(x) || 0);
    const alarm = { ...$configStore.alarms[index], hour, minute };
    configStore.updateAlarm(index, alarm);
  }

  function handleActiveChange(index, event) {
    const active = event.target.checked;
    const alarm = { ...$configStore.alarms[index], active };
    configStore.updateAlarm(index, alarm);
  }

  function removeAlarm(index) {
    configStore.removeAlarm(index);
  }

  function addAlarm() {
    configStore.addAlarm();
  }

  function initSelect(el, day) {
    if (el) el.value = day;
  }

  // Filter out alarms with day === 0 (not set)
  $: activeAlarms = $configStore.alarms
    .map((alarm, index) => ({ ...alarm, index }))
    .filter(alarm => alarm.day > 0);
</script>

<!-- Ein Wecker = immer genau eine Zeile (gleicher Aufbau wie die Licht-Sets) -->
<div class="alarms" aria-live="polite">
  {#each activeAlarms as alarm (alarm.index)}
    <div class="alarm-row" id="alarm-row-{alarm.index}" class:inactive={!alarm.active}>
      <input
        type="checkbox"
        role="switch"
        aria-label="Wecker {alarm.index + 1} aktiv"
        checked={alarm.active}
        on:change={(e) => handleActiveChange(alarm.index, e)}
      />
      <select
        aria-label="Wochentag Wecker {alarm.index + 1}"
        use:initSelect={alarm.day}
        on:change={(e) => handleDayChange(alarm.index, e)}
      >
        {#each Object.entries(dayNames) as [value, name]}
          <option value={value}>{name}</option>
        {/each}
      </select>
      <input
        type="time"
        aria-label="Uhrzeit Wecker {alarm.index + 1}"
        value="{pad2(alarm.hour)}:{pad2(alarm.minute)}"
        on:change={(e) => handleTimeChange(alarm.index, e)}
      />
      <button
        class="btn-small remove"
        type="button"
        on:click={() => removeAlarm(alarm.index)}
        aria-label="Wecker {alarm.index + 1} löschen">×</button
      >
    </div>
  {/each}

  <!-- Bei 10 Weckern (Maximum der Firmware) ausgegraut -->
  <button class="btn-small btn-blue add" type="button" disabled={activeAlarms.length >= 10} on:click={addAlarm}>
    Wecker hinzufügen
  </button>
</div>

<style>
  .alarms {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }

  /* Gleicher Zeilenaufbau wie LightSets.svelte: nie umbrechen, Tag und Uhrzeit teilen sich den Platz */
  .alarm-row {
    display: flex;
    align-items: center;
    flex-wrap: nowrap;
    gap: 0.35rem;
    padding: 0.3rem 0.4rem;
    border-radius: 6px;
    border: 1px solid transparent;
  }

  .alarm-row.inactive {
    opacity: 0.6;
  }

  input[type="checkbox"] {
    flex: 0 0 auto;
    margin: 0 !important;
  }

  select,
  input[type="time"] {
    flex: 1 1 0;
    min-width: 0;
    width: auto;
    margin: 0 !important;
    padding-left: 0.5rem !important;
  }

  select {
    padding-right: 1.6rem !important;
    text-overflow: ellipsis;
  }

  .remove {
    flex: 0 0 2rem;
    width: 2rem;
    padding: 0.2rem !important;
    margin: 0 !important;
  }

  .add {
    align-self: flex-start;
  }


  @media (max-width: 420px) {
    .alarm-row {
      gap: 0.25rem;
      padding: 0.25rem 0.3rem;
    }
    select,
    input[type="time"] {
      font-size: 0.85rem;
      padding-left: 0.35rem !important;
    }
    select {
      padding-right: 1.2rem !important;
      background-position: right 0.25rem center !important;
    }
  }
</style>
