<!-- Haken-Knopf der WLAN-Sektion (Testen bzw. Speichern einer Zeile).
     Voller Haken = Stand ist gespeichert, gestrichelter Haken = ungespeicherte Änderungen (dirty).
     Während busy dreht sich ein Ladekreis. Das Klick-Ereignis wird an die Elternkomponente weitergereicht. -->
<script>
  export let dirty = false;    // ungespeicherte Änderungen in den zugehörigen Feldern
  export let busy = false;     // Aktion läuft (Ladekreis statt Haken)
  export let disabled = false;
  export let title = "";
</script>

<button class="icon-btn" type="button" {title} aria-label={title} {disabled} on:click>
  {#if busy}
    <span class="spin"></span>
  {:else if dirty}
    <!-- gestrichelte Kontur: noch nicht gespeichert -->
    <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true" fill="none" stroke="currentColor"
      stroke-width="1.8" stroke-linejoin="round" stroke-dasharray="2 1.4">
      <path d="M2.8 12.6 L5 10.4 L9.9 15.3 L18.9 4.6 L21.3 6.6 L10.1 19.9 Z" />
    </svg>
  {:else}
    <!-- voller Haken: gespeichert -->
    <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true" fill="currentColor" stroke="currentColor"
      stroke-width="1" stroke-linejoin="round">
      <path d="M2.8 12.6 L5 10.4 L9.9 15.3 L18.9 4.6 L21.3 6.6 L10.1 19.9 Z" />
    </svg>
  {/if}
</button>

<style>
  /* Ladekreis (gleiche Optik wie die Lupe in Settings.svelte) */
  .spin {
    display: inline-block;
    width: 14px;
    height: 14px;
    border: 2px solid currentColor;
    border-right-color: transparent;
    border-radius: 50%;
    animation: cb-spin 0.8s linear infinite;
  }

  @keyframes cb-spin {
    to {
      transform: rotate(360deg);
    }
  }
</style>
