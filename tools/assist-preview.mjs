// Renders the Assist screen styles (src/ui/assist/assist_styles*.cpp) with
// real LVGL on the host, the way the panel draws them: the style's objects on
// the overlay's backdrop at 1024x600, driven through listening, thinking and
// answering with the overlay's simulated voice, and a frame of each phase
// saved as a PNG.
//
// Usage: node tools/assist-preview.mjs [style ...] [--frames] [--size=WxH]
//   style    atom reactor dot_halo voice_pills sentinel phosphor light_disc
//            lens iris (default: all)
//   --frames also prints the host's draw time per frame of each phase, as a
//            rough cost comparison between the styles
//   --size   the screen, default 1024x600; every style renders, also one the
//            panel would not offer at that size (assist_style_catalog.h)
//
// Output: build/assist-preview/<style>-<phase>.png
// Needs LVGL 9.6.0 and a host compiler (tools/lib/lvgl-host.mjs); prints SKIP
// without them.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from './lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const option = name => args.find(a => a.startsWith(`--${name}=`))?.slice(name.length + 3);
const STYLES = ['atom', 'reactor', 'dot_halo', 'voice_pills', 'sentinel', 'phosphor', 'light_disc', 'lens', 'iris'];
const selected = args.filter(a => !a.startsWith('--'));
for (const s of selected) if (!STYLES.includes(s)) throw new Error(`unknown style ${s}`);
const styles = selected.length ? selected : STYLES;
const [W, H] = (option('size') || '1024x600').split('x').map(Number);
const PHASES = ['listening', 'thinking', 'answering'];
const out = path.join(root, 'build/assist-preview');
fs.mkdirSync(out, {recursive: true});

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the Assist preview needs LVGL 9.6 and a host compiler');
  process.exit(0);
}

// speaker.h (the style enum) includes Arduino.h; the styles need none of it.
const stubs = path.join(out, 'stubs');
fs.mkdirSync(stubs, {recursive: true});
fs.writeFileSync(path.join(stubs, 'Arduino.h'), '#pragma once\n#include <stdint.h>\n#include <stddef.h>\n');

const harness = String.raw`
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <vector>
#include "src/ui/assist/assist_styles.cpp"
#include "src/ui/assist/assist_styles_lines.cpp"

using speaker::AssistScreenStyle;
static const int kW = ${W}, kH = ${H};

// The overlay's simulated voice (assist_overlay.cpp simulatedLevel), with a
// fixed seed so every run draws the same frames.
static uint32_t g_seed = 12345;
static uint32_t rnd(uint32_t lo, uint32_t hi) {
  g_seed = g_seed * 1103515245u + 12345u;
  return lo + (g_seed >> 8) % (hi - lo + 1);
}

int main(int argc, char** argv) {
  if (argc < 4) return 2;
  const int style = atoi(argv[1]);
  const char* raw = argv[2];
  const bool timing = argv[3][0] == '1';
  lv_init();
  lv_display_t* display = lv_display_create(kW, kH);
  std::vector<uint32_t> pixels(kW * kH), band(kW * 32);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, band.data(), nullptr, band.size() * 4, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(display, &pixels);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t* area, uint8_t* data) {
    auto& target = *static_cast<std::vector<uint32_t>*>(lv_display_get_user_data(d));
    auto* source = reinterpret_cast<uint32_t*>(data);
    for (int y = area->y1; y <= area->y2; ++y)
      for (int x = area->x1; x <= area->x2; ++x) target[y * kW + x] = *source++;
    lv_display_flush_ready(d);
  });
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070F), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_t* stage = lv_obj_create(screen);
  lv_obj_remove_style_all(stage);
  lv_obj_set_size(stage, LV_PCT(100), LV_PCT(100));
  lv_obj_t* label = lv_label_create(screen);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, &ui_font_28, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(assist_styles::labelColor(static_cast<AssistScreenStyle>(style))), 0);
  assist_styles::create(static_cast<AssistScreenStyle>(style), stage);
  lv_timer_handler();

  // The overlay's frame loop: 33 ms frames, eased level, mode and pop-in.
  const float base_r = assist_styles::baseRadius(static_cast<AssistScreenStyle>(style), kW, kH);
  float level = 0, mode = 0, appear = 0, sim_target = 0, last_t = 0;
  uint32_t sim_next = 0, now = 0;
  const char* texts[3] = {"Listening…", "Thinking…", "Answering…"};
  // 5 s listening, 3 s thinking, 4 s answering, 33 ms a frame; the shot of
  // each phase comes late in it, when the look has settled.
  const int frames[3] = {150, 90, 120};
  const int shot[3] = {120, 80, 100};
  FILE* file = fopen(raw, "wb");
  if (!file) return 3;
  for (int phase = 0; phase < 3; ++phase) {
    lv_label_set_text(label, texts[phase]);
    double total_us = 0;
    for (int i = 0; i < frames[phase]; ++i) {
      now += 33;
      lv_tick_inc(33);
      const float t = now / 1000.0f;
      if (now >= sim_next) {
        sim_next = now + 110 + rnd(0, 170);
        sim_target = rnd(0, 99) < 22 ? 0.0f : 0.25f + rnd(0, 75) / 100.0f;
      }
      const float target_level = phase == 1 ? 0.0f : sim_target;
      level += (target_level - level) * (target_level > level ? 0.55f : 0.12f);
      mode += ((phase == 0 ? 0.0f : 1.0f) - mode) * 0.12f;
      appear += (1.0f - appear) * 0.18f;
      assist_styles::Frame f;
      f.t = t;
      f.level = level;
      f.mode = mode;
      f.answering = phase == 2;
      f.cx = kW / 2.0f;
      f.cy = kH / 2.0f - kH * 0.05f;
      f.r = base_r * appear;
      f.w = kW;
      f.h = kH;
      const float dt = t - last_t;
      f.dt = dt > 0.05f ? 0.05f : dt;
      last_t = t;
      assist_styles::frame(static_cast<AssistScreenStyle>(style), f);
      lv_obj_set_y(label, static_cast<int32_t>(f.cy + base_r * 2.1f));
      const auto start = std::chrono::steady_clock::now();
      lv_timer_handler();
      total_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
      if (i == shot[phase]) fwrite(pixels.data(), 4, pixels.size(), file);
    }
    if (timing) printf("%s %.2f ms/frame\n", texts[phase], total_us / frames[phase] / 1000.0);
  }
  fclose(file);
  return 0;
}
`;

