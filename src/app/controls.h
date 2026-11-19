// The MIDI controls as the app plays them (FM Buddy 2.0): the one MidiControls (midictl.h), kept across restarts
// (CTL_KEY), sent as the Touch device (midi.h MIDI_TOUCH: through routing and the mapping, like the touch controller).
// The fader and knob pages (page_controls.cpp) show it; the encoder turns it (ui::encoder: the hardware's, the serial
// console's "enc", the simulator's keys)
#pragma once

#include "midictl.h"

namespace controls {
MidiControls& get();  // (loaded from what's kept, on first use)
// a finger moved control i to pos: sent when its value changed (true)
bool play(int i, uint8_t pos);
// control i turned by detents (a finger sliding on a knob; any kind): what it makes is sent. Returns how many messages
int turn(int i, int detents);
// positions changed: kept a few seconds after the last change (not a flash write for every move)
void moved();
// kept now (an assignment, a mode, a bank, a selection)
void save();
// (FM Buddy 2.0) the Global channel, 0-15: what a control on Global (CH_GLOBAL) sends on, and the touch controller's
// X/Y and Touche on Global. One for all of them: the channel the instrument plays its selected track from
int globalChannel();
void setGlobalChannel(int ch);
// the page showing a group (-1: none of them), and how it shows control i again (-1: all of it, built again)
void showing(int group, void (*show)(int i));
}  // namespace controls
