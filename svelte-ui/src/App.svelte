<!-- App-Rahmen: Seitenwechsel Hauptseite ↔ Einstellungen und Verbindungsprüfung zur Lampe. -->
<script>
  import { onMount, onDestroy } from "svelte";
  import Main from "./Main.svelte";
  import Settings from "./Settings.svelte";
  import { ping } from "./lib/api.js";
  import { online } from "./stores/connectionStore.js";
  import { configStore } from "./stores/configStore.js";
  import { messageStore } from "./stores/messageStore.js";

  let page = "main"; // 'main' | 'settings'
  let pingInterval;

  // Zur Einstellungsseite wechseln
  function navigateToSettings() {
    page = "settings";
  }

  // Zur Hauptseite zurück
  function navigateToMain() {
    page = "main";
  }

  // Prüft die Verbindung; bei Verlust alle Eingaben sperren (online=false) und Hinweis zeigen,
  // bei Rückkehr Hinweis ausblenden und die Konfiguration neu laden
  async function checkConnectivity() {
    const ok = await ping();
    let wasOnline;
    online.update((v) => ((wasOnline = v), ok));
    if (ok && !wasOnline) {
      messageStore.hide();
      configStore.load();
    } else if (ok && page === "main") {
      configStore.syncActiveSet(); // Set-Wechsel am Drehknopf sichtbar machen
    } else if (!ok && wasOnline) {
      messageStore.showPersistent("error", "Verbindung zur Lampe verloren – Eingaben sind gesperrt.");
    }
  }

  onMount(() => {
    checkConnectivity(); // sofort beim Start
    pingInterval = setInterval(checkConnectivity, 10000);
  });

  onDestroy(() => clearInterval(pingInterval));
</script>

{#if page === "main"}
  <Main onNavigateToSettings={navigateToSettings} />
{:else if page === "settings"}
  <Settings onNavigateBack={navigateToMain} />
{/if}
