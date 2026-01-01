# Copilot/AI Agent Instructions for lisa's_lamp (ESP32)

Purpose: Quickly orient AI coding agents to the project layout, demarcate responsibilities for components, provide examples of common modifications (e.g., captive portal / WiFi behavior), and surface gotchas discovered while reading the repo.

## Big picture
- This is a small ESP32 Arduino (PlatformIO) project implementing an LED lamp with:
  - UI via a captive portal Access Point (AP) to configure colors and alarms.
  - Local storage via LittleFS (web UI in `data/index.html`).
  - Rotary encoder and button input in `src/main.cpp` and `include/state.h`.
  - `wifi_controller.*` manages AP + DNSServer + WebServer, exposes endpoints and state callbacks.

Key files:
- `src/wifi_controller.cpp` - AP config / DNS / HTTP handlers. Important endpoints: `/get_config` (GET), `/set_config` (POST), `root` serving `index.html`.
- `include/wifi_controller.h` - public API: `wifi_register_state`, `wifi_set_state_update_callback`, `wifi_start_ap`, `wifi_stop_ap`, `wifi_handle_client`.
- `src/json_utils.cpp` + `include/json_utils.h` - JSON serialization of `FullConfig` and parsing; `FullConfig` is defined in `types.h`.
- `include/types.h` - `FullConfig`, `Alarm`, `RGB`, and `StateChangeType`.
- `src/main.cpp` - registers state with `wifi_register_state`, sets callback `wifi_set_state_update_callback(on_state_updated_from_wifi)`, and drives LED logic and inputs.

## Component boundaries and data flow
- The WiFi stack (AP + DNS + WebServer) is entirely inside `wifi_controller.*` and must be treated as the single integration point to the web UI.
- `main.cpp` holds the core hardware logic (rotary encoder, LED updates) and registers a pointer to `Alarm[]`, `RGB`, and `bool` override with `wifi_register_state` so wifi handlers can read/write values.
- When the web UI POSTs to `/set_config`, wifi_controller parses JSON (using `json_utils.cpp`) and notifies the main app using the `GenericStateUpdateCallback` type with `STATE_CHANGE_CONFIG` and a `FullConfig` pointer.

## Project-specific conventions / patterns
- Use `wifi_register_state()` to pass pointers/references to global state (alarms, color overrides). Do not directly reference these globals inside `wifi_controller`.
- Use `wifi_set_state_update_callback()` to register a function that accepts `StateChangeType` and `void* data` for state updates originating from web requests.
- The JSON shape used by the portal is documented by `createConfigJson` and `parseConfigJson` in `json_utils.cpp`. Example JSON payload for `/set_config`:
  {
    "override_color":{"r":255,"g":127,"b":0},
    "colorOverrideActive":true,
    "alarms":[{"day":1,"hour":7,"minute":0,"active":true}, ... up to MAX_ALARMS]
  }
- Web UI is served from LittleFS `data/index.html`. Add files to `data/` and PlatformIO will package them into LittleFS.
- Logging uses `serialPrint()` wrapper (`include/debug_utils.h`) — use it when adding instrumentation.

## Build & dev workflows
- Standard builds / upload / serial monitor (PlatformIO):
  - Build: `pio run -e esp32` (or `pio run`)
  - Upload: `pio run -e esp32 -t upload`
  - Monitor Serial: `pio device monitor -e esp32 -b 115200` or `pio device monitor -p <COM> -b 115200`
  - Clean: `pio run -t clean`
- Debugging hardware: `platformio.ini` has `debug_server` configured (openocd + FT232H). Use:
  - `pio debug -e esp32` or follow PlatformIO docs for custom FT232H debug.
- To test the captive portal during local dev:
  1. Build + upload firmware with AP mode enabled by long-pressing the button (in main loop it calls `wifi_start_ap()`).
  2. Connect your phone/laptop to `Lisas_Lamp_Config` (default password in code is `password` placeholder).
  3. Open a browser; if the captive portal doesn't auto-open, browse to `http://192.168.4.1` (AP IP) or `http://clients3.google.com/generate_204` (Android test) to trigger portal.

## Known bugs & gotchas (useful for AI code edits)
- toStringIp() bit-order issue:
  - Both the article and `wifi_controller.cpp` implement `toStringIp(IPAddress ip)` by shifting bytes as `ip >> (8 * i)` and building the string from least significant byte to most significant byte. This will produce a reversed IP like `1.4.168.192` when the AP IP is `192.168.4.1`.
  - Use `WiFi.softAPIP().toString()` or correct loops (iterate 3 to 0) instead. This reversed IP causes Location header to be incorrect and prevents client browsers/OS from connecting to the portal.
- DNS reply type: The article suggests `dnsServer.setErrorReplyCode(DNSReplyCode::NoError);` while the project sets TTL only. Server reply codes (NoError vs NXDOMAIN) can influence captive portal detection on clients; ensure `NoError` reply is used for failed DNS queries.
- The `wifi_is_client_connected()` function is declared in `include/wifi_controller.h` but no implementation exists; `main.cpp` expects to use it (commented out), so watch for missing or planned behavior.
- Some OS clients (iOS/Android) use HTTPS/host-specific endpoints for connectivity checks. If the system's check is HTTPS or HSTS-protected, redirects may be ignored. Testing should include OS-specific checks (Apple: `http://captive.apple.com/hotspot-detect.html`, Android: `http://connectivitycheck.gstatic.com/generate_204`).
- LittleFS mount fails are formatted via `LittleFS.begin(true)` — which might format and wipe data when `begin` fails, be cautious when iterating.

## Captive portal / WiFi troubleshooting checklist (quick steps for AI agents)
- Confirm `dnsServer.start(53, "*", apIP)` exists and `dnsServer.processNextRequest();` is called in the main loop.
- Check for `dnsServer.setErrorReplyCode(DNSReplyCode::NoError);` — add it if missing.
- Replace `toStringIp(...)` calls with `WiFi.softAPIP().toString()` for accuracy.
- Ensure `server.onNotFound(handleNotFound)` is registered and that `captivePortal()` is called before responding 404s (this repo does this correctly).
- Check web server response codes: use `server.send(302, ...)` and `Location` header, and keep `server.client().stop()` to close the connection.
- If the client doesn’t show captive portal, ask for: phone OS/version, whether the SSID requires a password, and any browser logs or serial output containing `Captive portal redirect for host:`. Also try visiting the AP IP URL manually.

## Example code snippets to improve captive portal reliability
- Correct IP creation and DNS reply code:
  ```cpp
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  ```
- When adding a new REST endpoint, mirror the pattern in `wifi_controller.cpp`:
  ```cpp
  server.on("/new_endpoint", HTTP_POST, handleNewEndpoint);
  server.onNotFound(handleNotFound);
  ```

## Small conventions/helpers for AI edits
- Return early on null pointers for s_alarms/s_overrideRGB to avoid crashes (project uses this check in handlers).
- Keep JSON `StaticJsonDocument<1024>` size usage consistent when modifying `createConfigJson` and `parseConfigJson`.
- Use existing `serialPrint()` for debug logs to follow project style.

---
If you want, I can now: 
- Create a PR patch that fixes `toStringIp()` and adds `dnsServer.setErrorReplyCode(DNSReplyCode::NoError);` in `wifi_start_ap()`, and/or implement `wifi_is_client_connected()`; or
- Expand the doc with a `CONTRIBUTING.md` and a checklist for WiFi testing.  
Which change would you like me to start with?  

<!-- End of copilot instructions -->
