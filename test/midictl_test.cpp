// the MIDI controls' model (midictl.h): scaling, sending, feedback, banks, the encoder, and what's kept. tools/unit-test.sh
// builds and runs it
#include "midictl.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(c)                                     \
  do {                                               \
    if (!(c)) {                                      \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++;                                       \
    }                                                \
  } while (0)

static CtlConfig cfg(uint8_t cc, uint8_t ch, uint8_t lo, uint8_t hi, uint8_t invert) {
  CtlConfig c{};
  c.cc = cc, c.ch = ch, c.lo = lo, c.hi = hi, c.invert = invert;
  return c;
}

static MidiControls fresh() {
  CtlSetup s;
  midictl::defaults(s);
  MidiControls m;
  m.begin(s);
  return m;
}

static bool msgIs(const uint8_t* m, uint8_t a, uint8_t b, uint8_t c) {
  return m[0] == a && m[1] == b && m[2] == c;
}

static void testDefaults() {
  CtlSetup s;
  midictl::defaults(s);
  CHECK(s.version == CTL_VERSION);
  CHECK(s.bank[GROUP_FADERS] == 0 && s.bank[GROUP_KNOBS] == 0 && s.step == 1 && s.selected == CTL_NONE);
  CHECK(s.mode[GROUP_FADERS] == MODE_PLAY && s.mode[GROUP_KNOBS] == MODE_PLAY);
  // bank 1, as asked: pitch bend (back to the middle when let go), modulation, expression, cutoff
  CHECK(s.ctl[0].kind == KIND_PB && s.ctl[0].rel == 1 && s.pos[0] == 64);
  CHECK(s.ctl[1].cc == 1 && s.ctl[2].cc == 11 && s.ctl[3].cc == 74);
  CHECK(!strcmp(s.ctl[0].label, "Pitch bend") && !strcmp(s.ctl[1].label, "Mod wheel") &&
        !strcmp(s.ctl[2].label, "Expression") && !strcmp(s.ctl[3].label, "Cutoff"));
  CHECK(s.ctl[12].cc == 71 && !strcmp(s.ctl[12].label, "Resonance"));  // (resonance: bank 4)
  for (int i = 0; i < CTL_COUNT; i++) {
    CHECK(s.ctl[i].ch == 0 && s.ctl[i].lo == 0 && s.ctl[i].hi == 127 && !s.ctl[i].invert);
    CHECK(i == 0 || (s.ctl[i].kind == KIND_ABS && s.ctl[i].rel == REL_OFFSET));
    CHECK(s.ctl[i].cc2 == s.ctl[i].cc && s.ctl[i].incVal == 127);
    CHECK(s.ctl[i].cc <= 127 && s.pos[i] <= 127);
    CtlConfig c = s.ctl[i];
    CHECK(midictl::sanitize(c, s.ctl[i]) == 0);  // (the defaults need no fixing)
  }
  // every fader has its own CC within a bank (no two faders on the screen doing the same)
  for (int g = 0; g < GROUPS; g++) {
    for (int b = 0; b < CTL_BANKS; b++) {
      int f = midictl::first(g, b), n = midictl::perBank(g);
      for (int i = f; i < f + n; i++) {
        for (int j = i + 1; j < f + n; j++) {
          CHECK(s.ctl[i].cc != s.ctl[j].cc);
        }
      }
    }
  }
  // the knobs: all on CCs MIDI leaves undefined, none twice, none a fader's
  for (int i = CTL_FIRST_KNOB; i < CTL_ENC; i++) {
    int cc = s.ctl[i].cc;
    CHECK((cc >= 20 && cc <= 31) || cc == 85 || cc == 86 || (cc >= 102 && cc <= 119));
    for (int j = 0; j < CTL_ENC; j++) {
      CHECK(j == i || s.ctl[j].cc != cc);
    }
  }
  CHECK(s.ctl[CTL_ENC].cc == 74);
  CHECK(s.pos[2] == 127);  // expression: all of it
  CHECK(s.pos[8] == 64);   // pan: the middle
  CHECK(sizeof(CtlSetup) == 4 + 2 * GROUPS + CTL_COUNT * sizeof(CtlConfig) + CTL_COUNT);  // bytes only: no padding
  CHECK(sizeof(CtlConfig) == CTL_LABEL + 9);
  CHECK(CTL_COUNT == 49 && sizeof(CtlSetup) == 1086);  // (the kept layout: a change here needs a new CTL_VERSION)
}

static void testScaling() {
  CtlConfig full = cfg(1, 0, 0, 127, 0);
  for (int p = 0; p <= 127; p++) {
    CHECK(midictl::toValue(full, (uint8_t)p) == p);  // the full range: position is value
    CHECK(midictl::toPos(full, (uint8_t)p, 0) == p);
  }
  CtlConfig inv = cfg(1, 0, 0, 127, 1);
  CHECK(midictl::toValue(inv, 0) == 127 && midictl::toValue(inv, 127) == 0 && midictl::toValue(inv, 27) == 100);
  CtlConfig narrow = cfg(1, 0, 20, 100, 0);
  CHECK(midictl::toValue(narrow, 0) == 20 && midictl::toValue(narrow, 127) == 100);
  CtlConfig narrowInv = cfg(1, 0, 20, 100, 1);
  CHECK(midictl::toValue(narrowInv, 0) == 100 && midictl::toValue(narrowInv, 127) == 20);
  // a value outside the range counts as its nearer end
  CHECK(midictl::toPos(narrow, 0, 50) == 0 && midictl::toPos(narrow, 127, 50) == 127);
  CHECK(midictl::toPos(narrowInv, 0, 50) == 127 && midictl::toPos(narrowInv, 127, 50) == 0);
  // one value only: it sends that, and a value says nothing about the position
  CtlConfig one = cfg(1, 0, 64, 64, 0);
  CHECK(midictl::toValue(one, 0) == 64 && midictl::toValue(one, 127) == 64);
  CHECK(midictl::toPos(one, 10, 77) == 77);
  // a position past 127 is 127
  CHECK(midictl::toValue(full, 200) == 127);
  // every range, both ways, every position: inside the range, monotonic, and a value heard back puts the control where
  // it sends that same value
  int bad = 0;
  for (int lo = 0; lo <= 127; lo++) {
    for (int hi = lo; hi <= 127; hi++) {
      for (int iv = 0; iv < 2; iv++) {
        CtlConfig c = cfg(1, 0, (uint8_t)lo, (uint8_t)hi, (uint8_t)iv);
        int last = iv ? 128 : -1;
        for (int p = 0; p <= 127; p++) {
          int v = midictl::toValue(c, (uint8_t)p);
          if (v < lo || v > hi || (iv ? v > last : v < last)) {
            bad++;
          }
          last = v;
          if (midictl::toValue(c, midictl::toPos(c, (uint8_t)v, (uint8_t)p)) != v) {
            bad++;
          }
        }
        // the ends: position 0 and 127 reach lo and hi exactly
        if (midictl::toValue(c, iv ? 127 : 0) != lo || midictl::toValue(c, iv ? 0 : 127) != hi) {
          bad++;
        }
      }
    }
  }
  CHECK(bad == 0);
}

