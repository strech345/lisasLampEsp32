<script>
  // Licht-Sets: bis zu 6 Kombinationen aus Lichtfarbe und Animation. Eines ist aktiv; am Drehknopf
  // wechselt "gedrückt halten + drehen" zwischen den Sets.
  import { configStore, MAX_LIGHT_SETS } from "../stores/configStore.js";

  // Lichtfarben; Reihenfolge = Anzeige, id = colorMode in der Firmware
  const colorModes = [
    { id: 4, name: "Kerzenlicht" },
    { id: 2, name: "Warmweiß" },
    { id: 1, name: "Neutralweiß" },
    { id: 0, name: "Kaltweiß" },
    { id: 3, name: "Eigene Farbe" },
  ];

  const animationModes = [
    { id: 0, name: "Statisch" },
    { id: 1, name: "Atmen" },
    { id: 2, name: "Flamme" },
  ];

  $: sets = $configStore.sets || [];
  $: activeSet = $configStore.activeSet ?? 0;

  // RGB-Objekt → "#RRGGBB" für das Farbfeld
  function rgbToHex(c) {
    const { r = 255, g = 255, b = 255 } = c || {};
    return "#" + [r, g, b].map((x) => x.toString(16).padStart(2, "0")).join("");
  }

  // "#RRGGBB" → RGB-Objekt
  function hexToRgb(hex) {
    return {
      r: parseInt(hex.substring(1, 3), 16),
      g: parseInt(hex.substring(3, 5), 16),
      b: parseInt(hex.substring(5, 7), 16),
    };
  }
</script>

<div class="sets">
  {#each sets as set, i}
    <div class="set-row" class:active={i === activeSet}>
      <input
        type="radio"
        name="activeSet"
        aria-label="Set {i + 1} aktivieren"
        title="Dieses Set aktivieren"
        checked={i === activeSet}
        on:change={() => configStore.setActiveSet(i)}
      />

      <!-- Lichtfarbe (+ Farbfeld bei "Eigene Farbe") in einer Spalte → Animation bleibt überall gleich breit -->
      <div class="color-cell">
      <select
        aria-label="Lichtfarbe Set {i + 1}"
        value={set.colorMode}
        on:change={(e) => configStore.updateSet(i, { colorMode: parseInt(e.target.value) })}
      >
        {#each colorModes as mode}
          <option value={mode.id}>{mode.name}</option>
        {/each}
      </select>

      {#if set.colorMode === 3}
        <input
          type="color"
          class="color-input"
          aria-label="Eigene Farbe Set {i + 1}"
          value={rgbToHex(set.color)}
          on:change={(e) => configStore.updateSet(i, { color: hexToRgb(e.target.value) })}
        />
      {/if}
      </div>

      <select
        aria-label="Animation Set {i + 1}"
        value={set.animationMode}
        on:change={(e) => configStore.updateSet(i, { animationMode: parseInt(e.target.value) })}
      >
        {#each animationModes as mode}
          <option value={mode.id}>{mode.name}</option>
        {/each}
      </select>

      {#if sets.length > 1}
        <button
          type="button"
          class="icon-btn"
          aria-label="Set {i + 1} löschen"
          title="Löschen"
          on:click={() => configStore.removeSet(i)}
        >
          <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M5 5 L19 19 M19 5 L5 19" stroke="currentColor" stroke-width="3.2" stroke-linecap="round" fill="none" /></svg>
        </button>
      {/if}
    </div>
  {/each}

  <!-- Bei MAX_LIGHT_SETS Sets ausgegraut -->
  <button
    type="button"
    class="btn-small btn-blue add"
    disabled={sets.length >= MAX_LIGHT_SETS}
    on:click={() => configStore.addSet()}
  >
    Set hinzufügen
  </button>
  <p class="hint">Am Drehknopf: gedrückt halten und drehen wechselt das Set.</p>
</div>

<style>
  .sets {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }

  /* Ein Set = immer genau eine Zeile, auch bei schmalen Bildschirmen: die beiden Auswahllisten
     teilen sich den Platz und dürfen schrumpfen, alles andere hat eine feste kleine Breite */
  .set-row {
    display: grid;
    grid-template-columns: var(--row-cols);
    align-items: center;
    gap: var(--row-gap);
    padding: var(--row-pad);
    border-radius: 6px;
    border: 1px solid transparent;
  }

  .set-row.active {
    border-color: var(--pico-primary, #0066cc);
  }

  input[type="radio"] {
    justify-self: start;
    margin: 0 !important;
  }

  .color-cell {
    display: flex;
    gap: var(--row-gap);
    min-width: 0;
  }

  select {
    flex: 1 1 0;
    min-width: 0;
    width: 100% !important;
    box-sizing: border-box;
    margin: 0 !important;
    padding-left: 0.5rem !important;
    padding-right: 1.6rem !important;
    text-overflow: ellipsis;
  }

  .color-input {
    flex: 0 0 2.2rem;
    width: 2.2rem;
    height: 2.2rem;
    padding: 0.15rem;
    margin: 0 !important;
  }


  .add {
    align-self: flex-start;
  }

  .hint {
    font-size: 0.85rem;
    opacity: 0.75;
    margin: 0;
  }

  /* Schmale Bildschirme: kleinere Schrift und weniger Platz für den Pfeil → mehr Text sichtbar */
  @media (max-width: 420px) {
    select {
      font-size: 0.85rem;
      padding-left: 0.35rem !important;
      padding-right: 1.2rem !important;
      background-position: right 0.25rem center !important;
    }
  }
</style>
