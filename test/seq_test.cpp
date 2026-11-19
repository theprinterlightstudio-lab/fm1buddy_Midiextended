// The sequencer engine's tests (src/app/seq.cpp): the pattern, the step and note model, swing, ratchets,
// probability, humanize, per-track lengths, mute and solo, and the transport. Everything is driven by hand
// (seq::advance), so what is played and exactly when can be checked.
//
//   c++ -std=gnu++17 -Wall -DSEQ_NO_MIDISYNC -I src/app -o /tmp/seq_test test/seq_test.cpp src/app/seq.cpp && /tmp/seq_test
#include "seq.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

namespace {

struct Ev {
  uint32_t at;  // the sub-tick it was played at
  uint8_t a, b, c;
};
std::vector<Ev> got;
uint32_t now;

void sink(const uint8_t* m, int n) {
  (void)n;
  got.push_back({now, m[0], m[1], m[2]});
}

int fails;

void check(bool ok, const std::string& what) {
  if (!ok) {
    printf("FAIL: %s\n", what.c_str());
    fails++;
  }
}

void checkEq(long a, long b, const std::string& what) {
  if (a != b) {
    printf("FAIL: %s (%ld, wanted %ld)\n", what.c_str(), a, b);
    fails++;
  }
}

// plays n sub-ticks, remembering when each message went out
void run(int n) {
  for (int i = 0; i < n; i++) {
    now = seq::ticks();
    seq::advance(1);
  }
}

void reset() {
  got.clear();
  seq::clear();
  seq::seed(12345);
}

// the note-ons of pitch p, and when
std::vector<uint32_t> onsOf(int p) {
  std::vector<uint32_t> v;
  for (const Ev& e : got) {
    if ((e.a & 0xF0) == 0x90 && e.b == p) {
      v.push_back(e.at);
    }
  }
  return v;
}

int count(int status) {
  int n = 0;
  for (const Ev& e : got) {
    n += (e.a & 0xF0) == status;
  }
  return n;
}

// ------------------------------------------------------------------------------------------------- tests ---

// 16 tracks: each addressable, each on its own channel by default, each independent of the others
void testSixteenTracks() {
  reset();
  checkEq(SEQ_TRACKS, 16, "16 tracks: the engine has that many");
  checkEq(seq::pattern().tracks, SEQ_TRACKS, "16 tracks: a new pattern uses them all");
  for (int t = 0; t < 16; t++) {
    checkEq(seq::channel(t), t, "default channel of track " + std::to_string(t + 1));
    check(seq::trackOn(t), "track " + std::to_string(t + 1) + " is on");
    checkEq((long)std::string(seq::trackName(t)).size() > 0, 1, "track " + std::to_string(t + 1) + " has a name");
  }
  // all sixteen at once, each on its own channel, each its own steps
  for (int t = 0; t < 16; t++) {
    seq::setNote(t, 0, 36 + t, 100);
  }
  seq::setLength(1);
  seq::play();
  run(SEQ_STEP);
  checkEq(count(0x90), 16, "16 tracks: all play together");
  for (int t = 0; t < 16; t++) {
    bool found = false;
    for (const Ev& e : got) {
      found |= e.a == (0x90 | t) && e.b == 36 + t;
    }
    check(found, "16 tracks: track " + std::to_string(t + 1) + " on channel " + std::to_string(t + 1));
  }
  // any track to any channel, the same channel twice, and one track's change touches no other
  reset();
  for (int t = 0; t < 16; t++) {
    seq::setChannel(t, 15 - t);
  }
  for (int t = 0; t < 16; t++) {
    checkEq(seq::channel(t), 15 - t, "re-assigned channel of track " + std::to_string(t + 1));
  }
  seq::setChannel(3, 9);
  seq::setChannel(4, 9);  // two tracks sharing one channel: allowed
  checkEq(seq::channel(3), 9, "two tracks may share a channel (one)");
  checkEq(seq::channel(4), 9, "two tracks may share a channel (the other)");
  checkEq(seq::channel(5), 10, "changing a track leaves its neighbour alone");
  checkEq(seq::channel(0), 15, "changing a track leaves track 1 alone");
  // a note ends on the channel it began on, even if the track moves meanwhile
  reset();
  seq::setLength(2);
  seq::setChannel(0, 0);
  seq::at(0, 0).n = 1;
  seq::at(0, 0).note[0] = {60, 100, 100, 100, 1, 0, 0, 0};
  seq::play();
  run(2);
  seq::setChannel(0, 7);  // moved while the note is sounding
  run(SEQ_STEP);
  long on = -1, off = -1;
  for (const Ev& e : got) {
    if ((e.a & 0xF0) == 0x90 && e.b == 60) {
      on = e.a & 15;
    }
    if ((e.a & 0xF0) == 0x80 && e.b == 60) {
      off = e.a & 15;
    }
  }
  checkEq(on, 0, "the note began on the track's channel");
  checkEq(off, 0, "and ends on the same one, though the track has moved");
  seq::stop();
  // a track switched off plays nothing; the rest carry on
  reset();
  seq::setLength(1);
  seq::setNote(0, 0, 60, 100);
  seq::setNote(1, 0, 61, 100);
  seq::setTrackOn(0, false);
  seq::play();
  run(SEQ_STEP);
  checkEq((long)onsOf(60).size(), 0, "a track switched off plays nothing");
  checkEq((long)onsOf(61).size(), 1, "the others play");
}

// everything at once on all sixteen: chords, ratchets, swing, probability and humanize. Nothing may be dropped,
// nothing left sounding
// The 32-step editor's window: two rows of 16, four pages over 128 steps. Every page and both rows are checked,
// and so is the thing that must never happen: a cell editing a step that is not under it
void testView() {
  reset();
  seq::setLength(128);
  checkEq(seq::viewPages(), 4, "128 steps: four pages");
  checkEq(SEQ_VIEW, 32, "32 steps at a time");
  checkEq(SEQ_COLS, 16, "two rows of 16");
  // every page, every cell
  for (int page = 0; page < 4; page++) {
    for (int cell = 0; cell < 32; cell++) {
      int want = page * 32 + cell;
      checkEq(seq::viewStep(page, cell), want,
              "page " + std::to_string(page + 1) + " cell " + std::to_string(cell + 1));
      checkEq(seq::viewCell(page, want), cell, "and back again");
      checkEq(seq::viewRow(cell), cell < 16 ? 0 : 1, "cell " + std::to_string(cell + 1) + " row");
      checkEq(seq::viewCol(cell), cell % 16, "cell " + std::to_string(cell + 1) + " column");
      checkEq(seq::viewPageOf(want), page, "step " + std::to_string(want + 1) + " is on its page");
    }
  }
  // the edges the brief names
  checkEq(seq::viewStep(0, 0), 0, "page 1 starts at step 1");
  checkEq(seq::viewStep(0, 15), 15, "page 1 row 1 ends at step 16");
  checkEq(seq::viewStep(0, 16), 16, "page 1 row 2 starts at step 17");
  checkEq(seq::viewStep(0, 31), 31, "page 1 ends at step 32");
  checkEq(seq::viewStep(1, 0), 32, "page 2 starts at step 33");
  checkEq(seq::viewStep(1, 15), 47, "step 48 is the end of page 2's first row");
  checkEq(seq::viewStep(1, 31), 63, "page 2 ends at step 64");
  checkEq(seq::viewStep(2, 0), 64, "page 3 starts at step 65");
  checkEq(seq::viewStep(3, 31), 127, "page 4 ends at step 128");
  // a cell on one page is never a step on another
  for (int page = 0; page < 4; page++) {
    for (int other = 0; other < 4; other++) {
      if (other == page) {
        continue;
      }
      for (int cell = 0; cell < 32; cell++) {
        check(seq::viewStep(page, cell) != seq::viewStep(other, cell), "pages never share a step");
      }
    }
  }
  checkEq(seq::viewCell(0, 32), -1, "step 33 is not on page 1");
  checkEq(seq::viewCell(1, 31), -1, "step 32 is not on page 2");
  // editing through the window: step 32 must not touch step 16, step 33 must not touch step 1
  seq::clearStep(0, 15);
  seq::setNote(0, seq::viewStep(0, 31), 60, 100);  // step 32
  check(seq::on(0, 31), "editing cell 32 of page 1 turns step 32 on");
  check(!seq::on(0, 15), "and step 16 is untouched");
  seq::setNote(0, seq::viewStep(1, 0), 61, 100);  // step 33
  check(seq::on(0, 32), "editing cell 1 of page 2 turns step 33 on");
  check(!seq::on(0, 0), "and step 1 is untouched");
  // a shorter pattern: the pages shrink, the cells past the end are empty
  seq::setLength(40);
  checkEq(seq::viewPages(), 2, "40 steps: two pages");
  checkEq(seq::viewStep(1, 7), 39, "40 steps: the last one is on page 2");
  checkEq(seq::viewStep(1, 8), -1, "40 steps: nothing past the end");
  seq::setLength(16);
  checkEq(seq::viewPages(), 1, "16 steps: one page");
  checkEq(seq::viewStep(0, 16), -1, "16 steps: the second row is empty");
}

void testStress() {
  reset();
  seq::setLength(64);
  seq::setSwing(55);
  for (int t = 0; t < SEQ_TRACKS; t++) {
    seq::setChannel(t, t);
    for (int s = 0; s < 64; s++) {
      seq::Step& st = seq::at(t, s);
      st.n = 3;  // a chord on every step of every track
      for (int i = 0; i < 3; i++) {
        st.note[i] = {(uint8_t)(36 + t + i * 4), 100, 80, (uint8_t)(i == 2 ? 70 : 100),
                      (uint8_t)(s % 4 == 3 ? 4 : 1), (uint8_t)(t % 3 ? 40 : 0), (uint8_t)(s % 8 ? 0 : 60),
                      (int8_t)(i == 1 ? 3 : 0)};
      }
    }
  }
  seq::play();
  run(64 * SEQ_STEP * 4);  // four times round
  seq::stop();
  checkEq((long)seq::dropped(), 0, "stress: nothing dropped on 16 tracks of chords and ratchets");
  checkEq(count(0x90), count(0x80), "stress: everything that started has ended");
  check(count(0x90) > 10000, "stress: a lot was played (" + std::to_string(count(0x90)) + ")");
  printf("  stress: %d note ons, %d offs, %u dropped, %u waiting at the most of %d\n", count(0x90), count(0x80),
         (unsigned)seq::dropped(), (unsigned)seq::peakPending(), seq::pendingRoom());
  check((long)seq::peakPending() < seq::pendingRoom(), "stress: the pending list had room to spare");
}

void testStepsAndTracks() {
  reset();
  seq::setLength(128);
  seq::setNote(0, 0, 60, 100);
  seq::setNote(0, 127, 72, 100);
  seq::play();
  run(128 * SEQ_STEP);  // one whole pattern
  checkEq((long)onsOf(60).size(), 1, "128 steps: step 1 plays once");
  checkEq((long)onsOf(72).size(), 1, "128 steps: step 128 plays once");
  checkEq((long)onsOf(72)[0], 127 * SEQ_STEP, "128 steps: step 128 is at its own time");
  // every track, each on its own channel
  reset();
  for (int t = 0; t < SEQ_TRACKS; t++) {
    seq::setChannel(t, t);
    seq::setNote(t, 0, 60 + t, 100);
  }
  seq::play();
  run(SEQ_STEP);
  checkEq(count(0x90), SEQ_TRACKS, "every track plays");
  for (int t = 0; t < SEQ_TRACKS; t++) {
    bool found = false;
    for (const Ev& e : got) {
      found |= e.a == (0x90 | t) && e.b == 60 + t;
    }
    check(found, "track " + std::to_string(t + 1) + " on its own channel");
  }
}

void testGateAndOffs() {
  reset();
  seq::at(0, 0).n = 1;
  seq::at(0, 0).note[0] = {60, 100, 50, 100, 1, 0, 0, 0};  // gate 50 %
  seq::play();
  run(SEQ_STEP);
  checkEq((long)onsOf(60).size(), 1, "gate: one note on");
  long off = -1;
  for (const Ev& e : got) {
    if ((e.a & 0xF0) == 0x80) {
      off = e.at;
    }
  }
  checkEq(off, SEQ_STEP / 2, "gate 50 %: it ends half a step later");
}

void testSwing() {
  reset();
  seq::setLength(4);
  for (int s = 0; s < 4; s++) {
    seq::setNote(0, s, 60 + s, 100);
  }
  seq::setSwing(100);  // the off-beats half a step late
  seq::play();
  run(4 * SEQ_STEP);
  checkEq((long)onsOf(60)[0], 0, "swing: step 1 on the beat");
  checkEq((long)onsOf(61)[0], SEQ_STEP + SEQ_STEP / 2, "swing 100: step 2 half a step late");
  checkEq((long)onsOf(62)[0], 2 * SEQ_STEP, "swing: step 3 on the beat");
  checkEq((long)onsOf(63)[0], 3 * SEQ_STEP + SEQ_STEP / 2, "swing 100: step 4 half a step late");
  reset();
  seq::setLength(4);
  seq::setNote(0, 1, 61, 100);
  seq::setSwing(50);
  seq::play();
  run(4 * SEQ_STEP);
  checkEq((long)onsOf(61)[0], SEQ_STEP + SEQ_STEP / 4, "swing 50: a quarter of a step late");
}

void testRatchet() {
  reset();
  seq::setLength(1);
  seq::at(0, 0).n = 1;
  seq::at(0, 0).note[0] = {60, 100, 80, 100, 4, 0, 0, 0};  // four hits
  seq::play();
  run(SEQ_STEP);
  std::vector<uint32_t> on = onsOf(60);
  checkEq((long)on.size(), 4, "ratchet 4: four hits");
  if (on.size() == 4) {
    checkEq((long)on[0], 0, "ratchet: the first on the step");
    checkEq((long)on[1], SEQ_STEP / 4, "ratchet: the second a quarter in");
    checkEq((long)on[2], SEQ_STEP / 2, "ratchet: the third halfway");
    checkEq((long)on[3], 3 * SEQ_STEP / 4, "ratchet: the fourth three quarters in");
  }
  checkEq(count(0x80), 4, "ratchet: each hit ends");
}

void testOffset() {
  reset();
  seq::setLength(2);
  seq::at(0, 1).n = 1;
  seq::at(0, 1).note[0] = {61, 100, 80, 100, 1, 0, 0, -4};  // four sub-ticks early
  seq::at(0, 0).n = 1;
  seq::at(0, 0).note[0] = {60, 100, 80, 100, 1, 0, 0, 6};  // six late
  seq::play();
  run(2 * SEQ_STEP);
  checkEq((long)onsOf(60)[0], 6, "offset +6: late");
  checkEq((long)onsOf(61)[0], SEQ_STEP - 4, "offset -4: early, before its step");
}

void testProbability() {
  reset();
  seq::setLength(1);
  seq::at(0, 0).n = 1;
  seq::at(0, 0).note[0] = {60, 100, 80, 50, 1, 0, 0, 0};  // half the time
  seq::play();
  run(200 * SEQ_STEP);
  long n = (long)onsOf(60).size();
  check(n > 60 && n < 140, "probability 50: about half of 200 (" + std::to_string(n) + ")");
  reset();
  seq::setLength(1);
  seq::setNote(0, 0, 60, 100);  // probability 100
  seq::play();
  run(50 * SEQ_STEP);
  checkEq((long)onsOf(60).size(), 50, "probability 100: every time");
}

void testHumanize() {
  reset();
  seq::setLength(1);
  seq::at(0, 0).n = 1;
  seq::at(0, 0).note[0] = {60, 80, 80, 100, 1, 100, 0, 0};
  seq::play();
  run(40 * SEQ_STEP);
  std::vector<uint32_t> on = onsOf(60);
  checkEq((long)on.size(), 40, "humanize: every step still plays");
  bool early = false, late = false, same = true;
  int vel = -1;
  bool velVaries = false;
  for (size_t i = 0; i < on.size(); i++) {
    long want = (long)i * SEQ_STEP;
    early |= (long)on[i] < want;
    late |= (long)on[i] > want;
    same &= (long)on[i] == want;
  }
  for (const Ev& e : got) {
    if ((e.a & 0xF0) == 0x90) {
      velVaries |= vel >= 0 && e.c != vel;
      vel = e.c;
    }
  }
  check(!same && (early || late), "humanize: the timing strays");
  check(velVaries, "humanize: the velocity strays");
  // and none of it before the pattern started
  for (uint32_t t : on) {
    check((int32_t)t >= 0, "humanize: nothing before the start");
  }
}

void testAccent() {
  reset();
  seq::setLength(1);
  seq::at(0, 0).n = 1;
  seq::at(0, 0).note[0] = {60, 64, 80, 100, 1, 0, 100, 0};  // accent all the way
  seq::play();
  run(SEQ_STEP);
  check(!got.empty() && got[0].c == 127, "accent 100: as loud as it goes");
  reset();
  seq::setLength(1);
  seq::setNote(0, 0, 60, 64);
  seq::play();
  run(SEQ_STEP);
  check(!got.empty() && got[0].c == 64, "no accent: the velocity as it is");
}

void testTransport() {
  reset();
  seq::setLength(4);
  seq::at(0, 0).n = 1;
  seq::at(0, 0).note[0] = {60, 100, 200, 100, 1, 0, 0, 0};  // a long one, still sounding at the stop
  seq::play();
  run(10);
  checkEq(count(0x80), 0, "stop: nothing has ended yet");
  seq::stop();
  checkEq(count(0x80), 1, "stop: what was sounding ends");
  check(!seq::playing(), "stop: it is not playing");
  checkEq(seq::step(), -1, "stop: no step");
  got.clear();
  seq::advance(SEQ_STEP * 4);
  checkEq((long)got.size(), 0, "stopped: nothing plays");
  seq::play();
  checkEq((long)seq::ticks(), 0, "play: from the top");
  run(1);
  checkEq(seq::step(), 0, "play: on step 1");
  run(SEQ_STEP);
  checkEq(seq::step(), 1, "play: on step 2 a step later");
  run(3 * SEQ_STEP);
  checkEq(seq::step(), 0, "play: round again to step 1");
  // continue goes on from where it stopped
  reset();
  seq::setLength(4);
  seq::setNote(0, 2, 62, 100);
  seq::play();
  run(SEQ_STEP + 5);
  seq::stop();
  uint32_t was = seq::ticks();
  seq::cont();
  check(seq::playing(), "continue: playing again");
  checkEq((long)seq::ticks(), (long)was, "continue: from where it stopped");
  got.clear();
  run(SEQ_STEP);
  checkEq((long)onsOf(62).size(), 1, "continue: the next step still plays");
}

void testTrackLengthMuteSolo() {
  reset();
  seq::setLength(16);
  seq::setNote(0, 0, 60, 100);
  seq::setTrackLength(0, 4);  // its own four steps
  seq::setNote(1, 0, 70, 100);
  seq::play();
  run(16 * SEQ_STEP);
  checkEq((long)onsOf(60).size(), 4, "track length 4: four times in sixteen steps");
  checkEq((long)onsOf(70).size(), 1, "the other track keeps the pattern's length");
  reset();
  seq::setLength(1);
  seq::setNote(0, 0, 60, 100);
  seq::setNote(1, 0, 70, 100);
  seq::setMute(0, true);
  seq::play();
  run(SEQ_STEP);
  checkEq((long)onsOf(60).size(), 0, "mute: it does not play");
  checkEq((long)onsOf(70).size(), 1, "mute: the others do");
  seq::stop();
  reset();
  seq::setLength(1);
  seq::setNote(0, 0, 60, 100);
  seq::setNote(1, 0, 70, 100);
  seq::setSolo(1, true);
  seq::play();
  run(SEQ_STEP);
  checkEq((long)onsOf(60).size(), 0, "solo: the others are quiet");
  checkEq((long)onsOf(70).size(), 1, "solo: the soloed one plays");
}

void testEditing() {
  reset();
  check(!seq::on(0, 5), "a new pattern's steps are off");
  check(seq::toggleStep(0, 5, 60, 100), "toggle: on");
  check(seq::on(0, 5), "toggle: it is on");
  check(!seq::toggleStep(0, 5, 60, 100), "toggle: off again");
  check(!seq::on(0, 5), "toggle: it is off");
  seq::setNote(0, 5, 60, 100);
  seq::addNote(0, 5, 64, 100);
  seq::addNote(0, 5, 67, 100);
  checkEq(seq::at(0, 5).n, 3, "a chord: three notes in one step");
  seq::play();
  run(5 * SEQ_STEP + 1);
  checkEq(count(0x90), 3, "a chord: all three play together");
  // the steps a pattern cannot have
  seq::setLength(999);
  checkEq(seq::pattern().steps, SEQ_STEPS, "length: no more than 128");
  seq::setLength(0);
  checkEq(seq::pattern().steps, 1, "length: at least 1");
}

void testNoDuplicates() {
  reset();
  seq::setLength(8);
  for (int s = 0; s < 8; s++) {
    seq::setNote(0, s, 60 + s, 100);
  }
  seq::play();
  run(8 * SEQ_STEP * 4);  // four times round
  checkEq(count(0x90), 8 * 4, "no duplicates: each step once a cycle");
  checkEq(count(0x80), 8 * 4, "no duplicates: one ending each");
  for (int s = 0; s < 8; s++) {
    std::vector<uint32_t> on = onsOf(60 + s);
    checkEq((long)on.size(), 4, "step " + std::to_string(s + 1) + " four times");
    for (size_t i = 0; i < on.size(); i++) {
      checkEq((long)on[i], (long)(i * 8 + s) * SEQ_STEP, "step " + std::to_string(s + 1) + " on its own beat");
    }
  }
}

void testLongRun() {  // it keeps its place over many cycles (the wrap does nothing to the times)
  reset();
  seq::setLength(16);
  seq::setNote(0, 0, 60, 100);
  seq::setNote(0, 15, 75, 100);
  seq::play();
  run(16 * SEQ_STEP * 64);
  checkEq((long)onsOf(60).size(), 64, "64 cycles: the first step every time");
  checkEq((long)onsOf(75).size(), 64, "64 cycles: the last step every time");
  std::vector<uint32_t> on = onsOf(75);
  checkEq((long)on[63], (long)(63 * 16 + 15) * SEQ_STEP, "64 cycles: still exactly on the beat");
  checkEq(count(0x90), count(0x80), "64 cycles: everything that started has ended");
}

}  // namespace