static void testSending() {
  MidiControls m = fresh();
  uint8_t msg[3];
  // a finger on fader 4 (cutoff, CC 74, channel 1)
  CHECK(m.set(3, 100, ORIGIN_USER, msg) && msgIs(msg, 0xB0, 74, 100));
  CHECK(!m.set(3, 100, ORIGIN_USER, msg));  // the same value again: nothing
  CHECK(m.set(3, 101, ORIGIN_USER, msg) && msgIs(msg, 0xB0, 74, 101));
  // another channel: channel 16 is status 0xBF
  CtlConfig c = cfg(20, 15, 0, 127, 0);
  m.assign(0, c);
  CHECK(m.set(0, 5, ORIGIN_USER, msg) && msgIs(msg, 0xBF, 20, 5));
  // from MIDI or from a setting: shown, not sent
  CHECK(!m.set(12, 33, ORIGIN_MIDI, msg) && m.pos(12) == 33);
  CHECK(!m.set(12, 34, ORIGIN_CONFIG, msg) && m.pos(12) == 34);
  // ... and the next finger move sends the value it makes, the first since then
  CHECK(m.set(12, 34, ORIGIN_USER, msg) && msgIs(msg, 0xB0, 71, 34));
  // a narrow, inverted range: what goes out stays in it
  m.assign(2, cfg(11, 2, 40, 90, 1));
  CHECK(m.set(2, 0, ORIGIN_USER, msg) && msgIs(msg, 0xB2, 11, 90));
  CHECK(m.set(2, 127, ORIGIN_USER, msg) && msgIs(msg, 0xB2, 11, 40));
  // positions that make the same value send once (a narrow range: several positions a value)
  m.assign(2, cfg(11, 2, 0, 3, 0));
  int sends = 0;
  for (int p = 0; p <= 127; p++) {
    sends += m.set(2, (uint8_t)p, ORIGIN_USER, msg);
  }
  CHECK(sends == 4);  // 0, 1, 2, 3: each once
  // out of range indexes: nothing
  CHECK(!m.set(-1, 5, ORIGIN_USER, msg) && !m.set(CTL_COUNT, 5, ORIGIN_USER, msg));
  // reassigning resends: a new CC or channel hasn't heard it
  m.assign(1, cfg(75, 0, 0, 127, 0));
  CHECK(m.set(1, 101, ORIGIN_USER, msg) && msgIs(msg, 0xB0, 75, 101));
  // an assignment out of range is brought into it
  CtlConfig wild = cfg(200, 30, 120, 10, 7);
  m.assign(4, wild);
  const CtlConfig& got = m.setup().ctl[4];
  CHECK(got.cc == 73 && got.ch == 0 && got.lo == 10 && got.hi == 120 && got.invert == 1);  // (cc, ch: kept as before)
}

static void testFeedback() {
  MidiControls m = fresh();
  uint8_t msg[3];
  int changed[CTL_COUNT];
  // cutoff on channel 1 comes in: fader 4 and the encoder (both CC 74 ch 1) take it
  int n = m.feedback(0xB0, 74, 90, changed);
  CHECK(n == 2 && changed[0] == 3 && changed[1] == CTL_ENC);
  CHECK(m.pos(3) == 90 && m.pos(CTL_ENC) == 90);
  // ... and a finger putting it at the same value sends nothing: no echo back to the synth
  CHECK(!m.set(3, 90, ORIGIN_USER, msg));
  CHECK(m.set(3, 91, ORIGIN_USER, msg) && msgIs(msg, 0xB0, 74, 91));
  // another channel, another number, not a CC: nothing
  CHECK(m.feedback(0xB1, 74, 10, changed) == 0 && m.pos(3) == 91);
  CHECK(m.feedback(0xB0, 99, 10, changed) == 0);
  CHECK(m.feedback(0x90, 74, 10, changed) == 0 && m.pos(3) == 91);  // (a note on, same bytes)
  CHECK(m.feedback(0xB0, 74, 200, changed) == 0);                     // (not MIDI)
  // a control in another bank takes it too (banks keep their values)
  CHECK(m.feedback(0xB0, 91, 64, changed) == 1 && changed[0] == 9 && m.pos(9) == 64);
  // an inverted, narrow control: shows where it would send that value
  m.assign(4, cfg(73, 0, 20, 100, 1));
  CHECK(m.feedback(0xB0, 73, 20, changed) == 1 && m.pos(4) == 127);
  CHECK(m.feedback(0xB0, 73, 100, changed) == 1 && m.pos(4) == 0);
  CHECK(m.feedback(0xB0, 73, 5, changed) == 1 && m.pos(4) == 127);  // (below lo: as lo)
  // feedback never sends: a stream of them, then the same value from a finger, sends nothing
  for (int v = 0; v <= 127; v++) {
    m.feedback(0xB0, 74, (uint8_t)v, changed);
  }
  CHECK(!m.set(3, 127, ORIGIN_USER, msg));
}

