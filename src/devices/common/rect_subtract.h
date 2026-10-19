#pragma once

// Rectangle subtraction for the tear-free UI path (p4_dsi_camera_presenter):
// the part of a rectangle that a set of other rectangles does not cover, as a
// short list of rectangles. Pure integer code, no LVGL, so the host tests
// cover it. Coordinates are inclusive.

#include <stdint.h>

namespace rect_subtract {

struct Box {
  int32_t x1, y1, x2, y2;
};

inline bool empty(const Box& b) { return b.x1 > b.x2 || b.y1 > b.y2; }

// Writes the up to four pieces of a minus b into out; returns their count,
// 1 (a itself) when they do not overlap, 0 when b covers a.
inline int subtract(const Box& a, const Box& b, Box* out) {
  if (b.x1 > a.x2 || b.x2 < a.x1 || b.y1 > a.y2 || b.y2 < a.y1) {
    out[0] = a;
    return 1;
  }
  int n = 0;
  if (b.y1 > a.y1) out[n++] = Box{a.x1, a.y1, a.x2, b.y1 - 1};  // above
  if (b.y2 < a.y2) out[n++] = Box{a.x1, b.y2 + 1, a.x2, a.y2};  // below
  const int32_t mid_y1 = b.y1 > a.y1 ? b.y1 : a.y1;
  const int32_t mid_y2 = b.y2 < a.y2 ? b.y2 : a.y2;
  if (b.x1 > a.x1) out[n++] = Box{a.x1, mid_y1, b.x1 - 1, mid_y2};  // left
  if (b.x2 < a.x2) out[n++] = Box{b.x2 + 1, mid_y1, a.x2, mid_y2};  // right
  return n;
}

// The pieces of `box` outside every one of the `count` covers, into out (at
// most `max`). Returns the count, or -1 when the pieces would not fit, in
// which case the caller treats the whole box as uncovered.
inline int outside(const Box& box, const Box* covers, int count, Box* out, int max) {
  if (max < 1) return -1;
  out[0] = box;
  int n = 1;
  for (int c = 0; c < count; ++c) {
    int m = 0;
    Box next[64];
    for (int i = 0; i < n; ++i) {
      Box pieces[4];
      const int k = subtract(out[i], covers[c], pieces);
      if (m + k > max || m + k > 64) return -1;
      for (int p = 0; p < k; ++p) next[m++] = pieces[p];
    }
    for (int i = 0; i < m; ++i) out[i] = next[i];
    n = m;
    if (n == 0) break;
  }
  return n;
}

}  // namespace rect_subtract
