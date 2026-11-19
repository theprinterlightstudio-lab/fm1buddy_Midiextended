#!/bin/sh
# FM Buddy 2.0 — flash the PCBfun board from a Mac, KEEPING its settings.
#
# The same two steps tools\pcbfun_flash.bat does on Windows:
#   1. write the app to app0 at 0x10000
#   2. clear otadata at 0xe000, so the board boots app0 even if a self-update had moved it to app1
# The settings (nvs at 0x9000) and the bootloader are not touched. A factory image from 0x0 WOULD wipe the
# settings, so this script refuses one.
#
#   chmod +x pcbfun_flash_mac.sh
#   ./pcbfun_flash_mac.sh fm1buddy-pcbfun-s3-28-app.bin
#
# Needs esptool. If it isn't there, the script says how to get it and stops.
set -e

APP="${1:-fm1buddy-pcbfun-s3-28-app.bin}"

if [ ! -f "$APP" ]; then
  echo "Can't find $APP. Give the file as the argument:"
  echo "  ./pcbfun_flash_mac.sh /path/to/fm1buddy-pcbfun-s3-28-app.bin"
  exit 1
fi

case "$APP" in
  *factory*)
    echo "That is the factory image. Writing it from 0x0 would erase the board's settings"
    echo "(touch calibration, MIDI setup, theme). Use the app .bin instead:"
    echo "  fm1buddy-pcbfun-s3-28-app.bin"
    exit 1
    ;;
esac

# ---------------------------------------------------------------------------- esptool
if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL="esptool.py"
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL="esptool"
elif python3 -c "import esptool" >/dev/null 2>&1; then
  ESPTOOL="python3 -m esptool"
else
  echo "esptool isn't installed. Any one of these will do:"
  echo "  brew install esptool"
  echo "  pipx install esptool"
  echo "  python3 -m pip install --user esptool"
  exit 1
fi

# ---------------------------------------------------------------------------- the port
# The ESP32-S3's own USB Serial/JTAG appears as /dev/cu.usbmodem*. Use cu, not tty: tty waits for carrier.
PORT="${PORT:-}"
if [ -z "$PORT" ]; then
  for p in /dev/cu.usbmodem*; do
    [ -e "$p" ] || continue
    PORT="$p"
    break
  done
fi
if [ -z "$PORT" ]; then
  echo "No board found on /dev/cu.usbmodem*."
  echo
  echo "Plug the board into the Mac with a USB-C cable that carries data, then try again."
  echo "If it still isn't there, put it in download mode yourself:"
  echo "  - on the board: Settings > Flash mode > Restart for flashing, or"
  echo "  - hold BOOT, tap RESET, let go of BOOT."
  echo "Or set the port by hand:  PORT=/dev/cu.usbmodem1101 ./pcbfun_flash_mac.sh $APP"
  exit 1
fi
echo "Board on $PORT"
echo

# esptool 5 uses hyphens (write-flash); older versions use underscores (write_flash).
if $ESPTOOL --help 2>&1 | grep -q "write-flash"; then
  WRITE="write-flash"; ERASE="erase-region"; BEFORE="default-reset"; AFTER_KEEP="no-reset"; AFTER_GO="hard-reset"
else
  WRITE="write_flash"; ERASE="erase_region"; BEFORE="default_reset"; AFTER_KEEP="no_reset"; AFTER_GO="hard_reset"
fi

echo "Writing $APP to app0 (0x10000) ..."
$ESPTOOL --chip esp32s3 --port "$PORT" --before "$BEFORE" --after "$AFTER_KEEP" "$WRITE" 0x10000 "$APP"

echo
echo "Clearing otadata (0xe000), so it boots app0 ..."
$ESPTOOL --chip esp32s3 --port "$PORT" --before "$BEFORE" --after "$AFTER_GO" "$ERASE" 0xe000 0x2000

echo
echo "Done. The board restarts with this build; Settings > About shows the version."