// ------------------------------------------------------------------------------------------- recording ---

// plays n sub-ticks and lets fn look at each one, so a note can be "played in" at an exact time
template <typename F>
void runWith(int n, F fn) {
  for (int i = 0; i < n; i++) {
    now = seq::ticks();
    fn((uint32_t)now);
    seq::advance(1);
  }
}

void testRecording() {
  // --------------------------------------------------------- a note lands on the step nearest to when it was played
  reset();
  seq::setLength(16);
  seq::setTracks(4);
  seq::setRecordTrack(1);
  seq::setOverdub(true);
  seq::arm(true);
  check(seq::armed(), "record: armed");
  seq::play();
  // exactly on step 3 (72 sub-ticks), a little late on step 6 (147), a little early on step 9 (214)
  runWith(16 * SEQ_STEP, [](uint32_t t) {
    if (t == 72) {
      seq::recordOn(60, 100);
    }
    if (t == 76) {
      seq::recordOff(60);
    }
    if (t == 147) {
      seq::recordOn(62, 90);
    }
    if (t == 159) {
      seq::recordOff(62);
    }
    if (t == 214) {
      seq::recordOn(64, 80);
    }
    if (t == 220) {
      seq::recordOff(64);
    }
  });
  seq::stop();
  checkEq(seq::recorded(), 3, "record: three notes went in");
  checkEq(seq::at(1, 3).n, 1, "record: on the beat went to step 3");
  checkEq(seq::at(1, 3).note[0].pitch, 60, "record: with its pitch");
  checkEq(seq::at(1, 3).note[0].vel, 100, "record: with its velocity");
  checkEq(seq::at(1, 3).note[0].offset, 0, "record: dead on the step");
  checkEq(seq::at(1, 6).n, 1, "record: a little late still went to step 6");
  checkEq(seq::at(1, 6).note[0].offset, 3, "record: and kept how late it was");
  checkEq(seq::at(1, 9).n, 1, "record: a little early went to step 9");
  checkEq(seq::at(1, 9).note[0].offset, -2, "record: and kept how early it was");
  // the gate comes from how long it was held, in musical time
  checkEq(seq::at(1, 6).note[0].gate, 12 * 100 / SEQ_STEP, "record: the gate is how long it was held");
  checkEq(seq::at(1, 3).note[0].prob, 100, "record: a recorded note always plays");

  // --------------------------------------------------------------- nothing is recorded unless armed and running
  reset();
  seq::setLength(16);
  seq::setRecordTrack(0);
  seq::recordOn(60, 100);  // not armed, not running
  checkEq(seq::recorded(), 0, "record: nothing while it is not armed");
  seq::arm(true);
  seq::recordOn(60, 100);  // armed, but stopped
  checkEq(seq::recorded(), 0, "record: nothing while it is stopped");
  seq::play();
  seq::recordOn(60, 100);
  checkEq(seq::recorded(), 1, "record: armed and running, it goes in");
  seq::arm(false);
  seq::recordOn(62, 100);
  checkEq(seq::recorded(), 1, "record: and nothing once it is un-armed");
  seq::stop();

  // ---------------------------------------------------------------------------------- it goes into the armed track
  reset();
  seq::setLength(16);
  seq::setTracks(8);
  seq::setRecordTrack(5);
  seq::arm(true);
  seq::play();
  seq::recordOn(67, 110);
  seq::recordOff(67);
  seq::stop();
  checkEq(seq::at(5, 0).n, 1, "record: into the armed track");
  for (int t = 0; t < SEQ_TRACKS; t++) {
    if (t != 5) {
      checkEq(seq::at(t, 0).n, 0, "record: and no other track");
    }
  }

  // ------------------------------------------------------------------------------------------ replace and overdub
  reset();
  seq::setLength(16);
  seq::setRecordTrack(0);
  seq::setNote(0, 4, 48, 70);  // something already there
  checkEq(seq::at(0, 4).n, 1, "record: a note was there to begin with");
  seq::setOverdub(true);
  seq::arm(true);
  seq::play();
  runWith(SEQ_STEP * 5, [](uint32_t t) {
    if (t == 4 * SEQ_STEP) {
      seq::recordOn(72, 100);
      seq::recordOff(72);
    }
  });
  seq::stop();
  checkEq(seq::at(0, 4).n, 2, "overdub: the new note is added to the old");
  checkEq(seq::at(0, 4).note[0].pitch, 48, "overdub: the old one is still first");

  seq::setOverdub(false);
  seq::arm(true);  // a new pass
  seq::play();
  runWith(SEQ_STEP * 5, [](uint32_t t) {
    if (t == 4 * SEQ_STEP) {
      seq::recordOn(76, 100);
      seq::recordOff(76);
    }
  });
  seq::stop();
  checkEq(seq::at(0, 4).n, 1, "replace: the step was emptied first");
  checkEq(seq::at(0, 4).note[0].pitch, 76, "replace: and holds only what was played");

  // in replace, a second note in the same step in the same pass is added, not another clear
  seq::arm(true);
  seq::play();
  runWith(SEQ_STEP * 5, [](uint32_t t) {
    if (t == 4 * SEQ_STEP) {
      seq::recordOn(60, 100);
      seq::recordOn(64, 100);
      seq::recordOff(60);
      seq::recordOff(64);
    }
  });
  seq::stop();
  checkEq(seq::at(0, 4).n, 2, "replace: a chord in one pass is one clear and two notes");

  // ------------------------------------------------------------------------------------------- a full step
  reset();
  seq::setLength(16);
  seq::setRecordTrack(0);
  seq::setOverdub(true);
  seq::arm(true);
  seq::play();
  for (int i = 0; i < 6; i++) {
    seq::recordOn(60 + i, 100);
  }
  seq::stop();
  checkEq(seq::at(0, 0).n, SEQ_NOTES, "record: a step takes no more than it holds");
  checkEq(seq::recorded(), SEQ_NOTES, "record: and only says what it wrote");

  // ------------------------------------------------------------------------------- a note that goes round the end
  reset();
  seq::setLength(4);
  seq::setRecordTrack(0);
  seq::setOverdub(true);
  seq::arm(true);
  seq::play();
  runWith(SEQ_STEP * 9, [](uint32_t t) {
    if (t == SEQ_STEP * 6 + 2) {  // the third step of the second time round
      seq::recordOn(60, 100);
      seq::recordOff(60);
    }
  });
  seq::stop();
  checkEq(seq::at(0, 2).n, 1, "record: the second time round writes into the same steps");
  checkEq(seq::at(0, 2).note[0].offset, 2, "record: with how late it was");

  // --------------------------------------------------------------------------------- what was recorded plays back
  reset();
  seq::setLength(4);
  seq::setRecordTrack(0);
  seq::setChannel(0, 3);
  seq::setOverdub(true);
  seq::arm(true);
  seq::play();
  runWith(SEQ_STEP * 4, [](uint32_t t) {
    if (t == SEQ_STEP) {
      seq::recordOn(65, 99);
    }
    if (t == SEQ_STEP + 12) {
      seq::recordOff(65);
    }
  });
  seq::arm(false);
  got.clear();
  run(SEQ_STEP * 4);  // the next time round
  seq::stop();
  std::vector<uint32_t> ons = onsOf(65);
  checkEq((long)ons.size(), 1, "record: it plays back once a cycle");
  bool rightCh = false;
  for (const Ev& e : got) {
    if (e.b == 65 && (e.a & 0xF0) == 0x90) {
      rightCh = (e.a & 0x0F) == 3;
    }
  }
  check(rightCh, "record: on the track's channel");
}

