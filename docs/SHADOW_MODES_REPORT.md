# Shadow granules — modes, minimal velocity, and the fade

**Date:** 2026-11-19
**Status:** implemented, simulator-verified, firmware builds. **Nothing flashed, no commits.**
Shadows remain **off by default**; the scale grid remains off by default.

---

## 1. Your three pitch ideas are one question, so they became a mode

Arpeggiator, random-relative-to-the-trigger, and a fifth are all "where does the granule's note come
from". Rather than pick one, **Shadow notes** is now a setting with four values:

| Mode | What it plays | Measured |
|---|---|---|
| **Trail** (default) | where the finger has just been | as before |
| **Arpeggio** | climbs 5 scale degrees from the lead, then starts over | from a lead on E: +1, +3 semitones — the scale's own steps, so the interval depends where you are |
| **Random** | a scale degree either side of the lead, ±4 | varies, always within span |
| **Fifth + octave** | alternates the fifth and the octave | exactly **+7** and **+12** semitones |

All four go through the scale table. Verified across every mode on a five-position sweep: **no
granule out of C major in any of them.** The arpeggio never lands on the lead's own rung.

A note on "fifth harmonic": the third harmonic is an octave *and* a fifth (+19). I built the plain
perfect fifth (+7) alternating with the octave (+12), because that sits under the lead rather than
above the next octave. If you meant the overtone, say and it's one constant.

## 2. Minimal velocity

`FS_VEL_NEAR` **92 → 44**, `FS_VEL_FAR` **26 → 12**. Against a lead at velocity 100 the nearest
granule now sits well under it, and distant ones are barely there. A test asserts `FS_VEL_NEAR < 64`
so this cannot quietly creep back up.

## 3. The fade

**Shadow fade: Never / Slow / Medium / Fast** — 4000 / 2000 / 900 ms, default **Medium**.

It is keyed to *movement*, not to the gesture's age: the texture is full while the hand is moving,
and dies away once it stops. Moving again brings it straight back. Measured over a 3-second still
hold:

```
velocities: 28  26  22  19  16  13  9  6  3  1  →  then nothing
```

Once faded to nothing it spawns **nothing at all** rather than going quiet — a tail that ends,
rather than a drone at low volume. Granule velocity never reaches zero, since velocity 0 is a
note-off in MIDI and would strand the note; the floor is 1.

This stacks with the rate thinning already there, so a still hand gets fewer *and* quieter granules.

## 4. Nothing else changed

| | Result |
|---|---|
| Note balance, all four modes | 5 on / 5 off each — **nothing stranded** |
| Stage 4 dropout | 2 note events — unchanged |
| Scale grid | unchanged, still off by default |
| `tools/unit-test.sh` | 15 suites, all passed (`fieldshadow` now **66 checks**) |

The note-off guarantees from the first build are untouched: deadlines are still booked at spawn,
`fsTick()` still runs unconditionally, `fsAllOff()` still sweeps on mode or page leave.

## 5. Settings and migration

Two rows added under Shadow granules: **Shadow notes** and **Shadow fade**, both only shown when
shadows are on.

`SETUP_V23 = 116`, two bytes. `sizeof(TouchSetup)` 116 → 118. Defaults: Trail, Medium fade — so a
board that had shadows off sees nothing new, and one that had them on keeps the behaviour it had
apart from gaining the fade.

## 6. Builds

| | |
|---|---|
| `pio run -e sim` | SUCCESS |
| `pio run -e pcbfun-s3-28` | SUCCESS — RAM 43.6% (142,820), Flash 34.9% (2,285,046) |

**+724 bytes** against the grid build.

## 7. NOT TESTED

- **Anything on hardware.**
- Whether velocity 44 is actually "minimal" against your synth's response curve — it is two
  constants.
- `FS_ARP_SPAN` 5 and `FS_RAND_SPAN` 4. Both are guesses at what sounds right; the arpeggio's reach
  and the random spread are the most likely things to want moving once you hear them.
- Whether Medium (2 s) is the right default fade.
- The three degree modes against non-diatonic scales on hardware. The degree arithmetic is checked
  in the simulator against C major only; the code walks the scale's own table, so pentatonic and the
  omnibus scales should follow, but I have not swept them.

---

**Nothing has been written to any board. No commits.** Version still `dev`.
