// Verlaufs-Store des Systemmonitors.
// Hält die letzten Messpunkte von /get_system_info im Speicher des Browsers, damit der Verlauf
// beim Zu-/Aufklappen der Rubrik erhalten bleibt (bei Seiten-Reload ist er bewusst weg).
import { writable } from "svelte/store";

// Maximale Anzahl Punkte: 360 Punkte bei 5 s Abstand = 30 Minuten
export const MAX_POINTS = 360;

// Liste der Punkte { t, cpu, heapUsedPct, heapFree, rssi, wifiPct, clientPct, tempC }
// (wifiPct = Signalqualität zum Router in %, -1 = nicht verbunden)
export const systemInfoHistory = writable([]);

// Hängt einen Messpunkt an und verwirft die ältesten, wenn mehr als MAX_POINTS vorhanden sind.
export function addSystemInfoPoint(point) {
  systemInfoHistory.update((list) => {
    const next = [...list, point];
    // Älteste Punkte vorne abschneiden
    return next.length > MAX_POINTS ? next.slice(next.length - MAX_POINTS) : next;
  });
}
