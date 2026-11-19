#include "midisync.h"

#include <string.h>

#include <atomic>

#include "hal.h"
#include "midi.h"
#include "play.h"
#include "seq.h"

// S7 unified clock ownership. This module coordinates timing consumers (play/seq) so there is
// one clock authority. It is driven from midi-locked realtime/tick contexts and from the
// sequencer's driver thread (for consuming accumulated external subticks).

namespace {

std::atomic<uint32_t> nowMsA{0};
std::atomic<uint32_t> seqTicksA{0};
std::atomic<uint32_t> droppedPulseA{0};
std::atomic<uint32_t> latePulseA{0};
std::atomic<bool> releaseNotesA{false};  // the clock has gone: the sequencer's thread must let its notes go
// How many of the four sub-ticks the newest pulse brought have been handed out already. It goes back to nought on
// every pulse, which is what re-anchors the spacing to the clock out there and stops any drift building up
std::atomic<int> givenThisPulse{0};
std::atomic<uint8_t> transportOutA{0};  // a start, continue or stop for the sequencer's thread to send
std::atomic<bool> leadOut{false};       // play's clock setting is "lead" (told to us; never asked for)

// Protected by midi lock for most fields (realtimeInLocked/tickLocked/internalPulseLocked run there).
midisync::Mode curMode = midisync::MODE_INTERNAL;
midisync::SourceKind curMaster = midisync::SRC_INTERNAL;
midisync::Transport tr = midisync::TR_STOPPED;
bool extValid = false;
bool clockLost = false;
bool lostHold = false;
int extDev = -1;
int hostDev = -1;
uint32_t extPulse = 0;      // 24 PPQ pulses since last (re)start/valid window
uint32_t lastPulseMs = 0;
uint32_t pulseIntervalMs = 0;  // smoothed
float extBpm = 120.0f;
uint8_t sppState = 0;  // 0 none, 1 got lsb
uint8_t sppLsb = 0;
uint16_t sppPos = 0;
bool sppReady = false;
// True once something has chosen the mode on purpose (the saved setting, applied by play::set/begin, or the UI).
// begin() may run before or after whoever applies that setting, so it must never decide the mode itself.
bool modeChosen = false;

const uint32_t CLOCK_TIMEOUT_MIN_MS = 400;   // absolute floor
const uint32_t CLOCK_TIMEOUT_FACTOR = 3;     // x interval

uint32_t timeoutMs() {
  uint32_t t = pulseIntervalMs ? pulseIntervalMs * CLOCK_TIMEOUT_FACTOR : 1000;
  return t < CLOCK_TIMEOUT_MIN_MS ? CLOCK_TIMEOUT_MIN_MS : t;
}

void pushSeqSubticks(int n) {
  if (n <= 0) return;
  seqTicksA.fetch_add((uint32_t)n);
}

void resetExternalTiming() {
  givenThisPulse.store(0);
  extPulse = 0;
  lastPulseMs = 0;
  pulseIntervalMs = 0;
  extBpm = 120.0f;
}

void enterClockLost() {
  if (clockLost) return;
  clockLost = true;
  lostHold = true;
  extValid = false;
  seqTicksA.store(0);
  // The timing is held, but a note that was sounding when the clock went would sound for ever. It cannot be ended
  // from here (this runs under the MIDI lock, and ending it goes out through that same lock), so the sequencer's
  // own thread is asked to do it. The transport is left exactly as it is: held, not stopped, not moved.
  releaseNotesA.store(true);
  // Do not silently switch to internal clock. Hold timing until clock resumes or source/mode
  // is explicitly changed. No stale pulses advance transport.
  hal::log("clock: external clock lost (holding; no internal fallback)");
}

void recoverFromLost(uint32_t now) {
  (void)now;
  clockLost = false;
  lostHold = false;
  extValid = true;
  seqTicksA.store(0);
  resetExternalTiming();
  // Resume following from current external stream; do not jump transport position silently.
  hal::log("clock: external clock recovered (resynced timing reference)");
}

bool modeUsesExternal() {
  return curMode == midisync::MODE_EXTERNAL_MIDI || curMode == midisync::MODE_HOST_MIDI;
}

}  // namespace

// leading as a sequencer: internal, the clock setting says lead, and there is a transport to lead with
bool midisync::seqIsMaster() {
  return curMode == MODE_INTERNAL && leadOut.load() && seq::playing();
}

void midisync::setLeading(bool on) {
  leadOut.store(on);
}

uint8_t midisync::takeTransportOut() {
  return transportOutA.exchange(0);
}

namespace {
// queue a transport message, if leading is ours to do. seq::playing() is not asked here: play() queues the start
// before the transport is actually running, and stop() after it has gone
void transportOut(uint8_t b) {
  if (curMode == midisync::MODE_INTERNAL && leadOut.load()) {
    transportOutA.store(b);
  }
}
}  // namespace

