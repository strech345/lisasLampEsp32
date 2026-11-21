<!-- Systemmonitor: fragt /get_system_info alle 5 s ab, zeichnet CPU-Last, RAM und WLAN-Signal zum Router als SVG-Liniendiagramm
     und zeigt darunter die Kennzahlen der letzten Abfrage in Gruppen: Uhr (Systemzeit zum Abfragezeitpunkt, letzte
     Abgleiche, nächster NTP-Abgleich),
     WLAN, Speicher, System. Der Verlauf liegt im systemInfoStore. -->
<script>
  import { onMount, onDestroy } from "svelte";
  import { getJson } from "../lib/api.js";
  import { t, lang, localeFor } from "../lib/i18n.js";
  import { systemInfoHistory, addSystemInfoPoint, MAX_POINTS } from "../stores/systemInfoStore.js";

  const INTERVAL_MS = 5000;
  // Diagramm-Geometrie im viewBox-Koordinatensystem (skaliert per width:100%)
  const W = 320, H = 150, PL = 28, PR = 6, PT = 6, PB = 18;

  let info = null; // letzte Antwort des Geräts
  let offline = false; // true, wenn die letzte Abfrage fehlschlug
  let busy = false; // verhindert überlappende Anfragen
  let timer = null;

  // Signalqualität in Prozent aus dBm: -100 dBm = 0 %, -50 dBm = 100 % (begrenzt auf 0..100)
  const rssiToPct = (rssi) => Math.min(100, Math.max(0, 2 * (rssi + 100)));

  // Holt einmal die Systemdaten und hängt einen Messpunkt an den Verlauf an
  async function poll() {
    if (busy) return; // läuft noch eine Anfrage: überspringen
    busy = true;
    try {
      const d = await getJson("/get_system_info");
      info = d;
      offline = false;
      // Eigenes Gerät am Hotspot (you=true); fehlt es, ist man über den Router verbunden
      const me = (d.apClients || []).find((c) => c.you);
      addSystemInfoPoint({
        t: Date.now(),
        cpu: d.cpuLoad, // -1 = noch nicht messbar (Lücke im Diagramm)
        heapUsedPct: d.heapTotal > 0 ? (100 * (d.heapTotal - d.heapFree)) / d.heapTotal : 0,
        heapFree: d.heapFree,
        rssi: d.rssi,
        wifiPct: d.rssi === 0 ? -1 : rssiToPct(d.rssi), // -1 = nicht verbunden (Lücke im Diagramm)
        clientPct: me ? rssiToPct(me.rssi) : -1, // eigenes Gerät → Hotspot; -1 = nicht am Hotspot (Lücke)
        tempC: d.tempC,
      });
    } catch (e) {
      offline = true; // still: letzte Werte bleiben, nur Hinweis
    } finally {
      busy = false;
    }
  }

  onMount(() => {
    poll(); // sofort beim Öffnen
    timer = setInterval(poll, INTERVAL_MS);
  });
  onDestroy(() => {
    if (timer) clearInterval(timer);
  });

  // --- Uhr-Gruppe ---
  // Gerätezeit zum Zeitpunkt der letzten Abfrage (ms); läuft bewusst nicht weiter. null ohne Antwort
  $: deviceNowMs = info && info.clock && info.clock.now ? info.clock.now * 1000 : null;

  // Datum + Uhrzeit in der Sprache der Oberfläche
  const fmtDateTime = (ms, _l) =>
    new Date(ms).toLocaleString(localeFor($lang), { day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit", second: "2-digit" });
  const fmtShort = (ms, _l) =>
    new Date(ms).toLocaleString(localeFor($lang), { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" });

  // Abstand in der Vergangenheit als "vor …" (gegen die Gerätezeit gerechnet, nicht die Handyzeit)
  function ago(ms, _t) {
    const s = Math.max(0, Math.floor(ms / 1000));
    if (s < 60) return $t("set.sysmon.agoSec", { n: s });
    const m = Math.floor(s / 60);
    if (m < 60) return $t("set.sysmon.agoMin", { n: m });
    if (m < 1440) return $t("set.sysmon.agoHour", { h: Math.floor(m / 60), m: m % 60 });
    return $t("set.sysmon.agoDay", { d: Math.floor(m / 1440) });
  }

  // Zeitpunkt eines Abgleichs (Unix-Sekunden, 0 = nie) als "TT.MM. hh:mm · vor …"
  function syncText(atSec, nowMs, _t) {
    if (!atSec) return $t("set.sysmon.never");
    const atMs = atSec * 1000;
    return `${fmtShort(atMs, $lang)} · ${ago((nowMs ?? atMs) - atMs, $t)}`;
  }

  // Absolut = jüngerer der beiden echten Zeitquellen NTP (Internet) und RDS (Radio), mit Quelle
  $: clk = info && info.clock;
  $: absAt = clk ? Math.max(clk.ntpAt || 0, clk.rdsAt || 0) : 0;
  $: absSrc = clk && absAt ? (clk.ntpAt >= clk.rdsAt ? "NTP" : "RDS") : "";

  // Nächster NTP-Abgleich: -1 = kein Heimnetz, 0 = läuft gerade, sonst Restzeit
  function nextText(ms, _t) {
    if (ms === -1) return $t("set.sysmon.noHomeWifi");
    if (ms === 0) return $t("set.sysmon.running");
    const min = Math.ceil(ms / 60000);
    return min < 60 ? $t("set.sysmon.inMin", { n: min }) : $t("set.sysmon.inHour", { h: Math.floor(min / 60), m: min % 60 });
  }

  // Linien: Schlüssel im Messpunkt, i18n-Label, CSS-Farbvariable
  const series = [
    { key: "cpu", label: "set.sysmon.cpu", color: "var(--sm-cpu)" },
    { key: "heapUsedPct", label: "set.sysmon.ram", color: "var(--sm-ram)" },
    { key: "wifiPct", label: "set.sysmon.wifiRouter", color: "var(--sm-wifi)" },
    { key: "clientPct", label: "set.sysmon.wifiClient", color: "var(--sm-client)" },
  ];

  $: hist = $systemInfoHistory;
  $: tFirst = hist.length ? hist[0].t : Date.now();
  // Zeitspanne in ms; mindestens 1 min, damit wenige Punkte nicht die ganze Breite füllen
  $: span = Math.max(60000, (hist.length ? hist[hist.length - 1].t : Date.now()) - tFirst);
  $: spanMin = Math.round(span / 60000);

  // Rechnet Zeit und Prozent in viewBox-Koordinaten um
  const px = (tt) => PL + ((tt - tFirst) / span) * (W - PL - PR);
  const py = (v) => PT + (1 - Math.min(100, Math.max(0, v)) / 100) * (H - PT - PB);

  // Abstand, ab dem zwei Punkte nicht verbunden werden (Rubrik war zu / Verbindung weg)
  const GAP_MS = INTERVAL_MS * 3;

  // Baut den Pfad einer Reihe; ungültige Werte (<0) und Zeitlücken beenden das Segment
  function pathFor(list, key, _span) {
    let d = "";
    let pen = false; // true = nächster Punkt wird mit Linie verbunden
    let prevT = 0; // Zeitstempel des vorigen Punkts
    for (const p of list) {
      const v = p[key];
      if (p.t - prevT > GAP_MS) pen = false; // Lücke im Verlauf: neu ansetzen
      prevT = p.t;
      if (v == null || v < 0) { pen = false; continue; }
      d += `${pen ? "L" : "M"}${px(p.t).toFixed(1)},${py(v).toFixed(1)}`;
      pen = true;
    }
    return d;
  }

  $: paths = series.map((s) => pathFor(hist, s.key, span + tFirst));
  $: last = hist.length ? hist[hist.length - 1] : null;

  // Zahlenformat: ein Punkt für Dezimalstellen, KB/MB gerundet
  const kb = (b) => Math.round(b / 1024);
  const mb = (b) => (b / 1048576).toFixed(b % 1048576 ? 1 : 0);

  // Laufzeit als "d h min"
  function fmtUptime(ms) {
    const m = Math.floor(ms / 60000);
    const d = Math.floor(m / 1440), h = Math.floor((m % 1440) / 60);
    return (d ? `${d}d ` : "") + `${h}h ${m % 60}min`;
  }

  // Neustartgrund: bekannte Werte übersetzen, unbekannte roh anzeigen
  const knownReasons = ["poweron", "software", "panic", "int_wdt", "task_wdt", "wdt", "deepsleep", "brownout", "sdio", "ext", "unknown"];
  function reasonText(r, _t) {
    return knownReasons.includes(r) ? $t("set.sysmon.reason." + r) : r;
  }

  // Wert für Legende (Prozent oder Strich)
  const pct = (v) => (v == null || v < 0 ? "–" : Math.round(v) + " %");
</script>

<div class="sysmon">
  {#if offline}
    <small class="hint sm-offline">{$t("set.sysmon.offline")}</small>
  {/if}

  <div class="sm-legend">
    {#each series as s}
      <span class="sm-leg"><i style="background:{s.color}"></i>{$t(s.label)} <b>{last ? pct(last[s.key]) : "–"}</b></span>
    {/each}
  </div>

  <svg viewBox="0 0 {W} {H}" width="100%" role="img" aria-label={$t("set.sysmon.chart")}>
    {#each [0, 25, 50, 75, 100] as g}
      <line x1={PL} x2={W - PR} y1={py(g)} y2={py(g)} class="sm-grid" />
      <text x={PL - 4} y={py(g) + 3} class="sm-axis" text-anchor="end">{g}</text>
    {/each}
    {#each series as s, i}
      <path d={paths[i]} fill="none" stroke={s.color} stroke-width="1.6" stroke-linejoin="round" />
    {/each}
    <text x={PL} y={H - 4} class="sm-axis">{$t("set.sysmon.axisPast", { min: spanMin })}</text>
    <text x={W - PR} y={H - 4} class="sm-axis" text-anchor="end">{$t("set.sysmon.axisNow")}</text>
  </svg>

  {#if info}
    {#if clk}
      <h4 class="sm-group">{$t("set.sysmon.grpClock")}</h4>
      <dl class="sm-list">
        <dt>{$t("set.sysmon.systemTime")}</dt><dd>{deviceNowMs ? fmtDateTime(deviceNowMs, $lang) : "–"}</dd>
        <dt>{$t("set.sysmon.absSync")}</dt><dd>{syncText(absAt, deviceNowMs, $t)}{absSrc ? ` (${absSrc})` : ""}</dd>
        <dt>{$t("set.sysmon.relSync")}</dt><dd>{syncText(clk.tsfAt, deviceNowMs, $t)}</dd>
        {#if clk.nextSyncInMs !== -1}
          <dt>{$t("set.sysmon.lastAttempt")}</dt>
          <dd class={clk.lastSyncOk ? "sm-ok" : "sm-bad"}>{clk.lastSyncOk ? $t("set.sysmon.ok") : $t("set.sysmon.failed")}</dd>
        {/if}
        <dt>{$t("set.sysmon.nextSync")}</dt><dd>{nextText(clk.nextSyncInMs, $t)}</dd>
      </dl>
    {/if}

    <h4 class="sm-group">{$t("set.sysmon.grpWifi")}</h4>
    <dl class="sm-list">
      {#if clk}
        <dt>{$t("set.sysmon.homeWifi")}</dt><dd>{clk.staActive ? $t("set.sysmon.staOn") : $t("set.sysmon.staOff")}</dd>
      {/if}
      <dt>{$t("set.sysmon.wifi")}</dt><dd>{info.rssi === 0 ? $t("set.sysmon.notConnected") : `${info.rssi} dBm (${Math.round(rssiToPct(info.rssi))} %)`}</dd>
      <dt>{$t("set.sysmon.apClients")}</dt>
      <dd>
        {#if !info.apClients || info.apClients.length === 0}
          {$t("set.sysmon.none")}
        {:else}
          {#each info.apClients as c}
            <span class="sm-client" class:sm-you={c.you}>{c.ip || "?"} – {c.rssi} dBm{c.you ? " " + $t("set.sysmon.you") : ""}</span>
          {/each}
        {/if}
      </dd>
    </dl>

    <h4 class="sm-group">{$t("set.sysmon.grpMemory")}</h4>
    <dl class="sm-list">
      <dt>{$t("set.sysmon.ramFree")}</dt><dd>{kb(info.heapFree)} / {kb(info.heapTotal)} KB</dd>
      <dt>{$t("set.sysmon.ramMin")}</dt><dd>{kb(info.heapMinFree)} KB</dd>
      <dt>{$t("set.sysmon.ramBlock")}</dt><dd>{kb(info.heapMaxAlloc)} KB</dd>
      <dt>{$t("set.sysmon.fs")}</dt><dd>{kb(info.fsUsed)} / {kb(info.fsTotal)} KB ({info.fsTotal > 0 ? Math.round((100 * info.fsUsed) / info.fsTotal) : 0} %)</dd>
      <dt>{$t("set.sysmon.firmware")}</dt><dd>{kb(info.sketchSize)} / {kb(info.sketchSize + info.sketchFree)} KB</dd>
      <dt>{$t("set.sysmon.flash")}</dt><dd>{mb(info.flashSize)} MB</dd>
    </dl>

    <h4 class="sm-group">{$t("set.sysmon.grpSystem")}</h4>
    <dl class="sm-list">
      <dt>{$t("set.sysmon.cpu")}</dt><dd>{info.cpuLoad < 0 ? $t("set.sysmon.notYet") : Math.round(info.cpuLoad) + " %"}</dd>
      <dt>{$t("set.sysmon.temp")}</dt><dd>{Number(info.tempC).toFixed(1)} °C</dd>
      <dt>{$t("set.sysmon.uptime")}</dt><dd>{fmtUptime(info.uptimeMs)}</dd>
      <dt>{$t("set.sysmon.reset")}</dt><dd>{reasonText(info.resetReason, $t)}</dd>
    </dl>
  {/if}
</div>

<style>
  /* Farben: in hellem und dunklem Pico-Theme gut lesbar */
  .sysmon {
    --sm-cpu: #d97706;
    --sm-ram: #2563eb;
    --sm-wifi: #16a34a;
    --sm-client: #9333ea;
  }
  /* Pico folgt ohne data-theme dem System-Theme → hellere Farben nur im dunklen Modus */
  @media (prefers-color-scheme: dark) {
    :global(:root:not([data-theme="light"])) .sysmon {
      --sm-cpu: #fbbf24;
      --sm-ram: #60a5fa;
      --sm-wifi: #4ade80;
      --sm-client: #c084fc;
    }
  }
  :global([data-theme="dark"]) .sysmon {
    --sm-cpu: #fbbf24;
    --sm-ram: #60a5fa;
    --sm-wifi: #4ade80;
    --sm-client: #c084fc;
  }
  .sm-legend { display: flex; flex-wrap: wrap; gap: 0.25rem 1rem; font-size: 0.85rem; margin-bottom: 0.25rem; }
  .sm-leg i { display: inline-block; width: 0.8rem; height: 0.25rem; border-radius: 2px; margin-right: 0.3rem; vertical-align: middle; }
  svg { display: block; max-width: 100%; height: auto; }
  .sm-grid { stroke: currentColor; stroke-opacity: 0.18; stroke-width: 0.6; }
  .sm-axis { fill: currentColor; fill-opacity: 0.65; font-size: 10px; }
  .sm-offline { color: var(--pico-del-color, #c62828); display: block; }
  .sm-list { display: grid; grid-template-columns: 1fr auto; gap: 0.2rem 1rem; margin: 0; font-size: 0.9rem; }
  /* Gruppenüberschrift (Uhr, WLAN, Speicher, System) mit Trennlinie */
  .sm-group {
    font-size: 0.8rem; font-weight: 600; text-transform: uppercase; letter-spacing: 0.04em;
    color: var(--pico-muted-color); margin: 0.9rem 0 0.35rem; padding-bottom: 0.2rem;
    border-bottom: 1px solid var(--pico-muted-border-color, rgba(127, 127, 127, 0.25));
  }
  .sm-ok { color: #16a34a; }
  .sm-bad { color: var(--pico-del-color, #c62828); }
  .sm-list dt { color: var(--pico-muted-color); margin: 0; }
  .sm-list dd { margin: 0; text-align: right; }
  /* Ein Hotspot-Gerät pro Zeile; das eigene Gerät fett */
  .sm-client { display: block; overflow-wrap: anywhere; }
  .sm-you { font-weight: 700; }
</style>
