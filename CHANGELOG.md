# Änderungen

## November 2026 — Nach der Bereinigung

- **Lichtfarben:** Warm- und Neutralweiß klar unterscheidbar (weiße LED für warme/kalte Töne
  zurückgenommen), neuer Ton **Kerzenlicht**; Auswahl in der UI von warm nach kalt, auf Deutsch.
- **Helligkeit:** Stufen an das Auge angepasst (geometrisch statt linear); **Stufe 7 = Boost** mit
  zusätzlichen Farb-Chips für deutlich mehr Licht.
- **Hotspot nur per langem Tastendruck:** Beim Start verbindet sich die Lampe nur mit dem Heimnetz.
- **Status-LED:** langsam = WLAN aus, 1× Heimnetz, 2× Hotspot, 3× beides.
- **Hotspot schaltet nach 10 min ohne Bedienung ab**, auch wenn ein Gerät verbunden bleibt.
- **Hotspot-Animation:** blauer Komet, solange der Hotspot auf ein Gerät wartet; Vorrang vor dem
  Wecker; Drücken/Drehen bricht den Hotspot ab.
- **LED-Anzahl** in der Weboberfläche einstellbar (1–16, Standard 4), gespeichert in `SystemSettings`.
  Dieses Update setzt die Systemeinstellungen einmalig zurück (Strukturgröße geändert).
- **Ereignisprotokoll** lieferte ungültiges JSON (ArduinoJson 7 leert den Ziel-String) — behoben.
- **`main.cpp` aufgeräumt** (373 → 121 Zeilen, Auftragsablage in `web_commands.cpp`); Zeitzone und
  gespeicherte Uhrzeit werden jetzt vor den Weckern gesetzt.

## November 2026 — Bereinigung (Branch `cleanup2026`)

Kompletter Durchgang durch Firmware, Weboberfläche und Doku; am Gerät getestet.

### Fehler behoben
- NTP-Status flackerte (`sntp_get_sync_status()` setzt sich nach dem Lesen zurück) → Status im Sync-Callback.
- Web-Handler liefen mit Seiteneffekten im `async_tcp`-Task → Auftragsablage, Ausführung in `loop()`.
- Ungültige Hotspot-SSID/-Passwörter konnten das Gerät aussperren → Prüfung in Firmware und UI.
- UI überschrieb die Helligkeit; Config-Änderungen stoppten laufende Wecker.
- Drehen beendete Gute Nacht nicht; Drehen im Sleep ließ LEDs im Deep Sleep leuchten.
- Systemwarnungen schalteten eine ausgeschaltete Lampe ein; Hinweise hoben Sleep auf.
- Wecker konnten bei verspätetem Aufwachen verpasst werden (jetzt 10-min-Fenster, realistischer Drift).
- Taster-Wakeup aus dem Deep Sleep (Pullup statt Pulldown) und verlorene Config vor dem Schlaf.
- RDS-Uhrzeit war um den Zeitzonen-Offset falsch; jetzt mit Blockfehler- und Plausibilitätsprüfung.
- Drehknopf verschluckte Rastungen (Bibliotheks-Beschleunigung, Rundung) → rastungsgenaue Auswertung.
- Licht beim Aufwecken kam erst nach ca. 3,4 s → nach ca. 60 ms.

### Sicherheit
- POSTs nur als JSON, Origin-Prüfung, Body-Limit 4 KB; Passwörter nicht mehr im Log.
- OTA-Upload entfernt; Hotspot-Passwort standardmäßig gesetzt (`include/secrets.h`).

### Struktur
- Pins zentral in `include/pins.h`, Zugangsdaten in `include/secrets.h`, Ports in `platformio_override.ini`.
- Schalter-Endpunkte zu `POST /set_option` zusammengelegt.
- Tote Dateien und Funktionen entfernt, Bibliotheksversionen gepinnt, Log ruhiger.
- Doku neu gegliedert (siehe README).

### Offen
- Einstellungen bei Firmware-Updates mit geänderter Strukturgröße erhalten (siehe TODO in der README).
