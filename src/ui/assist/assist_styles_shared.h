#pragma once

// Helpers shared by the Assist screen styles (assist_styles.cpp and
// assist_styles_lines.cpp): plain LVGL circles, pills, arcs and lines that
// are built once and then only moved, sized and faded per frame, plus the
// direct draw calls (lv_draw_line/arc/rect) the line styles paint with from
// one LV_EVENT_DRAW_MAIN callback.
//
// Loop task (LVGL) only.

#include <lvgl.h>
#include <math.h>

namespace assist_styles {
namespace detail {

constexpr float kTwoPi = 6.2831853f;
constexpr float kPi = 3.1415927f;

inline lv_obj_t* createCircle(lv_obj_t* parent, uint32_t color, lv_opa_t opa) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, opa, 0);
  lv_obj_set_size(obj, 1, 1);
  return obj;
}

// A circle lit from above: color on top fading to shade below.
inline lv_obj_t* createOrb(lv_obj_t* parent, uint32_t color, uint32_t shade, lv_opa_t opa) {
  lv_obj_t* obj = createCircle(parent, color, opa);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(shade), 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
  return obj;
}

// An outline-only circle.
inline lv_obj_t* createRing(lv_obj_t* parent, uint32_t color, int32_t width) {
  lv_obj_t* obj = createCircle(parent, color, LV_OPA_TRANSP);
  lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(obj, width, 0);
  return obj;
}

// A square: like createCircle without the rounding.
inline lv_obj_t* createBox(lv_obj_t* parent, uint32_t color, lv_opa_t opa) {
  lv_obj_t* obj = createCircle(parent, color, opa);
  lv_obj_set_style_radius(obj, 0, 0);
  return obj;
}

inline lv_obj_t* createSpinner(lv_obj_t* parent, int32_t width, uint32_t color) {
  lv_obj_t* arc = lv_arc_create(parent);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
  return arc;
}

// A line object whose points live in caller-owned storage (placeSegment).
inline lv_obj_t* createLine(lv_obj_t* parent, uint32_t color, int32_t width) {
  lv_obj_t* obj = lv_line_create(parent);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_line_width(obj, width, 0);
  lv_obj_set_style_line_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_line_rounded(obj, true, 0);
  return obj;
}

// A label with a fixed width, so its text may change without a relayout.
inline lv_obj_t* createLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color,
                             lv_text_align_t align, int32_t width) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(obj, width);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(obj, align, 0);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
  lv_label_set_text_static(obj, "");
  return obj;
}

// An invisible full-size object that paints with on_draw (LV_EVENT_DRAW_MAIN).
// The style invalidates only the area that changed (lv_obj_invalidate_area).
inline lv_obj_t* createPainter(lv_obj_t* parent, lv_event_cb_t on_draw) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(obj, 0, 0);
  lv_obj_add_event_cb(obj, on_draw, LV_EVENT_DRAW_MAIN, nullptr);
  return obj;
}

inline void place(lv_obj_t* obj, float cx, float cy, float diameter) {
  const int32_t d = diameter < 2.0f ? 2 : static_cast<int32_t>(diameter);
  lv_obj_set_size(obj, d, d);
  lv_obj_set_pos(obj, static_cast<int32_t>(cx) - d / 2, static_cast<int32_t>(cy) - d / 2);
}

inline void placePill(lv_obj_t* obj, float cx, float cy, float w, float h) {
  const int32_t iw = w < 2.0f ? 2 : static_cast<int32_t>(w);
  const int32_t ih = h < 2.0f ? 2 : static_cast<int32_t>(h);
  lv_obj_set_size(obj, iw, ih);
  lv_obj_set_pos(obj, static_cast<int32_t>(cx) - iw / 2, static_cast<int32_t>(cy) - ih / 2);
}

