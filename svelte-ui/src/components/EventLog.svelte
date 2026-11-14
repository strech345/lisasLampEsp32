<script>
  import { onMount, onDestroy } from "svelte";

  let logData = null;
  let loading = false;
  let error = null;
  let refreshInterval = null;

  async function loadLog() {
    loading = true;
    error = null;
    try {
      const res = await fetch("/get_event_log");
      if (res.ok) {
        logData = await res.json();
      } else {
        error = "Fehler beim Laden der Logs";
      }
    } catch (e) {
      error = "Verbindungsfehler: " + e.message;
    } finally {
      loading = false;
    }
  }

  // Format relative time from millisecond delta
  function formatRelative(deltaMs) {
    const s = Math.floor(deltaMs / 1000);
    if (s < 5) return "gerade";
    if (s < 60) return `${s}s`;
    const m = Math.floor(s / 60);
    if (m < 60) return `${m}min`;
    const h = Math.floor(m / 60);
    if (h < 24) return `${h}h ${m % 60}min`;
    return `${Math.floor(h / 24)}d ${h % 24}h`;
  }

  // Format absolute time from unix timestamp (seconds)
  function formatAbsolute(unixSec) {
    const d = new Date(unixSec * 1000);
    const pad = (n) => String(n).padStart(2, "0");
    return `${pad(d.getDate())}.${pad(d.getMonth() + 1)}. ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
  }

  // Get the best absolute time for an event:
  // Prefer event.unixTime if set, otherwise estimate from current unix + uptime diff.
  function eventAbsoluteTime(event) {
    if (!logData) return null;
    if (event.unixTime > 0) {
      return formatAbsolute(event.unixTime);
    }
    // Reconstruct if we now have a known time
    if (logData.unixTime > 0) {
      const diffMs = logData.uptimeMs - event.uptimeMs;
      const estimated = logData.unixTime - Math.floor(diffMs / 1000);
      if (estimated > 1577836800) {
        return "~" + formatAbsolute(estimated);
      }
    }
    return null;
  }

  function eventRelativeTime(event) {
    if (!logData) return "?";
    const delta = logData.uptimeMs - event.uptimeMs;
    return "vor " + formatRelative(delta);
  }

  onMount(() => {
    loadLog();
    refreshInterval = setInterval(loadLog, 10000);
  });

  onDestroy(() => {
    if (refreshInterval) clearInterval(refreshInterval);
  });
</script>

<div class="event-log">
  <div class="log-header">
    <span class="log-title">Event Log</span>
    <button class="btn-small btn-blue" type="button" on:click={loadLog} disabled={loading}>
      {loading ? "…" : "⟳"}
    </button>
  </div>

  {#if error}
    <p class="log-error">{error}</p>
  {:else if !logData}
    <p class="log-empty">Laden…</p>
  {:else if logData.events.length === 0}
    <p class="log-empty">Keine Einträge</p>
  {:else}
    <div class="log-table-wrap">
      <table class="log-table">
        <thead>
          <tr>
            <th>Zeit</th>
            <th>Vor</th>
            <th>Ereignis</th>
          </tr>
        </thead>
        <tbody>
          {#each logData.events as event}
            <tr>
              <td class="col-time">
                {#if eventAbsoluteTime(event)}
                  {eventAbsoluteTime(event)}
                {:else}
                  <span class="no-time">–</span>
                {/if}
              </td>
              <td class="col-rel">{eventRelativeTime(event)}</td>
              <td class="col-msg">{event.msg}</td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
  {/if}
</div>

<style>
  .event-log {
    margin-top: 0.5rem;
  }

  .log-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 0.5rem;
  }

  .log-title {
    font-weight: 600;
    font-size: 0.9rem;
  }

  .log-table-wrap {
    overflow-x: auto;
    max-height: 260px;
    overflow-y: auto;
    border: 1px solid var(--pico-table-border-color, #e0e0e0);
    border-radius: 4px;
  }

  .log-table {
    width: 100%;
    font-size: 0.75rem;
    border-collapse: collapse;
    margin: 0;
  }

  .log-table th {
    position: sticky;
    top: 0;
    background: var(--pico-background-color, #fff);
    z-index: 1;
    padding: 0.3rem 0.5rem;
    text-align: left;
    border-bottom: 1px solid var(--pico-table-border-color, #e0e0e0);
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--pico-muted-color, #888);
  }

  .log-table td {
    padding: 0.25rem 0.5rem;
    border-bottom: 1px solid var(--pico-table-border-color, #f0f0f0);
    vertical-align: top;
  }

  .log-table tr:last-child td {
    border-bottom: none;
  }

  .col-time {
    white-space: nowrap;
    color: var(--pico-muted-color, #888);
    min-width: 90px;
  }

  .col-rel {
    white-space: nowrap;
    color: var(--pico-muted-color, #888);
    min-width: 70px;
  }

  .col-msg {
    word-break: break-word;
  }

  .no-time {
    color: var(--pico-muted-color, #aaa);
  }

  .log-error {
    color: #ef4444;
    font-size: 0.8rem;
  }

  .log-empty {
    font-size: 0.8rem;
    color: var(--pico-muted-color, #888);
  }
</style>