bool midisync::takeNoteRelease() {
  return releaseNotesA.exchange(false);
}

void midisync::begin() {
  // The clock source belongs to the user's saved setting (play.h), not to whoever starts first. begin() is called
  // from the sequencer's driver and play::begin() applies the setting; the two have run in both orders, and this
  // one used to put the mode back to internal every power-up, so a saved "follow" never took effect.
  //
  // So: the mode is only decided here if nobody has decided it yet, and then it is read from the setting itself.
  // Everything else - what has been heard, what is playing - is runtime state and is always reset.
  {
    // begin() runs at start-up, on no lock, so the setting can be read here once and kept
    play::Setup saved = play::setup();
    leadOut.store(saved.clock == play::CLOCK_LEAD);
    if (!modeChosen) {
      curMode = saved.clock == play::CLOCK_FOLLOW ? MODE_EXTERNAL_MIDI : MODE_INTERNAL;
    }
  }
  curMaster = modeUsesExternal() ? (curMode == MODE_HOST_MIDI ? SRC_HOST : SRC_DEVICE) : SRC_INTERNAL;
  tr = TR_STOPPED;
  extValid = false;
  clockLost = false;
  lostHold = false;
  extDev = -1;
  hostDev = -1;
  resetExternalTiming();
  seqTicksA.store(0);
  droppedPulseA.store(0);
  latePulseA.store(0);
  releaseNotesA.store(false);
  givenThisPulse.store(0);
  transportOutA.store(0);
}

bool midisync::setMode(Mode m, int hd) {
  // Safe source-change policy: do not silently restart or jump transport position.
  // Switching mode stops external timing validity until (re)acquired; transport remains as-is
  // except callers/UI may choose to stop explicitly.
  if (m >= MODES) return false;
  curMode = m;
  modeChosen = true;  // (chosen on purpose: begin() must not put it back)
  hostDev = hd;
  if (modeUsesExternal()) {
    curMaster = (m == MODE_HOST_MIDI) ? SRC_HOST : SRC_DEVICE;
    extValid = false;
    lostHold = clockLost;  // remain in hold if already lost
    // keep extDev if any; will be (re)validated on incoming clock
  } else {
    curMaster = SRC_INTERNAL;
    extDev = -1;
    extValid = false;
    clockLost = false;
    lostHold = false;
    resetExternalTiming();
    seqTicksA.store(0);
  }
  return true;
}

midisync::Mode midisync::mode() { return curMode; }

midisync::State midisync::state() {
  State s = {};
  s.mode = curMode;
  s.master = curMaster;
  s.transport = tr;
  s.extValid = extValid ? 1 : 0;
  s.clockLost = clockLost ? 1 : 0;
  s.lostHold = lostHold ? 1 : 0;
  s.extDev = (int16_t)extDev;
  s.bpm = (modeUsesExternal() && extValid) ? extBpm : (float)play::setup().bpm;
  s.pulse = extPulse;
  s.seqTicks = seqTicksA.load();
  return s;
}

bool midisync::sppFeed(uint8_t b, uint16_t& positionSixteenths) {
  if (sppState == 0) {
    sppLsb = b & 0x7F;
    sppState = 1;
    return false;
  }
  uint8_t msb = b & 0x7F;
  positionSixteenths = (uint16_t)(sppLsb | (msb << 7));
  sppState = 0;
  return true;
}

void midisync::sppLocked(uint16_t positionSixteenths, midi::Out out) {
  (void)out;
  if (modeUsesExternal() && curMaster == SRC_INTERNAL) return; // not our external context
  // SPP is in 16th-note units (MIDI beat). At SEQ_PPQ=96, one 16th = 24 subticks.
  // Pattern-level seeking is supported; song-arrangement-level SPP mapping is not fully modeled.
  bool wasPlaying = seq::playing();
  bool wasRunning = tr == TR_PLAYING;
  // Seek within current pattern. Full song-position semantics across arrangement boundaries are
  // not fabricated; this is the safe supported subset.
  int steps = seq::pattern() .steps; // not safe if not ready
  (void)steps;
  // Determine target pattern step. Without a song-position map, treat SPP as pattern-relative
  // for the current pattern (common in pattern sync) and log when song mode/arrangement context
  // would require broader mapping.
  bool ok = false;
  if (seq::ready()) {
    int patSteps = seq::pattern().steps;
    if (patSteps < 1) patSteps = 1;
    if (patSteps > SEQ_STEPS) patSteps = SEQ_STEPS;
    int target = (int)(positionSixteenths % (uint16_t)patSteps);
    ok = seq::seekToPatternStep(target);
    if (positionSixteenths >= (uint16_t)patSteps) {
      hal::log("clock: SPP %u mapped to pattern step %d (song/arrangement mapping limited)",
               (unsigned)positionSixteenths, target);
    }
  }
  if (!ok) {
    seq::transportStop();
    hal::log("clock: SPP %u ignored (sequencer not ready)", (unsigned)positionSixteenths);
  }
  // Do not restart or jump transport silently beyond the requested position. If transport was
  // playing, remain positioned and ready; external Start/Continue/clock will drive progression.
  // Keep local running state consistent with transport authority.
  if (wasRunning && modeUsesExternal()) {
    // External transport remains authoritative; position is updated, running state follows master.
    (void)wasPlaying;
  } else if (wasPlaying && !modeUsesExternal()) {
    // Internal/local context: keep stopped at seek position unless explicitly continued.
    seq::stop();
  }
}

