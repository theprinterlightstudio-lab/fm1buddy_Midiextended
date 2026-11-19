// the simulator's own SHA-256 (src/sim/sha256.h, for Linux and WSL): the FIPS 180-4 test vectors, every length
// across the padding boundaries, and any split of the input across Update calls
#include "../src/sim/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>

static std::string hex(const unsigned char* d) {
  char s[65];
  for (int i = 0; i < 32; i++) {
    sprintf(s + i * 2, "%02x", d[i]);
  }
  return s;
}

static int fails;
static void expect(const char* what, const std::string& got, const char* want) {
  if (got != want) {
    printf("sha256: %s: got %s, want %s\n", what, got.c_str(), want);
    fails++;
  }
}

static std::string once(const std::string& m) {
  unsigned char d[32];
  return hex(CC_SHA256(m.data(), (CC_LONG)m.size(), d));
}

static std::string split(const std::string& m, size_t step) {
  CC_SHA256_CTX c;
  CC_SHA256_Init(&c);
  for (size_t i = 0; i < m.size(); i += step) {
    CC_SHA256_Update(&c, m.data() + i, (CC_LONG)std::min(step, m.size() - i));
  }
  unsigned char d[32];
  CC_SHA256_Final(d, &c);
  return hex(d);
}

int main(int argc, char** argv) {
  expect("empty", once(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  expect("abc", once("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  expect("448 bits", once("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  expect("896 bits",
         once("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrst"
              "nopqrstu"),
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
  expect("a million a", once(std::string(1000000, 'a')),
         "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  // every length 0..200 (all the padding cases), one call vs. 1-, 7-, 63- and 64-byte pieces
  std::string m;
  for (int n = 0; n <= 200; n++) {
    std::string a = once(m);
    for (size_t step : {1, 7, 63, 64}) {
      if (split(m, step) != a) {
        printf("sha256: length %d in %zu-byte pieces differs\n", n, step);
        fails++;
      }
    }
    m += (char)(n * 31 + 7);
  }
  // with a file argument: its hash, for comparing with another tool (sha256sum, Python's hashlib)
  if (argc == 2) {
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
      return 1;
    }
    CC_SHA256_CTX c;
    CC_SHA256_Init(&c);
    unsigned char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f));) {
      CC_SHA256_Update(&c, buf, (CC_LONG)n);
    }
    fclose(f);
    unsigned char d[32];
    CC_SHA256_Final(d, &c);
    printf("%s  %s\n", hex(d).c_str(), argv[1]);
  }
  if (fails) {
    return 1;
  }
  printf("sha256: ok\n");
  return 0;
}
