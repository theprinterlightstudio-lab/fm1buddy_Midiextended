// SPDX-License-Identifier: MIT
// Granular Field: the performance surface (FM Buddy 2.0, Stage 1 -- the look only, no MIDI).
//
// A still deep-water / deep-space field, and a glow where each finger is. Nothing here reads or
// writes MIDI, and nothing here reads the touch setup: page_touch.cpp calls in with positions and a
// layout, so the picture and the sound stay separate, as they must -- a glow still fading after a
// finger has gone must never put a parameter back.
//
// Fingers: FIELD_MAX is what this renderer is built for; fieldSlots is how many the board it is
// running on actually has, given to fieldBuild and nothing more. The FT6336 on the 2.8" panel feels
// two, so two is what it is given. Nothing here assumes the ceiling is reached, so the panel on a
// later board is a different number at one call, not a rewrite.
//
// What it costs per frame: the background, the wisps and the stars are built once at page entry and
// never touched again, so the only work while playing is moving a handful of small objects per
// finger. LVGL redraws the rectangles those cover and nothing else. Full-screen work per frame would
// fight the sequencer and the MIDI clock for this chip, so there is none.
#pragma once

#include "ui_priv.h"

enum { FIELD_MINIMAL,   // the glow alone, nothing written anywhere
       FIELD_LABELLED,  // each finger's own readout, resting at the nearer edge, in its own colour
       FIELD_LAYOUTS };

#define FIELD_MAX 5         // what the renderer is built for; the board says how many it has
#define FIELD_TRAIL 8       // trail marks kept per finger, reused round and round
#define FIELD_STARS 22
#define FIELD_WISPS 5
#define FIELD_HALO 108      // the soft outer glow
#define FIELD_MID 56        // between the halo and the core
#define FIELD_CORE 26
#define FIELD_TRAIL_D 17
#define FIELD_FADE_MS 560   // how long the glow takes to go after the finger does
#define FIELD_TRAIL_MS 45   // a trail mark no oftener than this (the panel is read every 10 ms)
#define FIELD_READ_W 62     // the readouts' column
#define FIELD_READ_GAP 30   // the least between two of them in the same column

// the field's own colours: it is the instrument, not the chrome, so it does not follow the theme
#define FIELD_DEEP 0x050d1c
#define FIELD_SHALLOW 0x0a2b33
#define FIELD_STAR 0xbfe9f5

// one colour each, all of them cool and luminous so no finger looks like a warning
static const uint32_t FIELD_CORE_RGB[FIELD_MAX] = {0x9ef6ff, 0xe3c4ff, 0xb8ffe3, 0xc6d4ff, 0xffc9e8};
static const uint32_t FIELD_MID_RGB[FIELD_MAX] = {0x3fc8e4, 0x9a6ddf, 0x45d6a8, 0x7d8fe8, 0xe06fb0};
static const uint32_t FIELD_HALO_RGB[FIELD_MAX] = {0x1d6f93, 0x4b3a86, 0x1f7a63, 0x3b4790, 0x8a3466};

// the still field, in percent of the surface: x, y, diameter, and which way it leans in colour
static const uint8_t FIELD_WISP[FIELD_WISPS][4] = {
    {18, 30, 62, 0}, {74, 22, 48, 1}, {46, 72, 70, 0}, {88, 62, 40, 1}, {8, 84, 36, 1}};
static const uint8_t FIELD_STAR_AT[FIELD_STARS][3] = {  // x, y, how bright (0 .. 2)
    {6, 12, 1},  {14, 54, 0}, {23, 20, 2}, {29, 77, 0}, {35, 41, 1}, {41, 9, 0},
    {44, 63, 2}, {52, 31, 0}, {57, 86, 1}, {63, 17, 0}, {66, 49, 1}, {71, 69, 0},
    {77, 37, 2}, {81, 13, 0}, {85, 79, 1}, {90, 45, 0}, {94, 24, 1}, {97, 66, 0},
    {11, 91, 0}, {38, 94, 1}, {59, 5, 0},  {73, 95, 0}};

static struct FieldFinger {
  lv_obj_t *halo, *mid, *core, *read;
  lv_obj_t* trail[FIELD_TRAIL];
  uint8_t trailOpa[FIELD_TRAIL];
  uint8_t next;
  uint32_t trailAt;
  int x, y;
  bool on;
  uint32_t offAt;  // when it lifted; 0 while it is down
  int opa;         // 0 .. 255, the glow's strength now
} fieldF[FIELD_MAX];

