// SPDX-License-Identifier: MIT
// Granular Field: which fingers are on the surface, and which of them is the voice.
//
// Plain logic: no LVGL, no MIDI, no hal, no board. It is given the contacts a panel reports and a
// clock, and it answers with what changed -- a finger landed, a finger has really gone, the voice
// has moved to another finger. tools/unit-test.sh drives all of it on the computer, and the same
// engine serves a panel that feels two fingers and one that feels five.
//
// Why it exists at all: the FT6336 drops a contact now and then, especially when two fingers line up
// vertically. The other five modes release a note the first poll a contact goes missing, which for a
// pad is right -- a pad's note is short and a lost poll is a stutter nobody notices. A granular voice
// is held for seconds, and a dropped poll would cut it. So Granular Field, and only Granular Field,
// waits FT_CONFIRM polls before it believes a finger has gone.
//
// What it is for:
//   * identity: a contact keeps its finger across polls, by the panel's id while that holds, and by
//     where it was when the panel renumbers its contacts
//   * three states: here, missing for a poll or two, really gone
//   * the voice: the first finger down owns the parameters, and when it goes the next oldest takes
//     over with no gap and no second note
//   * the geometry the parameters are made of -- how far apart, how fast, how long held -- so none of
//     it is worked out twice in the page
//
// What it is NOT for: notes, CCs, bend, pictures. It says what the hand did; the page decides what
// that is worth.
#pragma once

#include <stdint.h>

#define FT_MAX 5      // fingers this can hold at once. The 2.8" panel reports two; the engine is ready for five
#define FT_CONFIRM 3  // polls a finger may go missing before it is believed gone (POLL_MS 10: about 30 ms)

// How far a contact may be from where a missing finger was and still be taken for that finger. A
// hand moving quickly covers maybe 30 px in the confirmation window, so this is generous without
// being a free pass across the surface
#define FT_RECOVER_PX 48

// Two missing fingers this close to the same contact are not told apart. Rather than pick one and
// risk putting a held note on the wrong voice, the contact is taken for a new finger
#define FT_AMBIG_PX 8

enum { FT_DOWN,     // a finger landed. slot: which
       FT_UP,       // a finger is confirmed gone (FT_CONFIRM polls missing, or lifted cleanly)
       FT_PROMOTE,  // the voice has moved to another finger. slot: the new one
       FT_KINDS };

struct FtEvent {
  uint8_t kind;
  uint8_t slot;
};

// one contact, as the panel reports it
struct FtPoint {
  int16_t x, y;
  uint8_t id;
};

struct FtFinger {
  bool used;         // this slot is in play (here, or missing and not yet given up on)
  bool present;      // the panel reported it this poll
  uint8_t id;        // the panel's id for it, as last seen
  int16_t x, y;      // where it is, or where it was when it went missing
  int16_t px, py;    // where it was the poll before that (speed)
  uint8_t missing;   // polls in a row not reported
  uint32_t downAt;   // when it landed (how long held)
  uint32_t at;       // when x,y were last set (speed)
  uint32_t pat;      // when px,py were set, so speed is per millisecond and not per poll
};

struct FieldTouch {
  FtFinger f[FT_MAX];
  uint8_t slots;    // how many the panel can really feel. Beyond this nothing is tracked
  uint8_t voice;    // the slot that owns the parameters, FT_MAX none
  int16_t spread;   // how far apart the two widest fingers were, last time there were two, 0 .. 127
  bool hasSpread;   // whether that has ever been worth anything (before it has, it is not sent)
};

static inline void ftBegin(FieldTouch& t, int slots) {
  t = FieldTouch();
  for (int i = 0; i < FT_MAX; i++) {
    t.f[i] = FtFinger();
  }
  t.slots = (uint8_t)(slots < 1 ? 1 : slots > FT_MAX ? FT_MAX : slots);
  t.voice = FT_MAX;
  t.spread = 0;
  t.hasSpread = false;
}

static inline int ftPush(FtEvent* out, int max, int n, int kind, int slot) {
  if (out && n < max) {
    out[n].kind = (uint8_t)kind;
    out[n].slot = (uint8_t)slot;
  }
  return n + 1;
}

