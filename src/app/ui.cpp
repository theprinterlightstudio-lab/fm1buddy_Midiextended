// The app's frame: the theme, the rail on the left (the tabs, and what's on), pages, and watching the jobs
#include "ui.h"

#include <ctype.h>
#include <string.h>

#include <atomic>

#include "catalog.h"
#include "controls.h"
#include "fm1.h"
#include "hal.h"
#include "jobs.h"
#include "midi.h"
#include "play.h"
void seqDriveBegin();  // (seqdrive.cpp: the sequencer's engine, started with the rest)
#include "record.h"
#include "src/themes/lv_theme_private.h"
#include "ui_priv.h"

namespace ui {
void (*tick)();
void (*jobDone)();
}  // namespace ui

using namespace ui;

static lv_obj_t *rail, *pageObj, *tabs[TABS], *stSd, *stWifi, *stBt, *stFm1, *stPower, *stBat, *stBolt;
static Tab tab = FIRMWARE;
static Builder current = firmwarePage;
static uint32_t seenFinished;  // jobs::finished, as last seen

const Theme& ui::theme() {
  return THEMES[settings.theme];
}

// ---------------------------------------------------------------------------------------------- theme ---
static lv_theme_t th;
static lv_style_t sScreen, sCard, sBtn, sBtnPr, sBtnCk, sBtnDis, sInput, sInputFocus, sItems, sItemsPr, sItemsCk,
    sOff, sAccent, sKnob, sScrollbar, sPanel, sCursor;

static void apply(lv_theme_t*, lv_obj_t* o) {
  lv_obj_add_style(o, &sScrollbar, LV_PART_SCROLLBAR);
  if (!lv_obj_get_parent(o)) {
    lv_obj_add_style(o, &sScreen, 0);
  } else if (lv_obj_get_class(o) == &lv_obj_class) {
    lv_obj_add_style(o, &sCard, 0);
  } else if (lv_obj_check_type(o, &lv_button_class)) {
    lv_obj_add_style(o, &sBtn, 0);
    lv_obj_add_style(o, &sBtnPr, LV_STATE_PRESSED);
    lv_obj_add_style(o, &sBtnCk, LV_STATE_CHECKED);
    lv_obj_add_style(o, &sBtnDis, LV_STATE_DISABLED);
  } else if (lv_obj_check_type(o, &lv_buttonmatrix_class) || lv_obj_check_type(o, &lv_keyboard_class)) {
    // a keyboard's own keys (shift, backspace, ...) are "checked": quieter than the accent
    bool kb = lv_obj_check_type(o, &lv_keyboard_class);
    lv_obj_add_style(o, &sPanel, 0);
    lv_obj_add_style(o, &sItems, LV_PART_ITEMS);
    lv_obj_add_style(o, kb ? &sItemsPr : &sItemsCk, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_style(o, kb ? &sItemsCk : &sItemsPr, LV_PART_ITEMS | LV_STATE_PRESSED);
  } else if (lv_obj_check_type(o, &lv_textarea_class)) {
    lv_obj_add_style(o, &sInput, 0);
    lv_obj_add_style(o, &sInputFocus, LV_STATE_FOCUSED);
    lv_obj_add_style(o, &sCursor, LV_PART_CURSOR | LV_STATE_FOCUSED);
  } else if (lv_obj_check_type(o, &lv_dropdown_class)) {
    lv_obj_add_style(o, &sInput, 0);
    lv_obj_add_style(o, &sInputFocus, LV_STATE_FOCUSED);
  } else if (lv_obj_check_type(o, &lv_dropdownlist_class)) {
    lv_obj_add_style(o, &sPanel, 0);
    lv_obj_add_style(o, &sItemsPr, LV_PART_SELECTED | LV_STATE_PRESSED);
    lv_obj_add_style(o, &sItemsCk, LV_PART_SELECTED | LV_STATE_CHECKED);
  } else if (lv_obj_check_type(o, &lv_switch_class) || lv_obj_check_type(o, &lv_slider_class) ||
             lv_obj_check_type(o, &lv_bar_class)) {
    lv_obj_add_style(o, &sOff, 0);
    lv_obj_add_style(o, &sAccent, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_style(o, &sAccent, LV_PART_INDICATOR);
    lv_obj_add_style(o, &sKnob, LV_PART_KNOB);
  } else if (lv_obj_check_type(o, &lv_spinner_class)) {
    lv_obj_set_style_arc_color(o, color(theme().off), 0);
    lv_obj_set_style_arc_color(o, color(theme().accent), LV_PART_INDICATOR);
  }
}

static void setTheme(lv_display_t* disp) {
  static bool made;
  const Theme& t = theme();
  lv_style_t* all[] = {&sScreen, &sCard, &sBtn, &sBtnPr, &sBtnCk, &sBtnDis, &sInput, &sInputFocus, &sItems,
                       &sItemsPr, &sItemsCk, &sOff, &sAccent, &sKnob, &sScrollbar, &sPanel, &sCursor};
  for (lv_style_t* s : all) {
    if (made) {
      lv_style_reset(s);
    }
    lv_style_init(s);
  }
  made = true;
  lv_style_set_bg_color(&sScreen, color(t.bg));
  lv_style_set_bg_opa(&sScreen, LV_OPA_COVER);
  lv_style_set_text_color(&sScreen, color(t.text));

  lv_style_set_bg_color(&sCard, color(t.panel));
  lv_style_set_border_color(&sCard, color(t.beat));
  lv_style_set_text_color(&sCard, color(t.text));

  lv_style_set_bg_color(&sBtn, color(t.off));
  lv_style_set_text_color(&sBtn, color(t.text));
  lv_style_set_shadow_width(&sBtn, 0);
  lv_style_set_radius(&sBtn, 6);
  lv_style_set_pad_hor(&sBtn, 10);
  lv_style_set_pad_ver(&sBtn, 6);
  lv_style_set_bg_color(&sBtnPr, color(t.beat));
  lv_style_set_bg_color(&sBtnCk, color(t.accent));
  lv_style_set_text_color(&sBtnCk, color(t.bg));
  lv_style_set_bg_color(&sBtnDis, color(t.panel));
  lv_style_set_text_color(&sBtnDis, color(t.dim));

  lv_style_set_bg_color(&sInput, color(t.off));
  lv_style_set_bg_opa(&sInput, LV_OPA_COVER);
  lv_style_set_border_color(&sInput, color(t.beat));
  lv_style_set_border_width(&sInput, 1);
  lv_style_set_text_color(&sInput, color(t.text));
  lv_style_set_radius(&sInput, 6);
  lv_style_set_border_color(&sInputFocus, color(t.accent));
  lv_style_set_border_color(&sCursor, color(t.accent));

  lv_style_set_bg_color(&sPanel, color(t.panel));
  lv_style_set_bg_opa(&sPanel, LV_OPA_COVER);
  lv_style_set_border_color(&sPanel, color(t.beat));
  lv_style_set_text_color(&sPanel, color(t.text));

  lv_style_set_bg_color(&sItems, color(t.off));
  lv_style_set_text_color(&sItems, color(t.text));
  lv_style_set_shadow_width(&sItems, 0);
  lv_style_set_bg_color(&sItemsPr, color(t.beat));
  lv_style_set_bg_color(&sItemsCk, color(t.accent));
  lv_style_set_text_color(&sItemsCk, color(t.bg));

  lv_style_set_bg_color(&sOff, color(t.off));
  lv_style_set_bg_opa(&sOff, LV_OPA_COVER);
  lv_style_set_bg_color(&sAccent, color(t.accent));
  lv_style_set_bg_color(&sKnob, color(t.text));
  lv_style_set_bg_color(&sScrollbar, color(t.dim));

  // LVGL's own theme underneath (sizes, paddings, what each widget is made of), ours on top
  lv_theme_t* base = lv_theme_default_init(disp, color(t.accent), color(t.knob[3]), settings.theme != THEME_LIGHT,
                                           &lv_font_montserrat_14);
  th = *base;
  lv_theme_set_parent(&th, base);
  lv_theme_set_apply_cb(&th, apply);
  lv_display_set_theme(disp, &th);
}

// ------------------------------------------------------------------------------------- building blocks ---
lv_obj_t* ui::box(lv_obj_t* parent, lv_flex_flow_t flow) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(o, flow);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);  // a tap goes to what it's in
  return o;
}

