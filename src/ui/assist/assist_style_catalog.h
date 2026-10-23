#pragma once

// Which Assist screen styles a panel offers. Each style lists the panel
// features it needs; a panel offers every style whose needs it has. The
// offered styles are the Settings > Audio list, the Bridge's "Assist screen"
// select options (the status's "screen_styles") and the only ids a command
// may pick. A stored style the panel does not offer shows as the first
// offered one (Atom always is) and stays stored.
//
// To change where a style shows, edit its line in kNeeds. To take styles off
// one board only, its build defines HOMETILES_ASSIST_STYLES_OFF as a mask of
// style numbers (bit n = speaker::AssistScreenStyle n).
//
// No LVGL and no Arduino here: the host tests include it as it is.

#include <stdint.h>

#ifndef HOMETILES_ASSIST_STYLES_OFF
#define HOMETILES_ASSIST_STYLES_OFF 0u
#endif

namespace assist_style_catalog {

enum Feature : uint8_t {
  // The shown screen is at least kWideAspect times as wide as tall
  // (landscape panels; not square, not upright).
  kWideScreen = 1u << 0,
  // The UI is drawn into the hidden framebuffer and swapped in, so fast
  // movers do not tear (Device::Capabilities::tear_free_ui, and only while
  // the panel shows its native orientation).
  kTearFreeUi = 1u << 1,
};

inline constexpr float kWideAspect = 1.2f;

// The features each style needs, in speaker::AssistScreenStyle order.
inline constexpr uint8_t kNeeds[] = {
    0,             // atom
    0,             // reactor
    0,             // dot_halo
    0,             // voice_pills
    0,             // sentinel: its file has square and tall layouts
    0,             // phosphor: its file has square and tall layouts
    kTearFreeUi,   // light_disc: its trails move about 8 px per frame
    0,             // lens
    0,             // iris
};
inline constexpr uint8_t kStyleCount = sizeof(kNeeds) / sizeof(kNeeds[0]);
static_assert(kStyleCount <= 32, "style masks are 32-bit");
inline constexpr uint32_t kAllStyles =
    kStyleCount >= 32 ? 0xFFFFFFFFu : ((1u << kStyleCount) - 1u);

// The panel's features from the shown screen size and the board's
// tear-free capability.
inline uint8_t features(int32_t width, int32_t height, bool tear_free) {
  uint8_t out = tear_free ? kTearFreeUi : 0;
  if (height > 0 && static_cast<float>(width) >= kWideAspect * static_cast<float>(height)) {
    out |= kWideScreen;
  }
  return out;
}

// Bit n set = style n offered. Atom (0) is always offered.
inline uint32_t available(uint8_t panel_features, uint32_t off = HOMETILES_ASSIST_STYLES_OFF) {
  uint32_t mask = 0;
  for (uint8_t i = 0; i < kStyleCount; ++i) {
    if ((kNeeds[i] & panel_features) == kNeeds[i]) mask |= 1u << i;
  }
  return (mask & ~off & kAllStyles) | 1u;
}

// The style shown for a stored one: itself when offered, else the first
// offered.
inline uint8_t effective(uint8_t stored, uint32_t mask) {
  if (stored < kStyleCount && (mask & (1u << stored))) return stored;
  for (uint8_t i = 0; i < kStyleCount; ++i) {
    if (mask & (1u << i)) return i;
  }
  return 0;
}

// Settings lists the offered styles in catalog order: position <-> style.
inline uint8_t count(uint32_t mask) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kStyleCount; ++i) n += (mask >> i) & 1u;
  return n;
}

inline uint8_t styleAt(uint8_t position, uint32_t mask) {
  for (uint8_t i = 0; i < kStyleCount; ++i) {
    if (!(mask & (1u << i))) continue;
    if (position == 0) return i;
    --position;
  }
  return 0;
}

inline uint8_t positionOf(uint8_t style, uint32_t mask) {
  uint8_t position = 0;
  for (uint8_t i = 0; i < kStyleCount && i < style; ++i) position += (mask >> i) & 1u;
  return position;
}

}  // namespace assist_style_catalog
