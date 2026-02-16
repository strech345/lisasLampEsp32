Befehl,Beschreibung
WiFi.mode(WIFI_STA),Setzt nur Client-Modus.
WiFi.mode(WIFI_AP),Setzt nur Access-Point-Modus.
WiFi.mode(WIFI_AP_STA),Aktiviert beide Modi gleichzeitig (Koexistenz).
WiFi.setSleep(false),Deaktiviert den Stromsparmodus (erhöht Stabilität).
WiFi.scanNetworks(),Scannt nach verfügbaren WLANs in der Umgebung.


2. STA-Modus (Station / Client)
Befehle, um den ESP32 mit einem bestehenden Router zu verbinden.

WiFi.begin(ssid, pass): Startet den Verbindungsaufbau zum Router.

WiFi.status(): Gibt den aktuellen Status zurück (z. B. WL_CONNECTED, WL_IDLE_STATUS, WL_NO_SSID_AVAIL).

WiFi.localIP(): Gibt die vom Router zugewiesene IP-Adresse zurück.

WiFi.RSSI(): Gibt die Signalstärke in dBm zurück.

WiFi.setAutoReconnect(true/false): Bestimmt, ob der ESP32 bei Verbindungsverlust automatisch neu verbinden soll.

WiFi.disconnect(false): Trennt die aktuelle Verbindung, lässt das Radio aber für den AP-Modus an.


3. AP-Modus (Access Point)
Befehle, um den ESP32 selbst als WLAN-Hotspot bereitzustellen.

WiFi.softAP(ssid, pass): Startet den Hotspot. Ohne pass ist das WLAN offen.

WiFi.softAPConfig(local_ip, gateway, subnet): Setzt eine feste IP für den ESP32 im AP-Modus (Standard ist meist 192.168.4.1).

WiFi.softAPIP(): Gibt die IP-Adresse des ESP32-Hotspots zurück.

WiFi.softAPgetStationNum(): Gibt die Anzahl der aktuell mit dem ESP32 verbundenen Geräte zurück.

WiFi.softAPdisconnect(true): Schaltet den Hotspot komplett aus.

4. Wichtige Event-Handler (Fortgeschritten)
Anstatt die Loop mit Abfragen zu blockieren, kannst du auf Ereignisse reagieren:

WiFi.onEvent(callback): Erlaubt es, Code auszuführen, wenn bestimmte Dinge passieren (z.B. SYSTEM_EVENT_STA_GOT_IP oder SYSTEM_EVENT_AP_STACONNECTED).



Die goldene Reihenfolge (AP+STA)
WiFi.mode(WIFI_AP_STA); Zuerst musst du dem Chip sagen, dass er beide "Rollen" gleichzeitig übernehmen soll. Wenn du erst softAP startest und danach mode(WIFI_STA), wird der AP-Modus oft wieder abgeschaltet.

WiFi.softAPConfig(...); (Optional) Falls du eine feste IP für deinen Hotspot willst (z. B. 192.168.1.1), muss dies vor dem Start des APs geschehen.

WiFi.softAP("Name", "Passwort"); Starte den Hotspot. Ab diesem Moment ist dein ESP32 für andere Geräte sichtbar.

WiFi.begin("SSID", "Passwort"); Als letztes startest du die Verbindung zum Router.

Warum diese Reihenfolge?
Das Hauptproblem ist die Kanal-Synchronisation.

Der ESP32 hat nur ein Radio. Der AP-Modus startet standardmäßig auf Kanal 1 (oder dem zuletzt gespeicherten).

Sobald du WiFi.begin() aufrufst, scannt der ESP32 alle Kanäle, um den Router zu finden.

Wenn der Router auf Kanal 6 funkt, muss der ESP32 seinen eigenen AP-Modus von Kanal 1 auf Kanal 6 umstellen, damit beide gleichzeitig funktionieren können.

Wenn du WiFi.begin() vor softAP() aufrufst, kann es zu Timing-Problemen kommen, bei denen der AP gar nicht erst richtig startet, weil das Radio noch mit dem Scan-Vorgang des STA-Modus beschäftigt ist.