#!/bin/bash
# Türklingel-Display flashen (Linux): im Terminal ausführen (bash flash-linux.sh).
#   flash-linux.sh [Datei.bin] [--check]
# Sucht die Firmware (Argument, Ordner dieses Skripts, Downloads, sonst das neueste GitHub-Release über "gh"),
# richtet beim ersten Mal esptool in einer eigenen Umgebung ein und spielt sie auf das Board auf.
# --check liest nur die Chip-Kennung aus und ändert nichts.
set -u
cd "$(dirname "$0")"
REPO="alexanderschubert/reolink-doorbell-esp-display"
ASSET="doorbell-display-esp32s3-lcd5.bin"
HOME_DIR="$HOME/.doorbell-display-flasher"
CHECK=0; BIN=""
for a in "$@"; do case "$a" in --check) CHECK=1;; *) BIN="$a";; esac; done

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
die() { printf '\n\033[31m%s\033[0m\n' "$*"; echo; exit 1; }

say "Türklingel-Display flashen"

# 1. esptool bereitstellen
command -v python3 >/dev/null || die "python3 fehlt. Installiere die Kommandozeilen-Werkzeuge:  sudo apt install python3 python3-venv (oder die Entsprechung deiner Distribution)"
if [ ! -x "$HOME_DIR/venv/bin/esptool.py" ]; then
  say "Richte esptool ein (nur beim ersten Mal, braucht Internet) …"
  python3 -m venv "$HOME_DIR/venv" 2>/dev/null && "$HOME_DIR/venv/bin/pip" install --quiet esptool || die "esptool konnte nicht installiert werden."
fi
ESPTOOL="$HOME_DIR/venv/bin/esptool.py"

# 2. Firmware finden
if [ "$CHECK" = 0 ]; then
  if [ -z "$BIN" ]; then
    for c in ./"$ASSET" ./*.bin "$HOME/Downloads/$ASSET" "$HOME"/Downloads/doorbell-display-esp32s3-lcd5*.bin; do
      [ -f "$c" ] && BIN="$c" && break
    done
  fi
  if [ -z "$BIN" ] && command -v gh >/dev/null; then
    say "Lade die neueste Firmware von GitHub …"
    mkdir -p "$HOME_DIR/dl"
    TAG="$(gh release list --repo "$REPO" --limit 20 --json tagName -q '.[].tagName' 2>/dev/null | grep '^firmware-' | head -1)"
    [ -n "$TAG" ] && gh release download "$TAG" --repo "$REPO" --pattern "$ASSET" --dir "$HOME_DIR/dl" --clobber && BIN="$HOME_DIR/dl/$ASSET"
  fi
  [ -f "$BIN" ] || die "Keine Firmware gefunden. Lege $ASSET neben dieses Skript oder in den Ordner Downloads (Release-Seite: https://github.com/$REPO/releases), oder gib den Pfad an."
  echo "Firmware: $BIN ($(du -h "$BIN" | cut -f1))"
fi

# 3. Anschluss finden
PORTS=( $(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null) )
[ ${#PORTS[@]} -gt 0 ] || die "Kein Board gefunden. USB-Kabel anschließen (Datenkabel, nicht nur Ladekabel). Hilft das nicht: BOOT-Taste halten, Kabel einstecken, loslassen."
PORT="${PORTS[0]}"
[ -r "$PORT" ] && [ -w "$PORT" ] || echo "Hinweis: Kein Zugriff auf $PORT. Einmalig:  sudo usermod -aG dialout \$USER  (danach neu anmelden), oder das Skript mit sudo starten."
if [ ${#PORTS[@]} -gt 1 ]; then
  echo "Mehrere Anschlüsse:"; i=1; for p in "${PORTS[@]}"; do echo "  $i) $p"; i=$((i+1)); done
  read -r -p "Nummer wählen: " n; PORT="${PORTS[$((n-1))]:-}"; [ -n "$PORT" ] || die "Ungültige Auswahl."
fi
echo "Anschluss: $PORT"

# 4. Prüfen oder flashen
if [ "$CHECK" = 1 ]; then
  say "Prüfe die Verbindung …"
  "$ESPTOOL" --chip esp32s3 -p "$PORT" chip_id || die "Keine Antwort vom Board."
  say "Verbindung in Ordnung. Es wurde nichts geändert."
  exit 0
fi
echo
echo "ACHTUNG: Das Board wird komplett neu beschrieben. Eingerichtetes WLAN, Einstellungen und hochgeladene Bilder gehen verloren."
read -r -p "Fortfahren? [j/N] " ok
[[ "$ok" =~ ^[jJyY]$ ]] || { echo "Abgebrochen."; exit 0; }

say "Spiele die Firmware auf …"
"$ESPTOOL" --chip esp32s3 -p "$PORT" -b 921600 write_flash -z 0x0 "$BIN" \
  || { say "Nochmal langsamer …"; "$ESPTOOL" --chip esp32s3 -p "$PORT" -b 115200 write_flash -z 0x0 "$BIN"; } \
  || die "Aufspielen fehlgeschlagen. BOOT-Taste halten, Kabel neu einstecken, loslassen und noch einmal versuchen."

say "Fertig. Das Display startet neu und zeigt die Einrichtung:"
echo "  1. Mit dem WLAN \"Doorbell-Display-Setup\" verbinden (Kennwort steht auf dem Display)"
echo "  2. Im Browser http://192.168.4.1 öffnen und WLAN und die Zugangsdaten der Klingel eintragen"
