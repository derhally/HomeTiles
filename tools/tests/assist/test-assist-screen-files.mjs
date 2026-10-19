// Host regression for Assist screens from data (tools/assist-screen-lab):
// the Sentinel, Iris and Phosphor files load within the panel limits and run
// frames through every phase, and files that break a limit are refused with
// a message instead of loading (a downloaded file must not hurt the panel).
// Rendering is compared with the built-in styles by the lab itself
// (lab <file> --png=DIR --compare --frames).
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const lab = path.join(root, 'tools/assist-screen-lab');
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: assist screen files test needs LVGL 9.6.0 (LVGL_INCLUDE) and a host compiler');
  process.exit(0);
}

const program = String.raw`
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "engine/assist_screen.h"

// Load checks only need to know which sizes exist.
const lv_font_t* assist_screen_font(int px) {
  static lv_font_t font;
  return (px == 16 || px == 20 || px == 24 || px == 28 || px == 32 || px == 40 || px == 48) ? &font : nullptr;
}

static std::vector<char> memory(assist_screen::screenSize());
static assist_screen::Screen* screen = assist_screen::init(memory.data());
static char error[160];

static bool loadText(const std::string& text) {
  return assist_screen::load(screen, text.data(), text.size(), error, sizeof(error));
}

static void refused(const std::string& text, const char* expected) {
  if (loadText(text)) { printf("FAIL: loaded, expected '%s'\n", expected); exit(1); }
  if (!strstr(error, expected)) { printf("FAIL: '%s', expected '%s'\n", error, expected); exit(1); }
}

static std::string file(const std::string& items, const std::string& extra = "") {
  return "{\"format\": 1, \"id\": \"t\"" + extra + ", \"items\": [" + items + "]}";
}

int main(int argc, char** argv) {
  for (int a = 1; a < argc; ++a) {
    FILE* f = fopen(argv[a], "rb");
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    if (!loadText(text)) { printf("FAIL: %s: %s\n", argv[a], error); return 1; }
    const assist_screen::Info& in = assist_screen::info(screen);
    if (in.frame_cmds > assist_screen::kMaxFrameCmds || in.frame_ops > assist_screen::kMaxFrameOps) return 1;
    assist_screen::reset(screen, 7);
    // Listening, thinking, answering at 1024x600 with a moving voice level.
    uint32_t most = 0;
    for (int i = 0; i < 400; ++i) {
      const int phase = i / 134;
      assist_screen::Inputs inputs{i / 30.0f, 1 / 30.0f, 0.5f + 0.5f * sinf(i * 0.3f), phase ? 1.0f : 0.0f, phase == 2,
                                   102.0f * fminf(1.0f, i / 10.0f), 512, 270, 1024, 600};
      assist_screen::frame(screen, inputs);
      const uint32_t cmds = assist_screen::commandCount(screen);
      if (cmds > in.frame_cmds) { printf("FAIL: %s drew %u shapes, worst case %u\n", in.id, cmds, in.frame_cmds); return 1; }
      if (cmds > most) most = cmds;
      lv_area_t areas[8];
      const int dirty = assist_screen::dirty(screen, areas, 8);
      if (dirty < 0 || dirty > 8) return 1;
      if (i == 0 && dirty == 0) { printf("FAIL: %s: first frame not drawn\n", in.id); return 1; }
    }
    if (most == 0) { printf("FAIL: %s drew nothing\n", in.id); return 1; }
    printf("%s %u %u %u\n", in.id, in.frame_cmds, in.frame_ops, most);
  }

  // Limits a file cannot pass.
  refused(std::string(assist_screen::kMaxFileBytes + 1, ' '), "limit");
  refused(file("{\"type\": \"line\", \"x1\": 0, \"y1\": 0, \"x2\": 1, \"y2\": 1, \"colour\": 1}"), "colour");
  refused(file("{\"type\": \"line\", \"repeat\": 300, \"x1\": 0, \"y1\": 0, \"x2\": 1, \"y2\": 1}"), "repeat");
  refused(file("{\"type\": \"group\", \"repeat\": 200, \"items\": [{\"type\": \"line\", \"repeat\": 200, "
               "\"x1\": 0, \"y1\": 0, \"x2\": 1, \"y2\": 1}]}"), "limit");
  refused(file("{\"type\": \"line\", \"x1\": \"nope\", \"y1\": 0, \"x2\": 1, \"y2\": 1}"), "nope");
  refused(file("{\"type\": \"text\", \"x\": 0, \"y\": 0, \"font\": 15, \"text\": \"a\"}"), "font");
  refused(file("{\"type\": \"line\", \"x1\": \"sin(\", \"y1\": 0, \"x2\": 1, \"y2\": 1}"), "line 1");
  refused("{\"format\": 1, \"id\": \"t\", \"items\": [[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]}", "");
  // Bad numbers draw nothing harmful: NaN and infinity become 0, coordinates
  // are clamped.
  if (!loadText(file("{\"type\": \"line\", \"x1\": \"0 / 0\", \"y1\": \"1 / 0\", \"x2\": \"1e30\", \"y2\": \"-1e30\", \"width\": 4}"))) {
    printf("FAIL: %s\n", error);
    return 1;
  }
  assist_screen::reset(screen, 1);
  assist_screen::frame(screen, assist_screen::Inputs{0, 0, 0, 0, false, 100, 0, 0, 100, 100});
  lv_area_t areas[8];
  const int n = assist_screen::dirty(screen, areas, 8);
  if (n != 1 || areas[0].x1 < -40000 || areas[0].x2 > 40000 || areas[0].y1 < -40000 || areas[0].y2 > 40000) {
    printf("FAIL: clamping\n");
    return 1;
  }
  puts("limits ok");
  return 0;
}
`;

const out = path.join(root, 'build/tests/assist-screen-files');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, 'test');
fs.writeFileSync(source, program);
const build = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', '-I', lab, source,
  path.join(lab, 'engine/assist_screen.cpp'), host.archive, '-lm', '-o', binary], {encoding: 'utf8'});
assert.equal(build.status, 0, build.stdout + build.stderr);
const screens = ['sentinel', 'iris', 'phosphor'].map(id => path.join(lab, 'screens', `${id}.json`));
const run = spawnSync(binary, screens, {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const lines = run.stdout.trim().split('\n');
assert.equal(lines.at(-1), 'limits ok');
for (const id of ['sentinel', 'iris', 'phosphor']) {
  assert.ok(lines.some(line => line.startsWith(`${id} `)), `${id} ran`);
}
// The files name the panel features they need like the style catalog does.
const needs = id => JSON.parse(fs.readFileSync(path.join(lab, 'screens', `${id}.json`), 'utf8')).needs ?? [];
assert.deepEqual(needs('sentinel'), ['wide']);
assert.deepEqual(needs('phosphor'), ['wide']);
assert.deepEqual(needs('iris'), []);
console.log('assist screen files ok: ' + lines.slice(0, 3).join(', '));
