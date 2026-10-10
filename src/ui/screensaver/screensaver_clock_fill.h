#pragma once

#include "src/tiles/runtime/tile_renderer_fonts.h"

// The optional card behind the screensaver clock (ScreensaverConfigData::
// clock_fill): padding around the lines and corner radius in display pixels.
// The Web Admin preview draws the same card (web_admin_styles.cpp).
namespace screensaver_clock_fill {
inline int pad_x() { return tile_layout::scale(16); }
inline int pad_y() { return tile_layout::scale(8); }
inline int radius() { return tile_layout::scale(16); }
}  // namespace screensaver_clock_fill
