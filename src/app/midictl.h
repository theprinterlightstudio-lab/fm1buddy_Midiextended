// MIDI controls (FM Buddy 2.0): what the fader page, the rotary (knobs) page and the encoder send, as one model. Each
// control has an assignment (a CC number, a channel, the range of values it sends, inverted or not, a label) and a
// position, 0-127, which is what its widget shows. Its value is the position scaled into its range: what goes out.
//
// Two groups, each in four banks: 16 faders (4 on the screen at a time) and 32 knobs (8 at a time). Plus the encoder's
// own control, for when the encoder isn't steering a selected one. Each group's page has a mode:
//   MODE_PLAY    a finger moves the control it's on, and that sends its CC
//   MODE_SELECT  a tap selects a control (one at a time, as radio buttons); the encoder then turns the selected one,
//                which sends its own CC. Fingers don't move anything
// A control's position changes three ways, and only one of them sends:
//   ORIGIN_USER    a finger or the encoder: sent, when the value it makes isn't the one last sent
//   ORIGIN_MIDI    an incoming CC with the control's channel and number: shown, never sent (no feedback loop)
//   ORIGIN_CONFIG  the assignment changed, or what's kept was loaded: shown, not sent
//
// Plain logic (no LVGL, no HAL), so tools/unit-test.sh builds and runs it (test/midictl_test.cpp). What goes out goes
// through midi::bytesIn(MIDI_TOUCH, ...) like the touch controller's (controls.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

#define CTL_BANKS 4
#define CTL_FADER_BANK 4  // faders on the screen at a time
#define CTL_KNOB_BANK 8   // knobs on the screen at a time
#define CTL_FADERS (CTL_BANKS * CTL_FADER_BANK)
#define CTL_KNOBS (CTL_BANKS * CTL_KNOB_BANK)
#define CTL_FIRST_KNOB CTL_FADERS
#define CTL_ENC (CTL_FADERS + CTL_KNOBS)  // the encoder's own control: after the faders and the knobs
#define CTL_COUNT (CTL_ENC + 1)
#define CTL_NONE 0xFF       // no control (selected: nothing selected)
#define CTL_LABEL 12        // bytes, with its terminating 0
#define CTL_KEY "ctl"       // hal::load / hal::save key (its own: no older setting changes size)
#define CTL_VERSION 2       // CtlSetup's layout. Version 1 (no kinds) is upgraded (midictl::upgrade); others: defaults
#define CTL_STEP_MAX 16     // the encoder: positions per detent, at most

enum CtlGroup { GROUP_FADERS,
                GROUP_KNOBS,
                GROUPS };

enum CtlMode { MODE_PLAY,
               MODE_SELECT };

// what a control sends (the knobs and the encoder's own: any; a fader: KIND_ABS or KIND_PB)
enum CtlKind { KIND_ABS,     // its position, scaled into lo..hi
               KIND_REL,     // how far it turned, as one CC in an encoding (CtlRel); for endless controls
               KIND_INCDEC,  // a CC for each click: cc clockwise, cc2 anticlockwise, both with the value incVal
               KIND_PB,      // pitch bend (14 bits: 0, the middle 8192, 16383) from its position; rel 1: back to the
                             // middle when let go (a fader's finger lifting). Faders and knobs alike
               KINDS };

// KIND_REL's encodings of a turn of n clicks (n up to 63 a message)
enum CtlRel { REL_OFFSET,  // 64 + n: 65 is +1, 63 is -1 ("binary offset")
              REL_TWOS,    // n, or 128 + n: 1 is +1, 127 is -1 ("two's complement")
              REL_SIGN,    // n, or 64 - n: 1 is +1, 65 is -1 ("signed bit")
              RELS };
#define CTL_MSGS 16  // messages a move sends at most (KIND_INCDEC: one a click)
#define CH_GLOBAL 16  // (FM Buddy 2.0) a control's channel: Global, the one channel set for every control that follows
                      // the instrument's selected track (SLOOP: a channel no track has, GLO > MIDI 2 > OTHER = SEL).
                      // Its messages are built on channel 1; controls.cpp sends them on the Global channel
// the Global channel, 0-15, from what's kept (CtlSetup::global: 0 the default, 16; n: channel n)
#define CTL_GLOBAL_DEFAULT 15
inline int ctlGlobal(uint8_t kept) { return kept ? (kept - 1) & 15 : CTL_GLOBAL_DEFAULT; }

struct CtlConfig {
  char label[CTL_LABEL];  // what the page shows ("" shows "CC n")
  uint8_t cc;             // 0-127
  uint8_t ch;             // 0-15 (channel 1-16), or CH_GLOBAL: the Global channel (CtlSetup::global)
  uint8_t lo, hi;         // the values it sends, lo <= hi (0, 127: all of them)
  uint8_t invert;         // 1: position 0 sends hi, 127 sends lo (KIND_REL, KIND_INCDEC: the turn's other way)
  uint8_t kind;           // CtlKind
  uint8_t rel;            // KIND_REL: CtlRel. KIND_PB: 1, back to the middle when let go
  uint8_t cc2;            // KIND_INCDEC: the anticlockwise CC (cc is the clockwise one)
  uint8_t incVal;         // KIND_INCDEC: the value both send
};

