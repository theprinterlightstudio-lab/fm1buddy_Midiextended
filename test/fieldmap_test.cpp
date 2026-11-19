// Granular Field's mapping engine (fieldmap.h): curves, change detection, duplicate CC numbers, the
// floor under each parameter's rate, the budget and the order it is spent in, and silence when no
// finger is on the surface. tools/unit-test.sh builds and runs it
#include "fieldmap.h"

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

static FieldMsg msg[FM_PARAMS];

// everything the engine would send at now, with room for all of it
static int pump(FieldMap& m, uint32_t now, int budget = FM_PARAMS) {
  return fmCollect(m, now, budget, msg, FM_PARAMS);
}

static int sentOn(int n, uint8_t cc) {
  for (int i = 0; i < n; i++) {
    if (msg[i].cc == cc) {
      return msg[i].value;
    }
  }
  return -1;
}

int main() {
  // ---- the curves
  {
    CHECK(fmShape(0, FM_STRAIGHT) == 0);
    CHECK(fmShape(127, FM_STRAIGHT) == 127);
    CHECK(fmShape(64, FM_STRAIGHT) == 64);
    CHECK(fmShape(0, FM_STRAIGHT | FM_INVERT) == 127);
    CHECK(fmShape(127, FM_STRAIGHT | FM_INVERT) == 0);
    // soft hangs back in the middle, firm runs ahead, and both still reach the ends
    CHECK(fmShape(64, FM_SOFT) < 64);
    CHECK(fmShape(64, FM_FIRM) > 64);
    CHECK(fmShape(0, FM_SOFT) == 0 && fmShape(127, FM_SOFT) == 127);
    CHECK(fmShape(0, FM_FIRM) == 0 && fmShape(127, FM_FIRM) == 127);
    CHECK(fmShape(-5, FM_STRAIGHT) == 0 && fmShape(300, FM_STRAIGHT) == 127);  // nothing escapes 0..127
  }

  // ---- nothing is sent while nothing is touching
  {
    FieldMap m;
    fmBegin(m);
    CHECK(pump(m, 1000) == 0);
    CHECK(pump(m, 5000) == 0);
  }

  // ---- a value goes once, and then only when it changes
  {
    FieldMap m;
    fmBegin(m);
    fmSet(m, FM_POS, 40);
    CHECK(pump(m, 1000) == 1);
    CHECK(msg[0].cc == 16 && msg[0].value == 40);
    fmSet(m, FM_POS, 40);  // the finger has not moved
    CHECK(pump(m, 1100) == 0);
    CHECK(pump(m, 2000) == 0);
    fmSet(m, FM_POS, 41);
    CHECK(pump(m, 2000) == 1 && msg[0].value == 41);
  }

  // ---- a finger lifting leaves its last value standing, and says nothing more
  {
    FieldMap m;
    fmBegin(m);
    fmSet(m, FM_POS, 90);
    CHECK(pump(m, 1000) == 1 && msg[0].value == 90);
    fmIdle(m);  // every finger up
    CHECK(pump(m, 1100) == 0);
    CHECK(pump(m, 9000) == 0);  // and still nothing, however long the glow takes to fade
    CHECK(m.sent[FM_POS] == 90);
  }

  // ---- the floor under one parameter's rate
  {
    FieldMap m;
    fmBegin(m);
    fmSet(m, FM_POS, 10);
    CHECK(pump(m, 1000) == 1);
    fmSet(m, FM_POS, 11);
    CHECK(pump(m, 1000 + FM_FLOOR_MS[FM_POS] - 1) == 0);  // too soon
    CHECK(pump(m, 1000 + FM_FLOOR_MS[FM_POS]) == 1 && msg[0].value == 11);
    // and the value that finally goes is the latest one, not the one that was held back
    fmSet(m, FM_POS, 12);
    fmSet(m, FM_POS, 13);
    fmSet(m, FM_POS, 14);
    uint32_t t = 1000 + FM_FLOOR_MS[FM_POS] * 2;
    CHECK(pump(m, t) == 1 && msg[0].value == 14);
  }

  // ---- a sweep cannot machine-gun: 128 steps in 100 ms, counted
  {
    FieldMap m;
    fmBegin(m);
    int sends = 0;
    for (int ms = 0; ms <= 100; ms += 10) {  // the panel is read every 10 ms
      fmSet(m, FM_POS, ms * 127 / 100);
      sends += pump(m, 1000 + ms);
    }
    CHECK(sends <= 100 / FM_FLOOR_MS[FM_POS] + 1);
    CHECK(sends > 0);
  }

  // ---- two parameters on one CC: one of them sends, always the same one, with the larger value
  {
    FieldMap m;
    fmBegin(m);
    m.cc[FM_SIZE] = m.cc[FM_POS];  // both on CC 16
    fmSet(m, FM_POS, 30);
    fmSet(m, FM_SIZE, 70);
    int n = pump(m, 1000);
    CHECK(n == 1);                                    // one message, not two
    CHECK(msg[0].cc == 16 && msg[0].value == 70);     // the larger of them
    CHECK(m.sent[FM_POS] == 70 && m.sent[FM_SIZE] == 70);  // both know it went
    // and it stays deterministic as the values swap over
    fmSet(m, FM_POS, 80);
    fmSet(m, FM_SIZE, 20);
    n = pump(m, 2000);
    CHECK(n == 1 && msg[0].cc == 16 && msg[0].value == 80);
  }

  // ---- the owner of a CC number has no source, but something else on that number does
  {
    FieldMap m;
    fmBegin(m);
    m.cc[FM_SIZE] = m.cc[FM_POS];  // both on CC 16
    fmSet(m, FM_SIZE, 88);         // and only size has anything to say
    int n = pump(m, 1000);
    CHECK(n == 1);
    CHECK(msg[0].cc == 16 && msg[0].value == 88);  // it still goes

    // and it is held to its own floor, not to the floor of the dead parameter that is listed first
    fmSet(m, FM_SIZE, 89);
    CHECK(pump(m, 1000 + FM_FLOOR_MS[FM_SIZE] - 1) == 0);
    CHECK(pump(m, 1000 + FM_FLOOR_MS[FM_SIZE]) == 1 && msg[0].value == 89);
  }

  // ---- and when both are live again, the agreed rule is exactly as before
  {
    FieldMap m;
    fmBegin(m);
    m.cc[FM_SIZE] = m.cc[FM_POS];
    fmSet(m, FM_SIZE, 40);  // size first, alone
    CHECK(pump(m, 1000) == 1 && msg[0].value == 40);
    fmSet(m, FM_POS, 90);  // now position joins it
    fmSet(m, FM_SIZE, 40);
    int n = pump(m, 2000);
    CHECK(n == 1 && msg[0].cc == 16 && msg[0].value == 90);  // the larger, as always
    CHECK(m.sent[FM_POS] == 90 && m.sent[FM_SIZE] == 90);
    fmSet(m, FM_POS, 20);
    fmSet(m, FM_SIZE, 70);
    n = pump(m, 3000);
    CHECK(n == 1 && msg[0].value == 70);  // still the larger, whichever of them it is
  }

  // ---- nothing is marked as sent that was not actually emitted
  {
    FieldMap m;
    fmBegin(m);
    for (int p = 0; p < FM_PARAMS; p++) {
      fmSet(m, p, 60 + p);
    }
    int n = fmCollect(m, 1000, 1, msg, FM_PARAMS);  // room for one message only
    CHECK(n == 1);
    int gone = 0;
    for (int p = 0; p < FM_PARAMS; p++) {
      gone += m.sent[p] >= 0;
    }
    CHECK(gone == 1);  // one sent, five still untouched

    // the five that waited are sent later, with their values intact
    n = fmCollect(m, 2000, FM_PARAMS, msg, FM_PARAMS);
    CHECK(n == 5);
    for (int p = 0; p < FM_PARAMS; p++) {
      CHECK(m.sent[p] == 60 + p);
    }
    // and max is respected as well as budget: never more written than there is room for
    FieldMap m2;
    fmBegin(m2);
    for (int p = 0; p < FM_PARAMS; p++) {
      fmSet(m2, p, 10 + p);
    }
    CHECK(fmCollect(m2, 1000, FM_PARAMS, msg, 2) == 2);
    int gone2 = 0;
    for (int p = 0; p < FM_PARAMS; p++) {
      gone2 += m2.sent[p] >= 0;
    }
    CHECK(gone2 == 2);
  }

  // ---- the budget, and what it is spent on first
  {
    FieldMap m;
    fmBegin(m);
    for (int p = 0; p < FM_PARAMS; p++) {
      fmSet(m, p, 50 + p);
    }
    int n = fmCollect(m, 1000, 2, msg, FM_PARAMS);  // room for two only
    CHECK(n == 2);
    CHECK(sentOn(n, 16) >= 0);  // position
    CHECK(sentOn(n, 17) >= 0);  // size
    CHECK(sentOn(n, 18) < 0);   // density waited
    CHECK(sentOn(n, 21) < 0);   // and so did wet

    // with no room at all, nothing goes and nothing is marked as having gone
    FieldMap m2;
    fmBegin(m2);
    fmSet(m2, FM_POS, 99);
    CHECK(fmCollect(m2, 1000, 0, msg, FM_PARAMS) == 0);
    CHECK(m2.sent[FM_POS] == -1);
    CHECK(fmCollect(m2, 1000, 1, msg, FM_PARAMS) == 1 && msg[0].value == 99);
  }

  // ---- position keeps going while the rest are squeezed out
  {
    FieldMap m;
    fmBegin(m);
    int pos = 0, others = 0;
    for (int ms = 0; ms <= 300; ms += 10) {
      for (int p = 0; p < FM_PARAMS; p++) {
        fmSet(m, p, (ms + p * 7) % 128);
      }
      int n = fmCollect(m, 1000 + ms, 1, msg, FM_PARAMS);  // one message a pass, the tightest case
      for (int i = 0; i < n; i++) {
        (msg[i].cc == 16 ? pos : others)++;
      }
    }
    CHECK(pos > 0);
    CHECK(pos >= others);  // position is never the one starved
  }

  // ---- the curve is applied where the value is set, so what is sent is what is shaped
  {
    FieldMap m;
    fmBegin(m);
    m.curve[FM_SIZE] = FM_STRAIGHT | FM_INVERT;
    fmSet(m, FM_SIZE, 0);
    int n = pump(m, 1000);
    CHECK(n == 1 && msg[0].cc == 17 && msg[0].value == 127);
  }

  // ---- a parameter with no source is never sent, however long it is left
  {
    FieldMap m;
    fmBegin(m);
    fmSet(m, FM_POS, 64);
    CHECK(pump(m, 1000) == 1);
    CHECK(pump(m, 1000 + 5000) == 0);  // density, spray, spread and wet have no source in Stage 2
    CHECK(m.sent[FM_DENSITY] == -1 && m.sent[FM_WET] == -1);
  }


  // ---- Stage 4: the 80 ms slide when the voice moves to another finger
  {
    FieldMap m{};
    fmBegin(m);
    FieldMsg out[FM_PARAMS];
    uint32_t t = 1000;
    fmSet(m, FM_POS, 20);
    int n = fmCollect(m, t, FM_PARAMS, out, FM_PARAMS);
    CHECK(n == 1 && out[0].value == 20);
    CHECK(m.sent[FM_POS] == 20);

    // the voice moves: the new finger is at 100, but the synth has 20
    fmRamp(m, t);
    CHECK(m.ramping[FM_POS]);
    CHECK(m.rampFrom[FM_POS] == 20);
    fmSet(m, FM_POS, 100);

    // it does not jump
    t += 10;
    n = fmCollect(m, t, FM_PARAMS, out, FM_PARAMS);
    CHECK(n == 1);
    CHECK(out[0].value > 20 && out[0].value < 100);
    int first = out[0].value;

    // it keeps moving the same way
    t += 20;
    n = fmCollect(m, t, FM_PARAMS, out, FM_PARAMS);
    CHECK(n == 1 && out[0].value > first && out[0].value < 100);

    // and by FM_RAMP_MS it is exactly there
    t = 1000 + FM_RAMP_MS;
    n = fmCollect(m, t, FM_PARAMS, out, FM_PARAMS);
    CHECK(n == 1 && out[0].value == 100);
    CHECK(!m.ramping[FM_POS]);
    CHECK(!fmRamping(m));
  }

  // a starved budget delays the slide being heard but never leaves the value short of its target
  {
    FieldMap m{};
    fmBegin(m);
    FieldMsg out[FM_PARAMS];
    uint32_t t = 2000;
    fmSet(m, FM_POS, 0);
    fmCollect(m, t, FM_PARAMS, out, FM_PARAMS);
    fmRamp(m, t);
    fmSet(m, FM_POS, 127);
    for (uint32_t k = 1; k <= 20; k++) {  // nothing may go out for the whole of the slide
      fmCollect(m, t + k * 10, 0, out, FM_PARAMS);
    }
    int n = fmCollect(m, t + 400, FM_PARAMS, out, FM_PARAMS);
    CHECK(n == 1 && out[0].value == 127);  // the first thing it can say is the target, not a middle
    CHECK(!m.ramping[FM_POS]);
  }

  // every parameter lands exactly on its target by the deadline, whatever its own rate floor is.
  // FM_RAMP_MS is longer than the longest floor, so no floor can swallow a slide's last step; the
  // `landed` exemption in fmCollect is the guard that keeps that true if either constant moves
  {
    for (int p = 0; p < FM_PARAMS; p++) {
      FieldMap m{};
      fmBegin(m);
      FieldMsg out[FM_PARAMS];
      uint32_t t = 3000;
      fmSet(m, p, 0);
      fmCollect(m, t, FM_PARAMS, out, FM_PARAMS);
      CHECK(m.sent[p] == 0);
      fmRamp(m, t);
      fmSet(m, p, 100);
      uint32_t before = m.at[p];
      uint32_t end = t + FM_RAMP_MS;
      int n = fmCollect(m, end, FM_PARAMS, out, FM_PARAMS);
      CHECK(end - before >= FM_FLOOR_MS[p]);  // (so the floor is not what let it through)
      CHECK(n == 1 && out[0].value == 100);   // it is exactly there, not near it
      CHECK(!m.ramping[p]);
    }
  }

  // the slide starts from the CC's owner, not from a sharer's stale copy of it
  {
    FieldMap m{};
    fmBegin(m);
    FieldMsg out[FM_PARAMS];
    uint32_t t = 4000;
    m.cc[FM_SPRAY] = m.cc[FM_POS];  // two parameters, one CC number
    fmSet(m, FM_POS, 10);
    fmCollect(m, t, FM_PARAMS, out, FM_PARAMS);
    m.sent[FM_SPRAY] = 99;  // a stale value of its own, which must not be used as a starting point
    fmRamp(m, t);
    CHECK(m.rampFrom[FM_SPRAY] == m.sent[fmOwner(m, FM_SPRAY)]);
    CHECK(m.rampFrom[FM_SPRAY] != 99);
  }

  // a parameter never sent has nothing to slide from and simply takes its value
  {
    FieldMap m{};
    fmBegin(m);
    FieldMsg out[FM_PARAMS];
    fmRamp(m, 5000);
    CHECK(!m.ramping[FM_POS]);
    fmSet(m, FM_POS, 64);
    int n = fmCollect(m, 5000, FM_PARAMS, out, FM_PARAMS);
    CHECK(n == 1 && out[0].value == 64);
  }

  if (fails) {
    printf("fieldmap: %d failed\n", fails);
    return 1;
  }
  printf("fieldmap: ok\n");
  return 0;
}
