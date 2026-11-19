#include "seqstore.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#include <direct.h>
#define mkdir_p(path, mode) _mkdir(path)
#else
#include <unistd.h>
#define mkdir_p(path, mode) mkdir(path, mode)
#endif

#include "hal.h"

namespace {

// ---------------------------------------------------------------------------------------- bytes in and out ---
void put8(std::vector<uint8_t>& o, uint8_t v) {
  o.push_back(v);
}

void put16(std::vector<uint8_t>& o, uint16_t v) {
  o.push_back((uint8_t)(v & 0xff));
  o.push_back((uint8_t)(v >> 8));
}

void put32(std::vector<uint8_t>& o, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    o.push_back((uint8_t)(v >> (8 * i)));
  }
}

void putName(std::vector<uint8_t>& o, const char* s, int n) {
  for (int i = 0; i < n; i++) {
    o.push_back((uint8_t)(s[i]));
  }
}

// a reader that can't run off its bytes: once it has, every read gives 0 and bad() is true
struct In {
  const uint8_t* b;
  size_t n, at;
  bool over;
  In(const uint8_t* bytes, size_t len) : b(bytes), n(len), at(0), over(false) {}
  bool bad() const {
    return over;
  }
  uint8_t u8() {
    if (at + 1 > n) {
      over = true;
      return 0;
    }
    return b[at++];
  }
  uint16_t u16() {
    uint16_t a = u8();
    return (uint16_t)(a | (u8() << 8));
  }
  uint32_t u32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
      v |= (uint32_t)u8() << (8 * i);
    }
    return v;
  }
  void name(char* out, int len) {
    for (int i = 0; i < len; i++) {
      char c = (char)u8();
      out[i] = c;
    }
    out[len - 1] = 0;
  }
  void skip(size_t k) {
    if (at + k > n) {
      over = true;
      at = n;
    } else {
      at += k;
    }
  }
};

#define HEAD_BYTES 12  // magic(4) version(2) head(2) flags(2) spare(2)

}  // namespace

// ------------------------------------------------------------------------------------------------- CRC-32 ---
uint32_t seqstore::crc32(const uint8_t* b, size_t n) {
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < n; i++) {
    c ^= b[i];
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xedb88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
  }
  return ~c;
}

// --------------------------------------------------------------------------------------------------- pack ---
void seqstore::pack(const seq::Pattern& p, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(1024);
  putName(out, SEQ_FILE_MAGIC, 4);
  put16(out, SEQ_FILE_VERSION);
  put16(out, HEAD_BYTES);
  put16(out, 0);  // flags
  put16(out, 0);  // spare

  putName(out, p.name, SEQ_NAME);
  put16(out, p.bpm);
  put8(out, p.steps);
  put8(out, p.swing);
  put8(out, p.gate);
  put8(out, p.tracks);
  put16(out, 0);  // spare

  // only the tracks that hold something, or that aren't as a new track would be
  std::vector<int> keep;
  for (int t = 0; t < SEQ_TRACKS; t++) {
    const seq::Track& tr = p.track[t];
    bool any = tr.ch != (uint8_t)t || tr.len || tr.off || tr.mute || tr.solo || tr.base != 60;
    for (int s = 0; !any && s < SEQ_STEPS; s++) {
      any = tr.step[s].n != 0;
    }
    if (any) {
      keep.push_back(t);
    }
  }
  put16(out, (uint16_t)keep.size());
  for (int t : keep) {
    const seq::Track& tr = p.track[t];
    put8(out, (uint8_t)t);
    put8(out, tr.ch);
    put8(out, tr.len);
    put8(out, tr.off);
    put8(out, tr.mute);
    put8(out, tr.solo);
    put8(out, tr.base);
    put8(out, 0);  // spare
    putName(out, tr.name, SEQ_TNAME);
    size_t countAt = out.size();
    put16(out, 0);
    int steps = 0;
    for (int s = 0; s < SEQ_STEPS; s++) {
      const seq::Step& st = tr.step[s];
      if (!st.n) {
        continue;
      }
      steps++;
      put8(out, (uint8_t)s);
      uint8_t n = st.n > SEQ_NOTES ? SEQ_NOTES : st.n;
      put8(out, n);
      for (int k = 0; k < n; k++) {
        const seq::Note& nt = st.note[k];
        put8(out, nt.pitch);
        put8(out, nt.vel);
        put8(out, nt.gate);
        put8(out, nt.prob);
        put8(out, nt.repeat);
        put8(out, nt.human);
        put8(out, nt.accent);
        put8(out, (uint8_t)nt.offset);
      }
    }
    out[countAt] = (uint8_t)(steps & 0xff);
    out[countAt + 1] = (uint8_t)(steps >> 8);
  }
  put32(out, crc32(out.data(), out.size()));
}

