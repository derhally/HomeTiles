// Host regression for the Assist screens' direct rasterizer
// (src/ui/assist/assist_raster.h). It draws lines and arcs into plain RGB565,
// byte-swapped RGB565 and XRGB8888 buffers through target()/line()/arc() with
// a clip smaller than the buffer, and checks that nothing outside the clip
// changes, that the shapes land where lv_draw_line and lv_draw_arc put them
// (a filled sector has no hole, a ring has one, a line's middle is solid) and
// that the swapped buffer is the RGB565 one byte-swapped. It then renders a set
// of shapes through a real LVGL display in bands of a few rows, once with the
// rasterizer and once with lv_draw_line/lv_draw_arc, and compares the pictures:
// they may differ in edge pixels only.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: assist raster test needs LVGL 9.6.0 (LVGL_INCLUDE) and a host compiler');
  process.exit(0);
}

const out = path.join(root, 'build/tests/assist-raster');
fs.mkdirSync(out, {recursive: true});
const main = path.join(out, 'main.cpp');
fs.writeFileSync(main, `#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "src/ui/assist/assist_raster.h"

// ---- plain buffers ----
struct Plain {
  lv_draw_buf_t db;
  lv_layer_t layer;
  std::vector<uint8_t> mem;
  int w, h, bpp;
};
static void make(Plain& b, lv_color_format_t cf, int w, int h, lv_area_t clip, uint32_t fill) {
  b.w = w; b.h = h; b.bpp = cf == LV_COLOR_FORMAT_XRGB8888 ? 4 : 2;
  b.mem.assign(static_cast<size_t>(w) * h * b.bpp, 0);
  for (int i = 0; i < w * h; ++i) {
    if (b.bpp == 4) reinterpret_cast<uint32_t*>(b.mem.data())[i] = fill;
    else if (cf == LV_COLOR_FORMAT_RGB565_SWAPPED) reinterpret_cast<uint16_t*>(b.mem.data())[i] = __builtin_bswap16(static_cast<uint16_t>(fill));
    else reinterpret_cast<uint16_t*>(b.mem.data())[i] = static_cast<uint16_t>(fill);
  }
  memset(&b.db, 0, sizeof(b.db));
  b.db.header.magic = LV_IMAGE_HEADER_MAGIC; b.db.header.cf = cf; b.db.header.w = w; b.db.header.h = h; b.db.header.stride = w * b.bpp;
  b.db.data = b.mem.data(); b.db.data_size = b.mem.size();
  memset(&b.layer, 0, sizeof(b.layer));
  b.layer.draw_buf = &b.db;
  b.layer.buf_area = lv_area_t{10, 20, 10 + w - 1, 20 + h - 1};  // the band sits at (10, 20) on the screen
  b.layer._clip_area = clip;
}
static uint32_t at(const Plain& b, int sx, int sy) {  // screen coordinates
  const int i = (sy - 20) * b.w + (sx - 10);
  if (b.bpp == 4) return reinterpret_cast<const uint32_t*>(b.mem.data())[i] & 0xFFFFFF;
  const uint16_t v = reinterpret_cast<const uint16_t*>(b.mem.data())[i];
  return b.db.header.cf == LV_COLOR_FORMAT_RGB565_SWAPPED ? __builtin_bswap16(v) : v;
}
static void shapes(const assist_raster::Target& t, lv_color_t c) {
  assist_raster::line(t, 20, 30, 150, 110, 3, c, 255, true);      // skew, round
  assist_raster::line(t, 30, 125, 170, 125, 2, c, 255, false);    // horizontal, even width
  assist_raster::line(t, 175, 25, 175, 130, 1, c, 255, false);    // vertical, thin
  assist_raster::arc(t, 100, 80, 40, 6, 30, 200, c, 255, true);   // a sector with round ends
  assist_raster::arc(t, 100, 80, 30, 3, 0, 360, c, 255, false);   // a full ring
  assist_raster::arc(t, 50, 60, 12, 12, 0, 360, c, 255, false);   // a filled disc (width = radius)
  assist_raster::arc(t, 150, 50, 14, 20, 300, 60, c, 128, false); // a sector past 360, half opaque
  assist_raster::disc(t, 20, 90, 40, 110, c, 255, false, c);        // a plain disc
  assist_raster::disc(t, 150, 90, 180, 120, lv_color_hex(0xFF0000), 255, true, lv_color_hex(0x0000FF)); // shaded
}
static int plain() {
  const lv_color_t c = lv_color_hex(0x33CCFF);
  const uint32_t bg565 = 0x0841, bg888 = 0x05070F;
  const lv_area_t clip{14, 24, 190, 140};
  Plain p565, pswap, p888, wide_clip;
  make(p565, LV_COLOR_FORMAT_RGB565, 200, 130, clip, bg565);
  make(pswap, LV_COLOR_FORMAT_RGB565_SWAPPED, 200, 130, clip, bg565);
  make(p888, LV_COLOR_FORMAT_XRGB8888, 200, 130, clip, bg888);
  make(wide_clip, LV_COLOR_FORMAT_RGB565, 200, 130, lv_area_t{0, 0, 1000, 1000}, bg565);
  assist_raster::Target t565, tswap, t888, twide;
  if (!assist_raster::target(&p565.layer, &t565) || !assist_raster::target(&pswap.layer, &tswap) ||
      !assist_raster::target(&p888.layer, &t888) || !assist_raster::target(&wide_clip.layer, &twide)) {
    printf("FAIL target\\n");
    return 1;
  }
  // The clip is cut to the buffer.
  if (twide.clip.x1 != 10 || twide.clip.y1 != 20 || twide.clip.x2 != 209 || twide.clip.y2 != 149) {
    printf("FAIL clip %d,%d-%d,%d\\n", (int)twide.clip.x1, (int)twide.clip.y1, (int)twide.clip.x2, (int)twide.clip.y2);
    return 1;
  }
  shapes(t565, c); shapes(tswap, c); shapes(t888, c);
  // Nothing outside the clip, something inside.
  int outside = 0, inside = 0;
  for (int y = 20; y < 150; ++y) for (int x = 10; x < 210; ++x) {
    const bool in = x >= clip.x1 && x <= clip.x2 && y >= clip.y1 && y <= clip.y2;
    if (at(p565, x, y) != bg565) { if (in) ++inside; else ++outside; }
    if (at(p888, x, y) != bg888 && !in) ++outside;
  }
  printf("outside %d inside %d\\n", outside, inside);
  // The swapped buffer is the plain one byte-swapped.
  for (int i = 0; i < 200 * 130; ++i) {
    const uint16_t a = reinterpret_cast<const uint16_t*>(p565.mem.data())[i];
    const uint16_t b = reinterpret_cast<const uint16_t*>(pswap.mem.data())[i];
    if (a != __builtin_bswap16(b)) { printf("FAIL swapped at %d\\n", i); return 1; }
  }
  const uint16_t c565 = lv_color_to_u16(c);
  auto solid = [&](int x, int y) { return at(p565, x, y) == c565; };
  // The filled disc has no hole; the ring has one; a line's middle is solid;
  // the half-opaque sector is neither background nor the colour.
  // (100, 62) lies in both rings' holes, away from the skew line through the centre.
  // The shaded disc: red at its top row, blue at its bottom, its corner untouched.
  const uint32_t top = at(p888, 165, 90), low = at(p888, 165, 120), corner = at(p888, 150, 90);
  printf("disc_centre %d ring_hole_bg %d ring_body %d line_mid %d hor_rows %d%d%d ver %d%d%d half %d plain %d shade %d\\n",
         solid(50, 60), at(p565, 100, 62) == bg565, solid(100 + 30 - 1, 80), solid(85, 70),
         solid(100, 124), solid(100, 125), solid(100, 126), solid(174, 70), solid(175, 70), solid(176, 70),
         at(p565, 150 + 10, 50) != bg565 && !solid(150 + 10, 50), solid(30, 100) && at(p565, 21, 91) == bg565,
         (top >> 16) > 0xE0 && (top & 0xFF) < 0x20 && (low & 0xFF) > 0xE0 && (low >> 16) < 0x20 && corner == bg888);
  return 0;
}

// ---- through a banded LVGL display: rasterizer against lv_draw_* ----
static bool g_direct = false;
static std::vector<uint32_t> g_fb;
static int g_w = 240, g_h = 160;
static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const uint32_t* src = reinterpret_cast<const uint32_t*>(px);
  for (int y = a->y1; y <= a->y2; ++y) for (int x = a->x1; x <= a->x2; ++x) g_fb[y * g_w + x] = *src++ & 0xFFFFFF;
  lv_display_flush_ready(d);
}
struct Shape { int kind; int a, b, c, d, w, s, e; bool round; uint8_t opa; };  // kind 2: disc (a,b centre, c radius, d = shaded)
static const Shape kShapes[] = {
  {0, 20, 20, 220, 140, 1, 0, 0, false, 255}, {0, 20, 140, 220, 20, 3, 0, 0, true, 255},
  {0, 120, 10, 125, 150, 2, 0, 0, false, 255}, {0, 10, 80, 230, 84, 5, 0, 0, true, 160},
  {0, 60, 30, 180, 36, 7, 0, 0, false, 255}, {1, 120, 80, 50, 4, 0, 360, 0, false, 255},
  {1, 120, 80, 60, 3, 20, 110, 0, true, 255}, {1, 120, 80, 70, 8, 200, 340, 0, false, 200},
  {1, 60, 120, 25, 25, 0, 360, 0, false, 255}, {1, 180, 120, 20, 6, 350, 30, 0, true, 255},
  {2, 40, 40, 18, 0, 0, 0, 0, false, 255}, {2, 200, 60, 30, 1, 0, 0, 0, false, 200}, {2, 120, 80, 12, 1, 0, 0, 0, false, 255},
};
static void paint(lv_event_t* e) {
  lv_layer_t* layer = lv_event_get_layer(e);
  const lv_color_t c = lv_color_hex(0x5CE1FF);
  assist_raster::Target t;
  if (g_direct) {
    if (!assist_raster::target(layer, &t)) { printf("FAIL no target\\n"); return; }
  }
  for (const Shape& s : kShapes) {
    if (s.kind == 0) {
      if (g_direct) {
        assist_raster::line(t, s.a, s.b, s.c, s.d, s.w, c, s.opa, s.round);
      } else {
        lv_draw_line_dsc_t dsc; lv_draw_line_dsc_init(&dsc); dsc.base.layer = layer; dsc.color = c; dsc.opa = s.opa;
        dsc.width = s.w; dsc.round_start = dsc.round_end = s.round;
        dsc.p1.x = s.a; dsc.p1.y = s.b; dsc.p2.x = s.c; dsc.p2.y = s.d;
        lv_draw_line(layer, &dsc);
      }
    } else if (s.kind == 2) {
      const lv_color_t low = lv_color_hex(0x0E7490);
      if (g_direct) {
        assist_raster::disc(t, s.a - s.c, s.b - s.c, s.a + s.c, s.b + s.c, c, s.opa, s.d != 0, low);
      } else {
        lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc); dsc.base.layer = layer; dsc.bg_color = c; dsc.bg_opa = s.opa;
        dsc.radius = LV_RADIUS_CIRCLE;
        if (s.d) { const lv_color_t stops[2] = {c, low}; lv_grad_init_stops(&dsc.bg_grad, stops, nullptr, nullptr, 2); lv_grad_vertical_init(&dsc.bg_grad); }
        lv_area_t area{s.a - s.c, s.b - s.c, s.a + s.c, s.b + s.c};
        lv_draw_rect(layer, &dsc, &area);
      }
    } else if (g_direct) {
      assist_raster::arc(t, s.a, s.b, s.c, s.d, s.s, s.e, c, s.opa, s.round);
    } else {
      lv_draw_arc_dsc_t dsc; lv_draw_arc_dsc_init(&dsc); dsc.base.layer = layer; dsc.color = c; dsc.opa = s.opa;
      dsc.width = s.d; dsc.rounded = s.round; dsc.center.x = s.a; dsc.center.y = s.b; dsc.radius = s.c;
      dsc.start_angle = s.s; dsc.end_angle = s.e;
      lv_draw_arc(layer, &dsc);
    }
  }
}
static std::vector<uint32_t> render(bool direct) {
  g_direct = direct;
  g_fb.assign(static_cast<size_t>(g_w) * g_h, 0);
  lv_display_t* d = lv_display_create(g_w, g_h);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
  static uint32_t band[240 * 7];  // seven rows: the shapes cross many band edges
  lv_display_set_buffers(d, band, nullptr, sizeof(band), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d, flush);
  lv_obj_t* scr = lv_display_get_screen_active(d);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x05070F), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_t* o = lv_obj_create(scr);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
  lv_obj_add_event_cb(o, paint, LV_EVENT_DRAW_MAIN, nullptr);
  lv_refr_now(d);
  std::vector<uint32_t> copy = g_fb;
  lv_display_delete(d);
  return copy;
}
int main() {
  lv_init();
  if (plain() != 0) return 1;
  const std::vector<uint32_t> fast = render(true), slow = render(false);
  int differ = 0, drawn_fast = 0, drawn_slow = 0, far = 0;
  for (int y = 0; y < g_h; ++y) for (int x = 0; x < g_w; ++x) {
    const uint32_t a = fast[y * g_w + x], b = slow[y * g_w + x];
    int d = 0;
    for (int s = 0; s < 24; s += 8) d += abs(static_cast<int>((a >> s) & 0xFF) - static_cast<int>((b >> s) & 0xFF));
    if (d > 60) ++differ;
    if (a != 0x05070F) ++drawn_fast;
    if (b != 0x05070F) ++drawn_slow;
    // A clearly different pixel must have a drawn neighbour in the other
    // picture: differences sit on edges, not elsewhere.
    if (d > 60) {
      bool near = false;
      for (int dy = -1; dy <= 1 && !near; ++dy) for (int dx = -1; dx <= 1 && !near; ++dx) {
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= g_w || ny >= g_h) continue;
        if (fast[ny * g_w + nx] != 0x05070F && slow[ny * g_w + nx] != 0x05070F) near = true;
      }
      if (!near) ++far;
    }
  }
  printf("differ %d drawn_fast %d drawn_slow %d far %d total %d\\n", differ, drawn_fast, drawn_slow, far, g_w * g_h);
  return 0;
}
`);
const binary = path.join(out, process.platform === 'win32' ? 'raster.exe' : 'raster');
const build = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', '-Wall', '-Wno-unused-function', '-Wno-missing-field-initializers',
  '-I', root, main, path.join(root, 'src/ui/assist/assist_raster.cpp'), host.archive, '-lm', '-o', binary], {encoding: 'utf8'});
