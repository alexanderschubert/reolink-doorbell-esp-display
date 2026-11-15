#!/bin/bash
# Übersetzt (und spielt auf) die Firmware für das Waveshare ESP32-S3-Touch-LCD-5 (800×480).
#   ./build.sh                       übersetzen
#   ./build.sh upload /dev/cu.usbmodemXXXX    übersetzen und aufspielen
#
# Voraussetzungen (siehe README.md): arduino-cli im Pfad, ESP32-Kern 3.0.7, die Bibliotheken aus Waveshares Demo
# im Arduino-Bibliotheksordner, und WAVESHARE_DEMO zeigt auf den entpackten Ordner "ESP32-S3-Touch-LCD-5-Demo/Arduino".
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEMO="${WAVESHARE_DEMO:?Setze WAVESHARE_DEMO auf .../ESP32-S3-Touch-LCD-5-Demo/Arduino (siehe README.md)}"
BUILD="${BUILD_DIR:-$HERE/build}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,FlashMode=qio120,CPUFreq=240${FQBN_EXTRA:+,$FQBN_EXTRA}"   # FQBN_EXTRA=CDCOnBoot=cdc leitet Serial.print an den USB-Anschluss
ADAPTER="$DEMO/examples/09_lvgl_v8_demo"

rm -rf "$BUILD/doorbell_display" && mkdir -p "$BUILD"
cp -R "$HERE/doorbell_display" "$BUILD/doorbell_display"
# Waveshares Anpassung an LVGL und die Platine kommt aus ihrer Demo und liegt nicht in diesem Repo.
cp "$ADAPTER/esp_lv_adapter_arduino.h" "$ADAPTER/esp_lv_adapter_arduino.cpp" "$BUILD/doorbell_display/"
sed 's/#define ESP_PANEL_USE_1024_600_LCD .*(1)/#define ESP_PANEL_USE_1024_600_LCD           (0)/' \
  "$ADAPTER/esp_panel_board_custom_conf.h" > "$BUILD/doorbell_display/esp_panel_board_custom_conf.h"
PCLK_MHZ="${PCLK_MHZ:-12}"    # Waveshares 16 MHz ließ das Bild bei unserer Last rollen; 12 MHz steht ruhig
[ -n "$PCLK_MHZ" ] && sed -i.bak "/RGB_CLK_HZ/ s/(16 \* 1000 \* 1000)/($PCLK_MHZ * 1000 * 1000)/" "$BUILD/doorbell_display/esp_panel_board_custom_conf.h"   # Pixeltakt senken, falls das Bild rollt
[ -n "${DEBUG_LOG:-}" ] && echo "#define DEBUG_LOG $DEBUG_LOG" >> "$BUILD/doorbell_display/config.h"
[ -n "${BOUNCE_LINES:-}" ] && echo "#define BOUNCE_LINES $BOUNCE_LINES" >> "$BUILD/doorbell_display/config.h"
grep -q 'ESP_PANEL_USE_1024_600_LCD .*(0)' "$BUILD/doorbell_display/esp_panel_board_custom_conf.h" || { echo "Auflösung nicht auf 800x480 gesetzt"; exit 1; }

if [ "${1:-}" = "release" ]; then      # fertige Firmware ohne eingebaute Zugangsdaten: sie richtet sich im Browser ein
  cp "$HERE/doorbell_display/secrets.h.example" "$BUILD/doorbell_display/secrets.h"
elif [ ! -f "$HERE/doorbell_display/secrets.h" ]; then
  echo "Hinweis: secrets.h fehlt, es wird nur mit den Beispielwerten übersetzt. Zum Aufspielen secrets.h.example kopieren und ausfüllen."
  cp "$HERE/doorbell_display/secrets.h.example" "$BUILD/doorbell_display/secrets.h"
  [ "${1:-}" = "upload" ] && { echo "Ohne secrets.h wird nichts aufgespielt."; exit 1; }
fi

[ -n "${UI_LEVEL:-}" ] && echo "#define UI_LEVEL $UI_LEVEL" >> "$BUILD/doorbell_display/config.h"   # nur zum Eingrenzen von Fehlern
[ -n "${FORCE_DEMO:-}" ] && echo "#define FORCE_DEMO 1" >> "$BUILD/doorbell_display/config.h"   # Beispieldaten, kein WLAN (zum Eingrenzen von Bildfehlern)
arduino-cli compile --fqbn "$FQBN" --output-dir "$BUILD/out" "$BUILD/doorbell_display"

if [ "${1:-}" = "release" ]; then
  mkdir -p "$HERE/dist"
  BOOT0="$(find "$HOME" "$ARDUINO_DIRECTORIES_DATA" -name boot_app0.bin -path '*esp32*' 2>/dev/null | head -1 || true)"
  [ -n "$BOOT0" ] || { echo "boot_app0.bin nicht gefunden"; exit 1; }
  OUT="$HERE/dist/doorbell-display-esp32s3-lcd5.bin"
  ${ESPTOOL:-esptool.py} --chip esp32s3 merge_bin -o "$OUT" --flash_mode keep --flash_freq keep --flash_size keep \
    0x0 "$BUILD/out/doorbell_display.ino.bootloader.bin" 0x8000 "$BUILD/out/doorbell_display.ino.partitions.bin" 0xe000 "$BOOT0" 0x10000 "$BUILD/out/doorbell_display.ino.bin"
  ls -l "$OUT"; md5 "$OUT" 2>/dev/null || md5sum "$OUT"
fi

if [ "${1:-}" = "upload" ]; then
  PORT="${2:?Anschluss angeben, z. B. /dev/cu.usbmodem31101}"
  ESPTOOL="${ESPTOOL:-esptool.py}"
  # Einzelteile statt der zusammengefassten Datei: sie überschreibt den ganzen Flash und löscht die hochgeladenen Bilder
  BOOT0="$(find "$HOME" "$ARDUINO_DIRECTORIES_DATA" -name boot_app0.bin -path '*esp32*' 2>/dev/null | head -1 || true)"
  if [ -n "$BOOT0" ]; then
    $ESPTOOL --chip esp32s3 -p "$PORT" -b 921600 write_flash -z 0x0 "$BUILD/out/doorbell_display.ino.bootloader.bin" \
      0x8000 "$BUILD/out/doorbell_display.ino.partitions.bin" 0xe000 "$BOOT0" 0x10000 "$BUILD/out/doorbell_display.ino.bin"
  else
    echo "boot_app0.bin nicht gefunden, nehme die zusammengefasste Datei (löscht die Bilder im Display)"
    $ESPTOOL --chip esp32s3 -p "$PORT" -b 921600 write_flash -z 0x0 "$BUILD/out/doorbell_display.ino.merged.bin"
  fi
fi