static void testBanks() {
  MidiControls m = fresh();
  uint8_t msg[3];
  CHECK(midictl::first(GROUP_FADERS, 0) == 0 && midictl::first(GROUP_FADERS, 3) == 12);
  CHECK(midictl::first(GROUP_KNOBS, 0) == 16 && midictl::first(GROUP_KNOBS, 1) == 24 && midictl::first(GROUP_KNOBS, 3) == 40);
  CHECK(midictl::perBank(GROUP_FADERS) == 4 && midictl::perBank(GROUP_KNOBS) == 8);
  CHECK(midictl::groupOf(0) == GROUP_FADERS && midictl::groupOf(15) == GROUP_FADERS);
  CHECK(midictl::groupOf(16) == GROUP_KNOBS && midictl::groupOf(47) == GROUP_KNOBS);
  CHECK(midictl::groupOf(CTL_ENC) == -1 && midictl::groupOf(-1) == -1);
  CHECK(midictl::inBank(5, GROUP_FADERS, 1) && !midictl::inBank(8, GROUP_FADERS, 1));
  CHECK(midictl::inBank(31, GROUP_KNOBS, 1) && !midictl::inBank(32, GROUP_KNOBS, 1) && !midictl::inBank(CTL_ENC, GROUP_KNOBS, 3));
  // every control (but the encoder's) in exactly one group's bank
  for (int i = 0; i < CTL_COUNT; i++) {
    int in = 0;
    for (int g = 0; g < GROUPS; g++) {
      for (int b = 0; b < CTL_BANKS; b++) {
        in += midictl::inBank(i, g, b);
      }
    }
    CHECK(in == (i == CTL_ENC ? 0 : 1));
  }
  m.set(0, 10, ORIGIN_USER, msg);
  m.bank(GROUP_FADERS, 2);
  CHECK(m.bank(GROUP_FADERS) == 2 && m.bank(GROUP_KNOBS) == 0);  // each page its own bank
  CHECK(m.set(8, 99, ORIGIN_USER, msg) && msgIs(msg, 0xB0, 10, 99));  // faders bank 3's first: pan
  m.bank(GROUP_FADERS, 0);
  CHECK(m.pos(0) == 10 && m.pos(8) == 99);  // each bank's values kept
  m.bank(GROUP_FADERS, 4);
  m.bank(GROUP_FADERS, -1);
  m.bank(GROUPS, 1);
  CHECK(m.bank(GROUP_FADERS) == 0);  // out of range: kept as it was
  // a knob sends its own CC
  CHECK(m.set(16, 64, ORIGIN_USER, msg) && msgIs(msg, 0xB0, 20, 64));
}

static void testEncoder() {
  MidiControls m = fresh();
  uint8_t mm[CTL_MSGS][3];
  uint8_t* msg = mm[0];
  int moved;
  // no page in select mode: the encoder's own control (cutoff)
  CHECK(m.turn(1, -1, mm, &moved) && moved == CTL_ENC && msgIs(msg, 0xB0, 74, 1));
  CHECK(m.turn(-1, GROUP_FADERS, mm, &moved) && moved == CTL_ENC && msgIs(msg, 0xB0, 74, 0));
  CHECK(!m.turn(-5, -1, mm, &moved) && m.pos(CTL_ENC) == 0);  // at the bottom: nothing new
  m.step(4);
  CHECK(m.turn(3, -1, mm, &moved) && msgIs(msg, 0xB0, 74, 12));
  CHECK(m.turn(100, -1, mm, &moved) && msgIs(msg, 0xB0, 74, 127));
  CHECK(!m.turn(1, -1, mm, &moved));  // at the top
  m.step(0);
  CHECK(m.setup().step == 1);
  m.step(99);
  CHECK(m.setup().step == CTL_STEP_MAX);
  m.step(1);
  // its own channel and range
  m.assign(CTL_ENC, cfg(1, 9, 0, 63, 0));
  CHECK(m.turn(-200, -1, mm, &moved) && msgIs(msg, 0xB9, 1, 0));
}

static void testSelect() {
  MidiControls m = fresh();
  uint8_t mm[CTL_MSGS][3];
  uint8_t* msg = mm[0];
  int moved;
  CHECK(m.selected() == -1);
  // radio: one at a time
  m.select(2);
  CHECK(m.selected() == 2);
  m.select(20);
  CHECK(m.selected() == 20);
  m.select(CTL_ENC);  // (the encoder's own isn't selectable)
  CHECK(m.selected() == -1);
  m.select(-1);
  CHECK(m.selected() == -1);
  // selected, but its page in play mode: the encoder turns its own
  m.select(3);
  CHECK(m.target(GROUP_FADERS) == CTL_ENC);
  // the faders' page in select mode: the encoder turns the selected fader, with the fader's own CC
  m.mode(GROUP_FADERS, MODE_SELECT);
  CHECK(m.mode(GROUP_FADERS) == MODE_SELECT && m.mode(GROUP_KNOBS) == MODE_PLAY);
  CHECK(m.target(GROUP_FADERS) == 3);
  CHECK(m.turn(5, GROUP_FADERS, mm, &moved) && moved == 3 && msgIs(msg, 0xB0, 74, 5));
  // ... but not while another page shows, or none
  CHECK(m.target(GROUP_KNOBS) == CTL_ENC && m.target(-1) == CTL_ENC);
  // a knob selected on the knobs' page, in select mode
  m.mode(GROUP_KNOBS, MODE_SELECT);
  m.select(30);
  CHECK(m.target(GROUP_KNOBS) == 30 && m.target(GROUP_FADERS) == CTL_ENC);
  m.step(10);
  CHECK(m.turn(2, GROUP_KNOBS, mm, &moved) && moved == 30 && msgIs(msg, 0xB0, 102, 20));  // knob 15 (index 14): CC 102
  // an odd mode: kept as it was
  m.mode(GROUP_KNOBS, (CtlMode)7);
  CHECK(m.mode(GROUP_KNOBS) == MODE_SELECT);
  // the encoder's push: the next one of the bank on the screen, round the bank
  m.bank(GROUP_FADERS, 1);
  CHECK(m.selectNext(GROUP_FADERS) == 4);  // (the selected knob isn't on this page: the bank's first)
  CHECK(m.selectNext(GROUP_FADERS) == 5 && m.selectNext(GROUP_FADERS) == 6 && m.selectNext(GROUP_FADERS) == 7);
  CHECK(m.selectNext(GROUP_FADERS) == 4);
  m.bank(GROUP_KNOBS, 3);
  CHECK(m.selectNext(GROUP_KNOBS) == 40);
  for (int k = 0; k < 7; k++) {
    m.selectNext(GROUP_KNOBS);
  }
  CHECK(m.selected() == 47 && m.selectNext(GROUP_KNOBS) == 40);
}

