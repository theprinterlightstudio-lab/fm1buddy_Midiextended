// Granular Field's pitch layer (fieldnote.h): the four granularity modes, what is retriggered and
// what is bent, the note's life from landing to lifting, the mode changing under a held finger, and
// notes switched off. tools/unit-test.sh builds and runs it
#include "fieldnote.h"

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

static FieldEvent ev[FN_EVENTS];

static FieldNoteCfg cfg(int gran, int range = 2, int notes = 1, int lean = 50) {
  FieldNoteCfg c{};
  c.gran = (uint8_t)gran, c.notes = (uint8_t)notes, c.bendRange = (uint8_t)range;
  c.leanCents = (uint8_t)lean, c.vel = 100;
  return c;
}

static int count(int n, uint8_t kind) {
  int k = 0;
  for (int i = 0; i < n; i++) {
    k += ev[i].kind == kind;
  }
  return k;
}

static int firstOf(int n, uint8_t kind) {
  for (int i = 0; i < n; i++) {
    if (ev[i].kind == kind) {
      return ev[i].a;
    }
  }
  return -9999;
}

// where in the list each kind came, so the order of an on and an off can be checked
static int whereOf(int n, uint8_t kind) {
  for (int i = 0; i < n; i++) {
    if (ev[i].kind == kind) {
      return i;
    }
  }
  return -1;
}

// a scale note's pitch with nothing between the steps
static int at(int note) { return note * 256; }

