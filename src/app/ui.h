// The app: call ui::frame() once LVGL has its display (it draws the app's frame at once, and lights the screen), then
// ui::begin() once the board's ready (the SD card), then lv_timer_handler() as usual
#pragma once

namespace ui {
void frame();
void begin();
// FM Buddy 2.0: the encoder (a GPIO one, the serial console's "enc", the simulator's keys): turned by detents (+:
// clockwise), pushed, or held down (controls.cpp)
void encoder(int detents);
void encoderPush(bool held);
}  // namespace ui