lv_obj_t* ui::label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t c) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color(c), 0);
  return l;
}

lv_obj_t* ui::line(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t c) {
  lv_obj_t* l = label(parent, text, font, c);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_height(l, lv_font_get_line_height(font));
  return l;
}

lv_obj_t* ui::button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* data) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  if (cb) {
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
  }
  return b;
}

lv_obj_t* ui::header(lv_obj_t* page, const char* title, Builder back) {
  lv_obj_t* row = box(page, LV_FLEX_FLOW_ROW);
  lv_obj_set_height(row, HEAD_H);
  lv_obj_set_style_pad_right(row, EDGE_R - 4, 0);  // (FM Buddy 2.0: its right-hand buttons off the screen's edge)
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  if (back) {
    lv_obj_t* b = button(row, LV_SYMBOL_LEFT, [](lv_event_t* e) { go((Builder)lv_event_get_user_data(e)); }, (void*)back);
    lv_obj_set_size(b, HEAD_BTN_W, HEAD_BTN_H);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_20, 0);
  }
  if (title) {
    lv_obj_t* l = line(row, title, &lv_font_montserrat_20, theme().text);
    lv_obj_set_flex_grow(l, 1);
  }
  return row;
}

// ---- the scrollbar you can actually grab (FM Buddy 2.0) ---------------------------------------
// LVGL draws LV_PART_SCROLLBAR itself, but it is paint: there is no object there to press, so a
// finger cannot drag it. Every list on this device is driven by a fingertip on a 2.8" panel, where
// a 3 px bar is not something anyone can hit. So the bar is drawn here instead: a strip wide enough
// to land on, with the bar itself inside it, and the strip takes the press.
//
// The strip only takes presses while there is something to scroll. When a list fits on the screen
// the strip is out of the way entirely, so it never swallows a tap meant for the row beneath it.
//
// It lives on the PAGE, not inside the scroller, because pages rebuild their lists with
// lv_obj_clean() on the scroller -- a strip inside it would be destroyed with the rows, and the
// CHILD_CHANGED that clean sends would arrive pointing at freed memory.
#define BAR_W 12    // the bar you can see
#define GRIP_W 16   // the full-height strip, and the room reserved for it
#define THUMB_GROW 16  // how far the thumb's target reaches past the thumb, on every side
#define THUMB_MIN 44

static struct {
  lv_obj_t* sc;  // the scroller being dragged, null none
  int off;       // where on the thumb the finger landed, so it does not jump under the finger
} barDrag;

// how far it can scroll, how far it has, and how tall the thumb should be
static void barGeom(lv_obj_t* sc, int& range, int& at, int& track, int& thumb) {
  int top = lv_obj_get_scroll_top(sc), bottom = lv_obj_get_scroll_bottom(sc);
  top = top < 0 ? 0 : top;
  bottom = bottom < 0 ? 0 : bottom;
  range = top + bottom;
  at = top;
  track = lv_obj_get_content_height(sc);
  int content = track + range;
  thumb = content > 0 ? track * track / content : track;
  thumb = thumb < THUMB_MIN ? THUMB_MIN : thumb > track ? track : thumb;
}