assert.equal(build.status, 0, build.stdout + build.stderr);
assert.ok(!/warning:/.test(build.stdout + build.stderr), build.stdout + build.stderr);
const run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
assert.ok(!run.stdout.includes('FAIL'), run.stdout);
const lines = Object.fromEntries(run.stdout.trim().split('\n').map(l => [l.split(' ')[0], l]));
const [, outside, , inside] = lines.outside.split(' ');
assert.equal(Number(outside), 0, 'no pixel outside the clip changed');
assert.ok(Number(inside) > 1500, `shapes drawn inside the clip (${inside} px)`);
assert.equal(lines.disc_centre, 'disc_centre 1 ring_hole_bg 1 ring_body 1 line_mid 1 hor_rows 110 ver 010 half 1 plain 1 shade 1',
  'the shapes land like lv_draw_line / lv_draw_arc would put them');
const m = lines.differ.match(/differ (\d+) drawn_fast (\d+) drawn_slow (\d+) far (\d+) total (\d+)/);
const [differ, drawnFast, drawnSlow, far, total] = m.slice(1).map(Number);
// Only the anti-aliased edges differ (the scene is dense: most drawn pixels are edge pixels).
assert.ok(differ / drawnSlow < 0.3, `${(100 * differ / drawnSlow).toFixed(1)} % of the drawn pixels differ clearly from lv_draw_*`);
assert.ok(Math.abs(drawnFast - drawnSlow) / drawnSlow < 0.08, `drawn pixels ${drawnFast} vs ${drawnSlow}`);
assert.equal(far, 0, 'every clear difference sits on an edge both pictures draw');
console.log(`assist raster: ok (${(100 * differ / drawnSlow).toFixed(1)} % of the drawn pixels are edges that differ from lv_draw_*, ${drawnFast} vs ${drawnSlow} px drawn of ${total})`);