// a page of four controls side by side, 80 px each, under a 30 px bar
static int hitColumns(int x, int y, void*) {
  return y < 30 || x < 0 || x >= 320 ? -1 : x / 80;
}

static void testOwners() {
  TouchOwners o;
  int ended[OWN_FINGERS];
  // one finger on control 1: it takes it
  OwnPoint a = {0, 100, 120};
  CHECK(o.update(&a, 1, hitColumns, 0, ended) == 0);
  CHECK(o.holder(1) == 0 && o.slot(0).began && o.slot(0).y0 == 120);
  // it slides onto control 3: it still holds 1
  a.x = 300;
  CHECK(o.update(&a, 1, hitColumns, 0, ended) == 0 && o.holder(1) == 0 && o.holder(3) == -1 && !o.slot(0).began);
  // a second finger on control 3: it takes 3; two at once
  OwnPoint two[2] = {{0, 300, 120}, {1, 250, 200}};
  CHECK(o.update(two, 2, hitColumns, 0, ended) == 0 && o.holder(1) == 0 && o.holder(3) == 1);
  // the second lands where the first is, on the next touch: it doesn't take a held control
  CHECK(o.update(&two[0], 1, hitColumns, 0, ended) == 1 && ended[0] == 3);  // (the second lifted: 3 let go)
  OwnPoint grab[2] = {{0, 300, 120}, {2, 100, 100}};  // (a new finger on control 1, held by the first)
  CHECK(o.update(grab, 2, hitColumns, 0, ended) == 0 && o.holder(1) == 0 && o.slot(1).on && o.slot(1).ctl == -1);
  // ... and it never takes one by sliding onto it
  grab[1].x = 10;
  CHECK(o.update(grab, 2, hitColumns, 0, ended) == 0 && o.holder(0) == -1);
  // both lift: the first lets go of 1, the second had nothing
  CHECK(o.update(0, 0, hitColumns, 0, ended) == 1 && ended[0] == 1);
  CHECK(!o.slot(0).on && !o.slot(1).on && o.holder(1) == -1);
  // a finger on the bar holds nothing
  OwnPoint bar = {3, 100, 10};
  CHECK(o.update(&bar, 1, hitColumns, 0, ended) == 0 && o.slot(0).on && o.slot(0).ctl == -1);
  CHECK(o.slot(0).began);  // (new, though it holds nothing: the bar's swipes start from it)
  CHECK(o.update(&bar, 1, hitColumns, 0, ended) == 0 && !o.slot(0).began);
  CHECK(o.update(0, 0, hitColumns, 0, ended) == 0);
  // a bank change while two fingers hold controls: both let go, and they hold nothing until they lift
  OwnPoint both[2] = {{4, 40, 100}, {5, 200, 100}};
  o.update(both, 2, hitColumns, 0, ended);
  CHECK(o.holder(0) == 0 && o.holder(2) == 1);
  int n = o.releaseAll(ended);
  CHECK(n == 2 && ended[0] == 0 && ended[1] == 2);
  CHECK(o.update(both, 2, hitColumns, 0, ended) == 0 && o.holder(0) == -1 && o.holder(2) == -1);
  CHECK(o.releaseAll(ended) == 0);  // (nothing held: nothing to let go)
  CHECK(o.update(0, 0, hitColumns, 0, ended) == 0);  // lifted: nothing stuck
  CHECK(o.update(both, 2, hitColumns, 0, ended) == 0 && o.holder(0) == 0 && o.holder(2) == 1);  // and new touches work
  o.update(0, 0, hitColumns, 0, ended);
  // more fingers than the panel feels: the extra one is left out
  OwnPoint three[3] = {{6, 40, 100}, {7, 120, 100}, {8, 200, 100}};
  o.update(three, 3, hitColumns, 0, ended);
  CHECK(o.holder(0) == 0 && o.holder(1) == 1 && o.holder(2) == -1);
  // ids the panel reuses after a lift are new fingers
  OwnPoint again = {6, 280, 100};
  CHECK(o.update(&again, 1, hitColumns, 0, ended) == 1 && ended[0] == 1);
  CHECK(o.holder(0) == 0);  // (id 6 is still the same finger: it keeps control 0 wherever it is)
}

