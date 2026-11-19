# Lisas Lampe (ESP32-C3)

Smarte Nachttischlampe mit Wecker, Gute-Nacht-Modus und Weboberfläche über einen eigenen WLAN-Hotspot.

## Hotspot und Zugangsdaten

| | Standardwert |
|---|---|
| **WLAN-Name (SSID)** | `Lizas-Lamp` |
| **WLAN-Passwort** | **`Erdmaennchen`** |
| Weboberfläche | http://4.3.2.1 (öffnet sich als Captive Portal meist automatisch) |

- Der Hotspot startet beim Einschalten bzw. per **langem Druck auf den Drehknopf** und geht nach 1 Minute ohne verbundenes Gerät wieder aus.
- Das Passwort ist bewusst ohne Umlaut geschrieben („ae“ statt „ä“): WPA2 erlaubt nur ASCII-Zeichen, Umlaute funktionieren auf vielen Handys nicht.
- Name und Passwort lassen sich in der Weboberfläche unter **Einstellungen → WLAN** ändern (Passwort 8–63 Zeichen oder per „Passwort entfernen“ offen).
- Der Standardwert gilt nach der Erstinstallation und nach einem **Werksreset**. Geräte, die schon vorher eingerichtet waren, behalten ihre gespeicherten Einstellungen (bisher: offener Hotspot ohne Passwort).
- Passwort vergessen → Werksreset: In der Weboberfläche unter **Einstellungen → System** oder per **Boot-Taste** (siehe [doc/controls.md](doc/controls.md)).

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

Weitere Details: [svelte-ui/DEPLOYMENT.md](svelte-ui/DEPLOYMENT.md), Architektur: [doc/architektur.md](doc/architektur.md).

## TODO

- **Einstellungen bei Firmware-Updates erhalten (S6):** Ändert sich durch ein Update die Größe von
  `FullConfig` oder `SystemSettings` (`include/types.h`), werden beim nächsten Start **alle Einstellungen
  inkl. WLAN-Zugangsdaten und Hotspot-Passwort auf Standard zurückgesetzt** (`ensureStructExists` in
  `src/store.cpp`). Lösung: Versionsfeld in den gespeicherten Strukturen + Migration alter Versionen.
  Bis dahin: nach Updates, die diese Strukturen ändern, die Lampe neu einrichten.

## Entwicklung

### Serieller Monitor
- Probleme beim Wiederverbinden: diese Erweiterung verwenden: https://marketplace.visualstudio.com/items?itemName=ms-vscode.vscode-serial-monitor
