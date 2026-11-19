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

<div class="table-container">
  <table class="alarm-table" id="alarmsContainer" aria-live="polite">
    <thead>
      <tr>
        <th>Aktiv</th>
        <th>Tag</th>
        <th>Zeit</th>
        <th>Löschen</th>
      </tr>
    </thead>
    <tbody>
      {#each activeAlarms as alarm (alarm.index)}
        <tr id="alarm-row-{alarm.index}">
          <td class="center-cell">
            <input type="checkbox" 
                   role="switch" 
                   checked={alarm.active} 
                   on:change={(e) => handleActiveChange(alarm.index, e)}>
          </td>
          <td>
            <select class="table-select" use:initSelect={alarm.day} on:change={(e) => handleDayChange(alarm.index, e)}>
              <option value="0">Nicht gesetzt</option>
              {#each Object.entries(dayNames) as [value, name]}
                <option value={value}>{name}</option>
              {/each}
            </select>
          </td>
          <td>
            <input class="table-time" type="time" 
                   value="{pad2(alarm.hour)}:{pad2(alarm.minute)}" 
                   on:change={(e) => handleTimeChange(alarm.index, e)}>
          </td>
          <td class="center-cell">
            <button class="table-button square btn-blue" type="button" 
                    on:click={() => removeAlarm(alarm.index)}
                    aria-label="Wecker entfernen {alarm.index}">
              ×
            </button>
          </td>
        </tr>
      {/each}
    </tbody>
  </table>
</div>

<div style="display: flex; justify-content: space-between; align-items: center">
  <p><small>Maximal 10 Wecker.</small></p>
  <button class="btn-small btn-blue" type="button" on:click={addAlarm}>Wecker hinzufügen</button>
</div>

<style>
  .table-container {
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
  }
  
  .alarm-table {
    width: 100%;
    font-size: 0.9rem;
    border-collapse: collapse;
  }
  
  .alarm-table th,
  .alarm-table td {
    padding: 0.5rem;
    vertical-align: middle;
    text-align: left;
  }
  
  .center-cell {
    text-align: center;
  }

  
  .table-time {
    padding: 0.5rem;
    min-width: 100px;
  }
  
  .table-button {
    width: 2.5rem;
    padding: 0 !important;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 1rem !important;
  }
</style>