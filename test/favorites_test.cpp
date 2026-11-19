// the favourite firmwares (favorites.h): keys, adding and removing, what's kept. tools/unit-test.sh runs it
#include "favorites.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(c)                                     \
  do {                                               \
    if (!(c)) {                                      \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++;                                       \
    }                                                \
  } while (0)

int main() {
  // keys: the same firmware the same, another name or device another; never 0
  uint32_t zp = favorites::key("zp12", "FM-1_974");
  CHECK(zp == favorites::key("zp12", "FM-1_974"));
  CHECK(zp != favorites::key("zp12", "SMK-37 Elite_961"));
  CHECK(zp != favorites::key("zp13", "FM-1_974"));
  CHECK(favorites::key("a", "bc") != favorites::key("ab", "c"));  // (the separator)
  CHECK(favorites::key(0, 0) != 0 && favorites::key("", "") == favorites::key(0, 0));

  FavSet s;
  favorites::clear(s);
  CHECK(s.version == FAV_VERSION && s.n == 0 && !favorites::has(s, zp));
  CHECK(sizeof(FavSet) == 4 + 4 * FAV_MAX && sizeof(FavSet) == 508);  // (the kept layout)
  // a long press adds, another removes
  CHECK(favorites::toggle(s, zp) == favorites::ADDED && favorites::has(s, zp) && s.n == 1);
  uint32_t b = favorites::key("odin", "FM-1_974"), c = favorites::key("ritual", "FM-1_974");
  favorites::toggle(s, b);
  favorites::toggle(s, c);
  CHECK(favorites::toggle(s, b) == favorites::REMOVED && !favorites::has(s, b) && s.n == 2);
  CHECK(favorites::has(s, zp) && favorites::has(s, c) && s.key[2] == 0);  // (the rest moved up)
  // full: nothing more until one goes
  favorites::clear(s);
  for (uint32_t k = 1; k <= FAV_MAX; k++) {
    CHECK(favorites::toggle(s, k) == favorites::ADDED);
  }
  CHECK(favorites::toggle(s, 999) == favorites::FULL && !favorites::has(s, 999) && s.n == FAV_MAX);
  CHECK(favorites::toggle(s, 5) == favorites::REMOVED && favorites::toggle(s, 999) == favorites::ADDED);
  // kept: as it was, through bytes
  uint8_t bytes[sizeof(FavSet)];
  memcpy(bytes, &s, sizeof bytes);
  FavSet back;
  memcpy(&back, bytes, sizeof back);
  CHECK(favorites::restore(back, true) == 0 && !memcmp(&back, &s, sizeof back));
  // nothing kept, another version: none
  CHECK(favorites::restore(back, false) == -1 && back.n == 0);
  back = s;
  back.version = 7;
  CHECK(favorites::restore(back, true) == -1 && back.n == 0 && back.version == FAV_VERSION);
  // damaged: a count too big, zeros and repeats dropped
  FavSet d;
  favorites::clear(d);
  d.n = 200;
  d.key[0] = zp, d.key[1] = 0, d.key[2] = zp, d.key[3] = c;
  int fixed = favorites::restore(d, true);
  CHECK(fixed > 0 && d.n == 2 && d.key[0] == zp && d.key[1] == c && d.key[2] == 0);
  CHECK(favorites::restore(d, true) == 0);  // (and then sound)
  if (fails) {
    printf("favorites: %d failed\n", fails);
    return 1;
  }
  printf("favorites: ok\n");
  return 0;
}
