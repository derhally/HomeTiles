#pragma once

// Assist screens from data: a screen file (JSON, FORMAT.md) describes lines,
// arcs, discs, rectangles, polylines and text whose every property is a small
// expression of time, voice level, phase and radius. The engine compiles the
// file once and then, per frame, evaluates it into a draw list that one
// LVGL draw callback replays.
//
// Panel constraints (a file over a load-time limit is refused with a message
// naming it):
// - Memory: one Screen is a fixed-size block (sizeof(Screen), about 105 KB,
//   PSRAM on the panel) allocated once; the engine allocates nothing per load
//   or frame. The LVGL draw calls it makes allocate their draw tasks like any
//   widget's; only commands that reach the band being drawn are issued.
// - CPU: the worst-case formula cost per frame (functions such as sin and pow
//   weigh more than + or *) and the number of shapes are counted from the file
//   (repeat counts are constants) and capped at load.
// - Fill: each frame may paint at most kFillScreens times the screen area
//   (estimated per shape, glows included); shapes past that are skipped and
//   counted (skippedShapes). Radii are clamped to the screen diagonal.
// - Redraw: each top-level element keeps the box around what it drew this
//   frame and last frame; only the boxes of elements whose drawing changed
//   are invalidated, nothing when nothing changed.
// - Fonts: only the ui_font sizes every panel carries. Text is short readout
//   glyphs (each literal part at most kMaxLiteralText bytes), never UI wording,
//   which stays in the central translations.
// - Untrusted input: bounded JSON nesting and expression depth, NaN and
//   infinity become 0, coordinates are clamped.
//
// The engine is compiled with HOMETILES_ASSIST_DATA_SCREENS (on for every
// panel with Assist, assist_data_screens.h); the types and baseRadius() below
// are always available. Loop task (LVGL) only.

#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

#include "src/ui/assist/assist_data_screens.h"

