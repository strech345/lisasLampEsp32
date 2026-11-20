# Bauen, Aufspielen und UI-Entwicklung

_Stand: November 2026_

Das Projekt besteht aus der **Firmware** (PlatformIO, Arduino-ESP32, `src/`, `include/`) und der
**Weboberfläche** (Svelte 5 + Vite, `svelte-ui/`). Die UI wird gebaut, gzip-komprimiert nach `data/`
gelegt und als LittleFS-Dateisystem auf die Lampe geladen.

---

## Einmalige Einrichtung

```bash
cp platformio_override.example.ini platformio_override.ini   # upload_port / monitor_port eintragen
cp include/secrets.example.h include/secrets.h               # optional: eigene Standard-Zugangsdaten
```

Beide Dateien sind gitignored. Ohne `platformio_override.ini` sucht PlatformIO den Port selbst, ohne
`secrets.h` gilt die Vorlage. Ports am besten über `/dev/serial/by-id/…` angeben.

Voraussetzungen: PlatformIO (Standardpfad `~/.platformio/penv/bin/pio` oder `pio` im `PATH`),
Node.js ≥ 20.11 mit npm.

---

## Skripte

| Befehl | Wirkung |
|---|---|
| `./deploy.sh` | UI bauen, Firmware bauen, **Dateisystem und Firmware** aufspielen (Normalfall) |
| `./build.sh all` | UI und Firmware bauen (ohne Aufspielen) |
| `./build.sh ui` | nur UI bauen → `data/app.js.gzip`, `data/index.html.gzip` |
| `./build.sh firmware` | nur Firmware bauen |
| `./build.sh upload` | nur Firmware aufspielen (UI/Dateisystem unverändert) |
| `./build.sh dev` | UI bauen und Vite-Dev-Server starten |
| `./build.sh clean` | Build-Artefakte löschen (`dist`, `node_modules`, erzeugte `.gzip`, PlatformIO) |

Ändert sich die API zwischen UI und Firmware, immer `./deploy.sh` verwenden (beides zusammen).

### PlatformIO direkt

```bash
pio run                              # Firmware bauen
pio run -t upload                    # Firmware aufspielen
pio run -t uploadfs                  # Dateisystem (data/) aufspielen
pio run -t upload -t monitor         # aufspielen und seriellen Monitor öffnen
pio device monitor                   # serieller Monitor (115200 Baud, über monitor_port)
```

Die Lampe muss für das Aufspielen **wach** sein: Im Deep Sleep ist ihr USB-Anschluss abgeschaltet.
Kurz auf den Drehknopf drücken, dann erscheint der Port.

Das serielle Log läuft über UART0 (TX = GPIO21, z. B. FTDI-Adapter), nicht über USB.

---

## Dateien in `data/`

| Datei | Herkunft | Im Git |
|---|---|---|
| `index.html.gzip` | `svelte-ui/index.prod.html` | nein (wird gebaut) |
| `app.js.gzip` | Vite-Build `svelte-ui/dist/app.js` | nein (wird gebaut) |
| `pico.min.css.gzip` | Pico CSS | ja |
| `meerkat.avif` | Bild | ja |

`svelte-ui/build-esp32.js` komprimiert die Dateien (gzip Stufe 9) und entfernt veraltete. Die Firmware
liefert sie mit `Content-Encoding: gzip` aus. `index.prod.html` ist die Captive-Portal-taugliche
Vorlage (kein `type="module"`, kein CDN) und enthält ein `window.onerror`-Banner, das JS-Fehler direkt
auf der Seite anzeigt — bewusst drin lassen.

---

## UI-Entwicklung

```bash
cd svelte-ui
npm install
npm run dev            # http://localhost:5173
```

- **Mit Lampe:** Rechner mit dem Hotspot `Lizas-Lamp` verbinden. Der Dev-Server leitet API-Aufrufe
  (`/get_*`, `/set_*`, `/trigger_*`, `/ping`, `/restart`, `/factory_reset`) an `http://4.3.2.1` weiter.
- **Ohne Lampe (Mock-Modus):** `http://localhost:5173/?mock=true`. Alle API-Aufrufe liefern
  Testdaten aus `svelte-ui/src/lib/mockData.js`; Speichern „gelingt“ immer. Gut für Layout und
  Formularprüfung, nicht für echtes Verhalten.

Alle Requests laufen über `svelte-ui/src/lib/api.js` (`getJson`, `postJson`): POSTs senden immer
`Content-Type: application/json` (die Firmware lehnt anderes mit 415 ab), Fehlermeldungen der Firmware
(`{"status":"error","message":"…"}`) werden als Text angezeigt.

### Aufbau

```
svelte-ui/src/
├── App.svelte              Seitenwechsel Hauptseite ↔ Einstellungen
├── Main.svelte             Farbe, Dauern, Wecker
├── Settings.svelte         WLAN, Energie & Uhr, Warnungen, System, Ereignisprotokoll
├── components/             AlarmTable, ColorPicker, EventLog, MessageAlert, Snowfall, Balloons
├── stores/                 configStore (Lampe), systemStore (WLAN), messageStore (Meldungen)
└── lib/                    api.js (Fetch-Helfer), mockData.js (Mock-Modus), i18n.js + i18n/de.js, en.js
                            (Sprachen), logTranslations.js (Protokoll-Meldungen → Deutsch)
```