// Sizes a line object to the segment's own box (plus its width), so moving it
// invalidates only that box, and stores the two points in pts.
inline void placeSegment(lv_obj_t* line, lv_point_precise_t* pts, float x1, float y1, float x2,
                         float y2, int32_t width) {
  const int32_t minx = static_cast<int32_t>(floorf(fminf(x1, x2))) - width;
  const int32_t miny = static_cast<int32_t>(floorf(fminf(y1, y2))) - width;
  const int32_t maxx = static_cast<int32_t>(ceilf(fmaxf(x1, x2))) + width;
  const int32_t maxy = static_cast<int32_t>(ceilf(fmaxf(y1, y2))) + width;
  lv_obj_set_pos(line, minx, miny);
  lv_obj_set_size(line, maxx - minx, maxy - miny);
  pts[0].x = static_cast<lv_value_precise_t>(x1 - minx);
  pts[0].y = static_cast<lv_value_precise_t>(y1 - miny);
  pts[1].x = static_cast<lv_value_precise_t>(x2 - minx);
  pts[1].y = static_cast<lv_value_precise_t>(y2 - miny);
  lv_line_set_points(line, pts, 2);
}

inline lv_opa_t opa(float value) {
  if (value <= 0.0f) return LV_OPA_TRANSP;
  if (value >= 1.0f) return LV_OPA_COVER;
  return static_cast<lv_opa_t>(value * 255.0f);
}

inline int32_t wrapDegrees(float degrees) {
  const int32_t d = static_cast<int32_t>(degrees) % 360;
  return d < 0 ? d + 360 : d;
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Shows a spinner as an arc of sweep degrees starting at start, or hides it
// when it would be nearly invisible.
inline void spin(lv_obj_t* arc, float cx, float cy, float diameter, float start, float sweep,
                 float alpha) {
  if (alpha < 0.03f) {
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_HIDDEN);
  place(arc, cx, cy, diameter);
  lv_obj_set_style_arc_opa(arc, opa(alpha), LV_PART_INDICATOR);
  const int32_t s = wrapDegrees(start);
  lv_arc_set_angles(arc, s, sweep >= 360.0f ? s + 360 : wrapDegrees(start + sweep));
}

// Hides an object while it would be nearly invisible, so it costs no drawing.
inline bool visible(lv_obj_t* obj, float alpha) {
  if (alpha < 0.03f) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return false;
  }
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  return true;
}

// Invalidates the box around (cx, cy) with the given half size, in the
// painter's own coordinates.
inline void invalidateBox(lv_obj_t* painter, float cx, float cy, float half_w, float half_h) {
  lv_area_t coords;
  lv_obj_get_coords(painter, &coords);
  lv_area_t area;
  area.x1 = coords.x1 + static_cast<int32_t>(cx - half_w) - 1;
  area.y1 = coords.y1 + static_cast<int32_t>(cy - half_h) - 1;
  area.x2 = coords.x1 + static_cast<int32_t>(cx + half_w) + 1;
  area.y2 = coords.y1 + static_cast<int32_t>(cy + half_h) + 1;
  lv_obj_invalidate_area(painter, &area);
}

// ---- direct drawing, from a painter's draw callback ----
// Coordinates are the painter's own; ox/oy is its screen offset.
struct Pen {
  lv_layer_t* layer;
  int32_t ox, oy;
};

inline Pen pen(lv_event_t* e) {
  Pen p;
  p.layer = lv_event_get_layer(e);
  lv_area_t coords;
  lv_obj_get_coords(lv_event_get_current_target_obj(e), &coords);
  p.ox = coords.x1;
  p.oy = coords.y1;
  return p;
}

inline void drawLine(const Pen& p, float x1, float y1, float x2, float y2, float width,
                     uint32_t color, float alpha, bool rounded = true) {
  if (alpha < 0.02f) return;
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.base.layer = p.layer;
  dsc.color = lv_color_hex(color);
  dsc.opa = opa(alpha);
  dsc.width = width < 1.0f ? 1 : static_cast<int32_t>(width + 0.5f);
  dsc.round_start = rounded;
  dsc.round_end = rounded;
  dsc.p1.x = static_cast<lv_value_precise_t>(p.ox + x1);
  dsc.p1.y = static_cast<lv_value_precise_t>(p.oy + y1);
  dsc.p2.x = static_cast<lv_value_precise_t>(p.ox + x2);
  dsc.p2.y = static_cast<lv_value_precise_t>(p.oy + y2);
  lv_draw_line(p.layer, &dsc);
}

