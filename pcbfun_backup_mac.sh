#!/bin/sh
# FM Buddy 2.0 — read the board's flash back to the Mac BEFORE flashing anything.
#
# Run this first. The flash step writes app0 and then ERASES otadata, and an erase has no undo.
# This takes a copy of everything that would be hard to get back:
#
#   head   0x0      64 KB   bootloader, partition table, nvs (Wi-Fi, theme, MIDI setup, touch
#                           calibration), otadata
#   app0   0x10000  6.5 MB  the app the board is running now
#
# It writes nothing to the board. Reading is safe to repeat.
#
#   chmod +x pcbfun_backup_mac.sh
#   ./pcbfun_backup_mac.sh
#
# Restoring is in FLASH_CHECKLIST.md, section 5.
set -e

OUT="${1:-fm1buddy-backup-$(date +%Y%m%d-%H%M%S)}"

# ---------------------------------------------------------------------------- esptool
if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL="esptool.py"
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL="esptool"
elif python3 -c "import esptool" >/dev/null 2>&1; then
  ESPTOOL="python3 -m esptool"
elif [ -x "$HOME/esptool-env/bin/esptool.py" ]; then
  ESPTOOL="$HOME/esptool-env/bin/esptool.py"
elif [ -x "$HOME/esptool-env/bin/esptool" ]; then
  ESPTOOL="$HOME/esptool-env/bin/esptool"
else
  echo "esptool isn't installed. Any one of these will do:"
  echo "  source ~/esptool-env/bin/activate     (the one set up on 16 Nov)"
  echo "  brew install esptool"
  echo "  python3 -m pip install --user esptool"
  exit 1
fi

# ---------------------------------------------------------------------------- the port
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
  echo "Or set the port by hand:  PORT=/dev/cu.usbmodem1101 ./pcbfun_backup_mac.sh"
  exit 1
fi
echo "Board on $PORT"

# esptool 5 uses hyphens (read-flash); older versions use underscores (read_flash).
if $ESPTOOL --help 2>&1 | grep -q "read-flash"; then
  READ="read-flash"; BEFORE="default-reset"; AFTER="hard-reset"
else
  READ="read_flash"; BEFORE="default_reset"; AFTER="hard_reset"
fi

mkdir -p "$OUT"
echo
echo "Reading the head (bootloader, partitions, settings, otadata) -> $OUT/head-0x0-0x10000.bin"
$ESPTOOL --chip esp32s3 --port "$PORT" --before "$BEFORE" --after "$AFTER" \
         "$READ" 0x0 0x10000 "$OUT/head-0x0-0x10000.bin"

echo
echo "Reading app0 (the app it is running now) -> $OUT/app0-0x10000-0x640000.bin   (6.5 MB, a minute or so)"
$ESPTOOL --chip esp32s3 --port "$PORT" --before "$BEFORE" --after "$AFTER" \
         "$READ" 0x10000 0x640000 "$OUT/app0-0x10000-0x640000.bin"

shasum -a 256 "$OUT"/*.bin > "$OUT/SHA256.txt"

echo
echo "Backed up to $OUT:"
ls -lh "$OUT"
echo
echo "Check both files are there and not zero bytes BEFORE you flash."
echo "Keep this folder until the new firmware has proved itself."
