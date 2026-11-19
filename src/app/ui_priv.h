// Inside the app: the pieces the pages (page_*.cpp) share
#pragma once

#include <lvgl.h>

#include <string>

#include "hal.h"
#include "settings.h"
#include "sounds.h"
#include "themes.h"

#define RAIL_W 54     // (FM Buddy 2.0: 48 before: its tabs off the screen's left edge)
#define RAIL_IN 2     // the rail's tabs: this far in from the left edge, and from the top (8 before: the
                      // leftmost 8 px of the screen answered to nothing at all)
#define PAGE_W (SCREEN_PORTRAIT ? SCREEN_W : SCREEN_W - RAIL_W)  // (portrait: the rail is along the bottom)
#define RAIL_BOTTOM_H 46  // (portrait) the rail along the bottom: its height
#define PAGE_H (SCREEN_PORTRAIT ? SCREEN_H - RAIL_BOTTOM_H : SCREEN_H)
#define HEAD_H 44     // a page's top row (FM Buddy 2.0: 34 before, its buttons too small to hit)
#define HEAD_BTN_W 52  // a button in it: back, refresh
#define HEAD_BTN_H 40
#define RAIL_TAB_H (SCREEN_BIG ? 56 : 42)  // (42: room for FM-1/SMK above them)
// FM Buddy 2.0: the bar along the top of a full-screen page (Faders and Knobs, Touch): one size for all of them, big
// enough to hit (and to swipe along). Its buttons: TOP_BTN_W x TOP_BTN_H, icons in montserrat 20, words in TOP_FONT
#define TOP_BAR_H (SCREEN_BIG ? 46 : 40)
#define TOP_BTN_H (TOP_BAR_H - 4)
#define TOP_BTN_W (SCREEN_LONG / 8)
#define TOP_FONT (SCREEN_BIG ? &lv_font_montserrat_20 : &lv_font_montserrat_16)
// the screen's own edges answer touch badly: the bars' first and last buttons (back, the gear) keep this far in
// How far content keeps off each edge. Left and right were 10 and 8, so the left always lost two pixels more
// than the right, on every bar and on the sequencer's rows, for no reason anybody could point at. They are the
// same now, and EDGE_BAR puts the bars themselves all but on the glass, where there is nothing to spare
#define EDGE_BAR 2
#define EDGE_L 8
#define EDGE_R 8  // a tab on the rail: as big as the rail and the status under them allow
// and the top and bottom edges too (FM Buddy 2.0, 2026-11-14): every page's buttons and the full-screen pages' bars
// and surfaces keep this far in. A full-screen page's bottom strip still plays (a finger there: on the bottom row)
#define EDGE_T 3  // (6 and 8 were too much: 2026-11-14)
#define EDGE_B 4
#define ROW_H 44

namespace ui {
enum Tab { FIRMWARE,
           MIDI,
           SOUNDS,
           SETTINGS,
           TABS };
typedef void (*Builder)(lv_obj_t* page);

const Theme& theme();
inline lv_color_t color(uint32_t hex) { return lv_color_hex(hex); }

// show a page (in the current tab, or another)
void go(Builder b);
void go(Tab t, Builder b);
// the page over the whole screen, without the rail (until the next page)
void fullscreen(bool on);
// set by a page while it shows: called every 100 ms, and when a job has finished
extern void (*tick)();
extern void (*jobDone)();
// a message at the bottom for a moment (bad: in red)
void toast(const char* text, bool bad = false);
// text about the device, for the one there: "FM-1" as a word becomes "SMK" when it's an SMK-37 (fm1::machineName).
// Names and ids are left as they are ("FM-1_015", "ota-FM-1", "baudgirl-FM-1+VA")
std::string devText(const std::string& text);
// the theme changed: everything again, on the same page
void rebuild();

// building blocks
lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow = LV_FLEX_FLOW_COLUMN);  // no style, sized to its content
// FM Buddy 2.0: a star (favourites), 14 px or 20 (big), in colour c (star_icon.cpp)
const lv_image_dsc_t* starIcon(bool big);
lv_obj_t* star(lv_obj_t* parent, bool big, uint32_t c);
lv_obj_t* header(lv_obj_t* page, const char* title, Builder back = 0);       // the page's top row
lv_obj_t* scroller(lv_obj_t* page);                                          // the rest of the page, scrolling
lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color);
// a label on one line, cut with "..." (width: what the parent's flex leaves, or set)
lv_obj_t* line(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color);
lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* data = 0);
lv_obj_t* listRow(lv_obj_t* parent, lv_event_cb_t cb, void* data);  // a row to tap: a flex row
lv_obj_t* chip(lv_obj_t* parent, const char* text, uint32_t color);
// the flash map (jobs.h) and its key: cells of cell pixels; writing: the recovery's colors too
lv_obj_t* flashMap(lv_obj_t* parent, int cell, bool writing);

