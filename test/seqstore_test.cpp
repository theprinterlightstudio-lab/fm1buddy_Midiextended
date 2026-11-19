// seqstore: the sequencer's patterns as bytes and as files on the card.
//
//   c++ -std=gnu++17 -Wall -DSEQ_NO_MIDISYNC -I src/app -o /tmp/seqstore_test test/seqstore_test.cpp src/app/seqstore.cpp src/app/seq.cpp
//   /tmp/seqstore_test
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
#include <direct.h>
#include <io.h>
#include <process.h>
#define getpid _getpid
#define rmdir _rmdir
#define unlink _unlink
#define access _access
#ifndef F_OK
#define F_OK 0
#endif
#else
#include <unistd.h>
#endif

#include <sys/stat.h>

#include <string>
#include <vector>

#include "seqstore.h"

// what seqstore wants of the board: where the card is. The test puts it in /tmp
static std::string root = "/tmp/fm1b-card-test";
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

static int checks;
#define CHECK(c, what)                                   \
  do {                                                   \
    checks++;                                            \
    if (!(c)) {                                          \
      printf("seqstore: FAILED %s (line %d)\n", what, __LINE__); \
      return 1;                                          \
    }                                                    \
  } while (0)

// a pattern with something in every corner of it
static void fill(seq::Pattern& p) {
  memset(&p, 0, sizeof p);
  snprintf(p.name, SEQ_NAME, "Deep House");
  p.bpm = 124;
  p.steps = 64;
  p.swing = 55;
  p.gate = 70;
  p.tracks = SEQ_TRACKS;
  for (int t = 0; t < SEQ_TRACKS; t++) {
    seq::Track& tr = p.track[t];
    snprintf(tr.name, SEQ_TNAME, "Tk%d", t + 1);
    tr.ch = (uint8_t)((SEQ_TRACKS - 1 - t) & 15);
    tr.len = (uint8_t)(t == 3 ? 7 : 0);
    tr.off = t == 5;
    tr.mute = t == 6;
    tr.solo = t == 7;
    tr.base = (uint8_t)(36 + t);
    for (int s = t; s < SEQ_STEPS; s += 8) {
      seq::Step& st = tr.step[s];
      st.n = (uint8_t)(1 + (s % SEQ_NOTES));
      for (int k = 0; k < st.n; k++) {
        st.note[k].pitch = (uint8_t)((36 + t + k * 4) & 127);
        st.note[k].vel = (uint8_t)(1 + ((s * 7 + k) % 126));
        st.note[k].gate = (uint8_t)(10 + (s % 190));
        st.note[k].prob = (uint8_t)(s % 101);
        st.note[k].repeat = (uint8_t)(1 + (s % 8));
        st.note[k].human = (uint8_t)(s % 101);
        st.note[k].accent = (uint8_t)((s * 3) % 101);
        st.note[k].offset = (int8_t)((s % 23) - 11);
      }
    }
  }
}

static bool same(const seq::Pattern& a, const seq::Pattern& b) {
  if (strcmp(a.name, b.name) || a.bpm != b.bpm || a.steps != b.steps || a.swing != b.swing || a.gate != b.gate ||
      a.tracks != b.tracks) {
    return false;
  }
  for (int t = 0; t < SEQ_TRACKS; t++) {
    const seq::Track &x = a.track[t], &y = b.track[t];
    if (strcmp(x.name, y.name) || x.ch != y.ch || x.len != y.len || x.off != y.off || x.mute != y.mute ||
        x.solo != y.solo || x.base != y.base) {
      return false;
    }
    for (int s = 0; s < SEQ_STEPS; s++) {
      if (x.step[s].n != y.step[s].n) {
        return false;
      }
      for (int k = 0; k < x.step[s].n; k++) {
        if (memcmp(&x.step[s].note[k], &y.step[s].note[k], sizeof(seq::Note))) {
          return false;
        }
      }
    }
  }
  return true;
}

