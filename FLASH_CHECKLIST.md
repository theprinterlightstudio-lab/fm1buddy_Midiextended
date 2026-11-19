# FM Buddy 2.0 — flashing the PCBfun board

**Build:** 2026-11-19, `pcbfun-s3-28`
**Contains:** Granular Field Stage 4 (software-accepted) + the grabbable list scrollbar
**Version string on the board will read `dev`** — see §1.
**Nothing has been flashed. This is preparation only.**

---

## 0. Read this first: I cannot flash it from here

Your Mac is connected to this session, but the bridge gives me a **Linux VM on your machine, not
macOS itself**. It has no access to `/dev/cu.usbmodem*`, so it cannot see or talk to the board.
Flashing has to be run by you, in Terminal on the Mac.

Everything below is a command you run. I am not going to flash anything, and I won't ask you to
authorize me doing so — it isn't technically possible from this session.

## 1. Decide before you start

**The version will show `dev`, not `Arbi002`.** `FM1BUDDY_VERSION` in `src/app/hal.h:35` is still
`"dev"`. That is correct for a test flash — you said `Arbi002` is the release identifier — but
Settings ▸ About will read `dev`, and the Firmware Hub's "is there a newer one?" check compares
against that string. If you want this flash labelled, change that line and rebuild first.

**What is in this build that is not on the board now:**

- Granular Field, Stages 1–4: the sixth touch mode, two voices, the confirmed-touch policy, the
  shared-bend handover, the 80 ms parameter ramp.
- The new list scrollbar on every scrolling page.
- **The Shadow Granular Layer**: short quiet notes scattered from the finger's trail, always in the
  set scale, on their own MIDI channel (default 2). **Off by default** — turn it on in the Field
  settings under Shadow granules.
- **The scale grid**: faint 7x7 lines on the Field surface, with the gesture snapped to its cells.
  **Off by default** - Field settings, Scale grid.
- Nothing else. No MIDI routing, sequencer or storage changes.

**What is untested on real hardware** — this flash is how it gets tested:

- every Granular Field constant (`FT_CONFIRM`, `FT_RECOVER_PX`, `FT_MAX_PXMS`, `FT_FAST_PXMS`,
  `FT_WET_MS`), the 170–180 px identity-weighting band, and whether the FT6336's real dropouts fit
  inside a 30 ms window;
- whether the shared-bend handover is audible on a real granular synth;
- whether the 18 px scrollbar strip is a comfortable target on real glass;
- the panel's `weight`/`area`, which density is waiting on.

## 1b. The short way: one file does all of it

**`FLASH-FM-BUDDY.command`** (Mac — double-click it) does everything in sections 2 and 3 in order:
finds its own folder so it doesn't matter where you run it from, finds esptool including your
`~/esptool-env`, finds the board, backs it up, **checks the backup is the right size and refuses to
flash if it isn't**, asks you to type `yes`, then writes.

If double-clicking says the file can't be opened, run this once in Terminal:

    chmod +x ~/Downloads/fm-buddy-flash/FLASH-FM-BUDDY.command

`FLASH-FM-BUDDY.bat` is the Windows equivalent, built on the repo's proven `tools\pcbfun_flash.bat`
plus the backup step that one doesn't do. **It has not been run on Windows** — read what it prints
the first time rather than trusting it.

Sections 2 and 3 below are the same thing by hand, if you'd rather watch each step.

## 2. Back up the board — do not skip this

The flash step erases otadata, and an erase has no undo. Settings live at `nvs` (0x9000) and are
*not* touched by the normal flash, but a backup is what makes the whole thing reversible.

```sh
cd ~/Downloads/fm-buddy-flash          # wherever you unzipped this
chmod +x pcbfun_backup_mac.sh pcbfun_flash_mac.sh
./pcbfun_backup_mac.sh
```

It writes nothing to the board. You get a folder `fm1buddy-backup-<date>` containing:

| File | What it is |
|---|---|
| `head-0x0-0x10000.bin` | bootloader, partition table, **your settings**, otadata |
| `app0-0x10000-0x640000.bin` | the app the board is running right now |

**Check both files exist and are not zero bytes before going on.** Keep the folder until the new
firmware has proved itself.

If esptool isn't found, the virtualenv from 16 November is still in your home:

```sh
source ~/esptool-env/bin/activate
```

## 3. Flash

```sh
./pcbfun_flash_mac.sh fm1buddy-pcbfun-s3-28-app.bin
```

It does exactly two things:

1. writes the app to **app0 at 0x10000**
2. clears **otadata at 0xe000**, so the board boots app0 even if a self-update had moved it to app1

The bootloader and your settings are not written. Wi-Fi, theme, MIDI setup and touch calibration
survive.