// ------------------------------------------------------------------------------------------------- unpack ---
// Read twice: once to see that the bytes run to the end (p is 0), once to fill the pattern in. So a file that stops
// half way never leaves half a pattern behind, and nothing the size of a Pattern (68 KB) is ever put on the stack.
namespace {

bool walk(const uint8_t* b, size_t n, seq::Pattern* p, const char** why) {
  In in(b, n - 4);  // (the CRC at the end is not part of what is read)
  in.skip(4);
  uint16_t version = in.u16();
  uint16_t head = in.u16();
  if (version > SEQ_FILE_VERSION) {
    *why = "a newer pattern than this build knows";
    return false;
  }
  if (head < HEAD_BYTES || head > n - 4) {
    *why = "the file is damaged";
    return false;
  }
  in.at = head;  // (a later version may write more head: skip what this build doesn't know)

  char name[SEQ_NAME];
  in.name(name, SEQ_NAME);
  uint16_t bpm = in.u16();
  uint8_t steps = in.u8(), swing = in.u8(), gate = in.u8(), tracks = in.u8();
  in.skip(2);
  if (in.bad()) {
    *why = "the file stops too soon";
    return false;
  }
  if (bpm < 30 || bpm > 300 || !steps || steps > SEQ_STEPS || swing > 100 || gate > 200) {
    *why = "the pattern's settings are out of range";
    return false;
  }
  if (p) {
    memcpy(p->name, name, SEQ_NAME);
    p->bpm = bpm;
    p->steps = steps;
    p->swing = swing;
    p->gate = gate;
    p->tracks = tracks && tracks <= SEQ_TRACKS ? tracks : SEQ_TRACKS;
  }

  int records = in.u16();
  for (int i = 0; i < records && !in.bad(); i++) {
    int t = in.u8();
    // a track past this build's own (a 16-track file read by a build with fewer) is read and dropped, so what
    // follows it still reads
    seq::Track* tr = p && t >= 0 && t < SEQ_TRACKS ? &p->track[t] : 0;
    uint8_t ch = in.u8(), len = in.u8(), off = in.u8(), mute = in.u8(), solo = in.u8(), base = in.u8();
    in.skip(1);
    char tname[SEQ_TNAME];
    in.name(tname, SEQ_TNAME);
    if (tr) {
      tr->ch = (uint8_t)(ch & 15);
      tr->len = len > SEQ_STEPS ? 0 : len;
      tr->off = off ? 1 : 0;
      tr->mute = mute ? 1 : 0;
      tr->solo = solo ? 1 : 0;
      tr->base = base > 127 ? 60 : base;
      memcpy(tr->name, tname, SEQ_TNAME);
    }
    int written = in.u16();
    for (int k = 0; k < written && !in.bad(); k++) {
      int s = in.u8();
      int notes = in.u8();
      seq::Step* st = tr && s >= 0 && s < SEQ_STEPS ? &tr->step[s] : 0;
      if (st) {
        memset(st, 0, sizeof *st);
        st->n = (uint8_t)(notes > SEQ_NOTES ? SEQ_NOTES : notes);
      }
      for (int j = 0; j < notes; j++) {
        seq::Note nt;
        nt.pitch = (uint8_t)(in.u8() & 127);
        nt.vel = in.u8();
        nt.gate = in.u8();
        nt.prob = in.u8();
        nt.repeat = in.u8();
        nt.human = in.u8();
        nt.accent = in.u8();
        nt.offset = (int8_t)in.u8();
        nt.vel = nt.vel ? (uint8_t)(nt.vel & 127) : 100;
        nt.repeat = nt.repeat ? nt.repeat : 1;
        if (st && j < SEQ_NOTES) {
          st->note[j] = nt;
        }
      }
    }
  }
  if (in.bad()) {
    *why = "the file stops too soon";
    return false;
  }
  *why = "";
  return true;
}

}  // namespace