// the pages
void firmwarePage(lv_obj_t* page);
// the firmware list made again (catalog::sd changed), scrolled where it was: only while firmwarePage shows
void firmwareListAgain();
void midiPage(lv_obj_t* page);
void playPage(lv_obj_t* page);    // clock, arpeggiator, chords
void mapPage(lv_obj_t* page);     // the mapping's rules
void mapAddPage(lv_obj_t* page);  // a new rule
void touchPage(lv_obj_t* page);   // the touch controller (all of the screen)
void mirrorPage(lv_obj_t* page);  // the FM-1's screen, big (all of the screen; mirror.h)
void fadersPage(lv_obj_t* page);  // FM Buddy 2.0: four faders a bank (all of the screen; page_controls.cpp)
void knobsPage(lv_obj_t* page);   // FM Buddy 2.0: eight knobs a bank (all of the screen)
void seqPage(lv_obj_t* page);      // FM Buddy 2.0: the sequencer's 32-step editor (page_seq.cpp)
void seqSetupPage(lv_obj_t* page);
void seqPatternsPage(lv_obj_t* page);
void seqSongPage(lv_obj_t* page);      // song mode: the patterns in an order (song.h)

// the page fm1buddy starts on (Settings): its tab and page. Kept as a number: new ones go at the end
enum Start { START_FIRMWARE,  // the SD card's firmwares (no Wi-Fi needed)
             START_MIDI,
             START_MAPPING,
             START_TOUCH,  // the touch controller's X/Y pad
             START_SOUNDS,
             START_HUB,     // the Hub's firmwares
             START_PADS,    // the touch controller's pads
             START_SCREEN,  // the FM-1's screen (mirror.h)
             START_FADERS,  // FM Buddy 2.0: the faders
             START_KNOBS,   // FM Buddy 2.0: the knobs
             START_SEQ,     // FM Buddy 2.0: the sequencer
             STARTS };
extern const char* const START_NAMES[STARTS];
extern const Start START_ORDER[STARTS];  // as the list shows them
int startPage();
// FM Buddy 2.0: what it is (Settings > USB mode), kept as a number ("usbmode"):
//   USB_AUTO        on a computer the MIDI controller (USB MIDI, starting on the faders); on the FM-1 or a hub, FM Buddy
//   USB_FMBUDDY     on a computer the serial port (flashing, the log): fm1buddy as it ever was
//   USB_CONTROLLER  the MIDI controller always: USB MIDI on a computer, starting on the faders, no firmware or sounds tab
enum UsbMode { USB_AUTO,
               USB_FMBUDDY,
               USB_CONTROLLER,
               USB_MODES };
extern const char* const USB_MODE_NAMES[USB_MODES];
int usbMode();
void usbModeSet(int m);   // (on a computer, a change between the serial port and USB MIDI restarts it)
bool controllerStart();  // it starts on the faders (Auto on a computer, or Controller)
void firmwareSource(bool hub);  // the firmware page's list: the Hub's, or the SD card's
void touchMode(bool pads);      // the touch controller: its pads, or its X/Y pad
void touchFieldMode(int layout);  // (the simulator alone: Granular Field, for the visual review)
void touchFieldDemo(int fingers);
void touchFieldGran(int gran);
void touchFieldShadow(int amount);
void touchFieldGrid(int on);
void touchFieldShadowMode(int mode);  // (the simulator alone: a still preview with more glows than the panel feels)
// going to sleep after this many minutes with nothing going on (0: never; Settings > Sleep)
#define SLEEPS 4
extern const uint8_t SLEEP_MINUTES[SLEEPS];
int sleepMinutes();
void soundsPage(lv_obj_t* page);
void recordPage(lv_obj_t* page);       // the FM-1's USB audio to the SD card
int soundsDev();                       // the device picked there
const sounds::Speaks* soundsSpeaks();  // what it speaks (0 if it hasn't been asked yet)
void soundsReprobe();                  // ask it again (after something changed it): a probe job
void soundsProbed();                   // a probe ended: what it said
void sloopPage(lv_obj_t* page);        // SLOOP: backups, projects, sample slots
void slotsPage(lv_obj_t* page);        // Felucca and the firmwares built on it: sample slots
void syxPage(lv_obj_t* page);          // a .syx
void setupPage(lv_obj_t* page);        // a setup
void settingsPage(lv_obj_t* page);
void settingsLeft();  // a tab other than Settings: what Settings offered is forgotten
void wifiPage(lv_obj_t* page);
void backupPage(lv_obj_t* page);        // all of the FM-1's flash to the SD card
bool hasBackup();                       // a backup of it is on the SD card
void flashModePage(lv_obj_t* page);    // (FM Buddy 2.0) restarting into download mode, for flashing
void diskStartPage(lv_obj_t* page);     // USB disk: what it is, and starting it
void diskPage(lv_obj_t* page);          // USB disk mode itself (all of the screen)
void recoverStartPage(lv_obj_t* page);  // recovery: what goes on the FM-1, how, and starting it
void recoverPage(lv_obj_t* page);       // recovery mode itself (all of the screen)
}  // namespace ui