// everything kept, under CTL_KEY: bytes only, so its size and layout are the same everywhere
struct CtlSetup {
  uint8_t version;          // CTL_VERSION
  uint8_t step;             // the encoder: positions per detent, 1-CTL_STEP_MAX
  uint8_t selected;         // the selected control (MODE_SELECT), or CTL_NONE
  uint8_t global;           // (FM Buddy 2.0, a spare byte before) the Global channel: 0 channel 16, n channel n
  uint8_t bank[GROUPS];     // each group's bank on the screen, 0-3
  uint8_t mode[GROUPS];     // each group's CtlMode
  CtlConfig ctl[CTL_COUNT];  // the faders, the knobs, then CTL_ENC
  uint8_t pos[CTL_COUNT];    // positions, 0-127, as they were last
};

enum CtlOrigin { ORIGIN_USER,
                 ORIGIN_MIDI,
                 ORIGIN_CONFIG };

namespace midictl {
// the defaults: faders bank 1 pitch bend (back to the middle when let go), mod wheel (CC 1), expression (11), cutoff
// (74); banks 2-4 other common sound controllers (resonance, 71, on bank 4); knobs on the CCs MIDI leaves undefined (20-31, 85-86, 102-119); the encoder's own: cutoff. All on
// channel 1, full range, not inverted. Both pages in MODE_PLAY, bank 1, nothing selected
void defaults(CtlSetup& s);

// what hal::load gave (found: it was there, at this size): kept as it is if it's this version, every field brought into
// range (a bad one gets its default). Not found, or another version: the defaults. Returns how many fields it fixed,
// or -1 when it started from the defaults
int restore(CtlSetup& s, bool found);

// one assignment brought into range (lo > hi swapped, a label without its 0 ended): how many fields it fixed
int sanitize(CtlConfig& c, const CtlConfig& fallback);

// what version 1 kept (old: n bytes, as hal::load gave them): as this version, every control KIND_ABS. False if it
// isn't version 1 (then s is untouched)
bool upgrade(const void* old, size_t n, CtlSetup& s);
size_t v1Size();  // version 1's size, to load it by

// KIND_REL: a turn of n clicks (-63..63) as the value sent
uint8_t relValue(int rel, int n);

// KIND_PB: position (0-127; 64 the middle) to pitch bend (0-16383; 8192 the middle), and back
uint16_t toBend(const CtlConfig& c, uint8_t pos);
uint8_t bendPos(const CtlConfig& c, uint16_t bend);

// a kept setup from before pitch bend: faders bank 1 (mod wheel, cutoff, resonance, expression) and bank 4 (GP 1-4)
// still exactly the old defaults take the new ones (bank 1 pitch bend, mod wheel, expression, cutoff; bank 4
// resonance, GP 1-3). Banks changed in any way stay as they are. Returns the banks changed (0-2)
int adoptNewDefaults(CtlSetup& s);

// control i's default assignment and position
CtlConfig defaultOf(int i);
uint8_t defaultPosOf(int i);

// position (0-127) to the value it sends, and back: a value outside lo..hi counts as the nearer end. When lo == hi a
// value says nothing about the position: toPos gives back current
uint8_t toValue(const CtlConfig& c, uint8_t pos);
uint8_t toPos(const CtlConfig& c, uint8_t value, uint8_t current);

// the label to show: its own, or "CC n" (out: at least CTL_LABEL bytes)
const char* label(const CtlConfig& c, char* out);

// the groups: how many a bank shows, the first control of a bank, and which group control i is in (-1: CTL_ENC)
int perBank(int group);
int first(int group, int bank);
int groupOf(int i);
inline bool inBank(int i, int group, int bank) { return i >= first(group, bank) && i < first(group, bank) + perBank(group); }
}  // namespace midictl

// the controls as they're played: the kept setup, and what was last sent of each
class MidiControls {
 public:
  void begin(const CtlSetup& s);  // (every control as not sent yet)
  const CtlSetup& setup() const { return s_; }

  // control i to position pos. With ORIGIN_USER, and a value different from the one last sent: msg is the CC to
  // send (3 bytes) and it returns true. Otherwise false (nothing to send)
  bool set(int i, uint8_t pos, CtlOrigin origin, uint8_t msg[3]);
  uint8_t pos(int i) const { return s_.pos[i]; }
  uint8_t value(int i) const { return midictl::toValue(s_.ctl[i], s_.pos[i]); }
  // what its page shows: its value, or a pitch bend's distance from the middle (-8192..8191)
  int shown(int i) const;