bool seqstore::unpack(const uint8_t* b, size_t n, seq::Pattern& p, const char** why) {
  static const char* ignored;
  if (!why) {
    why = &ignored;
  }
  if (!b || n < HEAD_BYTES + 4 || memcmp(b, SEQ_FILE_MAGIC, 4)) {
    *why = "not a pattern file";
    return false;
  }
  if (crc32(b, n - 4) != ((uint32_t)b[n - 4] | ((uint32_t)b[n - 3] << 8) | ((uint32_t)b[n - 2] << 16) |
                          ((uint32_t)b[n - 1] << 24))) {
    *why = "the file is damaged";
    return false;
  }
  if (!walk(b, n, 0, why)) {  // (nothing is written until the whole file has been read once)
    return false;
  }
  memset(&p, 0, sizeof p);
  for (int t = 0; t < SEQ_TRACKS; t++) {  // a track the file doesn't mention is a new one
    snprintf(p.track[t].name, SEQ_TNAME, "T%d", t + 1);
    p.track[t].ch = (uint8_t)t;
    p.track[t].base = 60;
  }
  return walk(b, n, &p, why);
}

bool seqstore::peekName(const uint8_t* b, size_t n, char out[SEQ_NAME]) {
  out[0] = 0;
  if (!b || n < HEAD_BYTES + SEQ_NAME || memcmp(b, SEQ_FILE_MAGIC, 4)) {
    return false;
  }
  In in(b, n);
  in.skip(4);
  in.u16();
  uint16_t head = in.u16();
  if (head < HEAD_BYTES || (size_t)head + SEQ_NAME > n) {
    return false;
  }
  in.at = head;
  in.name(out, SEQ_NAME);
  return !in.bad();
}

// ------------------------------------------------------------------------------------------------- slots ---
std::string seqstore::slotFile(int bank, int slot) {
  if (bank < 0 || bank >= SEQ_BANKS || slot < 0 || slot >= SEQ_SLOTS) {
    return "";
  }
  char b[16];
  snprintf(b, sizeof b, "%c%02d.fmp", 'A' + bank, slot + 1);
  return b;
}

std::string seqstore::slotPath(int bank, int slot) {
  std::string f = slotFile(bank, slot);
  const char* root = hal::sd();
  if (f.empty() || !root) {
    return "";
  }
  return std::string(root) + SEQ_PAT_DIR "/" + f;
}

bool seqstore::makeDir() {
  const char* root = hal::sd();
  if (!root) {
    return false;
  }
  mkdir_p((std::string(root) + "/fm1buddy").c_str(), 0777);  // (it may be the first thing this board writes)
  mkdir_p((std::string(root) + SEQ_DIR).c_str(), 0777);
  mkdir_p((std::string(root) + SEQ_PAT_DIR).c_str(), 0777);
  struct stat st;
  return !stat((std::string(root) + SEQ_PAT_DIR).c_str(), &st);
}

// ------------------------------------------------------------------------------------------ files ---
bool seqstore::save(const char* path, const seq::Pattern& p, const char** why) {
  static const char* ignored;
  if (!why) {
    why = &ignored;
  }
  if (!path || !*path) {
    *why = "no SD card";
    return false;
  }
  std::vector<uint8_t> bytes;
  pack(p, bytes);
  std::string tmp = std::string(path) + ".new";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) {
    *why = "can't write to the card";
    return false;
  }
  bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  ok = !fclose(f) && ok;
  if (!ok) {
    remove(tmp.c_str());
    *why = "the card is full or gone";
    return false;
  }
  remove(path);  // (FAT: rename over a file that's there fails)
  if (rename(tmp.c_str(), path)) {
    remove(tmp.c_str());
    *why = "can't put it in its slot";
    return false;
  }
  *why = "";
  return true;
}

bool seqstore::load(const char* path, seq::Pattern& p, const char** why) {
  static const char* ignored;
  if (!why) {
    why = &ignored;
  }
  if (!path || !*path) {
    *why = "no SD card";
    return false;
  }
  FILE* f = fopen(path, "rb");
  if (!f) {
    *why = "the slot is empty";
    return false;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0 || (unsigned long)n > SEQ_FILE_MAX) {
    fclose(f);
    *why = "not a pattern file";
    return false;
  }
  std::vector<uint8_t> bytes((size_t)n);
  bool read = fread(bytes.data(), 1, (size_t)n, f) == (size_t)n;
  fclose(f);
  if (!read) {
    *why = "the card stopped answering";
    return false;
  }
  return unpack(bytes.data(), bytes.size(), p, why);
}

bool seqstore::peek(const char* path, char out[SEQ_NAME]) {
  out[0] = 0;
  if (!path || !*path) {
    return false;
  }
  FILE* f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  uint8_t b[HEAD_BYTES + SEQ_NAME];
  size_t got = fread(b, 1, sizeof b, f);
  fclose(f);
  return peekName(b, got, out);
}

bool seqstore::erase(const char* path) {
  return path && *path && !remove(path);
}