static void testKept() {
  CtlSetup s;
  // nothing kept, or kept by another version: the defaults
  CHECK(midictl::restore(s, false) == -1 && s.version == CTL_VERSION && s.ctl[3].cc == 74);
  midictl::defaults(s);
  s.version = 99;
  s.ctl[3].cc = 5;
  CHECK(midictl::restore(s, true) == -1 && s.ctl[3].cc == 74);
  // kept as it was: through bytes (as hal::save and hal::load move it) and back, the same
  MidiControls m = fresh();
  uint8_t msg[3];
  m.assign(5, cfg(30, 3, 10, 20, 1));
  m.set(5, 60, ORIGIN_USER, msg);
  m.bank(GROUP_FADERS, 3);
  m.bank(GROUP_KNOBS, 2);
  m.mode(GROUP_KNOBS, MODE_SELECT);
  m.select(33);
  m.step(5);
  uint8_t bytes[sizeof(CtlSetup)];
  memcpy(bytes, &m.setup(), sizeof bytes);
  CtlSetup back;
  memcpy(&back, bytes, sizeof back);
  CHECK(midictl::restore(back, true) == 0);
  CHECK(!memcmp(&back, &m.setup(), sizeof back));
  CHECK(back.bank[GROUP_FADERS] == 3 && back.bank[GROUP_KNOBS] == 2 && back.mode[GROUP_KNOBS] == MODE_SELECT);
  CHECK(back.selected == 33 && back.step == 5 && back.ctl[5].cc == 30 && back.ctl[5].ch == 3 && back.pos[5] == 60);
  // damaged fields: each fixed, the rest kept
  CtlSetup d = back;
  d.bank[GROUP_FADERS] = 9;                     // 1
  d.step = 0;                                   // 2
  d.ctl[0].cc = 200;                            // 3
  d.ctl[1].ch = 17;                             // 4 (16: CH_GLOBAL, kept)
  d.ctl[2].lo = 90, d.ctl[2].hi = 10;           // 5 (swapped)
  d.ctl[3].hi = 255;                            // 6
  d.ctl[4].invert = 3;                          // 7
  memset(d.ctl[6].label, 'x', CTL_LABEL);       // 8 (no end)
  d.ctl[7].label[0] = '\n';                     // 9
  d.pos[8] = 128;                               // 10
  d.mode[GROUP_FADERS] = 2;                     // 11
  d.selected = CTL_ENC;                         // 12 (the encoder's own: not selectable)
  d.ctl[20].cc = 128;                           // 13 (a knob: its default)
  CHECK(midictl::restore(d, true) == 13);
  CHECK(d.bank[GROUP_FADERS] == 0 && d.bank[GROUP_KNOBS] == 2 && d.step == 1);
  CHECK(d.mode[GROUP_FADERS] == MODE_PLAY && d.mode[GROUP_KNOBS] == MODE_SELECT && d.selected == CTL_NONE);
  CHECK(d.ctl[0].cc == 0 && d.ctl[1].ch == 0 && d.ctl[20].cc == 24);  // (their defaults)
  CHECK(d.ctl[2].lo == 10 && d.ctl[2].hi == 90 && d.ctl[3].hi == 127 && d.ctl[4].invert == 1);
  CHECK(d.ctl[6].label[CTL_LABEL - 1] == 0 && strlen(d.ctl[6].label) == CTL_LABEL - 1);
  CHECK(d.ctl[7].label[0] == '?' && d.pos[8] == 64);
  CHECK(d.ctl[5].cc == 30 && d.pos[5] == 60);  // untouched
  // and once fixed, it stays fixed
  CHECK(midictl::restore(d, true) == 0);
  // every byte random: restore always ends with everything in range
  unsigned seed = 12345;
  int bad = 0;
  for (int t = 0; t < 2000; t++) {
    uint8_t* p = (uint8_t*)&d;
    for (size_t k = 0; k < sizeof d; k++) {
      seed = seed * 1103515245u + 12345u;
      p[k] = (uint8_t)(seed >> 16);
    }
    d.version = CTL_VERSION;
    midictl::restore(d, true);
    if (d.step < 1 || d.step > CTL_STEP_MAX || (d.selected != CTL_NONE && d.selected >= CTL_ENC)) {
      bad++;
    }
    for (int g = 0; g < GROUPS; g++) {
      if (d.bank[g] >= CTL_BANKS || d.mode[g] > MODE_SELECT) {
        bad++;
      }
    }
    for (int i = 0; i < CTL_COUNT; i++) {
      const CtlConfig& c = d.ctl[i];
      if (c.cc > 127 || c.ch > CH_GLOBAL || c.lo > c.hi || c.hi > 127 || c.invert > 1 || d.pos[i] > 127 || c.kind >= KINDS ||
          c.rel >= RELS || c.cc2 > 127 || c.incVal > 127 || (i < CTL_FADERS && c.kind != KIND_ABS && c.kind != KIND_PB) ||
          memchr(c.label, 0, CTL_LABEL) == 0) {
        bad++;
      }
    }
    if (midictl::restore(d, true) != 0) {
      bad++;
    }
  }
  CHECK(bad == 0);
}


static CtlConfig kind(uint8_t cc, uint8_t ch, uint8_t k, uint8_t rel, uint8_t cc2, uint8_t incVal, uint8_t invert) {
  CtlConfig c = cfg(cc, ch, 0, 127, invert);
  c.kind = k, c.rel = rel, c.cc2 = cc2, c.incVal = incVal;
  return c;
}