static int fieldLayoutNow;
static int fieldSlots;  // how many fingers this board has: given to fieldBuild, never assumed
static int fieldW, fieldH;
static int fieldGridCols, fieldGridRows;  // 0: no grid

// The scale's own grid. Faint enough to read the field through, clear enough to aim at: the lines
// are where the notes change, so what you see is what you will hear. Drawn straight after the
// background and before every glow, so a finger's bloom always passes over it
#define FIELD_GRID_RGB 0x6E8FB8
#define FIELD_GRID_OPA 46
#define FIELD_GRID_ROWS 7

static lv_obj_t* fieldBlob(lv_obj_t* parent, int d, uint32_t rgb, int opa) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, color(rgb), 0);
  lv_obj_set_style_bg_opa(o, (lv_opa_t)opa, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

static void fieldPlace(lv_obj_t* o, int cx, int cy, int d) { lv_obj_set_pos(o, cx - d / 2, cy - d / 2); }

// built once, at page entry. slots: how many fingers the panel feels
// How many cells the grid has, before it is built. 0 columns: no grid at all
static void fieldGridSet(int cols, int rows) {
  fieldGridCols = cols < 2 ? 0 : cols > 24 ? 24 : cols;
  fieldGridRows = rows < 2 ? 0 : rows > 16 ? 16 : rows;
}

static void fieldBuild(lv_obj_t* surface, int w, int h, int layout, int slots) {
  fieldLayoutNow = layout % FIELD_LAYOUTS;
  fieldSlots = slots < 1 ? 1 : slots > FIELD_MAX ? FIELD_MAX
                                                 : slots;
  fieldW = w, fieldH = h;
  for (auto& f : fieldF) {
    f = FieldFinger{};
  }

  lv_obj_t* bg = lv_obj_create(surface);  // deep at the top, shallower at the bottom
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, w, h);
  lv_obj_set_pos(bg, 0, 0);
  lv_obj_set_style_bg_color(bg, color(FIELD_DEEP), 0);
  lv_obj_set_style_bg_grad_color(bg, color(FIELD_SHALLOW), 0);
  lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
  lv_obj_remove_flag(bg, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  for (int i = 1; i < fieldGridCols; i++) {  // the columns: one per note of the scale
    lv_obj_t* l = lv_obj_create(surface);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, 1, h);
    lv_obj_set_pos(l, i * w / fieldGridCols, 0);
    lv_obj_set_style_bg_color(l, color(FIELD_GRID_RGB), 0);
    lv_obj_set_style_bg_opa(l, FIELD_GRID_OPA, 0);
    lv_obj_remove_flag(l, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }
  for (int j = 1; j < fieldGridRows; j++) {  // the rows: grain size, in steps
    lv_obj_t* l = lv_obj_create(surface);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, w, 1);
    lv_obj_set_pos(l, 0, j * h / fieldGridRows);
    lv_obj_set_style_bg_color(l, color(FIELD_GRID_RGB), 0);
    lv_obj_set_style_bg_opa(l, FIELD_GRID_OPA, 0);
    lv_obj_remove_flag(l, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }

  for (auto& wi : FIELD_WISP) {  // the still clouds
    int d = wi[2] * (w < h ? w : h) / 100;
    fieldPlace(fieldBlob(surface, d, FIELD_HALO_RGB[wi[3]], 26), wi[0] * w / 100, wi[1] * h / 100, d);
  }
  for (auto& s : FIELD_STAR_AT) {
    int d = s[2] >= 2 ? 3 : 2;
    static const uint8_t OPA[3] = {70, 130, 200};
    fieldPlace(fieldBlob(surface, d, FIELD_STAR, OPA[s[2]]), s[0] * w / 100, s[1] * h / 100, d);
  }

  for (int i = 0; i < fieldSlots; i++) {
    FieldFinger& f = fieldF[i];
    // order is depth: the halo and the middle under the trail, the core over all of it, or the glow
    // swallows the marks it just left
    f.halo = fieldBlob(surface, FIELD_HALO, FIELD_HALO_RGB[i], 0);
    f.mid = fieldBlob(surface, FIELD_MID, FIELD_MID_RGB[i], 0);
    for (int k = 0; k < FIELD_TRAIL; k++) {
      f.trail[k] = fieldBlob(surface, FIELD_TRAIL_D, FIELD_CORE_RGB[i], 0);
      lv_obj_add_flag(f.trail[k], LV_OBJ_FLAG_HIDDEN);
      f.trailOpa[k] = 0;
    }
    f.core = fieldBlob(surface, FIELD_CORE, FIELD_CORE_RGB[i], 0);

    if (fieldLayoutNow == FIELD_LABELLED) {  // its own, in its own colour: which is whose is never a guess
      f.read = label(surface, "", &lv_font_montserrat_12, FIELD_CORE_RGB[i]);
      lv_obj_set_width(f.read, FIELD_READ_W);
      lv_obj_set_style_bg_color(f.read, color(FIELD_DEEP), 0);  // readable where a glow is behind it
      lv_obj_set_style_radius(f.read, 4, 0);
      lv_obj_set_style_pad_all(f.read, 3, 0);
      lv_obj_add_flag(f.read, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// what the gesture is worth, 0 .. 99, for the readouts. The same numbers Stage 2 will send; nothing
// is sent yet
// With the grid on these step as the gesture steps: a readout that slid smoothly while the MIDI
// jumped would be telling the player something that is not happening
static int fieldPos(int x) {
  if (fieldGridCols > 1 && fieldW > 1) {
    int c = x * fieldGridCols / fieldW;
    c = c < 0 ? 0 : c >= fieldGridCols ? fieldGridCols - 1 : c;
    return c * 99 / (fieldGridCols - 1);
  }
  return fieldW > 1 ? x * 99 / (fieldW - 1) : 0;
}
static int fieldSize(int y) {
  if (fieldGridRows > 1 && fieldH > 1) {
    int r = y * fieldGridRows / fieldH;
    r = r < 0 ? 0 : r >= fieldGridRows ? fieldGridRows - 1 : r;
    return (fieldGridRows - 1 - r) * 99 / (fieldGridRows - 1);
  }
  return fieldH > 1 ? (fieldH - 1 - y) * 99 / (fieldH - 1) : 0;
}

// Each readout goes to the edge its finger is nearer, at about its finger's height, and no two in a
// column come closer than FIELD_READ_GAP -- so five of them never pile up, and none of them sits
// under a fingertip.
static void fieldReadout() {
  if (fieldLayoutNow != FIELD_LABELLED) {
    return;
  }
  int side[2][FIELD_MAX], at[2][FIELD_MAX], n[2] = {0, 0};
  for (int i = 0; i < fieldSlots; i++) {
    FieldFinger& f = fieldF[i];
    if (!f.read) {
      continue;
    }
    if (!f.opa) {
      lv_obj_add_flag(f.read, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    // the edge its own finger is nearer, always: sending it to the far edge to dodge its own glow
    // only lands it on somebody else's, and then the colours say the wrong thing. A glow is a third
    // transparent, so a readout over one is read by giving it its own dark backing, below
    int c = f.x < fieldW / 2 ? 0 : 1;
    int y = f.y - 14;
    // a finger right up against its own edge would have its readout over its own glow: step the
    // readout above or below the glow instead, whichever side of it has the room
    bool under = c ? f.x > fieldW - FIELD_READ_W - 22 : f.x < FIELD_READ_W + 22;
    if (under) {
      int up = f.y - FIELD_HALO / 2 - FIELD_READ_GAP, down = f.y + FIELD_HALO / 2 + 4;
      y = up >= 2 ? up : down;
    }
    y = y < 2 ? 2 : y > fieldH - FIELD_READ_GAP ? fieldH - FIELD_READ_GAP
                                                : y;
    int k = n[c]++;
    while (k > 0 && at[c][k - 1] > y) {  // in order down the column
      side[c][k] = side[c][k - 1], at[c][k] = at[c][k - 1], k--;
    }
    side[c][k] = i, at[c][k] = y;
  }
  for (int c = 0; c < 2; c++) {
    for (int k = 1; k < n[c]; k++) {  // pushed apart, then the whole column back inside
      if (at[c][k] < at[c][k - 1] + FIELD_READ_GAP) {
        at[c][k] = at[c][k - 1] + FIELD_READ_GAP;
      }
    }
    int over = n[c] ? at[c][n[c] - 1] + FIELD_READ_GAP - fieldH : 0;
    for (int k = 0; k < n[c] && over > 0; k++) {
      at[c][k] = at[c][k] - over < 2 ? 2 : at[c][k] - over;
    }
    for (int k = 0; k < n[c]; k++) {
      FieldFinger& f = fieldF[side[c][k]];
      char b[32];
      snprintf(b, sizeof b, "POS %d\nSIZE %d", fieldPos(f.x), fieldSize(f.y));
      lv_label_set_text(f.read, b);
      lv_obj_set_style_text_align(f.read, c ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_pos(f.read, c ? fieldW - FIELD_READ_W - 10 : 4, at[c][k]);
      lv_obj_set_style_text_opa(f.read, (lv_opa_t)f.opa, 0);
      lv_obj_set_style_bg_opa(f.read, (lv_opa_t)(f.opa * 160 / 255), 0);  // the backing goes with it
      lv_obj_remove_flag(f.read, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void fieldShow(FieldFinger& f) {
  static const int SHARE[3] = {58, 120, 255};  // halo, middle, core, out of the glow's strength
  lv_obj_t* o[3] = {f.halo, f.mid, f.core};
  int d[3] = {FIELD_HALO, FIELD_MID, FIELD_CORE};
  for (int k = 0; k < 3; k++) {
    if (!o[k]) {
      continue;
    }
    int a = f.opa * SHARE[k] / 255;
    if (!a) {
      lv_obj_add_flag(o[k], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    fieldPlace(o[k], f.x, f.y, d[k]);
    lv_obj_set_style_bg_opa(o[k], (lv_opa_t)a, 0);
    lv_obj_remove_flag(o[k], LV_OBJ_FLAG_HIDDEN);
  }
}

// a finger landed or moved. x, y are the surface's own pixels
static void fieldTouch(int slot, int x, int y, uint32_t now) {
  if (slot < 0 || slot >= fieldSlots) {
    return;
  }
  FieldFinger& f = fieldF[slot];
  if (!f.halo) {
    return;  // not built (another mode)
  }
  bool landed = !f.on;
  f.on = true, f.offAt = 0, f.opa = 255;
  if (landed) {
    f.trailAt = now;
  } else if (now - f.trailAt >= FIELD_TRAIL_MS && (x != f.x || y != f.y)) {
    f.trailAt = now;  // leave a mark where it was
    lv_obj_t* t = f.trail[f.next];
    fieldPlace(t, f.x, f.y, FIELD_TRAIL_D);
    f.trailOpa[f.next] = 190;
    lv_obj_set_style_bg_opa(t, 190, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_HIDDEN);
    f.next = (uint8_t)((f.next + 1) % FIELD_TRAIL);
  }
  f.x = x, f.y = y;
  fieldShow(f);
  fieldReadout();
}

static void fieldLift(int slot, uint32_t now) {
  if (slot < 0 || slot >= fieldSlots) {
    return;
  }
  FieldFinger& f = fieldF[slot];
  if (!f.on) {
    return;
  }
  f.on = false;
  f.offAt = now ? now : 1;
}

// the glow going, and the trail behind it. Called from the touch page's own tick; it never looks at
// or changes anything to do with MIDI
static void fieldTick(uint32_t now) {
  bool any = false;
  for (int i = 0; i < fieldSlots; i++) {
    FieldFinger& f = fieldF[i];
    if (!f.halo) {
      return;
    }
    if (!f.on && f.offAt) {
      uint32_t t = now - f.offAt;
      int a = t >= FIELD_FADE_MS ? 0 : 255 - (int)(255 * t / FIELD_FADE_MS);
      if (a != f.opa) {
        f.opa = a;
        fieldShow(f);
        any = true;
      }
      if (!a) {
        f.offAt = 0;
      }
    }
    for (int k = 0; k < FIELD_TRAIL; k++) {  // every mark always on its way out
      if (!f.trailOpa[k]) {
        continue;
      }
      int a = f.trailOpa[k] - 4;  // (about half a second to go)
      f.trailOpa[k] = (uint8_t)(a < 0 ? 0 : a);
      if (!f.trailOpa[k]) {
        lv_obj_add_flag(f.trail[k], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_set_style_bg_opa(f.trail[k], f.trailOpa[k], 0);
      }
      any = true;
    }
  }
  if (any) {
    fieldReadout();
  }
}

static void fieldRest() {  // the page is leaving, or the mode changed
  for (int i = 0; i < FIELD_MAX; i++) {
    fieldF[i].on = false, fieldF[i].offAt = 0, fieldF[i].opa = 0;
    if (fieldF[i].read) {
      lv_obj_add_flag(fieldF[i].read, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// NOT THE HARDWARE. A still picture of a panel that feels more fingers than this board's does, so
// the layout can be judged before such a board exists. The simulator's --demo N alone reaches this;
// nothing on the device calls it, and no touch of any kind is involved
static void fieldDemoHold(int n, uint32_t now) {
  static const uint8_t AT[FIELD_MAX][2] = {{16, 68}, {33, 34}, {52, 72}, {71, 30}, {88, 62}};
  for (int i = 0; i < n && i < fieldSlots; i++) {
    fieldTouch(i, AT[i][0] * fieldW / 100, AT[i][1] * fieldH / 100, now);
  }
}
