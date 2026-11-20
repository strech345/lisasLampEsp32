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
      <label class="set-select" title="Dieses Set aktivieren">
        <input
          type="radio"
          name="activeSet"
          checked={i === activeSet}
          on:change={() => configStore.setActiveSet(i)}
        />
        Set {i + 1}
      </label>

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
          class="btn-small remove"
          aria-label="Set {i + 1} löschen"
          on:click={() => configStore.removeSet(i)}>×</button
        >
      {/if}
    </div>
  {/each}

  {#if sets.length < MAX_LIGHT_SETS}
    <button type="button" class="btn-small btn-blue add" on:click={() => configStore.addSet()}>
      Set hinzufügen
    </button>
  {/if}
  <p class="hint">Am Drehknopf: gedrückt halten und drehen wechselt das Set.</p>
</div>

<style>
  .sets {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .set-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    flex-wrap: wrap;
    padding: 0.35rem 0.5rem;
    border-radius: 6px;
    border: 1px solid transparent;
  }

  .set-row.active {
    border-color: var(--pico-primary, #0066cc);
  }

  .set-select {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    margin-bottom: 0;
    white-space: nowrap;
    min-width: 4.5rem;
  }

  select {
    width: auto;
    min-width: 8rem;
    flex: 1 1 8rem;
  }

  .color-input {
    width: 2.75rem;
    height: 2.25rem;
    padding: 0.2rem;
  }

  .remove {
    width: 2rem;
    padding: 0.2rem !important;
  }

  .add {
    align-self: flex-start;
  }

  .hint {
    font-size: 0.85rem;
    opacity: 0.75;
    margin: 0;
  }
</style>
