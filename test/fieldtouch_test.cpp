// SPDX-License-Identifier: MIT
// Granular Field, Stage 4: the touch tracker on the computer. Identity across dropped polls, the
// confirmation window, renumbered contacts, the ambiguity refusal, promotion, and the geometry.
#include "../src/app/fieldtouch.h"

#include <stdio.h>
#include <string.h>

static int fails;
static int checks;

static void ok(bool cond, const char* what) {
  checks++;
  if (!cond) {
    fails++;
    printf("  FAIL %s\n", what);
  }
}

static int countOf(const FtEvent* e, int n, int kind) {
  int c = 0;
  for (int i = 0; i < n; i++) {
    c += e[i].kind == kind;
  }
  return c;
}

static int slotOf(const FtEvent* e, int n, int kind) {
  for (int i = 0; i < n; i++) {
    if (e[i].kind == kind) {
      return e[i].slot;
    }
  }
  return -1;
}

// one poll, with however many contacts
static int poll(FieldTouch& t, uint32_t now, FtEvent* e, const FtPoint* p, int n) {
  return ftPoll(t, p, n, now, e, 8);
}

static void identity() {
  printf("identity across polls\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[2] = {{100, 100, 0}, {0, 0, 0}};
  int n = poll(t, 1000, e, a, 1);
  ok(n == 1 && e[0].kind == FT_DOWN && e[0].slot == 0, "first contact lands in slot 0");
  ok(t.voice == 0, "it is the voice");
  a[0].x = 120;
  n = poll(t, 1010, e, a, 1);
  ok(n == 0, "moving is not an event");
  ok(t.f[0].x == 120, "and it moved");
  a[1] = FtPoint{200, 150, 1};
  n = poll(t, 1020, e, a, 2);
  ok(n == 1 && e[0].kind == FT_DOWN && e[0].slot == 1, "second contact lands in slot 1");
  ok(t.voice == 0, "the voice has not moved");
  ok(ftDown(t) == 2, "two fingers down");
}

static void singleDropout() {
  printf("a single dropped poll\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[1] = {{100, 100, 0}};
  poll(t, 1000, e, a, 1);
  int n = poll(t, 1010, e, 0, 0);  // the panel reports nothing this poll
  ok(n == 0, "no event on the first missing poll");
  ok(ftLive(t, 0), "the finger is still live");
  ok(t.f[0].x == 100, "and has not moved");
  n = poll(t, 1020, e, a, 1);
  ok(n == 0, "it comes back without an event");
  ok(t.f[0].missing == 0, "the counter is cleared");
  ok(ftDown(t) == 1, "still one finger, not two");
}

static void repeatedDropout() {
  printf("repeated short dropouts\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[1] = {{100, 100, 0}};
  poll(t, 1000, e, a, 1);
  uint32_t now = 1000;
  int ev = 0;
  for (int round = 0; round < 6; round++) {  // two missing, one present, over and over
    ev += poll(t, now += 10, e, 0, 0);
    ev += poll(t, now += 10, e, 0, 0);
    ev += poll(t, now += 10, e, a, 1);
  }
  ok(ev == 0, "no event through six rounds of two-poll gaps");
  ok(ftLive(t, 0), "the finger survived all of them");
  ok(t.f[0].downAt == 1000, "and kept its age");
}

static void confirmedRemoval() {
  printf("confirmed removal\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[1] = {{100, 100, 0}};
  poll(t, 1000, e, a, 1);
  ok(poll(t, 1010, e, 0, 0) == 0, "poll 1 missing: nothing");
  ok(poll(t, 1020, e, 0, 0) == 0, "poll 2 missing: nothing");
  int n = poll(t, 1030, e, 0, 0);
  ok(n == 1 && e[0].kind == FT_UP && e[0].slot == 0, "poll 3 missing: confirmed gone");
  ok(!ftLive(t, 0), "the slot is free");
  ok(t.voice == FT_MAX, "and there is no voice");
  ok(countOf(e, n, FT_PROMOTE) == 0, "nothing to promote to");
}

static void cleanLift() {
  printf("a clean lift\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[1] = {{100, 100, 0}};
  poll(t, 1000, e, a, 1);
  int n = ftLift(t, 0, e, 8);
  ok(n == 1 && e[0].kind == FT_UP, "it goes at once, with no waiting");
  ok(!ftLive(t, 0), "the slot is free");
}

static void renumbered() {
  printf("the panel renumbers its contacts\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[2] = {{100, 100, 0}, {220, 180, 1}};
  poll(t, 1000, e, a, 2);
  ok(ftDown(t) == 2, "two down");
  // the first contact vanishes and the second is renumbered to 0, in the same poll
  FtPoint b[1] = {{222, 182, 0}};
  int n = poll(t, 1010, e, b, 1);
  ok(n == 0, "no landing and no removal: it was recognised by where it was");
  ok(t.f[1].id == 0, "slot 1 answers to its new number");
  ok(t.f[1].x == 222, "and took the new position");
  ok(t.f[0].missing == 1, "slot 0 is the one that went missing");
  ok(t.voice == 0, "the voice has not been handed to the wrong finger");
}

static void ambiguity() {
  printf("two missing fingers, one stray contact\n");
  FieldTouch t;
  ftBegin(t, 3);
  FtEvent e[8];
  FtPoint a[2] = {{100, 100, 0}, {100, 140, 1}};  // 40 px apart, vertically: the FT6336's bad case
  poll(t, 1000, e, a, 2);
  poll(t, 1010, e, 0, 0);  // both vanish
  // a contact appears exactly between them: 20 px from each, so neither can claim it
  FtPoint b[1] = {{100, 120, 7}};
  int n = poll(t, 1020, e, b, 1);
  ok(countOf(e, n, FT_DOWN) == 1, "it is taken for a new finger, not for either of theirs");
  ok(slotOf(e, n, FT_DOWN) == 2, "in the free slot");
  ok(t.f[0].used && t.f[1].used, "and both of theirs are still waiting");
  ok(t.voice == 0, "the voice is untouched");
}

static void recoverBound() {
  printf("recovery is bounded\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[1] = {{40, 40, 0}};
  poll(t, 1000, e, a, 1);
  poll(t, 1010, e, 0, 0);
  FtPoint b[1] = {{300, 200, 5}};  // far away: a different finger, not the same one renumbered
  int n = poll(t, 1020, e, b, 1);
  ok(countOf(e, n, FT_DOWN) == 1, "a distant contact is a new finger");
  ok(slotOf(e, n, FT_DOWN) == 1, "and does not steal slot 0");
}

static void promotion() {
  printf("promotion\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[2] = {{100, 100, 0}, {200, 150, 1}};
  poll(t, 1000, e, a, 1);
  a[1].id = 1;
  poll(t, 1010, e, a, 2);
  ok(t.voice == 0, "the first finger is the voice");
  FtPoint b[1] = {{200, 150, 1}};  // the primary goes; the secondary stays
  poll(t, 1020, e, b, 1);
  poll(t, 1030, e, b, 1);
  int n = poll(t, 1040, e, b, 1);
  ok(countOf(e, n, FT_UP) == 1, "the primary is confirmed gone");
  ok(countOf(e, n, FT_PROMOTE) == 1, "and the voice is promoted");
  ok(slotOf(e, n, FT_PROMOTE) == 1, "to the finger still down");
  ok(t.voice == 1, "which now owns the parameters");
  ok(countOf(e, n, FT_DOWN) == 0, "with no landing, so the page plays no new note");
  ok(t.f[1].downAt == 1010, "and keeps the age it landed with");
}

static void promotionOrder() {
  printf("promotion picks the oldest remaining\n");
  FieldTouch t;
  ftBegin(t, 3);
  FtEvent e[8];
  FtPoint a[3] = {{60, 100, 0}, {160, 100, 1}, {260, 100, 2}};
  poll(t, 1000, e, a, 1);
  poll(t, 1010, e, a, 2);
  poll(t, 1020, e, a, 3);
  ok(t.voice == 0, "the first is the voice");
  FtPoint b[2] = {{160, 100, 1}, {260, 100, 2}};
  poll(t, 1030, e, b, 2);
  poll(t, 1040, e, b, 2);
  poll(t, 1050, e, b, 2);
  ok(t.voice == 1, "it goes to the second finger, which landed before the third");
}

// The oldest remaining finger and the lowest free slot are not the same thing. A slot freed early
// and filled again holds the newest finger, and promotion must not hand the voice to it
static void promotionNotBySlot() {
  printf("promotion is by age, not by slot number\n");
  FieldTouch t;
  ftBegin(t, 3);
  FtEvent e[8];
  FtPoint a[3] = {{60, 100, 0}, {160, 100, 1}, {260, 100, 2}};
  poll(t, 1000, e, a, 1);  // A, slot 0
  poll(t, 1010, e, a, 2);  // B, slot 1
  poll(t, 1020, e, a, 3);  // C, slot 2
  ftLift(t, 1, e, 8);      // B goes, freeing slot 1
  ok(!ftLive(t, 1), "slot 1 is free");
  FtPoint b[3] = {{60, 100, 0}, {260, 100, 2}, {120, 200, 9}};
  int n = poll(t, 1100, e, b, 3);  // D lands in the free slot 1, newest of the three
  ok(slotOf(e, n, FT_DOWN) == 1, "D takes slot 1");
  ok(t.f[1].downAt == 1100 && t.f[2].downAt == 1020, "slot 1 is newer than slot 2");
  FtPoint c[2] = {{260, 100, 2}, {120, 200, 9}};  // A, the voice, goes
  poll(t, 1110, e, c, 2);
  poll(t, 1120, e, c, 2);
  n = poll(t, 1130, e, c, 2);
  ok(countOf(e, n, FT_PROMOTE) == 1, "the voice is promoted");
  ok(t.voice == 2, "to slot 2, which is older, and not to slot 1, which is lower");
}

static void dropoutNoPromotion() {
  printf("a dropped poll does not promote\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[2] = {{100, 100, 0}, {200, 150, 1}};
  poll(t, 1000, e, a, 1);
  poll(t, 1010, e, a, 2);
  FtPoint b[1] = {{200, 150, 1}};
  int n = poll(t, 1020, e, b, 1);  // the primary misses one poll only
  ok(n == 0, "no event");
  ok(t.voice == 0, "the voice stays with the primary");
  n = poll(t, 1030, e, a, 2);
  ok(n == 0, "it comes back quietly");
  ok(t.voice == 0, "still the voice");
}

static void spread() {
  printf("spread\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  ok(!t.hasSpread, "nothing to say before two fingers have been down");
  FtPoint a[2] = {{0, 0, 0}, {319, 239, 1}};
  poll(t, 1000, e, a, 2);
  ftSpread(t, 320, 240);
  ok(t.hasSpread, "two fingers give it a value");
  ok(t.spread == 127, "corner to corner is all the way");
  int wide = t.spread;
  FtPoint b[2] = {{150, 120, 0}, {170, 120, 1}};
  poll(t, 1010, e, b, 2);
  ftSpread(t, 320, 240);
  ok(t.spread < wide, "together is less");
  int close = t.spread;
  FtPoint c[1] = {{150, 120, 0}};  // the second lifts, confirmed
  poll(t, 1020, e, c, 1);
  poll(t, 1030, e, c, 1);
  poll(t, 1040, e, c, 1);
  ftSpread(t, 320, 240);
  ok(t.spread == close, "one finger left: the last real value stands, it does not snap to nothing");
  ok(t.spread != 0, "and it is not zero");
}

static void spreadOrder() {
  printf("spread does not care which finger is which\n");
  FieldTouch t, u;
  ftBegin(t, 2);
  ftBegin(u, 2);
  FtEvent e[8];
  FtPoint a[2] = {{60, 80, 0}, {240, 200, 1}};
  FtPoint b[2] = {{240, 200, 0}, {60, 80, 1}};  // the same two places, the other way round
  poll(t, 1000, e, a, 2);
  poll(u, 1000, e, b, 2);
  ftSpread(t, 320, 240);
  ftSpread(u, 320, 240);
  ok(t.spread == u.spread, "the same spread either way");
}

static void speed() {
  printf("speed is per millisecond, not per poll\n");
  FieldTouch t, u;
  ftBegin(t, 2);
  ftBegin(u, 2);
  FtEvent e[8];
  FtPoint a[1] = {{100, 100, 0}};
  poll(t, 1000, e, a, 1);
  poll(u, 1000, e, a, 1);
  ok(ftSpeed(t, 0) == 0, "a finger that has not moved is still");
  FtPoint b[1] = {{140, 100, 0}};  // 40 px
  poll(t, 1010, e, b, 1);          // in 10 ms
  poll(u, 1040, e, b, 1);          // the same 40 px in 40 ms
  ok(ftSpeed(t, 0) > 0, "moving reads as moving");
  ok(ftSpeed(t, 0) > ftSpeed(u, 0), "the same distance over longer reads slower");
  FtPoint c[1] = {{340, 100, 0}};
  poll(t, 1020, e, c, 1);
  ok(ftSpeed(t, 0) == 127, "a fast swipe tops out");
}

static void held() {
  printf("hold time\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[1] = {{100, 100, 0}};
  poll(t, 1000, e, a, 1);
  ok(ftHeld(t, 0, 1000) == 0, "nothing at the moment it lands");
  int early = ftHeld(t, 0, 1000 + 500);
  int later = ftHeld(t, 0, 1000 + 2000);
  ok(early > 0 && later > early, "it grows");
  ok(ftHeld(t, 0, 1000 + FT_WET_MS) == 127, "and tops out");
  ok(ftHeld(t, 0, 1000 + FT_WET_MS * 3) == 127, "and stays there");
  poll(t, 1010, e, 0, 0);  // a dropped poll
  ok(ftHeld(t, 0, 1000 + 2000) == later, "a dropped poll does not restart it");
}

static void beyondSlots() {
  printf("more contacts than the panel can hold\n");
  FieldTouch t;
  ftBegin(t, 2);  // a two-finger panel
  FtEvent e[8];
  FtPoint a[3] = {{60, 100, 0}, {160, 100, 1}, {260, 100, 2}};
  int n = poll(t, 1000, e, a, 3);
  ok(countOf(e, n, FT_DOWN) == 2, "only two land");
  ok(ftDown(t) == 2, "only two are tracked");
  ok(t.voice == 0, "the voice is the first of them");
}


// ---- adversarial: the panel recycles a contact id ------------------------------------------
// An id is strong evidence, not proof. The FT6336 hands a freed number to the next contact, so a
// number that matches is sometimes a different finger entirely. These are the cases that go wrong
// quietly rather than loudly, which is what makes them worth pinning down

// Two fingers down; one contact arrives carrying the first one's number, between the two of them.
// At this distance the id match is also the nearer finger, so there is nothing to argue about
static void idBetweenTwo() {
  printf("a contact carrying a live id, between two fingers\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[2] = {{100, 150, 0}, {220, 150, 1}};
  poll(t, 1000, e, a, 2);
  FtPoint c[1] = {{150, 150, 0}};  // 50 px from A, 70 px from B
  int n = poll(t, 1010, e, c, 1);
  ok(n == 0, "no landing and no removal");
  ok(t.f[0].x == 150, "A took it: it is both the number and the nearer finger");
  ok(t.f[1].missing == 1, "B is the one that went missing");
}

// The same, far enough over that the other finger is plainly the better fit. Spatial evidence has
// to be able to beat a number, or a renumbering panel would walk a held note onto the wrong voice
static void idLosesToDistance() {
  printf("a contact carrying a live id, but much nearer the other finger\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[2] = {{100, 150, 0}, {220, 150, 1}};
  poll(t, 1000, e, a, 2);
  FtPoint c[1] = {{210, 150, 0}};  // 110 px from A, 10 px from B
  int n = poll(t, 1010, e, c, 1);
  ok(n == 0, "no landing and no removal");
  ok(t.f[1].x == 210 && t.f[1].id == 0, "B took it and answers to the new number");
  ok(t.f[0].missing == 1, "A is the one that went missing");
}

// The one that matters: a finger lifts, its number is handed to a NEW finger that lands while the
// old one is still inside the confirmation window. Nothing may absorb that contact into the
// departed finger's slot -- a new finger must get its own voice and its own note, not inherit one
static void idReuseAfterLift() {
  printf("a freed id handed to a new finger inside the confirmation window\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[2] = {{100, 150, 0}, {220, 150, 1}};
  poll(t, 1000, e, a, 2);
  FtPoint b[1] = {{220, 150, 1}};  // A lifts; B stays
  poll(t, 1010, e, b, 1);
  ok(t.f[0].missing == 1 && t.f[0].used, "A is missing, not yet given up on");
  // a new finger lands 100 px from where A was, carrying A's freed number
  FtPoint c[2] = {{220, 150, 1}, {200, 150, 0}};
  int n = poll(t, 1020, e, c, 2);
  ok(countOf(e, n, FT_UP) == 1, "the departed finger is given up on: its note is released");
  ok(countOf(e, n, FT_DOWN) == 1, "and the new finger lands as a finger of its own");
  // reusing the freed slot number is fine; inheriting the old finger's note and age is not
  int s = slotOf(e, n, FT_DOWN);
  ok(s >= 0 && t.f[s].downAt == 1020, "it is new, with its own age, not the departed one's");
  ok(s >= 0 && t.f[s].x == 200, "and it is where the finger actually is");
}

// But a contact that reappears where the missing finger actually was is that finger, however its
// number came about. Nothing can tell those two apart, and the common case is a dropped poll
static void idReuseAtSamePlace() {
  printf("a missing finger reappearing where it was\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[2] = {{100, 150, 0}, {220, 150, 1}};
  poll(t, 1000, e, a, 2);
  FtPoint b[1] = {{220, 150, 1}};
  poll(t, 1010, e, b, 1);
  FtPoint c[2] = {{220, 150, 1}, {100, 150, 0}};
  int n = poll(t, 1020, e, c, 2);
  ok(n == 0, "it is taken for the same finger: no landing, no removal");
  ok(t.f[0].used && t.f[0].x == 100, "and it keeps its slot and its note");
}

// A finger that is being reported every poll keeps its number at any speed. The plausibility check
// is only on fingers the panel has stopped reporting, which is exactly when numbers get recycled
static void fastMoveKeepsIdentity() {
  printf("a fast finger keeps its identity\n");
  FieldTouch t;
  ftBegin(t, 2);
  FtEvent e[8];
  FtPoint a[1] = {{20, 150, 0}};
  poll(t, 1000, e, a, 1);
  FtPoint b[1] = {{300, 150, 0}};  // 280 px in one poll: a flick across the whole surface
  int n = poll(t, 1010, e, b, 1);
  ok(n == 0, "no landing and no removal");
  ok(t.f[0].x == 300, "it is the same finger, moved");
  ok(ftDown(t) == 1, "and there is still only one");
}

int main() {
  identity();
  singleDropout();
  repeatedDropout();
  confirmedRemoval();
  cleanLift();
  renumbered();
  ambiguity();
  recoverBound();
  promotion();
  promotionOrder();
  promotionNotBySlot();
  dropoutNoPromotion();
  spread();
  spreadOrder();
  speed();
  held();
  beyondSlots();
  idBetweenTwo();
  idLosesToDistance();
  idReuseAfterLift();
  idReuseAtSamePlace();
  fastMoveKeepsIdentity();
  printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
  return fails ? 1 : 0;
}