// -------------------------------------------------------------------------------------------- quantize ---

void testQuantize() {
  // ------------------------------------------------------------------------- everything pulled onto the 16ths
  reset();
  seq::setLength(16);
  seq::setTracks(2);
  seq::setNote(0, 2, 60, 100);
  seq::at(0, 2).note[0].offset = 7;  // late
  seq::setNote(0, 5, 62, 100);
  seq::at(0, 5).note[0].offset = -5;  // early
  seq::setNote(0, 9, 64, 100);
  seq::at(0, 9).note[0].offset = 0;  // already there
  int dropped = 0;
  int moved = seq::quantize(0, SEQ_GRID_16, 100, false, &dropped);
  checkEq(seq::at(0, 2).note[0].offset, 0, "quantize: the late one is on the step");
  checkEq(seq::at(0, 5).note[0].offset, 0, "quantize: the early one is on the step");
  checkEq(seq::at(0, 9).note[0].offset, 0, "quantize: the one already there did not move");
  checkEq(seq::at(0, 2).note[0].pitch, 60, "quantize: and keeps its pitch");
  checkEq(dropped, 0, "quantize: nothing was dropped");
  check(moved >= 2, "quantize: it says how many moved");

  // --------------------------------------------------------------------------------- half way, at strength 50
  reset();
  seq::setLength(16);
  seq::setNote(0, 3, 60, 100);
  seq::at(0, 3).note[0].offset = 8;
  seq::quantize(0, SEQ_GRID_16, 50, false, 0);
  checkEq(seq::at(0, 3).note[0].offset, 4, "quantize: at 50 it goes half way");

  // ----------------------------------------------------------------------------------------- nothing at zero
  reset();
  seq::setLength(16);
  seq::setNote(0, 3, 60, 100);
  seq::at(0, 3).note[0].offset = 8;
  seq::quantize(0, SEQ_GRID_16, 0, false, 0);
  checkEq(seq::at(0, 3).note[0].offset, 8, "quantize: at 0 nothing moves");
  checkEq(seq::at(0, 3).n, 1, "quantize: and the note is still there");

  // ----------------------------------------------------------- a coarser grid moves a note to another step
  reset();
  seq::setLength(16);
  seq::setNote(0, 3, 60, 100);  // a 16th that is not an 8th
  seq::quantize(0, SEQ_GRID_8, 100, false, 0);
  checkEq(seq::at(0, 3).n, 0, "quantize to 8ths: step 3 is empty");
  checkEq(seq::at(0, 4).n, 1, "quantize to 8ths: the note is on step 4");
  checkEq(seq::at(0, 4).note[0].pitch, 60, "quantize to 8ths: and it is the same note");

  reset();
  seq::setLength(16);
  seq::setNote(0, 5, 60, 100);
  seq::quantize(0, SEQ_GRID_4, 100, false, 0);
  checkEq(seq::at(0, 4).n, 1, "quantize to quarters: step 5 went back to step 4");

  // ------------------------------------------------------------------------- a note at the very end rounds round
  reset();
  seq::setLength(16);
  seq::setNote(0, 15, 60, 100);
  seq::at(0, 15).note[0].offset = 13;  // past the middle, so the nearest grid line is the end of the round
  seq::quantize(0, SEQ_GRID_16, 100, false, 0);
  checkEq(seq::at(0, 0).n, 1, "quantize: the last step's late note goes to step 1 of the next time round");
  checkEq(seq::at(0, 15).n, 0, "quantize: and leaves the last step");

  // ------------------------------------------------------------------------------------- a step that fills up
  reset();
  seq::setLength(16);
  for (int i = 0; i < SEQ_NOTES; i++) {
    seq::addNote(0, 4, 60 + i, 100);
  }
  seq::setNote(0, 5, 72, 100);  // a fifth note that quantizing would push into step 4
  seq::at(0, 5).note[0].offset = -13;
  dropped = 0;
  seq::quantize(0, SEQ_GRID_16, 100, false, &dropped);
  checkEq(seq::at(0, 4).n, SEQ_NOTES, "quantize: a full step stays full");
  checkEq(dropped, 1, "quantize: and the one with nowhere to go is counted");

  // --------------------------------------------------------------------------------------------- lengths
  reset();
  seq::setLength(16);
  seq::setNote(0, 2, 60, 100);
  seq::at(0, 2).note[0].gate = 55;  // a bit over half a step
  seq::quantize(0, SEQ_GRID_16, 100, true, 0);
  checkEq(seq::at(0, 2).note[0].gate, 100, "quantize lengths: the gate is a whole step");
  seq::setNote(0, 6, 62, 100);
  seq::at(0, 6).note[0].gate = 190;
  seq::quantize(0, SEQ_GRID_8, 100, true, 0);
  checkEq(seq::at(0, 6).note[0].gate, 200, "quantize lengths to 8ths: two steps");

  // -------------------------------------------------------------------------- one track only, and a bad one
  reset();
  seq::setLength(16);
  seq::setTracks(4);
  seq::setNote(0, 3, 60, 100);
  seq::setNote(1, 3, 61, 100);
  seq::quantize(0, SEQ_GRID_8, 100, false, 0);
  checkEq(seq::at(0, 4).n, 1, "quantize: the track asked for moved");
  checkEq(seq::at(1, 3).n, 1, "quantize: and no other track did");
  checkEq(seq::quantize(-1, SEQ_GRID_8, 100, false, 0), 0, "quantize: a track that isn't there does nothing");
  checkEq(seq::quantize(SEQ_TRACKS, SEQ_GRID_8, 100, false, 0), 0, "quantize: nor one past the end");
  checkEq(seq::quantize(0, 0, 100, false, 0), 0, "quantize: nor a grid of nothing");

  // ------------------------------------------------------------- what is quantized is what plays afterwards
  reset();
  seq::setLength(4);
  seq::setNote(0, 1, 60, 100);
  seq::at(0, 1).note[0].offset = 9;
  seq::quantize(0, SEQ_GRID_16, 100, false, 0);
  seq::play();
  run(SEQ_STEP * 4);
  seq::stop();
  std::vector<uint32_t> ons = onsOf(60);
  checkEq((long)ons.size(), 1, "quantize: it plays once");
  checkEq(ons.empty() ? -1 : (long)ons[0], SEQ_STEP, "quantize: exactly on its step, not nine ticks late");
}