If the board isn't found: plug it in with a USB-C cable **that carries data** (a charge-only cable
is the usual culprit), or put it in download mode yourself — Settings ▸ Flash mode ▸ Restart for
flashing on the board, or hold BOOT, tap RESET, release BOOT. You can also name the port:
`PORT=/dev/cu.usbmodem1101 ./pcbfun_flash_mac.sh fm1buddy-pcbfun-s3-28-app.bin`.

## 4. Check it came up

1. The board restarts on its own. It should reach the normal UI, not a boot loop.
2. Settings ▸ About → version reads `dev`.
3. Settings ▸ Wi-Fi still shows your network, and the theme is as you left it. If either is gone,
   the settings partition was disturbed — stop and restore from §5.
4. Open the Touch page. The mode list should offer **Field** between Touche and Sequencer.
5. Scroll any list. The scrollbar should be a fat rounded bar you can drag with a fingertip.

## 5. If it goes wrong

**Rolling back to the previous app** (the one you backed up in §2):

```sh
./pcbfun_flash_mac.sh fm1buddy-backup-<date>/app0-0x10000-0x640000.bin
```

Or to the 16 November build, which is in this package:

```sh
./pcbfun_flash_mac.sh recovery/fm1buddy-NOV16-app-rollback.bin
```

**If the settings are gone or corrupted**, write the head back. This restores the bootloader,
partition table and nvs together:

```sh
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 \
  write-flash 0x0 fm1buddy-backup-<date>/head-0x0-0x10000.bin
```

**If the board will not boot at all** and neither of the above reaches it, `recovery/` holds a
factory image. **It writes from 0x0 and will erase your settings** — it is the last resort, not a
normal flash, and `pcbfun_flash_mac.sh` deliberately refuses to write any file with `factory` in
its name:

```sh
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 \
  write-flash 0x0 recovery/fm1buddy-pcbfun-s3-28-factory.bin
```

## 6. What to do once it is running — the physical validation session

This is the session that has been blocking Granular Field. Keep the three kinds of evidence apart,
as before: what the host tests showed, what the simulator showed, and what the board actually does.

**Identity and tracking** — the constants nothing but hardware can settle:

- Hold one finger still in Field mode for 30 seconds. Does the note ever cut? If it does,
  `FT_CONFIRM` (3 polls, ~30 ms) is too short for this panel.
- Put two fingers on the same vertical line, about 40 px apart — the FT6336's known bad case. Does
  either voice drop?
- Lift one of two fingers and immediately land a new one elsewhere. Does the new finger get its own
  note, or inherit the old one? That is the recycled-id fix under real conditions.
- Move one finger fast across the surface. Does it keep its voice, or retrigger? Too many
  retriggers means `FT_MAX_PXMS` (4 px/ms) is too strict.

**Expressive response:**

- Sweep a finger at performance speed and watch CC 19 (spray). Does it reach the top? Does it feel
  proportionate? That is `FT_FAST_PXMS`.
- Hold a finger and watch CC 21 (wet) rise. Four seconds to full — too fast, too slow?
- Two fingers apart and together, CC 20 (spread). Lift one: it should hold, not snap to zero.
- **The one that needs a real synth:** with granularity on SNAP or FREE, bend a note with one
  finger, then land a second. You should hear the bend slide to centre over 80 ms and both notes
  land in tune. Listen for a click or a swoop at the moment the second finger arrives, and for the
  ~10 ms where the new voice sounds at the outgoing bend. That window is documented and expected;
  the question is whether it is audible enough to matter.

**Sensor characterization** — the gate on density leaving deferred status:

- Press light and hard, small contact and flat-finger. Do `weight` and `area` vary usefully, or are
  they noise? The simulator reports both as 0, so this has never been exercised.

**The scrollbar:**

- Can you land on it with a thumb without looking? Does dragging it ever open the row underneath?
  Does a tap just left of it still open the row?

## 7. Checksums

```
ffa7db53289d5ac93270f06a46b2acf8096ccc5cdb1b69520e7dab08e8b2e608  fm1buddy-pcbfun-s3-28-app.bin
b15f7add7cf74233ce310291d20262279123397a8db864483da562a1adfc2a4b  recovery/fm1buddy-NOV16-app-rollback.bin
461f1064d406d32fb931f083ae647f2f5e6b91d42af5052df5e296a5b52da383  recovery/fm1buddy-pcbfun-s3-28-factory.bin
```

Verify before flashing:

```sh
shasum -a 256 -c SHA256.txt
```

The 16 November app hash `b15f7add…` matches the one you already have, so you can confirm the
rollback image is the build you were running.

Build: RAM 43.6% (142,708 / 327,680), Flash 34.8% (2,279,486 / 6,553,600).
`pio run -e pcbfun-s3-28` SUCCESS. 14 host suites pass. Simulator verified.

---

**Nothing in this package has been written to any board. No commits have been made.**
