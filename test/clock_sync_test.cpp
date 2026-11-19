// The constants the clock's arithmetic rests on: 24 PPQ in, 96 PPQ out, four sub-ticks a pulse, a 16th a song
// position. It checks the numbers agree with each other, nothing more - it calls nothing in midisync.cpp and
// would pass if that file were empty. It is here to catch someone changing SEQ_PPQ or SEQ_STEP without meaning
// to; what the clock actually does is tested in clock_harness_test.cpp.
//
//   c++ -std=gnu++17 -Wall -I src/app -o /tmp/clock_sync_test test/clock_sync_test.cpp && /tmp/clock_sync_test
#include <stdio.h>
#include <stdint.h>

#include "seq.h"  // SEQ_PPQ, SEQ_STEP

static int fails;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    fails++;
  }
}

int main() {
  // MIDI Clock: 24 pulses per quarter note; sequencer runs at 96 PPQ.
  check(SEQ_PPQ == 96, "SEQ_PPQ is 96");
  check(SEQ_STEP == 24, "SEQ_STEP is 24 (one 16th)");
  check(SEQ_PPQ / 24 == 4, "24 PPQ clock -> 4 sequencer subticks per pulse");

  // 1 bar (16 sixteenths, 4 quarter notes) at 120 BPM: 24 * 4 = 96 MIDI clock pulses;
  // sequencer advances 96 * 4 = 384 subticks = 16 * SEQ_STEP.
  int pulsesPerBar = 24 * 4;
  int subticksPerBar = pulsesPerBar * 4;
  check(pulsesPerBar == 96, "96 MIDI clock pulses per 4/4 bar");
  check(subticksPerBar == 16 * SEQ_STEP, "pulse accumulation equals 16 sequencer steps (one bar)");

  // SPP: 14-bit value in 16th-note units (MIDI beat). Conversion to 96 PPQ subticks.
  uint16_t spp0 = 0;
  uint16_t spp1 = 1;
  uint16_t spp16 = 16;
  check((uint32_t)spp0 * (uint32_t)SEQ_STEP == 0, "SPP 0 -> tick 0");
  check((uint32_t)spp1 * (uint32_t)SEQ_STEP == 24, "SPP 1 (one 16th) -> 24 subticks");
  check((uint32_t)spp16 * (uint32_t)SEQ_STEP == 384, "SPP 16 -> one bar in subticks");

  // 14-bit SPP range
  uint16_t sppMax = 0x3FFF;
  check(sppMax == 16383, "SPP max is 14-bit");

  if (fails) {
    printf("clock_sync_test: %d FAIL(s)\n", fails);
    return 1;
  }
  printf("clock_sync_test: ok\n");
  return 0;
}
