# Änderungen

## November 2026 — Nach der Bereinigung

- **Einstellungen → WLAN:** Checkboxen „Passwort entfernen“ entfallen — leeres Passwortfeld = kein
  Passwort. Neue Icon-Knöpfe: **Lupe** sucht WLANs in der Nähe (Auswahlliste), **Stecker** testet die
  Verbindung mit den eingegebenen Daten (erfolgreich / Passwort falsch / Netz nicht gefunden).
- **Einstellungen → Persönlich:** Name der Lampe (Titel der Seite, Standard „Liza“) und bis zu 8
  **Party-Tage** frei wählbar; Standardwerte aus `include/secrets.h` (`SECRET_LAMP_NAME`,
  `SECRET_CELEBRATIONS`). Die Ballons der Weboberfläche folgen derselben Liste.
- **Einstellungen → Debug:** Status-LED, Ereignisprotokoll und serielle Logs einzeln abschaltbar;
  Log-Ausgabe wahlweise über UART (RX/TX-Pins) oder USB. Das Update setzt die Systemeinstellungen
  (inkl. WLAN-Daten) einmalig zurück.
- **Wecker:** bis zu 16, zusätzlich **einmalige Termine** mit Datum (Auswahl „Datum“ in der Zeile); Weckzeit über
  die Sommer-/Winterzeit-Umstellung hinweg korrigiert. Das Update setzt die Lampen-Konfiguration
  einmalig zurück.
- **„Licht-Sets“ heißen jetzt „Szenen“** (englisch „Scenes“).
- **Sprache Deutsch/Englisch** in den Einstellungen; Protokoll-Meldungen der Firmware einheitlich englisch,
  bei Deutsch in der Weboberfläche übersetzt.
- **Hardware-Erkennung:** Quarz und RDS-Modul werden bei jedem Einschalten geprüft; nicht vorhandene
  lassen sich nicht aktivieren.
- **Termine:** Vorschlag jetzt + 1 h, abgelaufene blass und nicht einschaltbar (aber bearbeitbar); aktives Set folgt dem Drehknopf in der UI;
  Zeitquellen mit Datum/Uhrzeit statt „vor X“; „Liza“ statt „Lisa“.
- **Weboberfläche ohne Verbindung:** alle Eingaben gesperrt, bis die Lampe wieder erreichbar ist.
- **Weboberfläche:** Licht-Sets, Wecker und Dauer je einzeilig mit gleich großen Feldern; Lösch-Knöpfe
  quadratisch wie der Einstellungs-Knopf mit kräftigem ✕.
- **Licht-Sets:** bis zu 6 Kombinationen aus Lichtfarbe und Animation, in der Weboberfläche
  verwaltbar; Standard: Neutralweiß statisch, Warmweiß atmend (aktiv), Kerzenlicht als Flamme.
  Das Update setzt die Lampen-Konfiguration (inkl. Wecker) einmalig zurück.
- **Drehknopf:** gedrückt halten + drehen wechselt das Set; Hotspot jetzt nach 2 s Halten (ohne Drehen).
- **Animation „Flamme“:** jedes Pixel flackert unabhängig, Töne aus der gewählten Lichtfarbe abgeleitet
  (dunkler = röter).
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
