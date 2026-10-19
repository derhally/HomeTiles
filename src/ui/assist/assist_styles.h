#pragma once

// The selectable looks of the Assist screen (speaker::AssistScreenStyle).
// Each style builds a handful of plain LVGL circles, pills, arcs and lines
// once and then only moves, sizes and fades them per frame: no layers, no
// canvas and no allocation per frame. The line styles (assist_styles_lines.cpp)
// paint their ticks, traces and fibers from one draw callback instead and
// invalidate only the box that changed. The overlay (assist_overlay.cpp) owns
// the parent, the timer, the label and the eased animation inputs.
//
// Sizes are fractions of the frame's base radius r (baseRadius(): 102 px at
// 1024x600). Motion stays at about 5 px per 33 ms frame at that radius: a
// panel that scans its DPI frame buffer out while LVGL flushes partial bands
// shows a tear line where a band ends under a faster mover. The overlay slows
// the clock on larger screens without the tear-free path (kTunedRadius).
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
  float w, h;       // the screen's size
  float dt;         // seconds since the last frame (0 on the first)
};

// The radius the styles were tuned at (1024x600).
inline constexpr float kTunedRadius = 102.0f;

// The base radius on a w x h screen: 17 % of the shorter side, smaller when
// the style's widest parts would leave the screen.
float baseRadius(speaker::AssistScreenStyle style, float w, float h);

// Builds the style's objects in parent (which the overlay cleans first).
void create(speaker::AssistScreenStyle style, lv_obj_t* parent);
// Positions the objects of the style created last.
void frame(speaker::AssistScreenStyle style, const Frame& f);
// The color of the phase label under the style.
uint32_t labelColor(speaker::AssistScreenStyle style);

}  // namespace assist_styles
