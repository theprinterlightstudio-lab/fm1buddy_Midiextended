# Granular Field — Stage 0: repository and architecture audit

No code has been changed. This is the audit the brief asks for before any implementation.

**What this was audited against.** The FM Buddy working tree in this session, which carries the S7
sequencer work plus the uncommitted edge-parity changes. It is the same source as the repo, but it is
**not a git checkout** — there is no `.git` here.

---

## 1. Branch and working tree — CANNOT CONFIRM

The brief asks me to confirm the branch and working tree first. **I can't.** The repo lives on the PC
at `D:\M-UPGRADE\FM1 Buddy\fm1buddy-main\fm1buddy-main`, and the device bridge reports that computer
is not connected. So:

- I cannot read `git status`, confirm `feature/fm-buddy-2.0-control-surface` is checked out, or see
  whether the tree is clean.
- I cannot commit. The brief's git requirements (§17) cannot be met from here at all.
- I cannot check whether another agent has changed these files since this tree was taken.

Everything below is therefore an audit of the **source**, which is sound, and not of the **repo
state**, which is unknown. Any code I write has to arrive as files or a patch for you to apply and
commit on the PC, exactly as the edge-parity work did.

One consequence worth stating now: the brief says "if the working tree contains uncommitted changes,
do not overwrite or discard them." I can honour that only by handing you a scoped patch and having
you read the diff before committing. I cannot verify it myself.

## 2. Touch-mode registration

There is no registration table — modes are an enum and a set of branches.

```c
enum { XY, PAD, DRUM, CHORD, TOUCHE, MODES };   // page_touch.cpp:144
```

`MODES` is both the count and a sentinel: the mode dropdown appends two non-mode entries after it
(`MODES` → the sequencer page, `MODES + 1` → the MIDI page), handled in `pickMode` around
`page_touch.cpp:2289`. Adding `FIELD` before `MODES` shifts nothing stored, because `cfg.mode` is
saved as a small integer and the existing five keep their values — but it does change what `MODES`
and `MODES + 1` mean, so **`pickMode` and the dropdown string must be updated together or the
sequencer and MIDI entries will be off by one.** That is the first regression to guard.

Places that must learn about a sixth mode:

| what | where |
|---|---|
| the enum | `page_touch.cpp:144` |
| the dropdown string | `"X/Y\nPads\nDrums\nChords\nTouche\nSequencer\nMIDI page"` (built in the bar) |
| `pickMode` | `≈2289` — the `m == MODES` / `m == MODES + 1` tests |
| the settings page title table | `TITLE[MODES]` at `≈3049`, five strings today |
| the settings page body | `touchSetupPage` at `≈3045` |
| the bar's label and its swipes | `≈2065`–`2090`, `≈2429`–`2460` |
| the surface builder | mode-specific build paths off `surfaceH()` |

**45 separate `cfg.mode` branches exist in `page_touch.cpp`.** Most are `== SOMETHING`, so a new mode
simply doesn't match and falls through safely. The dangerous ones are the negative tests, which a new
mode silently *enters*:

- `page_touch.cpp:1976` — `if (cfg.mode != XY) { return; }` in the move handler
- `:1115` and `:1130` — `if (cfg.mode != DRUM || !cfg.drumOne)`
- `:603` — `cfg.mode == PAD && ...` (safe, but it gates vibrato globally)

Every one of the 45 needs reading before Stage 1 ships. That is the single largest regression surface
in this feature and it is unavoidable.

## 3. Rendering architecture — the biggest finding

**LVGL 9.2.2 with retained objects. There is no `lv_canvas` anywhere in the project, and no per-frame
pixel buffer.** Pages build a tree of objects once and then move or restyle them; LVGL redraws only
the dirty rectangles.

What exists that is relevant to an "organic sea / cosmic space" surface:

- **Gradients**: `lv_obj_set_style_bg_grad_color` / `_dir` are already used (the Touche plate,
  `:1767`). Free, drawn by LVGL.