static void testKinds() {
  // the three relative encodings, both ways, to their ends
  CHECK(midictl::relValue(REL_OFFSET, 1) == 65 && midictl::relValue(REL_OFFSET, -1) == 63);
  CHECK(midictl::relValue(REL_OFFSET, 63) == 127 && midictl::relValue(REL_OFFSET, -63) == 1);
  CHECK(midictl::relValue(REL_TWOS, 1) == 1 && midictl::relValue(REL_TWOS, -1) == 127);
  CHECK(midictl::relValue(REL_TWOS, 63) == 63 && midictl::relValue(REL_TWOS, -63) == 65);
  CHECK(midictl::relValue(REL_SIGN, 1) == 1 && midictl::relValue(REL_SIGN, -1) == 65);
  CHECK(midictl::relValue(REL_SIGN, 63) == 63 && midictl::relValue(REL_SIGN, -63) == 127);
  CHECK(midictl::relValue(REL_OFFSET, 200) == 127 && midictl::relValue(REL_TWOS, -200) == 65);  // (63 at most)
  // every amount: never 0 or 64 for a turn (no encoding confuses a turn with none), and each one tells them apart
  for (int r = 0; r < RELS; r++) {
    int seen[128] = {};
    for (int n = -63; n <= 63; n++) {
      if (!n) {
        continue;
      }
      uint8_t v = midictl::relValue(r, n);
      CHECK(v <= 127 && v != 0 && v != 64 && !seen[v]);
      seen[v] = 1;
    }
  }

  MidiControls m = fresh();
  uint8_t mm[CTL_MSGS][3];
  int K = CTL_FIRST_KNOB;  // knob 1
  // relative: one message a move, step clicks a detent, its sign the way it turned
  m.assign(K, kind(20, 2, KIND_REL, REL_OFFSET, 0, 0, 0));
  CHECK(m.move(K, 1, mm) == 1 && msgIs(mm[0], 0xB2, 20, 65) && m.lastTurn(K) == 1);
  CHECK(m.move(K, -3, mm) == 1 && msgIs(mm[0], 0xB2, 20, 61) && m.lastTurn(K) == -3);
  m.step(4);
  CHECK(m.move(K, 2, mm) == 1 && msgIs(mm[0], 0xB2, 20, 72));  // 2 detents x 4
  CHECK(m.move(K, 100, mm) == 1 && msgIs(mm[0], 0xB2, 20, 127) && m.lastTurn(K) == 63);  // (63 at most)
  m.step(1);
  CHECK(m.move(K, 0, mm) == 0);  // no turn: nothing
  // every turn sends, the same one again too (it's a turn, not a value)
  CHECK(m.move(K, 1, mm) == 1 && m.move(K, 1, mm) == 1);
  // the other encodings, and inverted (the other way round)
  m.assign(K, kind(20, 2, KIND_REL, REL_TWOS, 0, 0, 0));
  CHECK(m.move(K, -1, mm) == 1 && msgIs(mm[0], 0xB2, 20, 127));
  m.assign(K, kind(20, 2, KIND_REL, REL_SIGN, 0, 0, 1));
  CHECK(m.move(K, 1, mm) == 1 && msgIs(mm[0], 0xB2, 20, 65) && m.lastTurn(K) == -1);
  // its pointer goes round and round (0-127, wrapping), whichever way it sends
  uint8_t p = m.pos(K);
  m.move(K, -1, mm);
  CHECK(m.pos(K) == p - 1 || (p == 0 && m.pos(K) == 127));
  for (int k = 0; k < 200; k++) {
    m.move(K, 1, mm);
  }
  CHECK(m.pos(K) <= 127);
  // a finger's set() on a relative control sends nothing (it moves by turns), and incoming CCs leave it alone
  CHECK(!m.set(K, 100, ORIGIN_USER, mm[0]));
  int changed[CTL_COUNT];
  CHECK(m.feedback(0xB2, 20, 10, changed) == 0);

  // clockwise / anticlockwise: a message a click, cc one way, cc2 the other, both with incVal
  m.assign(K + 1, kind(40, 0, KIND_INCDEC, 0, 41, 127, 0));
  CHECK(m.move(K + 1, 1, mm) == 1 && msgIs(mm[0], 0xB0, 40, 127) && m.lastTurn(K + 1) == 1);
  CHECK(m.move(K + 1, -2, mm) == 2 && msgIs(mm[0], 0xB0, 41, 127) && msgIs(mm[1], 0xB0, 41, 127));
  CHECK(m.move(K + 1, 100, mm) == CTL_MSGS && msgIs(mm[CTL_MSGS - 1], 0xB0, 40, 127));  // (CTL_MSGS at most)
  m.step(5);
  CHECK(m.move(K + 1, 1, mm) == 1);  // (a click a message: the step doesn't multiply them)
  m.step(1);
  m.assign(K + 1, kind(40, 15, KIND_INCDEC, 0, 41, 1, 1));  // its own value, inverted, channel 16
  CHECK(m.move(K + 1, 1, mm) == 1 && msgIs(mm[0], 0xBF, 41, 1));
  CHECK(m.move(K + 1, -1, mm) == 1 && msgIs(mm[0], 0xBF, 40, 1));

  // the encoder: the selected relative knob turns as relative, its own control in any kind
  m.mode(GROUP_KNOBS, MODE_SELECT);
  m.select(K + 1);
  int moved;
  CHECK(m.turn(3, GROUP_KNOBS, mm, &moved) == 3 && moved == K + 1 && msgIs(mm[2], 0xBF, 41, 1));
  m.assign(CTL_ENC, kind(10, 0, KIND_REL, REL_TWOS, 0, 0, 0));
  CHECK(m.turn(-2, -1, mm, &moved) == 1 && moved == CTL_ENC && msgIs(mm[0], 0xB0, 10, 126));
  // absolute through move: as the encoder did (step, clamped, and the same value isn't sent again)
  m.assign(K + 2, cfg(50, 0, 0, 127, 0));
  CHECK(m.move(K + 2, 5, mm) == 1 && msgIs(mm[0], 0xB0, 50, 5));
  CHECK(m.move(K + 2, -10, mm) == 1 && msgIs(mm[0], 0xB0, 50, 0) && m.move(K + 2, -1, mm) == 0);

  // a fader is always absolute: a relative assignment is taken as absolute
  m.assign(0, kind(1, 0, KIND_REL, REL_TWOS, 0, 0, 0));
  CHECK(m.setup().ctl[0].kind == KIND_ABS);
  m.set(0, 0, ORIGIN_CONFIG, mm[0]);
  CHECK(m.move(0, 1, mm) == 1 && msgIs(mm[0], 0xB0, 1, 1));
  // fields out of range: fixed
  CtlConfig bad = kind(30, 0, 9, 9, 200, 200, 0);
  CHECK(midictl::sanitize(bad, kind(30, 0, KIND_ABS, REL_OFFSET, 31, 127, 0)) == 4);
  CHECK(bad.kind == KIND_ABS && bad.rel == REL_OFFSET && bad.cc2 == 31 && bad.incVal == 127);
}

// version 1 (as the first faders-and-knobs build kept it): its layout, to make one as it would have
struct V1Config {
  char label[CTL_LABEL];
  uint8_t cc, ch, lo, hi, invert;
};
struct V1Setup {
  uint8_t version, step, selected, spare;
  uint8_t bank[GROUPS], mode[GROUPS];
  V1Config ctl[CTL_COUNT];
  uint8_t pos[CTL_COUNT];
};

