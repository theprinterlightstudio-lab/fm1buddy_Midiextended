// SPDX-License-Identifier: MIT
// Granular Field: the shadow granules on the computer. The one that matters is the last section --
// a granule's note-off must happen whatever else goes wrong.
#include "../src/app/fieldshadow.h"

#include <stdio.h>

static int fails, checks;
static void ok(bool cond, const char* what) {
  checks++;
  if (!cond) {
    fails++;
    printf("  FAIL %s\n", what);
  }
}
static int countOf(const FsEvent* e, int n, int kind) {
  int c = 0;
  for (int i = 0; i < n; i++) {
    c += e[i].kind == kind;
  }
  return c;
}

static void spawning() {
  printf("spawning\n");
  FieldShadow s;
  fsBegin(s, 1234, 127);
  FsEvent e[8];
  ok(!fsDue(s, 1000), "nothing is due before a touch");
  fsTouch(s, 99, 1000);
  ok(fsDue(s, 1000), "the first is due at once");
  int n = fsSpawn(s, 60, 48, 1000, e, 8);
  ok(n == 1 && e[0].kind == FS_ON && e[0].note == 60, "it starts");
  ok(fsLive(s) == 1, "one is sounding");
  fsAfter(s, 1000, 127);
  ok(!fsDue(s, 1000), "the next is not due yet");
  ok(fsDue(s, 1000 + FS_IDLE_MS), "but it comes");
}

static void amountOff() {
  printf("no texture asked for\n");
  FieldShadow s;
  fsBegin(s, 1, 0);
  fsTouch(s, 1, 1000);
  ok(!fsDue(s, 9000), "nothing is ever due at zero");
  fsAfter(s, 1000, 127);
  ok(s.nextAt == 0, "and nothing is booked");
}

static void speedThins() {
  printf("a still hand is thinned, not stopped\n");
  FieldShadow s;
  fsBegin(s, 1, 127);
  uint32_t fast = fsGap(s, 127), slow = fsGap(s, 0), mid = fsGap(s, 64);
  ok(fast > 0 && slow > 0, "both are real gaps");
  ok(fast < mid && mid < slow, "faster means closer together");
  ok(slow >= FS_FAST_MS, "and a still hand still gets them");
}

static void amountThins() {
  printf("less texture, longer gaps\n");
  FieldShadow a, b;
  fsBegin(a, 1, 127);
  fsBegin(b, 1, 20);
  ok(fsGap(b, 100) > fsGap(a, 100), "a small amount is sparser than a large one");
}

static void concurrency() {
  printf("the concurrency cap\n");
  FieldShadow s;
  fsBegin(s, 7, 127);
  fsTouch(s, 7, 1000);
  FsEvent e[8];
  int started = 0;
  for (int i = 0; i < FS_MAX + 4; i++) {
    started += fsSpawn(s, 60 + i, 48, 1000, e, 8);
  }
  ok(started == FS_MAX, "only FS_MAX ever start");
  ok(fsLive(s) == FS_MAX, "and only FS_MAX are sounding");
  int n = fsTick(s, 1000 + FS_DUR_MS, e, 8);
  ok(n == FS_MAX, "they all stop together when their time comes");
  ok(fsLive(s) == 0, "nothing is left");
}

static void deadlines() {
  printf("every granule stops on its own deadline\n");
  FieldShadow s;
  fsBegin(s, 3, 127);
  fsTouch(s, 3, 1000);
  FsEvent e[8];
  fsSpawn(s, 60, 48, 1000, e, 8);
  fsSpawn(s, 64, 48, 1100, e, 8);
  ok(fsLive(s) == 2, "two sounding");
  int n = fsTick(s, 1000 + FS_DUR_MS, e, 8);
  ok(n == 1 && e[0].note == 60, "the first stops first");
  ok(fsLive(s) == 1, "the second is still going");
  n = fsTick(s, 1100 + FS_DUR_MS, e, 8);
  ok(n == 1 && e[0].note == 64, "and then it stops");
  ok(fsLive(s) == 0, "nothing is left sounding");
}

static void velocityFalls() {
  printf("velocity falls with distance\n");
  ok(fsVel(60, 60) == FS_VEL_NEAR, "on the lead's note it is loudest");
  ok(fsVel(60 + FS_FALL, 60) == FS_VEL_FAR, "a long way off it is quietest");
  ok(fsVel(60 + FS_FALL * 2, 60) == FS_VEL_FAR, "and no quieter than that");
  ok(fsVel(66, 60) == fsVel(54, 60), "above and below are the same");
  ok(fsVel(64, 60) > fsVel(72, 60), "nearer is louder");
}

