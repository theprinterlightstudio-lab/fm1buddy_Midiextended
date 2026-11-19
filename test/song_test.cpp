// song: the arrangement — the library's patterns in an order, each played a number of times.
//
//   c++ -std=gnu++17 -Wall -DSEQ_NO_MIDISYNC -I src/app -o /tmp/song_test test/song_test.cpp src/app/song.cpp src/app/seqstore.cpp
//   src/app/seq.cpp && /tmp/song_test
//
// The card is a folder in /tmp, and the clock is driven by hand, so what plays and exactly when is known. The
// reading thread is not started: the test calls song::service() itself, so nothing here depends on timing.
#include "song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#include <string>
#include <vector>

#include "seqstore.h"

static std::string root = "/tmp/fm1b-song-test";
// the card is a folder of its own: the test makes it, so a clean machine needs nothing set up first
static void makeRoot() {
  if (!root.empty()) {
#ifdef _MSC_VER
    _mkdir(root.c_str());
#else
    mkdir(root.c_str(), 0777);
#endif
  }
}

namespace hal {
const char* sd() {
  return root.empty() ? 0 : root.c_str();
}
void log(const char*, ...) {}
}  // namespace hal

static int fails;
static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    fails++;
  }
}
static void checkEq(long a, long b, const char* what) {
  if (a != b) {
    printf("FAIL: %s (%ld, wanted %ld)\n", what, a, b);
    fails++;
  }
}

// what went out, and when
struct Ev {
  uint32_t at;
  uint8_t a, b, c;
};
static std::vector<Ev> got;
static uint32_t now;
static void sink(const uint8_t* m, int n) {
  (void)n;
  got.push_back({now, m[0], m[1], m[2]});
}

// plays n sub-ticks, reading the card between them as the board's own thread would
static void run(int n) {
  for (int i = 0; i < n; i++) {
    now = seq::ticks();
    seq::advance(1);
    song::service();
  }
}

static std::vector<uint32_t> onsOf(int p) {
  std::vector<uint32_t> v;
  for (const Ev& e : got) {
    if ((e.a & 0xF0) == 0x90 && e.b == p) {
      v.push_back(e.at);
    }
  }
  return v;
}

// a pattern of `steps` steps with one note of pitch `pitch` on its first step, saved into a slot
static void makePattern(int bank, int slot, int steps, int pitch, const char* name) {
  seq::clear();
  seq::setLength(steps);
  seq::setNote(0, 0, pitch, 100);
  snprintf(seq::pattern().name, SEQ_NAME, "%s", name);
  const char* why = "";
  std::string p = seqstore::slotPath(bank, slot);
  if (!seqstore::save(p.c_str(), seq::pattern(), &why)) {
    printf("FAIL: could not write %s (%s)\n", p.c_str(), why);
    fails++;
  }
}

