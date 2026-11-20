<!-- Weckerliste: je Wecker eine Zeile mit Aktiv-Schalter, Wochentag oder Datum (einmalig), Uhrzeit, Löschen. -->
<script>
  import { configStore, MAX_ALARMS } from '../stores/configStore.js';
  import { getJson } from '../lib/api.js';

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

  // Datum als "JJJJ-MM-TT" (lokale Zeit), Format des HTML-Datumsfelds
  function isoDate(d) {
    return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())}`;
  }

  // Heutiges Datum laut Lampenuhr; ohne gültige Uhrzeit (z. B. ohne Internet) der 1.1.2026
  async function lampToday() {
    try {
      const status = await getJson("/get_status");
      if (status.systemTime >= 1577836800) return isoDate(new Date(status.systemTime * 1000));
    } catch {}
    return "2026-01-01";
  }

  // Wochentag gewählt — oder "Datum": dann wird der Eintrag dauerhaft ein einmaliger Termin
  // (Datumsfeld statt Wochentag, ein Zurück zum Wochentag gibt es nicht)
  async function handleDayChange(index, event) {
    const current = $configStore.alarms[index];
    if (event.target.value === "date") {
      const date = await lampToday();
      configStore.updateAlarm(index, { ...current, once: true, day: 0, date });
    } else {
      configStore.updateAlarm(index, { ...current, day: parseInt(event.target.value) });
    }
  }

  // Datum eines einmaligen Weckers geändert
  function handleDateChange(index, event) {
    if (!event.target.value) return;
    const alarm = { ...$configStore.alarms[index], date: event.target.value };
    configStore.updateAlarm(index, alarm);
  }

  // Einmaliger Termin, der schon vorbei ist (wird blass angezeigt)
  function isPast(alarm) {
    if (!alarm.once || !alarm.date) return false;
    const [y, m, d] = alarm.date.split("-").map(Number);
    return new Date(y, m - 1, d, alarm.hour, alarm.minute) < new Date();
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

  // Neuer wöchentlicher Wecker (Montag 7:00)
  function addAlarm() {
    configStore.addAlarm();
  }


  // Belegte Plätze: wöchentlich (day > 0) oder einmalig (once)
  $: activeAlarms = $configStore.alarms
    .map((alarm, index) => ({ ...alarm, index }))
    .filter((alarm) => alarm.day > 0 || alarm.once);
</script>

<!-- Ein Wecker = immer genau eine Zeile (gleicher Aufbau wie die Licht-Sets) -->
<div class="alarms" aria-live="polite">
  {#each activeAlarms as alarm (alarm.index)}
    <div
      class="alarm-row"
      id="alarm-row-{alarm.index}"
      class:inactive={!alarm.active || isPast(alarm)}
      title={isPast(alarm) ? "Termin ist vorbei" : ""}
    >
      <input
        type="checkbox"
        role="switch"
        aria-label="Wecker {alarm.index + 1} aktiv"
        checked={alarm.active}
        on:change={(e) => handleActiveChange(alarm.index, e)}
      />
      <!-- Wöchentlich: Wochentag; einmaliger Termin: Datum an derselben Stelle, gleich groß -->
      {#if alarm.once}
        <input
          type="date"
          aria-label="Datum Wecker {alarm.index + 1}"
          value={alarm.date}
          on:change={(e) => handleDateChange(alarm.index, e)}
        />
      {:else}
        <select
          aria-label="Wochentag Wecker {alarm.index + 1}"
          value={String(alarm.day)}
          on:change={(e) => handleDayChange(alarm.index, e)}
        >
          {#each Object.entries(dayNames) as [value, name]}
            <option value={value}>{name}</option>
          {/each}
          <option value="date">Datum</option>
        </select>
      {/if}
      <input
        type="time"
        aria-label="Uhrzeit Wecker {alarm.index + 1}"
        value="{pad2(alarm.hour)}:{pad2(alarm.minute)}"
        on:change={(e) => handleTimeChange(alarm.index, e)}
      />
      <button
        class="icon-btn"
        type="button"
        title="Löschen"
        on:click={() => removeAlarm(alarm.index)}
        aria-label="Wecker {alarm.index + 1} löschen"
      >
        <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M5 5 L19 19 M19 5 L5 19" stroke="currentColor" stroke-width="3.2" stroke-linecap="round" fill="none" /></svg>
      </button>
    </div>
  {/each}

  <!-- Beim Maximum der Firmware (MAX_ALARMS) ausgegraut -->
  <button class="btn-small btn-blue add" type="button" disabled={activeAlarms.length >= MAX_ALARMS} on:click={addAlarm}>
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
  /* Raster: Schalter | Tag/Datum | Uhrzeit | ×  — die beiden mittleren Spalten sind exakt gleich breit */
  .alarm-row {
    display: grid;
    grid-template-columns: auto minmax(0, 1fr) minmax(0, 1fr) auto;
    align-items: center;
    gap: 0.35rem;
    padding: 0.3rem 0.4rem;
    border-radius: 6px;
    border: 1px solid transparent;
  }

  /* Inaktive/vergangene Wecker: nur die Felder blass, der Lösch-Knopf bleibt voll sichtbar */
  .alarm-row.inactive > :not(.icon-btn) {
    opacity: 0.6;
  }

  input[type="checkbox"] {
    flex: 0 0 auto;
    margin: 0 !important;
  }

  select,
  input[type="time"],
  input[type="date"] {
    box-sizing: border-box;
    min-width: 0;
    width: 100% !important;
    margin: 0 !important;
    padding-left: 0.5rem !important;
  }

  select {
    padding-right: 1.6rem !important;
    text-overflow: ellipsis;
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
    input[type="time"],
    input[type="date"] {
      font-size: 0.85rem;
      padding-left: 0.35rem !important;
    }
    select {
      padding-right: 1.2rem !important;
      background-position: right 0.25rem center !important;
    }
    /* Kalender-/Uhr-Symbole ausblenden (Pico zeichnet sie als Hintergrundbild): mehr Platz für
       Datum und Uhrzeit; Antippen öffnet die Auswahl trotzdem */
    input[type="time"],
    input[type="date"] {
      background-image: none !important;
      padding-right: 0.35rem !important;
    }
    .alarm-row :global(input[type="date"]::-webkit-calendar-picker-indicator),
    .alarm-row :global(input[type="time"]::-webkit-calendar-picker-indicator) {
      display: none;
    }
  }
</style>
