#pragma once

// Full-screen Assist screen on speaker panels. It appears when the wake word
// is heard (or an Assist start arrives) and follows speaker::assistUiPhase():
//   Listening  a glowing sphere of overlapping color lobes that swells and
//              changes shape with the live microphone level;
//   Thinking   the lobes gather into orbiting satellites around a pulsing
//              core while Home Assistant works on the answer;
//   Answering  the same orbit, its core and satellites following the speaker
//              level while the answer plays.
// When the conversation ends the screen hides and the panel shows what it
// showed before; a display that the wake word woke goes back to sleep. A tap
// hides the screen for the rest of the conversation.
//
// The sphere is a handful of plain LVGL circles moved by one 33 ms timer:
// no canvas, no layers and no allocation per frame. Only the sphere's area
// redraws, and the overlay is opaque, so the UI underneath does not.
//
// Threading: both functions run on the Arduino loop task.

namespace assist_overlay {

// Active loop, beside speaker::service(): shows, updates and hides the
// screen. Does nothing on panels without a speaker.
void service();

// Sleep loop: wakes the display when a conversation starts. Returns true
// when it did; the caller then leaves the sleep branch.
bool serviceSleep();

}  // namespace assist_overlay
