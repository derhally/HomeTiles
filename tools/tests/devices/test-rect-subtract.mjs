// Host regression for src/devices/common/rect_subtract.h: the pieces of a
// rectangle outside a set of covers, used by the 7B's tear-free path to copy
// into the hidden framebuffer only what the running LVGL refresh does not
// draw anyway. Checked against a pixel count over random cases, and the
// presenter and display manager wiring is checked in the sources.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

// --- wiring ---
const presenter = read('src/devices/common/p4_dsi_camera_presenter.cpp');
assert.match(presenter, /#include "src\/devices\/common\/rect_subtract\.h"/);
assert.match(presenter, /ui_refresh_areas_ \? ui_refresh_areas_\(cover_boxes, kMaxCovers\) : 0/);
assert.match(presenter, /rect_subtract::outside\(/);
const header = read('src/devices/common/p4_dsi_camera_presenter.h');
assert.match(header, /void setUiRefreshAreas\(UiRefreshAreasFn fn\)/);
const display = read('src/core/display/display_manager.cpp');
assert.match(display, /uint8_t DisplayManager::refreshAreas\(int32_t \(\*out\)\[4\], uint8_t max\)/);
assert.match(display, /lv_refr_get_disp_refreshing\(\)/);
assert.match(display, /inv_area_joined\[i\]/);
const device = read('src/devices/waveshare_touch_lcd_7b/device_waveshare_touch_lcd_7b.cpp');
assert.match(device, /g_camera_presenter\.setUiRefreshAreas\(&refresh_areas_on_panel\);/);
// The 180 degree rotation mirrors the bands, so the areas are mirrored too.
assert.match(device, /uint8_t refresh_areas_on_panel\(int32_t \(\*out\)\[4\], uint8_t max\) \{[\s\S]*?if \(!\(g_rotation & 0x02\)\) return n;[\s\S]*?out\[i\]\[0\] = w - 1 - x2;/);

// --- the subtraction itself ---
const cxx = [process.env.CXX, 'clang++', 'g++'].filter(Boolean).find(c => spawnSync(c, ['--version']).status === 0);
if (!cxx) {
  console.log('SKIP: rect subtract host check needs a C++ compiler');
  process.exit(0);
}
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'rect-subtract-'));
const main = path.join(work, 'main.cpp');
fs.writeFileSync(main, `#include "src/devices/common/rect_subtract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using rect_subtract::Box;
static uint32_t seed = 12345;
static int rnd(int lo, int hi) { seed = seed * 1103515245u + 12345u; return lo + (seed >> 8) % (hi - lo + 1); }
int main() {
  Box out[64];
  // Disjoint: the box itself. Covered: nothing. A hole: four pieces.
  Box a{10, 10, 20, 20};
  if (rect_subtract::outside(a, &a, 1, out, 64) != 0) { printf("FAIL covered\\n"); return 1; }
  Box far{30, 30, 40, 40};
  if (rect_subtract::outside(a, &far, 1, out, 64) != 1 || memcmp(&out[0], &a, sizeof(a))) { printf("FAIL disjoint\\n"); return 1; }
  Box hole{13, 13, 17, 17};
  if (rect_subtract::outside(a, &hole, 1, out, 64) != 4) { printf("FAIL hole\\n"); return 1; }
  if (rect_subtract::outside(a, &hole, 1, out, 3) != -1) { printf("FAIL overflow\\n"); return 1; }
  // Random cases against a pixel grid: the pieces cover exactly the pixels of
  // the box no cover holds, without overlapping each other.
  for (int t = 0; t < 2000; ++t) {
    Box box{rnd(0, 40), rnd(0, 40), 0, 0};
    box.x2 = box.x1 + rnd(0, 30); box.y2 = box.y1 + rnd(0, 30);
    const int nc = rnd(0, 5);
    Box covers[5];
    for (int c = 0; c < nc; ++c) { covers[c] = Box{rnd(-5, 60), rnd(-5, 60), 0, 0}; covers[c].x2 = covers[c].x1 + rnd(0, 40); covers[c].y2 = covers[c].y1 + rnd(0, 40); }
    const int n = rect_subtract::outside(box, covers, nc, out, 64);
    static int grid[80][80];
    memset(grid, 0, sizeof(grid));
    if (n < 0) continue;  // too many pieces: the caller copies the whole box
    for (int i = 0; i < n; ++i) {
      if (rect_subtract::empty(out[i])) { printf("FAIL empty piece\\n"); return 1; }
      for (int y = out[i].y1; y <= out[i].y2; ++y) for (int x = out[i].x1; x <= out[i].x2; ++x) grid[y + 5][x + 5]++;
    }
    for (int y = -5; y < 75; ++y) for (int x = -5; x < 75; ++x) {
      bool in = x >= box.x1 && x <= box.x2 && y >= box.y1 && y <= box.y2;
      for (int c = 0; c < nc && in; ++c) if (x >= covers[c].x1 && x <= covers[c].x2 && y >= covers[c].y1 && y <= covers[c].y2) in = false;
      if (grid[y + 5][x + 5] != (in ? 1 : 0)) { printf("FAIL case %d at %d,%d: %d\\n", t, x, y, grid[y + 5][x + 5]); return 1; }
    }
  }
  printf("ok\\n");
  return 0;
}
`);
const exe = path.join(work, 'rects');
const build = spawnSync(cxx, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root, main, '-o', exe], {encoding: 'utf8'});
assert.equal(build.status, 0, build.stdout + build.stderr);
const run = spawnSync(exe, [], {encoding: 'utf8'});
fs.rmSync(work, {recursive: true, force: true});
assert.equal(run.status, 0, run.stdout + run.stderr);
assert.equal(run.stdout.trim(), 'ok');
console.log('rect subtract: ok');