- **A raw pixel path**: `page_mirror.cpp` builds an `lv_image_dsc_t` over its own `uint16_t` buffer
  and lets LVGL blit it. That is the precedent for per-pixel work, and it proves the approach is
  available.
- **PSRAM**: LVGL's own pool is allocated from SPIRAM (`lv_conf.h:12`,
  `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`), and `hal::psramFirst(bool)` steers other allocations.
  So a 320 × 240 RGB565 buffer — 153,600 bytes — *fits*.
- **Arcs**: `lv_arc` is used for the knob meters in `page_controls.cpp`.
- **A touch marker already moves per finger**: `dots[FINGERS]` in X/Y mode, repositioned with
  `lv_obj_set_pos` at `:2003`. This is exactly the bloom mechanism, already written.

**My recommendation, and it differs from the obvious reading of the brief.** Do *not* animate a
full-screen particle buffer. It fits in PSRAM but it is the wrong trade: PSRAM bandwidth is a
fraction of internal SRAM, 76,800 pixels would be rewritten per frame, and the result would then be
pushed over SPI to the panel. That is precisely the "expensive rendering that interferes with MIDI
timing" the brief forbids, and the sequencer and MIDI clock share this chip.

Instead, get the same look with per-frame cost proportional to the fingers rather than the screen:

1. **The atmosphere is static.** Render the nebula once on page entry — a gradient object, or one
   pre-computed `lv_image_dsc_t` built at entry and never touched again. Zero per-frame cost.
2. **Each finger gets a small stack of objects** — a bright core, a soft halo, both with radius and
   opacity — moved with `lv_obj_set_pos` as `dots[]` already is. Two or three objects per finger.
3. **Trails are a ring of ~8 small objects per finger** with decreasing opacity, recycled. LVGL
   dirties only those rectangles.
4. **Release fade is an opacity animation** on those objects, not a redraw of the field.

This keeps the immersive look, costs almost nothing when nobody is touching, and cannot stall MIDI.
If after seeing it you want true particles, the `lv_image_dsc_t` route stays open as a later stage —
but it should be a decision made against a screenshot, not up front.

## 4. Touch input

```c
struct Finger { int16_t x, y; uint8_t id, weight, area; };   // hal.h:219
int fingers(Finger* out, int max);                            // hal.h:223
#define FINGERS 2     // page_touch.cpp:28  "the panel feels two"
#define POLL_MS 10    // page_touch.cpp:31
```

`tracks[FINGERS]` holds per-finger state including `float fx, fy` — already smoothed with a one-pole
filter, `tr.fx += 0.6f * (target - tr.fx)` (`toucheTick`, `:1688`), tuned for this panel's jitter.

**Confirmed limits, both already documented in the source:**

- Two fingers is the panel's ceiling, not the driver's.
- The panel can lose one of two fingers **when they are one directly above the other** — the code
  says so in several comments. The brief's hold-last-value requirement is therefore not defensive
  programming, it is required behaviour.

**Touch area is the one unknown that matters.** `VEL_SIZE`'s own comment reads "how much finger the
panel feels (**not every panel says**)". `hal.h:224`–`227` documents a Settings → Touch test page
that reads the FT6336's `TD_STATUS`, its two point records, and its chip/firmware/vendor/threshold
registers. **That test needs running on the board before density is wired to area.** Five of the six
parameters are unaffected.

## 5. MIDI engine and CC transmission

One engine, and nothing here needs a second. `page_touch.cpp` sends through its own small wrappers —
`cc(num, value)`, `noteOn`, `noteOff`, `bend(v)` (`:489`) — which go to `midi.cpp` (650 lines) and
out through routing to USB, BLE and DIN, with `/midimap.txt` rules applied on the way.

The pattern to copy for six parameters is `toucheSend` (`:1643`), which already does three of the
things the brief asks for:

- **change detection** — `ccSent[num]`, send only when the 7-bit value differs;
- **duplicate CC merging** — if two axes carry the same CC number, only the first sends, taking the
  strongest value, with deterministic tie-breaking;