static inline long ftDist2(int ax, int ay, int bx, int by) {
  long dx = ax - bx, dy = ay - by;
  return dx * dx + dy * dy;
}

// The oldest finger in play, which is the one the voice belongs to. Ties go to the lower slot, so
// two fingers landing in the same poll settle it the same way every time
static inline int ftOldest(const FieldTouch& t, int except) {
  int best = FT_MAX;
  for (int i = 0; i < FT_MAX; i++) {
    if (!t.f[i].used || i == except) {
      continue;
    }
    if (best == FT_MAX || (int32_t)(t.f[i].downAt - t.f[best].downAt) < 0) {
      best = i;
    }
  }
  return best;
}

// What it costs to call this contact that finger. Distance squared, with an id match counting for
// a great deal but not everything: a panel that renumbers its contacts hands the old number to a
// different finger, and then the number is a lie while the position is still the truth
#define FT_ID_WEIGHT 4  // an id match is worth this much of the distance

// How fast a finger could possibly be travelling, px per millisecond, for the purpose of doubting a
// number. A flick across the 2.8" surface is around 3 px/ms, so this is roughly double the fastest
// a hand goes, and it is used only to reject the absurd
#define FT_MAX_PXMS 4
static inline long ftCost(const FtFinger& g, const FtPoint& p, bool& byId) {
  long d = ftDist2(p.x, p.y, g.x, g.y);
  byId = g.id == p.id;
  return byId ? d / FT_ID_WEIGHT : d;
}

// Which finger this contact belongs to, or FT_MAX for none of them.
//   * an id match is allowed however far it has travelled -- a fast hand keeps its voice
//   * without an id match it must be within FT_RECOVER_PX, so a new touch is not mistaken for one
//     that went missing across the surface
//   * and if two fingers with no id to decide between them are about as close, nothing is claimed:
//     a held note on the wrong voice is worse than one more voice
// Could this finger have got here? Only asked of a finger the panel has STOPPED reporting, because
// that is exactly when a freed number gets handed to a different finger. A finger that is still
// being reported every poll keeps its number at any speed -- the normal path is untouched, and a
// flick across the whole surface stays one finger
static inline bool ftReachable(const FtFinger& g, const FtPoint& p, uint32_t now) {
  uint32_t dt = now - g.at;
  if (!dt) {
    dt = 1;
  }
  long reach = (long)FT_MAX_PXMS * (long)dt;
  if (reach < FT_RECOVER_PX) {
    reach = FT_RECOVER_PX;  // never tighter than the ordinary recovery radius
  }
  return ftDist2(p.x, p.y, g.x, g.y) <= reach * reach;
}

static inline int ftClaim(const FieldTouch& t, const FtPoint& p, const bool* taken, uint32_t now) {
  long bound = (long)FT_RECOVER_PX * FT_RECOVER_PX;
  int best = FT_MAX;
  long bd = 0, sd = 0;
  bool bestById = false, second = false;
  for (int i = 0; i < FT_MAX; i++) {
    if (!t.f[i].used || taken[i]) {
      continue;
    }
    bool byId = false;
    long c = ftCost(t.f[i], p, byId);
    if (!byId && c > bound) {
      continue;
    }
    if (byId && t.f[i].missing && !ftReachable(t.f[i], p, now)) {
      continue;  // its number, but it cannot have gone that far: the panel has reused the number
    }
    if (best == FT_MAX || c < bd) {
      sd = bd, second = best != FT_MAX;
      best = i, bd = c, bestById = byId;
    } else if (!second || c < sd) {
      sd = c, second = true;
    }
  }
  if (best == FT_MAX || bestById) {
    return best;  // its own number, and nothing nearer: that is the finger
  }
  if (second && sd - bd < (long)FT_AMBIG_PX * FT_AMBIG_PX) {
    return FT_MAX;  // two of them, equally likely, and no number to tell them apart
  }
  return best;
}

