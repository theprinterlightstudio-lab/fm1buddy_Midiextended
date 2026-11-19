# Granular Field — the scale grid

**Date:** 2026-11-19
**Status:** implemented, simulator-verified, firmware builds. **Nothing flashed, no commits.**
**Off by default.** With the grid off, everything behaves exactly as it did — verified, not assumed.

---

## 1. Columns: your 7×7 was right, and I made it scale-derived

Seven columns is exactly *one degree per note* for a seven-note scale — root through the 7th, with
no column wasted on the repeated octave. So rather than hard-coding 7, the columns come from the
scale itself:

| Scale | Columns |
|---|---|
| Major, minor, the modes | **7** |
| Major / minor pentatonic | 5 |
| Blues | 6 |
| Chromatic | 12 |

You get 7×7 in the scales you're most likely in, and the grid stays honest everywhere else — every
line is a real note boundary, so what you see is what you hear.

**Rows: 7, fixed.** You asked what I'd recommend and this is it — matching the columns keeps the
square 7×7 you asked for, and seven grain sizes is plenty to play with. Grain size snaps to
**0, 21, 42, 63, 84, 105, 127**, measured in the simulator. Rows aren't scale-derived because grain
size has no natural division.

## 2. The two decisions you skipped

**Rows snap grain size** rather than being decoration. A grid you can see but can't feel would be
misleading, and you confirmed mid-build that rows should be incremental.

**Granularity still shapes pitch; the grid only decides position.** Grid + Steps = every cell its
own note. Grid + Snap = lands on the cell then slides. The exception is Free, which ignores the
scale entirely and so contradicts a grid drawn from it — while the grid is on, Free behaves as Snap.

### Something the testing turned up, and you should know

With the grid on and the **default Snap** granularity, a sweep across all seven columns produced
only **four** note-ons, not seven. That is Snap working correctly, not a bug: the default bend range
is 2 semitones, which covers one scale step, so adjacent cells glide into each other instead of
retriggering. Only larger jumps force a new note.

Measured:

| | Distinct notes across the surface |
|---|---|
| Grid on, **Steps** | **7** — one per column, the true pad-grid feel |
| Grid on, Snap (default) | 4 — adjacent cells glide |
| Grid off, Steps | 15 — two octaves, **unchanged** |

If the grid should always mean "one cell, one note", then it should force Steps and I'll make that
one-line change. I left them independent because that was my stated assumption and it keeps both
behaviours reachable — but **Snap is the default, so out of the box the grid will glide rather than
step.** This is the decision most worth your attention.

## 3. The range cost

X used to spread two octaves across the surface — 15 note positions, about 21 px each. Seven columns
is one octave of degrees at about 45 px each: finger-sized, and the reason a grid is playable at all.
The bar's octave arrows reach the rest. This only applies while the grid is on.

## 4. What it looks like

Faint lines at `#6E8FB8`, opacity 46/255, drawn straight after the background and before every glow,
so a finger's bloom always passes over them. The cosmic field is still readable through it.

The readouts now step with the gesture too. They were sliding smoothly while the MIDI jumped, which
was telling you something that wasn't happening — `fieldPos` and `fieldSize` snap when the grid is
on. They remain computed independently of the MIDI values, as agreed at Stage 1.

## 5. Nothing existing changed

This is the part that was tested hardest, since you asked for the working feature to be retained:

| With the grid **off** | Result |
|---|---|
| Notes across the surface | 15 over two octaves — unchanged |
| Grain size | continuous, 16 distinct values in a vertical sweep |
| Stage 4 dropout | 2 note events — unchanged |
| Shadow granules | 5 on / 5 off, balanced — unchanged |
| Readouts | continuous, as before |

`tools/unit-test.sh`: 15 suites, all passed. `tools/field-migrate-test.sh`: passed with its control.

## 6. Settings and migration

One row, **Scale grid: Off / On**, in the Field settings above Readouts.

`SETUP_V22 = 115`, one byte (`fieldGrid`), pinned by the existing `static_assert`.
`sizeof(TouchSetup)` 115 → 116. Defaults to **0**, so an existing board gains nothing unasked. The
V20 and V21 migration paths are unchanged and still tested.

## 7. Builds

| | |
|---|---|
| `pio run -e sim` | SUCCESS |
| `pio run -e pcbfun-s3-28` | SUCCESS — Flash 34.9% (2,284,322) |

**+872 bytes** against the shadow build.

## 8. NOT TESTED

- **Anything on hardware.** No board has run this.
- Whether opacity 46/255 reads as "barely seen but still there" on real glass under real light. It
  is one constant (`FIELD_GRID_OPA`) if it needs moving.
- Whether 45 px cells are comfortable for your fingertips — that is the whole premise of the grid
  and only the panel can answer it.
- Non-seven-note scales on hardware. The column counts are derived and checked in the simulator, but
  I have only swept a major scale end to end.

---

**Nothing has been written to any board. No commits.** Version still `dev`.