static void collision() {
  printf("a granule on the lead's own note\n");
  FieldShadow s;
  fsBegin(s, 5, 127);
  FsEvent e[8];
  int n = fsSpawn(s, 60, 60, 1000, e, 8);
  ok(n == 1, "it is still played, not dropped");
  ok(e[0].note == 72, "an octave up: the same pitch class, so in key in any scale");
}

static void deterministic() {
  printf("the same gesture twice\n");
  FieldShadow a, b;
  fsBegin(a, 1, 127);
  fsBegin(b, 1, 127);
  fsTouch(a, 4242, 1000);
  fsTouch(b, 4242, 1000);
  bool same = true;
  for (int i = 0; i < 20; i++) {
    same = same && fsPick(a, 8) == fsPick(b, 8);
  }
  ok(same, "the same seed gives the same picks");

  FieldShadow c;
  fsBegin(c, 1, 127);
  fsTouch(c, 777, 1000);
  bool differs = false;
  FieldShadow d;
  fsBegin(d, 1, 127);
  fsTouch(d, 4242, 1000);
  for (int i = 0; i < 20; i++) {
    differs = differs || fsPick(c, 8) != fsPick(d, 8);
  }
  ok(differs, "a different seed gives a different gesture");
}

static void freezeRepeats() {
  printf("freeze\n");
  FieldShadow s;
  fsBegin(s, 1, 127);
  fsTouch(s, 2024, 1000);
  s.freeze = true;
  int first[FS_PATTERN], second[FS_PATTERN];
  for (int i = 0; i < FS_PATTERN; i++) {
    first[i] = fsPick(s, 8);
    s.spawned++;
  }
  for (int i = 0; i < FS_PATTERN; i++) {
    second[i] = fsPick(s, 8);
    s.spawned++;
  }
  bool same = true;
  for (int i = 0; i < FS_PATTERN; i++) {
    same = same && first[i] == second[i];
  }
  ok(same, "the pattern comes round again");

  FieldShadow u;
  fsBegin(u, 1, 127);
  fsTouch(u, 2024, 1000);
  int a[FS_PATTERN * 2];
  bool differs = false;
  for (int i = 0; i < FS_PATTERN * 2; i++) {
    a[i] = fsPick(u, 8);
    u.spawned++;
  }
  for (int i = 0; i < FS_PATTERN; i++) {
    differs = differs || a[i] != a[i + FS_PATTERN];
  }
  ok(differs, "unfrozen, it keeps wandering");
}

// ---- the one that matters ---------------------------------------------------------------------
// A granule's stop belongs to the granule. Nothing that happens to the finger, the page or the mode
// may leave one sounding
static void nothingIsStranded() {
  printf("nothing can strand a note\n");

  {  // the finger goes while granules are sounding
    FieldShadow s;
    fsBegin(s, 9, 127);
    fsTouch(s, 9, 1000);
    FsEvent e[8];
    fsSpawn(s, 60, 48, 1000, e, 8);
    fsSpawn(s, 67, 48, 1000, e, 8);
    fsIdle(s);  // the finger has gone
    ok(fsLive(s) == 2, "they are still sounding, as they should be");
    ok(!fsDue(s, 99999), "but nothing new is spawned, ever");
    int n = fsTick(s, 1000 + FS_DUR_MS, e, 8);
    ok(n == 2, "and they both stop on their own deadlines");
    ok(fsLive(s) == 0, "with nothing left");
  }

  {  // the page or the mode is left: everything off at once
    FieldShadow s;
    fsBegin(s, 9, 127);
    fsTouch(s, 9, 1000);
    FsEvent e[8];
    fsSpawn(s, 60, 48, 1000, e, 8);
    fsSpawn(s, 64, 48, 1000, e, 8);
    fsSpawn(s, 67, 48, 1000, e, 8);
    int n = fsAllOff(s, e, 8);
    ok(n == 3, "all three are stopped");
    ok(countOf(e, n, FS_OFF) == 3, "with a note-off each");
    ok(fsLive(s) == 0, "nothing is sounding");
    ok(!fsDue(s, 99999), "and nothing will be");
  }

  {  // time runs backwards or jumps: the deadline still fires
    FieldShadow s;
    fsBegin(s, 9, 127);
    fsTouch(s, 9, 0xFFFFFF00u);  // close to the wrap
    FsEvent e[8];
    fsSpawn(s, 60, 48, 0xFFFFFF00u, e, 8);
    int n = fsTick(s, 0xFFFFFF00u + FS_DUR_MS, e, 8);  // wraps past zero
    ok(n == 1 && e[0].kind == FS_OFF, "a granule spawned before the clock wraps still stops");
    ok(fsLive(s) == 0, "nothing is left over the wrap");
  }

  {  // every granule ever started is eventually stopped, over a long run
    FieldShadow s;
    fsBegin(s, 11, 127);
    fsTouch(s, 11, 1000);
    FsEvent e[8];
    int on = 0, off = 0;
    uint32_t now = 1000;
    for (int step = 0; step < 4000; step++) {
      now += 5;
      off += countOf(e, fsTick(s, now, e, 8), FS_OFF);
      if (fsDue(s, now)) {
        on += countOf(e, fsSpawn(s, 60 + (int)(now % 13), 48, now, e, 8), FS_ON);
        fsAfter(s, now, (int)(now % 128));
      }
    }
    fsIdle(s);
    for (int step = 0; step < 200; step++) {
      now += 5;
      off += countOf(e, fsTick(s, now, e, 8), FS_OFF);
    }
    ok(on > 20, "a good many were played");
    ok(on == off, "and every single one was stopped");
    ok(fsLive(s) == 0, "with none sounding at the end");
  }
}


