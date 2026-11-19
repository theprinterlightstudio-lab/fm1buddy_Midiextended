// The MIDI controls as the app plays them (controls.h), and the encoder (ui.h)
#include "controls.h"

#include <lvgl.h>

#include "hal.h"
#include "midi.h"
#include "ui.h"

#define KEEP_AFTER_MS 3000  // positions: kept this long after the last change
#define HEAR_MS 20          // incoming CCs (feedback): taken up this often
#define HEAR_MAX 32         // ... this many at a time at most

static MidiControls ctl;
static bool loaded, dirty;
static uint32_t dirtyAt;
static int shownGroup = -1;
static void (*shower)(int i);

// feedback: CCs a device sent (the computer in USB MIDI mode, the hub's, Bluetooth's) move the absolute controls on
// their channel and number, and nothing is sent back (midictl.h ORIGIN_MIDI)
static void hear(lv_timer_t*) {
  uint8_t st, cc, v;
  int changed[CTL_COUNT];
  for (int k = 0; k < HEAR_MAX && midi::ccHeard(st, cc, v); k++) {
    int n = ctl.feedback(st, cc, v, changed);
    if (n) {
      controls::moved();
    }
    for (int j = 0; j < n && shower; j++) {
      shower(changed[j]);
    }
  }
}

static void keepLater(lv_timer_t*) {
  if (dirty && hal::millis() - dirtyAt >= KEEP_AFTER_MS) {
    controls::save();
  }
}

MidiControls& controls::get() {
  if (!loaded) {
    loaded = true;
    CtlSetup s;
    bool found = hal::load(CTL_KEY, &s, sizeof s);
    static uint8_t old[sizeof(CtlSetup)];
    if (!found && midictl::v1Size() <= sizeof old && hal::load(CTL_KEY, old, midictl::v1Size()) &&
        midictl::upgrade(old, midictl::v1Size(), s)) {
      hal::log("controls: version 1's settings upgraded");  // (kept as this version at once, below)
      midictl::adoptNewDefaults(s);
      ctl.begin(s);
      save();
    } else {
      int fixed = midictl::restore(s, found);
      if (fixed > 0) {
        hal::log("controls: %d kept settings were out of range: fixed", fixed);
      }
      int adopted = found ? midictl::adoptNewDefaults(s) : 0;
      if (adopted) {
        hal::log("controls: %d untouched fader banks take the new defaults (pitch bend)", adopted);
      }
      ctl.begin(s);
      if (adopted) {
        save();
      }
    }
    lv_timer_create(keepLater, 1000, 0);
    lv_timer_create(hear, HEAR_MS, 0);
  }
  return ctl;
}

void controls::save() {
  hal::save(CTL_KEY, &get().setup(), sizeof(CtlSetup));
  dirty = false;
}

void controls::moved() {
  dirty = true;
  dirtyAt = hal::millis();
}

// a control's message out, as the Touch device: on its channel, or on the Global one (CH_GLOBAL, FM Buddy 2.0)
static void send(int i, const uint8_t* m) {
  if (i >= 0 && i < CTL_COUNT && controls::get().setup().ctl[i].ch == CH_GLOBAL && m[0] >= 0x80 && m[0] < 0xF0) {
    uint8_t o[3] = {(uint8_t)((m[0] & 0xF0) | controls::globalChannel()), m[1], m[2]};
    midi::bytesIn(MIDI_TOUCH, o, 3);
    return;
  }
  midi::bytesIn(MIDI_TOUCH, m, 3);
}

int controls::globalChannel() {
  return ctlGlobal(get().setup().global);
}

void controls::setGlobalChannel(int ch) {
  get().setGlobal((uint8_t)((ch & 15) + 1));
  save();
}

int controls::turn(int i, int detents) {
  uint8_t m[CTL_MSGS][3];
  int n = get().move(i, detents, m);
  for (int k = 0; k < n; k++) {
    send(i, m[k]);
  }
  moved();
  return n;
}

bool controls::play(int i, uint8_t pos) {
  uint8_t m[3];
  bool sent = get().set(i, pos, ORIGIN_USER, m);
  if (sent) {
    send(i, m);
  }
  moved();
  return sent;
}

void controls::showing(int group, void (*show)(int i)) {
  shownGroup = group;
  shower = show;
  if (group < 0 && dirty) {
    save();  // (leaving the page: what it moved is kept now)
  }
}

// ------------------------------------------------------------------------------------------ the encoder ---
// turned: the selected control (its page in select mode, and showing), else the encoder's own
void ui::encoder(int detents) {
  if (!detents) {
    return;
  }
  uint8_t m[CTL_MSGS][3];
  int moved;
  int n = controls::get().turn(detents, shownGroup, m, &moved);
  for (int k = 0; k < n; k++) {
    send(moved, m[k]);
  }
  controls::moved();
  if (shower) {
    shower(moved);
  }
}

// pushed: in select mode, the next control of the bank on the screen. Held: the page's mode, play <-> select
void ui::encoderPush(bool held) {
  if (shownGroup < 0) {
    return;
  }
  MidiControls& c = controls::get();
  if (held) {
    c.mode(shownGroup, c.mode(shownGroup) == MODE_PLAY ? MODE_SELECT : MODE_PLAY);
    controls::save();
    if (shower) {
      shower(-1);
    }
  } else if (c.mode(shownGroup) == MODE_SELECT) {
    int was = c.selected();
    c.selectNext(shownGroup);
    controls::save();
    if (shower) {
      shower(was);
      shower(c.selected());
    }
  }
}