// How far apart the two fingers furthest from each other are, 0 .. 127 across the diagonal of the
// surface. Fewer than two fingers: what it was last time there were two, which is why it never
// snaps to nothing when one finger lifts. Order does not come into it -- it is a distance
static inline void ftSpread(FieldTouch& t, int w, int h) {
  int a = FT_MAX, b = FT_MAX;
  long far = -1;
  for (int i = 0; i < FT_MAX; i++) {
    if (!t.f[i].used) {
      continue;
    }
    for (int j = i + 1; j < FT_MAX; j++) {
      if (!t.f[j].used) {
        continue;
      }
      long d = ftDist2(t.f[i].x, t.f[i].y, t.f[j].x, t.f[j].y);
      if (d > far) {
        far = d, a = i, b = j;
      }
    }
  }
  if (a == FT_MAX || b == FT_MAX) {
    return;  // one finger or none: the last real spread stands
  }
  long ww = w > 1 ? w - 1 : 1, hh = h > 1 ? h - 1 : 1;
  int64_t diag = (int64_t)ww * ww + (int64_t)hh * hh;  // the furthest two fingers can really be
  if (diag <= 0) {
    return;
  }
  // 127 * sqrt(far / diag), by integer square root of the ratio scaled up. 64-bit on the way
  // through: 127 * 127 * a 480-wide diagonal does not fit in a 32-bit long on the board
  int64_t q = (int64_t)far * 16129 / diag;  // 127 * 127
  long r = 0;
  while ((int64_t)(r + 1) * (r + 1) <= q) {
    r++;
  }
  t.spread = (int16_t)(r > 127 ? 127 : r);
  t.hasSpread = true;
}

// A poll. pts/n are what the panel reported; now is the clock. Events come back in the order they
// happened: landings, then confirmed removals, then a promotion if one followed
static inline int ftPoll(FieldTouch& t, const FtPoint* pts, int n, uint32_t now, FtEvent* out, int max) {
  int ev = 0;
  for (int i = 0; i < FT_MAX; i++) {
    t.f[i].present = false;
  }
  bool got[FT_MAX] = {false, false, false, false, false};   // contacts already given a finger
  bool mine[FT_MAX] = {false, false, false, false, false};  // fingers already given a contact
  int lim = n > FT_MAX ? FT_MAX : n;

  // Nearest first, so the clearest pairing is settled before the doubtful ones. With at most five
  // of each this is cheap, and it never leaves a contact on a finger that another contact fits better
  for (int round = 0; round < lim; round++) {
    int bk = FT_MAX, bi = FT_MAX;
    long bc = 0;
    for (int k = 0; k < lim; k++) {
      if (got[k]) {
        continue;
      }
      int i = ftClaim(t, pts[k], mine, now);
      if (i == FT_MAX) {
        continue;
      }
      bool byId = false;
      long c = ftCost(t.f[i], pts[k], byId);
      if (bk == FT_MAX || c < bc) {
        bk = k, bi = i, bc = c;
      }
    }
    if (bk == FT_MAX) {
      break;
    }
    FtFinger& g = t.f[bi];
    got[bk] = true, mine[bi] = true;
    g.present = true;
    g.id = pts[bk].id;  // it answers to whatever number the panel gives it now
    g.px = g.x, g.py = g.y, g.pat = g.at;
    g.x = pts[bk].x, g.y = pts[bk].y;
    g.missing = 0, g.at = now;
  }

  int gone = FT_MAX;  // the voice's own removal, dealt with after the rest so promotion comes last
  for (int i = 0; i < FT_MAX; i++) {
    if (!t.f[i].used || t.f[i].present) {
      continue;
    }
    if (t.f[i].missing < 255) {
      t.f[i].missing++;
    }
    // The panel is reporting as many contacts as it can feel, and this finger is not one of them.
    // It cannot be hiding behind the panel's limit, so it has not been dropped -- it has lifted,
    // and waiting the rest of the window would only hold its note on and keep its slot from the
    // finger that is really there
    if (lim >= t.slots && t.f[i].missing < FT_CONFIRM) {
      t.f[i].missing = FT_CONFIRM;
    }
    if (t.f[i].missing < FT_CONFIRM) {
      continue;  // missing, not gone: it keeps its slot, its note, its place and its age
    }
    t.f[i] = FtFinger();
    ev = ftPush(out, max, ev, FT_UP, i);
    if (t.voice == i) {
      gone = i;
    }
  }
  for (int k = 0; k < lim; k++) {  // whatever is left is new
    if (got[k]) {
      continue;
    }
    int free = FT_MAX;
    for (int i = 0; i < t.slots; i++) {
      if (!t.f[i].used) {
        free = i;
        break;
      }
    }
    if (free == FT_MAX) {
      continue;  // the panel feels more than it can hold: the extra is ignored, not swapped in
    }
    FtFinger& g = t.f[free];
    g = FtFinger();
    g.used = true, g.present = true, g.id = pts[k].id;
    g.x = g.px = pts[k].x, g.y = g.py = pts[k].y;
    g.downAt = now, g.at = now, g.pat = now;
    if (t.voice == FT_MAX) {
      t.voice = (uint8_t)free;
    }
    ev = ftPush(out, max, ev, FT_DOWN, free);
  }

  if (gone != FT_MAX) {
    int next = ftOldest(t, gone);
    t.voice = (uint8_t)next;
    if (next != FT_MAX) {
      ev = ftPush(out, max, ev, FT_PROMOTE, next);
    }
  }
  return ev;
}

