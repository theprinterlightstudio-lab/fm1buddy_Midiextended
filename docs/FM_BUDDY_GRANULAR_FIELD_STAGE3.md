# Granular Field — Stage 3: the musical pitch layer

**Current as of the closeout.** This supersedes the earlier Stage 3 report, which was written before
the two closing decisions and is stale on both of them: it described FREE as following the scale's
spacing, and it listed GF-12 as pending. Neither is true now. If you are holding a copy that says
either, discard it.

Stage 3 only — **no MPE, no density, spray, spread or wet.** Stage 4 has not been started.
**Nothing has been flashed. Nothing has run on the board.**

---

## 1. What a reader needs to know first

| | |
|---|---|
| the four granularity modes | implemented, tested, verified on real MIDI output |
| FREE's pitch | **equal tempered**, independent of the selected scale |
| notes on/off | implemented; CCs unaffected either way |
| register | the bar's existing octave swipe, writing the field's **own** root |
| `SETUP_V20` migration | **fixture written and executed here — passing.** See §6 for exactly what that does and does not mean |
| the renderer | `field.h` byte-identical to the approved Stage 1 Option D |

## 2. The pitch layer

`src/app/fieldnote.h` — plain logic: no LVGL, no MIDI, no hal, no board. It is told where the finger
is in musical terms and answers with events (note on, note off, bend now, bend by gliding); the page
performs those with the same `noteOn`, `noteOff`, `bend` and `glide` every other mode uses.

**The rule everything serves:** a finger that is down has exactly one note sounding, and when it
lifts that note stops.

### Where the finger is, musically — `fnPitchAt()`

Three numbers come out of one x:

| | follows the scale? | |
|---|---|---|
| `q` | **yes** | the scale note of the step the finger is in |
| `lean` | **yes** | where in that step it sits: −128 at the bottom, 0 in the middle, 127 at the top |
| `p256` | **no** | the pitch there in 1/256 of a semitone, running evenly from the root across `OCTAVES` octaves |

That split is the point of the decision taken at closeout. STEPS, SNAP and LEAN stay scale-aware
because they *are* the scale. FREE is a predictable continuous surface: the same distance moved is
the same pitch moved, whatever scale is selected. Both ends still meet the quantized modes exactly;
only the middle differs.

The arithmetic lives in `fieldnote.h` so it can be driven on the computer. `page_touch.cpp` only
resolves the scale, which is the part that genuinely belongs to it.

### The four modes

**STEPS** — the note is the scale note; nothing bends. A new step starts the new note and *then*
stops the old one, so a synth set to glide glides. That ordering is the existing Pads behaviour.

**SNAP** — one note is held and the pitch slides to each new scale note through the page's own
`glide`, at `GLIDE_MS`. Past the bend range it takes the new note itself, from no bend.

**LEAN** — quantized, and where in the step the finger sits bends it, up to `cfg.fieldLean` cents
either way (default 50). Leaving the step is a new note with the lean measured afresh.

**FREE** — the pitch follows the finger with no step anywhere. Past the bend range it moves to a new
note and carries on, so the range never clips.

### Notes on and off

`cfg.fieldNotes`. Switched off, nothing sounds — and a note already sounding when it is switched off
is stopped rather than left hanging. The CCs carry on untouched either way.

### Register

The bar's existing octave swipe. `octave()` already calls `endAll()` first, so nothing is left
sounding across the change.

The field has its **own** root, `cfg.fieldRoot`. Sharing `cfg.root` with X/Y would have meant moving
the field's register silently moved X/Y's.

## 3. Configuration

The `SETUP_V20` block, appended after everything older, as the struct's own rule requires:

```c
uint8_t fieldLayout;      // Minimal, Labelled
uint8_t fieldCc[6];       // 16..21: the general purpose controllers, so MIDI Learn has a start
uint8_t fieldCurve[6];
uint8_t fieldGran;        // Snap
uint8_t fieldNotes;       // on
uint8_t fieldMpe;         // off (Stage 5)
uint8_t fieldScale, fieldScaleHi;
uint8_t fieldHold;        // off (Stage 4)
uint8_t fieldRoot;        // 48
uint8_t fieldLean;        // 50 cents
```

`SETUP_V20` is at offset 91, pinned by the `static_assert` in `page_touch.cpp`.

## 4. Tests — what ran, and what it found

### `tools/unit-test.sh` — 13 host tests, all passing

```
midimap ok · sloopfmt ok · mirror ok · sha256 ok · midictl ok · favorites ok ·
fieldnote ok · fieldmap ok · seq ok · seqstore · song ok · clock_sync ok · clock_harness ok
```

`fieldnote_test.cpp` covers: the pitch mapping (both ends meeting the quantized range on three
scales; identical pitch between scales at all 320 x positions; equal x steps giving equal pitch
steps; the quantized note still disagreeing between scales at more than half the surface; `lean`
across a step boundary); bend arithmetic at its limits; landing and lifting; each of the four modes;
notes off, including switched off under a playing finger; **all sixteen granularity transitions
under a held finger**; the register moving under a held finger; a long slide in every mode leaving
nothing sounding; and that nothing is written past the room it is given.

