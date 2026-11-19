# Granular Field — a gesture surface for granular synthesis

A sixth touch mode beside X/Y, Pads, Drums, Chords and Touche: the whole surface as a continuous
parameter field driving a granular engine, while still playing notes from the active scale.

Written against the current FM Buddy working tree. **Nothing here is built yet, and nothing has been
tested on hardware.** Every hardware item is marked **NOT TESTED**.

---

## 1. What already exists to build on

Rather more than I expected. None of this needs writing:

| piece | where | what it gives the field |
|---|---|---|
| scale quantization over two octaves | `noteAt(x)`, `SCALE_LIB` | X → a scale note |
| 1,046 scales | `scales_omnibus.h` | the scale picker, already in the Pads page |
| pitch glide | the `glide` struct, `GLIDE_MS[]` 40/80/150/300 ms | sliding bends the held note to its target, legato past the range |
| bend range ±1..±48, announced by RPN 0 | `BEND_RANGE[]`, `bendRange()` | how far a slide can glide before re-triggering |
| per-finger MPE channels | `cfg.padMpe`, `chOverride` | two fingers bending independently |
| velocity from touch area | `VEL_FIXED` / `VEL_WHERE` / `VEL_SIZE` | press-size → velocity |
| jitter smoothing | `tr.fx += 0.6f * (target - tr.fx)` in `toucheTick` | a one-pole filter already tuned for this panel |
| multi-CC sending with change detection and duplicate merging | `toucheSend`, `ccSent[]` | the exact pattern a 6-CC field needs |
| versioned config growth | `SETUP_V10`..`SETUP_V18` + `migrate()` | adding settings without breaking saved setups |

The Touche mode is the closest relative: four CC axes from one gesture, smoothed, throttled by value
change. The granular field is that idea with more axes and a note layer under it.

## 2. What the panel can actually give

An **FT6336, two fingers** (`#define FINGERS 2`), polled every 10 ms (`POLL_MS`). `Finger` carries
`x, y, id, weight, area`.

Usable continuous dimensions, honestly rated:

| dimension | source | reliability |
|---|---|---|
| X | absolute | solid |
| Y | absolute | solid |
| touch area / weight | the panel's own report | **unverified** — the code's own comment says "not every panel says". Settings → Touch test reads the chip's registers; check there first |
| movement speed | derived from successive positions | solid once smoothed; raw is noisy |
| two-finger span (pinch) | distance between fingers | solid when both are seen, but see below |
| time held | the clock | free and exact |

Two warnings the existing code already carries, both of which bite this mode harder than the others:

- **Two fingers is the ceiling.** Not a driver limit — the panel.
- **The panel can lose one of two fingers when they are one directly above the other.** For a mode
  built on free sliding, a pinch that drifts into vertical alignment can drop to one finger
  mid-gesture. The field must degrade gracefully rather than jump.

Three fingers, finger angle at close spacing, and independent per-finger velocity while two are down
are all out. Any design that needs them is not buildable on this hardware.

## 3. Mapping proposal

Six assignable destinations, each defaulting to a sensible granular parameter and each reassignable
like `toucheCc[]` is today:

| gesture | default parameter | why this pairing |
|---|---|---|
| **X** | grain position in the buffer | left-to-right is scrubbing; it also reads as pitch, which is why the note layer shares it |
| **Y** | grain size | vertical as "zoom" on the grain is the intuition most granular UIs use |
| **touch area** | density, grains/sec | a flatter, heavier press thickens the cloud |
| **speed of movement** | spray / jitter | fast gestures scatter, slow ones stay tight — the gesture *is* the randomness |
| **two-finger span** | stereo spread | pinch to widen; collapses to mono when the second finger is lost |
| **time held** | wet / feedback ramp | holding builds the cloud instead of needing a pedal |

Each with a curve (the Touche page already offers linear / soft / firm) and an invert flag, because
"grain size" feels right in one direction and "density" in the other.

## 4. The note layer

The user's original ask — it still plays, and still obeys the scale. X does double duty: grain
position *and* pitch. How tightly pitch is quantized is a setting, four positions:

- **Steps** — hard quantize, no bend between notes. What X/Y does today.
- **Snap** — quantize to the nearest scale note but *glide* to it, so a slide is a portamento run.
- **Lean** — quantize, and your offset from the note's centre bends it slightly: push a note sharp or
  flat by where you rest, without leaving the scale.
- **Free** — no quantization. A ribbon; the scale draws guide lines you can see but not feel.

Snap and Lean reuse `glide` unchanged. Free needs continuous bend from the note at the gesture's
start, re-centring on release — the `glide.center` mechanism already does exactly that.

A **Notes off** switch matters here: driving a granular patch from a sustained drone, with the field
doing all the work and no new note-ons at all, is a legitimate way to play this.

## 5. MPE, switchable

Per the decision: build channel-agnostic with an MPE toggle in the mode's settings, as Pads has.

- **MPE on** — each finger gets its own channel (`chOverride` already does this). Independent bend
  per finger, Y as CC 74 by convention, area as channel pressure.
- **MPE off** — one channel. The dominant finger owns pitch bend, exactly as Pads behaves now; the
  second finger still contributes to span and to the parameter field. Everything still works, just
  monophonically for pitch.

Default off. It is the case that works with every synth, and the one that can't silently misbehave.

## 6. The real risk: MIDI bandwidth

