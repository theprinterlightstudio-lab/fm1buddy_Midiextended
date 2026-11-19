# Granular Field — Stage 4 Pre-Coding Audit and Strategy

**Date:** 2026-11-19
**Stage:** 4 (second finger as a voice) — §12 pre-coding audit
**Status:** audit complete. **No code written, nothing built, nothing committed, nothing flashed.**
**Baseline:** Stage 3 frozen tree

| File | Lines | SHA-256 |
|---|---|---|
| `src/app/field.h` | 322 | `ecb2e20d3ccecff6032a19eae72dec5cab910eb29b5333af092f39dc21a89ff2` |
| `src/app/fieldmap.h` | 163 | `76c2bcba17cedfaf2b08d458dc70e6d06ad7b27d5437f062d5ee299028304d26` |
| `src/app/fieldnote.h` | 216 | `9d810b85c6078b10fee5077cfe6c80d76b86639d527349c5a15c8e455cd04f0b` |

`field.h` is unchanged since your Stage 1 approval. 13 host tests pass. `SETUP_V20 = 91`.

---

## 1. Correction to the brief

The brief states the display is 240×320. It is **320×240 landscape** (`SCREEN_W 320`, `SCREEN_H 240`), and the dimensions are runtime-variable — the 4" board is 480×320. All Stage 4 geometry (spread normalisation in particular) will be derived from `SCREEN_W` and `surfaceH()`, never from constants.

## 2. Touch polling and finger identity (§12.2)

`page_touch.cpp`, poll loop from line ~2350, every `POLL_MS` (10 ms):

1. `hal::fingers(f, FINGERS)` — `FINGERS` is 2. Y is clamped to the surface bottom.
2. For each `tracks[s].on`, a linear search for `f[k].id == tracks[s].id`.
3. **No contact carries that id → `end(tracks[s], s)` immediately, on the first missing poll.**
4. `used[]` stops one contact driving two slots.
5. `picking` / `barFingers()` can return out of the whole poll — but *after* step 3, so it does not shield a track from release.
6. Unclaimed contacts take the first free slot: `start()` → set `px/py` → `move()`.

Step 3 is exactly the behaviour Stage 4 must replace for Granular Field, and must leave untouched for X/Y, Pads, Drums, Chords and Touche. Identity is the panel's contact id; there is no proximity fallback anywhere today.

## 3. Note assignment and release (§12.3)

- `start()` — `tr = {true, f.id, -1, -1, -1, -1}`, so `fieldOn` resets to false. Announces RPN 0 when `fieldNotes` is on and granularity is not `FN_STEPS`.
- `move()` FIELD block (line 2124) — `fieldTouch(slot, …)` runs for **every** finger, so both blooms already render. Then `owner` is computed as the lowest `on` slot, and **only the owner** writes `FM_POS`/`FM_SIZE` and plays notes, with `landed = !tr.fieldOn`.
- `end()` FIELD block (line 2179) — `fieldLift(slot, …)`, then `fieldQuiet()` if `tr.fieldOn`.
- `fieldTick()` (line 2409) — visual tick, `fmIdle()` when no finger is down, then `fieldSend()`.

**Two findings that shape the design:**

- **`fieldQuiet()` is global, not per-voice.** There is one `fieldNote` state; `fnUp` stops whatever is sounding and recentres the bend. A second voice therefore needs its own `FieldNote` instance, and promotion must never route through `fieldQuiet()`.
- **Pitch bend is one channel-wide resource.** `glide` is a single struct with a single `glide.track`, and MPE is deferred. Two simultaneous voices on one channel cannot hold independent bend — the primary's bend would detune the secondary.

### Decision taken on the bend conflict

The brief does not address this. I am following the precedent already in this codebase: Pads' `SLIDE_BEND` glides *only while one finger is down*. So:

> While a confirmed secondary voice is sounding, the primary's continuous bend is suspended and both voices play their quantised notes. On confirmed removal of the secondary, the primary re-acquires bend from its current position, with no retrigger.

With `FN_STEPS` granularity nothing changes at all, since STEPS never bends. This is one guarded branch and is trivially reversible. **Flagged for your review** — the alternative (secondary plays, bend stays live and audibly detunes it) is a one-line change if you prefer it.

