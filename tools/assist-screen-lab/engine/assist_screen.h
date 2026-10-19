#pragma once

// Assist screens from data: a screen file (JSON, FORMAT.md) describes lines,
// arcs, discs, rectangles, polylines and text whose every property is a small
// expression of time, voice level, phase and radius. The engine compiles the
// file once and then, per frame, evaluates it into a draw list that one
// LVGL draw callback replays.
//
// Panel constraints, enforced when a file loads (a file over any limit is
// refused with a message naming it, so a downloaded file cannot hurt the
// panel):
// - Memory: one Screen is a fixed-size block (sizeof(Screen), about 145 KB,
//   PSRAM on the panel) allocated once; nothing is allocated per frame or per
//   load beyond it.
// - CPU: the worst-case expression operations and draw commands per frame are
//   counted from the file (repeat counts are constants) and capped.
// - Redraw: each top-level element keeps the box around what it drew this
//   frame and last frame; only the boxes of elements whose drawing changed
//   are invalidated, nothing when nothing changed.
// - Fonts: only the ui_font sizes every panel carries.
// - Text: fixed buffers per text element; no allocation when it changes.
// - Untrusted input: bounded JSON nesting and expression depth, NaN and
//   infinity become 0, coordinates are clamped.
//
// Loop task (LVGL) only.

#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

namespace assist_screen {

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
inline constexpr uint32_t kMaxFrameOps = 60000;
inline constexpr uint32_t kMaxFrameCmds = 1200;

// What the panel feeds the screen every 33 ms frame (assist_styles::Frame).
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

// How far a screen reaches sideways: half_r * r, and side_r * r + side_px
// (assist_styles::baseRadius).
struct Reach {
  float half_r, side_r, side_px;
};

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

}  // namespace assist_screen