static void testUpgrade() {
  CHECK(sizeof(V1Setup) == 890 && midictl::v1Size() == 890);
  V1Setup v{};
  v.version = 1;
  v.step = 3;
  v.selected = 20;
  v.bank[GROUP_FADERS] = 2, v.bank[GROUP_KNOBS] = 1;
  v.mode[GROUP_KNOBS] = MODE_SELECT;
  for (int i = 0; i < CTL_COUNT; i++) {
    v.ctl[i].cc = (uint8_t)(i % 128), v.ctl[i].ch = (uint8_t)(i % 16), v.ctl[i].lo = 5, v.ctl[i].hi = 100;
    v.ctl[i].invert = (uint8_t)(i & 1);
    v.pos[i] = (uint8_t)(i * 2);
  }
  strcpy(v.ctl[1].label, "My cutoff");
  CtlSetup s;
  CHECK(midictl::upgrade(&v, sizeof v, s));
  CHECK(s.version == CTL_VERSION && s.step == 3 && s.selected == 20);
  CHECK(s.bank[GROUP_FADERS] == 2 && s.bank[GROUP_KNOBS] == 1 && s.mode[GROUP_KNOBS] == MODE_SELECT);
  int bad = 0;
  for (int i = 0; i < CTL_COUNT; i++) {
    const CtlConfig& c = s.ctl[i];
    if (c.cc != i % 128 || c.ch != i % 16 || c.lo != 5 || c.hi != 100 || c.invert != (i & 1) || s.pos[i] != i * 2 ||
        c.kind != KIND_ABS || c.cc2 != c.cc || c.incVal != 127) {
      bad++;
    }
  }
  CHECK(bad == 0);
  CHECK(!strcmp(s.ctl[1].label, "My cutoff"));
  CHECK(midictl::restore(s, true) == 0);  // (already in range)
  // what it keeps is fixed on the way, as anything kept
  v.ctl[3].ch = 99;
  v.bank[GROUP_FADERS] = 7;
  CHECK(midictl::upgrade(&v, sizeof v, s) && s.ctl[3].ch == 0 && s.bank[GROUP_FADERS] == 0);
  // not version 1, or not its size: not upgraded, s untouched
  CtlSetup before = s;
  v.version = 2;
  CHECK(!midictl::upgrade(&v, sizeof v, s) && !memcmp(&before, &s, sizeof s));
  v.version = 1;
  CHECK(!midictl::upgrade(&v, sizeof v - 1, s));
}


static void testGestures() {
  // nothing much yet: undecided
  CHECK(midictl::gesture(0, 0) == GESTURE_UNDECIDED && midictl::gesture(3, -4) == GESTURE_UNDECIDED);
  // up or down, 6 px: a slide (also when it went as far sideways)
  CHECK(midictl::gesture(0, -6) == GESTURE_SLIDE && midictl::gesture(0, 6) == GESTURE_SLIDE);
  CHECK(midictl::gesture(6, -6) == GESTURE_SLIDE && midictl::gesture(-5, 30) == GESTURE_SLIDE);
  // sideways, under 40 px: not yet (a small wobble isn't a swipe, and doesn't move the control either)
  CHECK(midictl::gesture(39, 2) == GESTURE_UNDECIDED && midictl::gesture(-20, 5) == GESTURE_UNDECIDED);
  // 40 px sideways, twice as far as up or down: a swipe; leftwards the next page, rightwards the previous
  CHECK(midictl::gesture(-40, 0) == GESTURE_NEXT && midictl::gesture(40, 0) == GESTURE_PREV);
  CHECK(midictl::gesture(-60, 5) == GESTURE_NEXT && midictl::gesture(80, -5) == GESTURE_PREV);
  // a diagonal: a slide once up or down passes 6 px and outweighs sideways; never a swipe unless twice as sideways
  CHECK(midictl::gesture(-50, 30) == GESTURE_UNDECIDED);  // (50 < 2 x 30, and 30 < 50)
  // pages in a row: faders 1-4, knobs 1-4, round
  int g = GROUP_FADERS, b = 0;
  int seen[GROUPS][CTL_BANKS] = {};
  for (int k = 0; k < 8; k++) {
    seen[g][b]++;
    midictl::nextPage(g, b, 1);
  }
  CHECK(g == GROUP_FADERS && b == 0);
  for (int gg = 0; gg < GROUPS; gg++) {
    for (int bb = 0; bb < CTL_BANKS; bb++) {
      CHECK(seen[gg][bb] == 1);
    }
  }
  g = GROUP_FADERS, b = 3;
  midictl::nextPage(g, b, 1);
  CHECK(g == GROUP_KNOBS && b == 0);
  midictl::nextPage(g, b, -1);
  CHECK(g == GROUP_FADERS && b == 3);
  g = GROUP_FADERS, b = 0;
  midictl::nextPage(g, b, -1);
  CHECK(g == GROUP_KNOBS && b == 3);
  midictl::nextPage(g, b, 1);
  CHECK(g == GROUP_FADERS && b == 0);
  g = GROUP_KNOBS, b = 1;
  midictl::nextPage(g, b, 1);
  CHECK(g == GROUP_KNOBS && b == 2);
}

static void testLabels() {
  char out[CTL_LABEL];
  CtlConfig c = cfg(74, 0, 0, 127, 0);
  CHECK(!strcmp(midictl::label(c, out), "CC 74"));
  strcpy(c.label, "Cutoff");
  CHECK(!strcmp(midictl::label(c, out), "Cutoff"));
  memset(c.label, 'y', CTL_LABEL);  // (no end: still ended in out)
  CHECK(strlen(midictl::label(c, out)) == CTL_LABEL - 1);
}


