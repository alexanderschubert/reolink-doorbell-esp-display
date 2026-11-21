# Reolink Doorbell ESP Display

Ein Touch-Display, das bei Bewegung vor der Haustür das **Live-Bild deines Reolink-Türspions** zeigt. Sonst ist es eine Uhr mit Bildschirmschoner und Nachtmodus. Läuft auf dem **Waveshare ESP32-S3-Touch-LCD-5** (800×480).

## Was es kann

- **Live-Bild bei Bewegung:** Das Display fragt den Türspion einmal pro Sekunde nach Bewegung (auch Person und Klingeltaste lösen aus) und zeigt dann sein Bild als Vollbild. Es sind JPEG-Einzelbilder, kein echtes Video: etwa 2 bis 5 Bilder pro Sekunde (geschätzt, noch nicht an der echten Klingel gemessen).
- **Dauer einstellbar:** Das Bild bleibt für die eingestellte Zeit ab der letzten erkannten Bewegung (3 bis 300 s), höchstens fünf Minuten am Stück.
- **Berührung schließt das Bild.** Es erscheint erst wieder, wenn die Bewegung einmal aufgehört hat.
- **Danach wie vorher:** Das Bild liegt über Bildschirmschoner und Nachtmodus. Danach ist wieder alles wie vorher, und war die Beleuchtung nachts aus, geht sie wieder aus.
- **Nuki-Schloss auf der Startseite** (Smart Lock 3.0 Pro, über die Nuki-Web-API): Zustand, Tür und Batterie. Optional (eigener Schalter, standardmäßig aus) lässt sich das Schloss am Display zu- und aufschließen; Aufschließen nur mit Gedrückthalten zum Bestätigen. API-Token auf der Webseite des Displays eintragen; zum Bedienen braucht er das Recht zum Ausführen von Aktionen. Anzeige und Bedienung lassen sich getrennt ein- und ausschalten, ebenso das Live-Bild des Türspions.
- **Uhr, Bildschirmschoner (Uhr oder Bilderschau), Nachtmodus**, dunkel/hell, Hintergrundbilder.
- **Einrichtung im Browser**, keine Zugangsdaten im Quelltext.

## Voraussetzungen

- Waveshare ESP32-S3-Touch-LCD-5 (800×480, 16 MB Flash, 8 MB PSRAM).
- Reolink-Türspion im selben Netz, Web-Zugang aktiv. Am besten ein eigener Benutzer auf dem Türspion. Das Display spricht HTTPS mit dem Türspion (sein selbst signiertes Zertifikat wird nicht geprüft); "http://" vor der Adresse erzwingt HTTP.

## Aufspielen und einrichten

Fertige Datei `doorbell-display-esp32s3-lcd5.bin` (bei den Releases, sobald es eine gibt) mit den Skripten in `firmware/flash/` (macOS, Windows, Linux) oder per `esptool`:

```bash
esptool.py --chip esp32s3 -b 921600 write_flash 0x0 doorbell-display-esp32s3-lcd5.bin
```

1. Nach dem Start zeigt das Display die Einrichtung: mit dem WLAN `Tuerspion-Display-Setup` verbinden (Kennwort steht auf dem Display), im Browser `http://192.168.4.1` öffnen, WLAN eintragen, speichern.
2. Danach ist die Webseite des Displays unter seiner Adresse erreichbar (Einstellungen → Dieses Display). Benutzer `display`, Kennwort ist die PIN vom Display.
3. Unter „Verbindung ändern“ Adresse, Benutzer und Kennwort des Türspions eintragen. Auf der Startseite der Webseite oder am Display unter Einstellungen → Türspion einschalten und die Dauer wählen. „Jetzt testen“ zeigt das Bild sofort.

Wer Frigate, go2rtc oder Home Assistant hat, kann dort eine eigene JPEG-Adresse eintragen, falls der Snap des Türspions nicht passt. Bewegung kommt dann trotzdem vom Türspion.

## Selbst bauen

Siehe `firmware/build.sh`. Es braucht `arduino-cli`, den ESP32-Kern 3.0.7, LVGL 8.4 und die Bibliotheken aus Waveshares Demo-ZIP (`files.waveshare.com/wiki/ESP32-S3-Touch-LCD-5/ESP32-S3-Touch-LCD-5-Demo.zip`); `WAVESHARE_DEMO` zeigt auf den entpackten Ordner `.../ESP32-S3-Touch-LCD-5-Demo/Arduino`.

```bash
cd firmware
./build.sh                          # übersetzen
./build.sh upload /dev/cu.usbmodemXXXX   # aufspielen (mit secrets.h, siehe secrets.h.example)
./build.sh release                  # Datei ohne Zugangsdaten nach firmware/dist/
```

## Technik

- Bewegung: HTTP(S)-API des Türspions, `GetEvents` (ältere Firmware: `GetMdState`).
- Bild: `Snap` (Substream) in schneller Folge, dekodiert mit dem JPEG-Dekoder im ROM des ESP32-S3 in den PSRAM (nur Baseline-JPEG), Anzeige per LVGL auf einer eigenen Ebene.
- Netzwerk und Dekodieren laufen in einem eigenen Task auf Kern 1, das Zeichnen auf Kern 0.

## Lizenz

MIT, siehe [LICENSE](LICENSE). Schriften: Lato (SIL OFL 1.1), Symbole: Font Awesome Free (CC BY 4.0), siehe `firmware/licenses/`.