// A bright line over a wide faint copy: the panel's cheap glow.
inline void glowLine(const Pen& p, float x1, float y1, float x2, float y2, float width,
                     uint32_t color, float alpha) {
  drawLine(p, x1, y1, x2, y2, width * 4.0f, color, alpha * 0.16f);
  drawLine(p, x1, y1, x2, y2, width, color, alpha);
}

// Angles in degrees, clockwise from 3 o'clock like LVGL's arcs.
inline void drawArc(const Pen& p, float cx, float cy, float radius, float start, float end,
                    float width, uint32_t color, float alpha, bool rounded = true) {
  if (alpha < 0.02f || radius < 1.0f) return;
  lv_draw_arc_dsc_t dsc;
  lv_draw_arc_dsc_init(&dsc);
  dsc.base.layer = p.layer;
  dsc.color = lv_color_hex(color);
  dsc.opa = opa(alpha);
  dsc.width = width < 1.0f ? 1 : static_cast<int32_t>(width + 0.5f);
  dsc.rounded = rounded;
  dsc.center.x = p.ox + static_cast<int32_t>(cx);
  dsc.center.y = p.oy + static_cast<int32_t>(cy);
  dsc.radius = static_cast<uint16_t>(radius);
  if (end - start >= 360.0f) {
    dsc.start_angle = 0;
    dsc.end_angle = 360;
  } else {
    dsc.start_angle = wrapDegrees(start);
    dsc.end_angle = wrapDegrees(end);
  }
  lv_draw_arc(p.layer, &dsc);
}

inline void glowArc(const Pen& p, float cx, float cy, float radius, float start, float end,
                    float width, uint32_t color, float alpha) {
  drawArc(p, cx, cy, radius, start, end, width * 4.0f, color, alpha * 0.16f);
  drawArc(p, cx, cy, radius, start, end, width, color, alpha);
}

inline void drawDisc(const Pen& p, float cx, float cy, float radius, uint32_t color, float alpha) {
  if (alpha < 0.02f || radius < 1.0f) return;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.base.layer = p.layer;
  dsc.bg_color = lv_color_hex(color);
  dsc.bg_opa = opa(alpha);
  dsc.radius = LV_RADIUS_CIRCLE;
  lv_area_t area;
  area.x1 = p.ox + static_cast<int32_t>(cx - radius);
  area.y1 = p.oy + static_cast<int32_t>(cy - radius);
  area.x2 = p.ox + static_cast<int32_t>(cx + radius);
  area.y2 = p.oy + static_cast<int32_t>(cy + radius);
  lv_draw_rect(p.layer, &dsc, &area);
}

inline void drawRect(const Pen& p, float x1, float y1, float x2, float y2, uint32_t color,
                     float alpha) {
  if (alpha < 0.02f) return;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.base.layer = p.layer;
  dsc.bg_color = lv_color_hex(color);
  dsc.bg_opa = opa(alpha);
  lv_area_t area;
  area.x1 = p.ox + static_cast<int32_t>(x1);
  area.y1 = p.oy + static_cast<int32_t>(y1);
  area.x2 = p.ox + static_cast<int32_t>(x2);
  area.y2 = p.oy + static_cast<int32_t>(y2);
  lv_draw_rect(p.layer, &dsc, &area);
}

// An outline rectangle from four lines.
inline void drawFrame(const Pen& p, float x1, float y1, float x2, float y2, float width,
                      uint32_t color, float alpha) {
  drawLine(p, x1, y1, x2, y1, width, color, alpha, false);
  drawLine(p, x2, y1, x2, y2, width, color, alpha, false);
  drawLine(p, x2, y2, x1, y2, width, color, alpha, false);
  drawLine(p, x1, y2, x1, y1, width, color, alpha, false);
}

}  // namespace detail
}  // namespace assist_styles
