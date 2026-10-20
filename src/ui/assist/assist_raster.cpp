#include "src/ui/assist/assist_raster.h"

#include <math.h>

// The direct writes rely on LVGL executing every queued draw task before the
// draw callback continues: one software draw unit, no threads, no PPA unit.
// Another configuration keeps every shape on the lv_draw_* path.
#if LV_USE_OS != LV_OS_NONE || LV_DRAW_SW_DRAW_UNIT_CNT > 1 || LV_USE_PPA
#define ASSIST_RASTER_DIRECT 0
#else
#define ASSIST_RASTER_DIRECT 1
#endif

namespace assist_raster {
namespace {

constexpr float kPi = 3.14159265f;

inline float sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float fmin3(float a, float b) { return a < b ? a : b; }
inline float fmax3(float a, float b) { return a > b ? a : b; }

// LVGL's RGB565 mix (lv_color_16_16_mix): the same result as its blends.
inline uint16_t mix565(uint16_t fg, uint16_t bg, uint32_t mix) {
  if (mix >= 255) return fg;
  if (mix == 0 || fg == bg) return bg;
  const uint32_t mix5 = (mix + 4) >> 3;
  const uint32_t b = (static_cast<uint32_t>(bg) | (static_cast<uint32_t>(bg) << 16)) & 0x7E0F81F;
  const uint32_t f = (static_cast<uint32_t>(fg) | (static_cast<uint32_t>(fg) << 16)) & 0x7E0F81F;
  const uint32_t result = ((((f - b) * mix5) >> 5) + b) & 0x7E0F81F;
  return static_cast<uint16_t>((result >> 16) | result);
}

// XRGB8888 mix per channel, like lv_color_mix32.
inline uint32_t mix888(uint32_t fg, uint32_t bg, uint32_t mix) {
  if (mix >= 255) return fg;
  if (mix == 0) return bg;
  const uint32_t inv = 255 - mix;
  const uint32_t r = (((fg >> 16) & 0xFF) * mix + ((bg >> 16) & 0xFF) * inv) / 255;
  const uint32_t g = (((fg >> 8) & 0xFF) * mix + ((bg >> 8) & 0xFF) * inv) / 255;
  const uint32_t b = ((fg & 0xFF) * mix + (bg & 0xFF) * inv) / 255;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// One row of the target: blends coverage * opa of the colour into x1..x2.
struct Row {
  const Target* t;
  uint8_t* row;      // the row's first pixel (buf.x1)
  uint16_t c16;
  uint32_t c32;
  uint32_t opa;
  inline void put(int32_t x, float cov) {
    if (cov <= 0.0f) return;
    const uint32_t mix = static_cast<uint32_t>(cov * static_cast<float>(opa) + 0.5f);
    if (!mix) return;
    const int32_t i = x - t->buf.x1;
    if (t->wide) {
      uint32_t* p = reinterpret_cast<uint32_t*>(row) + i;
      *p = mix888(c32, *p, mix);
    } else if (t->swapped) {
      uint16_t* p = reinterpret_cast<uint16_t*>(row) + i;
      *p = __builtin_bswap16(mix565(c16, __builtin_bswap16(*p), mix));
    } else {
      uint16_t* p = reinterpret_cast<uint16_t*>(row) + i;
      *p = mix565(c16, *p, mix);
    }
  }
};

inline Row row(const Target& t, int32_t y, lv_color_t color, uint8_t opa) {
  Row r;
  r.t = &t;
  r.row = t.pixels + static_cast<size_t>(y - t.buf.y1) * static_cast<size_t>(t.stride);
  r.c16 = lv_color_to_u16(color);
  r.c32 = lv_color_to_u32(color);
  r.opa = opa;
  return r;
}

// A pixel span [lo, hi] (inclusive) from a float interval, clipped.
inline bool span(float lo, float hi, int32_t clip_lo, int32_t clip_hi, int32_t* x1, int32_t* x2) {
  if (!(hi >= lo)) return false;
  const float a = floorf(lo), b = ceilf(hi);
  if (a > 2.0e9f || b < -2.0e9f) return false;
  *x1 = static_cast<int32_t>(a);
  *x2 = static_cast<int32_t>(b);
  if (*x1 < clip_lo) *x1 = clip_lo;
  if (*x2 > clip_hi) *x2 = clip_hi;
  return *x1 <= *x2;
}

}  // namespace

bool target(const lv_layer_t* layer, Target* out) {
#if !ASSIST_RASTER_DIRECT
  (void)layer;
  (void)out;
  return false;
#else
  if (!layer || !layer->draw_buf || !layer->draw_buf->data) return false;
  const lv_draw_buf_t* buf = layer->draw_buf;
  const lv_color_format_t cf = static_cast<lv_color_format_t>(buf->header.cf);
  if (cf != LV_COLOR_FORMAT_RGB565 && cf != LV_COLOR_FORMAT_RGB565_SWAPPED && cf != LV_COLOR_FORMAT_XRGB8888) {
    return false;
  }
  out->pixels = buf->data;
  out->stride = static_cast<int32_t>(buf->header.stride);
  out->wide = cf == LV_COLOR_FORMAT_XRGB8888;
  out->swapped = cf == LV_COLOR_FORMAT_RGB565_SWAPPED;
  out->buf = layer->buf_area;
  // The clip within the buffer (lv_area_intersect is private in LVGL 9.6).
  const lv_area_t& c = layer->_clip_area;
  const lv_area_t& b = layer->buf_area;
  out->clip.x1 = c.x1 > b.x1 ? c.x1 : b.x1;
  out->clip.y1 = c.y1 > b.y1 ? c.y1 : b.y1;
  out->clip.x2 = c.x2 < b.x2 ? c.x2 : b.x2;
  out->clip.y2 = c.y2 < b.y2 ? c.y2 : b.y2;
  return out->clip.x1 <= out->clip.x2 && out->clip.y1 <= out->clip.y2;
#endif
}

// A horizontal or vertical line without round ends is the rectangle
// lv_draw_line fills: width w spans w >> 1 pixels before the line's pixel
// and the rest after it; no anti-aliasing.
void fillAxisLine(const Target& t, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, lv_color_t color,
                  uint8_t opa) {
  const int32_t before = width >> 1, after = width - before - 1;
  lv_area_t a;
  if (y1 == y2) {
    a.x1 = x1 < x2 ? x1 : x2;
    a.x2 = x1 < x2 ? x2 : x1;
    a.y1 = y1 - before;
    a.y2 = y1 + after;
  } else {
    a.y1 = y1 < y2 ? y1 : y2;
    a.y2 = y1 < y2 ? y2 : y1;
    a.x1 = x1 - before;
    a.x2 = x1 + after;
  }
  if (a.x1 < t.clip.x1) a.x1 = t.clip.x1;
  if (a.y1 < t.clip.y1) a.y1 = t.clip.y1;
  if (a.x2 > t.clip.x2) a.x2 = t.clip.x2;
  if (a.y2 > t.clip.y2) a.y2 = t.clip.y2;
  if (a.x1 > a.x2 || a.y1 > a.y2) return;
  const uint16_t c16 = lv_color_to_u16(color);
  const uint32_t c32 = lv_color_to_u32(color);
  for (int32_t py = a.y1; py <= a.y2; ++py) {
    uint8_t* row_start = t.pixels + static_cast<size_t>(py - t.buf.y1) * static_cast<size_t>(t.stride);
    if (t.wide) {
      uint32_t* p = reinterpret_cast<uint32_t*>(row_start) + (a.x1 - t.buf.x1);
      for (int32_t px = a.x1; px <= a.x2; ++px, ++p) *p = mix888(c32, *p, opa);
    } else if (t.swapped) {
      uint16_t* p = reinterpret_cast<uint16_t*>(row_start) + (a.x1 - t.buf.x1);
      for (int32_t px = a.x1; px <= a.x2; ++px, ++p) *p = __builtin_bswap16(mix565(c16, __builtin_bswap16(*p), opa));
    } else {
      uint16_t* p = reinterpret_cast<uint16_t*>(row_start) + (a.x1 - t.buf.x1);
      if (opa >= 255) {
        for (int32_t px = a.x1; px <= a.x2; ++px, ++p) *p = c16;
      } else {
        for (int32_t px = a.x1; px <= a.x2; ++px, ++p) *p = mix565(c16, *p, opa);
      }
    }
  }
}

void line(const Target& t, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, lv_color_t color,
          uint8_t opa, bool round_ends) {
  if (width < 1 || opa == 0 || (x1 == x2 && y1 == y2)) return;
  if (!round_ends && (x1 == x2 || y1 == y2)) {
    fillAxisLine(t, x1, y1, x2, y2, width, color, opa);
    return;
  }
  // LVGL centres an odd width on the pixel and an even width on its edge.
  const float off = (width & 1) ? 0.5f : 0.0f;
  const float ax = static_cast<float>(x1) + off, ay = static_cast<float>(y1) + off;
  const float bx = static_cast<float>(x2) + off, by = static_cast<float>(y2) + off;
  const float dx = bx - ax, dy = by - ay;
  const float len = sqrtf(dx * dx + dy * dy);
  const float ux = dx / len, uy = dy / len;  // along the line
  const float nx = -uy, ny = ux;             // across it
  const float h = static_cast<float>(width) * 0.5f;
  const float reach = h + 1.0f;  // the anti-aliased edge's outer limit
  const float along_lo = round_ends ? -reach : -1.5f;
  const float along_hi = round_ends ? len + reach : len + 1.5f;
  const float top = fmin3(ay, by) - reach, bottom = fmax3(ay, by) + reach;
  int32_t py1 = static_cast<int32_t>(floorf(top)), py2 = static_cast<int32_t>(ceilf(bottom));
  if (py1 < t.clip.y1) py1 = t.clip.y1;
  if (py2 > t.clip.y2) py2 = t.clip.y2;
  constexpr float kEps = 1e-6f;
  for (int32_t py = py1; py <= py2; ++py) {
    const float yc = static_cast<float>(py) + 0.5f;
    const float ty = yc - ay;
    // Where this row crosses the thick line: the across condition
    // |tx*nx + ty*ny| <= reach and the along condition tx*ux + ty*uy in
    // [along_lo, along_hi], both intervals in x.
    float lo = -3.0e9f, hi = 3.0e9f;
    bool empty = false;
    if (fabsf(nx) > kEps) {
      const float a = ax + (-reach - ty * ny) / nx, b = ax + (reach - ty * ny) / nx;
      lo = fmax3(lo, fmin3(a, b));
      hi = fmin3(hi, fmax3(a, b));
    } else if (fabsf(ty * ny) > reach) {
      empty = true;
    }
    if (fabsf(ux) > kEps) {
      const float a = ax + (along_lo - ty * uy) / ux, b = ax + (along_hi - ty * uy) / ux;
      lo = fmax3(lo, fmin3(a, b));
      hi = fmin3(hi, fmax3(a, b));
    } else {
      const float along = ty * uy;
      if (along < along_lo || along > along_hi) empty = true;
    }
    if (round_ends) {
      // The end discs widen the row where they stick out past the body.
      const float da = yc - ay, db = yc - by;
      if (fabsf(da) < reach) {
        const float r = sqrtf(reach * reach - da * da);
        if (empty) {
          lo = ax - r;
          hi = ax + r;
          empty = false;
        } else {
          lo = fmin3(lo, ax - r);
          hi = fmax3(hi, ax + r);
        }
      }
      if (fabsf(db) < reach) {
        const float r = sqrtf(reach * reach - db * db);
        if (empty) {
          lo = bx - r;
          hi = bx + r;
          empty = false;
        } else {
          lo = fmin3(lo, bx - r);
          hi = fmax3(hi, bx + r);
        }
      }
    }
    if (empty) continue;
    int32_t px1, px2;
    if (!span(lo, hi, t.clip.x1, t.clip.x2, &px1, &px2)) continue;
    Row r = row(t, py, color, opa);
    for (int32_t px = px1; px <= px2; ++px) {
      const float tx = static_cast<float>(px) + 0.5f - ax;
      const float across = fabsf(tx * nx + ty * ny);
      const float along = tx * ux + ty * uy;
      float cov;
      if (round_ends && along < 0.0f) {
        cov = sat(h + 0.5f - sqrtf(tx * tx + ty * ty));
      } else if (round_ends && along > len) {
        const float ex = tx - dx, ey = ty - dy;
        cov = sat(h + 0.5f - sqrtf(ex * ex + ey * ey));
      } else {
        cov = sat(h + 0.5f - across);
        if (!round_ends) cov *= sat(fmin3(along, len - along) + 1.0f);
      }
      r.put(px, cov);
    }
  }
}

void arc(const Target& t, int32_t cx, int32_t cy, int32_t radius, int32_t width, int32_t start_deg, int32_t end_deg,
         lv_color_t color, uint8_t opa, bool round_ends) {
  if (width < 1 || radius < 1 || opa == 0 || start_deg == end_deg) return;
  if (width > radius) width = radius;
  const bool full = end_deg - start_deg >= 360 || start_deg - end_deg >= 360;
  int32_t s = start_deg % 360, e = end_deg % 360;
  if (s < 0) s += 360;
  if (e < 0) e += 360;
  int32_t sweep = e - s;
  if (sweep <= 0) sweep += 360;
  if (full) sweep = 360;
  // The ring is the border of the box centre +- radius: its outer circle has
  // the radius radius + 0.5 around the pixel centre.
  const float cxf = static_cast<float>(cx) + 0.5f, cyf = static_cast<float>(cy) + 0.5f;
  const float ro = static_cast<float>(radius) + 0.5f;
  // A width of the radius fills the sector to its centre (no inner edge).
  const float ri = width >= radius ? -1.0f : ro - static_cast<float>(width);
  const float ro1 = ro + 1.0f;                       // the anti-aliased outer edge's limit
  const float ri1 = ri > 1.0f ? ri - 1.0f : 0.0f;    // and the inner edge's
  const float sr = static_cast<float>(s) * kPi / 180.0f, er = static_cast<float>(e) * kPi / 180.0f;
  const float cs = cosf(sr), ss = sinf(sr), ce = cosf(er), se = sinf(er);
  const float cap_r = static_cast<float>(width) * 0.5f;
  const float cap_ring = ro - cap_r;  // the end discs' centres sit mid-ring
  const float cap_sx = cxf + cap_ring * cs, cap_sy = cyf + cap_ring * ss;
  const float cap_ex = cxf + cap_ring * ce, cap_ey = cyf + cap_ring * se;

  // The box to visit: the whole ring, or the sweep's reach from its end points
  // and the axis extremes it crosses (plus the end discs).
  float bx1 = cxf - ro1, by1 = cyf - ro1, bx2 = cxf + ro1, by2 = cyf + ro1;
  if (!full) {
    bx1 = bx2 = cxf;
    by1 = by2 = cyf;
    auto take = [&](float x, float y, float margin) {
      bx1 = fmin3(bx1, x - margin);
      bx2 = fmax3(bx2, x + margin);
      by1 = fmin3(by1, y - margin);
      by2 = fmax3(by2, y + margin);
    };
    take(cxf + ro * cs, cyf + ro * ss, 1.5f);
    take(cxf + ro * ce, cyf + ro * se, 1.5f);
    take(cxf + ri * cs, cyf + ri * ss, 1.5f);
    take(cxf + ri * ce, cyf + ri * se, 1.5f);
    for (int32_t axis = 0; axis < 360; axis += 90) {
      int32_t d = axis - s;
      if (d < 0) d += 360;
      if (d <= sweep) {
        const float ar = static_cast<float>(axis) * kPi / 180.0f;
        take(cxf + ro * cosf(ar), cyf + ro * sinf(ar), 1.5f);
      }
    }
    if (round_ends) {
      take(cap_sx, cap_sy, cap_r + 1.5f);
      take(cap_ex, cap_ey, cap_r + 1.5f);
    }
  }
  int32_t py1 = static_cast<int32_t>(floorf(by1)), py2 = static_cast<int32_t>(ceilf(by2));
  if (py1 < t.clip.y1) py1 = t.clip.y1;
  if (py2 > t.clip.y2) py2 = t.clip.y2;
  int32_t cx1 = static_cast<int32_t>(floorf(bx1)), cx2 = static_cast<int32_t>(ceilf(bx2));
  if (cx1 < t.clip.x1) cx1 = t.clip.x1;
  if (cx2 > t.clip.x2) cx2 = t.clip.x2;
  if (py1 > py2 || cx1 > cx2) return;
  const bool narrow = sweep <= 180;

  for (int32_t py = py1; py <= py2; ++py) {
    const float yc = static_cast<float>(py) + 0.5f;
    const float dy = yc - cyf;
    const float oy2 = ro1 * ro1 - dy * dy;
    if (oy2 <= 0.0f) continue;
    const float ox = sqrtf(oy2);
    // One span through the ring, or two around the hole.
    float spans[2][2];
    int spans_n = 1;
    spans[0][0] = cxf - ox;
    spans[0][1] = cxf + ox;
    const float iy2 = ri1 * ri1 - dy * dy;
    if (ri1 > 0.0f && iy2 > 0.0f) {
      const float ix = sqrtf(iy2);
      spans[0][1] = cxf - ix;
      spans[1][0] = cxf + ix;
      spans[1][1] = cxf + ox;
      spans_n = 2;
    }
    Row r = row(t, py, color, opa);
    for (int k = 0; k < spans_n; ++k) {
      int32_t px1, px2;
      if (!span(spans[k][0], spans[k][1], cx1, cx2, &px1, &px2)) continue;
      for (int32_t px = px1; px <= px2; ++px) {
        const float dx = static_cast<float>(px) + 0.5f - cxf;
        const float d = sqrtf(dx * dx + dy * dy);
        float cov = sat(ro + 0.5f - d) * sat(d - ri + 0.5f);
        if (!full) {
          // Past the start ray and before the end ray (a sector), or not in
          // the gap between them (a sweep over 180 degrees).
          const float ds = cs * dy - ss * dx;
          const float de = -(ce * dy - se * dx);
          const float angular = narrow ? sat(0.5f + ds) * sat(0.5f + de) : 1.0f - sat(0.5f - ds) * sat(0.5f - de);
          cov *= angular;
          if (round_ends && cov < 1.0f) {
            // The end discs, only where a pixel lies within their boxes.
            const float xc = static_cast<float>(px) + 0.5f;
            const float sx = xc - cap_sx, sy = yc - cap_sy;
            const float ex = xc - cap_ex, ey = yc - cap_ey;
            const float cap_reach = cap_r + 1.0f;
            float cap = 0.0f;
            if (fabsf(sx) < cap_reach && fabsf(sy) < cap_reach) cap = sat(cap_r + 0.5f - sqrtf(sx * sx + sy * sy));
            if (fabsf(ex) < cap_reach && fabsf(ey) < cap_reach) {
              cap = fmax3(cap, sat(cap_r + 0.5f - sqrtf(ex * ex + ey * ey)));
            }
            if (cap > cov) cov = cap;
          }
        }
        r.put(px, cov);
      }
    }
  }
}


void disc(const Target& t, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t color, uint8_t opa, bool shaded,
          lv_color_t bottom) {
  if (opa == 0 || x2 < x1 || y2 < y1) return;
  const int32_t w = x2 - x1 + 1, h = y2 - y1 + 1;
  // The circle inscribed in the box: radius half the shorter side, centred.
  const float r = static_cast<float>(w < h ? w : h) * 0.5f;
  const float cxf = static_cast<float>(x1) + static_cast<float>(w) * 0.5f;
  const float cyf = static_cast<float>(y1) + static_cast<float>(h) * 0.5f;
  const float reach = r + 1.0f;
  int32_t py1 = y1, py2 = y2;
  if (py1 < t.clip.y1) py1 = t.clip.y1;
  if (py2 > t.clip.y2) py2 = t.clip.y2;
  const uint32_t c32_top = lv_color_to_u32(color), c32_bottom = lv_color_to_u32(bottom);
  for (int32_t py = py1; py <= py2; ++py) {
    const float yc = static_cast<float>(py) + 0.5f;
    const float dy = yc - cyf;
    const float d2 = reach * reach - dy * dy;
    if (d2 <= 0.0f) continue;
    const float half = sqrtf(d2);
    int32_t px1, px2;
    if (!span(cxf - half, cxf + half, t.clip.x1 > x1 ? t.clip.x1 : x1, t.clip.x2 < x2 ? t.clip.x2 : x2, &px1, &px2)) continue;
    // The row's colour: a straight blend from the top row to the bottom one.
    lv_color_t row_color = color;
    if (shaded && h > 1) {
      const uint32_t mix = static_cast<uint32_t>((py - y1) * 255 / (h - 1));
      const uint32_t c = mix888(c32_bottom, c32_top, mix);
      row_color = lv_color_make((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
    Row row_at = row(t, py, row_color, opa);
    // Full pixels inside as one plain run, the distance test only near the edge.
    const float inner = sqrtf(fmax3(0.0f, (r - 1.5f) * (r - 1.5f) - dy * dy));
    int32_t ix1 = static_cast<int32_t>(ceilf(cxf - inner)), ix2 = static_cast<int32_t>(floorf(cxf + inner)) - 1;
    if (ix1 < px1) ix1 = px1;
    if (ix2 > px2) ix2 = px2;
    if (r <= 2.0f) ix2 = ix1 - 1;
    for (int32_t px = px1; px < ix1 && px <= px2; ++px) {
      const float dx = static_cast<float>(px) + 0.5f - cxf;
      row_at.put(px, sat(r + 0.5f - sqrtf(dx * dx + dy * dy)));
    }
    if (ix1 <= ix2) {
      if (t.wide) {
        uint32_t* p = reinterpret_cast<uint32_t*>(row_at.row) + (ix1 - t.buf.x1);
        if (opa >= 255) {
          for (int32_t px = ix1; px <= ix2; ++px, ++p) *p = row_at.c32;
        } else {
          for (int32_t px = ix1; px <= ix2; ++px, ++p) *p = mix888(row_at.c32, *p, opa);
        }
      } else if (t.swapped) {
        uint16_t* p = reinterpret_cast<uint16_t*>(row_at.row) + (ix1 - t.buf.x1);
        const uint16_t swapped = __builtin_bswap16(row_at.c16);
        if (opa >= 255) {
          for (int32_t px = ix1; px <= ix2; ++px, ++p) *p = swapped;
        } else {
          for (int32_t px = ix1; px <= ix2; ++px, ++p) *p = __builtin_bswap16(mix565(row_at.c16, __builtin_bswap16(*p), opa));
        }
      } else {
        uint16_t* p = reinterpret_cast<uint16_t*>(row_at.row) + (ix1 - t.buf.x1);
        if (opa >= 255) {
          for (int32_t px = ix1; px <= ix2; ++px, ++p) *p = row_at.c16;
        } else {
          for (int32_t px = ix1; px <= ix2; ++px, ++p) *p = mix565(row_at.c16, *p, opa);
        }
      }
    }
    for (int32_t px = (ix2 + 1 > px1 ? ix2 + 1 : px1); px <= px2; ++px) {
      if (px >= ix1 && px <= ix2) continue;
      const float dx = static_cast<float>(px) + 0.5f - cxf;
      row_at.put(px, sat(r + 0.5f - sqrtf(dx * dx + dy * dy)));
    }
  }
}

}  // namespace assist_raster
