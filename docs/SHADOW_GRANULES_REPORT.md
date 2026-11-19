# Granular Field — Shadow Granular Layer

**Date:** 2026-11-19
**Status:** implemented, host- and simulator-verified, firmware builds. **Nothing flashed, no commits.**
**Stage 4 is untouched** — the bend engine, `fieldVoice[]` and the tracker are byte-for-byte as frozen.

---

## 1. One conflict in your decision table

You listed **Density → `FM_DENSITY` / CC 19**. CC 19 is Spray's default; `FM_DENSITY` is **CC 18**.
I drove `FM_DENSITY` on CC 18 and left Spray on 19. If you meant them merged onto one CC, say so —
the duplicate-CC machinery from Stage 2 already handles two parameters sharing a number.

`FM_DENSITY` now carries live granule count scaled to 0–127, so the parameter that had a settings
row and never transmitted finally sends something, without waiting on the panel's `area` sensor.

## 2. What was built

| File | State | What it is |
|---|---|---|
| `src/app/fieldshadow.h` | **new** | the scheduler: spawning, deadlines, concurrency, velocity, PRNG, freeze. Pure — no LVGL, MIDI, hal or scale |
| `test/fieldshadow_test.cpp` | **new** | 45 checks, the last section entirely about not stranding notes |
| `src/app/page_touch.cpp` | modified | trail capture, spawn, own channel, `FM_DENSITY`, cleanup paths, settings rows, `SETUP_V21` |
| `src/app/fm1.cpp` | modified | the unrecognised-device log now skips the touchscreen itself |
| `src/sim/main.cpp`, `ui_priv.h` | modified | `--shadow N`, simulator only |

Every invariant you named holds. Shadows never enter `fieldVoice[]`, never take pitch bend, never
appear in `fieldVoices()`, and so can never trip the Stage 4 bend suspension.

## 3. The decisions, as built

- **Channel.** `cfg.fieldShadowCh`, default 2. `shadowCh()` compares it against the lead's current
  channel every send and shifts up one if they collide — the separation holds whatever is
  configured, which is the validation you asked for.
- **Pitch.** Trail positions through `fieldPitch()`, the lead's own function. In-scale **by
  construction**, never snapped afterwards. Resolved at spawn, so changing scale, root or octave
  mid-gesture cannot leave a stale granule.
- **Rate.** `fsGap()` interpolates `FS_FAST_MS` (70) to `FS_IDLE_MS` (900) by finger speed, then
  stretches by up to 4× as the amount setting drops. A stationary finger is thinned, never stopped —
  your decision 1.
- **Velocity.** `FS_VEL_NEAR` 92 at the lead's note falling to `FS_VEL_FAR` 26 over `FS_FALL` 24
  semitones.
- **Free mode.** Suppressed by default; `Shadows in Free` turns them on. The row only appears when
  granularity is Free — your decision 2.
- **Concurrency.** `FS_MAX` 4. At the cap a granule is simply not played rather than stealing an
  existing one's note-off.
- **Collision with the lead's note.** Displaced up an octave, not dropped. Twelve semitones is the
  same pitch class, so it stays in key in any scale and the texture keeps its density.
- **Queue priority.** A granule spawns only when `fieldBudget() >= FM_PARAMS`. Under backpressure
  the texture thins while parameters keep flowing.
- **PRNG.** xorshift32, seeded per gesture from landing position and time.
- **Freeze.** `FS_PATTERN` 8 — the sequence returns to its seed every eight granules.

## 4. The safety property, and how it is held

> A granule's stop belongs to the granule.

`off` is set once at spawn and nothing may move it — not a finger, not the page, not the mode.
`fsTick()` runs **unconditionally** at the top of the field tick, before any check of whether a
finger is down. `fsIdle()` stops spawning but lets what is sounding finish. `fsAllOff()` is called
from `endAll()` and from the mode-leave path.

Four simulator scenarios, counting note-ons against note-offs:

| Scenario | Result |
|---|---|
| Thick texture, long moving gesture | 11 on / 11 off — **balanced** |
| Release mid-texture | 5 on / 5 off — balanced |
| **Leave the page 50 ms after release, granules still sounding** | 7 on / 7 off — balanced |
| Stationary finger held 2.5 s | 24 granule messages — thinned, not stopped |

Channel separation in every run: lead on ch1, granules on ch2, never mixed.

Scale check on a seven-position sweep: lead played {50, 55, 60, 62, 64, 69}, granules played
{50, 55, 62, 64, 69} — **every granule in C major**, none out.

Host tests verified by deliberate breakage, each reverted at once:

| Break | Caught |
|---|---|
| A later spawn moves an earlier granule's deadline (the classic stuck note) | 3 fail |
| `fsIdle` kills sounding granules instead of letting them finish | 3 fail |
| `fsAllOff` forgets to emit the note-offs | 2 fail |
| A still hand stops the texture entirely | 3 fail |
| Concurrency cap removed | 10 fail |

The long-run test plays 4,000 ticks of varying speed, then idles, and asserts **every note-on was
matched by a note-off** — plus a clock-wraparound case, since `off` is a `uint32_t` deadline.

## 5. Settings migration — `SETUP_V21`

Three bytes appended: `fieldShadow`, `fieldShadowCh`, `fieldShadowFree`. `SETUP_V21 = 112`, pinned
by the existing `static_assert`. `sizeof(TouchSetup)` 112 → 115.

Proven end to end, not just by inspection: a 112-byte V20 setup was loaded, the octave moved to
force a save, and the file came back **115 bytes with `fieldShadow=0, fieldShadowCh=2,
fieldShadowFree=0`** — shadows off by default, so an existing board gains nothing it did not ask
for. The Stage 3 migration fixture still passes with its control case.

## 6. Verification summary

| Evidence | Result |
|---|---|
| `tools/unit-test.sh` | **15 suites**, all passed (`fieldshadow` 45 checks) |
| `tools/field-migrate-test.sh` | passed, control case included |
| Stage 4 regression | 2 note events through dropouts, 4 across promotion — unchanged |
| `pio run -e sim` | SUCCESS |
| `pio run -e pcbfun-s3-28` | SUCCESS — Flash 34.8% (2,283,450) |

Cost against the pre-shadow build: **+1,640 bytes Flash**.

## 7. NOT TESTED

- **Anything on hardware.** No board has run this.
- **`Shadows in Free` set to On.** The branch is one condition (`!cfg.fieldShadowFree`) and is
  correct by inspection, but there is no simulator flag for it, so I have not exercised it.
- Whether `FS_DUR_MS` 180, `FS_FAST_MS` 70 and `FS_IDLE_MS` 900 feel musical. These are first
  guesses and the most likely things to want changing after you hear it.
- Whether four concurrent granules plus two voices is comfortable for your synth's polyphony.
- Real MIDI throughput with the texture running — the budget yield is tested in the simulator, where
  no device is attached and the queue never actually backs up.

## 8. Your sequence, against what was done

Stages 1–5 of your list are complete. Stage 6, hardware validation, is what the flash is for. The
shadow items fold into the physical session already outstanding for Stage 4: add polyphony under
texture, the channel separation on a real synth, and whether the thinned stationary rate sits right.

---

**Nothing has been written to any board. No commits.** Version still `dev`.