// ------------------------------------------------------------------------------------------- song mode ---
// The engine's half of it: it says when the pattern has played its last step, and starts the next one from its own
// first step however long it is. What a song is lives in song.cpp

int cycles;
int swapTo;  // the length the hook gives the pattern when it is told (0: it leaves it alone)

void onCycle() {
  cycles++;
  if (swapTo) {
    seq::setLength(swapTo);
    seq::setNote(0, 0, 90, 100);  // a note on the new pattern's first step, so it can be heard
    seq::rebase();
    swapTo = 0;
  }
}

void testSong() {
  // ------------------------------------------------------------------------- it is told once a time round
  reset();
  cycles = 0, swapTo = 0;
  seq::setCycle(onCycle);
  seq::setLength(4);
  seq::setNote(0, 0, 60, 100);
  seq::play();
  run(SEQ_STEP * 4 * 3);
  seq::stop();
  check(cycles >= 2 && cycles <= 4, "song: told once each time the pattern goes round");
  checkEq((long)onsOf(60).size(), 3, "song: and the pattern played three times");

  // ------------------------------------------------- a pattern of another length starts at its own first step
  reset();
  cycles = 0;
  seq::setCycle(onCycle);
  seq::setLength(4);
  seq::setNote(0, 0, 60, 100);
  seq::setNote(0, 3, 61, 100);
  swapTo = 7;  // the next time round, a 7-step pattern goes in
  seq::play();
  run(SEQ_STEP * 4);     // the first pattern, once
  run(SEQ_STEP * 7 * 2);  // the second, twice
  seq::stop();
  checkEq((long)onsOf(60).size(), 1, "song: the first pattern played once");
  std::vector<uint32_t> nine = onsOf(90);
  checkEq((long)nine.size(), 2, "song: the one put in its place played twice");
  checkEq(nine.empty() ? -1 : (long)nine[0], SEQ_STEP * 4, "song: starting exactly where the first one ended");
  checkEq(nine.size() < 2 ? -1 : (long)nine[1], SEQ_STEP * 11, "song: and going round at its own length, not the old one");

  // -------------------------------------------------------------------- the step shown follows the new pattern
  reset();
  cycles = 0;
  seq::setCycle(onCycle);
  seq::setLength(4);
  swapTo = 7;
  seq::play();
  run(SEQ_STEP * 4 + 1);
  checkEq(seq::step(), 0, "song: the playhead is on the new pattern's first step");
  run(SEQ_STEP * 6);
  checkEq(seq::step(), 6, "song: and counts to its own last one");
  run(SEQ_STEP);
  checkEq(seq::step(), 0, "song: then round again");
  seq::stop();
  seq::setCycle(0);

  // ------------------------------------------------------------------------- without a hook, nothing changes
  reset();
  seq::setLength(4);
  seq::setNote(0, 0, 60, 100);
  seq::play();
  run(SEQ_STEP * 8);
  seq::stop();
  checkEq((long)onsOf(60).size(), 2, "song: with no hook the pattern just goes round");
}

int main() {
  seq::useScratch(malloc(seq::scratchBytes()));  // (the board gives this from its PSRAM)
  seq::setOut(sink);
  seq::clear();
  testSixteenTracks();
  testStepsAndTracks();
  testGateAndOffs();
  testSwing();
  testRatchet();
  testOffset();
  testProbability();
  testHumanize();
  testAccent();
  testTransport();
  testTrackLengthMuteSolo();
  testEditing();
  testNoDuplicates();
  testLongRun();
  testView();
  testRecording();
  testQuantize();
  testSong();
  testStress();
  if (fails) {
    printf("seq: %d FAILED\n", fails);
    return 1;
  }
  printf("seq: ok\n");
  return 0;
}