static void modes() {
  printf("the modes\n");
  FieldShadow s;
  fsBegin(s, 5, 127);
  fsTouch(s, 5, 1000);

  s.mode = FS_ARP;
  int first = fsChoice(s);
  bool climbs = true, repeats = false;
  int prev = first;
  for (int i = 1; i <= FS_ARP_SPAN; i++) {
    s.spawned++;
    int c = fsChoice(s);
    if (i < FS_ARP_SPAN) {
      climbs = climbs && c > prev;
    } else {
      repeats = c == first;  // round again at the top
    }
    prev = c;
  }
  ok(climbs, "the arpeggio climbs");
  ok(repeats, "and starts over");
  ok(first >= 1, "never on the lead's own rung");

  fsBegin(s, 5, 127);
  s.mode = FS_FIFTH;
  int a = fsChoice(s);
  s.spawned++;
  int b = fsChoice(s);
  s.spawned++;
  int c = fsChoice(s);
  ok(a != b, "the fifth alternates with the octave");
  ok(a == c, "and comes back round");
  ok(a == 4 || a == 7, "both are consonant degrees");

  fsBegin(s, 5, 127);
  s.mode = FS_RANDOM;
  bool inRange = true, varied = false;
  int seen = fsChoice(s);
  for (int i = 0; i < 40; i++) {
    int v = fsChoice(s);
    inRange = inRange && v >= -FS_RAND_SPAN && v <= FS_RAND_SPAN;
    varied = varied || v != seen;
  }
  ok(inRange, "random stays within its span");
  ok(varied, "and actually varies");
}

static void fading() {
  printf("fading away\n");
  FieldShadow s;
  fsBegin(s, 5, 127);
  fsTouch(s, 5, 1000);
  s.fade = 0;
  ok(fsFaded(s, 1000 + 99999) == 127, "fade off: it never dies");

  s.fade = 2;  // medium
  s.movedAt = 1000;
  ok(fsFaded(s, 1000) == 127, "full, the moment the hand moves");
  int mid = fsFaded(s, 1000 + FS_FADE_MS[2] / 2);
  ok(mid > 0 && mid < 127, "half way through it is half there");
  ok(fsFaded(s, 1000 + FS_FADE_MS[2]) == 0, "and then it is gone");

  FsEvent e[8];
  int n = fsSpawn(s, 60, 48, 1000 + FS_FADE_MS[2], e, 8);
  ok(n == 0, "nothing is spawned once it has died");
  ok(fsLive(s) == 0, "and nothing is sounding");

  fsMoving(s, 1000 + FS_FADE_MS[2] + 10, 127);  // the hand moves again
  n = fsSpawn(s, 60, 48, 1000 + FS_FADE_MS[2] + 10, e, 8);
  ok(n == 1, "moving again brings it back");
  ok(e[0].vel > 0, "at full strength");

  fsMoving(s, 2000, 1);  // a crawl is not moving
  ok(s.movedAt != 2000, "a crawl does not count as moving");
  fsMoving(s, 2000, FS_MOVING);
  ok(s.movedAt == 2000, "but FS_MOVING does");
}

static void quiet() {
  printf("velocity is minimal\n");
  ok(FS_VEL_NEAR < 64, "even the nearest granule sits well under a lead at 100");
  ok(fsVel(60, 60) == FS_VEL_NEAR, "nearest is the loudest it gets");
  FieldShadow s;
  fsBegin(s, 5, 127);
  s.fade = 3;
  s.movedAt = 1000;
  FsEvent e[8];
  fsSpawn(s, 60, 60, 1000 + FS_FADE_MS[3] * 3 / 4, e, 8);
  ok(e[0].vel >= 1, "a fading granule never reaches velocity zero, which would be a note-off");
}

int main() {
  spawning();
  amountOff();
  speedThins();
  amountThins();
  concurrency();
  deadlines();
  velocityFalls();
  collision();
  deterministic();
  freezeRepeats();
  modes();
  fading();
  quiet();
  nothingIsStranded();
  printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
  return fails ? 1 : 0;
}
