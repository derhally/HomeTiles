#pragma once

// The selectable looks of the Assist screen (speaker::AssistScreenStyle).
// Each style builds a handful of plain LVGL circles, pills and arcs once and
// then only moves, sizes and fades them per frame: no layers, no canvas and
// no allocation per frame. The overlay (assist_overlay.cpp) owns the parent,
// the timer, the label and the eased animation inputs.
//
// Motion stays at about 5 px per 33 ms frame: the 7B scans its DPI frame
// buffer out while LVGL flushes partial bands, so a faster mover shows a tear
// line where a band ends.
//
// Loop task (LVGL) only.

#include <lvgl.h>

#include "src/audio/speaker.h"

namespace assist_styles {

struct Frame {
  float t;          // seconds since the screen opened
  float level;      // voice (listening) or answer (answering) loudness, 0..1
  float mode;       // 0 = listening look, 1 = thinking/answering look, eased
  bool answering;   // the answer is playing
  float cx, cy, r;  // center and base radius (grows in as the screen opens)
};

// Builds the style's objects in parent (which the overlay cleans first).
void create(speaker::AssistScreenStyle style, lv_obj_t* parent);
// Positions the objects of the style created last.
void frame(speaker::AssistScreenStyle style, const Frame& f);

}  // namespace assist_styles