static const int8_t MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
static const int8_t PENTA[5] = {0, 2, 4, 7, 9};
static const int8_t CHROM[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

int main() {
  // ---- where the finger is, in musical terms
  {
    int q, p, lean;
    // the two ends meet the quantized range exactly, on any scale
    for (int s = 0; s < 3; s++) {
      const int8_t* st = s == 0 ? MAJOR : s == 1 ? PENTA
                                                 : CHROM;
      int per = s == 0 ? 7 : s == 1 ? 5
                                    : 12;
      fnPitchAt(0, 320, 48, 2, st, per, q, p, lean);
      CHECK(q == 48 && p == 48 * 256);
      fnPitchAt(319, 320, 48, 2, st, per, q, p, lean);
      CHECK(p == (48 + 24) * 256);  // two octaves up, to the pitch
      CHECK(q == 48 + 24);          // and the top step is the root again
    }

    // FREE is equal tempered: the same place is the same pitch whatever scale is chosen
    for (int x = 0; x < 320; x += 17) {
      int q1, p1, l1, q2, p2, l2;
      fnPitchAt(x, 320, 48, 2, PENTA, 5, q1, p1, l1);
      fnPitchAt(x, 320, 48, 2, CHROM, 12, q2, p2, l2);
      CHECK(p1 == p2);
    }
    // and it really is even: equal steps across give equal pitch
    int prev = -1, step = -1;
    for (int x = 0; x <= 319; x += 11) {
      fnPitchAt(x, 320, 48, 2, MAJOR, 7, q, p, lean);
      if (prev >= 0) {
        int d = p - prev;
        if (step < 0) {
          step = d;
        }
        CHECK(d >= step - 2 && d <= step + 2);  // the same every time, give or take rounding
      }
      prev = p;
    }

    // the quantized note does follow the scale: across the surface the two disagree often, while the
    // pitch under them never does. That is exactly the point of the change
    int differ = 0, same = 0;
    for (int x = 0; x < 320; x++) {
      int qa, qb, pa, pb, la, lb;
      fnPitchAt(x, 320, 48, 2, PENTA, 5, qa, pa, la);
      fnPitchAt(x, 320, 48, 2, CHROM, 12, qb, pb, lb);
      differ += qa != qb;
      same += pa == pb;
    }
    CHECK(differ > 160);  // more than half the surface
    CHECK(same == 320);   // and the pitch agrees everywhere

    // lean runs from the bottom of a step to its top
    fnPitchAt(0, 320, 48, 2, MAJOR, 7, q, p, lean);
    CHECK(lean == -128);
    int atStep = 320 / (7 * 2 + 1);  // one step across
    fnPitchAt(atStep - 1, 320, 48, 2, MAJOR, 7, q, p, lean);
    CHECK(lean > 0);  // near the top of the first step
    fnPitchAt(atStep + 1, 320, 48, 2, MAJOR, 7, q, p, lean);
    CHECK(lean < 0);  // and over into the bottom of the next
    CHECK(q == 48 + 2);
  }

  // ---- bend arithmetic
  {
    CHECK(fnBendFor(0, 2) == 0);
    CHECK(fnBendFor(2 * 256, 2) == 8191);     // the whole range up
    CHECK(fnBendFor(-2 * 256, 2) == -8192);   // and down
    CHECK(fnBendFor(1 * 256, 2) == 4096);     // half of it
    CHECK(fnBendFor(12 * 256, 12) == 8191);   // the same at a wider range
    CHECK(fnBendFor(1 * 256, 12) == 682);     // one semitone of twelve
    CHECK(fnBendFor(99 * 256, 2) == 8191);    // nothing escapes the wheel
    CHECK(fnBendFor(-99 * 256, 2) == -8192);
  }

  // ---- landing and lifting: one note on, one note off, nothing left sounding
  {
    FieldNote s;
    fnBegin(s);
    FieldNoteCfg c = cfg(FN_STEPS);
    int n = fnTouch(s, c, 60, at(60), 0, true, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_ON) == 1 && firstOf(n, FN_NOTE_ON) == 60);
    CHECK(count(n, FN_NOTE_OFF) == 0);
    CHECK(s.on && s.note == 60);
    n = fnUp(s, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_OFF) == 1 && firstOf(n, FN_NOTE_OFF) == 60);
    CHECK(!s.on && s.note == -1);
    n = fnUp(s, ev, FN_EVENTS);  // lifting twice must not send a second off
    CHECK(n == 0);
  }

  // ---- STEPS: the note changes, nothing bends, and the new one starts before the old one stops
  {
    FieldNote s;
    fnBegin(s);
    FieldNoteCfg c = cfg(FN_STEPS);
    fnTouch(s, c, 60, at(60), 0, true, ev, FN_EVENTS);
    int n = fnTouch(s, c, 60, at(60), 100, false, ev, FN_EVENTS);  // same step, well off its middle
    CHECK(n == 0);                                                 // STEPS does not care where in it
    n = fnTouch(s, c, 62, at(62), 0, false, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_ON) == 1 && firstOf(n, FN_NOTE_ON) == 62);
    CHECK(count(n, FN_NOTE_OFF) == 1 && firstOf(n, FN_NOTE_OFF) == 60);
    CHECK(whereOf(n, FN_NOTE_ON) < whereOf(n, FN_NOTE_OFF));  // on first: legato
    CHECK(count(n, FN_BEND_NOW) == 0 && count(n, FN_BEND_GLIDE) == 0);
    CHECK(s.note == 62);
    fnUp(s, ev, FN_EVENTS);
  }

  // ---- SNAP: one note, and the pitch glides to the next while it is within reach
  {
    FieldNote s;
    fnBegin(s);
    FieldNoteCfg c = cfg(FN_SNAP, 12);
    fnTouch(s, c, 60, at(60), 0, true, ev, FN_EVENTS);
    int n = fnTouch(s, c, 62, at(62), 0, false, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_ON) == 0);  // nothing retriggered
    CHECK(count(n, FN_NOTE_OFF) == 0);
    CHECK(count(n, FN_BEND_GLIDE) == 1);
    CHECK(firstOf(n, FN_BEND_GLIDE) == fnBendFor(2 * 256, 12));
    CHECK(s.note == 60 && s.anchor == 60);  // still the note it started on
    // and past the range it takes the note itself, from no bend
    n = fnTouch(s, c, 74, at(74), 0, false, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_ON) == 1 && firstOf(n, FN_NOTE_ON) == 74);
    CHECK(count(n, FN_NOTE_OFF) == 1 && firstOf(n, FN_NOTE_OFF) == 60);
    CHECK(s.note == 74 && s.anchor == 74 && s.bend == 0);
    fnUp(s, ev, FN_EVENTS);
  }

  // ---- LEAN: the note is quantized, and where in the step it sits bends it a little
  {
    FieldNote s;
    fnBegin(s);
    FieldNoteCfg c = cfg(FN_LEAN, 2, 1, 50);
    fnTouch(s, c, 60, at(60), 0, true, ev, FN_EVENTS);
    CHECK(s.bend == 0);  // the middle of the step is the note itself
    int n = fnTouch(s, c, 60, at(60), 127, false, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_ON) == 0);  // the top of the step is still that note
    CHECK(count(n, FN_BEND_NOW) == 1);
    CHECK(s.bend > 0);
    CHECK(s.bend <= fnBendFor(50 * 256 / 100, 2) + 1);  // no further than leanCents
    n = fnTouch(s, c, 60, at(60), -128, false, ev, FN_EVENTS);
    CHECK(s.bend < 0);  // and the other way at the bottom of it
    // a new step is a new note, and the lean is measured in that one
    n = fnTouch(s, c, 62, at(62), 0, false, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_ON) == 1 && firstOf(n, FN_NOTE_ON) == 62);
    CHECK(count(n, FN_NOTE_OFF) == 1 && firstOf(n, FN_NOTE_OFF) == 60);
    CHECK(s.bend == 0);
    fnUp(s, ev, FN_EVENTS);
  }

  // ---- FREE: the pitch follows the finger, with no step in it anywhere
  {
    FieldNote s;
    fnBegin(s);
    FieldNoteCfg c = cfg(FN_FREE, 12);
    fnTouch(s, c, 60, at(60), 0, true, ev, FN_EVENTS);
    CHECK(s.note == 60);
    int last = 0, moves = 0;
    for (int k = 1; k <= 24; k++) {  // a slow slide of a quarter tone at a time
      int p = at(60) + k * 32;
      int n = fnTouch(s, c, 60, p, 0, false, ev, FN_EVENTS);
      CHECK(count(n, FN_NOTE_ON) == 0);  // nothing is retriggered inside the range
      CHECK(count(n, FN_BEND_GLIDE) == 0);  // and FREE does not glide: it follows
      if (n) {
        moves++;
        CHECK(firstOf(n, FN_BEND_NOW) > last);  // always further up, never a step back
        last = firstOf(n, FN_BEND_NOW);
      }
    }
    CHECK(moves > 20);  // it really is continuous
    CHECK(s.note == 60);
    // beyond the range it moves to a new note and carries on from there
    int n = fnTouch(s, c, 60, at(73), 0, false, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_ON) == 1 && firstOf(n, FN_NOTE_ON) == 73);
    CHECK(count(n, FN_NOTE_OFF) == 1 && firstOf(n, FN_NOTE_OFF) == 60);
    CHECK(s.anchor == 73);
    fnUp(s, ev, FN_EVENTS);
  }

  // ---- notes off: nothing sounds, and a note already sounding is stopped
  {
    FieldNote s;
    fnBegin(s);
    FieldNoteCfg on = cfg(FN_STEPS), off = cfg(FN_STEPS, 2, 0);
    int n = fnTouch(s, off, 60, at(60), 0, true, ev, FN_EVENTS);
    CHECK(n == 0 && !s.on);  // nothing at all
    n = fnTouch(s, off, 64, at(64), 0, false, ev, FN_EVENTS);
    CHECK(n == 0 && !s.on);

    fnBegin(s);  // and switched off under a finger that is already playing
    fnTouch(s, on, 60, at(60), 0, true, ev, FN_EVENTS);
    CHECK(s.on);
    n = fnTouch(s, off, 60, at(60), 0, false, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_OFF) == 1 && firstOf(n, FN_NOTE_OFF) == 60);
    CHECK(!s.on);
    n = fnTouch(s, off, 62, at(62), 0, false, ev, FN_EVENTS);
    CHECK(n == 0);  // and it stays quiet
    CHECK(fnUp(s, ev, FN_EVENTS) == 0);  // nothing left to stop
  }

  // ---- the mode changed while the finger was down: no stuck note, no double off
  {
    for (int from = 0; from < FN_GRANS; from++) {
      for (int to = 0; to < FN_GRANS; to++) {
        FieldNote s;
        fnBegin(s);
        FieldNoteCfg a = cfg(from, 12), b = cfg(to, 12);
        fnTouch(s, a, 60, at(60), 0, true, ev, FN_EVENTS);
        CHECK(s.on && s.note == 60);
        int sounding = s.note;
        int n = fnTouch(s, b, 60, at(60), 0, false, ev, FN_EVENTS);
        // whatever happens, one note is sounding afterwards and the books balance
        int ons = count(n, FN_NOTE_ON), offs = count(n, FN_NOTE_OFF);
        CHECK(ons == offs);  // either neither, or one swapped for the other
        CHECK(s.on);
        if (ons) {
          CHECK(firstOf(n, FN_NOTE_OFF) == sounding);
        }
        CHECK(s.anchor == s.note);  // measured from where it actually is
        int held = s.note;          // (fnUp forgets it, so read it first)
        n = fnUp(s, ev, FN_EVENTS);
        CHECK(count(n, FN_NOTE_OFF) == 1 && firstOf(n, FN_NOTE_OFF) == held);
        CHECK(!s.on);
      }
    }
  }

  // ---- the register moved under a held finger (the page stops everything first, then this)
  {
    FieldNote s;
    fnBegin(s);
    FieldNoteCfg c = cfg(FN_STEPS);
    fnTouch(s, c, 60, at(60), 0, true, ev, FN_EVENTS);
    int n = fnUp(s, ev, FN_EVENTS);  // what octave() does: endAll()
    CHECK(count(n, FN_NOTE_OFF) == 1 && firstOf(n, FN_NOTE_OFF) == 60);
    n = fnTouch(s, c, 72, at(72), 0, true, ev, FN_EVENTS);  // the finger is still down, an octave up
    CHECK(count(n, FN_NOTE_ON) == 1 && firstOf(n, FN_NOTE_ON) == 72);
    CHECK(count(n, FN_NOTE_OFF) == 0);  // nothing of the old octave lingers
    fnUp(s, ev, FN_EVENTS);
  }

  // ---- a long slide in every mode leaves nothing sounding once the finger goes
  {
    for (int g = 0; g < FN_GRANS; g++) {
      FieldNote s;
      fnBegin(s);
      FieldNoteCfg c = cfg(g, 2);
      int ons = 0, offs = 0;
      fnTouch(s, c, 48, at(48), 0, true, ev, FN_EVENTS);
      ons++;
      for (int p = at(48); p <= at(84); p += 40) {
        int q = (p + 128) >> 8;
        int n = fnTouch(s, c, q, p, 0, false, ev, FN_EVENTS);
        ons += count(n, FN_NOTE_ON), offs += count(n, FN_NOTE_OFF);
      }
      int n = fnUp(s, ev, FN_EVENTS);
      offs += count(n, FN_NOTE_OFF);
      CHECK(ons == offs);  // every note that started was stopped
      CHECK(!s.on);
    }
  }

  // ---- nothing is written past the room it is given
  {
    FieldNote s;
    fnBegin(s);
    FieldNoteCfg c = cfg(FN_STEPS);
    fnTouch(s, c, 60, at(60), 0, true, ev, FN_EVENTS);
    FieldEvent one[1];
    int n = fnTouch(s, c, 65, at(65), 0, false, one, 1);
    CHECK(n == 1);
  }


  // ---- Stage 4: the shared pitch bend, handed over and taken back (fnBendAt / fnBendSet)
  //
  // Two voices live on one MIDI channel, so one bend serves both. While a second voice sounds the
  // first voice's bend is held at the middle; when the second goes the first takes it back from
  // where its finger is, with no note restarted. These check the query the page does that with
  for (int g = 0; g < FN_GRANS; g++) {
    FieldNoteCfg c = cfg(g, 12);
    FieldNote s{};
    fnBegin(s);
    int q, p, lean;

    // land it in the middle of the surface
    fnPitchAt(160, 320, 48, 2, CHROM, 12, q, p, lean);
    int n = fnTouch(s, c, q, p, lean, true, ev, FN_EVENTS);
    CHECK(count(n, FN_NOTE_ON) == 1);
    int landed = s.note;

    // unbent: the query agrees with what was last asked for
    CHECK(fnBendAt(s, c, q, p, lean) == s.bend);

    // bend it positively by moving right, then negatively by moving left of where it landed
    fnPitchAt(200, 320, 48, 2, CHROM, 12, q, p, lean);
    fnTouch(s, c, q, p, lean, false, ev, FN_EVENTS);
    int up = fnBendAt(s, c, q, p, lean);
    CHECK(up == s.bend);
    if (g != FN_STEPS) {
      CHECK(up > 0);  // STEPS alone never bends
    } else {
      CHECK(up == 0);
    }

    fnPitchAt(120, 320, 48, 2, CHROM, 12, q, p, lean);
    fnTouch(s, c, q, p, lean, false, ev, FN_EVENTS);
    int down = fnBendAt(s, c, q, p, lean);
    CHECK(down == s.bend);
    if (g != FN_STEPS) {
      CHECK(down < 0);
    }

    // the page holds the channel at the middle for the second voice. No note events: the query and
    // the setter are the whole of it
    int sounding = s.note, anchored = s.anchor;
    fnBendSet(s, 0);
    CHECK(s.bend == 0);
    CHECK(s.note == sounding);    // suspending the bend moves no note
    CHECK(s.anchor == anchored);  // and re-anchors nothing
    CHECK(s.on);
    (void)landed;

    // and takes it back where the finger now is. Still no note events
    int back = fnBendAt(s, c, q, p, lean);
    CHECK(back == down);  // the same value it had before it was given up
    fnBendSet(s, back);
    CHECK(s.bend == back);
    CHECK(s.on);
    CHECK(s.note >= 0);

    // asking again changes nothing, and the next ordinary touch at the same place sends no bend
    n = fnTouch(s, c, q, p, lean, false, ev, FN_EVENTS);
    CHECK(count(n, FN_BEND_NOW) + count(n, FN_BEND_GLIDE) == 0);
    CHECK(count(n, FN_NOTE_ON) == 0 && count(n, FN_NOTE_OFF) == 0);
  }

  // a finger that wandered past the bend range while the bend was suspended comes back clamped,
  // not re-anchored: re-anchoring would mean a note change, and reacquisition may not make one
  {
    FieldNoteCfg c = cfg(FN_FREE, 2);  // a narrow range, easy to walk out of
    FieldNote s{};
    fnBegin(s);
    int q, p, lean;
    fnPitchAt(10, 320, 48, 2, CHROM, 12, q, p, lean);
    fnTouch(s, c, q, p, lean, true, ev, FN_EVENTS);
    int anchor = s.anchor;
    fnBendSet(s, 0);                                    // suspended
    fnPitchAt(300, 320, 48, 2, CHROM, 12, q, p, lean);  // and the finger goes a long way
    int back = fnBendAt(s, c, q, p, lean);
    CHECK(s.anchor == anchor);  // the state did not move (fnBendAt takes a const FieldNote&, so
                                // re-anchoring is a compile error, not merely untested)
    CHECK(back == 8191);        // and it saturates in range rather than wrapping
  }

  // notes off, or nothing sounding: there is no bend to give back
  {
    FieldNoteCfg c = cfg(FN_FREE, 12, 0);
    FieldNote s{};
    fnBegin(s);
    CHECK(fnBendAt(s, c, 60, 60 * 256, 0) == 0);
    FieldNoteCfg on = cfg(FN_FREE, 12);
    fnBegin(s);
    CHECK(fnBendAt(s, on, 60, 60 * 256, 0) == 0);  // nothing has landed yet
  }

  if (fails) {
    printf("fieldnote: %d failed\n", fails);
    return 1;
  }
  printf("fieldnote: ok\n");
  return 0;
}