- **a "nothing before the first touch" rule** — `(down || ccSent[num] >= 0)`.

`chOverride` (`:466`) is the per-finger channel used by Pads' MPE; `bendRange()` and the RPN 0
announcement already exist. No parallel implementation is needed anywhere.

## 6. Scale and pitch quantization

- `SCALE_LIB[]` (`:161`) — the common scales, with the first five frozen so a saved X/Y pick keeps
  its meaning.
- `scales_omnibus.h` — 1,046 lines, the Scale Omnibus, grouped; `PAD_SCALES = SCALES + OMNI_COUNT`.
- `noteAt(x)` (`:738`) — x → a note across `OCTAVES` (2) of the scale plus the root on top.
- `glide` (`:554`) — `{track, from, to, at, center}`, interpolated in `glideTick`, with
  `GLIDE_MS[] = {40, 80, 150, 300}` and `glide.center` already handling "bend back to none after the
  note has faded".
- `BEND_RANGE[] = {2, 12, 24, 48, 1, 5}`.

**Steps** is `noteAt` unchanged. **Snap** is `noteAt` plus `glide`, unchanged. **Lean** needs one new
calculation: the offset within a step scaled to a fraction of the bend range. **Free** needs
continuous bend from the gesture's starting note, and `glide.center` already provides the
re-centring. So the pitch layer is mostly wiring, not new machinery.

## 7. Configuration and migration

`TouchSetup` is a flat struct saved as bytes, grown by appending and gated by `offsetof` markers:

```c
#define SETUP_V10 offsetof(TouchSetup, padScale)
...
#define SETUP_V18 offsetof(TouchSetup, touchePbLock)
```

