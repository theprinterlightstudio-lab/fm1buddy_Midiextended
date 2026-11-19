// Deterministic host harness for S7 clock/transport logic (midisync + seq).
// Stubs platform deps (hal/play) to exercise realtime handling, 24->96 accumulation,
// transport, SPP pattern seeking, and clock-loss hold/recovery without hardware.
// This does not replace full midi-dispatch/routing/thru integration or physical validation.
//
//   c++ -std=gnu++17 -Wall -I src/app -o build/clock_harness_test
//       test/clock_harness_test.cpp src/app/midisync.cpp src/app/seq.cpp
//   build/clock_harness_test
//
// seq.cpp is built here WITHOUT SEQ_NO_MIDISYNC, on purpose: the hooks between the engine's transport and the
// clock's owner are part of what is being tested (a local Play while an external clock is the authority), and
// stubbing them out would have hidden the very thing that was wrong.

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "midisync.h"
#include "play.h"
#include "seq.h"

static uint32_t fakeNow = 1000;
static int fails;

namespace hal {
uint32_t millis() { return fakeNow; }
void log(const char* fmt, ...) {
  (void)fmt;
  // keep harness output quiet by default
}
void* bigAlloc(unsigned long bytes) { return calloc(1, (size_t)bytes); }
void breathe() {}
}  // namespace hal

namespace midi {
bool routing = true;  // not used by this harness
}

static std::vector<uint8_t> outRealtime;

// what the engine itself played, so notes left sounding can be seen
struct Note3 {
  uint8_t a, b, c;
};
static std::vector<Note3> played;
static std::vector<uint32_t> whenPlayed;  // the millisecond each went out, for timing checks
static void seqOut(const uint8_t* m, int n) {
  if (n >= 3) {
    played.push_back({m[0], m[1], m[2]});
    whenPlayed.push_back(fakeNow);
  }
}
static int countOf(uint8_t kind, uint8_t pitch) {
  int n = 0;
  for (const Note3& e : played) {
    if ((e.a & 0xF0) == kind && e.b == pitch) {
      n++;
    }
  }
  return n;
}
static bool sentRealtime(uint8_t b) {
  for (uint8_t x : outRealtime) {
    if (x == b) {
      return true;
    }
  }
  return false;
}

static void outCb(int d, const uint8_t* msg, uint32_t n) {
  (void)d;
  if (n == 1) outRealtime.push_back(msg[0]);
}

namespace play {
const char* const CLOCK_NAMES[CLOCKS] = {"Off", "Lead", "Follow"};
const char* const ARP_NAMES[ARPS] = {"up", "down", "up-down", "random", "as played"};
const char* const RATE_NAMES[RATES] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
const char* const CHORD_NAMES[CHORDS] = {"off", "major", "minor", "7th", "maj7", "min7", "sus4", "5th", "octave"};

static Setup cur{};

Setup setup() { return cur; }
void set(const Setup& s) {
  cur = s;
  midisync::setLeading(s.clock == CLOCK_LEAD);  // as play.cpp does: it tells midisync, it is never asked
}
void begin() {}
float tempo() { return cur.bpm; }
bool playing() { return false; }
void start() {}
void stop() {}
void tap() {}
uint32_t beat() { return 0; }
void summary(char* out, int n) { if (n > 0) out[0] = 0; }
void skip(uint32_t) {}
bool noteLocked(int, const uint8_t*, Out) { return false; }
bool realtimeLocked(int, uint8_t b, Out) {
  // Neutral stub: external clock master behavior is tested via midisync state/accumulator
  // and seq transport application. (Full play-follow coupling not exercised here.)
  (void)b;
  return false;
}
void tickLocked(Out) {}
void allOffLocked(Out) {}
}  // namespace play


// ---------------------------------------------------------------------------- spacing between clock pulses ---
// The sequencer's thread wakes about every millisecond and takes whatever is due (seqdrive.cpp). These stand in
// for it, so what comes out can be timed.
static void pulse() {
  midisync::realtimeInLocked(0, 0xF8, outCb);
}

// let ms of time pass a millisecond at a time, playing whatever falls due. Returns how many sub-ticks went out
static int pump(int ms) {
  int total = 0;
  for (int i = 0; i < ms; i++) {
    fakeNow += 1;
    midisync::setNowMs(fakeNow);
    int got = midisync::consumeSeqTicks(32);
    if (got > 0) {
      seq::advance(got);
      total += got;
    }
  }
  return total;
}

// a steady clock of `interval` ms a pulse, long enough for its length to be known. A pulse lands on the instant
// this is called at, and `interval` ms pass before the next one, so pulse to pulse is exactly that
static void steadyClock(int interval, int pulses) {
  for (int i = 0; i < pulses; i++) {
    pulse();
    (void)midisync::consumeSeqTicks(32);
    (void)pump(interval);
  }
}

