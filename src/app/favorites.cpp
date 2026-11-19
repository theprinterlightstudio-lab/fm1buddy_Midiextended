// the favourite firmwares (favorites.h)
#include "favorites.h"

#include <string.h>

uint32_t favorites::key(const char* stem, const char* product) {
  uint32_t h = 2166136261u;  // FNV-1a, over "stem|product"
  for (const char* p = stem ? stem : ""; *p; p++) {
    h = (h ^ (uint8_t)*p) * 16777619u;
  }
  h = (h ^ '|') * 16777619u;
  for (const char* p = product ? product : ""; *p; p++) {
    h = (h ^ (uint8_t)*p) * 16777619u;
  }
  return h ? h : 1;
}

void favorites::clear(FavSet& s) {
  memset(&s, 0, sizeof s);
  s.version = FAV_VERSION;
}

bool favorites::has(const FavSet& s, uint32_t k) {
  for (int i = 0; i < s.n && i < FAV_MAX; i++) {
    if (s.key[i] == k) {
      return true;
    }
  }
  return false;
}

favorites::Toggled favorites::toggle(FavSet& s, uint32_t k) {
  for (int i = 0; i < s.n; i++) {
    if (s.key[i] == k) {
      memmove(&s.key[i], &s.key[i + 1], (s.n - i - 1) * sizeof s.key[0]);
      s.key[--s.n] = 0;
      return REMOVED;
    }
  }
  if (s.n >= FAV_MAX || !k) {
    return FULL;
  }
  s.key[s.n++] = k;
  return ADDED;
}

int favorites::restore(FavSet& s, bool found) {
  if (!found || s.version != FAV_VERSION) {
    clear(s);
    return -1;
  }
  int fixed = 0;
  if (s.n > FAV_MAX) {
    s.n = FAV_MAX;
    fixed++;
  }
  int n = 0;
  for (int i = 0; i < s.n; i++) {
    uint32_t k = s.key[i];
    bool again = false;
    for (int j = 0; j < n; j++) {
      again = again || s.key[j] == k;
    }
    if (!k || again) {
      fixed++;
      continue;
    }
    s.key[n++] = k;
  }
  for (int i = n; i < FAV_MAX; i++) {
    s.key[i] = 0;
  }
  s.n = (uint8_t)n;
  return fixed;
}
