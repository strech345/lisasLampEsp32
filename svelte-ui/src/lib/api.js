// Kleiner Fetch-Helfer für die Lampen-API: prüft res.ok, liefert verständliche
// Fehlertexte aus der Antwort und nutzt im Mock-Modus (?mock=true) mockFetch.
import { isMockEnabled, mockFetch } from "./mockData.js";

// Fehlerklasse mit HTTP-Status (0 = Netzwerkfehler)
export class ApiError extends Error {
  constructor(message, status = 0) {
    super(message);
    this.name = "ApiError";
    this.status = status;
  }
}

// Führt den Request aus und wirft bei Netzwerk- oder HTTP-Fehlern einen ApiError
async function request(url, options) {
  const fetchFn = isMockEnabled() ? mockFetch : fetch;
  let res;
  try {
    res = await fetchFn(url, options);
  } catch (e) {
    throw new ApiError(`Keine Verbindung zur Lampe (${e.message})`, 0);
  }
  if (!res.ok) {
    let text = "";
    try {
      text = (await res.text()).trim();
      // Firmware antwortet bei Fehlern mit {"status":"error","message":"..."} → nur die Meldung zeigen
      const parsed = JSON.parse(text);
      if (parsed && typeof parsed.message === "string") text = parsed.message;
    } catch {}
    throw new ApiError(text || res.statusText || `HTTP ${res.status}`, res.status);
  }
  return res;
}

// GET und Antwort als JSON parsen
export async function getJson(url) {
  const res = await request(url);
  return res.json();
}

// POST mit JSON-Body (Standard "{}"); gibt die Antwort als Text zurück
export async function postJson(url, body = {}) {
  const res = await request(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  try {
    return await res.text();
  } catch {
    return "";
  }
}

// Prüft ob die Lampe erreichbar ist (GET /ping, max. 5 s); true/false, wirft nie
export async function ping() {
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), 5000);
  try {
    await request("/ping", { signal: controller.signal });
    return true;
  } catch {
    return false;
  } finally {
    clearTimeout(timeoutId);
  }
}