// when, in milliseconds after the pulse, each of that pulse's sub-ticks was handed out
static void spacingAfterPulse(int interval, std::vector<int>& at) {
  at.clear();
  pulse();
  int got = midisync::consumeSeqTicks(32);
  for (int i = 0; i < got; i++) {
    at.push_back(0);
  }
  for (int ms = 1; ms < interval; ms++) {
    fakeNow += 1;
    midisync::setNowMs(fakeNow);
    got = midisync::consumeSeqTicks(32);
    for (int i = 0; i < got; i++) {
      at.push_back(ms);
    }
  }
}

static bool near(int a, int b, int by) {
  return a - b <= by && b - a <= by;
}


// ------------------------------------------------------------- all sixteen tracks, on someone else's clock ---
// What the engine played, and the sub-tick it played it on. Musical time, not the clock on the wall: this is what
// must come out the same whether the engine is driven by its own clock or by a master's pulses.
struct Beat {
  uint32_t at;
  uint8_t a, b;
};
static std::vector<Beat> beats;
static bool beatsOn;
static void beatOut(const uint8_t* m, int n) {
  if (n >= 3 && beatsOn) {
    beats.push_back({seq::ticks(), m[0], m[1]});
  }
}

// 16 tracks, a three-note chord on every step, ratchets, swing, micro-timing, probability and humanize
static void fillAllSixteen() {
  seq::clear();
  seq::setLength(16);
  seq::setTracks(SEQ_TRACKS);
  seq::setSwing(50);
  for (int t = 0; t < SEQ_TRACKS; t++) {
    seq::setChannel(t, t);
    for (int st = 0; st < 16; st++) {
      for (int k = 0; k < 3; k++) {
        seq::addNote(t, st, 36 + ((t + k * 4) & 63), 100);
      }
      seq::Step& step = seq::at(t, st);
      step.note[0].repeat = 2;                            // a ratchet
      step.note[1].human = 40;                            // played by hand
      step.note[2].offset = (int8_t)((st % 5) - 2);       // early and late
      step.note[2].prob = 80;                             // and not every time
    }
  }
}

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    fails++;
  }
}

static void resetAll() {
  fakeNow = 1000;
  outRealtime.clear();
  played.clear();
  whenPlayed.clear();
  midisync::setNowMs(fakeNow);
  seq::clear();
  seq::setBpm(120);
  midisync::begin();
  midisync::setMode(midisync::MODE_EXTERNAL_MIDI, -1);
}

