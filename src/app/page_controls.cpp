// The fader and knob pages (FM Buddy 2.0, from the MIDI page): MIDI controls over all of the screen. Faders: four at a
// time, in four banks. Knobs: eight at a time (two rows of four), in four banks. Each control sends its own CC on its
// own channel, in its own range (controls.h, midictl.h), as the Touch device.
//
// Each page has two modes (the bar's Play / Select, or holding the encoder down):
//   Play    a finger moves the control it lands on: a fader to where the finger is, a knob by sliding (up or right:
//           clockwise; down or left: anticlockwise; 120 px for all of it; lifting and touching again carries on)
//           (a relative or clockwise/anticlockwise knob: a click every KNOB_PX_CLICK pixels of it)
//   Select  a tap selects a control (one at a time); the encoder turns it, sending its CC. Fingers move nothing
// A pitch bend control (KIND_PB) fills from the middle and shows how far from it it is; set to return, it goes back to
// the middle when its finger lifts. A pitch bend fader's value, below it, is a button: a tap there switches it between
// returning to the middle and holding where it was let go (it's no part of the fader: a finger landing there moves
// nothing)
// With nothing selected on the page showing (or another page showing), the encoder turns its own control (Settings).
//
// Two fingers at once (the panel feels two): each holds the control it landed on until it lifts (TouchOwners).
// Swiping sideways along the bar at the top goes to the next page (leftwards) or the previous one (rightwards): the
// faders' banks 1-4, then the knobs' 1-4, and round (midictl::gesture, nextPage). The controls never swipe: a finger
// on one answers at once, as ever
#include <stdio.h>
#include <string.h>

#include <string>

#include "controls.h"
#include "hal.h"
#include "midi.h"
#include "ui_priv.h"

using namespace ui;

#define BAR_H (TOP_BAR_H + EDGE_T)  // the bar along the top (ui_priv.h: the same on every full-screen page), off the edge
#define BAR_BTN TOP_BTN_H
#define BAR_FONT TOP_FONT
#define POLL_MS 10
#define LABEL_H 18  // a fader's name, above it
#define VALUE_H 20  // its value, below it
#define KNOB_PX_CLICK 8  // a relative knob: a click every this many pixels a finger slides
#define REL_ARC 14       // a relative knob's pointer: degrees either side

static int grp = GROUP_FADERS;  // the group the page shows (and its settings pages)
static int editIdx;             // the control the edit page shows
static lv_obj_t *cells[CTL_KNOB_BANK], *meters[CTL_KNOB_BANK], *values[CTL_KNOB_BANK], *names[CTL_KNOB_BANK];
static lv_timer_t* poller;
static TouchOwners owners;
static uint8_t pos0[OWN_FINGERS];  // a knob's position when its finger landed
static int clicked[OWN_FINGERS];   // a relative knob: the clicks its finger has sent so far

static Builder pageOf(int g) {
  return g == GROUP_KNOBS ? knobsPage : fadersPage;
}

// --------------------------------------------------------------------------------------- the layout ---
static int surfaceH() {
  return SCREEN_H - BAR_H - EDGE_B;  // (FM Buddy 2.0: off the bottom edge)
}
static int rows() {
  return grp == GROUP_KNOBS ? 2 : 1;
}
static int cellW() {
  return SCREEN_W / 4;
}
static int cellH() {
  return surfaceH() / rows();
}
// a fader's track, in screen y
static int trackTop() {
  return BAR_H + LABEL_H + 4;
}
static int trackH() {
  return surfaceH() - LABEL_H - VALUE_H - 8;
}
// a knob: pixels of sliding for all of its turn (120 on the 320 x 240 screen). Up and right both turn it clockwise,
// so a knob on the top row, with little room above it, turns as well sideways
static int knobSpan() {
  return surfaceH() * 3 / 5;
}

// how far a finger has turned a knob, in pixels: up and right clockwise (+), down and left anticlockwise
static int knobTravel(const TouchOwners::Owner& o, const hal::Finger& f) {
  return (f.x - o.x0) + (o.y0 - f.y);
}

static int firstShown() {
  return midictl::first(grp, controls::get().bank(grp));
}

static bool bends(int i) {
  return controls::get().setup().ctl[i].kind == KIND_PB;
}

// a pitch bend fader's hold button: the strip below its track
static int holdTop() {
  return BAR_H + surfaceH() - VALUE_H - 3;
}

// the control at (x, y) on the screen, -1 none (the bar, a pitch bend fader's hold button)
static int hitAt(int x, int y, void*) {
  if (y < BAR_H || x < 0 || x >= SCREEN_W || y >= SCREEN_H) {
    return -1;
  }
  int col = x / cellW(), row = (y - BAR_H) / cellH();
  col = col > 3 ? 3 : col;
  row = row >= rows() ? rows() - 1 : row;
  int i = firstShown() + row * 4 + col;
  if (grp == GROUP_FADERS && y >= holdTop() && bends(i)) {
    return -1;  // (its button's)
  }
  return i;
}