// put the thumb where the scroll is, and take the strip out of the way when there is nothing to do
static void barSync(lv_obj_t* sc) {
  lv_obj_t* grip = (lv_obj_t*)lv_obj_get_user_data(sc);
  if (!grip || !lv_obj_is_valid(grip)) {
    return;
  }
  lv_obj_t* thumb = (lv_obj_t*)lv_obj_get_user_data(grip);
  if (!thumb || !lv_obj_is_valid(thumb)) {
    return;
  }
  int range, at, track, th;
  barGeom(sc, range, at, track, th);
  if (range <= 0) {
    lv_obj_add_flag(grip, LV_OBJ_FLAG_HIDDEN);  // nothing to scroll: neither takes a press
    lv_obj_add_flag(thumb, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(grip, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(thumb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(thumb, th);
  int y = track > th ? (track - th) * at / range : 0;
  // Both are placed from the scroller's own coordinates, every time. Relying on a layout event to
  // place the strip left it one pixel tall at the top corner: the thumb still worked, because it is
  // measured from the scroller, and the track silently took no presses at all
  lv_area_t sa, pa;
  lv_obj_get_coords(sc, &sa);
  lv_obj_get_content_coords(lv_obj_get_parent(thumb), &pa);
  int x = sa.x2 - pa.x1, top = sa.y1 - pa.y1, h = sa.y2 - sa.y1 + 1;
  lv_obj_set_pos(grip, x - GRIP_W + 1, top);
  lv_obj_set_height(grip, h);
  lv_obj_set_pos(thumb, x - BAR_W + 1, top + y);
}

// the strip is not inside the scroller, so it has to be put where the scroller is
static void barPlace(lv_obj_t* sc) {
  lv_obj_t* grip = (lv_obj_t*)lv_obj_get_user_data(sc);
  if (!grip || !lv_obj_is_valid(grip)) {
    return;
  }
  lv_area_t sa, pa;
  lv_obj_get_coords(sc, &sa);
  lv_obj_get_content_coords(lv_obj_get_parent(grip), &pa);
  lv_obj_set_pos(grip, sa.x2 - pa.x1 - GRIP_W + 1, sa.y1 - pa.y1);
  lv_obj_set_height(grip, sa.y2 - sa.y1 + 1);
}

static void barScrolled(lv_event_t* e) {
  lv_obj_t* sc = (lv_obj_t*)lv_event_get_target(e);
  if (lv_event_get_code(e) == LV_EVENT_DELETE) {
    if (barDrag.sc == sc) {
      barDrag.sc = nullptr;
    }
    return;
  }
  if (!lv_obj_is_valid(sc)) {
    return;
  }
  barPlace(sc);
  barSync(sc);
}

static void barPressed(lv_event_t* e) {
  lv_obj_t* sc = (lv_obj_t*)lv_event_get_user_data(e);
  lv_obj_t* grip = (lv_obj_t*)lv_obj_get_user_data(sc);  // the track, pressed or not: the ruler
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    barDrag.sc = nullptr;
    return;
  }
  lv_indev_t* in = lv_indev_active();
  if (!in) {
    return;
  }
  lv_point_t p;
  lv_indev_get_point(in, &p);
  int range, at, track, th;
  barGeom(sc, range, at, track, th);
  if (range <= 0 || track <= th) {
    return;
  }
  lv_area_t a;
  lv_obj_get_coords(grip, &a);
  int py = p.y - a.y1;  // where the finger is down the strip
  int thumbY = (track - th) * at / range;
  if (code == LV_EVENT_PRESSED) {
    barDrag.sc = sc;
    // landed on the thumb: keep it under the finger. Landed on the track: bring it to the finger
    barDrag.off = (py >= thumbY && py < thumbY + th) ? py - thumbY : th / 2;
  }
  if (barDrag.sc != sc) {
    return;
  }
  int want = py - barDrag.off;
  want = want < 0 ? 0 : want > track - th ? track - th : want;
  lv_obj_scroll_to_y(sc, (int)((long)want * range / (track - th)), LV_ANIM_OFF);
}

lv_obj_t* ui::scroller(lv_obj_t* page) {
  lv_obj_t* o = box(page);
  lv_obj_set_width(o, LV_PCT(100));
  lv_obj_set_flex_grow(o, 1);
  lv_obj_add_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(o, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);  // the one below replaces it
  // The rows stop before the STRIP, not merely before the bar. The strip takes presses, so any
  // control left underneath it would be a button a finger could not reach. Reserved always, not
  // only while the bar shows: padding that came and went with scrollability would reflow the
  // content, which can change how tall it is, which can change whether it scrolls at all
  lv_obj_set_style_pad_right(o, GRIP_W, 0);  // (the strip only; the thumb's wider target overhangs)

  lv_obj_t* grip = lv_obj_create(page);  // on the page: lv_obj_clean(scroller) must not destroy it
  lv_obj_add_flag(grip, LV_OBJ_FLAG_FLOATING);  // and the page's flex layout must not place it
  lv_obj_remove_flag(grip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(grip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(grip, GRIP_W, 1);  // put where the scroller is, by barPlace
  lv_obj_set_style_bg_opa(grip, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grip, 0, 0);
  lv_obj_set_style_pad_all(grip, 0, 0);
  lv_obj_set_style_radius(grip, 0, 0);

  // The thumb is the grip's SIBLING, not its child. A child's press area is clipped to its parent,
  // and the whole point of the thumb is that its target reaches well past it: THUMB_GROW on every
  // side, so a fingertip landing near the bar still catches it. That wide target only exists beside
  // the thumb, so the rows lose room to the strip (GRIP_W) and to nothing else
  lv_obj_t* thumb = lv_obj_create(page);
  lv_obj_add_flag(thumb, LV_OBJ_FLAG_FLOATING);
  lv_obj_remove_flag(thumb, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(thumb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(thumb, THUMB_GROW);
  lv_obj_set_size(thumb, BAR_W, THUMB_MIN);
  lv_obj_set_style_bg_color(thumb, color(theme().dim), 0);
  lv_obj_set_style_bg_opa(thumb, LV_OPA_70, 0);
  lv_obj_set_style_border_width(thumb, 0, 0);
  lv_obj_set_style_radius(thumb, BAR_W / 2, 0);
  lv_obj_set_style_pad_all(thumb, 0, 0);

  lv_obj_set_user_data(grip, thumb);
  lv_obj_set_user_data(o, grip);
  for (lv_obj_t* who : {grip, thumb}) {
    lv_obj_add_event_cb(who, barPressed, LV_EVENT_PRESSED, o);
    lv_obj_add_event_cb(who, barPressed, LV_EVENT_PRESSING, o);
    lv_obj_add_event_cb(who, barPressed, LV_EVENT_RELEASED, o);
    lv_obj_add_event_cb(who, barPressed, LV_EVENT_PRESS_LOST, o);
  }
  lv_obj_add_event_cb(o, barScrolled, LV_EVENT_SCROLL, nullptr);
  lv_obj_add_event_cb(o, barScrolled, LV_EVENT_SIZE_CHANGED, nullptr);
  lv_obj_add_event_cb(o, barScrolled, LV_EVENT_CHILD_CHANGED, nullptr);
  lv_obj_add_event_cb(o, barScrolled, LV_EVENT_DELETE, nullptr);
  lv_obj_add_event_cb(o, barScrolled, LV_EVENT_LAYOUT_CHANGED, nullptr);
  barPlace(o);
  barSync(o);
  return o;
}

lv_obj_t* ui::listRow(lv_obj_t* parent, lv_event_cb_t cb, void* data) {
  const Theme& t = theme();
  lv_obj_t* r = box(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_height(r, ROW_H);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(r, 6, 0);
  lv_obj_set_style_pad_column(r, 6, 0);
  lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(r, 1, 0);
  lv_obj_set_style_border_color(r, color(t.off), 0);
  lv_obj_set_style_bg_color(r, color(t.off), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
  if (cb) {
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, data);
  }
  return r;
}

lv_obj_t* ui::chip(lv_obj_t* parent, const char* text, uint32_t c) {
  lv_obj_t* l = label(parent, text, &lv_font_montserrat_12, c);
  lv_obj_set_style_border_color(l, color(c), 0);
  lv_obj_set_style_border_width(l, 1, 0);
  lv_obj_set_style_radius(l, 8, 0);
  lv_obj_set_style_pad_hor(l, 6, 0);
  lv_obj_set_style_pad_ver(l, 1, 0);
  return l;
}

// (FM Buddy 2.0, 2026-11-14) at the top right, over the bar's end, and quickly gone: an indicator (an octave, a
// drum, "Saved") 250 ms; a longer message long enough to read (1.5 s); a problem 4 s. One at a time: a new one
// takes the last one's place
static lv_obj_t* lastToast;
int hal::screenW = SCREEN_LONG, hal::screenH = SCREEN_SHORT;  // (landscape until the screen starts: screenTurn)
void ui::toast(const char* text, bool bad) {
  const Theme& t = theme();
  if (lastToast) {
    lv_obj_delete(lastToast);
  }
  lv_obj_t* l = lastToast = label(lv_layer_top(), text, &lv_font_montserrat_14, bad ? t.text : t.bg);
  lv_obj_set_style_bg_color(l, color(bad ? 0xc02020 : t.accent), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(l, 6, 0);
  lv_obj_set_style_pad_hor(l, 8, 0);
  lv_obj_set_style_pad_ver(l, 5, 0);
  lv_obj_set_style_max_width(l, SCREEN_W * 2 / 3, 0);
  lv_obj_set_width(l, LV_SIZE_CONTENT);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -EDGE_R, EDGE_T + 2);
  lv_obj_add_event_cb(l, [](lv_event_t* e) {
    if (lastToast == lv_event_get_target(e)) {
      lastToast = 0;
    }
  }, LV_EVENT_DELETE, 0);
  size_t n = strlen(text);
  lv_obj_delete_delayed(l, bad ? 4000 : n <= 24 ? 250 : 1500);
}

// --------------------------------------------------------------------------------------------- frame ---
static void railStatus() {
  const Theme& t = theme();
  lv_obj_set_style_text_color(stSd, color(hal::sd() ? t.dim : t.knob[0]), 0);
  bool wifi = hal::wifiOn() || (jobs::running && (jobs::step == jobs::JOINING || jobs::step == jobs::FETCHING));
  lv_obj_set_style_text_color(stWifi, color(wifi ? t.accent : t.dim), 0);
  lv_obj_set_style_opa(stWifi, wifi ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color(stBt, color(t.accent), 0);
  lv_obj_set_style_opa(stBt, hal::bleIsOn() ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  int d = fm1::current();
  const Fm1Id* id = fm1::known(d);
  lv_obj_set_style_text_color(stFm1, color(!id ? t.dim : id->ota() ? t.knob[1]
                                                                   : t.accent),
                              0);
  const char* name = !id ? "NO" : isSmk(id->model) ? "SMK"
                                                   : "FM-1";  // (what's on the hub: an FM-1, an SMK-37, none)
  if (hal::onComputer()) {  // (FM Buddy 2.0: a computer on the USB-C port instead of the hub: its serial port or USB MIDI)
    name = "USB";
    lv_obj_set_style_text_color(stFm1, color(t.accent), 0);
  }
  if (strcmp(lv_label_get_text(stFm1), name)) {
    lv_label_set_text(stFm1, name);
  }
}

std::string ui::devText(const std::string& text) {
  const char* name = fm1::machineName();
  if (!strcmp(name, "FM-1")) {
    return text;
  }
  std::string o;
  size_t at = 0;
  for (size_t k; (k = text.find("FM-1", at)) != std::string::npos; at = k + 4) {
    char before = k ? text[k - 1] : ' ', after = k + 4 < text.size() ? text[k + 4] : ' ';
    bool word = !isalnum((unsigned char)before) && before != '-' && !isalnum((unsigned char)after) && after != '_' &&
                after != '+' && after != '-';
    o += text.substr(at, k - at) + (word ? name : "FM-1");
  }
  return o + text.substr(at);
}

static void markTab() {
  for (int i = 0; i < TABS; i++) {
    if (i == tab) {
      lv_obj_add_state(tabs[i], LV_STATE_CHECKED);
    } else {
      lv_obj_remove_state(tabs[i], LV_STATE_CHECKED);
    }
  }
}

void ui::fullscreen(bool on) {
  if (on) {
    lv_obj_add_flag(rail, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_HIDDEN);
  }
  if (SCREEN_PORTRAIT) {  // (the rail along the bottom)
    lv_obj_set_pos(pageObj, 0, 0);
    lv_obj_set_size(pageObj, SCREEN_W, on ? SCREEN_H : PAGE_H);
    return;
  }
  lv_obj_set_pos(pageObj, on ? 0 : RAIL_W, 0);
  lv_obj_set_width(pageObj, on ? SCREEN_W : PAGE_W);
}

void ui::go(Builder b) {
  hal::bleSearch(false);  // only while the MIDI page shows
  tick = 0;
  jobDone = 0;
  current = b;
  fullscreen(false);
  lv_obj_clean(pageObj);
  lv_obj_set_style_pad_all(pageObj, 6, 0);  // (a page may change them: the touch controller)
  // The right is the screen's own edge (the rail has the left), so it keeps only what the bars keep: a row used
  // to stop six pixels short of the glass while the rail answered two pixels in
  lv_obj_set_style_pad_right(pageObj, EDGE_BAR, 0);
  lv_obj_set_style_pad_top(pageObj, 6 + EDGE_T, 0);  // (FM Buddy 2.0: off the screen's top and bottom edges)
  lv_obj_set_style_pad_bottom(pageObj, EDGE_B, 0);  // (6 + EDGE_B before: ten dead pixels along the bottom)
  lv_obj_set_style_pad_row(pageObj, 4, 0);
  lv_obj_scroll_to_y(pageObj, 0, LV_ANIM_OFF);
  b(pageObj);
}

void ui::go(Tab t, Builder b) {
  if (t != SETTINGS) {
    settingsLeft();
  }
  tab = t;
  markTab();
  go(b);
}

static void tabClicked(lv_event_t* e) {
  static const Builder home[] = {firmwarePage, midiPage, soundsPage, settingsPage};
  Tab t = (Tab)(intptr_t)lv_event_get_user_data(e);
  go(t, home[t]);
}

static void build() {
  const Theme& t = theme();
  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  bool portrait = SCREEN_PORTRAIT;  // (FM Buddy 2.0: the rail along the bottom, its tabs in a row)
  rail = ui::box(scr, portrait ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(rail, portrait ? SCREEN_W : RAIL_W, portrait ? RAIL_BOTTOM_H : SCREEN_H);
  if (portrait) {
    lv_obj_set_pos(rail, 0, SCREEN_H - RAIL_BOTTOM_H);
  }
  lv_obj_set_style_bg_color(rail, color(t.panel), 0);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_top(rail, 4, 0);  // (FM Buddy 2.0: FM-1/SMK on top, then the tabs, off the top edge)
  lv_obj_set_style_pad_bottom(rail, 2, 0);
  lv_obj_set_style_pad_left(rail, RAIL_IN, 0);
  lv_obj_set_style_pad_right(rail, 2, 0);
  lv_obj_set_style_pad_row(rail, SCREEN_BIG ? 4 : 1, 0);
  lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (portrait) {
    lv_obj_set_style_pad_all(rail, 2, 0);
    lv_obj_set_style_pad_bottom(rail, 2 + EDGE_B, 0);  // (off the bottom edge)
    lv_obj_set_style_pad_left(rail, 4, 0);
    lv_obj_set_style_pad_column(rail, 2, 0);
  }
  static const char* icons[] = {LV_SYMBOL_DOWNLOAD, LV_SYMBOL_AUDIO, LV_SYMBOL_DIRECTORY, LV_SYMBOL_SETTINGS};
  // (FM Buddy 2.0) each tab its own colour when it's the one open: firmware blue, MIDI green, sounds amber, settings
  // pink; its icon in it, on a tint of it
  static const uint32_t TAB_COLOR[] = {0x4aa8ff, 0x3ad6a0, 0xffc21f, 0xff6b8b};
  bool controller = usbMode() == USB_CONTROLLER;  // (FM Buddy 2.0: the controller alone: no firmware, no sounds)
  // what's on the hub (FM-1, SMK, NO): at the top (FM Buddy 2.0: seen first; the tabs a little lower)
  stFm1 = label(rail, "NO", &lv_font_montserrat_14, t.dim);
  lv_obj_set_style_margin_bottom(stFm1, portrait ? 0 : SCREEN_BIG ? 4 : 2, 0);
  if (portrait) {
    lv_obj_set_width(stFm1, 36);
    lv_obj_set_style_text_font(stFm1, &lv_font_montserrat_12, 0);
  }
  for (int i = 0; i < TABS; i++) {
    tabs[i] = button(rail, icons[i], tabClicked, (void*)(intptr_t)i);
    lv_obj_set_size(tabs[i], RAIL_W - RAIL_IN - 2, RAIL_TAB_H);  // (the rail's width: easy to hit)
    if (portrait) {
      lv_obj_set_size(tabs[i], 40, RAIL_BOTTOM_H - 4 - EDGE_B);
      lv_obj_set_flex_grow(tabs[i], 1);
      lv_obj_set_style_pad_all(tabs[i], 0, 0);
      lv_obj_set_style_text_font(tabs[i], &lv_font_montserrat_20, 0);
    }
    lv_obj_set_style_bg_opa(tabs[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(tabs[i], color(t.dim), 0);
    lv_obj_set_style_text_font(tabs[i], portrait ? &lv_font_montserrat_20 : &lv_font_montserrat_28, 0);
    lv_obj_set_style_bg_opa(tabs[i], LV_OPA_30, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tabs[i], color(TAB_COLOR[i]), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tabs[i], color(TAB_COLOR[i]), LV_STATE_CHECKED);
    if (controller && (i == FIRMWARE || i == SOUNDS)) {
      lv_obj_add_flag(tabs[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  lv_obj_t* gap = ui::box(rail);
  lv_obj_set_flex_grow(gap, portrait ? 0 : 1);
  if (portrait) {
    lv_obj_set_size(gap, 0, 0);  // (the tabs share the row)
  }
  // Wi-Fi, Bluetooth and the SD card in one row (FM Buddy 2.0: room for the bigger tabs), and the battery when
  // there's one to show (batteryWatch): it wraps to a row of its own then
  lv_obj_t* radios = ui::box(rail, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_width(radios, portrait ? 40 : RAIL_W - RAIL_IN - 2);
  lv_obj_set_flex_align(radios, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(radios, 2, 0);
  stWifi = label(radios, LV_SYMBOL_WIFI, &lv_font_montserrat_16, t.dim);
  stBt = label(radios, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_16, t.dim);
  lv_obj_set_style_opa(stBt, LV_OPA_TRANSP, 0);
  stPower = radios;
  stSd = label(stPower, LV_SYMBOL_SD_CARD, &lv_font_montserrat_16, t.dim);
  stBat = label(stPower, LV_SYMBOL_BATTERY_FULL, &lv_font_montserrat_12, t.dim);
  stBolt = label(stPower, LV_SYMBOL_CHARGE, &lv_font_montserrat_12, t.accent);
  lv_obj_add_flag(stBat, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(stBolt, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_margin_left(stBolt, -2, 0);

  pageObj = ui::box(scr);
  lv_obj_set_size(pageObj, PAGE_W, PAGE_H);
  lv_obj_set_pos(pageObj, portrait ? 0 : RAIL_W, 0);
  lv_obj_set_style_pad_all(pageObj, 6, 0);
  lv_obj_set_style_pad_right(pageObj, EDGE_BAR, 0);
  lv_obj_set_style_pad_top(pageObj, 6 + EDGE_T, 0);
  lv_obj_set_style_pad_bottom(pageObj, EDGE_B, 0);  // (6 + EDGE_B before: ten dead pixels along the bottom)
  lv_obj_set_style_pad_row(pageObj, 4, 0);

  markTab();
  railStatus();
}

// each device's messages, in the log once it has been quiet for 2 s (to count them after a test)
static void logCounts() {
  static uint32_t logged[MIDI_DEVS], dropped[MIDI_DEVS];
  uint32_t now = hal::millis();
  for (int d = 0; d < MIDI_DEVS; d++) {
    midi::Dev v;
    if (!midi::dev(d, v)) {
      logged[d] = dropped[d] = 0;
      continue;
    }
    if ((v.count != logged[d] || v.dropped != dropped[d]) && now - v.lastIn > 2000) {
      logged[d] = v.count;
      dropped[d] = v.dropped;
      hal::log("midi: %s sent %u, %u to it dropped", v.name, (unsigned)v.count, (unsigned)v.dropped);
    }
  }
}

// ---------------------------------------------------------------------------------------------- sleep ---
// with nothing going on for a while (Settings > Sleep), the screen dims, and 30 s later the board sleeps until it's
// touched. Never while a job runs, a take records, the clock plays, a computer is on the USB port, or a device is on
// the hub or on Bluetooth (the hub on the charger, or someone playing)
const uint8_t ui::SLEEP_MINUTES[SLEEPS] = {0, 2, 5, 15};
#define DIM_MS 30000

int ui::sleepMinutes() {
  uint8_t m = 0;  // never, unless it's asked for
  hal::load("sleep", &m, 1);
  return m;
}

static bool busy() {
  if (jobs::running || record::recording() || play::playing() || hal::onComputer() || hal::diskMode() ||
      hal::recoveryMode()) {
    return true;
  }
  midi::Dev d;
  for (int i = 0; i < MIDI_TOUCH; i++) {
    if (midi::dev(i, d)) {
      return true;
    }
  }
  return false;
}

static lv_obj_t* dimmed;  // over everything while it's dim: the touch that wakes it does nothing else

static void undim() {
  if (dimmed) {
    lv_obj_delete(dimmed);
    dimmed = 0;
    hal::backlight(settings.brightness);
  }
}

static void sleepWatch() {
  static uint32_t quietSince = hal::millis();
  static int minutes = sleepMinutes();
  static uint32_t checked;
  if (hal::millis() - checked > 1000) {  // (the setting, now and then)
    checked = hal::millis();
    minutes = sleepMinutes();
  }
  if (!minutes || busy() || lv_display_get_inactive_time(0) < 1000) {
    quietSince = hal::millis();
  }
  uint32_t quiet = hal::millis() - quietSince, after = minutes * 60000u;
  if (!minutes || quiet < after - DIM_MS) {
    undim();
    return;
  }
  if (!dimmed) {
    hal::log("sleep: dim, sleeping in %d s", DIM_MS / 1000);
    hal::backlight(settings.brightness / 5 > 2 ? settings.brightness / 5 : 2);
    dimmed = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(dimmed);
    lv_obj_set_size(dimmed, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(dimmed, LV_OBJ_FLAG_CLICKABLE);
  }
  if (quiet >= after) {
    hal::log("sleep: %d min with nothing going on", minutes);
    quietSince = hal::millis();  // (the simulator doesn't sleep)
    hal::sleep();
  }
}

// --------------------------------------------------------------------------------------------- battery ---
// shown only while there's a battery to show. With none, the charger holds the battery socket at about 4.12 V (measured
// on a CYD-S3); a battery under the board's load reads below 4.05 V. So below that, a battery runs it, or one that
// isn't full is charging (the voltage rising: a bolt). Above: no battery, or a full one on the charger: nothing shown
#define BAT_SHOW 4050
#define BAT_HIDE 4100

// a LiPo under load: mV to percent
static int batteryPercent(float mv) {
  static const int CURVE[][2] = {{4080, 100}, {4000, 85}, {3900, 70}, {3800, 50}, {3700, 30}, {3600, 15}, {3500, 5}, {3400, 0}};
  if (mv >= CURVE[0][0]) {
    return 100;
  }
  for (int i = 1; i < 8; i++) {
    if (mv >= CURVE[i][0]) {
      return CURVE[i][1] + (int)((mv - CURVE[i][0]) * (CURVE[i - 1][1] - CURVE[i][1]) / (CURVE[i - 1][0] - CURVE[i][0]));
    }
  }
  return 0;
}

static void batteryWatch() {
  static uint32_t at, trendAt, loggedAt;
  static float avg, before;
  static bool shown, charging;
  if (hal::millis() - at < 1000) {
    return;  // (once a second)
  }
  at = hal::millis();
  uint32_t mv = hal::batteryMv();
  if (!mv) {
    return;
  }
  avg = avg ? avg * 0.9f + mv * 0.1f : mv;
  if (hal::millis() - trendAt > 60000) {  // charging: risen over the last minute
    charging = before && avg - before > 8;
    before = avg;
    trendAt = hal::millis();
  }
  bool was = shown;
  shown = shown ? avg < BAT_HIDE : avg < BAT_SHOW;
  if (shown != was || hal::millis() - loggedAt > 300000) {
    hal::log("battery: %d mV, %d%%%s%s", (int)avg, batteryPercent(avg), charging ? ", charging" : "", shown ? "" : " (not shown)");
    loggedAt = hal::millis();
  }
  if (!shown) {
    lv_obj_add_flag(stBat, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(stBolt, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  int pct = batteryPercent(avg);
  const char* glyph = pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                                                      : pct > 30   ? LV_SYMBOL_BATTERY_2
                                                      : pct > 10   ? LV_SYMBOL_BATTERY_1
                                                                   : LV_SYMBOL_BATTERY_EMPTY;
  if (strcmp(lv_label_get_text(stBat), glyph)) {
    lv_label_set_text(stBat, glyph);
  }
  const Theme& t = theme();
  lv_obj_set_style_text_color(stBat, color(pct <= 10 && !charging ? t.knob[0] : t.dim), 0);
  if (charging) {
    lv_obj_remove_flag(stBolt, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(stBolt, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(stBat, LV_OBJ_FLAG_HIDDEN);
}

// ------------------------------------------------------------------------------------ the firmwares ---
// at power-on, on a thread of their own (the screen answers meanwhile): the Hub's list (its short copy, catalog::loadHub,
// in a moment; without one, the whole list, seconds, which writes the copy for next time), then the SD card's
// firmwares. The UI's thread takes them once they're there (bootWatch)
static FwList bootHub, bootSd;
static std::string bootUpdated;
static std::atomic<bool> bootDone{false};
static bool bootRunning;
static bool hubNew;      // the Hub's list came new since: the one read at power-on is old
static int bootUnknown;  // the card's files without a sha256 yet

static void bootBegin() {
  bootRunning = true;
  hal::spawn(
      [](void*) {
        uint32_t t = hal::millis();
        std::string err;
        bool kept = catalog::loadHub(bootHub, bootUpdated);
        if (!kept) {
          catalog::parseHub(bootHub, bootUpdated, err);
        }
        uint32_t hubMs = hal::millis() - t;
        bootUnknown = catalog::scanSdInto(bootHub, bootSd);
        hal::log("boot: hub %u firmwares %s (%u ms)%s%s, sd %u (%u ms)", (unsigned)bootHub.size(), kept ? "kept" : "read",
                 (unsigned)hubMs, err.empty() ? "" : ": ", err.c_str(), (unsigned)bootSd.size(),
                 (unsigned)(hal::millis() - t - hubMs));
        bootDone = true;
      },
      0);
}

static void bootWatch() {
  if (!bootRunning || !bootDone) {
    return;
  }
  bootRunning = false;
  if (hubNew) {  // (a newer list came meanwhile: the card's firmwares again, with it)
    catalog::scanSd();
  } else {
    catalog::hub.swap(bootHub);
    catalog::hubUpdated = bootUpdated;
    catalog::sd.swap(bootSd);
  }
  FwList().swap(bootHub);
  FwList().swap(bootSd);
  if (bootUnknown) {
    catalog::hashSd();
  }
  if (current == firmwarePage) {
    go(firmwarePage);
  }
}

static void watch(lv_timer_t*) {
  if (!bootRunning && catalog::sdHashed()) {  // (more of the card's firmwares known: what they are)
    catalog::scanSd();
    if (current == firmwarePage) {
      firmwareListAgain();
    }
  }
  if (jobs::bluetoothPaused()) {
    toast("Bluetooth MIDI is paused: Wi-Fi is on.");
  }
  if (seenFinished != jobs::finished) {  // (a quick one may start and end between two looks)
    seenFinished = jobs::finished;
    if (jobs::ok && jobs::kind == jobs::HUB) {
      catalog::hub.swap(jobs::hub);
      FwList().swap(jobs::hub);  // the old list
      hubNew = true;
      catalog::hubUpdated = jobs::hubUpdated;
      catalog::scanSd();
    } else if (jobs::kind == jobs::DOWNLOAD || jobs::kind == jobs::INSTALL || jobs::kind == jobs::FW_UPDATE) {
      catalog::scanSd();
    }
    if (jobDone) {
      jobDone();
    }
  }
  fm1::watch();
  uint32_t fm1s = 0;  // the arpeggiator and chords leave the FM-1s' notes alone
  for (int d = 0; d < MIDI_DEVS; d++) {
    fm1s |= fm1::known(d) ? 1u << d : 0;
  }
  play::skip(fm1s);
  logCounts();
  if (tick) {
    tick();
  }
  railStatus();
  bootWatch();
  batteryWatch();
  sleepWatch();
}

const char* const ui::START_NAMES[STARTS] = {"Firmware: SD card", "MIDI", "Mapping", "Touch: X/Y pad",
                                             "Manager", "Firmware: Hub", "Touch: pads", "Big Screen",
                                             "Faders", "Knobs", "Sequencer"};
const Start ui::START_ORDER[STARTS] = {START_FIRMWARE, START_HUB, START_MIDI, START_MAPPING,
                                       START_TOUCH, START_PADS, START_FADERS, START_KNOBS,
                                       START_SEQ, START_SCREEN, START_SOUNDS};

const char* const ui::USB_MODE_NAMES[USB_MODES] = {"Auto", "FM Buddy", "Controller"};

int ui::usbMode() {
  if (!hal::computerMidi()) {
    return USB_FMBUDDY;  // (the serial port on a computer is FM Buddy, whatever was picked: a computer can ask for it)
  }
  uint8_t m = USB_AUTO;  // (USB MIDI picked before there were modes: Auto)
  hal::load("usbmode", &m, 1);
  return m == USB_CONTROLLER ? USB_CONTROLLER : USB_AUTO;
}

void ui::usbModeSet(int m) {
  uint8_t b = (uint8_t)m;
  hal::save("usbmode", &b, 1);
  bool midi = m != USB_FMBUDDY;
  if (midi != hal::computerMidi()) {
    hal::computerMidiSet(midi);
  }
}

bool ui::controllerStart() {
  int m = usbMode();
  return m == USB_CONTROLLER || (m == USB_AUTO && hal::onComputer());
}

int ui::startPage() {
  uint8_t s = 0;
  hal::load("start", &s, 1);
  return s < STARTS ? s : 0;
}

void ui::rebuild() {
  lv_async_call(
      [](void*) {
        setTheme(lv_display_get_default());
        build();
        go(current);
      },
      0);
}

// the frame: the rail, the start page's tab lit, its page empty. Drawn as soon as there's a screen, so the app is there
// at once: the rest comes in a moment (ui::begin)
void ui::frame() {
  settingsLoad();
  setTheme(lv_display_get_default());
  build();
  static const Tab tabs[STARTS] = {FIRMWARE, MIDI, MIDI, MIDI, SOUNDS, FIRMWARE, MIDI, SOUNDS, MIDI, MIDI, MIDI};
  lv_obj_add_state(::tabs[controllerStart() ? MIDI : tabs[startPage()]], LV_STATE_CHECKED);
  lv_refr_now(0);
  hal::backlight(settings.brightness);  // (the light on once it's drawn: not a lit black screen meanwhile)
  hal::log("boot: frame on screen at %u ms", (unsigned)hal::millis());
}

void ui::begin() {
  hal::TouchCal cal;
  if (hal::load("touchcal", &cal, sizeof cal)) {
    hal::touchCal(&cal);
  }
  bool bt = false;
  if (!hal::recoveryMode() && hal::load("bluetooth", &bt, 1) && bt) {
    hal::bleOn(true);
  }
  play::begin();
  seqDriveBegin();  // (FM Buddy 2.0: the sequencer's engine and its clock thread, seqdrive.cpp)
  bool routing = false;  // MIDI routing from power-on (its own switch on the MIDI page)
  hal::load("routing", &routing, 1);
  if (!hal::diskMode() && !hal::recoveryMode()) {
    midi::loadMap();
  }
  midi::route(routing && !hal::diskMode() && !hal::recoveryMode());
  if (hal::diskMode() || hal::recoveryMode()) {
    go(hal::diskMode() ? diskPage : recoverPage);
  } else {
    midi::mount(MIDI_TOUCH, 0, 0, "Touch");  // the touchscreen, as a controller that's always there
    static const Tab tabs[STARTS] = {FIRMWARE, MIDI, MIDI, MIDI, SOUNDS, FIRMWARE, MIDI, SOUNDS, MIDI, MIDI, MIDI};
    static const Builder pages[STARTS] = {firmwarePage, midiPage, mapPage, touchPage, soundsPage, firmwarePage, touchPage,
                                          mirrorPage, fadersPage, knobsPage, seqPage};
    controls::get();  // (FM Buddy 2.0: the controls, listening for feedback from power-on)
    int s = startPage();
    if (s == START_FIRMWARE || s == START_HUB) {
      firmwareSource(s == START_HUB);
    } else if (s == START_TOUCH || s == START_PADS) {
      touchMode(s == START_PADS);
    }
    if (controllerStart()) {
      go(MIDI, fadersPage);  // (FM Buddy 2.0: the MIDI controller starts on the faders)
    } else {
      go(tabs[s], pages[s]);
    }
  }
  lv_timer_create(watch, 100, 0);
  lv_refr_now(0);
  hal::log("boot: on screen at %u ms", (unsigned)hal::millis());
  if (!hal::diskMode()) {  // (the computer has the card then: nothing here touches it)
    bootBegin();
  }
}
