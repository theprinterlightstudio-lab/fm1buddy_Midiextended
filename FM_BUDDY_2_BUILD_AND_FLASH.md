# FM Buddy 2.0

Firmware for a PCBfun ESP32-S3 2.8" touchscreen board, turning it into a MIDI control surface for
the M-VAVE FM-1 — touch faders and knobs, pads, a sequencer, a firmware hub, and **Granular Field**,
a whole-surface gesture mode driving a granular synth while still playing in a scale.

Built on **fm1buddy** by David Konsumer. Distributed under **GPL-3.0-only**, as your fork is.

---

## Read this before unzipping over your repo

This package is **not a replacement repo**. It is the source as it stood in the cloud workspace,
which was missing files your fork has. Two files here were renamed so they cannot overwrite yours:

| In this package | Your repo's own |
|---|---|
| `FM_BUDDY_2_BUILD_AND_FLASH.md` (this file) | `README.md` — **keep yours** |
| `gitignore.additions` | `.gitignore` — **merge**, do not replace |

`LICENSE` and `LICENSING.md` are **not** in this package, so unzipping cannot touch them. They stay
as they are.

### Take `platformio.ini` from your repo, not from here

The workspace this was last built in blocks the PlatformIO registry, so its `lib_deps` had been
rewritten to `symlink:///home/claude/libs/...` — absolute paths that exist on no other machine. I
replaced them with registry specs pinned to the exact versions that were vendored:

| Library | Version |
|---|---|
| lvgl | 9.2.2 |
| ArduinoJson | 7.2.1 |
| NimBLE-Arduino | 2.5.1 |
| TFT_eSPI | 2.5.43 |
| TinyUSB (upstream, not the Adafruit wrapper) | 0.21.0 |

**None of these were resolved against the registry**, because it is unreachable from there. The
TinyUSB tag is a guess and may need to be `v0.21.0`. **Your repo's `platformio.ini` is the one known
to build — use it, and take only the versions above as a cross-check.**

### Licensing

The fork is **GPL-3.0-only**, forked from **konsumer/fm1buddy**, with `LICENSE` and `LICENSING.md`
in the root. Nothing here changes that: the work still distributes under GPL-3.0.

Upstream source files carry individual MIT headers — `src/app/bigscreen.h` is
`SPDX-License-Identifier: MIT` with `Copyright (c) 2026 David Konsumer`. The Granular Field files
added here follow that convention and are marked MIT, which is GPL-compatible, so the combined work
remains GPL-3.0-only.

**One thing is incomplete.** These new files carry `SPDX-License-Identifier: MIT` with **no
copyright line**, unlike konsumer's:

```
src/app/field.h  fieldtouch.h  fieldnote.h  fieldmap.h  fieldshadow.h
test/fieldtouch_test.cpp  fieldshadow_test.cpp
```

An MIT notice without a copyright holder names nobody. Tell me which name to use and I will add
`Copyright (c) 2026 <name>` to each — or say the word and I will mark them GPL-3.0-only instead, to
match the project rather than the upstream file convention.

### A fresh clone does not patch TFT_eSPI on its first build

`tools/tft_fast.py` trims 270 ms off the screen's start-up by patching `TFT_eSPI.cpp`, which does
not exist until PlatformIO has downloaded it. **It used to abort the build.** It now prints a notice
and carries on; run the build a second time and the patch applies. This fix is worth taking into
your repo — without it, anyone cloning fresh hits a hard failure on their first build.

## Building

```sh
pio run -e pcbfun-s3-28     # the board
pio run -e sim              # the desktop simulator (SDL2)
bash tools/unit-test.sh     # 15 host test suites
```

Run the board build **twice** on a fresh clone (see the TFT_eSPI note above).

The simulator takes flags for the Granular Field work: `--field N` (layout), `--gran N`
(granularity), `--shadow N` (shadow amount), `--shmode N` (shadow notes), `--grid 0|1`, `--demo N`.
Scripts are inline and semicolon-separated:

```sh
./.pio/build/sim/program --headless --field 1 --grid 1 \
  --script "wait 500; press 100 150; wait 200; release; wait 300; shot out.bmp; quit"
```

Script commands: `tap X Y`, `press X Y`, `release`, `drag X Y X Y`, `finger2 X Y`, `lift2`,
`glitch N`, `glitch2 N` (drop a contact for N panel reads), `wait MS`, `shot FILE.bmp`, `dump`,
`quit`.

## Flashing

**Easiest:** `FLASH-FM-BUDDY.command` (macOS, double-click) or `FLASH-FM-BUDDY.bat` (Windows). Each
one backs the board up, checks the backup is the right size, asks you to confirm, then writes. They
refuse to go on without a good backup.

```sh
chmod +x FLASH-FM-BUDDY.command     # once, if macOS says it cannot be opened
./FLASH-FM-BUDDY.command
```

**By hand**, after `pio run -e pcbfun-s3-28`:

```sh
./pcbfun_backup_mac.sh                                   # read the board back first
./pcbfun_flash_mac.sh .pio/build/pcbfun-s3-28/firmware.bin
```

This writes the app to **app0 at 0x10000** and clears **otadata at 0xe000**. The bootloader and the
settings partition are never written, so Wi-Fi, theme, MIDI setup and touch calibration survive.

If the board is not found: use a USB-C cable that carries **data**, or put the board in download mode
— Settings ▸ Flash mode ▸ Restart for flashing, or hold BOOT, tap RESET, release BOOT.

`FLASH_CHECKLIST.md` has the full procedure, the rollback steps, and what to check afterwards.

### Never flash without a backup

`pcbfun_backup_mac.sh` reads back the settings partition and the running app. Keep it until the new
firmware has proved itself. `recovery/` holds a factory image as a last resort — it writes from 0x0
and **erases your settings**, which is why the flash scripts refuse any file with `factory` in the
name.

## Layout

```
src/app/     the application: UI, MIDI, pages, Granular Field
src/s3/      the ESP32-S3 board: display, touch, USB, storage
src/sim/     the desktop simulator (SDL2)
test/        host tests, no hardware needed
tools/       build helpers, flashing, scale generation
docs/        Granular Field stage reports
```

### Granular Field

| File | What it is |
|---|---|
| `src/app/field.h` | the renderer: blooms, trails, readouts, the scale grid |
| `src/app/fieldtouch.h` | finger identity, the three-poll confirmation, promotion |
| `src/app/fieldnote.h` | pitch: scale quantisation, bend, the four granularities |
| `src/app/fieldmap.h` | CC scheduling: rate floors, priority, the 80 ms ramp |
| `src/app/fieldshadow.h` | shadow granules: spawning, deadlines, modes, fade |

Each is plain logic — no LVGL, no MIDI, no board — so every one is tested on the host.

## Licence

**GPL-3.0-only**, per `LICENSE` and `LICENSING.md` in your repo. Forked from konsumer/fm1buddy.
The Granular Field files are marked MIT in the upstream convention; see the Licensing note above
about their missing copyright line.
