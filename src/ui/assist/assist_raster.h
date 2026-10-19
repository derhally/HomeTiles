#pragma once

// Direct rasterizer for the Assist screen engine's lines and arcs
// (assist_screen.cpp). LVGL's software renderer draws a skewed line or an arc
// by evaluating masks over the shape's whole bounding box, so a diagonal line
// costs its box, not its length, and a thin ring costs its disc. The styles
// are made of hundreds of such shapes per frame (Sentinel's ticks, Iris'
// fibers, Phosphor's trace). These routines visit only the pixels along the
// shape, with the same anti-aliasing idea (coverage from the distance to the
// edge) and LVGL's own RGB565 colour mixing, and write straight into the band
// LVGL is rendering.
//
// Called from a LV_EVENT_DRAW_MAIN callback: with LVGL's single software draw
// unit every earlier lv_draw_* call has already been executed when the
// callback runs (lv_draw_finalize_task_creation dispatches at once), so a
// direct write lands over the right pixels, and later lv_draw_* calls draw
// over it. Supported targets: RGB565 (the panels) and XRGB8888 (the desktop
// lab and the host tests); anything else keeps the lv_draw_* path.
//
// Geometry follows lv_draw_line / lv_draw_arc: whole-pixel endpoints, an even
// width centred on a pixel edge and an odd width on a pixel centre, round
// ends as half discs, a ring as the border of the box centre +- radius.
//
// Loop task (LVGL) only. No allocation.

#include <stdint.h>

#include <lvgl.h>

namespace assist_raster {

struct Target {
  uint8_t* pixels;   // row 0 is buf.y1, column 0 is buf.x1
  int32_t stride;    // bytes per row
  bool wide;         // XRGB8888 (else RGB565)
  lv_area_t buf;     // the screen area the buffer holds
  lv_area_t clip;    // the screen area to draw within (inside buf)
};

// The band a draw callback may write into; false when the layer's buffer is
// not a plain RGB565 or XRGB8888 buffer.
bool target(const lv_layer_t* layer, Target* out);

// A line of the given width (>= 1 px) between two whole-pixel points in
// screen coordinates, opa 0..255, optionally with round ends.
void line(const Target& t, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, lv_color_t color,
          uint8_t opa, bool round_ends);

// An arc of a ring: centre and outer radius in whole pixels, the ring's width
// (clamped to the radius), angles in whole degrees clockwise from 3 o'clock
// like lv_draw_arc (end - start >= 360 is a full ring), round ends as discs
// at both ends.
void arc(const Target& t, int32_t cx, int32_t cy, int32_t radius, int32_t width, int32_t start_deg, int32_t end_deg,
         lv_color_t color, uint8_t opa, bool round_ends);

}  // namespace assist_raster