bool midisync::realtimeInLocked(int d, uint8_t b, midi::Out out) {
  uint32_t now = nowMsA.load();
  if (!now) now = hal::millis();
  // Centralized realtime handler (S7.1+). Preserve existing play behavior while adding seq sync hooks.
  bool dropForward = false;

  if (b == 0xF2) {
    sppState = 1; // next byte is lsb
    // SPP is a system common message with 2 data bytes; our realtimeInLocked is called for status byte
    // from packet stream in current design. We will also accept data feeding via sppFeed if integrated.
    // For cin 0xF realtime path p[1]==0xF2 only status seen; full SPP data arrives as system common packets.
    return false; // allow forwarding as before unless policy says otherwise
  }

  // Existing play engine behavior (clock follow/lead, arp/beat, transport) must be preserved.
  // Delegate to play for F8/FA/FB/FC as before; play returns true to suppress forwarding in LEAD.
  if (b == 0xF8 || b == 0xFA || b == 0xFB || b == 0xFC) {
    dropForward = play::realtimeLocked(d, b, out);
  }

  if (!modeUsesExternal()) {
    return dropForward;
  }

  // External/HOST mode: incoming MIDI clock/transport can be authoritative.
  // Prevent generated/internal clock from being consumed as external: only accept from non-internal sources.
  if (curMaster == SRC_INTERNAL) return dropForward;

  // HOST_MIDI: if a specific host device is configured, only follow it; else accept first valid source.
  if (curMode == MODE_HOST_MIDI && hostDev >= 0 && d != hostDev) {
    return dropForward;
  }

  if (b == 0xF8) {
    // Clock pulse ownership: follow one source while valid to avoid duplicate masters.
    if (extDev != -1 && extDev != d) {
      if (extValid && !clockLost) return dropForward;
      // allow takeover on recovery from lost/invalid
    }
    if (clockLost) {
      recoverFromLost(now);
      extDev = d;
      resetExternalTiming();
      lastPulseMs = now;
      // Resynced timing reference: do not generate a catch-up burst.
      return dropForward;
    }
    if (!extValid || extDev != d) {
      extDev = d;
      extValid = true;
      lostHold = false;
      resetExternalTiming();
      lastPulseMs = now;
      extPulse = 1;
      return dropForward;
    }
    // Valid continuing pulse: 24 PPQ -> 4 sequencer subticks (96 PPQ), deterministic accumulation.
    uint32_t dt = lastPulseMs ? (now - lastPulseMs) : 0;
    lastPulseMs = now;
    if (dt > 0 && dt < 4000) {
      float inst = 60000.0f / (dt * 24.0f);
      extBpm = (pulseIntervalMs == 0) ? inst : (extBpm * 0.7f + inst * 0.3f);
      pulseIntervalMs = pulseIntervalMs ? (pulseIntervalMs * 3 + dt) / 4 : dt;
    } else if (dt >= 4000) {
      // implausibly large gap but not yet timed out by tickLocked: don't poison interval estimate
      latePulseA.fetch_add(1);
    }
    extPulse++;
    if (tr == TR_PLAYING && !lostHold) {
      pushSeqSubticks(4);
      givenThisPulse.store(0);  // a new pulse: the four it brings are spaced out from here
    }
    // Prevent feedback: consumed external pulse is never re-emitted as generated master clock.
    return dropForward;
  }

  if (b == 0xFA) { // Start
    tr = TR_PLAYING;
    extValid = true;
    lostHold = false;
    clockLost = false;
    extDev = d;
    extPulse = 0;
    lastPulseMs = now;
    seqTicksA.store(0);
    givenThisPulse.store(0);
    // Ensure sequencer transport starts from beginning in sync (authoritative external transport)
    seq::transportPlay();
    return dropForward;
  }
  if (b == 0xFB) { // Continue
    tr = TR_PLAYING;
    extValid = true;
    lostHold = false;
    clockLost = false;
    extDev = d;
    lastPulseMs = now;
    seq::transportCont();
    return dropForward;
  }
  if (b == 0xFC) { // Stop
    tr = TR_STOPPED;
    seqTicksA.store(0);
    seq::transportStop();
    return dropForward;
  }

  return dropForward;
}