## 4. FieldMap scheduling interface (§12.4)

`FieldMap { cc[6], curve[6], live[6], want[6], sent[6], at[6] }`, with `fmBegin`, `fmSet`, `fmIdle`, `fmShape`, `fmOwner`, `fmValueFor`, `fmCollect`.

`fmCollect` order: owner check → value (max among live sharers) → change detection → `FM_FLOOR_MS` rate floor → emit within budget in `FM_ORDER` → mark **all** sharers' `sent`/`at`. `sent[p]` is only written when a value is actually emitted — the Stage 2 property you asked to be tested, and the one the ramp depends on.

`sent[p]` is the correct ramp origin, **but** under a duplicate CC the non-owner's `sent[]` is stale. The ramp must read `sent[fmOwner(m, p)]`, not `sent[p]`. This was the Stage 2 caution and it is now load-bearing.

Ramp state: `rampFrom[6]` (int16) + `rampAt[6]` (uint32) = 36 bytes, **runtime only**. It is not performance configuration, so it does not enter `TouchSetup` and **`SETUP_V20` stays at 91** — no migration change in Stage 4.

## 5. Simulator contact model (§12.5)

`hal::fingers()` in `src/sim/main.cpp:153`:

```c
if (p.down && n < max)      out[n++] = {appX(p.x), appY(p.y), 0, 0, 0};
if (second.down && n < max) out[n++] = {appX(second.x), appY(second.y), 1, 0, 0};
```

- Primary is always id 0, secondary always id 1, **always in that order**.
- `weight` and `area` are always 0 — so the simulator cannot exercise density. That confirms, from the simulator side as well as the panel side, that density stays deferred to the physical Touch Test.
- **A dropout cannot be simulated at all**, and neither can an id reorder.

The brief requires single-frame and repeated short-dropout scenarios. I propose two simulator-only script commands, `glitch N` and `glitch2 N`, which suppress the respective contact for the next N calls of `hal::fingers()` — about eight lines in `src/sim/main.cpp`, with no firmware reachability. Id reorder is not reproducible in the simulator and will be covered by host tests only; the report will label it as such.

## 6. Proposed changes and test strategy (§12.6)

**New file — `src/app/fieldtouch.h`** (pure, LVGL-free, MIDI-free, host-testable, five-finger ready):

- `FtFinger { used, present, id, x, y, missing, downAt, px, py, pat }`, `FieldTouch { f[FT_MAX], slots }`, `FT_CONFIRM 3`.
- `ftPoll(t, pts, n, now, out, max)` emitting `FT_DOWN` / `FT_UP` / `FT_PROMOTE`.
- Matching: by id first; then an unmatched contact may re-claim a finger that is `used && !present` by nearest last position within a radius, which covers id reorder; then a free slot.
- Confirmation: `missing++` per absent poll; at `FT_CONFIRM` the removal is confirmed, `FT_UP` is emitted and the slot freed. Promotion picks the earliest remaining `downAt`.
- Derived geometry, all pure: `ftSpread` (hold-last-value, order-independent, never snaps to zero), `ftSpeed` (time-normalised), `ftHeld` (from `downAt`).

**`src/app/fieldmap.h`** — 80 ms time-based ramp from `sent[fmOwner()]` to the live target, inside the existing floor/priority/budget discipline, completing on time under congestion.

**`src/app/page_touch.cpp`** — the FIELD branch consumes `fieldtouch.h` instead of per-slot `end()`; a second `FieldNote` for the secondary voice; the bend suspension of §3; `cfg.mode == FIELD` guards everywhere, so the other five modes keep first-missing-frame release untouched. No refactor of their lifecycle.

**Tests** — `test/fieldtouch_test.cpp` (identity, single-frame dropout, repeated dropout, confirmed lift, id reorder, promotion with zero note events, spread hold, order independence) and ramp cases added to `test/fieldmap_test.cpp`. Every assertion verified by deliberately breaking the code and reverting, as in Stages 2 and 3. Simulator scenarios for promotion and dropout. The report will separate host, simulator, firmware-build and physical evidence, and will mark every hardware-only item **NOT TESTED**.

---

**Open for your review before I write code:** the bend-suspension decision in §3. Everything else follows the brief as written. No commits, no flashing.