// ------------------------------------------------------------------------------------------ showing ---
static void outline(int k) {
  if (!cells[k]) {
    return;
  }
  MidiControls& c = controls::get();
  bool sel = c.mode(grp) == MODE_SELECT && c.selected() == firstShown() + k;
  lv_obj_set_style_border_opa(cells[k], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(cells[k], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void show(int i);

static void showAll(int) {
  go(pageOf(grp));
}

// control i as it is now (if it's on the screen); -1: the page again
static void show(int i) {
  if (i < 0) {
    showAll(i);
    return;
  }
  MidiControls& c = controls::get();
  if (!midictl::inBank(i, grp, c.bank(grp))) {
    return;
  }
  int k = i - firstShown();
  if (!meters[k]) {
    return;
  }
  const CtlConfig& cfg = c.setup().ctl[i];
  if (cfg.kind == KIND_PB) {  // (from the middle: -64..63)
    if (grp == GROUP_KNOBS) {
      lv_arc_set_value(meters[k], c.pos(i) - 64);
    } else {
      lv_bar_set_value(meters[k], c.pos(i) - 64, LV_ANIM_OFF);
    }
    int v = c.shown(i);
    bool hold = !cfg.rel;
    if (grp == GROUP_FADERS) {
      lv_label_set_text_fmt(values[k], v ? "%s%+d" : "%s0", hold ? LV_SYMBOL_PAUSE " " : "", v);
      const Theme& t = theme();
      lv_obj_set_style_bg_opa(values[k], hold ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      lv_obj_set_style_text_color(values[k], color(hold ? t.bg : t.text), 0);
    } else {
      lv_label_set_text_fmt(values[k], v ? "%+d" : "0", v);
    }
  } else if (grp == GROUP_KNOBS && cfg.kind != KIND_ABS) {
    // endless: a pointer that goes round (where it is says nothing; the last turn is the number)
    int a = 270 + c.pos(i) * 360 / 128;
    lv_arc_set_angles(meters[k], (a - REL_ARC + 360) % 360, (a + REL_ARC) % 360);
    int t = c.lastTurn(i);
    if (t) {
      lv_label_set_text_fmt(values[k], "%+d", t);
    } else {
      lv_label_set_text(values[k], cfg.kind == KIND_REL ? LV_SYMBOL_REFRESH : LV_SYMBOL_LOOP);
    }
  } else {
    if (grp == GROUP_KNOBS) {
      lv_arc_set_value(meters[k], c.pos(i));
    } else {
      lv_bar_set_value(meters[k], c.pos(i), LV_ANIM_OFF);
    }
    lv_label_set_text_fmt(values[k], "%u", c.value(i));
  }
  outline(k);
}

static void selectCtl(int i) {
  MidiControls& c = controls::get();
  int was = c.selected();
  if (was == i) {
    return;
  }
  c.select(i);
  controls::save();
  show(was);
  show(i);
}

// a pitch bend that returns, let go of while playing: back to the middle (sent). shown: and on the screen too (not
// while the page is going)
static void letGo(const int* ended, int n, bool shown) {
  MidiControls& c = controls::get();
  for (int e = 0; e < n; e++) {
    int i = ended[e];
    if (i < 0 || i >= CTL_COUNT || !bends(i) || !c.setup().ctl[i].rel || c.mode(midictl::groupOf(i)) != MODE_PLAY) {
      continue;
    }
    controls::play(i, 64);
    if (shown) {
      show(i);
    }
  }
}

// a pitch bend fader's hold button: return to the middle when let go, or hold
static void holdTapped(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  MidiControls& c = controls::get();
  CtlConfig cfg = c.setup().ctl[i];
  if (cfg.kind != KIND_PB) {
    return;
  }
  cfg.rel = cfg.rel ? 0 : 1;
  c.assign(i, cfg);
  controls::save();
  if (cfg.rel && owners.holder(i) < 0) {
    controls::play(i, 64);  // (returning: from now, so back to the middle now)
  }
  show(i);
  toast(cfg.rel ? "Returns to the middle when let go" : "Holds where it's let go");
}

// ------------------------------------------------------------------------------------------ fingers ---
static uint8_t way[OWN_FINGERS];  // a finger in the bar: its Gesture (a swipe once it's gone far enough sideways)

// LVGL's pointer (the first finger) leaves the bar's buttons alone until it lifts: a finger holding a fader or a knob
// that slides up into the bar, or one that swiped, presses nothing there
static void quietPointer() {
  lv_indev_t* in = lv_indev_get_next(0);
  while (in && lv_indev_get_type(in) != LV_INDEV_TYPE_POINTER) {
    in = lv_indev_get_next(in);
  }
  if (in) {
    lv_indev_reset(in, 0);
    lv_indev_wait_release(in);
  }
}

// a swipe along the bar: the next (+1) or previous (-1) page, the faders' banks then the knobs' (midictl::nextPage)
static void swipeTo(void* d) {
  MidiControls& c = controls::get();
  int g = grp, b = c.bank(grp);
  midictl::nextPage(g, b, (int)(intptr_t)d);
  int ended[OWN_FINGERS];
  letGo(ended, owners.releaseAll(ended), false);
  c.bank(g, b);
  controls::save();
  go(pageOf(g));
}

static void poll(lv_timer_t*) {
  hal::Finger f[OWN_FINGERS];
  int n = hal::fingers(f, OWN_FINGERS);
  OwnPoint p[OWN_FINGERS];
  for (int j = 0; j < n; j++) {
    p[j] = {f[j].id, f[j].x, f[j].y};
  }
  int ended[OWN_FINGERS];
  // (a fader or a knob stays where it was let go: nothing to send. A pitch bend that returns: the middle)
  letGo(ended, owners.update(p, n, hitAt, 0, ended), true);
  MidiControls& c = controls::get();
  bool selecting = c.mode(grp) == MODE_SELECT;
  for (int s = 0; s < OWN_FINGERS; s++) {
    const TouchOwners::Owner& o = owners.slot(s);
    if (!o.on) {
      continue;
    }
    if (o.began) {
      way[s] = GESTURE_UNDECIDED;
    }
    int j = 0;
    while (j < n && f[j].id != o.id) {
      j++;
    }
    if (j == n) {
      continue;
    }
    // a finger that landed on the bar: sideways far enough, a swipe (the controls themselves never swipe: they
    // answer at once, as ever)
    if (o.ctl < 0) {
      if (o.y0 < BAR_H && way[s] == GESTURE_UNDECIDED) {
        int g = midictl::gesture(f[j].x - o.x0, f[j].y - o.y0);
        if (g == GESTURE_NEXT || g == GESTURE_PREV) {
          way[s] = (uint8_t)g;
          quietPointer();  // (not the button it started on, nor what's under it on the next page)
          lv_async_call(swipeTo, (void*)(intptr_t)(g == GESTURE_NEXT ? 1 : -1));  // (not from inside this timer)
          return;
        }
      }
      continue;
    }
    if (o.began) {
      quietPointer();  // (holding a control: sliding into the bar presses nothing there)
    }
    if (selecting) {
      if (o.began) {
        selectCtl(o.ctl);
      }
      continue;
    }
    if (o.began) {
      pos0[s] = c.pos(o.ctl);
      clicked[s] = 0;
    }
    if (grp == GROUP_KNOBS && c.setup().ctl[o.ctl].kind != KIND_ABS && !bends(o.ctl)) {
      int clicks = knobTravel(o, f[j]) / KNOB_PX_CLICK - clicked[s];  // (up or right: clockwise)
      if (clicks) {
        clicked[s] += clicks;
        controls::turn(o.ctl, clicks);
        show(o.ctl);
      }
      continue;
    }
    int pos = grp == GROUP_FADERS ? (trackTop() + trackH() - 1 - f[j].y) * 127 / (trackH() - 1)
                                  : pos0[s] + knobTravel(o, f[j]) * 127 / knobSpan();
    pos = pos < 0 ? 0 : pos > 127 ? 127 : pos;
    if (pos != c.pos(o.ctl)) {
      controls::play(o.ctl, (uint8_t)pos);
      show(o.ctl);
    }
  }
}

static void gone(lv_event_t*) {
  int ended[OWN_FINGERS];
  letGo(ended, owners.releaseAll(ended), false);
  if (poller) {
    lv_timer_delete(poller);
    poller = 0;
  }
  controls::showing(-1, 0);
  for (int k = 0; k < CTL_KNOB_BANK; k++) {
    cells[k] = meters[k] = values[k] = names[k] = 0;
  }
}

// ------------------------------------------------------------------------------------------ the page ---
static void ctlListPage(lv_obj_t* page);

static void pickBank(lv_event_t* e) {
  lv_obj_t* m = (lv_obj_t*)lv_event_get_target(e);
  int b = (int)lv_buttonmatrix_get_selected_button(m);
  MidiControls& c = controls::get();
  if (b < 0 || b >= CTL_BANKS || b == c.bank(grp)) {
    return;
  }
  int ended[OWN_FINGERS];
  letGo(ended, owners.releaseAll(ended), false);  // (fingers still down take nothing in the new bank until they lift)
  c.bank(grp, b);
  controls::save();
  go(pageOf(grp));
}

static void pickMode(lv_event_t* e) {
  lv_obj_t* m = (lv_obj_t*)lv_event_get_target(e);
  int v = (int)lv_buttonmatrix_get_selected_button(m);
  MidiControls& c = controls::get();
  if (v != MODE_PLAY && v != MODE_SELECT) {
    return;
  }
  int ended[OWN_FINGERS];
  letGo(ended, owners.releaseAll(ended), false);
  c.mode(grp, (CtlMode)v);
  controls::save();
  go(pageOf(grp));
}

static lv_obj_t* matrix(lv_obj_t* bar, const char** map, int checked, int w, lv_event_cb_t cb) {
  lv_obj_t* m = lv_buttonmatrix_create(bar);
  lv_buttonmatrix_set_map(m, map);
  // (on a tap that ends on it, not on the press: a swipe along the bar that starts on one doesn't pick it)
  lv_buttonmatrix_set_button_ctrl_all(m, (lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_CLICK_TRIG));
  lv_buttonmatrix_set_one_checked(m, true);
  lv_buttonmatrix_set_button_ctrl(m, checked, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_obj_set_size(m, w, BAR_BTN);
  lv_obj_set_style_text_font(m, BAR_FONT, 0);
  lv_obj_set_style_pad_all(m, 1, 0);
  lv_obj_set_style_pad_column(m, 2, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_add_event_cb(m, cb, LV_EVENT_VALUE_CHANGED, 0);
  return m;
}

static void routingOn(lv_event_t*) {
  midi::route(true);
  go(pageOf(grp));
}

static void buildPage(lv_obj_t* page, int group) {
  grp = group;
  MidiControls& c = controls::get();
  const Theme& t = theme();
  fullscreen(true);
  lv_obj_set_style_pad_all(page, 0, 0);
  lv_obj_set_style_pad_row(page, 0, 0);

  // the bar: back, the banks, play or select (or routing, if it's off), settings
  lv_obj_t* bar = box(page, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(bar, SCREEN_W, BAR_H);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(bar, EDGE_T, 0);
  lv_obj_set_style_pad_left(bar, EDGE_BAR, 0);  // (back and the gear off the screen's edges)
  lv_obj_set_style_pad_right(bar, EDGE_BAR, 0);
  lv_obj_set_style_pad_column(bar, 3, 0);
  lv_obj_set_style_bg_color(bar, color(t.panel), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_t* back = button(bar, LV_SYMBOL_LEFT, [](lv_event_t*) { go(MIDI, midiPage); });
  lv_obj_set_size(back, SCREEN_W / 8, BAR_BTN);
  lv_obj_set_style_text_font(back, &lv_font_montserrat_20, 0);
  static const char* banks[] = {"1", "2", "3", "4", ""};
  matrix(bar, banks, c.bank(grp), SCREEN_W * 112 / 320, pickBank);
  lv_obj_t* gap = box(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_grow(gap, 1);
  lv_obj_set_flex_align(gap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (!midi::routing) {
    lv_obj_t* r = button(gap, "Routing off", routingOn);
    lv_obj_set_height(r, BAR_BTN);
    lv_obj_set_style_text_color(r, color(t.knob[1]), 0);
    lv_obj_set_style_pad_ver(r, 2, 0);
  } else {
    static const char* modes[] = {"Play", "Select", ""};
    matrix(gap, modes, c.mode(grp), SCREEN_W * 100 / 320, pickMode);
  }
  lv_obj_t* gear = button(bar, LV_SYMBOL_SETTINGS, [](lv_event_t*) { go(ctlListPage); });
  lv_obj_set_size(gear, SCREEN_W / 8, BAR_BTN);
  lv_obj_set_style_text_font(gear, &lv_font_montserrat_20, 0);

  // the surface: a cell for each control on the screen
  lv_obj_t* surface = lv_obj_create(page);
  lv_obj_remove_style_all(surface);
  lv_obj_set_size(surface, SCREEN_W, surfaceH());
  lv_obj_remove_flag(surface, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_style_bg_color(surface, color(t.bg), 0);
  lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(surface, gone, LV_EVENT_DELETE, 0);

  int n = midictl::perBank(grp), f = firstShown();
  for (int k = 0; k < CTL_KNOB_BANK; k++) {
    cells[k] = meters[k] = values[k] = names[k] = 0;
  }
  for (int k = 0; k < n; k++) {
    int i = f + k, col = k % 4, row = k / 4;
    const CtlConfig& cfg = c.setup().ctl[i];
    lv_obj_t* cell = lv_obj_create(surface);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, cellW() - 4, cellH() - 4);
    lv_obj_set_pos(cell, col * cellW() + 2, row * cellH() + 2);
    lv_obj_remove_flag(cell, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_radius(cell, 8, 0);
    lv_obj_set_style_bg_color(cell, color(t.panel), 0);
    lv_obj_set_style_border_color(cell, color(t.accent), 0);
    lv_obj_set_style_border_width(cell, 2, 0);
    cells[k] = cell;
    char nm[CTL_LABEL];
    midictl::label(cfg, nm);
    if (grp == GROUP_FADERS) {
      names[k] = line(cell, nm, &lv_font_montserrat_12, t.text);
      lv_obj_set_width(names[k], cellW() - 10);
      lv_obj_set_style_text_align(names[k], LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(names[k], LV_ALIGN_TOP_MID, 0, 3);
      lv_obj_t* bar = lv_bar_create(cell);
      if (cfg.kind == KIND_PB) {
        lv_bar_set_mode(bar, LV_BAR_MODE_SYMMETRICAL);  // (filled from the middle)
        lv_bar_set_range(bar, -64, 63);
      } else {
        lv_bar_set_range(bar, 0, 127);
      }
      lv_obj_set_size(bar, 30, trackH());
      lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, trackTop() - BAR_H - 2);
      lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_radius(bar, 6, 0);
      lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(bar, color(t.off), 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(bar, color(t.accent), LV_PART_INDICATOR);
      meters[k] = bar;
      values[k] = label(cell, "", &lv_font_montserrat_14, t.text);
      if (cfg.kind == KIND_PB) {  // (a button: hold, or return to the middle)
        lv_obj_set_size(values[k], cellW() - 12, VALUE_H + 1);
        lv_obj_set_style_text_align(values[k], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(values[k], 2, 0);
        lv_obj_set_style_radius(values[k], 6, 0);
        lv_obj_set_style_border_width(values[k], 1, 0);
        lv_obj_set_style_border_color(values[k], color(t.accent), 0);
        lv_obj_set_style_bg_color(values[k], color(t.accent), 0);
        lv_obj_add_flag(values[k], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(values[k], holdTapped, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_align(values[k], LV_ALIGN_BOTTOM_MID, 0, -1);
      } else {
        lv_obj_align(values[k], LV_ALIGN_BOTTOM_MID, 0, -2);
      }
    } else {
      int d = cellH() - 4 - 18 - 4;
      d = d > cellW() - 10 ? cellW() - 10 : d;
      lv_obj_t* arc = lv_arc_create(cell);
      lv_obj_set_size(arc, d, d);
      lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 3);
      if (cfg.kind == KIND_ABS) {
        lv_arc_set_bg_angles(arc, 135, 45);
        lv_arc_set_range(arc, 0, 127);
      } else if (cfg.kind == KIND_PB) {
        lv_arc_set_bg_angles(arc, 135, 45);
        lv_arc_set_mode(arc, LV_ARC_MODE_SYMMETRICAL);  // (from the top: the middle)
        lv_arc_set_range(arc, -64, 63);
      } else {
        lv_arc_set_bg_angles(arc, 0, 360);  // (endless: all the way round)
      }
      lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_arc_width(arc, 7, 0);
      lv_obj_set_style_arc_width(arc, 7, LV_PART_INDICATOR);
      lv_obj_set_style_arc_color(arc, color(t.off), 0);
      lv_obj_set_style_arc_color(arc, color(t.accent), LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
      lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
      meters[k] = arc;
      values[k] = label(arc, "", &lv_font_montserrat_14, t.text);
      lv_obj_center(values[k]);
      names[k] = line(cell, nm, &lv_font_montserrat_12, t.text);
      lv_obj_set_width(names[k], cellW() - 10);
      lv_obj_set_style_text_align(names[k], LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(names[k], LV_ALIGN_BOTTOM_MID, 0, -2);
    }
    show(i);
  }

  if (poller) {
    lv_timer_delete(poller);
  }
  poller = lv_timer_create(poll, POLL_MS, 0);
  controls::showing(grp, show);
}

void ui::fadersPage(lv_obj_t* page) {
  buildPage(page, GROUP_FADERS);
}

void ui::knobsPage(lv_obj_t* page) {
  buildPage(page, GROUP_KNOBS);
}

// ---------------------------------------------------------------------------------------- settings ---
static void ctlEditPage(lv_obj_t* page);
static void nameEditPage(lv_obj_t* page);

static const std::string& numbers(const char* prefix) {  // "CC 0".."CC 127", or "0".."127"
  static std::string cc, plain;
  std::string& s = prefix[0] ? cc : plain;
  if (s.empty()) {
    for (int i = 0; i < 128; i++) {
      s += (i ? "\n" : "") + std::string(prefix) + std::to_string(i);
    }
  }
  return s;
}

static const std::string& channels() {
  static std::string s;
  if (s.empty()) {
    for (int i = 1; i <= 16; i++) {
      s += (i > 1 ? "\n" : "") + std::to_string(i);
    }
    s += "\nGlobal";  // (CH_GLOBAL: the Global channel, set once for all)
  }
  return s;
}

// "ch 4", "global"
static const char* chText(const CtlConfig& c) {
  static char s[8];
  if (c.ch == CH_GLOBAL) {
    return "global";
  }
  snprintf(s, sizeof s, "ch %u", c.ch + 1);
  return s;
}

static void heading(lv_obj_t* body, const char* text) {
  lv_obj_t* l = label(body, text, &lv_font_montserrat_12, theme().accent);
  lv_obj_set_style_pad_top(l, 4, 0);
}

static lv_obj_t* pickRow(lv_obj_t* body, const char* name, const std::string& opts, int sel, lv_event_cb_t cb,
                         intptr_t data) {
  const Theme& t = theme();
  lv_obj_t* r = listRow(body, 0, 0);
  lv_obj_set_height(r, 38);
  lv_obj_t* l = label(r, name, &lv_font_montserrat_14, t.text);
  lv_obj_set_flex_grow(l, 1);
  lv_obj_t* dd = lv_dropdown_create(r);
  lv_dropdown_set_options(dd, opts.c_str());
  lv_dropdown_set_selected(dd, sel);
  lv_obj_set_width(dd, 110);
  lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, (void*)data);
  return r;
}

static void ctlRow(lv_obj_t* body, int i, const char* what) {
  const Theme& t = theme();
  const CtlConfig& cfg = controls::get().setup().ctl[i];
  lv_obj_t* r = listRow(
      body,
      [](lv_event_t* e) {
        editIdx = (int)(intptr_t)lv_event_get_user_data(e);
        go(ctlEditPage);
      },
      (void*)(intptr_t)i);
  label(r, what, &lv_font_montserrat_14, t.dim);
  char nm[CTL_LABEL];
  lv_obj_t* n = line(r, midictl::label(cfg, nm), &lv_font_montserrat_16, t.text);
  lv_obj_set_flex_grow(n, 1);
  char s[32];
  if (cfg.kind == KIND_PB) {
    snprintf(s, sizeof s, "%s  %s", cfg.rel ? "bend" : "bend, hold", chText(cfg));
  } else if (cfg.kind == KIND_INCDEC) {
    snprintf(s, sizeof s, "CC %u/%u  %s", cfg.cc, cfg.cc2, chText(cfg));
  } else if (cfg.label[0]) {
    snprintf(s, sizeof s, "CC %u%s  %s", cfg.cc, cfg.kind == KIND_REL ? " rel" : "", chText(cfg));
  } else {
    snprintf(s, sizeof s, "%s%s", cfg.kind == KIND_REL ? "rel  " : "", chText(cfg));  // (its name is its CC)
  }
  label(r, s, &lv_font_montserrat_12, t.dim);
  label(r, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, t.dim);
}

static void pickStep(lv_event_t* e) {
  controls::get().step((int)lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e)) + 1);
  controls::save();
}

static void ctlListPage(lv_obj_t* page) {
  MidiControls& c = controls::get();
  const Theme& t = theme();
  header(page, grp == GROUP_KNOBS ? "Knobs" : "Faders", pageOf(grp));
  lv_obj_t* body = scroller(page);
  lv_obj_set_style_pad_row(body, 2, 0);
  char h[24];
  snprintf(h, sizeof h, "Bank %d", c.bank(grp) + 1);
  heading(body, h);
  int f = firstShown();
  for (int k = 0; k < midictl::perBank(grp); k++) {
    char w[4];
    snprintf(w, sizeof w, "%d", k + 1);
    ctlRow(body, f + k, w);
  }
  char rt[40];
  snprintf(rt, sizeof rt, "Hold to reset bank %d", c.bank(grp) + 1);
  lv_obj_t* rb = button(body, rt, [](lv_event_t*) {}, 0);
  lv_obj_set_width(rb, LV_PCT(100));
  lv_obj_add_event_cb(
      rb,
      [](lv_event_t*) {
        MidiControls& m = controls::get();
        int b = m.bank(grp);
        m.resetBank(grp, b);
        controls::save();
        char msg[40];
        snprintf(msg, sizeof msg, "Bank %d: back to its defaults", b + 1);
        toast(msg);
        go(ctlListPage);
      },
      LV_EVENT_LONG_PRESSED, 0);
  {  // (FM Buddy 2.0) every control of the page, all four banks, on one channel, or Global
    lv_obj_t* r = listRow(body, 0, 0);
    lv_obj_set_height(r, 40);
    lv_obj_t* l = label(r, "All on channel", &lv_font_montserrat_14, t.text);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_t* dd = lv_dropdown_create(r);
    std::string o = "Pick...\n" + channels();  // (a first line: picking any channel is a change)
    lv_dropdown_set_options(dd, o.c_str());
    lv_obj_set_width(dd, 120);
    lv_obj_add_event_cb(
        dd,
        [](lv_event_t* e) {
          uint32_t v = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
          if (!v--) {
            return;
          }
          MidiControls& m = controls::get();
          for (int b = 0; b < CTL_BANKS; b++) {
            for (int k = 0; k < midictl::perBank(grp); k++) {
              int i = midictl::first(grp, b) + k;
              CtlConfig cc = m.setup().ctl[i];
              cc.ch = (uint8_t)v;
              m.assign(i, cc);
            }
          }
          controls::save();
          toast(v == CH_GLOBAL ? "All on Global" : "All on one channel");
          go(ctlListPage);
        },
        LV_EVENT_VALUE_CHANGED, 0);
  }
  {  // (FM Buddy 2.0) the Global channel: the one the controls on Global send on
    std::string chs;
    for (int i = 1; i <= 16; i++) {
      chs += (i > 1 ? "\n" : "") + std::to_string(i);
    }
    pickRow(body, "Global is channel", chs, controls::globalChannel(),
            [](lv_event_t* e) {
              controls::setGlobalChannel((int)lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e)));
              go(ctlListPage);
            },
            0);
  }
  heading(body, "Encoder");
  std::string steps;
  for (int i = 1; i <= CTL_STEP_MAX; i++) {
    steps += (i > 1 ? "\n" : "") + std::to_string(i);
  }
  pickRow(body, "Step a click", steps, c.setup().step - 1, pickStep, 0);
  ctlRow(body, CTL_ENC, "Own");
  lv_obj_t* l = label(body,
                      "Play: a finger moves a control, and it sends its CC. Select: a tap selects one, and the "
                      "encoder turns it (push: the next one, hold: play or select). With nothing selected, the "
                      "encoder turns its own control. They play as the Touch device: MIDI routing and the mapping "
                      "work on them. Global: a channel for the controls that follow the instrument's selected track, "
                      "set once (Global is channel; X/Y and Touche share it). On SLOOP: a channel no track has, and "
                      "GLO > MIDI 2 > OTHER = SEL: what comes on it plays the selected track.",
                      &lv_font_montserrat_12, t.dim);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
}

enum Field { F_CC,
             F_CH,
             F_LO,
             F_HI,
             F_KIND,
             F_REL,
             F_CC2,
             F_VAL,
             F_RET };

static const char* const KIND_NAMES = "Absolute\nRelative\nCW / CCW\nPitch bend";  // (as CtlKind)
static const char* const FADER_KINDS = "CC\nPitch bend";
static const char* const REL_NAMES = "64 +/- n\n2's compl.\nSign bit";

static void editPicked(lv_event_t* e) {
  MidiControls& c = controls::get();
  CtlConfig cfg = c.setup().ctl[editIdx];
  uint8_t v = (uint8_t)lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
  switch ((Field)(intptr_t)lv_event_get_user_data(e)) {
    case F_CC:
      cfg.cc = v;
      break;
    case F_CH:
      cfg.ch = v;
      break;
    case F_LO:
      cfg.lo = v;
      break;
    case F_HI:
      cfg.hi = v;
      break;
    case F_KIND:
      if (midictl::groupOf(editIdx) == GROUP_FADERS) {
        v = v == 1 ? KIND_PB : KIND_ABS;
      }
      if (v == KIND_PB && cfg.kind != KIND_PB) {
        cfg.rel = 1;  // (returns, as a pitch wheel)
      } else if (v != KIND_PB && cfg.kind == KIND_PB) {
        cfg.rel = REL_OFFSET;
      }
      cfg.kind = v;
      break;
    case F_RET:
      cfg.rel = v == 0 ? 1 : 0;
      break;
    case F_REL:
      cfg.rel = v;
      break;
    case F_CC2:
      cfg.cc2 = v;
      break;
    case F_VAL:
      cfg.incVal = v;
      break;
  }
  bool swapped = cfg.lo > cfg.hi;
  Field fld = (Field)(intptr_t)lv_event_get_user_data(e);
  bool again = fld == F_KIND;  // (its rows change)
  c.assign(editIdx, cfg);
  if (cfg.kind == KIND_PB && (fld == F_KIND || (fld == F_RET && cfg.rel))) {
    uint8_t m[3];
    c.set(editIdx, 64, ORIGIN_CONFIG, m);  // (a bend starts, or returns, at the middle; sent on the next move)
    controls::play(editIdx, 64);
  }
  controls::save();
  if (swapped) {
    toast("Lowest above highest: swapped.");
  }
  if (swapped || again) {
    go(ctlEditPage);
  }
}

static void invertSwitched(lv_event_t* e) {
  MidiControls& c = controls::get();
  CtlConfig cfg = c.setup().ctl[editIdx];
  cfg.invert = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
  c.assign(editIdx, cfg);
  controls::save();
}

static void ctlEditPage(lv_obj_t* page) {
  const Theme& t = theme();
  const CtlConfig& cfg = controls::get().setup().ctl[editIdx];
  char nm[CTL_LABEL];
  midictl::label(cfg, nm);
  header(page, nm, ctlListPage);
  lv_obj_t* body = scroller(page);
  lv_obj_set_style_pad_row(body, 2, 0);
  lv_obj_t* r = listRow(body, [](lv_event_t*) { go(nameEditPage); }, 0);
  lv_obj_set_height(r, 38);
  lv_obj_t* l = label(r, "Name", &lv_font_montserrat_14, t.text);
  lv_obj_set_flex_grow(l, 1);
  label(r, cfg.label[0] ? cfg.label : "(none)", &lv_font_montserrat_14, cfg.label[0] ? t.text : t.dim);
  label(r, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, t.dim);
  bool turns = midictl::groupOf(editIdx) != GROUP_FADERS;  // (knobs and the encoder's own: any kind)
  int kind = cfg.kind < KINDS ? cfg.kind : KIND_ABS;
  if (turns) {
    pickRow(body, "Sends", KIND_NAMES, kind, editPicked, F_KIND);
  } else {
    pickRow(body, "Sends", FADER_KINDS, kind == KIND_PB ? 1 : 0, editPicked, F_KIND);
  }
  if (kind == KIND_PB) {
    // (no CC, no range: all of the bend)
  } else if (kind == KIND_INCDEC) {
    pickRow(body, "CC clockwise", numbers("CC "), cfg.cc & 127, editPicked, F_CC);
    pickRow(body, "CC anticlockwise", numbers("CC "), cfg.cc2 & 127, editPicked, F_CC2);
  } else {
    pickRow(body, "CC", numbers("CC "), cfg.cc & 127, editPicked, F_CC);
  }
  pickRow(body, "MIDI channel", channels(), cfg.ch % 17, editPicked, F_CH);
  if (kind == KIND_ABS) {
    pickRow(body, "Lowest value", numbers(""), cfg.lo & 127, editPicked, F_LO);
    pickRow(body, "Highest value", numbers(""), cfg.hi & 127, editPicked, F_HI);
  } else if (kind == KIND_REL) {
    pickRow(body, "Encoding", REL_NAMES, cfg.rel % RELS, editPicked, F_REL);
  } else if (kind == KIND_PB) {
    pickRow(body, "When let go", "Return\nHold", cfg.rel ? 0 : 1, editPicked, F_RET);
  } else {
    pickRow(body, "Value sent", numbers(""), cfg.incVal & 127, editPicked, F_VAL);
  }
  lv_obj_t* sr = listRow(body, 0, 0);
  lv_obj_set_height(sr, 38);
  lv_obj_t* sl = label(sr, kind == KIND_ABS || kind == KIND_PB ? "Inverted" : "Reversed", &lv_font_montserrat_14, t.text);
  lv_obj_set_flex_grow(sl, 1);
  lv_obj_t* sw = lv_switch_create(sr);
  if (cfg.invert) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(sw, invertSwitched, LV_EVENT_VALUE_CHANGED, 0);
  static const char* const HELP[KINDS] = {
      "It sends its CC on its channel, from the lowest value (all the way down) to the highest (all the way up); "
      "inverted, the other way round.",
      "Endless: each turn sends its CC with how far it turned, n clicks (times the encoder's step, up to 63). "
      "64 +/- n: 65 is one click clockwise, 63 anticlockwise (binary offset). 2's complement: 1 clockwise, 127 "
      "anticlockwise. Sign bit: 1 clockwise, 65 anticlockwise. Pick the one the synth or DAW expects.",
      "Endless: each click clockwise sends one CC, each click anticlockwise the other, both with the value sent.",
      "Pitch bend on its channel, all of it: the middle is no bend, up bends up, down bends down (inverted, the "
      "other way round). Return: back to the middle when the finger lifts, like a pitch wheel. Hold: it stays bent "
      "where it's let go. On the fader page, a tap on a pitch bend fader's value switches it."};
  lv_obj_t* help = label(body, HELP[kind % KINDS], &lv_font_montserrat_12, t.dim);
  lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(help, LV_PCT(100));
}

static lv_obj_t* nameTa;

static void nameKeys(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) {
    MidiControls& c = controls::get();
    CtlConfig cfg = c.setup().ctl[editIdx];
    memset(cfg.label, 0, sizeof cfg.label);
    strncpy(cfg.label, lv_textarea_get_text(nameTa), CTL_LABEL - 1);
    c.assign(editIdx, cfg);
    controls::save();
  }
  go(ctlEditPage);
}

static void nameEditPage(lv_obj_t* page) {
  const Theme& t = theme();
  fullscreen(true);
  header(page, "Name", ctlEditPage);
  const CtlConfig& cfg = controls::get().setup().ctl[editIdx];
  nameTa = lv_textarea_create(page);
  lv_textarea_set_one_line(nameTa, true);
  lv_textarea_set_max_length(nameTa, CTL_LABEL - 1);
  char ph[CTL_LABEL];
  snprintf(ph, sizeof ph, "CC %u", cfg.cc);
  lv_textarea_set_placeholder_text(nameTa, ph);
  lv_textarea_set_text(nameTa, cfg.label);
  lv_obj_set_width(nameTa, LV_PCT(100));
  lv_obj_add_state(nameTa, LV_STATE_FOCUSED);
  lv_obj_t* hint = line(page, "Empty: it shows its CC.", &lv_font_montserrat_12, t.dim);
  lv_obj_set_width(hint, LV_PCT(100));
  lv_obj_t* kb = lv_keyboard_create(page);
  lv_obj_set_size(kb, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(kb, 1);
  lv_keyboard_set_textarea(kb, nameTa);
  lv_obj_add_event_cb(kb, nameKeys, LV_EVENT_READY, 0);
  lv_obj_add_event_cb(kb, nameKeys, LV_EVENT_CANCEL, 0);
}