`migrate(from)` fills anything the saved file didn't reach — `if (from <= offsetof(TouchSetup, x))
{ cfg.x = default; }`. The struct's own comment states the rule plainly: new fields go **after
everything older, so a saved setup's bytes stay where they were.**

`SETUP_V19` and the seven new fields follow that pattern exactly. The migration is mechanical; what
needs testing is that a pre-V19 file loads with the new defaults and **no other field shifted**,
which is acceptance test GF-12.

Save is `saveCfg()` at `:326`.

## 8. Simulator — verified working here

`pio run -e sim` **succeeds in this container**, 28 seconds. I ran it and captured a screenshot to
confirm the pipeline rather than assume it: a 320 × 240 BMP was written.

`src/sim/main.cpp` supports, per its own header comment and the parser at `:373`:

```
tap X Y, press X Y, release, drag, finger2 X Y, lift2, wait MS, shot FILE.bmp, quit
```

**`finger2` and `lift2` exist**, so genuine two-finger screenshots are possible — not mocked, driven
through the same touch path the hardware uses. `sim/start.bin` selects the start page and
`sim/touch.bin` seeds the touch setup, so the Granular Field screen can be opened directly with a
chosen configuration.

Conclusion: **all five screens the brief requires (idle, one finger, two fingers, release, fade) and
all three layout alternatives are producible here, as real renders.** No limitation to report.

---

## 9. Exact files and functions to modify

| file | change | size |
|---|---|---|
| `src/app/field.h` | **new.** The whole mode: mapping maths, the four granularity modes, the rate limiter, the bloom/trail rendering. Header-only static inline, following the project's own convention (`scales_omnibus.h`, `bigscreen.h`, `dial.h`-style). | ~500–700 lines |
| `src/app/page_touch.cpp` | the enum, the dropdown, `pickMode`, `TITLE[]`, `touchSetupPage`, the bar label and swipes, the surface builder, the move/end handlers, `TouchSetup` + `SETUP_V19` + `migrate` | ~150–250 lines changed |
| `src/sim/main.cpp` | nothing — `finger2`/`lift2`/`shot` already cover it | 0 |
| `test/` | a new host test for the mapping maths and the rate limiter as pure functions | ~200 lines |
| `tools/unit-test.sh` | one line to activate it | 1 |
| `docs/` | this audit, plus the implementation report | — |

**`page_touch.cpp` is 3,412 lines.** Putting 600+ more into it is not maintainable, which is why the
field's own logic belongs in `field.h`. It needs `tracks[]`, `cfg`, `glide`, `cc()`, `noteOn/Off` and
`bend()`, all of which are `static` in `page_touch.cpp` — a header included at the right point gets
all of them without exposing internals to the rest of the build. A separate `.cpp` would require
making those non-static, which is a larger and riskier change.

## 10. Risks and regressions

| risk | severity | mitigation |
|---|---|---|
| the `MODES` / `MODES + 1` sentinels shift and the Sequencer / MIDI dropdown entries go off by one | **high** — silently sends you to the wrong page | change the enum, the dropdown string and `pickMode` in one commit; a simulator script that opens each dropdown entry and shoots it |
| one of the 45 `cfg.mode` branches treats the new mode as an old one | **high** | read all 45 before Stage 1 ships; the `!=` tests first (`:1976`, `:1115`, `:1130`) |
| per-frame rendering stalls MIDI or the sequencer | **high** | §3: static atmosphere, objects per finger, no full-screen buffer. Measure with the sequencer running |
| six CCs saturate DIN MIDI and the surface feels laggy | **high** | change detection, a per-CC floor, an aggregate per-output budget with pitch and grain position prioritised |
| touch area turns out unreliable | medium | Settings → Touch test on the board first; five parameters don't need it |
| FREE pitch wanders at the left and right edges | medium | the edge-parity audit found the outer 24 px rely on extrapolation past the calibration dots; consider a small dead margin in FREE, or accept and document |
| `TouchSetup` grows and an older saved setup mis-parses | medium | the `offsetof` discipline, plus GF-12 |
| the field's objects leak across page changes | low | the page's children are deleted on change, not the page object — the `gone()` hook had to be moved to a child for exactly this reason earlier |

## 11. Two corrections to the brief

**The display is 320 × 240, landscape.** The brief says 240 × 320. `hal.h:13-16` defines
`SCREEN_W 320`, `SCREEN_H 240`, and both are *runtime* values on device (`hal::screenW/H`), because
the project also supports a 4" 480 × 320 and has `SCREEN_PORTRAIT` handling. So the field's layout
must be computed from `SCREEN_W` / `surfaceH()`, never from constants — which is how the other modes
already do it.

**Y cannot carry both grain size and register without a conflict.** The brief maps Y to grain size
*and* to musical register. Those fight: you can never play a high register with small grains, or a
low one with large grains, because one gesture sets both. Three ways out:

1. **Y is grain size only; register comes from the bar's existing octave swipe.** The bar already
   does this for X/Y and Pads (`:2441`). Nothing new, nothing visible, and it matches the brief's own
   principle that the musical mapping stays internal.
2. **Y is register, and grain size moves to touch area or speed.** Loses a parameter if the panel
   doesn't report area.
3. **Keep both and accept the coupling** as an instrument characteristic — defensible for a granular
   field, where big grains at the bottom and fine grains up high is arguably musical.

I would take 1, and I have not implemented any of them. **This is the one design decision Stage 1
cannot start without.**

## 12. What I recommend as the order from here

Stages 1–7 as the brief sets them, with one change: **the visual review moves to the front.** Stage 1
produces the surface and the three layout alternatives as real renders, and nothing else proceeds
until you have looked at them. That is cheap here — the simulator builds in 28 seconds — and it is
the decision most expensive to revisit later.

## 13. Not established

- The repo's branch, cleanliness, and whether these files have changed since this tree was taken.
  **Unknown — the PC is offline.**
- Whether the panel reports usable touch `area`. **Needs Settings → Touch test on the board.**
- Any rendering or MIDI timing figure. Nothing has been measured, on hardware or in the simulator.
- The granular engine's CC map. Defaulting to CC 16–21 with a named preset table, per your decision.
- Nothing in this document has been compiled, and no file in the tree has been modified.
