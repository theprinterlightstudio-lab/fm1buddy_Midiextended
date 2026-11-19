// FM Buddy 2.0: song mode — the patterns of the library (seqstore.h), in an order, each played a number of times.
//
// The engine plays one pattern and knows nothing about songs. This hangs on the hook it offers (seq::setCycle): at
// the moment a pattern has played its last step, the next pattern is put in and seq::rebase() makes it start at its
// own first step, whatever its length. There is no gap and no restart: the clock never stops.
//
// Two pattern buffers: the one playing, and a spare the next pattern is read into *before* it is wanted. The card
// is read on a thread of its own, never on the clock's, so a slow card can never hold a note up. If the next one is
// not ready when the pattern goes round, the pattern that is playing plays again and the song tries once more: it
// never stops and never plays something half read.
#pragma once

#include <stdint.h>

#include "seq.h"

#define SONG_STEPS 64  // entries a song has at most
#define SONG_NAME 16
#define SONG_PATH SEQ_DIR "/song.fms"  // (SONG_FILE is taken: sounds.h)

struct SongStep {
  uint8_t bank;     // 0-3 (A-D)
  uint8_t slot;     // 0-15
  uint8_t repeats;  // 1-99 times round before the next one
  uint8_t spare;
};

struct Song {
  char name[SONG_NAME];
  uint8_t n;     // how many of step[] are used
  uint8_t loop;  // at the end, start again
  uint8_t spare[2];
  SongStep step[SONG_STEPS];
};

namespace song {

Song& get();
void clear();
bool add(int bank, int slot, int repeats);  // at the end. false if the song is full
bool insert(int at, int bank, int slot, int repeats);
void remove(int at);
void move(int at, int by);  // up or down the list
void setRepeats(int at, int n);
void setLoop(bool on);
bool loop();
int length();

// the spare pattern buffer (PSRAM) and the thread that fills it. Called once at startup. withThread false: the
// caller does the reading itself with service() (the tests do this, so nothing depends on timing)
void begin(void* sparePattern, bool withThread = true);
bool ready();
// read the next entry's pattern into the spare buffer if one is wanted. Never called from the clock's thread
void service();

// Start the song from entry `from`: its pattern is read in (this blocks, on the caller's thread, once), and then
// the transport runs. false and why if the pattern can't be read
bool play(int from, const char** why);
void stop();
bool playing();
int at();    // the entry being played (-1: the song is not playing)
int pass();  // which time round of it (1 upwards)

// the song itself, on the card: written whole and renamed, as a pattern is
bool save(const char** why);
bool load(const char** why);

}  // namespace song