`fieldmap_test.cpp` carries the Stage 2 engine and its duplicate-CC correction.

### The tests were checked by breaking the code

| removed | tests that failed |
|---|---|
| the guard that stops STEPS retriggering on every move | 5 |
| the note-off when a finger lifts | 22 |
| (Stage 2) change detection | 4 |
| (Stage 2) the per-parameter rate floor | 2 |
| (Stage 2) the priority order, reversed | 5 |
| (Stage 2 fix) the live-owner preference | 2 |

Every one reverted immediately afterwards.

## 5. Evidence from real MIDI output

Through the simulator's real touch path with `--fm1`, so the pretend FM-1 logs what it receives.

**The bend range is announced** through the existing `sendBendRange()` — RPN 0, two semitones, RPN
then closed. STEPS never bends, so it does not announce.

**The same small slide in each mode:**

| mode | note-ons | note-offs | bends |
|---|---|---|---|
| STEPS | 2 | 2 | **0** |
| SNAP | **1** | **1** | 3 |
| FREE | 2 | 2 | **7** |

**Ordering, on a wider slide:**

```
90 32 64   note 50 on
90 3B 64   note 59 on     <- the new one first
80 32 00   note 50 off    <- then the old one
90 45 64   note 69 on
80 3B 00   note 59 off
80 45 00   note 69 off    <- the finger lifts; nothing left sounding
```

**Notes OFF**, same gesture: 0 note-ons, 0 note-offs, 0 bends, **4 CC16 and 4 CC17**.

**FREE is scale-independent** — the same slide under scale 1 and scale 11:

```
E0 10 49 / E0 30 5C / E0 50 6F / E0 70 42      (both scales, byte-identical)
```

## 6. SETUP_V20 migration — GF-12

**Status: fixture written, and executed in the development container, passing.** It has not been run
on your machine, and nothing has been flashed, so this is evidence from one environment rather than
independent confirmation. The script is in the package; running it is one command.

`tools/field-migrate-test.sh`. It cannot be a host test — `TouchSetup` and `migrate()` live in
`page_touch.cpp`, which needs LVGL — so it drives the simulator and judges by what actually leaves
the MIDI port.

It writes a setup of exactly `SETUP_V20` bytes (91: everything up to Granular Field and nothing of
it, as a setup saved before the field existed), runs the field, and checks the output. The run:

```
--- a setup saved before Granular Field (91 bytes, SETUP_V20)
B5 65 00
B5 64 00
B5 06 02
B5 26 00
B5 65 7F
B5 64 7F
95 3C 64
B5 10 3B
  channel 5 survived, CC 16/17 defaulted, root defaulted, note released

--- the control: the same setup with the field's bytes left at zero
B5 00 3B
  zeroed bytes send CC 0, so the checks above are real

--- SETUP_V20 migration: ok
```

What each line proves:

- **`B5` throughout** — channel 5, which was in the old file at offset 2 and survived untouched.
- **CC 16 and CC 17** — the field's CC defaults were filled for bytes the file never had.
- **note `3C` = 60** — the field's root defaulted to 48. The old file sets *X/Y's* root to 60, and
  the field did not take it: they are separate, as intended.
- **`85 3C`** — the note was released.
- **the control** — the same setup at full size with the field's bytes zeroed sends `B5 00`, CC 0,
  not CC 16. The script **fails** if the control does not behave that way, so the checks above cannot
  pass by accident.

The 91 is pinned by the `static_assert`. If the struct shifts, this stops matching and says so rather
than passing quietly.

## 7. Builds

| target | result |
|---|---|
| `pio run -e sim` | SUCCESS |
| `pio run -e pcbfun-s3-28` | SUCCESS — RAM 43.5% (142,500 of 327,680), Flash 34.7% (2,275,978 of 6,553,600) |

## 8. Option D preserved

`src/app/field.h` is **byte-for-byte identical** to the approved Stage 1 file. Idle and two-finger
captures are pixel-identical to Stage 2's. The fade capture differs by about two fade steps —
releasing a finger now also sends a note-off and arms the bend's return to centre, so the capture at
the same `wait` lands slightly later in the fade. Layout, text, positions and colours are identical.

## 9. Open

- **Granularity from the performance surface.** Deferred by agreement, to be designed alongside
  Stage 4's second-finger gestures. The surface currently has no non-musical gesture at all, so
  whichever is chosen will set the pattern.
- **Hardware validation.** Nothing flashed. Feel, latency, whether `GLIDE_MS` suits this gesture,
  whether 50 cents is the right default lean — all unknown.
- **The second finger** contributes nothing musically yet. Stages 4 and 5.
- **The migration fixture has not been run outside the development container.**