int main() {
  makeRoot();
  seq::usePattern(calloc(1, seq::patternBytes()));
  seq::useScratch(calloc(1, seq::scratchBytes()));
  seq::setOut(sink);
  seq::clear();
  seqstore::makeDir();

  // three patterns on the card: 4 steps / C, 8 steps / D, 2 steps / E
  makePattern(0, 0, 4, 60, "One");
  makePattern(0, 1, 8, 62, "Two");
  makePattern(0, 2, 2, 64, "Three");

  song::begin(calloc(1, seq::patternBytes()), false);  // no thread: this test does the reading itself
  check(song::ready(), "song: there is room for it");

  // ------------------------------------------------------------------------------------- building a song ---
  song::clear();
  checkEq(song::length(), 0, "song: a new song is empty");
  check(song::add(0, 0, 2), "song: an entry goes in");
  check(song::add(0, 1, 1), "song: and another");
  check(song::add(0, 2, 3), "song: and a third");
  checkEq(song::length(), 3, "song: three entries");
  checkEq(song::get().step[2].repeats, 3, "song: with their repeats");
  check(!song::add(9, 0, 1), "song: a bank that isn't there is refused");
  check(!song::add(0, 99, 1), "song: nor a slot that isn't there");
  song::insert(1, 0, 2, 1);
  checkEq(song::get().step[1].slot, 2, "song: an entry can go in the middle");
  song::remove(1);
  checkEq(song::get().step[1].slot, 1, "song: and come out again");
  checkEq(song::length(), 3, "song: leaving the rest");
  song::move(0, 2);
  checkEq(song::get().step[2].slot, 0, "song: an entry can be moved down");
  song::move(2, -2);
  checkEq(song::get().step[0].slot, 0, "song: and back up");
  song::setRepeats(0, 150);
  checkEq(song::get().step[0].repeats, 99, "song: repeats are kept sane");
  song::setRepeats(0, 2);

  // ---------------------------------------------------------------------------------- the song on the card ---
  song::setLoop(false);
  const char* why = "";
  check(song::save(&why), "song: it saves");
  Song keep = song::get();
  song::clear();
  checkEq(song::length(), 0, "song: and the one in memory can be thrown away");
  check(song::load(&why), "song: it loads");
  checkEq(song::length(), 3, "song: with all three entries");
  check(!memcmp(&keep.step, &song::get().step, sizeof keep.step), "song: exactly as they were");
  checkEq(song::loop(), false, "song: and the loop setting too");
  {
    // a damaged song is refused and what is in memory is left alone
    std::string p = root + SONG_PATH;
    FILE* f = fopen(p.c_str(), "r+b");
    check(f != 0, "song: the song file is there");
    fseek(f, 12, SEEK_SET);
    fputc('X', f);
    fclose(f);
    check(!song::load(&why), "song: a damaged song is refused");
    check(!strcmp(why, "the song is damaged"), "song: and says so");
    checkEq(song::length(), 3, "song: and the song in memory is untouched");
    check(song::save(&why), "song: writing it again mends the file");
    check(song::load(&why), "song: and it loads");
  }

  // ------------------------------------------------------------------------------------ playing a song ---
  // One twice (4 steps), Two once (8 steps), Three three times (2 steps), then it stops
  got.clear();
  song::setLoop(false);
  check(song::play(0, &why), "song: it plays");
  checkEq(song::at(), 0, "song: starting at the first entry");
  checkEq(song::pass(), 1, "song: on its first time round");
  check(song::playing(), "song: and the transport is running");

  run(26 * 24);  // 2x4 + 1x8 + 3x2 = 22 steps of pattern, and a little past the end
  std::vector<uint32_t> one = onsOf(60), two = onsOf(62), three = onsOf(64);
  checkEq((long)one.size(), 2, "song: the first pattern played twice");
  checkEq((long)two.size(), 1, "song: the second once");
  checkEq((long)three.size(), 3, "song: the third three times");
  checkEq(one.empty() ? -1 : (long)one[0], 0, "song: the first started at the top");
  checkEq(one.size() < 2 ? -1 : (long)one[1], 4 * 24, "song: and went round at its own length");
  checkEq(two.empty() ? -1 : (long)two[0], 8 * 24, "song: the second came in when the first had had its two");
  checkEq(three.empty() ? -1 : (long)three[0], 16 * 24, "song: and the third when the second had had its one");
  checkEq(three.size() < 3 ? -1 : (long)three[2], 20 * 24, "song: each of its three on its own two steps");
  check(!song::playing(), "song: and at the end it stops");
  checkEq(song::at(), -1, "song: with nothing playing");
  {
    int ons = 0, offs = 0;
    for (const Ev& e : got) {
      ons += (e.a & 0xF0) == 0x90;
      offs += (e.a & 0xF0) == 0x80;
    }
    checkEq(ons, offs, "song: everything that started has ended");
  }

  // ------------------------------------------------------------------------------------------ going round ---
  got.clear();
  song::setLoop(true);
  check(song::play(0, &why), "song: a looping song plays");
  run(30 * 24);  // past the end of the song
  check(song::playing(), "song: a looping song does not stop at the end");
  checkEq((long)onsOf(60).size(), 4, "song: and the first pattern comes round again");
  song::stop();
  check(!song::playing(), "song: it can be stopped");

  // ---------------------------------------------------------------------------- starting from the middle ---
  got.clear();
  song::setLoop(false);
  check(song::play(1, &why), "song: it can start part way in");
  checkEq(song::at(), 1, "song: at the entry asked for");
  run(10 * 24);
  checkEq((long)onsOf(60).size(), 0, "song: the entries before it are not played");
  checkEq((long)onsOf(62).size(), 1, "song: the one asked for is");
  song::stop();

  // ----------------------------------------------------------------------------------- a song that can't ---
  song::clear();
  check(!song::play(0, &why), "song: an empty song does not play");
  check(!strcmp(why, "the song is empty"), "song: and says so");
  song::add(3, 15, 1);  // a slot with nothing in it
  check(!song::play(0, &why), "song: a song whose first pattern isn't there does not play");
  check(!song::playing(), "song: and nothing is left running");

  // an entry in the middle whose slot is empty: the song keeps going on the pattern it has
  song::clear();
  song::setLoop(false);
  song::add(0, 0, 1);   // One, 4 steps
  song::add(3, 14, 1);  // nothing there
  got.clear();
  check(song::play(0, &why), "song: it starts on the first pattern");
  run(12 * 24);
  check((long)onsOf(60).size() >= 2, "song: a missing pattern leaves the one playing in place");
  song::stop();

  // --------------------------------------------------------------------------------------- with no card ---
  root = "";
  song::clear();
  song::add(0, 0, 1);
  check(!song::play(0, &why), "no card: a song does not play");
  check(!song::save(&why) && !strcmp(why, "no SD card"), "no card: a song is not saved");
  check(!song::load(&why) && !strcmp(why, "no SD card"), "no card: and none is read");

  if (fails) {
    printf("song: %d FAILED\n", fails);
    return 1;
  }
  printf("song: ok\n");
  return 0;
}