function encodePng(width, height, rgb) {
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (width * 3 + 1)] = 0;
    rgb.copy(raw, y * (width * 3 + 1) + 1, y * width * 3, (y + 1) * width * 3);
  }
  const chunk = (type, data) => {
    const length = Buffer.alloc(4); length.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32(body) >>> 0);
    return Buffer.concat([length, body, crc]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4);
  header[8] = 8; header[9] = 2; header[10] = 0; header[11] = 0; header[12] = 0;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header),
    chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

const source = path.join(out, 'assist-preview.cpp'), binary = path.join(out, 'assist-preview.exe');
fs.writeFileSync(source, harness);
const build = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-DUI_FONT_16=1', '-DUI_FONT_28=1', '-I', stubs,
  source, host.archive, '-o', binary], {encoding: 'utf8'});
if (build.status !== 0) {
  console.error(build.stdout + build.stderr);
  process.exit(1);
}
for (const style of styles) {
  const raw = path.join(out, `${style}.raw`);
  const run = spawnSync(binary, [String(STYLES.indexOf(style)), raw, args.includes('--frames') ? '1' : '0'], {encoding: 'utf8'});
  if (run.status !== 0) {
    console.error(`${style}: render failed (${run.status})\n${run.stdout}${run.stderr}`);
    process.exit(1);
  }
  const xrgb = fs.readFileSync(raw);
  PHASES.forEach((phase, k) => {
    const rgb = Buffer.alloc(W * H * 3);
    const offset = k * W * H * 4;
    for (let i = 0; i < W * H; i++) {
      rgb[i * 3] = xrgb[offset + i * 4 + 2]; rgb[i * 3 + 1] = xrgb[offset + i * 4 + 1]; rgb[i * 3 + 2] = xrgb[offset + i * 4];
    }
    fs.writeFileSync(path.join(out, `${style}-${phase}.png`), encodePng(W, H, rgb));
  });
  fs.unlinkSync(raw);
  console.log(`${style}: ${path.join(out, `${style}-{listening,thinking,answering}.png`)}${run.stdout ? '\n' + run.stdout.trimEnd() : ''}`);
}