namespace assist_screen {

// The file format this engine reads. A newer file is refused with a message
// naming both numbers; additions to the format raise it.
inline constexpr int kFormat = 1;

// Hard limits of one screen file.
inline constexpr size_t kMaxFileBytes = 24 * 1024;
inline constexpr int kMaxJsonNodes = 2048;   // load time only
inline constexpr int kMaxJsonDepth = 12;
inline constexpr int kMaxSlots = 192;          // variables, lets and indexes
inline constexpr int kMaxOps = 4096;           // compiled expression code
inline constexpr int kMaxExprDepth = 24;       // nesting and value stack
inline constexpr int kMaxElements = 160;
inline constexpr int kMaxVars = 64;
inline constexpr int kMaxLets = 160;
inline constexpr int kMaxColors = 24;
inline constexpr int kMaxRepeat = 256;
inline constexpr int kMaxGroupDepth = 4;
inline constexpr int kMaxPolyPoints = 128;     // per polyline
inline constexpr int kMaxTrail = 4;            // older copies of a polyline
inline constexpr int kPointPool = 2048;        // all polylines with their trails
inline constexpr int kMaxTexts = 24;           // text elements plus log rows
inline constexpr int kTextBytes = 48;          // per text buffer
inline constexpr int kMaxLogRows = 8;
inline constexpr int kMaxSegments = 96;        // literal and {expr} parts of texts
inline constexpr int kStringPool = 2048;       // literal text, names
// Per frame, worst case over all branches.
inline constexpr uint32_t kMaxFrameOps = 60000;  // weighted formula cost
inline constexpr uint32_t kMaxFrameCmds = 1200;
inline constexpr float kFillScreens = 6.0f;      // painted area per frame, in screens
inline constexpr int kMaxLiteralText = 8;        // bytes per literal text part
inline constexpr int kMaxKeep = 8;               // history values per state var
inline constexpr int kMaxHistoryVars = 16;

// What the panel feeds the screen every 33 ms frame (assist_overlay.cpp).
struct Inputs {
  float t;          // seconds since the screen opened
  float dt;         // seconds since the last frame
  float level;      // voice or answer loudness, 0..1
  float mode;       // 0 listening look .. 1 thinking/answering look, eased
  bool answering;   // the answer is playing
  float r;          // base radius (pops in as the screen opens)
  float cx, cy;     // centre
  float w, h;       // screen size
};

// The screen's shape. A file can give each shape its own layout (reach and
// var overrides) and show elements on some shapes only.
enum Shape : uint8_t { kWide, kSquare, kTall, kShapeCount };
inline constexpr float kWideAspect = 1.2f;  // assist_style_catalog::kWideAspect
inline Shape shapeOf(float w, float h) {
  if (w >= kWideAspect * h) return kWide;
  if (h >= kWideAspect * w) return kTall;
  return kSquare;
}

// How far a screen reaches: sideways half_r * r, and side_r * r + side_px for
// readouts of a fixed pixel width; up_r * r above and down_r * r below the
// centre when set (0 = not limited). label_r places the phase label below the
// centre, in r.
struct Reach {
  float half_r = 2.0f, side_r = 0.0f, side_px = 0.0f;
  float up_r = 0.0f, down_r = 0.0f;
  float label_r = 2.1f;
};

// The screen's centre (the overlay's).
inline float centerY(float h) { return h / 2.0f - h * 0.05f; }

// The base radius on a w x h screen: 17 % of the shorter side, smaller when
// the reach would leave the screen (an 8 px edge).
inline float baseRadius(const Reach& reach, float w, float h) {
  constexpr float kEdgePx = 8.0f;
  float r = (w < h ? w : h) * 0.17f;
  const float half = w / 2.0f - kEdgePx;
  const float half_r = reach.half_r > 0.0f ? reach.half_r : 2.0f;
  const float side_alone = reach.side_r > 0.0f ? 0.0f : reach.side_px;
  if ((half - side_alone) / half_r < r) r = (half - side_alone) / half_r;
  if (reach.side_r > 0.0f && (half - reach.side_px) / reach.side_r < r) r = (half - reach.side_px) / reach.side_r;
  const float cy = centerY(h);
  if (reach.up_r > 0.0f && (cy - kEdgePx) / reach.up_r < r) r = (cy - kEdgePx) / reach.up_r;
  if (reach.down_r > 0.0f && (h - cy - kEdgePx) / reach.down_r < r) r = (h - cy - kEdgePx) / reach.down_r;
  return r < 1.0f ? 1.0f : r;
}

// What a screen asks of the panel (assist_style_catalog.h features).
enum Need : uint8_t { kNeedWide = 1u << 0, kNeedTearFree = 1u << 1 };

struct Info {
  char id[33];
  char name[33];
  uint8_t needs;
  uint32_t label_color;
  Reach reach[kShapeCount];  // by Shape; a shape without a layout keeps the file's
  uint8_t layouts;           // bit n = Shape n has a layout of its own
  // Measured at load: the worst case per frame.
  uint32_t frame_ops;
  uint32_t frame_cmds;
};

struct Screen;  // opaque, sizeof() is fixed

size_t screenSize();
// Places a Screen in memory of screenSize() bytes (the panel: PSRAM).
Screen* init(void* memory);

// Compiles a file. On failure returns false with a message (English, names
// the problem and where); the screen is then empty.
bool load(Screen* s, const char* text, size_t length, char* error, size_t error_size);
const Info& info(const Screen* s);

// Starts over: initial values, logs refilled. seed feeds rand().
void reset(Screen* s, uint32_t seed);
// Evaluates one frame into the draw list.
void frame(Screen* s, const Inputs& in);
// The areas to invalidate (screen coordinates) after frame(), at most max
// (overlapping ones are joined, the rest join the last); 0 when the picture
// did not change.
int dirty(const Screen* s, lv_area_t* areas, int max);
// Replays the draw list; ox/oy is the painter's screen offset.
void draw(const Screen* s, lv_layer_t* layer, int32_t ox, int32_t oy);
// Draw commands in the last frame (for measuring).
uint32_t commandCount(const Screen* s);
// Shapes skipped in the last frame because the fill budget ran out.
uint32_t skippedShapes(const Screen* s);

}  // namespace assist_screen
