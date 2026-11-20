# Lizas Lampe (ESP32-C3)

Smarte Nachttischlampe mit Wecker, Gute-Nacht-Modus und Weboberfläche über einen eigenen WLAN-Hotspot.

- **Drehknopf:** Helligkeit in 8 Stufen, gedrückt halten + drehen = Licht-Set wechseln, kurzer Druck =
  Gute Nacht / Wecker aus, 3 s halten = Hotspot
- **Licht-Sets:** bis zu 6 Kombinationen aus Lichtfarbe und Animation (z. B. Kerze mit Flammen-Effekt)
- **Wecker:** bis zu 16 Wecker — wöchentlich (Wochentag) oder als einmaliger Termin (Datum); Licht wird langsam heller
- **Uhrzeit:** NTP über das Heim-WLAN, ohne Internet per WLAN-Beacons (TSF) oder UKW-RDS
- **Stromsparen:** Deep Sleep, wenn die Lampe aus ist; wacht rechtzeitig vor dem Wecker auf

## Dokumentation

| Thema | Datei |
|---|---|
| Bedienung (Drehknopf, Tasten, Status-LED) | [doc/bedienung.md](doc/bedienung.md) |
| Lampenzustände und Übergänge | [doc/zustaende.md](doc/zustaende.md) |
| Hardware und GPIO-Belegung | [doc/hardware.md](doc/hardware.md) |
| WLAN, NTP, TSF- und RDS-Uhrzeit | [doc/wlan-und-zeit.md](doc/wlan-und-zeit.md) |
| Architektur, Threads, HTTP-API | [doc/architektur.md](doc/architektur.md) |
| Bauen, Aufspielen, UI-Entwicklung | [doc/build-und-deploy.md](doc/build-und-deploy.md) |
| Änderungen | [CHANGELOG.md](CHANGELOG.md) |

## Hotspot und Zugangsdaten

| | Standardwert |
|---|---|
| **WLAN-Name (SSID)** | `Lizas-Lamp` |
| **WLAN-Passwort** | **`Erdmaennchen`** |
| Weboberfläche | http://4.3.2.1 (öffnet sich als Captive Portal meist automatisch) |

- Der Hotspot startet **nur per langem Druck auf den Drehknopf** (3 s halten, ohne zu drehen) und geht wieder aus, sobald 1 Minute kein Gerät verbunden ist **oder** 10 Minuten lang niemand die Weboberfläche bedient hat (auch wenn ein Handy noch verbunden ist). Beim Einschalten verbindet sich die Lampe nur mit dem Heim-WLAN (falls eingetragen), ohne Hotspot.
- **Ersteinrichtung:** Lampe einschalten, Drehknopf 3 s gedrückt halten, mit `Lizas-Lamp` verbinden, unter **Einstellungen → WLAN** das Heim-WLAN eintragen.
- Das Passwort ist bewusst ohne Umlaut geschrieben („ae“ statt „ä“): WPA2 erlaubt nur ASCII-Zeichen, Umlaute funktionieren auf vielen Handys nicht.
- Name und Passwort lassen sich in der Weboberfläche unter **Einstellungen → WLAN** ändern (Passwort 8–63 Zeichen oder per „Passwort entfernen“ offen).
- Der Standardwert gilt nach der Erstinstallation und nach einem **Werksreset**. Geräte, die schon vorher eingerichtet waren, behalten ihre gespeicherten Einstellungen (bisher: offener Hotspot ohne Passwort).
- Passwort vergessen → Werksreset: In der Weboberfläche unter **Einstellungen → System** oder per **Boot-Taste** (siehe [doc/bedienung.md](doc/bedienung.md)).

### Eigene Standard-Zugangsdaten (`include/secrets.h`)

Die oben genannten Standardwerte stammen aus [include/secrets.example.h](include/secrets.example.h).
Für eigene Werte, z. B. das Heim-WLAN, damit die Lampe es nach einem Werksreset sofort wieder kennt:

```bash
cp include/secrets.example.h include/secrets.h
```

Dann `include/secrets.h` bearbeiten. Die Datei steht in `.gitignore` und wird nie eingecheckt. Fehlt sie,
wird automatisch die Vorlage verwendet. Ungültige Werte (Passwort nicht leer und kürzer als 8 Zeichen,
SSID länger als 32 Zeichen) brechen den Build mit einer Fehlermeldung ab.

> Wer `SECRET_AP_PASSWORD` ändert, sollte die Tabelle oben für sich nicht mehr als gültig ansehen. Sie
> beschreibt die Werte der Vorlage.

## Bauen und Aufspielen

Einmalig die rechnerspezifischen Ports eintragen (gitignored):

```bash
cp platformio_override.example.ini platformio_override.ini   # dann upload_port/monitor_port anpassen
```

Ohne diese Datei sucht PlatformIO den Port selbst.

```bash
./build.sh all      # UI bauen + Firmware kompilieren
./deploy.sh         # UI + Firmware bauen und beides (LittleFS + Firmware) auf das Gerät laden
```

Weitere Details, Mock-Modus und UI-Entwicklung: [doc/build-und-deploy.md](doc/build-und-deploy.md).

## TODO

- **Einstellungen bei Firmware-Updates erhalten:** Ändert sich durch ein Update die Größe von
  `FullConfig` oder `SystemSettings` (`include/types.h`), werden beim nächsten Start **alle Einstellungen
  inkl. WLAN-Zugangsdaten und Hotspot-Passwort auf Standard zurückgesetzt** (`ensureStructExists` in
  `src/store.cpp`). Lösung: Versionsfeld in den gespeicherten Strukturen + Migration alter Versionen.
  Bis dahin: nach Updates, die diese Strukturen ändern, die Lampe neu einrichten.

## Entwicklung

- Serieller Monitor: `pio device monitor` (Log über UART0/TX = GPIO21, 115200 Baud).
  Bei Problemen mit dem Wiederverbinden hilft die VS-Code-Erweiterung
  [Serial Monitor](https://marketplace.visualstudio.com/items?itemName=ms-vscode.vscode-serial-monitor).
- Kommentare und Doku sind auf Deutsch. Pins nur in `include/pins.h`, Zugangsdaten nur in
  `include/secrets.h`. Web-Handler führen nichts selbst aus, sondern legen Aufträge für `loop()` ab
  (siehe [doc/architektur.md](doc/architektur.md)).
