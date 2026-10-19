#include "src/ui/assist/assist_styles_lines.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "src/fonts/ui_fonts.h"
#include "src/ui/assist/assist_styles_shared.h"

// The five styles drawn mostly from lines, after the options page's canvas
// previews (Sentinel, Phosphor, Light Disc, Lens, Iris). Sizes are fractions
// of the frame's base radius r (102 px at 1024x600), so the looks scale with
// the screen like the circle styles do.

namespace assist_styles {
namespace {

using namespace detail;

// The answering look fades in and out like the thinking look does.
float answerMix(float current, bool answering) {
  return current + ((answering ? 1.0f : 0.0f) - current) * 0.12f;
}

// A fixed pseudo-random fraction per index, 0..1 (no table, no allocation).
float hash01(int i, int salt) { return static_cast<float>((i * 7 + salt * 13) % 23) / 22.0f; }

// A polyline of three points in a box of its own (like placeSegment).
void placePolyline3(lv_obj_t* line, lv_point_precise_t* pts, const float x[3], const float y[3],
                    int32_t width) {
  float minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
  for (int i = 1; i < 3; ++i) {
    minx = fminf(minx, x[i]);
    maxx = fmaxf(maxx, x[i]);
    miny = fminf(miny, y[i]);
    maxy = fmaxf(maxy, y[i]);
  }
  const int32_t x0 = static_cast<int32_t>(floorf(minx)) - width;
  const int32_t y0 = static_cast<int32_t>(floorf(miny)) - width;
  lv_obj_set_pos(line, x0, y0);
  lv_obj_set_size(line, static_cast<int32_t>(ceilf(maxx)) + width - x0,
                  static_cast<int32_t>(ceilf(maxy)) + width - y0);
  for (int i = 0; i < 3; ++i) {
    pts[i].x = static_cast<lv_value_precise_t>(x[i] - x0);
    pts[i].y = static_cast<lv_value_precise_t>(y[i] - y0);
  }
  lv_line_set_points(line, pts, 3);
}

}  // namespace

// ---------------------------------------------------------------------------
// Sentinel: a heads-up display. 72 ticks around the core rise with the voice
// like a round spectrum inside broken rings and a turning triangle; while
// thinking the rings spin up, an amber arc races around and a scan line
// sweeps; while answering the ticks pulse in waves. Number readouts on both
// sides (level, peak, hex, time).
// ---------------------------------------------------------------------------
namespace sentinel {

constexpr uint32_t kCyan = 0x5CE1FF;
constexpr uint32_t kAmber = 0xFFB547;
constexpr uint32_t kWhite = 0xE8FBFF;
constexpr int kTicks = 72;
constexpr int kDashes = 24;
constexpr int kHexRows = 4;
constexpr int kMeter = 10;
constexpr float kBoxR = 1.95f;  // the painted box's half size in r

lv_obj_t* g_painter;
lv_obj_t* g_callout[2];
lv_point_precise_t g_callout_pts[2][3];
lv_obj_t* g_number;
lv_obj_t* g_peak;
lv_obj_t* g_clock;
lv_obj_t* g_hex[kHexRows];
lv_obj_t* g_meter[kMeter];
char g_number_text[8];
char g_peak_text[12];
char g_clock_text[12];
char g_hex_text[kHexRows][8];
Frame g_f;
float g_answer;
float g_peak_level;
float g_last_r;
uint32_t g_tick;

void onDraw(lv_event_t* e) {
  const Pen p = pen(e);
  const float t = g_f.t, lv = g_f.level, m = g_f.mode, r = g_f.r, cx = g_f.cx, cy = g_f.cy;
  const float a = g_answer;
  const float think = fmaxf(0.0f, m - a);
  if (r < 2.0f) return;
  // Guide circle.
  drawArc(p, cx, cy, r * 1.66f, 0.0f, 360.0f, 1.0f, kCyan, 0.14f);
  // Tick spectrum.
  const float head = fmodf(t * 2.4f, kTwoPi);
  const float r0 = r * 1.45f;
  for (int i = 0; i < kTicks; ++i) {
    const float slot = i * kTwoPi / kTicks;
    const float ang = slot - kPi / 2.0f;
    const float freq = 3.0f + hash01(i, 1) * 6.0f;
    const float phase = hash01(i, 2) * kTwoPi;
    const float wave = (0.5f + 0.5f * sinf(t * freq + phase)) * (0.55f + 0.45f * sinf(ang * 4.0f + t * 3.0f));
    float diff = head - slot;
    if (diff < 0.0f) diff += kTwoPi;
    const float trail = diff < 1.7f ? 1.0f - diff / 1.7f : 0.0f;
    const float ans_wave = 0.5f + 0.5f * sinf(ang * 6.0f - t * 8.0f);
    const float len = r * (0.06f + (1.0f - m) * lv * wave * 0.42f + a * lv * ans_wave * 0.30f +
                           think * trail * 0.14f);
    const float alpha =
        0.3f + 0.7f * clamp01((1.0f - m) * (wave * lv * 1.4f + 0.15f) + think * trail +
                              a * (0.3f + 0.7f * lv * ans_wave));
    const uint32_t color = (1.0f - m) * lv * wave > 0.62f ? kAmber : (think * trail > 0.9f ? kWhite : kCyan);
    const float c = cosf(ang), s = sinf(ang);
    drawLine(p, cx + r0 * c, cy + r0 * s, cx + (r0 + len) * c, cy + (r0 + len) * s, 3.0f, color, alpha,
             false);
  }
  // Broken ring: four 62 degree arcs, faster while thinking.
  const float rot1 = t * (10.0f + 92.0f * think) + a * sinf(t * 2.0f) * 12.0f;
  for (int k = 0; k < 4; ++k) {
    const float s = rot1 + k * 90.0f;
    glowArc(p, cx, cy, r * 1.2f * (1.0f + 0.06f * lv), s, s + 62.0f, 3.0f, kCyan, 0.85f);
  }
  // Dashed inner ring, counter-turning.
  const float rot2 = -t * (6.0f + 57.0f * think);
  for (int k = 0; k < kDashes; ++k) {
    const float s = rot2 + k * (360.0f / kDashes);
    drawArc(p, cx, cy, r * 1.0f, s, s + 5.0f, 2.0f, kCyan, 0.55f, false);
  }
  // Amber racing arcs while thinking.
  const float rot3 = t * 240.0f;
  glowArc(p, cx, cy, r * 1.66f, rot3, rot3 + 34.0f, 4.0f, kAmber, think * 0.95f);
  glowArc(p, cx, cy, r * 1.66f, -rot3 * 0.7f, -rot3 * 0.7f + 20.0f, 2.0f, kAmber, think * 0.6f);
  // Scan line while thinking.
  const float hc = cosf(head - kPi / 2.0f), hs = sinf(head - kPi / 2.0f);
  glowLine(p, cx + r * 0.7f * hc, cy + r * 0.7f * hs, cx + r * 1.4f * hc, cy + r * 1.4f * hs, 2.0f, kWhite,
           think * 0.7f);
  // Core: outline, filled layers, a bright dot and the turning triangle.
  glowArc(p, cx, cy, r * (0.62f + 0.10f * lv * (1.0f - think)), 0.0f, 360.0f, 3.0f, kCyan, 0.9f);
  drawDisc(p, cx, cy, r * 0.50f * (1.0f + 0.45f * lv), kCyan, 0.12f + 0.25f * lv);
  drawDisc(p, cx, cy, r * 0.36f * (1.0f + 0.45f * lv), kCyan, 0.22f + 0.25f * lv);
  drawDisc(p, cx, cy, r * 0.22f * (1.0f + 0.45f * lv), kCyan, 0.50f + 0.25f * lv);
  drawDisc(p, cx, cy, r * (0.10f + 0.08f * lv), kWhite, 0.9f);
  const float tri = t * (0.3f + 2.2f * think) - kPi / 2.0f;
  float tx[3], ty[3];
  for (int k = 0; k < 3; ++k) {
    tx[k] = cx + r * 0.45f * cosf(tri + k * kTwoPi / 3.0f);
    ty[k] = cy + r * 0.45f * sinf(tri + k * kTwoPi / 3.0f);
  }
  for (int k = 0; k < 3; ++k) {
    const int n = (k + 1) % 3;
    glowLine(p, tx[k], ty[k], tx[n], ty[n], 2.0f, kWhite, 0.8f);
  }
}

// The callouts and readouts follow the radius while the screen pops in.
void layout(float cx, float cy, float r) {
  const float la = kPi * 1.08f, ra = -kPi * 0.08f;
  const float lx = cx + r * 1.66f * cosf(la), ly = cy + r * 1.66f * sinf(la);
  const float rx = cx + r * 1.66f * cosf(ra), ry = cy + r * 1.66f * sinf(ra);
  const float by = ly - 0.3f * r;
  const float left_x[3] = {lx, lx - 0.4f * r, cx - 3.15f * r};
  const float left_y[3] = {ly, by, by};
  const float right_x[3] = {rx, rx + 0.4f * r, cx + 3.15f * r};
  const float right_y[3] = {ry, ry - 0.3f * r, ry - 0.3f * r};
  placePolyline3(g_callout[0], g_callout_pts[0], left_x, left_y, 2);
  placePolyline3(g_callout[1], g_callout_pts[1], right_x, right_y, 2);
  const float bx = cx - 3.95f * r;
  lv_obj_set_pos(g_number, static_cast<int32_t>(bx), static_cast<int32_t>(by - 0.38f * r));
  lv_obj_set_pos(g_peak, static_cast<int32_t>(bx + 0.78f * r), static_cast<int32_t>(by - 0.2f * r));
  for (int k = 0; k < kMeter; ++k) {
    placePill(g_meter[k], bx + 0.03f * r + k * 0.08f * r, by + 0.24f * r, 0.05f * r, 0.14f * r);
    lv_obj_set_style_radius(g_meter[k], 0, 0);
  }
  const float hx = cx + 3.27f * r;
  for (int k = 0; k < kHexRows; ++k) {
    lv_obj_set_pos(g_hex[k], static_cast<int32_t>(hx), static_cast<int32_t>(ry - 0.38f * r + k * 0.18f * r));
  }
  lv_obj_set_pos(g_clock, static_cast<int32_t>(hx), static_cast<int32_t>(ry + 0.36f * r));
}

void create(lv_obj_t* parent) {
  g_painter = createPainter(parent, onDraw);
  for (int i = 0; i < 2; ++i) {
    g_callout[i] = createLine(parent, kCyan, 2);
    lv_obj_set_style_line_opa(g_callout[i], 150, 0);
    lv_obj_set_style_line_rounded(g_callout[i], false, 0);
  }
  g_number = createLabel(parent, &ui_font_32, kWhite, LV_TEXT_ALIGN_LEFT, 90);
  g_peak = createLabel(parent, &ui_font_16, kCyan, LV_TEXT_ALIGN_LEFT, 90);
  lv_obj_set_style_text_opa(g_peak, 180, 0);
  g_clock = createLabel(parent, &ui_font_16, kCyan, LV_TEXT_ALIGN_LEFT, 110);
  lv_obj_set_style_text_opa(g_clock, 150, 0);
  for (int k = 0; k < kHexRows; ++k) {
    g_hex[k] = createLabel(parent, &ui_font_16, k == kHexRows - 1 ? kAmber : kCyan, LV_TEXT_ALIGN_LEFT, 110);
    lv_obj_set_style_text_opa(g_hex[k], static_cast<lv_opa_t>(115 + k * 38), 0);
    snprintf(g_hex_text[k], sizeof(g_hex_text[k]), "%02X %02X", static_cast<unsigned>(lv_rand(0, 255)),
             static_cast<unsigned>(lv_rand(0, 255)));
    lv_label_set_text_static(g_hex[k], g_hex_text[k]);
  }
  for (int k = 0; k < kMeter; ++k) g_meter[k] = createBox(parent, k >= 8 ? kAmber : kCyan, 38);
  snprintf(g_number_text, sizeof(g_number_text), "000");
  snprintf(g_peak_text, sizeof(g_peak_text), "PK 000");
  snprintf(g_clock_text, sizeof(g_clock_text), "T+00.0");
  lv_label_set_text_static(g_number, g_number_text);
  lv_label_set_text_static(g_peak, g_peak_text);
  lv_label_set_text_static(g_clock, g_clock_text);
  g_answer = 0.0f;
  g_peak_level = 0.0f;
  g_last_r = -1.0f;
  g_tick = 0;
}

void frame(const Frame& f) {
  g_f = f;
  g_answer = answerMix(g_answer, f.answering);
  const float r = f.r, cx = f.cx, cy = f.cy, lv = f.level;
  invalidateBox(g_painter, cx, cy, r * kBoxR + 8.0f, r * kBoxR + 8.0f);
  if (fabsf(r - g_last_r) > 0.5f) {
    g_last_r = r;
    layout(cx, cy, r);
  }
  g_peak_level = fmaxf(lv, g_peak_level * 0.985f);
  const int lit = static_cast<int>(lv * kMeter + 0.5f);
  for (int k = 0; k < kMeter; ++k) lv_obj_set_style_bg_opa(g_meter[k], k < lit ? 230 : 38, 0);
  ++g_tick;
  if (g_tick % 4 == 0) {
    snprintf(g_number_text, sizeof(g_number_text), "%03d", static_cast<int>(lv * 100.0f));
    snprintf(g_peak_text, sizeof(g_peak_text), "PK %03d", static_cast<int>(g_peak_level * 100.0f));
    snprintf(g_clock_text, sizeof(g_clock_text), "T+%04.1f", static_cast<double>(fmodf(f.t, 100.0f)));
    lv_label_set_text_static(g_number, g_number_text);
    lv_label_set_text_static(g_peak, g_peak_text);
    lv_label_set_text_static(g_clock, g_clock_text);
  }
  const uint32_t hex_every = (f.mode - g_answer) > 0.5f ? 3 : 18;
  if (g_tick % hex_every == 0) {
    for (int k = 0; k + 1 < kHexRows; ++k) memcpy(g_hex_text[k], g_hex_text[k + 1], sizeof(g_hex_text[k]));
    snprintf(g_hex_text[kHexRows - 1], sizeof(g_hex_text[0]), "%02X %02X", static_cast<unsigned>(lv_rand(0, 255)),
             static_cast<unsigned>(lv_rand(0, 255)));
    for (int k = 0; k < kHexRows; ++k) lv_label_set_text_static(g_hex[k], g_hex_text[k]);
  }
}

}  // namespace sentinel

// ---------------------------------------------------------------------------
// Phosphor: a green oscilloscope. The voice draws the trace with fading
// afterglow copies behind it; while thinking the trace goes flat with a
// travelling blip and number rows print in the corner; while answering it
// curls into a Lissajous figure that grows with the reply.
// ---------------------------------------------------------------------------
namespace phosphor {

constexpr uint32_t kGreen = 0x39FF7A;
constexpr uint32_t kDim = 0x1B7A3C;
constexpr int kPoints = 64;
constexpr int kGlow = 3;  // older traces kept for the afterglow
constexpr int kRows = 5;
constexpr float kHalfW = 3.55f;  // the scope box in r
constexpr float kHalfH = 1.76f;

lv_obj_t* g_painter;
lv_obj_t* g_channel;
lv_obj_t* g_time;
lv_obj_t* g_plate;
lv_obj_t* g_rows[kRows];
char g_channel_text[16];
char g_time_text[16];
char g_row_text[kRows][20];
int16_t g_pts[kPoints][2];
int16_t g_old[kGlow][kPoints][2];
int g_old_count;
int g_rows_shown;
Frame g_f;
float g_answer;
float g_last_r;
float g_seed[3];
uint32_t g_tick;

void onDraw(lv_event_t* e) {
  const Pen p = pen(e);
  const float r = g_f.r, cx = g_f.cx, cy = g_f.cy;
  if (r < 2.0f) return;
  const float x0 = cx - kHalfW * r, x1 = cx + kHalfW * r;
  const float y0 = cy - kHalfH * r, y1 = cy + kHalfH * r;
  // Graticule: dotted rows (dashed lines), the centre cross with ticks and
  // the frame.
  const float step_x = (x1 - x0) / 10.0f;
  for (int gy = 0; gy <= 6; ++gy) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.base.layer = p.layer;
    dsc.color = lv_color_hex(kDim);
    dsc.opa = 150;
    dsc.width = 3;
    dsc.dash_width = 3;
    dsc.dash_gap = static_cast<int32_t>(step_x) - 3;
    const float y = y0 + gy * (y1 - y0) / 6.0f;
    dsc.p1.x = static_cast<lv_value_precise_t>(p.ox + x0 - 1.0f);
    dsc.p1.y = static_cast<lv_value_precise_t>(p.oy + y);
    dsc.p2.x = static_cast<lv_value_precise_t>(p.ox + x1 + 2.0f);
    dsc.p2.y = dsc.p1.y;
    lv_draw_line(p.layer, &dsc);
  }
  drawLine(p, x0, cy, x1, cy, 1.0f, kDim, 0.6f, false);
  drawLine(p, cx, y0, cx, y1, 1.0f, kDim, 0.6f, false);
  for (int k = 0; k <= 25; ++k) {
    const float x = x0 + k * (x1 - x0) / 25.0f;
    drawLine(p, x, cy - 4.0f, x, cy + 4.0f, 1.0f, kDim, 0.6f, false);
  }
  drawFrame(p, x0, y0, x1, y1, 1.0f, kDim, 0.6f);
  // Afterglow: the older traces, dimmer the older.
  constexpr float kOldAlpha[kGlow] = {0.30f, 0.18f, 0.10f};
  for (int g = 0; g < g_old_count; ++g) {
    for (int i = 1; i < kPoints; ++i) {
      drawLine(p, g_old[g][i - 1][0], g_old[g][i - 1][1], g_old[g][i][0], g_old[g][i][1], 2.0f, kGreen,
               kOldAlpha[g]);
    }
  }
  for (int i = 1; i < kPoints; ++i) {
    drawLine(p, g_pts[i - 1][0], g_pts[i - 1][1], g_pts[i][0], g_pts[i][1], 3.0f, kGreen, 1.0f);
  }
}

void layout(float cx, float cy, float r) {
  const float x0 = cx - kHalfW * r, x1 = cx + kHalfW * r, y0 = cy - kHalfH * r;
  lv_obj_set_pos(g_channel, static_cast<int32_t>(x0), static_cast<int32_t>(y0 - 0.32f * r));
  lv_obj_set_width(g_time, static_cast<int32_t>(2.2f * r));
  lv_obj_set_pos(g_time, static_cast<int32_t>(x1 - 2.2f * r), static_cast<int32_t>(y0 - 0.32f * r));
  lv_obj_set_pos(g_plate, static_cast<int32_t>(x0 + 0.1f * r), static_cast<int32_t>(y0 + 0.1f * r));
  lv_obj_set_size(g_plate, static_cast<int32_t>(2.3f * r), static_cast<int32_t>(1.35f * r));
  for (int k = 0; k < kRows; ++k) {
    lv_obj_set_pos(g_rows[k], static_cast<int32_t>(x0 + 0.18f * r), static_cast<int32_t>(y0 + 0.18f * r + k * 0.24f * r));
  }
}

void create(lv_obj_t* parent) {
  g_painter = createPainter(parent, onDraw);
  g_channel = createLabel(parent, &ui_font_16, kGreen, LV_TEXT_ALIGN_LEFT, 160);
  g_time = createLabel(parent, &ui_font_16, kGreen, LV_TEXT_ALIGN_RIGHT, 160);
  lv_obj_set_style_text_opa(g_channel, 220, 0);
  lv_obj_set_style_text_opa(g_time, 220, 0);
  g_plate = createBox(parent, 0x020503, 220);
  lv_obj_add_flag(g_plate, LV_OBJ_FLAG_HIDDEN);
  for (int k = 0; k < kRows; ++k) {
    g_rows[k] = createLabel(parent, &ui_font_16, kGreen, LV_TEXT_ALIGN_LEFT, 220);
    lv_obj_set_style_text_opa(g_rows[k], static_cast<lv_opa_t>(100 + k * 36), 0);
    lv_obj_add_flag(g_rows[k], LV_OBJ_FLAG_HIDDEN);
    g_row_text[k][0] = '\0';
  }
  snprintf(g_channel_text, sizeof(g_channel_text), "CH1  0.00 V");
  snprintf(g_time_text, sizeof(g_time_text), "0000.0 S");
  lv_label_set_text_static(g_channel, g_channel_text);
  lv_label_set_text_static(g_time, g_time_text);
  for (int i = 0; i < kPoints; ++i) g_pts[i][0] = g_pts[i][1] = 0;
  g_old_count = 0;
  g_rows_shown = 0;
  g_answer = 0.0f;
  g_last_r = -1.0f;
  for (float& s : g_seed) s = lv_rand(0, 628) / 100.0f;
  g_tick = 0;
}

void frame(const Frame& f) {
  g_f = f;
  g_answer = answerMix(g_answer, f.answering);
  const float t = f.t, lv = f.level, m = f.mode, r = f.r, cx = f.cx, cy = f.cy;
  const float a = g_answer;
  const float think = fmaxf(0.0f, m - a);
  if (fabsf(r - g_last_r) > 0.5f) {
    g_last_r = r;
    layout(cx, cy, r);
  }
  ++g_tick;
  // Every third frame the trace joins the afterglow.
  if (g_tick % 3 == 0) {
    for (int g = kGlow - 1; g > 0; --g) memcpy(g_old[g], g_old[g - 1], sizeof(g_old[g]));
    memcpy(g_old[0], g_pts, sizeof(g_old[0]));
    if (g_old_count < kGlow) ++g_old_count;
  }
  const float x0 = cx - kHalfW * r, x1 = cx + kHalfW * r;
  const float blip = fmodf(t * 0.6f, 1.2f) - 0.1f;
  const float sz = r * (0.7f + 0.9f * lv);
  for (int i = 0; i < kPoints; ++i) {
    const float u = static_cast<float>(i) / (kPoints - 1);
    const float env = sinf(kPi * u);
    const float w = sinf(u * 22.0f + t * 9.0f + g_seed[0]) * 0.6f + sinf(u * 47.0f - t * 13.0f + g_seed[1]) * 0.3f +
                    sinf(u * 91.0f + t * 21.0f + g_seed[2]) * 0.15f;
    const float lx = x0 + u * (x1 - x0);
    const float ly = cy - env * w * lv * 1.5f * r;
    const float bd = (u - blip) / 0.03f;
    const float ty = cy - expf(-bd * bd) * 0.6f * r * sinf((u - blip) * 120.0f);
    const float th = kTwoPi * u;
    const float ax = cx + sinf(3.0f * th + t * 0.9f) * sz * 1.5f;
    const float ay = cy + sinf(2.0f * th) * sz;
    g_pts[i][0] = static_cast<int16_t>((1.0f - m) * lx + think * lx + a * ax);
    g_pts[i][1] = static_cast<int16_t>((1.0f - m) * ly + think * ty + a * ay);
  }
  invalidateBox(g_painter, cx, cy, kHalfW * r + 6.0f, kHalfH * r + 6.0f);
  if (g_tick % 4 == 0) {
    snprintf(g_channel_text, sizeof(g_channel_text), "CH1  %.2f V", static_cast<double>(lv * 2.5f));
    snprintf(g_time_text, sizeof(g_time_text), "%06.1f S", static_cast<double>(fmodf(t, 1000.0f)));
    lv_label_set_text_static(g_channel, g_channel_text);
    lv_label_set_text_static(g_time, g_time_text);
    // Number rows print while thinking and clear when the look leaves.
    if (think > 0.4f) {
      if (g_rows_shown == kRows) {
        for (int k = 0; k + 1 < kRows; ++k) memcpy(g_row_text[k], g_row_text[k + 1], sizeof(g_row_text[k]));
        --g_rows_shown;
      }
      snprintf(g_row_text[g_rows_shown], sizeof(g_row_text[0]), "%04u  %04u  %04u",
               static_cast<unsigned>(lv_rand(0, 9999)), static_cast<unsigned>(lv_rand(0, 9999)),
               static_cast<unsigned>(lv_rand(0, 9999)));
      ++g_rows_shown;
    } else if (m < 0.2f && g_rows_shown > 0) {
      for (int k = 0; k + 1 < kRows; ++k) memcpy(g_row_text[k], g_row_text[k + 1], sizeof(g_row_text[k]));
      --g_rows_shown;
    }
    for (int k = 0; k < kRows; ++k) {
      if (k < g_rows_shown) {
        lv_label_set_text_static(g_rows[k], g_row_text[k]);
        lv_obj_remove_flag(g_rows[k], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(g_rows[k], LV_OBJ_FLAG_HIDDEN);
      }
    }
    visible(g_plate, g_rows_shown ? 1.0f : 0.0f);
  }
}

}  // namespace phosphor

// ---------------------------------------------------------------------------
// Light Disc: a glowing disc on a dark grid. Eight ring segments light with
// the voice; while thinking the disc turns and two light trails, cyan and
// orange, race the grid in right angles behind it; while answering the rings
// pulse with the reply.
// ---------------------------------------------------------------------------
namespace disc {

constexpr uint32_t kCyan = 0x5FF4FF;
constexpr uint32_t kOrange = 0xFF9A2E;
constexpr uint32_t kWhite = 0xEFFFFF;
constexpr uint32_t kBack = 0x05070F;
constexpr int kSegments = 8;
constexpr int kCorners = 12;
constexpr int kCycles = 2;
constexpr float kCellR = 0.63f;      // grid cell in r
constexpr float kTrailCells = 6.3f;  // a trail's length in cells
constexpr float kSpeed = 2.4f;       // r per second (about 8 px per frame)

struct Cycle {
  float x, y, dx, dy, tx, ty;
  float corners[kCorners][2];
  int count;
  lv_obj_t* lines[kCorners];
  lv_point_precise_t pts[kCorners][2];
  float placed[kCorners][4];  // the endpoints each line shows, to skip no-op moves
  lv_obj_t* head;
};

lv_obj_t* g_grid;
Cycle g_cycles[kCycles];
lv_obj_t* g_dark;
lv_obj_t* g_outer_glow;
lv_obj_t* g_outer;
lv_obj_t* g_seg[kSegments];
lv_obj_t* g_inner;
lv_obj_t* g_core;
float g_rot;
float g_answer;
float g_trail_alpha;
Frame g_f;
bool g_started;

void onDrawGrid(lv_event_t* e) {
  const Pen p = pen(e);
  const float gs = g_f.r * kCellR;
  if (gs < 4.0f) return;
  for (float x = 0.0f; x <= g_f.w; x += gs) drawLine(p, x, 0.0f, x, g_f.h, 1.0f, kCyan, 0.1f, false);
  for (float y = 0.0f; y <= g_f.h; y += gs) drawLine(p, 0.0f, y, g_f.w, y, 1.0f, kCyan, 0.1f, false);
}

bool outside(float x, float y, float gs) {
  return x < gs || x > g_f.w - gs || y < gs || y > g_f.h - gs;
}

void pushCorner(Cycle& c) {
  if (c.count == kCorners) {
    for (int i = 0; i + 1 < kCorners; ++i) {
      c.corners[i][0] = c.corners[i + 1][0];
      c.corners[i][1] = c.corners[i + 1][1];
    }
    --c.count;
  }
  c.corners[c.count][0] = c.x;
  c.corners[c.count][1] = c.y;
  ++c.count;
}

// Moves the cycle dist along the grid, turning at nodes now and then or when
// the edge is near.
void step(Cycle& c, float dist, float gs) {
  while (dist > 0.0f) {
    const float rem = fabsf(c.tx - c.x) + fabsf(c.ty - c.y);
    if (dist < rem) {
      c.x += c.dx * dist;
      c.y += c.dy * dist;
      return;
    }
    c.x = c.tx;
    c.y = c.ty;
    dist -= rem;
    float ndx = c.dx, ndy = c.dy;
    if (lv_rand(0, 99) < 30 || outside(c.x + ndx * gs, c.y + ndy * gs, gs)) {
      const float left[2] = {c.dy, -c.dx};
      const float right[2] = {-c.dy, c.dx};
      const bool left_ok = !outside(c.x + left[0] * gs, c.y + left[1] * gs, gs);
      const bool right_ok = !outside(c.x + right[0] * gs, c.y + right[1] * gs, gs);
      const float* pick = nullptr;
      if (left_ok && right_ok) pick = lv_rand(0, 1) ? left : right;
      else if (left_ok) pick = left;
      else if (right_ok) pick = right;
      if (pick) {
        ndx = pick[0];
        ndy = pick[1];
      } else {
        ndx = -c.dx;
        ndy = -c.dy;
      }
      if (ndx != c.dx || ndy != c.dy) pushCorner(c);
    }
    c.dx = ndx;
    c.dy = ndy;
    c.tx = c.x + ndx * gs;
    c.ty = c.y + ndy * gs;
  }
}

// Keeps the trail at most kTrailCells long, from the head backward.
void trim(Cycle& c, float gs) {
  const float max_len = kTrailCells * gs;
  float len = 0.0f;
  float px = c.x, py = c.y;
  for (int i = c.count - 1; i >= 0; --i) {
    const float seg = fabsf(px - c.corners[i][0]) + fabsf(py - c.corners[i][1]);
    if (len + seg > max_len) {
      const float k = seg > 0.0f ? (max_len - len) / seg : 0.0f;
      c.corners[i][0] = px + (c.corners[i][0] - px) * k;
      c.corners[i][1] = py + (c.corners[i][1] - py) * k;
      // Drop every corner before i.
      const int drop = i;
      for (int j = 0; j + drop < c.count; ++j) {
        c.corners[j][0] = c.corners[j + drop][0];
        c.corners[j][1] = c.corners[j + drop][1];
      }
      c.count -= drop;
      return;
    }
    len += seg;
    px = c.corners[i][0];
    py = c.corners[i][1];
  }
}

void showTrail(Cycle& c, float alpha) {
  const int32_t width = 3;
  for (int i = 0; i < kCorners; ++i) {
    if (i >= c.count || !visible(c.lines[i], alpha)) continue;
    const float x1 = c.corners[i][0], y1 = c.corners[i][1];
    const float x2 = i + 1 < c.count ? c.corners[i + 1][0] : c.x;
    const float y2 = i + 1 < c.count ? c.corners[i + 1][1] : c.y;
    lv_obj_set_style_line_opa(c.lines[i], opa(alpha), 0);
    float* was = c.placed[i];
    if (was[0] == x1 && was[1] == y1 && was[2] == x2 && was[3] == y2) continue;
    was[0] = x1;
    was[1] = y1;
    was[2] = x2;
    was[3] = y2;
    placeSegment(c.lines[i], c.pts[i], x1, y1, x2, y2, width);
  }
  if (visible(c.head, alpha)) {
    lv_obj_set_style_bg_opa(c.head, opa(alpha), 0);
    place(c.head, c.x, c.y, 8.0f);
  }
}

void create(lv_obj_t* parent) {
  g_grid = createPainter(parent, onDrawGrid);
  const uint32_t colors[kCycles] = {kCyan, kOrange};
  for (int c = 0; c < kCycles; ++c) {
    Cycle& cy = g_cycles[c];
    for (int i = 0; i < kCorners; ++i) {
      cy.lines[i] = createLine(parent, colors[c], 3);
      lv_obj_add_flag(cy.lines[i], LV_OBJ_FLAG_HIDDEN);
      for (float& v : cy.placed[i]) v = -1.0f;
    }
    cy.head = createBox(parent, kWhite, LV_OPA_COVER);
    lv_obj_add_flag(cy.head, LV_OBJ_FLAG_HIDDEN);
    cy.count = 0;
  }
  g_dark = createCircle(parent, kBack, LV_OPA_COVER);
  g_outer_glow = createSpinner(parent, 22, kCyan);
  g_outer = createSpinner(parent, 6, kCyan);
  for (int k = 0; k < kSegments; ++k) g_seg[k] = createSpinner(parent, 8, k % 2 ? kCyan : kWhite);
  g_inner = createSpinner(parent, 3, kCyan);
  g_core = createSpinner(parent, 3, kOrange);
  g_rot = 0.0f;
  g_answer = 0.0f;
  g_trail_alpha = 0.0f;
  g_started = false;
}

void frame(const Frame& f) {
  const bool size_changed = f.r != g_f.r || f.w != g_f.w || f.h != g_f.h;
  g_f = f;
  g_answer = answerMix(g_answer, f.answering);
  const float t = f.t, lv = f.level, m = f.mode, r = f.r, cx = f.cx, cy = f.cy;
  const float a = g_answer;
  const float think = fmaxf(0.0f, m - a);
  const float gs = r * kCellR;
  if (size_changed) lv_obj_invalidate(g_grid);
  // The light cycles start at grid nodes near two corners once the screen
  // has popped in, and ride while the thinking or answering look shows.
  if (!g_started && r > 4.0f && f.dt > 0.0f && f.t > 0.6f) {
    g_started = true;
    const float sx = floorf(2.0f * gs / gs) * gs;
    g_cycles[0].x = g_cycles[0].tx = sx;
    g_cycles[0].y = g_cycles[0].ty = sx;
    g_cycles[0].dx = 1.0f;
    g_cycles[0].dy = 0.0f;
    g_cycles[1].x = g_cycles[1].tx = floorf((f.w - 2.0f * gs) / gs) * gs;
    g_cycles[1].y = g_cycles[1].ty = floorf((f.h - 2.0f * gs) / gs) * gs;
    g_cycles[1].dx = -1.0f;
    g_cycles[1].dy = 0.0f;
    for (Cycle& c : g_cycles) {
      c.count = 0;
      pushCorner(c);
      c.tx = c.x + c.dx * gs;
      c.ty = c.y + c.dy * gs;
    }
  }
  if (g_started) {
    g_trail_alpha += (m - g_trail_alpha) * 0.15f;
    for (Cycle& c : g_cycles) {
      if (m > 0.05f) {
        step(c, f.dt * kSpeed * r, gs);
        trim(c, gs);
      }
      showTrail(c, g_trail_alpha);
    }
  }
  // The disc, pulsing with the voice; the dark fill hides the trails behind it.
  g_rot += f.dt * (12.0f + 140.0f * think);
  const float pulse = 1.0f + 0.08f * lv;
  place(g_dark, cx, cy, 2.0f * r * 1.62f * pulse);
  spin(g_outer_glow, cx, cy, 2.0f * r * 1.5f * pulse + 22.0f, 0.0f, 360.0f, 0.15f);
  spin(g_outer, cx, cy, 2.0f * r * 1.5f * pulse + 6.0f, 0.0f, 360.0f, 0.95f);
  const int lit = static_cast<int>(fmodf(t * 6.0f, static_cast<float>(kSegments)));
  for (int k = 0; k < kSegments; ++k) {
    const float band = 0.5f + 0.5f * sinf(t * (5.0f + k * 1.3f) + k * 2.0f);
    const float on = (1.0f - m) * clamp01(lv * 1.4f * band) + think * (k == lit ? 1.0f : 0.35f) +
                     a * (0.3f + 0.7f * lv * band);
    const float s = g_rot + k * (360.0f / kSegments);
    spin(g_seg[k], cx, cy, 2.0f * r * 1.12f * pulse + 8.0f, s + 3.5f, 360.0f / kSegments - 7.0f,
         0.2f + 0.8f * on);
  }
  spin(g_inner, cx, cy, 2.0f * r * 0.62f * pulse + 3.0f, 0.0f, 360.0f, 0.9f);
  spin(g_core, cx, cy, 2.0f * r * 0.32f * (1.0f + 0.4f * lv) + 3.0f, 0.0f, 360.0f, 0.3f + 0.6f * lv);
}

}  // namespace disc

// ---------------------------------------------------------------------------
// Lens: a calm red lens in a brushed-metal ring. The glow around the bright
// centre grows with the voice; while thinking it breathes slowly; while
// answering it brightens with each word. Nothing else moves.
// ---------------------------------------------------------------------------
namespace lens {

struct Layer {
  uint32_t color;
  float radius;  // in r
};
constexpr Layer kLayers[] = {{0x1A0303, 1.38f}, {0x3D0606, 1.18f}, {0x6E0B0A, 0.98f}, {0xA8120F, 0.78f},
                             {0xE2231B, 0.56f}, {0xFF5A32, 0.36f}, {0xFF9A55, 0.20f}, {0xFFE3A0, 0.09f}};
constexpr int kLayerCount = sizeof(kLayers) / sizeof(kLayers[0]);
struct Bezel {
  uint32_t color;
  float radius;  // outer edge, in r
  int32_t width;
};
constexpr Bezel kBezel[] = {{0x5A626E, 1.735f, 3}, {0x9AA3AF, 1.67f, 6}, {0xC9D0D8, 1.58f, 2}, {0x3A4048, 1.52f, 4}};
constexpr int kBezelCount = sizeof(kBezel) / sizeof(kBezel[0]);

lv_obj_t* g_plate;
lv_obj_t* g_bezel[kBezelCount];
lv_obj_t* g_layers[kLayerCount];
lv_obj_t* g_gleam[2];
float g_answer;

void create(lv_obj_t* parent) {
  g_plate = createCircle(parent, 0x0E1116, LV_OPA_COVER);
  for (int i = 0; i < kBezelCount; ++i) g_bezel[i] = createRing(parent, kBezel[i].color, kBezel[i].width);
  for (int i = 0; i < kLayerCount; ++i) g_layers[i] = createCircle(parent, kLayers[i].color, LV_OPA_COVER);
  g_gleam[0] = createSpinner(parent, 5, 0xFFFFFF);
  g_gleam[1] = createSpinner(parent, 3, 0xFFFFFF);
  g_answer = 0.0f;
}

void frame(const Frame& f) {
  g_answer = answerMix(g_answer, f.answering);
  const float t = f.t, lv = f.level, m = f.mode, r = f.r, cx = f.cx, cy = f.cy;
  const float a = g_answer;
  const float think = fmaxf(0.0f, m - a);
  const float g = (1.0f - m) * lv + think * (0.25f + 0.25f * sinf(t * 1.8f)) + a * (0.15f + 0.85f * lv);
  place(g_plate, cx, cy, 2.0f * r * 1.75f);
  for (int i = 0; i < kBezelCount; ++i) place(g_bezel[i], cx, cy, 2.0f * r * kBezel[i].radius);
  for (int i = 0; i < kLayerCount; ++i) {
    const float grow = i < 3 ? 1.0f : 1.0f + (0.15f + 0.12f * i / 8.0f) * g * 2.0f;
    place(g_layers[i], cx, cy, 2.0f * fminf(r * 1.45f, r * kLayers[i].radius * grow));
    const float alpha = i < 3 ? 1.0f : clamp01(0.55f + 0.45f * g + (i > 5 ? 0.3f : 0.0f));
    lv_obj_set_style_bg_opa(g_layers[i], opa(alpha), 0);
  }
  // Fixed reflections.
  spin(g_gleam[0], cx, cy, 2.0f * r * 1.22f + 5.0f, 198.0f, 50.0f, 0.22f);
  spin(g_gleam[1], cx, cy, 2.0f * r * 1.05f + 3.0f, 27.0f, 27.0f, 0.12f);
}

}  // namespace lens

// ---------------------------------------------------------------------------
// Iris: a close-up eye. The pupil opens with the voice; while thinking an
// inspection box hunts across the iris with crosshairs, a scan line and
// coordinates; while answering the pupil and fibers pulse with the reply.
// ---------------------------------------------------------------------------
namespace iris {

constexpr uint32_t kTeal = 0x3FD0C9;
constexpr uint32_t kAmber = 0xFF9D3C;
constexpr uint32_t kWhite = 0xF2F6F7;
constexpr int kFibers = 90;
constexpr float kIrisR = 1.6f;  // in r

lv_obj_t* g_fibers;  // painter under the pupil
lv_obj_t* g_rim;
lv_obj_t* g_pupil;
lv_obj_t* g_gleam;
lv_obj_t* g_brackets[8];
lv_point_precise_t g_bracket_pts[8][2];
lv_obj_t* g_scan;  // painter over the pupil: the inspection box
lv_obj_t* g_coords;
char g_coords_text[12];
Frame g_f;
float g_answer;
float g_pupil_r;
float g_rot;
float g_box_x, g_box_y, g_box_tx, g_box_ty, g_box_at;
float g_last_r;
bool g_scan_shown;
uint32_t g_tick;

void onDrawFibers(lv_event_t* e) {
  const Pen p = pen(e);
  const float r = g_f.r, cx = g_f.cx, cy = g_f.cy, lv = g_f.level;
  if (r < 2.0f) return;
  const float r0 = r * 0.3f, ri = r * kIrisR;
  const float pulse = g_answer * lv * 0.25f;
  for (int i = 0; i < kFibers; ++i) {
    const float ang = i * kTwoPi / kFibers + (hash01(i, 3) - 0.5f) * 0.05f + g_rot;
    const float len = 0.75f + hash01(i, 4) * 0.25f;
    const uint32_t color = hash01(i, 5) < 0.3f ? kAmber : kTeal;
    const float alpha = clamp01(0.35f + hash01(i, 6) * 0.5f + pulse);
    const float r1 = r0 + (ri - r0) * len;
    const float c = cosf(ang), s = sinf(ang);
    drawLine(p, cx + r0 * c, cy + r0 * s, cx + r1 * c, cy + r1 * s, 2.0f, color, alpha, false);
  }
}

void onDrawScan(lv_event_t* e) {
  const Pen p = pen(e);
  const float r = g_f.r, cx = g_f.cx, cy = g_f.cy, t = g_f.t;
  const float think = fmaxf(0.0f, g_f.mode - g_answer);
  if (r < 2.0f || think < 0.02f) return;
  const float ri = r * kIrisR;
  drawFrame(p, g_box_x - 0.4f * r, g_box_y - 0.3f * r, g_box_x + 0.4f * r, g_box_y + 0.3f * r, 2.0f, kWhite,
            think * 0.9f);
  drawLine(p, g_box_x, cy - ri - 0.1f * r, g_box_x, cy + ri + 0.1f * r, 1.0f, kWhite, think * 0.9f, false);
  drawLine(p, cx - ri - 0.1f * r, g_box_y, cx + ri + 0.1f * r, g_box_y, 1.0f, kWhite, think * 0.9f, false);
  const float sy = cy - ri + fmodf(t * 2.2f * r, 2.0f * ri);
  drawLine(p, cx - ri, sy, cx + ri, sy, 2.0f, kTeal, think * 0.6f, false);
}

void layout(float cx, float cy, float r) {
  const float ri = r * kIrisR;
  const float bx = ri + 0.3f * r, by = (ri + 0.3f * r) * 0.85f, len = 0.3f * r;
  const float sx[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
  const float sy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
  for (int k = 0; k < 4; ++k) {
    const float x = cx + sx[k] * bx, y = cy + sy[k] * by;
    placeSegment(g_brackets[k * 2], g_bracket_pts[k * 2], x, y, x - sx[k] * len, y, 2);
    placeSegment(g_brackets[k * 2 + 1], g_bracket_pts[k * 2 + 1], x, y, x, y - sy[k] * len, 2);
  }
  place(g_rim, cx, cy, 2.0f * (ri + 0.08f * r));
}

void create(lv_obj_t* parent) {
  g_fibers = createPainter(parent, onDrawFibers);
  g_rim = createRing(parent, 0x0B1A1C, 8);
  g_pupil = createCircle(parent, 0x020304, LV_OPA_COVER);
  g_gleam = createCircle(parent, kWhite, 215);
  for (int k = 0; k < 8; ++k) {
    g_brackets[k] = createLine(parent, kAmber, 2);
    lv_obj_set_style_line_opa(g_brackets[k], 205, 0);
    lv_obj_set_style_line_rounded(g_brackets[k], false, 0);
  }
  g_scan = createPainter(parent, onDrawScan);
  g_coords = createLabel(parent, &ui_font_16, kWhite, LV_TEXT_ALIGN_LEFT, 110);
  lv_obj_add_flag(g_coords, LV_OBJ_FLAG_HIDDEN);
  g_coords_text[0] = '\0';
  g_answer = 0.0f;
  g_pupil_r = 0.45f;
  g_rot = 0.0f;
  g_box_x = g_box_y = g_box_tx = g_box_ty = 0.0f;
  g_box_at = 0.0f;
  g_last_r = -1.0f;
  g_scan_shown = false;
  g_tick = 0;
}

void frame(const Frame& f) {
  // Against the radius last laid out, so the easing pop-in catches up (per
  // frame its steps fall under 0.5 px about 3 px short).
  const bool size_changed = fabsf(f.r - g_last_r) > 0.5f;
  if (size_changed) g_last_r = f.r;
  g_f = f;
  g_answer = answerMix(g_answer, f.answering);
  const float lv = f.level, m = f.mode, r = f.r, cx = f.cx, cy = f.cy;
  const float a = g_answer;
  const float think = fmaxf(0.0f, m - a);
  const float ri = r * kIrisR;
  if (size_changed) {
    layout(cx, cy, r);
    g_box_x = g_box_tx = cx;
    g_box_y = g_box_ty = cy;
  }
  // The fibers only change while they turn (thinking) or pulse (answering).
  g_rot += f.dt * 0.4f * think;
  if (size_changed || m > 0.01f) invalidateBox(g_fibers, cx, cy, ri + 4.0f, ri + 4.0f);
  // Pupil.
  const float target = (1.0f - m) * (0.42f + 0.3f * lv) + think * 0.32f + a * (0.36f + 0.26f * lv);
  g_pupil_r += (target - g_pupil_r) * 0.2f;
  const float rp = r * g_pupil_r;
  place(g_pupil, cx, cy, 2.0f * rp);
  place(g_gleam, cx - rp * 0.45f, cy - rp * 0.45f, 0.14f * r);
  // The inspection box hunts across the iris while thinking.
  g_box_at += f.dt;
  if (g_box_at > 0.7f) {
    g_box_at = 0.0f;
    const float an = lv_rand(0, 628) / 100.0f;
    const float rr = lv_rand(0, 100) / 100.0f * ri * 0.8f;
    g_box_tx = cx + rr * cosf(an);
    g_box_ty = cy + rr * sinf(an);
  }
  g_box_x += (g_box_tx - g_box_x) * 0.12f;
  g_box_y += (g_box_ty - g_box_y) * 0.12f;
  const bool scan = think > 0.02f;
  if (scan || g_scan_shown) invalidateBox(g_scan, cx, cy, ri + 0.12f * r, ri + 0.12f * r);
  g_scan_shown = scan;
  ++g_tick;
  if (visible(g_coords, think * 0.9f)) {
    lv_obj_set_pos(g_coords, static_cast<int32_t>(g_box_x + 0.45f * r), static_cast<int32_t>(g_box_y - 0.42f * r));
    if (g_tick % 4 == 0) {
      snprintf(g_coords_text, sizeof(g_coords_text), "%04d %04d", static_cast<int>(g_box_x),
               static_cast<int>(g_box_y));
      lv_label_set_text_static(g_coords, g_coords_text);
    }
  }
}

}  // namespace iris

}  // namespace assist_styles
