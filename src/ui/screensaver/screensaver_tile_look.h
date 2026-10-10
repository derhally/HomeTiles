#pragma once

#include <Arduino.h>

#include "src/tiles/config/tile_config.h"

// Per-tile look of a screensaver tile (Tile::ss_*): its fill opacity,
// whether its icon shows, an own text color and the text position beside the
// icon. The background follows the tile color (None = no fill). Stored in /_screensaver/tile_looks.json by ScreensaverConfigStore;
// normal grids keep the defaults.
namespace screensaver_tile_look {

// Set bit of Tile::ss_text_color: black stays a valid own text color.
inline constexpr uint32_t kTextColorSet = 0x01000000u;

inline bool has_text_color(const Tile& tile) {
  return (tile.ss_text_color & kTextColorSet) != 0;
}

inline uint32_t text_rgb(const Tile& tile) { return tile.ss_text_color & 0xFFFFFFu; }

// "#RRGGBB" -> stored value; anything else is no own text color.
inline uint32_t parse_text_color(const String& raw) {
  String text = raw;
  text.trim();
  if (text.length() != 7 || text[0] != '#') return 0;
  uint32_t rgb = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = text[i];
    uint32_t nibble;
    if (c >= '0' && c <= '9') nibble = c - '0';
    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
    else return 0;
    rgb = (rgb << 4) | nibble;
  }
  return kTextColorSet | rgb;
}

inline String format_text_color(uint32_t stored) {
  if ((stored & kTextColorSet) == 0) return String();
  char hex[8];
  snprintf(hex, sizeof(hex), "#%06X", static_cast<unsigned>(stored & 0xFFFFFFu));
  return String(hex);
}

// Tile::ss_text_align. Standard keeps the title line even without a title, so
// a lone value sits on the second line; centered puts the shown lines in the
// middle of the icon row (compact_sensor_layout.h).
inline constexpr uint8_t kTextAlignStandard = 0;
inline constexpr uint8_t kTextAlignCentered = 1;

inline uint8_t normalize_text_align(long raw) {
  return raw == kTextAlignCentered ? kTextAlignCentered : kTextAlignStandard;
}

// Background opacity of a screensaver card: the tile's Opacity scales the
// global fill opacity (ScreensaverConfigData::tile_opacity). Like the clock
// card's opacity it fades only the fill, never the text or the icon.
inline uint8_t fill_opacity(uint8_t global_opacity, const Tile& tile) {
  return static_cast<uint8_t>((static_cast<unsigned>(global_opacity) * tile.ss_opacity + 127) / 255);
}

inline bool is_default(const Tile& tile) {
  return tile.ss_opacity == 255 && tile.ss_show_icon && !has_text_color(tile) &&
         tile.ss_text_align == kTextAlignStandard;
}

inline void reset(Tile& tile) {
  tile.ss_opacity = 255;
  tile.ss_show_icon = true;
  tile.ss_text_color = 0;
  tile.ss_text_align = kTextAlignStandard;
}

}  // namespace screensaver_tile_look
