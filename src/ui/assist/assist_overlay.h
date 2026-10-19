#pragma once

// Full-screen Assist screen on speaker panels. It appears when the wake word
// is heard (or an Assist start arrives) and follows speaker::assistUiPhase():
// Listening reacts to the live microphone level, Thinking runs while Home
// Assistant works on the answer, and Answering follows the speaker level
// while the answer plays. The look is the stored speaker::assistScreenStyle()
// (assist_styles.h: Atom, Reactor, Dot Halo, Voice Pills).
// When the conversation ends the screen hides and the panel shows what it
// showed before; a display that the wake word woke goes back to sleep. A tap
// hides the screen for the rest of the conversation.
//
// Every style is a handful of plain LVGL objects moved by one 33 ms timer:
// no canvas, no layers and no allocation per frame. Only the sphere's area
// redraws. The screen underneath is hidden while the overlay shows: LVGL
// looks for a covering object only among the screen's children, so an
// opaque overlay on the top layer would still have the whole tile grid
// drawn under every changed pixel (on the 7B that was most of each frame).
// Only the chosen style's objects exist; changing the style replaces them.
//
// Threading: every function runs on the Arduino loop task.

namespace assist_overlay {

// Active loop, beside speaker::service(): shows, updates and hides the
// screen. Does nothing on panels without a speaker.
void service();

// Sleep loop: wakes the display when a conversation starts. Returns true
// when it did; the caller then leaves the sleep branch.
bool serviceSleep();

// Settings: plays listening, thinking and answering once with the stored
// style and a simulated voice, then hides. Ignored during a conversation.
void preview();

}  // namespace assist_overlay