void midisync::tickLocked(midi::Out out, uint32_t now) {
  (void)out;
  nowMsA.store(now);
  if (!modeUsesExternal()) return;
  if (extValid && !clockLost && lastPulseMs) {
    if (now - lastPulseMs > timeoutMs()) {
      enterClockLost();
      seqTicksA.store(0);
    }
  }
}

// Kept for the shape of the interface, and deliberately empty. Nothing calls it, and nothing should: there is
// one 24 PPQ generator while leading, and it is play's clock thread, or the sequencer's own when the sequencer is
// the master (seqIsMaster). A pulse generated here would be the second one
void midisync::internalPulseLocked(midi::Out out) {
  nowMsA.store(hal::millis());
  if (!modeUsesExternal()) {
    curMaster = SRC_INTERNAL;
  }
  (void)out;
}

void midisync::requestStart() {
  transportOut(0xFA);
  if (modeUsesExternal()) {
    // External clock is transport/timing authority; local request does not force internal advance.
    // We align local transport intent but actual progression follows external pulses/transport.
    tr = TR_PLAYING;
    // Don't emit competing clock; optionally could emit Start to devices in some policies (not here).
    return;
  }
  tr = TR_PLAYING;
}

void midisync::requestStop() {
  transportOut(0xFC);
  tr = TR_STOPPED;
  if (modeUsesExternal()) {
    seqTicksA.store(0);
  }
}

void midisync::requestContinue() {
  transportOut(0xFB);
  if (modeUsesExternal()) {
    tr = TR_PLAYING;
    return;
  }
  tr = TR_PLAYING;
}

void midisync::requestToggle() {
  if (tr == TR_PLAYING) requestStop();
  else requestStart();
}

bool midisync::seqInternalDrive() {
  if (!modeUsesExternal()) return true;
  // External master: internal 96 PPQ thread must not advance sequencer simultaneously.
  // If clock lost and in hold, no advancement (and no accumulated ticks consumed into play).
  return false;
}

// A MIDI clock gives 24 pulses a quarter note and the engine counts 96, so each pulse is worth four sub-ticks.
// Handing all four over at once would make everything inside a pulse happen at the same instant, and a sixteenth
// at 125 BPM is twenty milliseconds wide: a note recorded three sub-ticks late, or the late half of a swing, would
// land exactly where a note dead on the beat does. What was recorded would then play differently depending on
// whose clock was running, which is not a thing a sequencer may do.
//
// So the four are spaced across the gap between pulses, at the length that gap was last measured to be: the first
// on the pulse itself, the others a quarter, a half and three quarters of the way to the next one. The fourth is
// due before the next pulse, never after it, and every pulse sets the spacing out again from itself - so a master
// that speeds up, slows down or jitters is followed rather than argued with, and nothing accumulates.
//
// It is not a loop that locks onto a phase: there is nothing to tune, nothing that rings, and the worst it can be
// wrong by is one pulse, which is what it was before any of this.
int midisync::consumeSeqTicks(int maxSub) {
  if (maxSub <= 0) return 0;
  if (modeUsesExternal() && lostHold) return 0;
  uint32_t have = seqTicksA.load();
  if (!have) return 0;

  int allow;
  if (have > 4 || !pulseIntervalMs || !lastPulseMs) {
    // behind, or nothing measured yet (the first pulses after a start): let what is owed go at once. Being late
    // is not the same as running ahead, and a pulse boundary is the right place to catch up
    allow = (int)have;
  } else {
    uint32_t since = hal::millis() - lastPulseMs;
    int due = 1 + (int)(since * 4 / pulseIntervalMs);  // the first is due on the pulse itself
    allow = (due > 4 ? 4 : due) - givenThisPulse.load();
    if (allow > (int)have) {
      allow = (int)have;
    }
  }
  if (allow <= 0) {
    return 0;  // the next one is not due yet
  }

  int take = allow > maxSub ? maxSub : allow;
  while (!seqTicksA.compare_exchange_weak(have, have - (uint32_t)take)) {
    if ((int)have < take) {
      take = (int)have;
    }
    if (take <= 0) {
      return 0;
    }
  }
  givenThisPulse.fetch_add(take);
  return take;
}

void midisync::setNowMs(uint32_t now) { nowMsA.store(now); }
uint32_t midisync::nowMs() { return nowMsA.load(); }
uint32_t midisync::droppedPulses() { return droppedPulseA.load(); }
uint32_t midisync::latePulses() { return latePulseA.load(); }
