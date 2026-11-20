# Hinweise für KI-Assistenten (Lisas Lampe, ESP32-C3)

Kurzorientierung. Ausführlich: [README](../README.md) und `doc/`.

## Projekt
- PlatformIO/Arduino-Firmware (`src/`, `include/`) für eine Nachttischlampe mit WS2812B, KY-040-Drehknopf,
  Wecker, Deep Sleep und Captive-Portal-Weboberfläche.
- Weboberfläche: Svelte 5 + Vite in `svelte-ui/`, gebaut und gzip-komprimiert nach `data/` (LittleFS).
- Architektur, Threads und HTTP-API: `doc/architektur.md`.

## Regeln
- **Sprache:** Kommentare, Doku und UI-Texte auf Deutsch. Jede Datei hat einen Header-Kommentar,
  jede Funktion einen kurzen Kommentar.
- **Pins** nur in `include/pins.h`, **Zugangsdaten** nur in `include/secrets.h` (gitignored,
  Vorlage `secrets.example.h`), rechnerspezifische Ports in `platformio_override.ini` (gitignored).
- **Thread-Sicherheit:** HTTP-Handler (async_tcp-Task) prüfen nur, legen über
  `queueWebCommand()` (`web_commands.cpp`) einen Auftrag ab und antworten. LEDs, WLAN, NVS, `delay()` und
  `ESP.restart()` nur aus `loop()`. WiFi-Events und SNTP-Callback posten nur in die Queue.
- **API:** POSTs nur `application/json`; neue Schalter über `/set_option` und `OPTIONS[]` in
  `route_handlers.cpp`; UI-Requests über `svelte-ui/src/lib/api.js`.
- **Persistenz:** `FullConfig`/`SystemSettings` werden binär gespeichert — Größenänderung setzt beim
  Update alle Einstellungen zurück (offenes TODO, siehe README).
- Logging über `serialPrint()`; nichts Periodisches im Sekundentakt loggen.
- `index.prod.html` enthält ein `window.onerror`-Banner — drin lassen.

## Bauen
- `./deploy.sh` (UI + Firmware + Dateisystem), `./build.sh firmware`, `pio run`.
- UI ohne Lampe: `cd svelte-ui && npm run dev`, dann `http://localhost:5173/?mock=true`.