// A finger has lifted cleanly, as far as the caller knows -- the page has no such news from this
// panel, but a simulator or a future panel might. Same outcome as a confirmed absence, at once
static inline int ftLift(FieldTouch& t, int slot, FtEvent* out, int max) {
  int ev = 0;
  if (slot < 0 || slot >= FT_MAX || !t.f[slot].used) {
    return 0;
  }
  t.f[slot] = FtFinger();
  ev = ftPush(out, max, ev, FT_UP, slot);
  if (t.voice == slot) {
    int next = ftOldest(t, slot);
    t.voice = (uint8_t)next;
    if (next != FT_MAX) {
      ev = ftPush(out, max, ev, FT_PROMOTE, next);
    }
  }
  return ev;
}

// Everything off: the page is leaving, or the mode is
static inline void ftAllUp(FieldTouch& t) {
  for (int i = 0; i < FT_MAX; i++) {
    t.f[i] = FtFinger();
  }
  t.voice = FT_MAX;  // the spread stands: what went out last is what the synth still has
}

static inline int ftDown(const FieldTouch& t) {
  int n = 0;
  for (int i = 0; i < FT_MAX; i++) {
    n += t.f[i].used;
  }
  return n;
}

// Is this finger worth listening to: here, or missing and not yet given up on. The picture and the
// parameters both use it, so a dropped poll moves nothing
static inline bool ftLive(const FieldTouch& t, int slot) {
  return slot >= 0 && slot < FT_MAX && t.f[slot].used;
}

// How fast it is moving, 0 .. 127, from the distance covered between the last two polls over the
// time they were apart -- normalised by time, so a slow poll does not read as a fast hand.
// FT_FAST_PXMS is the speed that counts as all the way
#define FT_FAST_PXMS 2  // px per ms: 2 is a brisk swipe across the 2.8" surface
static inline int ftSpeed(const FieldTouch& t, int slot) {
  if (!ftLive(t, slot)) {
    return 0;
  }
  const FtFinger& g = t.f[slot];
  uint32_t dt = g.at - g.pat;
  if (!dt) {
    return 0;  // nothing has moved yet, or two reads landed in the same millisecond
  }
  long dx = g.x - g.px, dy = g.y - g.py;
  long d2 = dx * dx + dy * dy;
  long r = 0;
  while ((r + 1) * (r + 1) <= d2) {
    r++;  // px covered, by integer square root
  }
  long v = r * 127 / ((long)FT_FAST_PXMS * (long)dt);
  return (int)(v > 127 ? 127 : v);
}

// How long it has been held, 0 .. 127 over FT_WET_MS
#define FT_WET_MS 4000u
static inline int ftHeld(const FieldTouch& t, int slot, uint32_t now) {
  if (!ftLive(t, slot)) {
    return 0;
  }
  uint32_t ms = now - t.f[slot].downAt;
  return (int)(ms >= FT_WET_MS ? 127 : ms * 127 / FT_WET_MS);
}