This is the part most likely to spoil the feel, and it deserves stating plainly.

Six continuous parameters at a 100 Hz poll is up to 1,800 bytes a second. **DIN MIDI carries about
1,000.** USB and BLE are far faster and will cope; a DIN output will not, and the symptom is not a
dropped message — it is latency building up in a queue until the gesture feels like it is lagging
behind the finger.

Three mitigations, all cheap:

1. **Change detection on the 7-bit value**, which `toucheSend` already does. A parameter only sends
   when it crosses a 1/128 boundary, so slow gestures are nearly free. This alone handles most
   playing.
2. **A per-parameter minimum interval** — a floor of, say, 10 ms per CC number, so a fast full-range
   sweep is capped rather than bursting.
3. **A total budget per destination.** When DIN is among the outputs, cap the field's aggregate rate
   and drop the least-changed parameters first, so pitch and grain position stay smooth while spray
   coarsens.

7-bit CCs give 128 steps, which is fine for grain size and density. If grain *position* needs finer
resolution, one parameter can go 14-bit (MSB/LSB pair) at double the bytes — worth offering for X
alone, not for all six.

## 7. Config growth

`TouchSetup` gains a block, with a new `SETUP_V19` marker and a `migrate()` clause, following the
established pattern exactly so saved setups survive:

```c
uint8_t fieldCc[6];     // the six destinations
uint8_t fieldCurve[6];  // linear / soft / firm, plus an invert bit
uint8_t fieldGran;      // Steps / Snap / Lean / Free
uint8_t fieldNotes;     // the note layer on or off
uint8_t fieldMpe;       // per-finger channels
uint8_t fieldScale, fieldScaleHi;  // its own scale, as the Pads have
```

Defaults: `fieldCc = {16, 17, 18, 19, 20, 21}` (the general-purpose controllers, which collide with
nothing), `fieldGran = Snap`, `fieldNotes = on`, `fieldMpe = off`.

## 8. Build order

Each step is useful and committable on its own, and every one is testable in the simulator before any
hardware is involved.

1. **The mode and its surface** — the mode in the dropdown, the field drawn, scale guide lines, a
   readout of the live parameter values. No MIDI yet.
2. **X and Y to CCs**, reusing `toucheSend`'s change detection. The field is already playable here.
3. **The note layer** with the granularity ladder, reusing `glide`.
4. **Area, speed, span, time held** as the remaining four destinations. Area gated on the Touch test
   confirming the panel reports it.
5. **Rate limiting** — the per-parameter floor and the per-destination budget.
6. **The MPE toggle.**
7. **Settings page** for the mode: the six destinations, curves, granularity, scale, switches.
8. **On hardware:** feel, latency, and whether area and span survive real playing.

## 9. Acceptance criteria

- **GF-1** With the mode selected and nothing touched, no MIDI is sent.
- **GF-2** A single touch sends each enabled parameter once, then only on change.
- **GF-3** Two parameters assigned the same CC number send once, the stronger winning —
  `toucheSend`'s existing rule.
- **GF-4** A full-width sweep in 200 ms sends no more than the rate limit allows, verified by counting
  messages in the simulator.
- **GF-5** In **Steps**, pitch matches `noteAt(x)` exactly — the same notes X/Y plays today.
- **GF-6** In **Snap**, sliding across three scale degrees produces one note-on and a bend that
  settles within `GLIDE_MS`.
- **GF-7** In **Free**, pitch is continuous and returns to centre on release.
- **GF-8** With **Notes off**, no note-on is ever sent; the parameter field still works.
- **GF-9** Losing the second finger mid-pinch holds the last span value rather than jumping to zero.
- **GF-10** With **MPE on**, two fingers bend independently on separate channels; with it off, the
  dominant finger owns bend and the other is silent for pitch.
- **GF-11** A setup saved before this mode existed loads with the defaults above and no other field
  changed.
- **GF-12** `tools/unit-test.sh` covers the mapping maths and the rate limiter as pure functions.
- **GF-13** *(hardware)* Area and span behave under real playing. **NOT TESTED.**
- **GF-14** *(hardware)* No perceptible lag with DIN among the outputs. **NOT TESTED.**

## 10. Decisions to make

1. **Which granular engine are you driving?** The six defaults above are generic. If it is a specific
   plugin or hardware unit, its own CC map should be the default, and the parameter names on screen
   should be its names rather than "grain size".
2. **Does the field replace X/Y or sit beside it?** Beside it is assumed here; X/Y is well used and
   people's saved CCs point at it.
3. **14-bit for grain position?** Finer scrubbing, double the bytes on an already tight DIN budget.
4. **Should "time held" exist at all?** It is the one dimension with no physical analogue — holding
   still and hearing something change can feel like a fault rather than a feature. Easy to include,
   easy to leave out.

## 11. Not established

- Whether the panel reports usable `area` / `weight`. **Check Settings → Touch test first** — four of
  the six destinations are fine without it, but density would need another source.
- The real-world cost of two fingers plus six parameters at 100 Hz on BLE. **NOT TESTED.**
- Whether `Free` is musically usable given the panel's jitter, even with the one-pole filter. The
  edge-parity audit found the outer 24 px rely on extrapolation beyond the calibration dots, so pitch
  near the left and right edges may wander. **NOT TESTED.**
- Nothing in this document has been compiled.