int main() {
  makeRoot();
  const char* why = 0;

  // -------------------------------------------------------------------------------- there and back again ---
  seq::Pattern p, back;
  fill(p);
  std::vector<uint8_t> bytes;
  seqstore::pack(p, bytes);
  CHECK(seqstore::unpack(bytes.data(), bytes.size(), back, &why), "a packed pattern unpacks");
  CHECK(same(p, back), "everything comes back as it went in");

  // a pattern with notes on a sixteenth of its steps is far smaller than the 68 KB it takes in memory
  CHECK(bytes.size() < 8192, "the file is small");
  printf("seqstore: a 16-track pattern with %u steps written is %u bytes\n", (unsigned)(SEQ_TRACKS * SEQ_STEPS / 8),
         (unsigned)bytes.size());

  // --------------------------------------------------------------------------------------- a new pattern ---
  seq::Pattern empty, emptyBack;
  memset(&empty, 0, sizeof empty);
  snprintf(empty.name, SEQ_NAME, "Pattern");
  empty.bpm = 120;
  empty.steps = 32;
  empty.gate = 80;
  empty.tracks = SEQ_TRACKS;
  for (int t = 0; t < SEQ_TRACKS; t++) {
    snprintf(empty.track[t].name, SEQ_TNAME, "T%d", t + 1);
    empty.track[t].ch = (uint8_t)t;
    empty.track[t].base = 60;
  }
  seqstore::pack(empty, bytes);
  CHECK(bytes.size() < 100, "an empty pattern is a handful of bytes");
  CHECK(seqstore::unpack(bytes.data(), bytes.size(), emptyBack, &why), "an empty pattern unpacks");
  CHECK(same(empty, emptyBack), "an empty pattern comes back as it went in");

  // ------------------------------------------------------------------------------------------- the name ---
  fill(p);
  seqstore::pack(p, bytes);
  char name[SEQ_NAME];
  CHECK(seqstore::peekName(bytes.data(), bytes.size(), name), "the name can be read from the front");
  CHECK(!strcmp(name, "Deep House"), "the name is right");
  CHECK(seqstore::peekName(bytes.data(), 28, name), "the name is in the first bytes, not the whole file");

  // --------------------------------------------------------------------------------- bytes that are wrong ---
  seq::Pattern wreck;
  memset(&wreck, 0x5a, sizeof wreck);
  seq::Pattern keep = wreck;
  std::vector<uint8_t> bad = bytes;
  bad[40] ^= 0x01;
  CHECK(!seqstore::unpack(bad.data(), bad.size(), wreck, &why), "a changed byte is caught");
  CHECK(!strcmp(why, "the file is damaged"), "and said so");
  CHECK(!memcmp(&wreck, &keep, sizeof wreck), "a pattern that doesn't read isn't touched");

  bad = bytes;
  bad[0] = 'X';
  CHECK(!seqstore::unpack(bad.data(), bad.size(), wreck, &why), "another file's bytes are caught");
  CHECK(!strcmp(why, "not a pattern file"), "and said so");

  bad.assign(bytes.begin(), bytes.begin() + (long)bytes.size() / 2);
  CHECK(!seqstore::unpack(bad.data(), bad.size(), wreck, &why), "half a file is caught");

  CHECK(!seqstore::unpack(bytes.data(), 3, wreck, &why), "three bytes are caught");
  CHECK(!seqstore::unpack(0, 0, wreck, &why), "nothing at all is caught");

  bad = bytes;
  bad[4] = 99;  // a version from the future
  bad[bad.size() - 4] = 0;
  {
    uint32_t c = seqstore::crc32(bad.data(), bad.size() - 4);
    for (int i = 0; i < 4; i++) {
      bad[bad.size() - 4 + i] = (uint8_t)(c >> (8 * i));
    }
  }
  CHECK(!seqstore::unpack(bad.data(), bad.size(), wreck, &why), "a newer file is refused, not misread");
  CHECK(strstr(why, "newer") != 0, "and said so");

  // ----------------------------------------------------------- a file from a build with other dimensions ---
  // the last track's record is given a track number past this build's: it is skipped, and the rest still reads
  fill(p);
  seqstore::pack(p, bytes);
  {
    // find the track records: after the head (12) and the pattern's own 24 bytes comes the count
    size_t at = 12 + SEQ_NAME + 2 + 4 + 2;
    int tracks = bytes[at] | (bytes[at + 1] << 8);
    CHECK(tracks == SEQ_TRACKS, "every track was written");
    bytes[at + 2] = SEQ_TRACKS + 4;  // the first record says it is track 20
    uint32_t c = seqstore::crc32(bytes.data(), bytes.size() - 4);
    for (int i = 0; i < 4; i++) {
      bytes[bytes.size() - 4 + i] = (uint8_t)(c >> (8 * i));
    }
    seq::Pattern other;
    CHECK(seqstore::unpack(bytes.data(), bytes.size(), other, &why), "a file with a track we don't have still reads");
    CHECK(other.track[0].base == 60 && other.track[0].ch == 0, "the track we don't have is left as a new one");
    CHECK(other.track[0].step[0].n == 0, "and holds nothing");
    for (int t = 1; t < SEQ_TRACKS; t++) {
      CHECK(!memcmp(&other.track[t], &p.track[t], sizeof(seq::Track)), "every other track is exactly as it was");
    }
  }

  // ------------------------------------------------------------------------------------------ the slots ---
  CHECK(seqstore::slotFile(0, 0) == "A01.fmp", "bank A slot 1");
  CHECK(seqstore::slotFile(3, 15) == "D16.fmp", "bank D slot 16");
  CHECK(seqstore::slotFile(1, 9) == "B10.fmp", "bank B slot 10");
  CHECK(seqstore::slotFile(-1, 0).empty() && seqstore::slotFile(4, 0).empty(), "a bank that isn't there");
  CHECK(seqstore::slotFile(0, 16).empty() && seqstore::slotFile(0, -1).empty(), "a slot that isn't there");

  // ---------------------------------------------------------------------------------- files on the card ---
  CHECK(seqstore::makeDir(), "the folders are made");
  std::string path = seqstore::slotPath(2, 4);
  CHECK(path.find("C05.fmp") != std::string::npos, "the slot's path");
  remove(path.c_str());

  fill(p);
  CHECK(seqstore::save(path.c_str(), p, &why), "it saves");
  seq::Pattern fromCard;
  CHECK(seqstore::load(path.c_str(), fromCard, &why), "it loads");
  CHECK(same(p, fromCard), "what came back is what went out");
  CHECK(seqstore::peek(path.c_str(), name) && !strcmp(name, "Deep House"), "a slot's name without reading it all");

  // saving again over it keeps nothing behind
  snprintf(p.name, SEQ_NAME, "Second");
  CHECK(seqstore::save(path.c_str(), p, &why), "it saves over itself");
  CHECK(seqstore::load(path.c_str(), fromCard, &why) && !strcmp(fromCard.name, "Second"), "the new one is there");
  CHECK(access((path + ".new").c_str(), F_OK) != 0, "and the half-written file is gone");

  // a slot with nothing in it
  std::string none = seqstore::slotPath(3, 15);
  remove(none.c_str());
  CHECK(!seqstore::load(none.c_str(), fromCard, &why), "an empty slot doesn't load");
  CHECK(!strcmp(why, "the slot is empty"), "and says so");
  CHECK(!seqstore::peek(none.c_str(), name) && !name[0], "and has no name");

  // a file with rubbish in it
  {
    FILE* f = fopen(none.c_str(), "wb");
    CHECK(f != 0, "a rubbish file can be written");
    fputs("this is not a pattern, it is a letter to my mother", f);
    fclose(f);
  }
  CHECK(!seqstore::load(none.c_str(), fromCard, &why), "rubbish doesn't load");
  CHECK(!strcmp(why, "not a pattern file"), "and says so");
  CHECK(seqstore::erase(none.c_str()), "a slot can be emptied");
  CHECK(!seqstore::erase(none.c_str()), "and an empty one says so");

  // -------------------------------------------------------------------------------------- with no card ---
  root = "";
  CHECK(seqstore::slotPath(0, 0).empty(), "no card: no path");
  CHECK(!seqstore::makeDir(), "no card: no folders");
  CHECK(!seqstore::save("", p, &why) && !strcmp(why, "no SD card"), "no card: nothing is saved");
  CHECK(!seqstore::load("", fromCard, &why) && !strcmp(why, "no SD card"), "no card: nothing is loaded");
  CHECK(!seqstore::peek("", name), "no card: no names");

  printf("seqstore: ok (%d checks)\n", checks);
  return 0;
}