  // which control the encoder turns while group's page shows (-1: no page of these): the selected one when that page
  // is in MODE_SELECT and the selected one is in its group, else the encoder's own (CTL_ENC)
  int target(int group) const;
  // control i turned by detents (+ clockwise; the encoder, or a finger sliding on a knob). KIND_ABS: step positions a
  // detent, to 0-127, as set(USER). KIND_REL: one message, the turn (step a detent, up to 63) in its encoding.
  // KIND_INCDEC: one message a detent (up to CTL_MSGS). msgs gets them; returns how many (0: nothing to send)
  int move(int i, int detents, uint8_t msgs[][3]);
  // the encoder turned: its target (above) moves. moved gets which
  int turn(int detents, int group, uint8_t msgs[][3], int* moved);
  // the last turn of a KIND_REL or KIND_INCDEC control, in clicks (+ clockwise), for its page to show
  int lastTurn(int i) const { return turned_[i]; }

  // an incoming CC (status 0xB0-0xBF): every KIND_ABS control on its channel and number takes the value (ORIGIN_MIDI;
  // relative ones have no position to show). A pitch bend (0xE0-0xEF; cc its low 7 bits, value its high 7): every
  // KIND_PB control on its channel. changed gets their indexes (room for CTL_COUNT); returns how many. Nothing is
  // ever sent for it
  int feedback(uint8_t status, uint8_t cc, uint8_t value, int* changed);

  // a bank (group's bank b) back to its defaults: assignments and positions (ORIGIN_CONFIG)
  void resetBank(int group, int b);

  // a new assignment for control i (ORIGIN_CONFIG): its position stays, what it sends next is from the new one
  void assign(int i, const CtlConfig& c);
  void setGlobal(uint8_t kept) { s_.global = kept > 16 ? 0 : kept; }  // (CtlSetup::global)

  // each group's bank on the screen (0-3, out of range: kept as it is). The page lets go of its fingers first
  void bank(int group, int b);
  int bank(int group) const { return s_.bank[group]; }

  // each group's mode
  void mode(int group, CtlMode m);
  CtlMode mode(int group) const { return (CtlMode)s_.mode[group]; }

  // the selected control (radio: selecting one unselects the other), CTL_NONE for none
  void select(int i);
  int selected() const { return s_.selected == CTL_NONE ? -1 : s_.selected; }
  // short press of the encoder: the next control of group's bank on the screen is selected (the first if none of them)
  int selectNext(int group);

  // the encoder's step (1-CTL_STEP_MAX)
  void step(int n);

 private:
  CtlSetup s_{};
  int16_t sent_[CTL_COUNT];  // the value last sent (or last heard: feedback), -1 none
  int8_t turned_[CTL_COUNT];  // the last turn (KIND_REL, KIND_INCDEC)
};

// a finger's way from where it landed (dx, dy pixels, + right and down): not decided yet, a slide along its control (a
// fader's or a knob's travel is up and down), or a swipe to the next page (leftwards) or the previous one (rightwards)
enum Gesture { GESTURE_UNDECIDED,
               GESTURE_SLIDE,
               GESTURE_NEXT,
               GESTURE_PREV };
#define GESTURE_SLIDE_PX 6   // this far up or down (and more than sideways): a slide
#define GESTURE_SWIPE_PX 40  // this far sideways (and twice as far as up or down): a swipe

namespace midictl {
int gesture(int dx, int dy);
// the pages in a row, as swipes go through them: the faders' banks 1-4, then the knobs' 1-4, then round again. From
// group's bank, dir pages on (+1 next, -1 previous): group and bank become that page's
void nextPage(int& group, int& bank, int dir);
}  // namespace midictl

// fingers on a page of controls: which finger holds which control. A finger takes the control it lands on, if no
// other finger holds it, and keeps it until it lifts, wherever it slides; a finger that lands on none, or on one already
// held, holds nothing until it lifts (it never takes one by sliding onto it)
#define OWN_FINGERS 2  // the panel feels two

struct OwnPoint {
  uint8_t id;
  int16_t x, y;
};

class TouchOwners {
 public:
  struct Owner {
    bool on;        // a finger is down in this slot
    uint8_t id;     // the panel's id for it
    int ctl;        // the control it holds, -1 none
    bool began;     // it landed on this update (on a control or not)
    int16_t x0, y0;  // where it landed
  };
  // hit: the control at (x, y), -1 none. ended gets the controls let go on this update (room for OWN_FINGERS);
  // returns how many
  int update(const OwnPoint* f, int n, int (*hit)(int x, int y, void* arg), void* arg, int* ended);
  // let go of everything (a bank change, the page going): fingers still down hold nothing until they lift. ended as
  // update's
  int releaseAll(int* ended);
  const Owner& slot(int k) const { return o_[k]; }
  int holder(int ctl) const;  // the slot holding ctl, -1 none

 private:
  Owner o_[OWN_FINGERS]{};
};
