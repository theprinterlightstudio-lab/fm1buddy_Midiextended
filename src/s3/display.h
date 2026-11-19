// The board's screen, for LVGL and hal::sleep: the 2.8" (FNK0104B) and the 4" (FNK0104S) through TFT_eSPI, the 3.5"
// (FNK0104N) through its QSPI ST77922 (SCREEN_ST77922: platformio.ini). The 3.5"'s panel is portrait and LVGL turns
// its picture 270 degrees: a flush turns the pixels on their way out (the panel's own axis swap is unreliable)
#pragma once

#include <lvgl.h>

namespace display {
// the panel up, its backlight still off (ui::frame lights it with the app's frame drawn): LVGL's display for it, with
// its buffers and flush
lv_display_t* begin();
// the panel off and asleep (hal::sleep, before the chip's)
void sleep();
// the panel is portrait and LVGL turns it: its touch readings are portrait too
bool rotated();
// a touch reading (the panel's own pixels), as the screen shows it in landscape (SCREEN_LONG x SCREEN_SHORT: main.cpp
// calibrates it there, then turns it to portrait when the screen is: turn)
void toScreen(int px, int py, int& x, int& y);
// a point on the screen, as the panel has it (toScreen's other way round: LVGL takes a turned panel's own pixels)
void fromScreen(int x, int y, int& px, int& py);
// FM Buddy 2.0: the whole picture turned 180 degrees (the USB-C port on the other side), by the screen's controller.
// Kept ("flip"), and as kept from begin. Not on the 3.5" (its picture is turned on the way out: canFlip false)
bool canFlip();
bool flipped();
void flip(bool on);
// FM Buddy 2.0: portrait (hal::TURN_PORTRAIT, TURN_PORTRAIT_2), as kept ("turn") at begin: it sets hal::screenW, H.
// canTurn: not on the 3.5"
bool canTurn();
int turn();
}  // namespace display
