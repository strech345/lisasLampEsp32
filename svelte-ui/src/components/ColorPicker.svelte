<script>
  import { configStore } from "../stores/configStore.js";

  function rgbToHex(r, g, b) {
    return (
      "#" +
      ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1).toUpperCase()
    );
  }

  function hexToRgb(hex) {
    const r = parseInt(hex.substring(1, 3), 16);
    const g = parseInt(hex.substring(3, 5), 16);
    const b = parseInt(hex.substring(5, 7), 16);
    return { r, g, b };
  }

  const colorModes = [
    { id: 0, name: "Cool White", color: { r: 173, g: 216, b: 230 } },
    { id: 1, name: "Neutral White", color: { r: 255, g: 255, b: 255 } },
    { id: 2, name: "Warm White", color: { r: 255, g: 200, b: 150 } },
    { id: 3, name: "Custom", color: null },
  ];

  const animationModes = [
    { id: 0, name: "Static" },
    { id: 1, name: "Breathing" },
  ];

  $: colorHex = rgbToHex(
    $configStore.override_color.r,
    $configStore.override_color.g,
    $configStore.override_color.b
  );
  $: selectedMode = $configStore.colorMode || 0;
  $: selectedAnimationMode = $configStore.animationMode || 0;
  $: animationSpeed = $configStore.animationSpeed || 200;
  $: isCustomMode = selectedMode === 3;

  function handleColorChange(event) {
    const { r, g, b } = hexToRgb(event.target.value);
    configStore.updateColor(r, g, b);
  }

  function handleModeChange(event) {
    const modeId = parseInt(event.target.value);
    console.log("mode change", modeId);
    configStore.setColorMode(modeId);
  }

  function handleAnimationModeChange(event) {
    const modeId = parseInt(event.target.value);
    console.log("animation mode change", modeId);
    configStore.setAnimationMode(modeId);
  }

  function handleAnimationSpeedChange(event) {
    const speed = parseInt(event.target.value);
    configStore.setAnimationSpeed(speed);
  }
</script>

<form>
  <div class="color-picker-container">
    <label for="animationMode">Animation Mode:</label>
    <select
      id="animationMode"
      value={selectedAnimationMode}
      on:change={handleAnimationModeChange}
      class="color-mode-select"
    >
      {#each animationModes as mode}
        <option value={mode.id}>{mode.name}</option>
      {/each}
    </select>
    {#if selectedAnimationMode !== 0}
      <div class="speed-control">
        <label for="animationSpeed">Speed:</label>
        <input
          type="range"
          id="animationSpeed"
          min="10"
          max="5000"
          step="10"
          value={animationSpeed}
          on:change={handleAnimationSpeedChange}
          style="margin-bottom: 0;"
        />
        <span class="muted">{animationSpeed}ms</span>
      </div>
    {/if}
  </div>

  <div class="color-picker-container">
    <div class="color-mode-group">
      <label for="colorMode">Color Mode:</label>
      <select
        id="colorMode"
        value={selectedMode}
        on:change={handleModeChange}
        class="color-mode-select"
      >
        {#each colorModes as mode}
          <option value={mode.id}>{mode.name}</option>
        {/each}
      </select>
    </div>

    {#if isCustomMode}
      <input
        type="color"
        id="colorPicker"
        value={colorHex}
        on:change={handleColorChange}
        class="color-input"
      />
    {/if}
  </div>
</form>

<style>
  .color-picker-container {
    display: flex;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
    margin-bottom: 1rem;
  }

  .color-mode-group {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }

  label {
    margin-bottom: 0;
    font-weight: 500;
    white-space: nowrap;
  }

  .color-mode-select {
    width: 200px;
  }

  .color-input {
    width: 3rem;
    height: 2.5rem;
    padding: 0.25rem;
    border-radius: 4px;
  }

  @media (max-width: 480px) {
    .color-picker-container {
      gap: 0.5rem;
    }

    .color-mode-group {
      gap: 0.25rem;
    }

    .color-mode-select {
      width: 120px;
      min-width: 120px;
    }

    .color-input {
      width: 2.5rem;
      height: 2rem;
    }
  }

  .speed-control {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex-grow: 1;
    min-width: 250px;
  }

  .speed-control input[type="range"] {
    flex-grow: 1;
  }

  .muted {
    font-size: 0.8rem;
    color: #888;
    white-space: nowrap;
    min-width: 50px;
  }
</style>
