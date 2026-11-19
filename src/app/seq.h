// The step sequencer's engine (FM Buddy 2.0): one pattern of up to 128 steps and 16 tracks, played on its own clock.
//
// It knows nothing about the screen. The page (page_seq.cpp) reads it and edits it; nothing here calls LVGL, the SD
// card or anything that blocks. Time comes in as sub-ticks (SEQ_PPQ a quarter note) from one driver: on the board a
// thread of its own (seq::begin), in a test the test itself. What it plays goes out through one function the app
// sets (seq::setOut), which hands it to the MIDI engine as the touch controller's own notes: no second MIDI engine.
//
// What a step holds, and how it is played, follows the SFX MIDI Bridge sequencer (see
// docs/FM_BUDDY_SEQUENCER_ARCHITECTURE.md): notes with velocity, gate, probability, micro-timing offset, ratchet
// repeats, humanize and accent; swing by delaying the off-beat steps; per-track lengths. The storage, the 32-step
// four-page editor, the pattern library and song mode are FM Buddy's own.
#pragma once

#include <stdint.h>

namespace seq {

#define SEQ_TRACKS 16  // tracks a pattern has (1-16, each on any MIDI channel; several may share one)
#define SEQ_STEPS 128  // steps a track has at most
#define SEQ_NOTES 4    // notes a step holds at most (a chord)
#define SEQ_PPQ 96     // sub-ticks in a quarter note
#define SEQ_STEP 24    // sub-ticks in a step (a 16th)
#define SEQ_NAME 16    // a pattern's name, with its ending 0
#define SEQ_TNAME 8    // a track's

// one note of a step. gate, probability, repeat, humanize and accent are per note, as SMB has them
struct Note {
  uint8_t pitch;  // 0-127
  uint8_t vel;    // 1-127
  uint8_t gate;   // how long it sounds, % of a step (1-200; 0: the pattern's)
  uint8_t prob;   // 0-100 it plays at all (0 or 100: always, see below)
  uint8_t repeat; // 1-8: the ratchet, that many evenly through the step
  uint8_t human;  // 0-100: how much its timing and velocity stray
  uint8_t accent; // 0-100: louder
  int8_t offset;  // sub-ticks early (-) or late (+), within a step
};

struct Step {
  uint8_t n;  // how many of note[] are used (0: the step is off)
  Note note[SEQ_NOTES];
};

// A track: its own channel, its own length, its own steps. Its number in the pattern (0-15) is its id, and that
// never changes. Changing one track changes nothing about any other
struct Track {
  char name[SEQ_TNAME];  // "T1" .. "T16" until it is given one
  uint8_t ch;     // MIDI channel 0-15 (several tracks may share one: not checked)
  uint8_t len;    // its own length, 1-128 steps (0: the pattern's)
  uint8_t off;    // the track is switched off: it plays nothing and records nothing
  uint8_t mute;
  uint8_t solo;
  uint8_t base;   // the pitch a step is given when it is switched on by hand (60 = C4)
  uint8_t spare;
  Step step[SEQ_STEPS];
};

struct Pattern {
  char name[SEQ_NAME];
  uint16_t bpm;    // 30-300
  uint8_t steps;   // the pattern's length, 1-128
  uint8_t swing;   // 0-100: how far the off-beat steps are pushed back (100 = half a step)
  uint8_t gate;    // the default gate, % of a step
  uint8_t tracks;  // how many of track[] are used, 1-16
  Track track[SEQ_TRACKS];
};

// what plays a MIDI message (3 bytes, or 2 for a 2-byte status): set by the app at startup
typedef void (*Out)(const uint8_t* msg, int n);
void setOut(Out fn);

// the pattern in memory (about 68 KB: the board keeps it in the PSRAM). Edited through the calls below while it
// plays; read freely for drawing. Nothing here asks for memory once it is going
Pattern& pattern();
unsigned long patternBytes();
// where the pattern is kept: the board hands the engine a block of PSRAM before anything starts (seqdrive.cpp).
// Without one the engine takes it from the ordinary heap the first time it is needed
void usePattern(void* mem);
bool ready();  // there was room for it: without that the engine does nothing at all
void clear();              // an empty pattern, default tempo, 16 steps
void begin();

// ---------------------------------------------------------------------------------------------- transport ---
void play();     // from the top (local/UI request)
void stop();     // and every note it holds ends (local/UI request)
void cont();     // on from where it stopped (local/UI request)
void toggle();
void transportPlay();   // apply Start from clock/transport authority
void transportStop();   // apply Stop from clock/transport authority
void transportCont();   // apply Continue from clock/transport authority
// Seek to a step within the current pattern (0-based, 16th-note steps). Releases sounding notes,
// does not modify pattern data, and repositions transport so playback can continue from that step.
// Returns false if the position is unsupported (e.g. no pattern ready). Safe for SPP.
bool seekToPatternStep(int stepInPattern);
// Let go of every note the sequencer is holding, and forget the ones it had lined up, without touching the
// transport: it stays playing (or stopped) and keeps its place. For a clock that has gone, where the engine must
// fall silent but must not restart or lose its position. Nothing is sent for a note that was never played, and a
// second call sends nothing, so it cannot end a note twice
void releaseSounding();
bool playing();
int step();           // the step it is on (0 .. steps-1), -1 stopped
uint32_t ticks();     // sub-ticks since it started playing (the song position)
void setBpm(int bpm);
int bpm();

// ------------------------------------------------------------------------------------------------ editing ---
// a step's notes. track 0-7, step 0-127 (the pattern's own index, never the screen's)
Step& at(int track, int step);
bool on(int track, int step);                      // it has notes
void setNote(int track, int step, int pitch, int vel);  // one note (a step that was off comes on)
void addNote(int track, int step, int pitch, int vel);  // one more (a chord), up to SEQ_NOTES
bool toggleStep(int track, int step, int pitch, int vel);  // off -> one note, on -> off. True if it is on now
void clearStep(int track, int step);
void setLength(int steps);        // the pattern's, 1-128
void setGate(int percent);        // the default gate, % of a step
void setTracks(int n);            // how many tracks the pattern uses, 1-16
void setTrackLength(int track, int len);  // 0: follow the pattern's
void setSwing(int percent);
void setChannel(int track, int ch);  // 0-15. Nothing else changes, on this track or any other
int channel(int track);
void setTrackName(int track, const char* name);
const char* trackName(int track);
void setBase(int track, int pitch);   // the pitch a step gets when switched on
int base(int track);
void setTrackOn(int track, bool on);   // off: it plays nothing
bool trackOn(int track);
void setMute(int track, bool on);
void setSolo(int track, bool on);
bool trackPlays(int track);       // on, not muted, and soloed if anything is

// --------------------------------------------------------------------- the editor's window on the pattern ---
// The 32-step editor shows two rows of 16 steps, a page at a time, and a 128-step pattern needs four pages. These
// four are the ONE place a place on the screen becomes a step of the pattern and back: the page's drawing and its
// taps both go through them, so a cell can never edit another step than the one under it. They are pure: the tests
// check every page and every row (test/seq_test.cpp)
#define SEQ_VIEW 32  // steps shown at once
#define SEQ_COLS 16  // in each of its two rows

int viewPages();                 // how many pages this pattern's length needs (1-4)
int viewStep(int page, int cell);  // the pattern step cell 0-31 of this page shows (-1: past the pattern's end)
int viewCell(int page, int step);  // where that step is on this page (0-31), or -1 if it is on another page
int viewPageOf(int step);          // the page a step is on
int viewRow(int cell);             // 0 the top row, 1 the bottom one
int viewCol(int cell);             // 0-15 across

// ------------------------------------------------------------------------------------- the clock's driver ---
// time passes: n sub-ticks. Every note that falls due in them is played. The engine's thread calls this on the
// board; a test calls it itself, so what it plays can be checked exactly
// ------------------------------------------------------------------------------------------- recording ---
// Notes played in go into the armed track at the step nearest the playhead, with how far off they were kept as the
// note's offset: nothing is rounded away while it is being played. Quantize comes after, and can be undone by
// quantizing again at another strength only in so far as the offsets allow - record first, quantize second.
#define SEQ_GRID_16 SEQ_STEP        // the grids quantize() takes, in sub-ticks
#define SEQ_GRID_8 (SEQ_STEP * 2)
#define SEQ_GRID_4 (SEQ_STEP * 4)
#define SEQ_GRID_32 (SEQ_STEP / 2)
#define SEQ_GRID_8T (SEQ_PPQ / 3)   // a triplet eighth
#define SEQ_GRID_16T (SEQ_PPQ / 6)

void arm(bool on);                 // record the next notes that come in (it also needs the transport running)
bool armed();
void setRecordTrack(int t);        // which track they go into (the page sets it to the one being edited)
int recordTrack();
void setOverdub(bool on);          // true: notes are added to what is there. false: the first note recorded into a
bool overdub();                    // step in this pass clears that step first
// a note played in. Ignored unless armed and running. recordOff sets the note's gate from how long it was held
void recordOn(int pitch, int vel);
void recordOff(int pitch);
uint32_t recorded();               // notes written since arm(true)

// Move the track's notes onto a grid. strength 0-100 (100: exactly on it). lengths: the gates are snapped too.
// A note whose new step is full has nowhere to go and is dropped; dropped (if given) says how many.
// Returns how many notes moved.
int quantize(int track, int grid, int strength, bool lengths, int* dropped = 0);
// Quantizing needs room to lift a track's notes out before putting them back. The board hands it a block of PSRAM
// at startup, as it does the pattern; without one, quantize() does nothing and returns 0
unsigned long scratchBytes();
void useScratch(void* mem);

// ---------------------------------------------------------------------------------------------- song mode ---
// The engine plays one pattern. Song mode (song.h) swaps another in at the moment this one has played its last
// step: setCycle is told just before the next step 0 is worked out, and rebase() makes the pattern that is in now
// start at its own first step. Nothing here reads the SD card or knows what a song is.
typedef void (*Cycle)();
void setCycle(Cycle fn);
void rebase();

void advance(int sub = 1);
// what random choices (probability, humanize) start from: a test sets it so a run repeats
void seed(uint32_t s);
// notes that could not be fitted into the pending list (it is bounded: nothing is allocated while it plays). 0 is
// what a healthy run gives; a stress test watches it
uint32_t dropped();
// the most events that ever waited at once, and how many it has room for: a stress test's headroom
uint32_t peakPending();
int pendingRoom();

}  // namespace seq
