// The step sequencer's engine (seq.h).
//
// How it keeps time. One number is the truth: pos, the sub-ticks since play() (SEQ_PPQ a quarter note, SEQ_STEP a
// step). Which step the playhead is on, and where it is drawn, both come from that; nothing the screen does can
// move it. Every time is absolute and counted from the start, so a pattern going round again needs nothing undone.
//
// A step's notes are worked out one step BEFORE they are due and put in the pending list, each with the sub-tick it
// is due at, so a note can be early (a negative offset) as well as late. advance() walks the sub-ticks and plays
// what falls due, so a ratchet, a micro-timing offset and a humanized stray each land where they should instead of
// being rounded to the step.
//
// Swing pushes the off-beat steps back: step k is due at k * SEQ_STEP, and a step with an odd number in the
// pattern that much later again (swing % of half a step). The down-beats are not touched.
#include "seq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SEQ_NO_MIDISYNC
namespace midisync {
inline bool seqInternalDrive() { return true; }
inline void requestStart() {}
inline void requestStop() {}
inline void requestContinue() {}
}  // namespace midisync
#else
#include "midisync.h"
#endif

namespace {

// The pattern is about 68 KB, so it is never a static: the board hands the engine a block of PSRAM
// (seq::usePattern, from seqdrive.cpp) and the host's test takes one from the ordinary heap the first time it is
// needed. ready() is false only if there was no room at all, and then every call below does nothing: no engine,
// but nothing crashes either
seq::Pattern* patp;
seq::Out out;
uint32_t drops, peak;

bool ready() {
  if (!patp) {
    patp = (seq::Pattern*)calloc(1, sizeof(seq::Pattern));
  }
  return patp != 0;
}
#define pat (*patp)

// What is still to be played, each at its own sub-tick (a step's notes land here a step ahead). A ratchet keeps one
// slot and arms itself again after each hit, instead of filling the list with every hit at once: with that, the
// most the list ever holds is one waiting note plus one sounding note for each note of each track, whatever the
// ratchets do (SEQ_TRACKS x SEQ_NOTES x 2 = 128), and a gate longer than a step cannot push it past PENDING
struct Pending {
  uint32_t at;    // the sub-tick it is due
  uint8_t kind;   // 0 free, 1 note on, 2 note off
  uint8_t ch;     // the channel it began on: its ending uses the same one
  uint8_t pitch;
  uint8_t vel;
  uint8_t left;   // a ratchet: hits still to come after this one
  uint16_t every; // and how far apart they are
  uint16_t hold;  // how long each hit sounds
};
// bounded: enough for every track to hold a chord with a few ratchets at once. Nothing is allocated while it plays,
// so a run that would need more drops notes and says so (seq::dropped)
#define PENDING 256
Pending pend[PENDING];

bool running;
uint32_t pos;    // sub-ticks since play() (never wraps while a pattern plays)
uint32_t nextPlan;  // the step, counted from the start, whose notes have not been worked out yet
int curStep;     // the step it is on, counted from the start (-1: stopped)
uint32_t origin; // the step, counted from the start, at which the pattern now playing began (song mode swaps
                 // patterns of different lengths in, and the new one starts at its step 1 from there)
seq::Cycle cycleFn;  // told when the pattern is about to go round again
uint32_t rnd = 0x2545F491;

// ------------------------------------------------------------------------------------------ recording ---
bool recArmed, recOver;
int recTrack;
uint32_t recCount;
uint32_t recPass;                 // goes up on every arm(): a step cleared in an earlier pass is cleared again
struct Held {                     // a note still down: where it was written, so its note-off can set the gate
  uint8_t pitch, used;
  uint8_t step, slot;
  uint32_t at;                    // the sub-tick it started
};
#define HELD 16
Held held[HELD];
uint32_t clearedPass[SEQ_STEPS];  // the pass in which each step of the armed track was last cleared

uint32_t roll() {
  rnd ^= rnd << 13, rnd ^= rnd >> 17, rnd ^= rnd << 5;
  return rnd;
}

int clampi(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

int patSteps() {
  return ready() ? clampi(pat.steps, 1, SEQ_STEPS) : 1;
}

// which step of the pattern the step k (counted from the start) is
int patStepOf(uint32_t k) {
  return (int)((k - origin) % (uint32_t)patSteps());
}

void send(uint8_t a, uint8_t b, uint8_t c) {
  if (out) {
    uint8_t m[3] = {a, b, c};
    out(m, 3);
  }
}

// when step k (counted from the start) is due, in sub-ticks: its place in the bar, and swing for an off-beat one
uint32_t stepAt(uint32_t k) {
  uint32_t t = k * SEQ_STEP;
  if (patStepOf(k) & 1) {
    t += (uint32_t)(pat.swing * (SEQ_STEP / 2) / 100);
  }
  return t;
}

// an event waits its turn. The channel is kept with it, so a note ends on the channel it started on even if the
// track is moved to another one meanwhile
void put(uint32_t at, int kind, int ch, int pitch, int vel, int left = 0, int every = 0, int hold = 1) {
  for (auto& p : pend) {
    if (!p.kind) {
      p = {at,
           (uint8_t)kind,
           (uint8_t)(ch & 15),
           (uint8_t)clampi(pitch, 0, 127),
           (uint8_t)clampi(vel, 0, 127),
           (uint8_t)clampi(left, 0, 255),
           (uint16_t)clampi(every, 0, 65535),
           (uint16_t)clampi(hold, 1, 65535)};
      return;
    }
  }
  drops++;  // (no room: this one is not played. A healthy run never gets here)
}

// how many are waiting now, remembered if it is the most so far
void mark() {
  uint32_t n = 0;
  for (const auto& p : pend) {
    n += p.kind != 0;
  }
  peak = n > peak ? n : peak;
}

// every note track t holds at this step goes into the pending list, around `base`
void planTrack(int t, uint32_t globalStep, uint32_t base) {
  const seq::Track& tr = pat.track[t];
  if (!seq::trackPlays(t)) {
    return;
  }
  int len = clampi(tr.len ? tr.len : patSteps(), 1, SEQ_STEPS);
  const seq::Step& s = tr.step[globalStep % (uint32_t)len];  // (its own length: it goes round inside the pattern)
  for (int i = 0; i < s.n && i < SEQ_NOTES; i++) {
    const seq::Note& n = s.note[i];
    if (n.prob && n.prob < 100 && (int)(roll() % 100) >= n.prob) {
      continue;  // (it did not come up this time)
    }
    int vel = n.vel ? n.vel : 100;
    if (n.accent) {
      vel += (127 - vel) * n.accent / 100;
    }
    int jitter = 0;
    if (n.human) {
      int most = n.human * SEQ_STEP / 200;  // (at 100: half a step either way)
      jitter = most ? (int)(roll() % (uint32_t)(2 * most + 1)) - most : 0;
      vel += (int)(roll() % 25) - 12;
    }
    vel = clampi(vel, 1, 127);
    int gate = n.gate ? n.gate : (pat.gate ? pat.gate : 80);
    int reps = clampi(n.repeat ? n.repeat : 1, 1, 8);
    uint32_t hold = (uint32_t)clampi(SEQ_STEP * gate / 100, 1, SEQ_STEP * 2);
    if (reps > 1) {
      hold = (uint32_t)clampi(SEQ_STEP / reps * 2 / 5, 1, SEQ_STEP);  // (a ratchet: each hit short, as SMB's)
    }
    int32_t on = (int32_t)base + n.offset + jitter;
    on = on < 0 ? 0 : on;
    put((uint32_t)on, 1, tr.ch, n.pitch, vel, reps - 1, SEQ_STEP / reps, (int)hold);
  }
}

// the notes of every step due within the next step's worth of time are worked out, once each
void plan() {
  while (stepAt(nextPlan) <= pos + SEQ_STEP) {
    if (cycleFn && nextPlan > origin && patStepOf(nextPlan) == 0) {
      cycleFn();  // the pattern has played its last step: song mode may put another one in (and rebase)
    }
    uint32_t at = stepAt(nextPlan), k = (uint32_t)patStepOf(nextPlan);
    nextPlan++;
    for (int t = 0; t < pat.tracks && t < SEQ_TRACKS; t++) {
      planTrack(t, k, at);
    }
  }
}

// everything still sounding ends now, and nothing is left waiting
void allOff() {
  for (auto& p : pend) {
    if (p.kind == 2) {
      send((uint8_t)(0x80 | p.ch), p.pitch, 0);
    }
    p.kind = 0;
  }
}

}  // namespace

void seq::setOut(Out fn) {
  out = fn;
}

// only to be used once begin() or clear() has found room (seq::ready)
seq::Pattern& seq::pattern() {
  ready();
  return pat;
}

bool seq::ready() {
  return ::ready();
}

unsigned long seq::patternBytes() {
  return (unsigned long)sizeof(seq::Pattern);
}

void seq::usePattern(void* mem) {
  if (mem) {
    patp = (seq::Pattern*)mem;
  }
}

uint32_t seq::dropped() {
  return drops;
}

uint32_t seq::peakPending() {
  return peak;
}

int seq::pendingRoom() {
  return PENDING;
}

void seq::clear() {
  if (!ready()) {
    return;
  }
  memset(&pat, 0, sizeof pat);
  strncpy(pat.name, "Pattern", SEQ_NAME - 1);
  pat.bpm = 120;
  pat.steps = 32;  // (FM Buddy 2.0: a page of the editor)
  pat.gate = 80;
  pat.tracks = SEQ_TRACKS;
  for (int t = 0; t < SEQ_TRACKS; t++) {
    pat.track[t].ch = (uint8_t)t;  // (track 1 on channel 1, and so on, as the defaults)
    snprintf(pat.track[t].name, SEQ_TNAME, "T%d", t + 1);
    pat.track[t].base = 60;  // (C4 until it is changed)
  }
  memset(pend, 0, sizeof pend);
  running = false, pos = 0, nextPlan = 0, curStep = -1, drops = 0, peak = 0, origin = 0;
  recArmed = false, recOver = false, recTrack = 0, recCount = 0, recPass = 1;
  for (auto& h : held) {
    h.used = 0;
  }
  for (auto& c : clearedPass) {
    c = 0;
  }
}

void seq::begin() {
  clear();
}

// ---------------------------------------------------------------------------------------------- transport ---

void seq::transportPlay() {
  if (!ready()) {
    return;
  }
  allOff();
  pos = 0, nextPlan = 0, curStep = -1, origin = 0;
  running = true;
  plan();  // (the first steps' notes, before their time comes)
}

void seq::transportStop() {
  running = false;
  allOff();
  curStep = -1;
}

void seq::transportCont() {
  if (!ready() || running) {
    return;
  }
  running = true;
  while (nextPlan && stepAt(nextPlan - 1) >= pos) {
    nextPlan--;
  }
  plan();
}

void seq::releaseSounding() {
  if (!ready()) {
    return;
  }
  allOff();  // (every note still sounding ends on the channel it began on; the pending list is emptied)
}

bool seq::seekToPatternStep(int stepInPattern) {
  if (!ready()) {
    return false;
  }
  int steps = patSteps();
  if (stepInPattern < 0) {
    stepInPattern = 0;
  }
  if (stepInPattern >= steps) {
    stepInPattern = steps - 1;
  }
  // Safe seek: release sounding notes and clear pending events without altering pattern data.
  allOff();
  uint32_t k = origin + (uint32_t)stepInPattern;
  pos = stepAt(k);
  curStep = (int)k - 1;
  nextPlan = k;
  // Re-plan upcoming notes from the new position so the step at pos is ready when advanced.
  plan();
  return true;
}

void seq::play() {
  if (!ready()) {
    return;
  }
  // Following an external clock, the transport still starts here. What changes is only who moves it on: the
  // engine is armed and shows itself as playing, and then waits for that clock's pulses to advance it. It used to
  // return without starting and wait for a Start message, which left the button doing nothing at all with the
  // many masters that send clock and never send one. The clock out there stays the authority: nothing is advanced
  // from our own clock, and no Start of ours goes back to it (midisync::requestStart).
  midisync::requestStart();
  transportPlay();
}

void seq::stop() {
  midisync::requestStop();
  transportStop();
}

void seq::cont() {
  midisync::requestContinue();
  transportCont();  // (as play(): armed here, moved on by whichever clock is the authority)
}

void seq::toggle() {
  running ? stop() : (pos == 0 ? play() : cont());
}

bool seq::playing() {
  return running;
}

int seq::step() {
  return running && curStep >= 0 ? patStepOf((uint32_t)curStep) : -1;
}

void seq::setCycle(Cycle fn) {
  cycleFn = fn;
}

void seq::rebase() {
  origin = nextPlan;  // the pattern just put in starts at its first step, here
}

uint32_t seq::ticks() {
  return pos;
}

void seq::setBpm(int b) {
  if (!ready()) {
    return;
  }
  pat.bpm = (uint16_t)clampi(b, 30, 300);
}

int seq::bpm() {
  return ready() && pat.bpm ? pat.bpm : 120;
}

// ------------------------------------------------------------------------------------------------ editing ---

seq::Step& seq::at(int t, int s) {
  static Step spare;
  if (!ready() || t < 0 || t >= SEQ_TRACKS || s < 0 || s >= SEQ_STEPS) {
    spare = {};
    return spare;
  }
  return pat.track[t].step[s];
}

bool seq::on(int t, int s) {
  return at(t, s).n > 0;
}

void seq::setNote(int t, int s, int pitch, int vel) {
  Step& st = at(t, s);
  st.n = 1;
  st.note[0] = {(uint8_t)clampi(pitch, 0, 127), (uint8_t)clampi(vel, 1, 127), 0, 100, 1, 0, 0, 0};
}

void seq::addNote(int t, int s, int pitch, int vel) {
  Step& st = at(t, s);
  if (st.n < SEQ_NOTES) {
    st.note[st.n++] = {(uint8_t)clampi(pitch, 0, 127), (uint8_t)clampi(vel, 1, 127), 0, 100, 1, 0, 0, 0};
  }
}

bool seq::toggleStep(int t, int s, int pitch, int vel) {
  if (on(t, s)) {
    clearStep(t, s);
    return false;
  }
  setNote(t, s, pitch, vel);
  return true;
}

void seq::clearStep(int t, int s) {
  at(t, s) = {};
}

void seq::setLength(int steps) {
  if (!ready()) {
    return;
  }
  pat.steps = (uint8_t)clampi(steps, 1, SEQ_STEPS);
}

void seq::setTrackLength(int t, int len) {
  if (!ready()) {
    return;
  }
  if (t >= 0 && t < SEQ_TRACKS) {
    pat.track[t].len = (uint8_t)clampi(len, 0, SEQ_STEPS);
  }
}

void seq::setSwing(int p) {
  if (!ready()) {
    return;
  }
  pat.swing = (uint8_t)clampi(p, 0, 100);
}

void seq::setChannel(int t, int ch) {
  if (!ready()) {
    return;
  }
  if (t >= 0 && t < SEQ_TRACKS) {
    pat.track[t].ch = (uint8_t)(ch & 15);  // (only this track: notes already waiting keep the channel they began on)
  }
}

void seq::setBase(int t, int pitch) {
  if (ready() && t >= 0 && t < SEQ_TRACKS) {
    pat.track[t].base = (uint8_t)clampi(pitch, 0, 127);
  }
}

int seq::base(int t) {
  int b = ready() && t >= 0 && t < SEQ_TRACKS ? pat.track[t].base : 60;
  return b ? b : 60;
}

void seq::setGate(int percent) {
  if (ready()) {
    pat.gate = (uint8_t)clampi(percent, 1, 200);
  }
}

void seq::setTracks(int n) {
  if (ready()) {
    pat.tracks = (uint8_t)clampi(n, 1, SEQ_TRACKS);
  }
}

int seq::channel(int t) {
  return ready() && t >= 0 && t < SEQ_TRACKS ? pat.track[t].ch : 0;
}

void seq::setTrackName(int t, const char* name) {
  if (!ready()) {
    return;
  }
  if (t >= 0 && t < SEQ_TRACKS && name) {
    snprintf(pat.track[t].name, SEQ_TNAME, "%s", name);
  }
}

const char* seq::trackName(int t) {
  return ready() && t >= 0 && t < SEQ_TRACKS ? pat.track[t].name : "";
}

void seq::setTrackOn(int t, bool on) {
  if (!ready()) {
    return;
  }
  if (t >= 0 && t < SEQ_TRACKS) {
    pat.track[t].off = (uint8_t)!on;
  }
}

bool seq::trackOn(int t) {
  return ready() && t >= 0 && t < SEQ_TRACKS && !pat.track[t].off;
}

void seq::setMute(int t, bool v) {
  if (!ready()) {
    return;
  }
  if (t >= 0 && t < SEQ_TRACKS) {
    pat.track[t].mute = (uint8_t)v;
  }
}

void seq::setSolo(int t, bool v) {
  if (!ready()) {
    return;
  }
  if (t >= 0 && t < SEQ_TRACKS) {
    pat.track[t].solo = (uint8_t)v;
  }
}

bool seq::trackPlays(int t) {
  if (!ready() || t < 0 || t >= SEQ_TRACKS || pat.track[t].mute || pat.track[t].off) {
    return false;
  }
  for (int i = 0; i < SEQ_TRACKS; i++) {
    if (pat.track[i].solo) {
      return pat.track[t].solo != 0;
    }
  }
  return true;
}

// --------------------------------------------------------------------- the editor's window on the pattern ---

int seq::viewPages() {
  return (patSteps() + SEQ_VIEW - 1) / SEQ_VIEW;
}

int seq::viewStep(int page, int cell) {
  if (page < 0 || cell < 0 || cell >= SEQ_VIEW) {
    return -1;
  }
  int s = page * SEQ_VIEW + cell;
  return s < patSteps() ? s : -1;  // (a short pattern leaves the rest of the page empty)
}

int seq::viewCell(int page, int step) {
  if (page < 0 || step < 0 || step >= patSteps()) {
    return -1;
  }
  int cell = step - page * SEQ_VIEW;
  return cell >= 0 && cell < SEQ_VIEW ? cell : -1;
}

int seq::viewPageOf(int step) {
  return step < 0 ? 0 : step / SEQ_VIEW;
}

int seq::viewRow(int cell) {
  return (cell / SEQ_COLS) & 1;
}

int seq::viewCol(int cell) {
  return cell % SEQ_COLS;
}

// --------------------------------------------------------------------------------------------- recording ---

namespace {

// the step nearest sub-tick t, and how far off it was (+ late, - early). k is counted from the start, not the
// pattern. Swing moves a step's time, so the one before and the one after are both looked at
void nearestStep(uint32_t t, uint32_t& k, int& offset) {
  uint32_t guess = t / SEQ_STEP;
  uint32_t first = guess ? guess - 1 : 0;
  uint32_t best = first;
  long bestOff = (long)t - (long)stepAt(first);
  for (uint32_t c = first + 1; c <= guess + 1; c++) {
    long off = (long)t - (long)stepAt(c);
    long a = off < 0 ? -off : off, b = bestOff < 0 ? -bestOff : bestOff;
    if (a < b) {
      best = c, bestOff = off;
    }
  }
  k = best;
  offset = clampi((int)bestOff, -127, 127);
}

}  // namespace

void seq::arm(bool on) {
  recArmed = on;
  if (on) {
    recCount = 0;
    recPass++;
    for (auto& h : held) {
      h.used = 0;
    }
  }
}

bool seq::armed() {
  return recArmed;
}

void seq::setRecordTrack(int t) {
  recTrack = clampi(t, 0, SEQ_TRACKS - 1);
}

int seq::recordTrack() {
  return recTrack;
}

void seq::setOverdub(bool on) {
  recOver = on;
}

bool seq::overdub() {
  return recOver;
}

uint32_t seq::recorded() {
  return recCount;
}

void seq::recordOn(int pitch, int vel) {
  if (!ready() || !recArmed || !running || pitch < 0 || pitch > 127) {
    return;
  }
  if (vel <= 0) {
    recordOff(pitch);  // (a note-on with no velocity is a note-off)
    return;
  }
  seq::Track& tr = pat.track[recTrack];
  int len = clampi(tr.len ? tr.len : patSteps(), 1, SEQ_STEPS);
  uint32_t k;
  int offset;
  nearestStep(pos, k, offset);
  int step = (int)((k % (uint32_t)patSteps()) % (uint32_t)len);
  seq::Step& st = tr.step[step];
  if (!recOver && clearedPass[step] != recPass) {  // replace: the first note in this pass empties the step
    st.n = 0;
  }
  clearedPass[step] = recPass;
  if (st.n >= SEQ_NOTES) {
    return;  // (the step is full: the note is heard but not written)
  }
  int slot = st.n++;
  seq::Note& n = st.note[slot];
  n.pitch = (uint8_t)pitch;
  n.vel = (uint8_t)clampi(vel, 1, 127);
  n.gate = 0;  // (until the note-off; 0 means the pattern's)
  n.prob = 100;
  n.repeat = 1;
  n.human = 0;
  n.accent = 0;
  n.offset = (int8_t)offset;
  recCount++;
  for (auto& h : held) {  // remember where it went, so its note-off can set the gate
    if (!h.used) {
      h = {(uint8_t)pitch, 1, (uint8_t)step, (uint8_t)slot, pos};
      break;
    }
  }
}

void seq::recordOff(int pitch) {
  if (!ready() || pitch < 0 || pitch > 127) {
    return;
  }
  for (auto& h : held) {
    if (!h.used || h.pitch != (uint8_t)pitch) {
      continue;
    }
    h.used = 0;
    seq::Track& tr = pat.track[recTrack];
    if (h.step >= SEQ_STEPS || h.slot >= SEQ_NOTES || tr.step[h.step].n <= h.slot) {
      return;  // (the step was edited while the key was down)
    }
    seq::Note& n = tr.step[h.step].note[h.slot];
    if (n.pitch != (uint8_t)pitch) {
      return;
    }
    uint32_t sounded = pos > h.at ? pos - h.at : 1;  // musical time, not the clock on the wall
    n.gate = (uint8_t)clampi((int)(sounded * 100 / SEQ_STEP), 1, 200);
    return;
  }
}

// --------------------------------------------------------------------------------------------- quantize ---

namespace {
// a note lifted out of its step while a track is quantized
struct Taken {
  uint32_t at;
  uint8_t from;  // the step it was in
  seq::Note n;
};
Taken* scratch;
}  // namespace

unsigned long seq::scratchBytes() {
  return (unsigned long)sizeof(Taken) * SEQ_STEPS * SEQ_NOTES;
}

void seq::useScratch(void* mem) {
  scratch = (Taken*)mem;
}

int seq::quantize(int track, int grid, int strength, bool lengths, int* dropped) {
  int away = 0;
  if (dropped) {
    *dropped = 0;
  }
  if (!ready() || track < 0 || track >= SEQ_TRACKS || grid < 1) {
    return 0;
  }
  strength = clampi(strength, 0, 100);
  seq::Track& tr = pat.track[track];
  int len = clampi(tr.len ? tr.len : patSteps(), 1, SEQ_STEPS);
  uint32_t round = (uint32_t)len * SEQ_STEP;  // the track goes round in this many sub-ticks

  // every note comes out first, so a note moved into a step that has not been read yet is not moved twice
  Taken* takenRoom = scratch;
  if (!takenRoom) {
    return 0;  // (no room was given: nothing is touched)
  }
  int taken = 0;
  for (int s = 0; s < len; s++) {
    seq::Step& st = tr.step[s];
    for (int i = 0; i < st.n && i < SEQ_NOTES; i++) {
      long at = (long)s * SEQ_STEP + st.note[i].offset;
      at = at < 0 ? at + (long)round : at;  // (a note played early at the very start belongs to the end)
      takenRoom[taken].at = (uint32_t)(at % (long)round);
      takenRoom[taken].from = (uint8_t)s;
      takenRoom[taken].n = st.note[i];
      taken++;
    }
    st.n = 0;
  }

  int moved = 0;
  for (int i = 0; i < taken; i++) {
    uint32_t at = takenRoom[i].at;
    seq::Note n = takenRoom[i].n;
    uint32_t on = ((at + (uint32_t)grid / 2) / (uint32_t)grid) * (uint32_t)grid;
    on = on % round;  // (the last grid line of the round is the first of the next)
    long move = (long)on - (long)at;
    if (move > (long)round / 2) {
      move -= (long)round;  // (round the short way)
    } else if (move < -(long)round / 2) {
      move += (long)round;
    }
    long want = (long)at + move * strength / 100;
    want = (want % (long)round + (long)round) % (long)round;
    if (lengths) {
      int held2 = clampi((int)n.gate ? (int)n.gate * SEQ_STEP / 100 : SEQ_STEP * 80 / 100, 1, SEQ_STEP * 2);
      int snapped = ((held2 + grid / 2) / grid) * grid;
      snapped = snapped < grid ? grid : snapped;
      n.gate = (uint8_t)clampi(snapped * 100 / SEQ_STEP, 1, 200);
    }
    int step = (int)(want / SEQ_STEP) % len;
    int offset = (int)(want - (long)step * SEQ_STEP);
    if (offset > SEQ_STEP / 2) {  // (nearer the next step: it belongs to that one)
      step = (step + 1) % len;
      offset -= SEQ_STEP;
    }
    n.offset = (int8_t)clampi(offset, -127, 127);
    seq::Step& st = tr.step[step];
    if (st.n >= SEQ_NOTES) {
      if (dropped) {
        (*dropped)++;
      }
      away++;
      continue;
    }
    if (step != takenRoom[i].from || n.offset != takenRoom[i].n.offset) {
      moved++;
    }
    st.note[st.n++] = n;
  }
  (void)away;
  return moved;
}

// ------------------------------------------------------------------------------------- the clock's driver ---

void seq::seed(uint32_t s) {
  rnd = s ? s : 1;
}

void seq::advance(int sub) {
  if (!ready()) {
    return;
  }
  if (!running) {
    return;
  }
  for (int i = 0; i < sub; i++) {
    for (auto& p : pend) {  // whatever is due by now
      if (!p.kind || p.at > pos) {
        continue;
      }
      if (p.kind == 2) {
        send((uint8_t)(0x80 | p.ch), p.pitch, 0);
        p.kind = 0;
        continue;
      }
      send((uint8_t)(0x90 | p.ch), p.pitch, p.vel ? p.vel : 1);
      put(p.at + p.hold, 2, p.ch, p.pitch, 0);  // it ends after its hold
      if (p.left) {  // a ratchet: the next hit arms itself, so the list never fills with them
        p.at += p.every ? p.every : 1;
        p.left--;
      } else {
        p.kind = 0;
      }
    }
    if (curStep + 1 >= 0 && pos >= stepAt((uint32_t)(curStep + 1))) {  // the playhead has reached the next step
      curStep++;
    }
    pos++;
    plan();
    mark();
  }
}
