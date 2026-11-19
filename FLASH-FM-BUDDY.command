#!/bin/sh
# FM Buddy 2.0 — back up the board, then flash it. One file, double-click it.
#
# Does the whole sequence in order, and refuses to flash if the backup did not work:
#   1. finds its own folder, so it does not matter where you run it from
#   2. finds esptool (including the ~/esptool-env set up on 16 Nov)
#   3. finds the board
#   4. reads the board's settings and current app back to this folder
#   5. checks the backup is real before going further
#   6. asks you before it writes anything
#   7. writes the app to app0, clears otadata
#
# Nothing is written to the board before step 7, and step 7 does not happen without a yes.

cd "$(dirname "$0")" || exit 1

APP="fm1buddy-pcbfun-s3-28-app.bin"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="fm1buddy-backup-$STAMP"

say() { printf '%s\n' "$*"; }
stop() { say ""; say "STOPPED: $*"; say ""; say "Nothing was written to the board."; say "Press return to close."; read _x; exit 1; }

say "FM Buddy 2.0 — flash"
say "===================="
say ""
say "Folder: $(pwd)"

# ----------------------------------------------------------------- 1. the app file
[ -f "$APP" ] || stop "$APP is not in this folder."
case "$APP" in
  *factory*) stop "That is the factory image: writing it would erase the board's settings." ;;
esac

# ----------------------------------------------------------------- 2. esptool
ESPTOOL=""
[ -z "$ESPTOOL" ] && command -v esptool.py >/dev/null 2>&1 && ESPTOOL="esptool.py"
[ -z "$ESPTOOL" ] && command -v esptool >/dev/null 2>&1 && ESPTOOL="esptool"
[ -z "$ESPTOOL" ] && python3 -c "import esptool" >/dev/null 2>&1 && ESPTOOL="python3 -m esptool"
if [ -z "$ESPTOOL" ] && [ -f "$HOME/esptool-env/bin/activate" ]; then
  . "$HOME/esptool-env/bin/activate"
  python3 -c "import esptool" >/dev/null 2>&1 && ESPTOOL="python3 -m esptool"
fi
if [ -z "$ESPTOOL" ]; then
  say ""
  say "esptool isn't installed. Try one of these in Terminal, then run this again:"
  say "    source ~/esptool-env/bin/activate"
  say "    python3 -m pip install --user esptool"
  stop "no esptool"
fi
say "esptool: $ESPTOOL"

# esptool 5 uses hyphens; older versions use underscores
if $ESPTOOL --help 2>&1 | grep -q "write-flash"; then
  READ=read-flash; WRITE=write-flash; ERASE=erase-region; BEFORE=default-reset; KEEP=no-reset; GO=hard-reset
else
  READ=read_flash; WRITE=write_flash; ERASE=erase_region; BEFORE=default_reset; KEEP=no_reset; GO=hard_reset
fi

# ----------------------------------------------------------------- 3. the board
if [ -z "$PORT" ]; then
  for p in /dev/cu.usbmodem*; do
    [ -e "$p" ] || continue
    PORT="$p"
    break
  done
fi
if [ -z "$PORT" ]; then
  say ""
  say "No board found on /dev/cu.usbmodem*."
  say ""
  say "  - use a USB-C cable that carries DATA (a charge-only cable is the usual reason)"
  say "  - or on the board: Settings > Flash mode > Restart for flashing"
  say "  - or hold BOOT, tap RESET, let go of BOOT"
  stop "no board"
fi
say "Board:   $PORT"

# ----------------------------------------------------------------- 4. back up
say ""
say "Backing up to $BACKUP (writes nothing to the board) ..."
mkdir -p "$BACKUP" || stop "could not make $BACKUP"

say "  settings and boot data (64 KB) ..."
$ESPTOOL --chip esp32s3 --port "$PORT" --before "$BEFORE" --after "$GO" \
         "$READ" 0x0 0x10000 "$BACKUP/head-0x0-0x10000.bin" || stop "could not read the board's settings"

say "  the app it is running now (6.5 MB, a minute or so) ..."
$ESPTOOL --chip esp32s3 --port "$PORT" --before "$BEFORE" --after "$GO" \
         "$READ" 0x10000 0x640000 "$BACKUP/app0-0x10000-0x640000.bin" || stop "could not read the board's app"

# ----------------------------------------------------------------- 5. is the backup real
for f in "$BACKUP/head-0x0-0x10000.bin" "$BACKUP/app0-0x10000-0x640000.bin"; do
  [ -s "$f" ] || stop "$f is empty. Without a good backup this is not reversible."
done
HEAD_SZ=$(wc -c < "$BACKUP/head-0x0-0x10000.bin" | tr -d ' ')
APP0_SZ=$(wc -c < "$BACKUP/app0-0x10000-0x640000.bin" | tr -d ' ')
[ "$HEAD_SZ" = "65536" ] || stop "the settings backup is $HEAD_SZ bytes, expected 65536."
[ "$APP0_SZ" = "6553600" ] || stop "the app backup is $APP0_SZ bytes, expected 6553600."
shasum -a 256 "$BACKUP"/*.bin > "$BACKUP/SHA256.txt" 2>/dev/null
say "  backup good: $HEAD_SZ + $APP0_SZ bytes"

# ----------------------------------------------------------------- 6. ask
say ""
say "About to write $APP to the board."
say "Its settings (Wi-Fi, theme, MIDI setup, touch calibration) are kept."
say "If anything goes wrong, $BACKUP puts it back."
say ""
printf "Flash it now? Type yes and press return: "
read ANSWER
case "$ANSWER" in
  yes|YES|Yes) ;;
  *) say ""; say "Not flashed. The backup in $BACKUP is still there."; say "Press return to close."; read _x; exit 0 ;;
esac

# ----------------------------------------------------------------- 7. flash
say ""
say "Writing the app to app0 (0x10000) ..."
$ESPTOOL --chip esp32s3 --port "$PORT" --before "$BEFORE" --after "$KEEP" \
         "$WRITE" 0x10000 "$APP" || stop "writing the app failed. The board still has its old app; nothing else was changed."

say ""
say "Clearing otadata (0xe000), so it boots the new app ..."
$ESPTOOL --chip esp32s3 --port "$PORT" --before "$BEFORE" --after "$GO" \
         "$ERASE" 0xe000 0x2000 || stop "clearing otadata failed. The new app is in app0; run this again."

say ""
say "Done. The board restarts on its own."
say ""
say "Check:  Settings > About        version reads dev"
say "        Settings > Wi-Fi        your network is still there"
say "        Touch page > mode list  Field is between Touche and Sequencer"
say "        any list                the scrollbar is a fat bar you can drag"
say ""
say "Backup kept in $BACKUP — keep it until the new firmware has proved itself."
say ""
say "Press return to close."
read _x