static void testPitchBend() {
  CtlConfig pb{};
  pb.kind = KIND_PB;
  // position to bend: the ends and the middle exactly
  CHECK(midictl::toBend(pb, 0) == 0 && midictl::toBend(pb, 64) == 8192 && midictl::toBend(pb, 127) == 16383);
  CHECK(midictl::toBend(pb, 32) == 4096 && midictl::toBend(pb, 200) == 16383);
  int last = -1, bad = 0;
  for (int p = 0; p <= 127; p++) {
    int b = midictl::toBend(pb, (uint8_t)p);
    bad += b <= last;                              // (always up)
    bad += midictl::bendPos(pb, (uint16_t)b) != p;  // (and back)
    last = b;
  }
  CHECK(bad == 0);
  CHECK(midictl::bendPos(pb, 8192) == 64 && midictl::bendPos(pb, 8193) == 64 && midictl::bendPos(pb, 0) == 0);
  CHECK(midictl::bendPos(pb, 16383) == 127 && midictl::bendPos(pb, 20000) == 127);
  CtlConfig inv = pb;
  inv.invert = 1;
  CHECK(midictl::toBend(inv, 0) == 16383 && midictl::toBend(inv, 127) == 0 && midictl::toBend(inv, 64) == 8192);
  last = 16384;
  for (int p = 0; p <= 127; p++) {
    int v = midictl::toBend(inv, (uint8_t)p);
    bad += v >= last;                               // (always down)
    bad += midictl::bendPos(inv, (uint16_t)v) != p;  // (and back)
    last = v;
  }
  CHECK(bad == 0);

  // the default bank 1 fader: pitch bend on channel 1, at the middle
  MidiControls m = fresh();
  uint8_t msg[3];
  CHECK(m.pos(0) == 64 && m.shown(0) == 0 && m.shown(2) == 127);
  CHECK(m.set(0, 127, ORIGIN_USER, msg) && msgIs(msg, 0xE0, 0x7F, 0x7F) && m.shown(0) == 8191);
  CHECK(!m.set(0, 127, ORIGIN_USER, msg));  // (the same bend: nothing)
  CHECK(m.set(0, 64, ORIGIN_USER, msg) && msgIs(msg, 0xE0, 0x00, 0x40));  // (the middle: 8192)
  CHECK(m.set(0, 0, ORIGIN_USER, msg) && msgIs(msg, 0xE0, 0, 0) && m.shown(0) == -8192);
  // on another channel
  CtlConfig c = m.setup().ctl[0];
  c.ch = 3;
  m.assign(0, c);
  CHECK(m.setup().ctl[0].kind == KIND_PB);  // (a fader may bend)
  CHECK(m.set(0, 64, ORIGIN_USER, msg) && msgIs(msg, 0xE3, 0x00, 0x40));
  // the encoder turns it as an absolute one, a step at a time
  uint8_t mm[CTL_MSGS][3];
  CHECK(m.move(0, 1, mm) == 1 && mm[0][0] == 0xE3 && m.pos(0) == 65);
  // feedback: a bend on its channel moves it, and isn't sent back
  int changed[CTL_COUNT];
  CHECK(m.feedback(0xE3, 0x00, 0x60, changed) == 1 && changed[0] == 0 && m.pos(0) == midictl::bendPos(c, 0x3000));
  CHECK(!m.set(0, m.pos(0), ORIGIN_USER, msg));
  CHECK(m.feedback(0xE0, 0, 0x60, changed) == 0);  // (another channel)
  CHECK(m.feedback(0xB3, 0, 0x60, changed) == 0);  // (a CC 0, not a bend)
  CHECK(m.feedback(0xE3, 0, 200, changed) == 0);   // (not MIDI)
  // ... and a CC never moves a bend: Mod wheel's CC 1 moves Mod wheel only
  CHECK(m.feedback(0xB0, 1, 99, changed) == 1 && changed[0] == 1);
  // a knob can bend too; it shows its distance from the middle
  CtlConfig k = m.setup().ctl[CTL_FIRST_KNOB];
  k.kind = KIND_PB;
  m.assign(CTL_FIRST_KNOB, k);
  CHECK(m.setup().ctl[CTL_FIRST_KNOB].kind == KIND_PB);
  CHECK(m.set(CTL_FIRST_KNOB, 64, ORIGIN_USER, msg) && msgIs(msg, 0xE0, 0, 0x40));

  // a bank back to its defaults: just that bank
  m.assign(5, cfg(30, 3, 10, 20, 1));
  m.set(0, 100, ORIGIN_CONFIG, msg);
  m.resetBank(GROUP_FADERS, 0);
  CHECK(m.setup().ctl[0].kind == KIND_PB && m.setup().ctl[0].ch == 0 && m.pos(0) == 64);
  CHECK(m.setup().ctl[5].cc == 30);  // (bank 2: as it was)
  CHECK(m.set(0, 127, ORIGIN_USER, msg) && msgIs(msg, 0xE0, 0x7F, 0x7F));  // (news again)
  m.resetBank(GROUP_FADERS, 1);
  CHECK(m.setup().ctl[5].cc == midictl::defaultOf(5).cc && m.setup().ctl[5].ch == 0);
  m.resetBank(GROUP_KNOBS, 9);  // (no such bank: nothing)
}

// a setup kept before pitch bend: banks 1 and 4 untouched take the new defaults, changed ones stay
static void testNewDefaults() {
  CtlSetup s;
  midictl::defaults(s);
  CHECK(midictl::adoptNewDefaults(s) == 0);  // (already the new ones)
  static const char* const OLD1[4] = {"Mod wheel", "Cutoff", "Resonance", "Expression"};
  static const uint8_t CC1[4] = {1, 74, 71, 11}, CC4[4] = {16, 17, 18, 19};
  CtlSetup old = s;
  for (int k = 0; k < 4; k++) {
    CtlConfig c{};
    strcpy(c.label, OLD1[k]);
    c.cc = c.cc2 = CC1[k], c.hi = 127, c.incVal = 127;
    old.ctl[k] = c;
    old.pos[k] = k == 3 ? 127 : 0;
    CtlConfig g{};
    snprintf(g.label, sizeof g.label, "GP %d", k + 1);
    g.cc = g.cc2 = CC4[k], g.hi = 127, g.incVal = 127;
    old.ctl[12 + k] = g;
    old.pos[12 + k] = 0;
  }
  CtlSetup a = old;
  CHECK(midictl::adoptNewDefaults(a) == 2);
  CHECK(!memcmp(&a, &s, sizeof a));  // (the new defaults, all of it)
  // bank 1 changed (a CC): theirs; bank 4 untouched: the new ones
  CtlSetup b = old;
  b.ctl[1].cc = 75;
  CHECK(midictl::adoptNewDefaults(b) == 1);
  CHECK(b.ctl[0].kind == KIND_ABS && b.ctl[1].cc == 75 && b.ctl[12].cc == 71);
  // a renamed control is a change too
  CtlSetup c = old;
  strcpy(c.ctl[13].label, "Mine");
  CHECK(midictl::adoptNewDefaults(c) == 1 && c.ctl[0].kind == KIND_PB && !strcmp(c.ctl[13].label, "Mine"));
  // positions alone are no change: a fader moved still takes them
  CtlSetup d = old;
  d.pos[1] = 99;
  CHECK(midictl::adoptNewDefaults(d) == 2 && d.pos[0] == 64);
}

int main() {
  testDefaults();
  testScaling();
  testSending();
  testFeedback();
  testBanks();
  testEncoder();
  testSelect();
  testOwners();
  testKinds();
  testUpgrade();
  testGestures();
  testKept();
  testLabels();
  testPitchBend();
  testNewDefaults();
  if (fails) {
    printf("midictl: %d failed\n", fails);
    return 1;
  }
  printf("midictl: ok\n");
  return 0;
}