int main() {
  static uint8_t patBuf[sizeof(seq::Pattern)];
  static uint8_t scratchBuf[sizeof(seq::Pattern)];
  memset(patBuf, 0, sizeof(patBuf));
  memset(scratchBuf, 0, sizeof(scratchBuf));
  seq::usePattern(patBuf);
  seq::useScratch(scratchBuf);
  seq::clear();
  seq::setOut(nullptr);

  // ------------------------------------------------------------------------------------------------------
  // A) The clock source survives starting up, whichever order things start in.
  //
  // These run before anything else chooses a mode, so they see begin() exactly as the board does on power-up.
  // The defect: begin() put the mode back to internal every time, so a saved "follow" never took effect and the
  // sequencer quietly ran on its own clock.
  {
    play::Setup saved{};
    saved.clock = play::CLOCK_FOLLOW;
    saved.bpm = 120;
    play::set(saved);

    // begin() running before anything applied the setting: it must take the setting, not force internal
    midisync::begin();
    check(midisync::mode() == midisync::MODE_EXTERNAL_MIDI, "A: begin() takes the saved follow setting");

    // and the order the board actually uses: the setting is applied first, then begin() runs
    midisync::setMode(midisync::MODE_EXTERNAL_MIDI, -1);
    midisync::begin();
    check(midisync::mode() == midisync::MODE_EXTERNAL_MIDI, "A: begin() does not undo a mode already chosen");
    check(midisync::state().master == midisync::SRC_DEVICE, "A: and the master is the external device");

    // the other way round too: internal stays internal
    midisync::setMode(midisync::MODE_INTERNAL, -1);
    midisync::begin();
    check(midisync::mode() == midisync::MODE_INTERNAL, "A: internal stays internal through begin()");
    check(midisync::state().master == midisync::SRC_INTERNAL, "A: with itself as master");
  }

  // 1) External Start + 24 PPQ pulses => 96 PPQ accumulator (4:1) while playing.
  resetAll();
  midisync::realtimeInLocked(0, 0xFA, outCb);  // Start (establishes external source/timing ref)
  check(seq::playing(), "external Start starts sequencer transport");
  for (int i = 0; i < 24; i++) {
    fakeNow += 20;  // ~125 BPM clock, regular
    midisync::setNowMs(fakeNow);
    midisync::realtimeInLocked(0, 0xF8, outCb);
  }
  int got = midisync::consumeSeqTicks(1000);
  if (got != 24 * 4) {
    printf("  debug: got=%d expected=%d pulse=%u tr=%u extValid=%u lost=%u\n", got, 24 * 4,
           (unsigned)midisync::state().pulse, (unsigned)midisync::state().transport,
           (unsigned)midisync::state().extValid, (unsigned)midisync::state().clockLost);
  }
  check(got == 24 * 4, "24 external pulses after Start produce 96 sequencer subticks (4:1)");
  check(midisync::state().clockLost == 0, "regular clock not marked lost");

  // 2) Stop halts transport and clears pending accumulation semantics for sync state.
  midisync::realtimeInLocked(0, 0xFC, outCb);
  check(!seq::playing(), "external Stop stops sequencer transport");

  // 3) Continue resumes without resetting position to start.
  resetAll();
  midisync::realtimeInLocked(0, 0xFA, outCb);
  // advance a bit internally by consuming simulated external ticks
  for (int i = 0; i < 10; i++) {
    fakeNow += 20;
    midisync::setNowMs(fakeNow);
    midisync::realtimeInLocked(0, 0xF8, outCb);
  }
  int adv = midisync::consumeSeqTicks(1000);
  if (adv > 0) seq::advance(adv);
  uint32_t posBeforeStop = seq::ticks();
  midisync::realtimeInLocked(0, 0xFC, outCb);
  check(!seq::playing(), "stop before continue test");
  midisync::realtimeInLocked(0, 0xFB, outCb);
  check(seq::playing(), "external Continue resumes transport");
  // position should remain at/near where stopped (not reset to 0)
  check(seq::ticks() >= posBeforeStop, "continue preserves position (no reset to start)");
  check(seq::ticks() < posBeforeStop + (uint32_t)SEQ_STEP, "continue does not jump a step on resuming");

  // 4) SPP pattern-relative seeking (supported subset): 0, 1, mid, boundary, beyond-pattern modulo.
  resetAll();
  midisync::realtimeInLocked(0, 0xFA, outCb);
  midisync::sppLocked(0, outCb);
  check(seq::ticks() == 0 * SEQ_STEP, "SPP 0 seeks to start (tick 0)");
  midisync::sppLocked(1, outCb);
  check(seq::ticks() == 1 * SEQ_STEP, "SPP 1 seeks to step 1 tick");
  int patSteps = seq::pattern().steps ? seq::pattern().steps : 16;
  int mid = patSteps / 2;
  midisync::sppLocked((uint16_t)mid, outCb);
  check(seq::ticks() == (uint32_t)mid * (uint32_t)SEQ_STEP, "SPP mid-pattern seeks to expected tick");
  midisync::sppLocked((uint16_t)(patSteps - 1), outCb);
  check(seq::ticks() == (uint32_t)(patSteps - 1) * (uint32_t)SEQ_STEP, "SPP pattern boundary (last step) seeks correctly");
  // Beyond pattern length: current supported subset maps modulo current pattern length.
  midisync::sppLocked((uint16_t)(patSteps + 2), outCb);
  check(seq::ticks() == (uint32_t)((patSteps + 2) % patSteps) * (uint32_t)SEQ_STEP,
        "SPP beyond pattern length maps to pattern-relative modulo (documented subset)");

  // 5) Clock loss: timeout enters hold, no subticks advanced while held.
  resetAll();
  midisync::realtimeInLocked(0, 0xFA, outCb);
  fakeNow += 20;
  midisync::setNowMs(fakeNow);
  midisync::realtimeInLocked(0, 0xF8, outCb);
  fakeNow += 20;
  midisync::setNowMs(fakeNow);
  midisync::realtimeInLocked(0, 0xF8, outCb);  // establish interval
  (void)midisync::consumeSeqTicks(1000);
  fakeNow += 3000;  // long gap > timeout
  midisync::setNowMs(fakeNow);
  midisync::tickLocked(outCb, fakeNow);
  check(midisync::state().clockLost != 0, "clock loss detected after timeout");
  check(midisync::state().lostHold != 0, "clock loss enters hold (no silent fallback)");
  int duringHold = midisync::consumeSeqTicks(1000);
  check(duringHold == 0, "no sequencer subticks consumed while in clock-lost hold");

  // 6) Recovery: next pulse resyncs without burst/catch-up.
  midisync::realtimeInLocked(0, 0xF8, outCb);
  check(midisync::state().clockLost == 0, "clock recovers on resumed pulse");
  int afterRecover = midisync::consumeSeqTicks(1000);
  check(afterRecover == 0, "recovery pulse establishes timing without catch-up burst");

  // ------------------------------------------------------------------------------------------------------
  // B) The clock goes while a note is sounding: the note must end, and nothing else may move.
  resetAll();
  seq::setOut(seqOut);
  seq::setLength(16);
  seq::setNote(0, 0, 60, 100);
  seq::at(0, 0).note[0].gate = 200;  // two steps long, so it is still sounding when the clock goes
  midisync::realtimeInLocked(0, 0xFA, outCb);
  for (int i = 0; i < 3; i++) {  // a few pulses: the note starts, and an interval is established
    fakeNow += 20;
    midisync::setNowMs(fakeNow);
    midisync::realtimeInLocked(0, 0xF8, outCb);
  }
  seq::advance(midisync::consumeSeqTicks(1000));
  check(countOf(0x90, 60) == 1, "B: the note is sounding before the clock goes");
  check(countOf(0x80, 60) == 0, "B: and has not ended yet");
  uint32_t heldAt = seq::ticks();

  fakeNow += 3000;  // the clock stops
  midisync::setNowMs(fakeNow);
  midisync::tickLocked(outCb, fakeNow);
  check(midisync::state().clockLost != 0, "B: the clock is seen to have gone");
  check(midisync::state().lostHold != 0, "B: and the timing is held, not fallen back");

  // the engine's own thread does the releasing, one turn later: this stands in for it (seqdrive.cpp)
  // exactly as the driver's thread does it: it only releases when it has been asked (seqdrive.cpp)
  bool asked = midisync::takeNoteRelease();
  check(asked, "B: the sequencer is asked to let its notes go");
  if (asked) {
    seq::releaseSounding();
  }
  check(countOf(0x80, 60) == 1, "B: the note that was sounding is ended");
  check(seq::playing(), "B: and the transport is held, not stopped");
  check(seq::ticks() == heldAt, "B: and has not moved");
  check(!midisync::takeNoteRelease(), "B: it is asked once only, so no note is ended twice");

  // nothing more goes out while it is held
  int wasOn = countOf(0x90, 60), wasOff = countOf(0x80, 60);
  check(midisync::consumeSeqTicks(1000) == 0, "B: nothing is played while the clock is held");
  seq::advance(0);
  check(countOf(0x90, 60) == wasOn && countOf(0x80, 60) == wasOff, "B: and nothing is sent");

  // the clock comes back: it resyncs without ending that note again and without starting one
  midisync::realtimeInLocked(0, 0xF8, outCb);
  check(midisync::state().clockLost == 0, "B: the clock is heard again");
  check(countOf(0x80, 60) == 1, "B: the note is not ended a second time");
  check(countOf(0x90, 60) == 1, "B: and none is started by the recovery itself");
  check(seq::playing(), "B: the transport was not restarted by the recovery");
  seq::setOut(nullptr);

  // ------------------------------------------------------------------------------------------------------
  // C) Play, pressed here, while an external clock is the authority.
  //
  // The defect: it returned without starting anything and waited for a Start message that many masters never
  // send, so the button did nothing at all and said nothing.
  resetAll();
  seq::setOut(seqOut);
  seq::setLength(16);
  seq::setNote(0, 0, 72, 100);
  check(!seq::playing(), "C: stopped to begin with");
  seq::play();
  check(seq::playing(), "C: Play starts the transport, so the screen can show it running");
  check(!sentRealtime(0xFA) && !sentRealtime(0xFB), "C: and no Start of ours goes back to the master");
  check(!midisync::seqInternalDrive(), "C: our own clock still does not drive it");
  check(midisync::consumeSeqTicks(1000) == 0, "C: with no clock yet, there is nothing to play");
  uint32_t atRest = seq::ticks();

  for (int i = 0; i < 8; i++) {  // the master's clock arrives: now it moves
    fakeNow += 20;
    midisync::setNowMs(fakeNow);
    midisync::realtimeInLocked(0, 0xF8, outCb);
  }
  int moved = midisync::consumeSeqTicks(1000);
  check(moved > 0, "C: the master's pulses are what move it on");
  seq::advance(moved);
  check(seq::ticks() > atRest, "C: and it has moved");
  check(countOf(0x90, 72) == 1, "C: the first step played");
  check(!sentRealtime(0xF8), "C: and we never send a clock of our own while following");

  // a Start from the master while it is already running starts it again from the top
  midisync::realtimeInLocked(0, 0xFA, outCb);
  check(seq::playing(), "C: an incoming Start keeps it running");
  check(seq::ticks() == 0, "C: from the top");
  midisync::realtimeInLocked(0, 0xFC, outCb);
  check(!seq::playing(), "C: and an incoming Stop still stops it");
  seq::setOut(nullptr);


  // ------------------------------------------------------------------------------------------------------
  // D) The four sub-ticks of a pulse are spread across the gap, not handed over in a lump.
  //
  // A MIDI clock gives 24 a quarter note and the engine counts 96. Taking all four at once put everything inside
  // a pulse on the same instant - twenty milliseconds wide at 125 BPM - so a note recorded three sub-ticks late
  // played exactly where one on the beat did, and only when following someone else's clock.
  {
    // 125 BPM: 20 ms a pulse, so a quarter of the way is 5 ms
    resetAll();
    midisync::realtimeInLocked(0, 0xFA, outCb);
    steadyClock(20, 4);
    std::vector<int> at;
    spacingAfterPulse(20, at);
    check((int)at.size() == 4, "D: four sub-ticks between one pulse and the next");
    if ((int)at.size() == 4) {
      check(at[0] == 0, "D: the first is on the pulse");
      check(near(at[1], 5, 1), "D: the second a quarter of the way");
      check(near(at[2], 10, 1), "D: the third half way");
      check(near(at[3], 15, 1), "D: the fourth three quarters of the way");
    }

    // 100 BPM: 25 ms a pulse, so the spacing scales with it
    resetAll();
    midisync::realtimeInLocked(0, 0xFA, outCb);
    steadyClock(25, 4);
    spacingAfterPulse(25, at);
    check((int)at.size() == 4, "D: four sub-ticks at the slower clock too");
    if ((int)at.size() == 4) {
      check(at[0] == 0, "D: the first is on the pulse");
      check(near(at[1], 6, 1), "D: the second a quarter of 25 ms");
      check(near(at[2], 12, 1), "D: the third half of it");
      check(near(at[3], 18, 1), "D: the fourth three quarters of it");
    }
  }

  // nothing is played past the pulse that is coming, however long it takes to arrive
  {
    resetAll();
    midisync::realtimeInLocked(0, 0xFA, outCb);
    steadyClock(20, 4);
    pulse();
    int went = midisync::consumeSeqTicks(32);
    went += pump(20 * 3);  // the master has suddenly slowed right down, or stopped sending for a while
    check(went == 4, "D: a pulse is worth four sub-ticks and no more, however late the next one is");
  }

  // a master that jitters is followed, and nothing builds up over time
  {
    resetAll();
    midisync::realtimeInLocked(0, 0xFA, outCb);
    steadyClock(20, 2);
    int pulses = 0, went = 0;
    const int JITTER[8] = {20, 17, 23, 19, 21, 16, 24, 20};
    for (int round = 0; round < 8; round++) {
      for (int i = 0; i < 8; i++) {
        pulse();
        pulses++;
        went += midisync::consumeSeqTicks(32);
        went += pump(JITTER[i]);  // then that long until the next one
      }
    }
    check(went == pulses * 4, "D: over 64 jittered pulses, exactly four sub-ticks each and no drift");
  }

  // the clock goes: nothing more is played, however much time passes
  {
    resetAll();
    midisync::realtimeInLocked(0, 0xFA, outCb);
    steadyClock(20, 4);
    fakeNow += 3000;
    midisync::setNowMs(fakeNow);
    midisync::tickLocked(outCb, fakeNow);
    check(midisync::state().clockLost != 0, "D: the clock is seen to have gone");
    check(pump(500) == 0, "D: and nothing is played while it is away");
  }

  // it comes back: no pile of overdue sub-ticks arrives at once
  {
    pulse();
    check(midisync::state().clockLost == 0, "D: the clock is heard again");
    int burst = midisync::consumeSeqTicks(32);
    check(burst == 0, "D: and nothing is owed from the time it was away");
    fakeNow += 20;
    midisync::setNowMs(fakeNow);
    pulse();
    check(midisync::consumeSeqTicks(32) <= 4, "D: it picks up at four a pulse, not in a rush");
  }

  // ------------------------------------------------------------------------------------------------------
  // D2) What was recorded still plays as it was recorded, on someone else's clock.
  //
  // Two notes on the same step, one of them three sub-ticks late - which is what recording keeps, so that
  // quantizing stays something you ask for afterwards. Under an external clock they must still come out at
  // different moments. The burst put them on the same instant, and only when following someone else.
  {
    resetAll();
    seq::setOut(seqOut);
    seq::setLength(16);
    seq::addNote(0, 4, 60, 100);  // on the step
    seq::addNote(0, 4, 67, 100);  // three sub-ticks late, as it was played
    seq::at(0, 4).note[1].offset = 3;
    midisync::realtimeInLocked(0, 0xFA, outCb);
    steadyClock(20, 2);  // the clock's length is known from here
    played.clear();
    whenPlayed.clear();

    for (int i = 0; i < 40; i++) {  // on through the pattern at a steady 125 BPM
      pulse();
      int got = midisync::consumeSeqTicks(32);
      if (got > 0) {
        seq::advance(got);
      }
      (void)pump(20);
    }

    int onTime = -1, late = -1;
    for (size_t i = 0; i < played.size(); i++) {
      if ((played[i].a & 0xF0) != 0x90 || !played[i].c) {
        continue;
      }
      if (played[i].b == 60 && onTime < 0) {
        onTime = (int)whenPlayed[i];
      }
      if (played[i].b == 67 && late < 0) {
        late = (int)whenPlayed[i];
      }
    }
    check(onTime >= 0 && late >= 0, "D2: both notes played");
    check(late > onTime, "D2: the one recorded late still plays later than the one on the step");
    check(near(late - onTime, 15, 2), "D2: and by about the three sub-ticks it was recorded by");
    seq::setOut(nullptr);
  }


  // ------------------------------------------------------------------------------------------------------
  // E) All sixteen tracks at once, on a master's clock.
  //
  // The same pattern is played twice: once on the engine's own clock, driven by hand, and once from a master's
  // pulses through midisync. Every note must land on the same sub-tick both times - following someone else's
  // clock may change when a sub-tick happens in the room, never what is played or where it falls in the music.
  {
    const int CYCLES = 12, STEPS = 16;  // a long run, not a demonstration
    const uint32_t SUBS = (uint32_t)CYCLES * STEPS * SEQ_STEP;  // 1,536 sub-ticks, 384 pulses

    // its own clock
    resetAll();
    midisync::setMode(midisync::MODE_INTERNAL, -1);
    seq::setOut(beatOut);
    fillAllSixteen();
    seq::seed(0x5EED1234);
    beats.clear();
    beatsOn = true;
    seq::play();
    for (uint32_t i = 0; i < SUBS; i++) {
      seq::advance(1);
    }
    seq::stop();
    beatsOn = false;
    std::vector<Beat> alone = beats;
    uint32_t droppedAlone = seq::dropped();
    uint32_t peakAlone = seq::peakPending();

    // the same, from a master's pulses
    resetAll();
    midisync::setMode(midisync::MODE_EXTERNAL_MIDI, -1);
    seq::setOut(beatOut);
    fillAllSixteen();
    seq::seed(0x5EED1234);
    beats.clear();
    beatsOn = true;
    midisync::realtimeInLocked(0, 0xFA, outCb);
    uint32_t pulses = 0, handed = 0;
    while (handed < SUBS && pulses < SUBS) {
      pulse();
      pulses++;
      int got = midisync::consumeSeqTicks(32);
      if (got > 0) {
        seq::advance(got);
        handed += (uint32_t)got;
      }
      handed += (uint32_t)pump(20);  // 125 BPM, a millisecond at a time, as the driver's thread does
    }
    seq::stop();  // as the run on its own clock ended: what is still sounding is let go, and counted
    beatsOn = false;
    std::vector<Beat> followed = beats;
    uint32_t droppedFollowed = seq::dropped();
    uint32_t peakFollowed = seq::peakPending();

    printf("  16 tracks: %u events alone, %u following; %u pulses, %u sub-ticks; dropped %u/%u, most waiting %u/%u\n",
           (unsigned)alone.size(), (unsigned)followed.size(), (unsigned)pulses, (unsigned)handed,
           (unsigned)droppedAlone, (unsigned)droppedFollowed, (unsigned)peakAlone, (unsigned)peakFollowed);

    check(handed == (uint32_t)pulses * 4, "E: four sub-ticks a pulse over the whole run, nothing gained or lost");
    check(alone.size() > 2000, "E: the run is a real workload");
    check(followed.size() == alone.size(), "E: the same number of notes either way");

    size_t same = 0, upTo = alone.size() < followed.size() ? alone.size() : followed.size();
    for (size_t i = 0; i < upTo; i++) {
      if (alone[i].at == followed[i].at && alone[i].a == followed[i].a && alone[i].b == followed[i].b) {
        same++;
      }
    }
    check(same == upTo, "E: every note on the same sub-tick, the same channel and the same pitch");

    int ons = 0, offs = 0;
    for (const Beat& e : followed) {
      ons += (e.a & 0xF0) == 0x90;
      offs += (e.a & 0xF0) == 0x80;
    }
    check(ons == offs, "E: following, everything that started has ended");
    check(droppedFollowed == 0, "E: and nothing was dropped for want of room");
    check(droppedFollowed == droppedAlone, "E: no more than on its own clock");
    check(peakFollowed <= 256, "E: the waiting list stayed within itself");
    seq::setOut(nullptr);
  }


  // ------------------------------------------------------------------------------------------------------
  // E2) Channels, shared channels, mute, solo and a track moved while it is playing - on a master's clock.
  {
    resetAll();
    midisync::setMode(midisync::MODE_EXTERNAL_MIDI, -1);
    seq::setOut(beatOut);
    seq::clear();
    seq::setLength(8);
    seq::setTracks(SEQ_TRACKS);
    for (int t = 0; t < SEQ_TRACKS; t++) {
      seq::setChannel(t, t);
      seq::setNote(t, t % 8, 40 + t, 100);
    }
    seq::setChannel(3, 7);   // two tracks sharing one channel, on purpose
    seq::setChannel(11, 7);  // and a third
    seq::setMute(5, true);
    seq::setSolo(0, false);
    beats.clear();
    beatsOn = true;
    midisync::realtimeInLocked(0, 0xFA, outCb);
    for (int i = 0; i < 8 * 6; i++) {  // six times round
      pulse();
      int got = midisync::consumeSeqTicks(32);
      if (got > 0) {
        seq::advance(got);
      }
      (void)pump(20);
    }
    seq::stop();
    beatsOn = false;

    // every channel balances, shared ones included
    int onCh[16] = {0}, offCh[16] = {0};
    bool muted = false;
    for (const Beat& e : beats) {
      int ch = e.a & 0x0F;
      if ((e.a & 0xF0) == 0x90) {
        onCh[ch]++;
      } else if ((e.a & 0xF0) == 0x80) {
        offCh[ch]++;
      }
      if (e.b == 45) {  // track 6's note: it is muted
        muted = true;
      }
    }
    bool balanced = true;
    for (int c = 0; c < 16; c++) {
      balanced = balanced && onCh[c] == offCh[c];
    }
    check(balanced, "E2: every channel ends what it starts, shared channels included");
    check(onCh[7] > 0, "E2: the shared channel played");
    check(!muted, "E2: a muted track stays quiet");

    // solo: only the soloed track is heard
    seq::setMute(5, false);
    seq::setSolo(2, true);
    beats.clear();
    beatsOn = true;
    midisync::realtimeInLocked(0, 0xFA, outCb);
    for (int i = 0; i < 8 * 4; i++) {
      pulse();
      int got = midisync::consumeSeqTicks(32);
      if (got > 0) {
        seq::advance(got);
      }
      (void)pump(20);
    }
    seq::stop();
    beatsOn = false;
    bool onlySolo = !beats.empty();
    for (const Beat& e : beats) {
      if ((e.a & 0xF0) == 0x90 && e.b != 42) {
        onlySolo = false;
      }
    }
    check(onlySolo, "E2: soloed, only that track is heard");
    seq::setSolo(2, false);

    // a track moved to another channel while a note of its is sounding: the note must end where it began
    seq::clear();
    seq::setLength(8);
    seq::setChannel(0, 0);
    seq::setNote(0, 0, 60, 100);
    seq::at(0, 0).note[0].gate = 200;
    beats.clear();
    beatsOn = true;
    midisync::realtimeInLocked(0, 0xFA, outCb);
    pulse();
    int got = midisync::consumeSeqTicks(32);
    if (got > 0) {
      seq::advance(got);
    }
    seq::setChannel(0, 9);  // moved while it is sounding
    for (int i = 0; i < 8 * 3; i++) {
      pulse();
      got = midisync::consumeSeqTicks(32);
      if (got > 0) {
        seq::advance(got);
      }
      (void)pump(20);
    }
    seq::stop();
    beatsOn = false;
    int firstOnCh = -1, firstOffCh = -1;
    for (const Beat& e : beats) {
      if ((e.a & 0xF0) == 0x90 && firstOnCh < 0) {
        firstOnCh = e.a & 0x0F;
      } else if ((e.a & 0xF0) == 0x80 && firstOffCh < 0) {
        firstOffCh = e.a & 0x0F;
      }
    }
    check(firstOnCh == 0 && firstOffCh == 0, "E2: a track moved mid-note still ends that note where it began");
    int ons2 = 0, offs2 = 0;
    for (const Beat& e : beats) {
      ons2 += (e.a & 0xF0) == 0x90;
      offs2 += (e.a & 0xF0) == 0x80;
    }
    check(ons2 == offs2, "E2: and nothing is left sounding after the move");
    seq::setOut(nullptr);
  }

  // ------------------------------------------------------------------------------------------------------
  // E3) The clock goes with all sixteen tracks sounding, and comes back.
  {
    resetAll();
    midisync::setMode(midisync::MODE_EXTERNAL_MIDI, -1);
    seq::setOut(beatOut);
    seq::clear();
    seq::setLength(8);
    seq::setTracks(SEQ_TRACKS);
    for (int t = 0; t < SEQ_TRACKS; t++) {
      seq::setChannel(t, t);
      seq::setNote(t, 0, 50 + t, 100);
      seq::at(t, 0).note[0].gate = 200;  // all sixteen still sounding when the clock goes
    }
    beats.clear();
    beatsOn = true;
    midisync::realtimeInLocked(0, 0xFA, outCb);
    for (int i = 0; i < 3; i++) {
      pulse();
      int got = midisync::consumeSeqTicks(32);
      if (got > 0) {
        seq::advance(got);
      }
      (void)pump(20);
    }
    int sounding = 0;
    for (const Beat& e : beats) {
      sounding += ((e.a & 0xF0) == 0x90) ? 1 : -1;
    }
    check(sounding == SEQ_TRACKS, "E3: all sixteen tracks are sounding");

    fakeNow += 3000;
    midisync::setNowMs(fakeNow);
    midisync::tickLocked(outCb, fakeNow);
    check(midisync::state().clockLost != 0, "E3: the clock has gone");
    bool asked = midisync::takeNoteRelease();
    check(asked, "E3: the sequencer is asked to let go");
    if (asked) {
      seq::releaseSounding();
    }
    int left = 0;
    for (const Beat& e : beats) {
      left += ((e.a & 0xF0) == 0x90) ? 1 : -1;
    }
    check(left == 0, "E3: and all sixteen are released, none left sounding");
    check(seq::playing(), "E3: with the transport still held, not stopped");
    uint32_t heldAt = seq::ticks();

    // it comes back: no pile of events, no jump
    size_t wasEvents = beats.size();
    pulse();
    check(midisync::state().clockLost == 0, "E3: the clock is back");
    check(beats.size() == wasEvents, "E3: and nothing is played by the recovery itself");
    check(seq::ticks() == heldAt, "E3: the place is where it was left, no jump");
    int burst = midisync::consumeSeqTicks(32);
    check(burst == 0, "E3: nothing is owed from the time it was away");
    beatsOn = false;
    seq::setOut(nullptr);
  }

  // ------------------------------------------------------------------------------------------------------
  // E4) The master changes tempo: the spacing follows it.
  //
  // The gap between pulses is averaged, so that a master that wavers is not chased; it therefore takes a few
  // pulses to come across to a new tempo. These check where it settles, not the first pulse after the change.
  {
    resetAll();
    midisync::setMode(midisync::MODE_EXTERNAL_MIDI, -1);
    midisync::realtimeInLocked(0, 0xFA, outCb);
    steadyClock(20, 4);  // 125 BPM
    std::vector<int> at;
    spacingAfterPulse(20, at);
    check((int)at.size() == 4 && near(at[1], 5, 1), "E4: at 125 BPM, a quarter of 20 ms");

    steadyClock(40, 20);  // the master halves its tempo, and holds it
    spacingAfterPulse(40, at);
    check((int)at.size() == 4, "E4: still four a pulse at the new tempo");
    if ((int)at.size() == 4) {
      check(near(at[1], 10, 2), "E4: and now a quarter of 40 ms");
      check(near(at[3], 30, 2), "E4: three quarters of it");
    }

    steadyClock(15, 20);  // and speeds well up, and holds it
    spacingAfterPulse(15, at);
    check((int)at.size() == 4, "E4: four a pulse when it speeds up too");
    if ((int)at.size() == 4) {
      check(near(at[1], 3, 1), "E4: a quarter of 15 ms");
      check(near(at[3], 11, 2), "E4: three quarters of it");
    }
  }


  // ------------------------------------------------------------------------------------------------------
  // F) Leading: the sequencer as the clock master.
  //
  // There was always a master here - play.cpp sends 24 PPQ while its clock setting is "lead" - but it ran at
  // play's tempo while the sequencer ran at the pattern's, and starting the sequencer told nobody. The sequencer
  // now owns it while it is running. These check who owns it and what goes out; that play's own tick steps
  // aside, and that the bytes reach the devices, are in play.cpp and seqdrive.cpp and are covered by the builds.
  {
    play::Setup lead{};
    lead.clock = play::CLOCK_LEAD;
    lead.bpm = 120;

    resetAll();
    play::set(lead);
    midisync::setMode(midisync::MODE_INTERNAL, -1);
    seq::clear();
    seq::setLength(8);
    (void)midisync::takeTransportOut();

    check(!midisync::seqIsMaster(), "F: stopped, the sequencer is not the master");
    seq::play();
    check(midisync::seqIsMaster(), "F: running and leading, it is");
    check(midisync::takeTransportOut() == 0xFA, "F: and a start went out");
    check(midisync::takeTransportOut() == 0, "F: once, not again");

    seq::stop();
    check(!midisync::seqIsMaster(), "F: stopped again, it is not the master");
    check(midisync::takeTransportOut() == 0xFC, "F: and a stop went out");

    seq::play();
    (void)midisync::takeTransportOut();
    seq::stop();
    (void)midisync::takeTransportOut();
    seq::cont();
    check(midisync::takeTransportOut() == 0xFB, "F: carrying on sends a continue, not a start");

    // the clock setting says off: we are nobody's master and send nothing
    seq::stop();
    (void)midisync::takeTransportOut();
    play::Setup off{};
    off.clock = play::CLOCK_OFF;
    off.bpm = 120;
    play::set(off);
    seq::play();
    check(!midisync::seqIsMaster(), "F: with the clock setting off, the sequencer leads nothing");
    check(midisync::takeTransportOut() == 0, "F: and no transport goes out");
    seq::stop();

    // following someone else: nothing of ours goes out, so an incoming clock is never echoed
    resetAll();
    play::Setup follow{};
    follow.clock = play::CLOCK_FOLLOW;
    follow.bpm = 120;
    play::set(follow);
    midisync::setMode(midisync::MODE_EXTERNAL_MIDI, -1);
    (void)midisync::takeTransportOut();
    seq::play();
    check(!midisync::seqIsMaster(), "F: following, the sequencer is not the master");
    check(midisync::takeTransportOut() == 0, "F: and sends no transport of its own");
    midisync::realtimeInLocked(0, 0xFA, outCb);
    check(midisync::takeTransportOut() == 0, "F: a start heard from the master is not sent back");
    seq::stop();
    (void)midisync::takeTransportOut();

    // put the stub's setting back for whatever runs after
    play::Setup none{};
    play::set(none);
  }

  // 7) MIDI thru/routing independence of sync detection is a midi.cpp dispatch contract
  // (verified by code inspection and requires full midi dispatch harness for execution).

  if (fails) {
    printf("clock_harness_test: %d FAIL(s)\n", fails);
    return 1;
  }
  printf("clock_harness_test: ok\n");
  return 0;
}
